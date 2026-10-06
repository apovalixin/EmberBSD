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
 * Clock controller of the always-on domain of the Allwinner A100 and A133.
 */

#include <sys/cdefs.h>

__KERNEL_RCSID(1, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_ccu.h>
#include <arm/sunxi/sun50i_a100_r_ccu.h>

#define	CPUS_CFG_REG		0x00
#define	APB1_CFG_REG		0x0c
#define	APB2_CFG_REG		0x10

static int sun50i_a100_r_ccu_match(device_t, cfdata_t, void *);
static void sun50i_a100_r_ccu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun50i-a100-r-ccu" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(sunxi_a100_r_ccu, sizeof(struct sunxi_ccu_softc),
	sun50i_a100_r_ccu_match, sun50i_a100_r_ccu_attach, NULL, NULL);

static struct sunxi_ccu_reset sun50i_a100_r_ccu_resets[] = {
	SUNXI_CCU_RESET(A100_R_RST_APB1_TIMER, 0x11c, 16),
	SUNXI_CCU_RESET(A100_R_RST_APB1_PWM, 0x13c, 16),
	SUNXI_CCU_RESET(A100_R_RST_APB1_PPU, 0x17c, 16),
	SUNXI_CCU_RESET(A100_R_RST_APB2_UART, 0x18c, 16),
	SUNXI_CCU_RESET(A100_R_RST_APB2_I2C0, 0x19c, 16),
	SUNXI_CCU_RESET(A100_R_RST_APB2_I2C1, 0x19c, 17),
	SUNXI_CCU_RESET(A100_R_RST_APB1_IR, 0x1cc, 16),
	SUNXI_CCU_RESET(A100_R_RST_AHB_RTC, 0x20c, 16),
};

static const char *cpus_parents[] = { "hosc", "losc", "iosc", "pll-periph" };
static const char *apb1_parents[] = { "r-ahb" };

static struct sunxi_ccu_clk sun50i_a100_r_ccu_clks[] = {
	SUNXI_CCU_PREDIV(A100_R_CLK_CPUS, "r-cpus", cpus_parents,
	    CPUS_CFG_REG,	/* reg */
	    __BITS(4,0),	/* prediv */
	    __BIT(3),		/* prediv_sel */
	    __BITS(9,8),	/* div */
	    __BITS(25,24),	/* sel */
	    SUNXI_CCU_PREDIV_POWER_OF_TWO),

	SUNXI_CCU_FIXED_FACTOR(A100_R_CLK_AHB, "r-ahb", "r-cpus", 1, 1),

	SUNXI_CCU_DIV(A100_R_CLK_APB1, "r-apb1", apb1_parents,
	    APB1_CFG_REG,	/* reg */
	    __BITS(1,0),	/* div */
	    0,			/* sel */
	    SUNXI_CCU_DIV_POWER_OF_TWO),

	SUNXI_CCU_PREDIV(A100_R_CLK_APB2, "r-apb2", cpus_parents,
	    APB2_CFG_REG,	/* reg */
	    __BITS(4,0),	/* prediv */
	    __BIT(3),		/* prediv_sel */
	    __BITS(9,8),	/* div */
	    __BITS(25,24),	/* sel */
	    SUNXI_CCU_PREDIV_POWER_OF_TWO),

	SUNXI_CCU_GATE(A100_R_CLK_APB1_TIMER, "r-apb1-timer", "r-apb1",
	    0x11c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB1_TWD, "r-apb1-twd", "r-apb1", 0x12c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB1_BUS_PWM, "r-apb1-pwm", "r-apb1",
	    0x13c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB1_PPU, "r-apb1-ppu", "r-apb1", 0x17c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB2_UART, "r-apb2-uart", "r-apb2",
	    0x18c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB2_I2C0, "r-apb2-i2c0", "r-apb2",
	    0x19c, 0),
	SUNXI_CCU_GATE(A100_R_CLK_APB2_I2C1, "r-apb2-i2c1", "r-apb2",
	    0x19c, 1),
	SUNXI_CCU_GATE(A100_R_CLK_APB1_BUS_IR, "r-apb1-ir", "r-apb1",
	    0x1cc, 0),
	SUNXI_CCU_GATE(A100_R_CLK_AHB_RTC, "r-ahb-rtc", "r-ahb", 0x20c, 0),
};

static int
sun50i_a100_r_ccu_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun50i_a100_r_ccu_attach(device_t parent, device_t self, void *aux)
{
	struct sunxi_ccu_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;

	sc->sc_resets = sun50i_a100_r_ccu_resets;
	sc->sc_nresets = __arraycount(sun50i_a100_r_ccu_resets);

	sc->sc_clks = sun50i_a100_r_ccu_clks;
	sc->sc_nclks = __arraycount(sun50i_a100_r_ccu_clks);

	if (sunxi_ccu_attach(sc) != 0)
		return;

	aprint_naive("\n");
	aprint_normal(": A100 PRCM CCU\n");

	sunxi_ccu_print(sc);
}
