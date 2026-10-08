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
 * Origin: EmberBSD; firmware-ready A733 GPU identification only.
 * Hardware facts: Orange Pi BSP 2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f.
 * No clocks, resets, supplies or power domains are changed here.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>
#include <arm/sunxi/sun60i_a733_ccu.h>

#define A733_GPU_BASE		0x01800000
#define A733_GPU_PBVNC		0x20
#define A733_GPU_ID_SIZE		(A733_GPU_PBVNC + sizeof(uint64_t))
#define A733_GPU_TOP		5
#define A733_GPU_EXPECTED	UINT64_C(0x00240038006800b7)

struct sun60i_gpu_softc {
	device_t sc_dev;
	int sc_phandle;
	bus_space_tag_t sc_bst;
	const char *sc_stage;
	uint64_t sc_bvnc;
	u_int sc_core_hz, sc_bus_hz, sc_uvol;
	bool sc_have_id, sc_probed;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-gpu" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry power_compat[] = {
	{ .compat = "allwinner,sun60i-a733-pck-600" },
	DEVICE_COMPAT_EOL
};
static const struct device_compatible_entry pmic_compat[] = {
	{ .compat = "x-powers,axp8191" },
	DEVICE_COMPAT_EOL
};

static int
sun60i_gpu_identify(struct sun60i_gpu_softc *sc)
{
	struct fdtbus_regulator *supply = NULL;
	struct clk *gpu = NULL;
	const uint32_t *pd, *clocks;
	const char *name;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t cells;
	uint64_t id;
	int len, node, error;
	bool enabled;

	sc->sc_have_id = false;
	sc->sc_stage = "binding";
	if (OF_getproplen(sc->sc_phandle, "netbsd,consumer-managed-power") != 0)
		return EINVAL;
	error = fdtbus_get_reg(sc->sc_phandle, 0, &addr, &size);
	if (error != 0)
		return error;
	if (addr != A733_GPU_BASE || size < A733_GPU_ID_SIZE)
		return EINVAL;
	pd = fdtbus_get_prop(sc->sc_phandle, "power-domains", &len);
	if (pd == NULL || len != 2 * sizeof(*pd) ||
	    be32toh(pd[1]) != A733_GPU_TOP)
		return EINVAL;
	node = fdtbus_get_phandle_from_native(be32toh(pd[0]));
	if (!of_compatible_match(node, power_compat) ||
	    of_getprop_uint32(node, "#power-domain-cells", &cells) != 0 ||
	    cells != 1)
		return EINVAL;
	clocks = fdtbus_get_prop(sc->sc_phandle, "clocks", &len);
	if (clocks == NULL || len != 2 * sizeof(*clocks) ||
	    be32toh(clocks[1]) != A733_CLK_GPU0)
		return EINVAL;
	node = fdtbus_get_phandle_from_native(be32toh(clocks[0]));
	if (of_getprop_uint32(node, "#clock-cells", &cells) != 0 || cells != 1)
		return EINVAL;
	if (OF_getproplen(sc->sc_phandle, "gpu-supply") != sizeof(uint32_t))
		return EINVAL;
	node = fdtbus_get_phandle(sc->sc_phandle, "gpu-supply");
	name = node > 0 ? fdtbus_get_string(node, "name") : NULL;
	if (name == NULL || strcmp(name, "dcdc4") != 0 ||
	    !of_compatible_match(OF_parent(OF_parent(node)), pmic_compat))
		return EINVAL;

	sc->sc_stage = "supply provider";
	supply = fdtbus_regulator_acquire(sc->sc_phandle, "gpu-supply");
	if (supply == NULL)
		return ENXIO;
	sc->sc_stage = "supply state";
	error = fdtbus_regulator_is_enabled(supply, &enabled);
	if (error != 0)
		goto out;
	if (!enabled) {
		error = EBUSY;
		goto out;
	}
	sc->sc_stage = "supply voltage";
	error = fdtbus_regulator_get_voltage(supply, &sc->sc_uvol);
	if (error != 0)
		goto out;
	sc->sc_stage = "GPU_TOP state";
	error = fdtbus_powerdomain_is_enabled_index(sc->sc_phandle, 0, &enabled);
	if (error != 0)
		goto out;
	if (!enabled) {
		error = EBUSY;
		goto out;
	}
	sc->sc_stage = "clock provider";
	gpu = fdtbus_clock_get_index(sc->sc_phandle, 0);
	if (gpu == NULL) {
		error = ENXIO;
		goto out;
	}
	sc->sc_stage = "clock/reset state";
	error = sun60i_a733_ccu_gpu_ready(gpu, &sc->sc_core_hz, &sc->sc_bus_hz);
	if (error != 0)
		goto out;
	/* These two BSP operating points use 800 mV across all listed bins. */
	sc->sc_stage = "firmware operating point";
	if (sc->sc_uvol != 800000 ||
	    (sc->sc_core_hz != 400000000 && sc->sc_core_hz != 600000000)) {
		error = EOPNOTSUPP;
		goto out;
	}

	sc->sc_stage = "register mapping";
	error = bus_space_map(sc->sc_bst, addr, A733_GPU_ID_SIZE, 0, &bsh);
	if (error != 0)
		goto out;
	sc->sc_stage = "PBVNC read";
	error = bus_space_peek_8(sc->sc_bst, bsh, A733_GPU_PBVNC, &id);
	bus_space_unmap(sc->sc_bst, bsh, A733_GPU_ID_SIZE);
	if (error != 0) {
		error = EFAULT;
		goto out;
	}
	sc->sc_bvnc = id;
	sc->sc_have_id = true;
	sc->sc_stage = "PBVNC value";
	error = id == A733_GPU_EXPECTED ? 0 : ENODEV;
out:
	if (gpu != NULL)
		clk_put(gpu);
	fdtbus_regulator_release(supply);
	return error;
}

