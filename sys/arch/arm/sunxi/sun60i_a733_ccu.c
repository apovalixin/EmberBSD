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
 * Allwinner A733 (sun60iw2) clock control unit.
 *
 * The register map was read on an Orange Pi Zero 4 and agrees with the
 * vendor kernel. The boot loader leaves the peripheral PLL at 2400 MHz;
 * this driver reports it and never retunes it.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(1, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

#include <arm/sunxi/sunxi_ccu.h>
#include <arm/sunxi/sun60i_a733_ccu.h>

#define	PLL_PERIPH0_CTRL_REG	0x0a0
#define	AHB_CFG_REG		0x500
#define	APB0_CFG_REG		0x510
#define	APB1_CFG_REG		0x518
#define	APB_UART_CFG_REG	0x538
#define	MBUS_GATE_REG		0x5e4
#define	CE_CLK_REG		0xac0
#define	CE_BGR_REG		0xac4
#define	SMHC0_CLK_REG		0xd00
#define	SMHC0_BGR_REG		0xd0c
#define	SMHC1_CLK_REG		0xd10
#define	SMHC1_BGR_REG		0xd1c
#define	SMHC2_CLK_REG		0xd20
#define	SMHC2_BGR_REG		0xd2c
#define	UART_BGR_REG(n)		(0xe00 + 4 * (n))
#define	TWI_BGR_REG(n)		(0xe80 + 4 * (n))
#define	GPADC0_24M_CLK_REG	0xfc0
#define	THS_BGR_REG		0xfe4
#define	USB0_CLK_REG		0x1300
#define	USB0_BGR_REG		0x1304
#define	USB1_CLK_REG		0x1308
#define	USB1_BGR_REG		0x130c
#define	GMAC0_PHY_CLK_REG	0x1410
#define	GMAC0_BGR_REG		0x141c

static int sun60i_a733_ccu_match(device_t, cfdata_t, void *);
static void sun60i_a733_ccu_attach(device_t, device_t, void *);

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-ccu" },
	DEVICE_COMPAT_EOL
};

CFATTACH_DECL_NEW(sunxi_a733_ccu, sizeof(struct sunxi_ccu_softc),
	sun60i_a733_ccu_match, sun60i_a733_ccu_attach, NULL, NULL);

static struct sunxi_ccu_reset sun60i_a733_ccu_resets[] = {
	SUNXI_CCU_RESET(A733_RST_BUS_CE, CE_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_CE_SYS, CE_BGR_REG, 17),

	SUNXI_CCU_RESET(A733_RST_BUS_MMC0, SMHC0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_MMC1, SMHC1_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_MMC2, SMHC2_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_BUS_UART0, UART_BGR_REG(0), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART1, UART_BGR_REG(1), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART2, UART_BGR_REG(2), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART3, UART_BGR_REG(3), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART4, UART_BGR_REG(4), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART5, UART_BGR_REG(5), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_UART6, UART_BGR_REG(6), 16),

	SUNXI_CCU_RESET(A733_RST_BUS_I2C0, TWI_BGR_REG(0), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C1, TWI_BGR_REG(1), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C2, TWI_BGR_REG(2), 16),
	SUNXI_CCU_RESET(A733_RST_BUS_I2C3, TWI_BGR_REG(3), 16),

	SUNXI_CCU_RESET(A733_RST_BUS_THS0, THS_BGR_REG, 16),

	SUNXI_CCU_RESET(A733_RST_USB_PHY0, USB0_CLK_REG, 30),
	SUNXI_CCU_RESET(A733_RST_BUS_OHCI0, USB0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_EHCI0, USB0_BGR_REG, 20),
	SUNXI_CCU_RESET(A733_RST_BUS_OTG, USB0_BGR_REG, 24),
	SUNXI_CCU_RESET(A733_RST_USB_PHY1, USB1_CLK_REG, 30),
	SUNXI_CCU_RESET(A733_RST_BUS_OHCI1, USB1_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_EHCI1, USB1_BGR_REG, 20),

	SUNXI_CCU_RESET(A733_RST_BUS_GMAC0, GMAC0_BGR_REG, 16),
	SUNXI_CCU_RESET(A733_RST_BUS_GMAC0_AXI, GMAC0_BGR_REG, 17),
};

