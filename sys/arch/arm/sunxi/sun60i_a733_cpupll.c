/*-
 * Copyright (c) 2026 Anton and oxtorg contributors
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * Allwinner A733 processor clocks.
 *
 * Each of the two core clusters runs from its own PLL: 24 MHz times N,
 * then divided by a power of two in the cluster's clock register. The
 * boot loader leaves the PLLs locked. This driver only moves the
 * divider, which needs no relock and no change of the supply voltage:
 * a cluster can be slowed to a half, a quarter or an eighth of what the
 * boot loader set, and brought back.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>

#define	CPUPLL_REF_HZ		24000000

/* Offsets within a cluster's block. */
#define	PLL_CTRL_REG		0x00
#define	 PLL_CTRL_N		__BITS(15,8)
#define	CPU_CLK_REG		0x1c
#define	 CPU_CLK_SRC		__BITS(26,24)
#define	 CPU_CLK_SRC_PLL	3
#define	 CPU_CLK_P		__BITS(17,16)

#define	CPUPLL_NCLUSTERS	2

struct sun60i_a733_cpupll_softc;

struct sun60i_a733_cpupll_clk {
	struct clk		base;
	bus_size_t		reg;
};

struct sun60i_a733_cpupll_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	struct clk_domain	sc_clkdom;
	struct sun60i_a733_cpupll_clk sc_clk[CPUPLL_NCLUSTERS];
};

static const struct {
	const char	*name;
	bus_size_t	reg;
} sun60i_a733_cpupll_clusters[CPUPLL_NCLUSTERS] = {
	{ "cpu-l", 0x1000 },
	{ "cpu-b", 0x2000 },
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-cpupll" },
	DEVICE_COMPAT_EOL
};

#define	RD4(sc, reg)							\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	WR4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

static u_int
sun60i_a733_cpupll_pll_rate(struct sun60i_a733_cpupll_softc *sc,
    struct sun60i_a733_cpupll_clk *clk)
{
	return CPUPLL_REF_HZ *
	    __SHIFTOUT(RD4(sc, clk->reg + PLL_CTRL_REG), PLL_CTRL_N);
}

static struct clk *
sun60i_a733_cpupll_get(void *priv, const char *name)
{
	struct sun60i_a733_cpupll_softc * const sc = priv;

	for (u_int i = 0; i < CPUPLL_NCLUSTERS; i++) {
		if (strcmp(name, sc->sc_clk[i].base.name) == 0)
			return &sc->sc_clk[i].base;
	}

	return NULL;
}

static void
sun60i_a733_cpupll_put(void *priv, struct clk *clk)
{
}

static u_int
sun60i_a733_cpupll_get_rate(void *priv, struct clk *clkp)
{
	struct sun60i_a733_cpupll_softc * const sc = priv;
	struct sun60i_a733_cpupll_clk *clk =
	    (struct sun60i_a733_cpupll_clk *)clkp;
	const uint32_t val = RD4(sc, clk->reg + CPU_CLK_REG);

	/* The other sources are for the boot loader. */
	if (__SHIFTOUT(val, CPU_CLK_SRC) != CPU_CLK_SRC_PLL)
		return 0;

	return sun60i_a733_cpupll_pll_rate(sc, clk) >>
	    __SHIFTOUT(val, CPU_CLK_P);
}

static int
sun60i_a733_cpupll_set_rate(void *priv, struct clk *clkp, u_int rate)
{
	struct sun60i_a733_cpupll_softc * const sc = priv;
	struct sun60i_a733_cpupll_clk *clk =
	    (struct sun60i_a733_cpupll_clk *)clkp;
	const u_int pll = sun60i_a733_cpupll_pll_rate(sc, clk);
	uint32_t val;
	u_int p;

	val = RD4(sc, clk->reg + CPU_CLK_REG);
	if (__SHIFTOUT(val, CPU_CLK_SRC) != CPU_CLK_SRC_PLL)
		return ENXIO;
	for (p = 0; p <= __SHIFTOUT_MASK(CPU_CLK_P); p++) {
		if (pll >> p == rate)
			break;
	}
	if (p > __SHIFTOUT_MASK(CPU_CLK_P))
		return EINVAL;

	val &= ~CPU_CLK_P;
	val |= __SHIFTIN(p, CPU_CLK_P);
	WR4(sc, clk->reg + CPU_CLK_REG, val);

	return 0;
}

static const struct clk_funcs sun60i_a733_cpupll_clk_funcs = {
	.get = sun60i_a733_cpupll_get,
	.put = sun60i_a733_cpupll_put,
	.get_rate = sun60i_a733_cpupll_get_rate,
	.set_rate = sun60i_a733_cpupll_set_rate,
};

static struct clk *
sun60i_a733_cpupll_decode(device_t dev, int cc_phandle, const void *data,
    size_t len)
{
	struct sun60i_a733_cpupll_softc * const sc = device_private(dev);

	if (len != 4)
		return NULL;
	const u_int id = be32dec(data);
	if (id >= CPUPLL_NCLUSTERS)
		return NULL;

	return &sc->sc_clk[id].base;
}

static const struct fdtbus_clock_controller_func sun60i_a733_cpupll_funcs = {
	.decode = sun60i_a733_cpupll_decode,
};

static int
sun60i_a733_cpupll_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_cpupll_attach(device_t parent, device_t self, void *aux)
{
	struct sun60i_a733_cpupll_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	sc->sc_clkdom.name = device_xname(self);
	sc->sc_clkdom.funcs = &sun60i_a733_cpupll_clk_funcs;
	sc->sc_clkdom.priv = sc;

	aprint_naive("\n");
	aprint_normal(": Processor clocks");
	for (u_int i = 0; i < CPUPLL_NCLUSTERS; i++) {
		struct sun60i_a733_cpupll_clk *clk = &sc->sc_clk[i];

		clk->base.domain = &sc->sc_clkdom;
		clk->base.name = sun60i_a733_cpupll_clusters[i].name;
		clk->reg = sun60i_a733_cpupll_clusters[i].reg;
		clk_attach(&clk->base);
		aprint_normal("%s %s %u MHz", i == 0 ? ":" : ",",
		    clk->base.name, clk_get_rate(&clk->base) / 1000000);
	}
	aprint_normal("\n");

	fdtbus_register_clock_controller(self, phandle,
	    &sun60i_a733_cpupll_funcs);
}

CFATTACH_DECL_NEW(sun60i_a733_cpupll, sizeof(struct sun60i_a733_cpupll_softc),
    sun60i_a733_cpupll_match, sun60i_a733_cpupll_attach, NULL, NULL);
