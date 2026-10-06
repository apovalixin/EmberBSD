/*-
 * Copyright (c) 2026 Anton and EmberBSD contributors
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
 * PLLs are put into the mode in which a new N is approached as a ramp,
 * so the cores keep running from their PLL while it moves.
 *
 * Which rates a chip may use, and at what supply voltage, depends on
 * its speed grade in the EFUSE; the operating point tables in the
 * device tree carry a mask of grades per entry.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/clk/clk_backend.h>
#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_sid.h>

#define	CPUPLL_REF_HZ		24000000

/* Offsets within a cluster's block. */
#define	PLL_CTRL_REG		0x00
#define	 PLL_CTRL_ENABLE	__BIT(31)
#define	 PLL_CTRL_LDO_EN	__BIT(30)
#define	 PLL_CTRL_LOCK_EN	__BIT(29)
#define	 PLL_CTRL_LOCK		__BIT(28)
#define	 PLL_CTRL_OUTPUT	__BIT(27)
#define	 PLL_CTRL_UPDATE	__BIT(26)	/* clears itself */
#define	 PLL_CTRL_N		__BITS(15,8)
#define	PLL_PAT0_REG		0x04
#define	 PLL_PAT0_TRIANGULAR	__BITS(30,29)
#define	PLL_PAT1_REG		0x08
#define	 PLL_PAT1_RAMP_EN	__BIT(31)
#define	PLL_RAMP_REG		0x18
#define	 PLL_RAMP_ENABLE	__BIT(31)
#define	 PLL_RAMP_STEP		__BIT(8)	/* 1.125 MHz per microsecond */
#define	 PLL_RAMP_BYPASS	__BIT(0)
#define	CPU_CLK_REG		0x1c
#define	 CPU_CLK_SRC		__BITS(26,24)
#define	 CPU_CLK_SRC_PLL	3
#define	 CPU_CLK_P		__BITS(17,16)

#define	CPUPLL_NCLUSTERS	2
/* The range of N the PLL locks in, and how long to wait for it. */
#define	CPUPLL_N_MIN		28
#define	CPUPLL_N_MAX		94
#define	CPUPLL_TIMEOUT_US	70000

/* The speed grade: a byte of the EFUSE, with a spare copy that wins. */
#define	CPUPLL_GRADE_OFFSET	0x4c
#define	 CPUPLL_GRADE		__BITS(15,8)
#define	 CPUPLL_GRADE_SPARE	__BITS(23,16)

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
sun60i_a733_cpupll_wait(struct sun60i_a733_cpupll_softc *sc, bus_size_t reg,
    uint32_t mask, uint32_t want)
{
	u_int waited;

	for (waited = 0; (RD4(sc, reg) & mask) != want; waited += 10) {
		if (waited >= CPUPLL_TIMEOUT_US)
			return ETIMEDOUT;
		delay(10);
	}

	return 0;
}

/*
 * Write the PLL's control register and tell the PLL to take the new
 * settings over. Until it has, the register reads back the old ones.
 */
static int
sun60i_a733_cpupll_update(struct sun60i_a733_cpupll_softc *sc,
    struct sun60i_a733_cpupll_clk *clk, uint32_t val)
{
	const bus_size_t reg = clk->reg + PLL_CTRL_REG;

	WR4(sc, reg, val);
	WR4(sc, reg, val | PLL_CTRL_UPDATE);

	return sun60i_a733_cpupll_wait(sc, reg, PLL_CTRL_UPDATE, 0);
}

static void
sun60i_a733_cpupll_set_p(struct sun60i_a733_cpupll_softc *sc,
    struct sun60i_a733_cpupll_clk *clk, u_int p)
{
	uint32_t val;

	val = RD4(sc, clk->reg + CPU_CLK_REG);
	val &= ~CPU_CLK_P;
	val |= __SHIFTIN(p, CPU_CLK_P);
	WR4(sc, clk->reg + CPU_CLK_REG, val);
}

