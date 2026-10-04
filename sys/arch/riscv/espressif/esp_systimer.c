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
 * ESP32-S31 system timer: the clock interrupt and the timecounter.
 *
 * The counter runs from the crystal at 16 MHz and keeps running while the
 * hart waits for an interrupt, which the timer behind the SBI calls does
 * not.  Unit 0 is the counter, target 0 a one-shot alarm that is armed
 * again on every tick.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cpu.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/timetc.h>

#include <dev/fdt/fdtvar.h>

#include <machine/machdep.h>

#define	ST_CONF			0x00
#define	 ST_CONF_UNIT0_WORK_EN		__BIT(30)
#define	 ST_CONF_TARGET0_WORK_EN	__BIT(24)
#define	ST_UNIT0_OP		0x04
#define	 ST_UNIT0_OP_UPDATE		__BIT(30)
#define	 ST_UNIT0_OP_VALID		__BIT(29)
#define	ST_TARGET0_HI		0x1c
#define	ST_TARGET0_LO		0x20
#define	ST_TARGET0_CONF		0x34
#define	 ST_TARGET0_CONF_UNIT_SEL	__BIT(31)
#define	 ST_TARGET0_CONF_PERIOD_MODE	__BIT(30)
#define	ST_UNIT0_VALUE_HI	0x40
#define	ST_UNIT0_VALUE_LO	0x44
#define	ST_COMP0_LOAD		0x50
#define	ST_INT_ENA		0x64
#define	ST_INT_CLR		0x6c
#define	 ST_INT_TARGET0			__BIT(0)
#define	ST_HI_MASK		__BITS(19, 0)

struct espsystimer_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	uint32_t		sc_freq;
	uint32_t		sc_ticks_per_hz;
	uint64_t		sc_next;
	struct timecounter	sc_tc;
};

static struct espsystimer_softc *espsystimer_sc;

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "esp,esp32s31-systimer" },
	DEVICE_COMPAT_EOL
};

#define	RD4(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	WR4(sc, reg, val)	\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

/* The snapshot registers are shared, so read with interrupts blocked. */
static uint64_t
espsystimer_count(struct espsystimer_softc *sc)
{
	const int s = splhigh();
	u_int guard = 100000;

	WR4(sc, ST_UNIT0_OP, RD4(sc, ST_UNIT0_OP) | ST_UNIT0_OP_UPDATE);
	while ((RD4(sc, ST_UNIT0_OP) & ST_UNIT0_OP_VALID) == 0 && --guard != 0)
		continue;
	const uint32_t hi = RD4(sc, ST_UNIT0_VALUE_HI) & ST_HI_MASK;
	const uint32_t lo = RD4(sc, ST_UNIT0_VALUE_LO);
	splx(s);

	return ((uint64_t)hi << 32) | lo;
}

static void
espsystimer_arm(struct espsystimer_softc *sc, uint64_t target)
{
	WR4(sc, ST_CONF, RD4(sc, ST_CONF) & ~ST_CONF_TARGET0_WORK_EN);
	WR4(sc, ST_TARGET0_HI, (target >> 32) & ST_HI_MASK);
	WR4(sc, ST_TARGET0_LO, (uint32_t)target);
	WR4(sc, ST_TARGET0_CONF, RD4(sc, ST_TARGET0_CONF) &
	    ~(ST_TARGET0_CONF_UNIT_SEL | ST_TARGET0_CONF_PERIOD_MODE));
	WR4(sc, ST_COMP0_LOAD, 1);
	WR4(sc, ST_CONF, RD4(sc, ST_CONF) | ST_CONF_TARGET0_WORK_EN);
}

static u_int
espsystimer_get_timecount(struct timecounter *tc)
{
	return (u_int)espsystimer_count(tc->tc_priv);
}

/* Established with a null argument, so the argument is the clock frame. */
static int
espsystimer_intr(void *arg)
{
	struct espsystimer_softc * const sc = espsystimer_sc;
	struct clockframe * const cf = arg;

	/* Disarm before clearing, or the level interrupt comes straight back. */
	WR4(sc, ST_CONF, RD4(sc, ST_CONF) & ~ST_CONF_TARGET0_WORK_EN);
	WR4(sc, ST_INT_CLR, ST_INT_TARGET0);

	const uint64_t now = espsystimer_count(sc);
	do {
		sc->sc_next += sc->sc_ticks_per_hz;
	} while (sc->sc_next <= now + 16);
	espsystimer_arm(sc, sc->sc_next);

	hardclock(cf);

	return 1;
}

static void
espsystimer_initclocks(void)
{
	struct espsystimer_softc * const sc = espsystimer_sc;

	sc->sc_ticks_per_hz = sc->sc_freq / hz;
	sc->sc_next = espsystimer_count(sc) + sc->sc_ticks_per_hz;
	WR4(sc, ST_INT_CLR, ST_INT_TARGET0);
	WR4(sc, ST_INT_ENA, RD4(sc, ST_INT_ENA) | ST_INT_TARGET0);
	espsystimer_arm(sc, sc->sc_next);
}

static int
espsystimer_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
espsystimer_attach(device_t parent, device_t self, void *aux)
{
	struct espsystimer_softc * const sc = device_private(self);
	const struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	char intrstr[128];
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}
	if (of_getprop_uint32(phandle, "clock-frequency", &sc->sc_freq) != 0)
		sc->sc_freq = 16000000;

	/* Quiet the alarm, start the counter. */
	WR4(sc, ST_CONF, (RD4(sc, ST_CONF) & ~ST_CONF_TARGET0_WORK_EN) |
	    ST_CONF_UNIT0_WORK_EN);
	WR4(sc, ST_INT_ENA, RD4(sc, ST_INT_ENA) & ~ST_INT_TARGET0);
	WR4(sc, ST_INT_CLR, ST_INT_TARGET0);

	aprint_naive("\n");
	aprint_normal(": system timer, %u Hz\n", sc->sc_freq);

	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error_dev(self, "couldn't decode interrupt\n");
		return;
	}
	espsystimer_sc = sc;
	if (fdtbus_intr_establish_xname(phandle, 0, IPL_SCHED,
	    FDT_INTR_MPSAFE, espsystimer_intr, NULL,
	    device_xname(self)) == NULL) {
		aprint_error_dev(self, "couldn't establish interrupt on %s\n",
		    intrstr);
		return;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);

	sc->sc_tc.tc_get_timecount = espsystimer_get_timecount;
	sc->sc_tc.tc_counter_mask = ~0u;
	sc->sc_tc.tc_frequency = sc->sc_freq;
	sc->sc_tc.tc_name = device_xname(self);
	sc->sc_tc.tc_quality = 200;
	sc->sc_tc.tc_priv = sc;
	tc_init(&sc->sc_tc);

	riscv_timer_register(espsystimer_initclocks);
}

CFATTACH_DECL_NEW(espsystimer, sizeof(struct espsystimer_softc),
    espsystimer_match, espsystimer_attach, NULL, NULL);
