/* $NetBSD$ */

/*-
 * Copyright (c) 2026 EmberBSD contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Origin: EmberBSD; AI-assisted native A733 power-domain controller.
 * Register facts: Arm DEN0051E and Allwinner BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * See ember/boot/a733-power-domains.md for exact provenance and limits.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>

#define PCK600_DOMAIN_SIZE	0x1000
#define PCK600_NDOMAINS		11
#define PCK600_GPU_CORE		6
#define PCK600_PWPR		0x000
#define PCK600_PMER		0x004
#define PCK600_PWSR		0x008
#define PCK600_DCDR0		0x170
#define PCK600_DCDR1		0x174
#define PCK600_SWITCH0		0xc00
#define PCK600_SWITCH1		0xc04
#define PCK600_OFF2ON		0xc10
#define PCK600_MODE		__BITS(3, 0)
#define PCK600_ON		8
#define PCK600_OFF		0
#define PCK600_DYNAMIC		(__BIT(8) | __BIT(24))
#define PCK600_LOCK		__BIT(12)
#define PCK600_EMULATION	__BIT(0)
#define PCK600_TIMEOUT_US	10000
#define PCK600_POLL_US		10

struct sun60i_pck600_softc {
	device_t sc_dev;
	bus_space_tag_t sc_bst;
	bus_space_handle_t sc_bsh;
	kmutex_t sc_lock;
	bool sc_failed[PCK600_NDOMAINS];
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-pck-600" },
	DEVICE_COMPAT_EOL
};

static uint32_t
sun60i_pck600_read(struct sun60i_pck600_softc *sc, bus_size_t reg)
{
	return bus_space_read_4(sc->sc_bst, sc->sc_bsh, reg);
}

static int
sun60i_pck600_write(struct sun60i_pck600_softc *sc, bus_size_t reg,
    uint32_t value)
{
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, reg, value);
	bus_space_barrier(sc->sc_bst, sc->sc_bsh, reg, sizeof(value),
	    BUS_SPACE_BARRIER_WRITE | BUS_SPACE_BARRIER_READ);
	/* Read back also drains posted writes before status polling. */
	return sun60i_pck600_read(sc, reg) == value ? 0 : EIO;
}

static int
sun60i_pck600_transition(struct sun60i_pck600_softc *sc, u_int id,
    bool enable)
{
	static const struct {
		bus_size_t reg;
		uint32_t value;
	} delays[] = {
		{ PCK600_DCDR0, 0x1f1f1f },
		{ PCK600_DCDR1, 0x1f1f },
		{ PCK600_SWITCH0, 0x08080808 },
		{ PCK600_SWITCH1, 0x0808 },
		{ PCK600_OFF2ON, 0x08 },
	};
	const bus_size_t base = id * PCK600_DOMAIN_SIZE;
	const uint32_t mode = enable ? PCK600_ON : PCK600_OFF;
	uint32_t policy, status;
	int error;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (id >= PCK600_NDOMAINS)
		return EINVAL;
	/* Upstream marks GPU_CORE always-on; never request its power-off. */
	if (id == PCK600_GPU_CORE && !enable)
		return EOPNOTSUPP;
	if (sc->sc_failed[id])
		return EIO;

	policy = sun60i_pck600_read(sc, base + PCK600_PWPR);
	status = sun60i_pck600_read(sc, base + PCK600_PWSR);
	/* Do not take over firmware's dynamic, locked or emulated policy. */
	if ((policy | status) & (PCK600_DYNAMIC | PCK600_LOCK))
		return EOPNOTSUPP;
	if (sun60i_pck600_read(sc, base + PCK600_PMER) & PCK600_EMULATION)
		return EOPNOTSUPP;
	if ((policy & PCK600_MODE) != (status & PCK600_MODE))
		return EBUSY;
	if ((status & PCK600_MODE) == mode)
		return 0;

	for (u_int n = 0; n < __arraycount(delays); n++) {
		error = sun60i_pck600_write(sc, base + delays[n].reg,
		    delays[n].value);
		if (error != 0)
			goto failed;
	}
	policy = (policy & ~PCK600_MODE) | mode;
	error = sun60i_pck600_write(sc, base + PCK600_PWPR, policy);
	if (error != 0)
		goto failed;

	for (u_int elapsed = 0; ; elapsed += PCK600_POLL_US) {
		/* A denied static request can revert PWPR (Arm DEN0051E). */
		if (sun60i_pck600_read(sc, base + PCK600_PWPR) != policy) {
			error = EIO;
			goto failed;
		}
		status = sun60i_pck600_read(sc, base + PCK600_PWSR);
		if ((status & (PCK600_MODE | PCK600_DYNAMIC | PCK600_LOCK))
		    == mode)
			return 0;
		if (elapsed == PCK600_TIMEOUT_US)
			break;
		delay(PCK600_POLL_US);
	}
	error = ETIMEDOUT;
failed:
	/* An in-flight transition cannot safely be undone by another write. */
	sc->sc_failed[id] = true;
	return error;
}