static int
sun60i_a733_cpupll_set_rate(void *priv, struct clk *clkp, u_int rate)
{
	struct sun60i_a733_cpupll_softc * const sc = priv;
	struct sun60i_a733_cpupll_clk *clk =
	    (struct sun60i_a733_cpupll_clk *)clkp;
	uint32_t val;
	u_int n, p, old_p;
	int error;

	val = RD4(sc, clk->reg + CPU_CLK_REG);
	if (__SHIFTOUT(val, CPU_CLK_SRC) != CPU_CLK_SRC_PLL)
		return ENXIO;
	old_p = __SHIFTOUT(val, CPU_CLK_P);

	/* The smallest divider that keeps N in the PLL's range. */
	for (p = 0; p <= __SHIFTOUT_MASK(CPU_CLK_P); p++) {
		n = ((uint64_t)rate << p) / CPUPLL_REF_HZ;
		if (n >= CPUPLL_N_MIN && n <= CPUPLL_N_MAX &&
		    (CPUPLL_REF_HZ * n) >> p == rate)
			break;
	}
	if (p > __SHIFTOUT_MASK(CPU_CLK_P))
		return EINVAL;

	/*
	 * A larger divider goes in before the PLL moves and a smaller
	 * one after, so the cores never see more than either rate.
	 */
	if (p > old_p)
		sun60i_a733_cpupll_set_p(sc, clk, p);

	val = RD4(sc, clk->reg + PLL_CTRL_REG);
	if (__SHIFTOUT(val, PLL_CTRL_N) != n) {
		val &= ~PLL_CTRL_N;
		val |= __SHIFTIN(n, PLL_CTRL_N);
		error = sun60i_a733_cpupll_update(sc, clk, val);
		if (error == 0)
			error = sun60i_a733_cpupll_wait(sc,
			    clk->reg + PLL_CTRL_REG, PLL_CTRL_LOCK,
			    PLL_CTRL_LOCK);
		if (error != 0) {
			device_printf(sc->sc_dev, "%s: PLL did not settle\n",
			    clk->base.name);
			return error;
		}
	}

	if (p < old_p)
		sun60i_a733_cpupll_set_p(sc, clk, p);

	return 0;
}

/* Switch a running PLL to ramping between rates. */
static int
sun60i_a733_cpupll_ramp_enable(struct sun60i_a733_cpupll_softc *sc,
    struct sun60i_a733_cpupll_clk *clk)
{
	uint32_t val;
	int error;

	WR4(sc, clk->reg + PLL_PAT0_REG,
	    RD4(sc, clk->reg + PLL_PAT0_REG) | PLL_PAT0_TRIANGULAR);
	WR4(sc, clk->reg + PLL_PAT1_REG,
	    RD4(sc, clk->reg + PLL_PAT1_REG) | PLL_PAT1_RAMP_EN);
	val = RD4(sc, clk->reg + PLL_RAMP_REG);
	val &= ~PLL_RAMP_BYPASS;
	val |= PLL_RAMP_ENABLE | PLL_RAMP_STEP;
	WR4(sc, clk->reg + PLL_RAMP_REG, val);
	val = RD4(sc, clk->reg + PLL_CTRL_REG);
	if ((error = sun60i_a733_cpupll_update(sc, clk, val)) != 0)
		return error;

	val |= PLL_CTRL_ENABLE | PLL_CTRL_LDO_EN | PLL_CTRL_LOCK_EN |
	    PLL_CTRL_OUTPUT;
	if ((error = sun60i_a733_cpupll_update(sc, clk, val)) != 0)
		return error;

	return sun60i_a733_cpupll_wait(sc, clk->reg + PLL_CTRL_REG,
	    PLL_CTRL_LOCK, PLL_CTRL_LOCK);
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
		if (sun60i_a733_cpupll_ramp_enable(sc, clk) != 0)
			aprint_error(" (%s: no ramp)", clk->base.name);
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

/*
 * An operating point lists the speed grades it is for as bits of
 * "opp-supported-hw", numbered in the order of this table.
 */
static const uint8_t sun60i_a733_grades[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x14, 0x15
};

static bool
sun60i_a733_opp_supported(const int opp_table, const int opp_node)
{
	uint32_t word, mask;
	u_int grade, i;

	if (of_getprop_uint32(opp_node, "opp-supported-hw", &mask) != 0 ||
	    sunxi_sid_read(CPUPLL_GRADE_OFFSET, &word, 1) != 0)
		return false;
	grade = __SHIFTOUT(word, CPUPLL_GRADE_SPARE);
	if (grade == 0)
		grade = __SHIFTOUT(word, CPUPLL_GRADE);
	for (i = 0; i < __arraycount(sun60i_a733_grades); i++) {
		if (sun60i_a733_grades[i] == grade)
			return (mask & __BIT(i)) != 0;
	}

	return false;
}

FDT_OPP(sun60i_a733, "allwinner,sun60i-a733-operating-points",
    sun60i_a733_opp_supported);