static const char *pll_periph0_4x_parent[] = { "pll-periph0-4x" };
static const char *pll_periph0_150M_parent[] = { "pll-periph0-150M" };
static const char *bus_parents[] = {
	"sys-24M", "losc", "iosc", "pll-periph0-600M"
};
static const char *apb_uart_parents[] = {
	"sys-24M", "losc", "iosc", "pll-periph0-600M", "pll-periph0-480M"
};
static const char *mmc_parents[] = {
	"sys-24M", "pll-periph0-400M", "pll-periph0-300M",
	"pll-periph1-400M", "pll-periph1-300M"
};
static const char *ce_parents[] = {
	"sys-24M", "pll-periph0-400M", "pll-periph0-600M"
};
static const char *gpadc_24m_parents[] = { "sys-24M", "hosc" };
static const char *emmc_parents[] = {
	"sys-24M", "pll-periph0-800M", "pll-periph0-600M",
	"pll-periph1-800M", "pll-periph1-600M"
};

/*
 * The SD/MMC controllers halve their module clock, so a card clock of
 * 50 MHz is a 100 MHz module clock.
 */
#define	A733_MMC(_id, _name, _parents, _reg)				\
	SUNXI_CCU_NM(_id, _name, _parents, _reg,			\
	    __BITS(12,8),	/* n */					\
	    __BITS(4,0),	/* m */					\
	    __BITS(26,24),	/* sel */				\
	    __BIT(31),		/* enable */				\
	    SUNXI_CCU_NM_ROUND_DOWN | SUNXI_CCU_NM_DIVIDE_BY_TWO)