static int
sun60i_pck600_set(device_t dev, const uint32_t *data, bool enable)
{
	struct sun60i_pck600_softc * const sc = device_private(dev);
	const u_int id = be32toh(data[1]);
	int error;

	mutex_enter(&sc->sc_lock);
	error = sun60i_pck600_transition(sc, id, enable);
	mutex_exit(&sc->sc_lock);
	if (error != 0)
		aprint_error_dev(dev, "domain %u power %s failed: %d\n",
		    id, enable ? "on" : "off", error);
	return error;
}

static int
sun60i_pck600_state(struct sun60i_pck600_softc *sc, u_int id, bool *enabled)
{
	const bus_size_t base = id * PCK600_DOMAIN_SIZE;
	uint32_t policy, status, emulation;

	KASSERT(mutex_owned(&sc->sc_lock));
	if (id >= PCK600_NDOMAINS)
		return EINVAL;
	if (sc->sc_failed[id])
		return EIO;

	policy = sun60i_pck600_read(sc, base + PCK600_PWPR);
	emulation = sun60i_pck600_read(sc, base + PCK600_PMER);
	status = sun60i_pck600_read(sc, base + PCK600_PWSR);
	/* Detect a changing firmware snapshot without requesting a transition. */
	if (policy != sun60i_pck600_read(sc, base + PCK600_PWPR) ||
	    emulation != sun60i_pck600_read(sc, base + PCK600_PMER) ||
	    status != sun60i_pck600_read(sc, base + PCK600_PWSR))
		return EBUSY;
	if (((policy | status) & PCK600_DYNAMIC) != 0 ||
	    (emulation & PCK600_EMULATION) != 0)
		return EOPNOTSUPP;
	if ((policy & PCK600_MODE) != (status & PCK600_MODE))
		return EBUSY;

	/* A locked static policy can be observed without changing it. */
	switch (status & PCK600_MODE) {
	case PCK600_OFF:
		*enabled = false;
		return 0;
	case PCK600_ON:
		*enabled = true;
		return 0;
	default:
		return EOPNOTSUPP;
	}
}

static int
sun60i_pck600_get(device_t dev, const uint32_t *data, bool *enabled)
{
	struct sun60i_pck600_softc * const sc = device_private(dev);
	const u_int id = be32toh(data[1]);
	int error;

	mutex_enter(&sc->sc_lock);
	error = sun60i_pck600_state(sc, id, enabled);
	mutex_exit(&sc->sc_lock);

	return error;
}

static const struct fdtbus_powerdomain_controller_func sun60i_pck600_funcs = {
	.pdc_set = sun60i_pck600_set,
	.pdc_get = sun60i_pck600_get,
};

static int
sun60i_pck600_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_pck600_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_pck600_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_addr_t addr;
	bus_size_t size;
	struct clk *clk;
	uint32_t cells;
	int error;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	if (of_getprop_uint32(phandle, "#power-domain-cells", &cells) != 0 ||
	    cells != 1 || fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    size < PCK600_NDOMAINS * PCK600_DOMAIN_SIZE) {
		aprint_error(": invalid power-domain binding\n");
		return;
	}
	clk = fdtbus_clock_get_index(phandle, 0);
	if (clk == NULL) {
		aprint_error(": missing PPU clock\n");
		return;
	}
	error = bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh);
	if (error != 0) {
		aprint_error(": cannot map registers: %d\n", error);
		return;
	}
	error = clk_enable(clk);
	if (error != 0) {
		aprint_error(": cannot enable PPU clock: %d\n", error);
		goto unmap;
	}
	/* A733 has no PPU reset. Do not reset domains left on by firmware. */
	mutex_init(&sc->sc_lock, MUTEX_DEFAULT, IPL_NONE);
	error = fdtbus_register_powerdomain_controller(self, phandle,
	    &sun60i_pck600_funcs);
	if (error != 0) {
		aprint_error(": cannot register power domains: %d\n", error);
		mutex_destroy(&sc->sc_lock);
		clk_disable(clk);
		goto unmap;
	}
	aprint_naive("\n");
	aprint_normal(": A733 PCK-600 power domains\n");
	return;
unmap:
	bus_space_unmap(sc->sc_bst, sc->sc_bsh, size);
}

CFATTACH_DECL_NEW(sun60i_a733_pck, sizeof(struct sun60i_pck600_softc),
    sun60i_pck600_match, sun60i_pck600_attach, NULL, NULL);