static int
sun60i_gpu_finalize(device_t dev)
{
	struct sun60i_gpu_softc * const sc = device_private(dev);
	int error;

	/* Other finalizers may request another pass; never probe twice. */
	if (sc->sc_probed)
		return 0;
	sc->sc_probed = true;
	error = sun60i_gpu_identify(sc);
	if (sc->sc_have_id) {
		aprint_normal_dev(dev, "PBVNC 0x%016llx: %u.%u.%u.%u%s\n",
		    (unsigned long long)sc->sc_bvnc,
		    (u_int)(sc->sc_bvnc >> 48),
		    (u_int)((sc->sc_bvnc >> 32) & 0xffff),
		    (u_int)((sc->sc_bvnc >> 16) & 0xffff),
		    (u_int)(sc->sc_bvnc & 0xffff),
		    error == 0 ? " (expected A733 GPU)" : " (unexpected)");
	}
	if (error != 0)
		aprint_error_dev(dev, "identification unavailable at %s: %d; "
		    "firmware state left unchanged\n", sc->sc_stage, error);
	return 0;
}

static int
sun60i_gpu_match(device_t parent, cfdata_t cf, void *aux)
{
	const struct fdt_attach_args *faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_gpu_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_gpu_softc * const sc = device_private(self);
	const struct fdt_attach_args *faa = aux;
	int error;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;
	aprint_naive("\n");
	aprint_normal(": firmware-ready GPU identification only\n");
	/* The FDT root scan can attach us after our /soc parent has returned. */
	error = config_finalize_register(self, sun60i_gpu_finalize);
	if (error != 0)
		aprint_error_dev(self, "cannot register identification hook: %d\n",
		    error);
}

CFATTACH_DECL_NEW(sun60i_a733_gpu, sizeof(struct sun60i_gpu_softc),
    sun60i_gpu_match, sun60i_gpu_attach, NULL, NULL);