static struct sunxi_ccu_clk sun60i_a733_ccu_clks[] = {
	/*
	 * PLL_REF turns a 19.2, 24 or 26 MHz crystal into 24 MHz. Boards
	 * with a 24 MHz crystal run it one to one, which is all that is
	 * described here.
	 */
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_REF, "pll-ref", "hosc", 1, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_SYS_24M, "sys-24M", "pll-ref", 1, 1),

	SUNXI_CCU_NKMP(A733_CLK_PLL_PERIPH0_4X, "pll-periph0-4x", "pll-ref",
	    PLL_PERIPH0_CTRL_REG,	/* reg */
	    __BITS(15,8),		/* n */
	    0,				/* k */
	    0,				/* m */
	    0,				/* p */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_2X, "pll-periph0-2x",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(22,20),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_800M, "pll-periph0-800M",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(18,16),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_PLL_PERIPH0_480M, "pll-periph0-480M",
	    pll_periph0_4x_parent, PLL_PERIPH0_CTRL_REG,
	    __BITS(4,2),		/* div */
	    0,				/* sel */
	    0),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_600M, "pll-periph0-600M",
	    "pll-periph0-2x", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_400M, "pll-periph0-400M",
	    "pll-periph0-2x", 3, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_300M, "pll-periph0-300M",
	    "pll-periph0-600M", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_200M, "pll-periph0-200M",
	    "pll-periph0-400M", 2, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_160M, "pll-periph0-160M",
	    "pll-periph0-480M", 3, 1),
	SUNXI_CCU_FIXED_FACTOR(A733_CLK_PLL_PERIPH0_150M, "pll-periph0-150M",
	    "pll-periph0-300M", 2, 1),

	SUNXI_CCU_DIV(A733_CLK_AHB, "ahb", bus_parents, AHB_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB0, "apb0", bus_parents, APB0_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB1, "apb1", bus_parents, APB1_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(25,24),		/* sel */
	    0),
	SUNXI_CCU_DIV(A733_CLK_APB_UART, "apb-uart", apb_uart_parents,
	    APB_UART_CFG_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    0),

	SUNXI_CCU_GATE(A733_CLK_MBUS_GMAC0, "mbus-gmac0", "ahb",
	    MBUS_GATE_REG, 11),

	SUNXI_CCU_GATE(A733_CLK_MBUS_CE, "mbus-ce", "ahb",
	    MBUS_GATE_REG, 2),
	SUNXI_CCU_DIV_GATE(A733_CLK_CE, "ce", ce_parents, CE_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_CE, "bus-ce", "ahb",
	    CE_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_CE_SYS, "bus-ce-sys", "ahb",
	    CE_BGR_REG, 1),

	A733_MMC(A733_CLK_MMC0, "mmc0", mmc_parents, SMHC0_CLK_REG),
	A733_MMC(A733_CLK_MMC1, "mmc1", mmc_parents, SMHC1_CLK_REG),
	A733_MMC(A733_CLK_MMC2, "mmc2", emmc_parents, SMHC2_CLK_REG),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC0, "bus-mmc0", "ahb",
	    SMHC0_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC1, "bus-mmc1", "ahb",
	    SMHC1_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_MMC2, "bus-mmc2", "ahb",
	    SMHC2_BGR_REG, 0),

	SUNXI_CCU_GATE(A733_CLK_BUS_UART0, "bus-uart0", "apb-uart",
	    UART_BGR_REG(0), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART1, "bus-uart1", "apb-uart",
	    UART_BGR_REG(1), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART2, "bus-uart2", "apb-uart",
	    UART_BGR_REG(2), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART3, "bus-uart3", "apb-uart",
	    UART_BGR_REG(3), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART4, "bus-uart4", "apb-uart",
	    UART_BGR_REG(4), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART5, "bus-uart5", "apb-uart",
	    UART_BGR_REG(5), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_UART6, "bus-uart6", "apb-uart",
	    UART_BGR_REG(6), 0),

	SUNXI_CCU_GATE(A733_CLK_BUS_I2C0, "bus-i2c0", "apb1",
	    TWI_BGR_REG(0), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C1, "bus-i2c1", "apb1",
	    TWI_BGR_REG(1), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C2, "bus-i2c2", "apb1",
	    TWI_BGR_REG(2), 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_I2C3, "bus-i2c3", "apb1",
	    TWI_BGR_REG(3), 0),

	/* The thermal sensors convert on this clock. */
	SUNXI_CCU_DIV_GATE(A733_CLK_GPADC0_24M, "gpadc0-24m",
	    gpadc_24m_parents, GPADC0_24M_CLK_REG,
	    __BITS(4,0),		/* div */
	    __BITS(26,24),		/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_THS0, "bus-ths0", "apb0",
	    THS_BGR_REG, 0),

	/* The 24 MHz reference of the two USB 2.0 ports and their buses. */
	SUNXI_CCU_GATE(A733_CLK_USB_OHCI0, "usb-ohci0", "hosc",
	    USB0_CLK_REG, 31),
	SUNXI_CCU_GATE(A733_CLK_BUS_OHCI0, "bus-ohci0", "ahb",
	    USB0_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_EHCI0, "bus-ehci0", "ahb",
	    USB0_BGR_REG, 4),
	SUNXI_CCU_GATE(A733_CLK_BUS_OTG, "bus-otg", "ahb",
	    USB0_BGR_REG, 8),
	SUNXI_CCU_GATE(A733_CLK_USB_OHCI1, "usb-ohci1", "hosc",
	    USB1_CLK_REG, 31),
	SUNXI_CCU_GATE(A733_CLK_BUS_OHCI1, "bus-ohci1", "ahb",
	    USB1_BGR_REG, 0),
	SUNXI_CCU_GATE(A733_CLK_BUS_EHCI1, "bus-ehci1", "ahb",
	    USB1_BGR_REG, 4),

	SUNXI_CCU_DIV_GATE(A733_CLK_GMAC0_PHY, "gmac0-phy",
	    pll_periph0_150M_parent, GMAC0_PHY_CLK_REG,
	    __BITS(4,0),		/* div */
	    0,				/* sel */
	    __BIT(31),			/* enable */
	    0),
	SUNXI_CCU_GATE(A733_CLK_BUS_GMAC0, "bus-gmac0", "ahb",
	    GMAC0_BGR_REG, 0),
};

static int
sun60i_a733_ccu_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
sun60i_a733_ccu_attach(device_t parent, device_t self, void *aux)
{
	struct sunxi_ccu_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;

	sc->sc_dev = self;
	sc->sc_phandle = faa->faa_phandle;
	sc->sc_bst = faa->faa_bst;

	sc->sc_resets = sun60i_a733_ccu_resets;
	sc->sc_nresets = __arraycount(sun60i_a733_ccu_resets);

	sc->sc_clks = sun60i_a733_ccu_clks;
	sc->sc_nclks = __arraycount(sun60i_a733_ccu_clks);

	if (sunxi_ccu_attach(sc) != 0)
		return;

	aprint_naive("\n");
	aprint_normal(": A733 CCU\n");

	sunxi_ccu_print(sc);
}
