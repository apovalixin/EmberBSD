/*-
 * Copyright (c) 2026 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * ESP32-S31 interrupt routing: the interrupt matrix and the CLIC.
 *
 * Every peripheral drives a fixed source line into the matrix; a register
 * per source names the CLIC slot it is routed to.  The firmware delegates
 * slots 16-47 to supervisor mode.  A slot reaches the kernel as an
 * interrupt whose code is the slot number, so the local interrupt
 * controller dispatches it; this driver hands out slots, programs the
 * route and masks a slot through its CLIC enable byte.
 *
 * The device tree gives the matrix and the CLIC a node each.  The driver
 * attaches to the matrix, which the peripherals name as their interrupt
 * parent, and maps the CLIC bank itself.
 */

#include "opt_riscv_clic.h"

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <machine/machdep.h>

#include <riscv/espressif/esp_rom.h>

/* One 32-bit register per source; the low six bits are the CLIC slot. */
#define	INTMTX_SOURCE(src)	((src) * 4)
#define	INTMTX_NSOURCES		128

/* CLIC supervisor bank: four bytes per slot. */
#define	CLIC_INTIP(slot)	(0x1000 + (slot) * 4 + 0)
#define	CLIC_INTIE(slot)	(0x1000 + (slot) * 4 + 1)
#define	CLIC_INTATTR(slot)	(0x1000 + (slot) * 4 + 2)
#define	CLIC_INTCTL(slot)	(0x1000 + (slot) * 4 + 3)
#define	 CLIC_ATTR_TRIG		__BITS(2, 1)	/* zero: level */
#define	 CLIC_CTL_MAX		0xe0

/* The kernel takes interrupt codes up to 31. */
#define	CLIC_SLOT_FIRST		16
#define	CLIC_SLOT_LAST		31

struct espintmtx_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_mtx_bsh;
	bus_space_handle_t	sc_clic_bsh;
	uint32_t		sc_slots;	/* slots in use */
	uint8_t			sc_slot[INTMTX_NSOURCES];
};

static struct espintmtx_softc *espintmtx_sc;

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "esp,esp32s31-intmtx" },
	DEVICE_COMPAT_EOL
};

static void
espintmtx_mask(u_int slot, bool masked)
{
	struct espintmtx_softc * const sc = espintmtx_sc;

	bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh, CLIC_INTIE(slot),
	    masked ? 0 : 1);
}

static void *
espintmtx_establish(device_t dev, u_int *specifier, int ipl, int flags,
    int (*func)(void *), void *arg, const char *xname)
{
	struct espintmtx_softc * const sc = device_private(dev);

	/* 1st cell is the source, 2nd the trigger; all sources are level. */
	const u_int src = be32toh(specifier[0]);
	if (src >= INTMTX_NSOURCES) {
		aprint_error_dev(dev, "source %u out of range\n", src);
		return NULL;
	}

	u_int slot = sc->sc_slot[src];
	if (slot == 0) {
		for (slot = CLIC_SLOT_FIRST; slot <= CLIC_SLOT_LAST; slot++) {
			if ((sc->sc_slots & __BIT(slot)) == 0)
				break;
		}
		if (slot > CLIC_SLOT_LAST) {
			aprint_error_dev(dev, "no free CLIC slot\n");
			return NULL;
		}
		sc->sc_slots |= __BIT(slot);
		sc->sc_slot[src] = slot;

		/* Leave the slot masked until the handler is in place. */
		bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh,
		    CLIC_INTIE(slot), 0);
		bus_space_write_4(sc->sc_bst, sc->sc_mtx_bsh,
		    INTMTX_SOURCE(src), slot);
		const uint8_t attr = bus_space_read_1(sc->sc_bst,
		    sc->sc_clic_bsh, CLIC_INTATTR(slot));
		bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh,
		    CLIC_INTATTR(slot), attr & ~CLIC_ATTR_TRIG);
		bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh,
		    CLIC_INTCTL(slot), CLIC_CTL_MAX);
		bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh,
		    CLIC_INTIP(slot), 0);
	}

	return riscv_intc_establish_source(slot, ipl, flags, func, arg,
	    xname, espintmtx_mask);
}

static void
espintmtx_disestablish(device_t dev, void *ih)
{
	/* Handlers stay for the life of the system. */
}

static bool
espintmtx_intrstr(device_t dev, u_int *specifier, char *buf, size_t buflen)
{
	snprintf(buf, buflen, "%s source %u", device_xname(dev),
	    be32toh(specifier[0]));
	return true;
}

static struct fdtbus_interrupt_controller_func espintmtx_funcs = {
	.establish = espintmtx_establish,
	.disestablish = espintmtx_disestablish,
	.intrstr = espintmtx_intrstr,
};

static int
espintmtx_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
espintmtx_attach(device_t parent, device_t self, void *aux)
{
	struct espintmtx_softc * const sc = device_private(self);
	const struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_mtx_bsh) != 0) {
		aprint_error(": couldn't map the matrix\n");
		return;
	}

	const int clic = of_find_bycompat(OF_finddevice("/"),
	    "esp,esp32s31-clic");
	if (clic <= 0 || fdtbus_get_reg(clic, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_clic_bsh) != 0) {
		aprint_error(": couldn't map the CLIC\n");
		return;
	}

	/* Drop whatever routes the boot loaders left behind. */
	for (u_int slot = CLIC_SLOT_FIRST; slot <= CLIC_SLOT_LAST; slot++) {
		bus_space_write_1(sc->sc_bst, sc->sc_clic_bsh,
		    CLIC_INTIE(slot), 0);
	}
	for (u_int src = 0; src < INTMTX_NSOURCES; src++) {
		bus_space_write_4(sc->sc_bst, sc->sc_mtx_bsh,
		    INTMTX_SOURCE(src), 0);
	}

	espintmtx_sc = sc;

	const int error = fdtbus_register_interrupt_controller(self, phandle,
	    &espintmtx_funcs);
	if (error != 0) {
		aprint_error(": couldn't register with fdtbus: %d\n", error);
		return;
	}

	aprint_naive("\n");
	aprint_normal(": interrupt matrix, CLIC slots %u-%u\n",
	    CLIC_SLOT_FIRST, CLIC_SLOT_LAST);

	esp_rom_init();
}

CFATTACH_DECL_NEW(espintmtx, sizeof(struct espintmtx_softc),
    espintmtx_match, espintmtx_attach, NULL, NULL);
