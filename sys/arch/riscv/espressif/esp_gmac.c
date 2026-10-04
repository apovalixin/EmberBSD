/*	$NetBSD$	*/

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
 * Glue for the DesignWare GMAC of the Espressif ESP32-S31.
 *
 * The boot loader leaves the controller untouched and the port has no
 * clock, reset, pin or GPIO drivers, so everything the controller needs
 * is set up here: clock gates and the RGMII reference clock, the block
 * reset, the pad multiplexers and the reset line of the PHY.  Register
 * layouts follow the Linux drivers published by Espressif.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/intr.h>
#include <sys/rndsource.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <dev/mii/miivar.h>

#include <dev/ic/dwc_gmac_var.h>
#include <dev/ic/dwc_gmac_reg.h>

#include <dev/fdt/fdtvar.h>

/* Blocks outside the controller, at fixed addresses on this chip. */
#define	S31_IOMUX_BASE		0x20582000
#define	S31_IOMUX_SIZE		0x100
#define	S31_GPIO_BASE		0x20583000
#define	S31_GPIO_SIZE		0xe00
#define	S31_CLKRST_BASE		0x20587000
#define	S31_CLKRST_SIZE		0x400
#define	S31_CNNT_PAD_BASE	0x20588000
#define	S31_CNNT_PAD_SIZE	0x400
#define	S31_CNNT_SYS_BASE	0x20359000
#define	S31_CNNT_SYS_SIZE	0x100

/* IO_MUX: one word per pad. */
#define	IOMUX_PAD(pin)		((pin) * 4)
#define	 IOMUX_MODE		__BITS(14,12)
#define	 IOMUX_MODE_GPIO	1
#define	 IOMUX_MODE_GMAC	2
#define	 IOMUX_DRV		__BITS(11,10)
#define	 IOMUX_IE		__BIT(9)
#define	 IOMUX_PU		__BIT(8)
#define	 IOMUX_PD		__BIT(7)
#define	PAD_DRV_20MA		2

/* GPIO block: output latches and the signal matrix. */
#define	GPIO_OUT_SET		0x08
#define	GPIO_OUT_CLR		0x0c
#define	GPIO_ENABLE_SET		0x38
#define	GPIO_MATRIX_IN(sig)	(0x2f4 + (sig) * 4)
#define	 GPIO_MATRIX_IN_SEL	__BIT(9)
#define	GPIO_MATRIX_OUT(pin)	(0xaf4 + (pin) * 4)
#define	SIG_GMII_MDI_IN		107
#define	SIG_GMII_MDC_OUT	108
#define	SIG_GMII_MDO_OUT	109
#define	SIG_GPIO_OUT		256

/* Dedicated pads of the connectivity subsystem: GPIO13..GPIO19 for GMAC. */
#define	CNNT_PAD(pin)		(((pin) - 13) * 4)
#define	 CNNT_PAD_DRV		__BITS(10,9)
#define	 CNNT_PAD_IE		__BIT(8)
#define	 CNNT_PAD_PU		__BIT(7)
#define	 CNNT_PAD_PD		__BIT(6)
#define	CNNT_PAD_CTRL		0x3f4
#define	 CNNT_PAD_CTRL_GMAC_DED	__BIT(1)

/* Clock and reset controller. */
#define	CLKRST_EMAC_CTRL0	0x0c8
#define	 EMAC_SYS_CLK_EN	__BIT(0)

/* Connectivity system controller. */
#define	CNNT_EMAC_CTRL		0x03c
#define	 EMAC_RST_EN		__BIT(1)
#define	CNNT_EMAC_REF_CTRL	0x040
#define	 EMAC_REF_CLK_SEL	__BITS(1,0)
#define	 EMAC_REF_CLK_EN	__BIT(2)
#define	 EMAC_REF_CLK_DIV	__BITS(15,8)
#define	CNNT_EMAC_RMII_PAD_CTRL	0x044
#define	 EMAC_RMII_PAD_CLK_EN	__BIT(1)
#define	 EMAC_RMII_PAD_CLK_INV	__BIT(2)
#define	CNNT_EMAC_RMII_CTRL	0x048
#define	 EMAC_RMII_CLK_SEL	__BIT(0)
#define	 EMAC_RMII_CLK_EN	__BIT(1)
#define	 EMAC_RMII_PAD_OUT_CLK	__BIT(2)
#define	CNNT_EMAC_RX_CTRL	0x04c
#define	 EMAC_RX_PAD_CLK_EN	__BIT(0)
#define	 EMAC_RX_PAD_CLK_INV	__BIT(1)
#define	 EMAC_RX_CLK_SEL	__BIT(2)
#define	 EMAC_RX_180_CLK_EN	__BIT(3)
#define	CNNT_EMAC_TX_CTRL	0x050
#define	 EMAC_TX_PAD_CLK_EN	__BIT(0)
#define	 EMAC_TX_CLK_SEL	__BIT(2)
#define	 EMAC_TX_180_CLK_EN	__BIT(3)
#define	CNNT_EMAC_PTP_CTRL	0x054
#define	 EMAC_PTP_REF_CLK_EN	__BIT(0)
#define	CNNT_GMAC_CTRL0		0x060
#define	 GMAC_PHY_INTF_SEL	__BITS(4,2)
#define	 GMAC_PHY_INTF_RGMII	1

#define	S31_MPLL_HZ		500000000

/* Board wiring, as in the device tree of the vendor. */
#define	PIN_MDC			5
#define	PIN_MDIO		6
#define	PIN_PHY_RESET		7
#define	PIN_TX_FIRST		8
#define	PIN_TX_LAST		12
#define	PIN_DED_FIRST		13
#define	PIN_DED_LAST		19

struct esp_gmac_softc {
	struct dwc_gmac_softc	sc_core;
	bus_space_handle_t	sc_iomux;
	bus_space_handle_t	sc_gpio;
	bus_space_handle_t	sc_clkrst;
	bus_space_handle_t	sc_cnntpad;
	bus_space_handle_t	sc_cnntsys;
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "esp,esp32s31-dwmac" },
	DEVICE_COMPAT_EOL
};

static inline void
esp_gmac_update(struct esp_gmac_softc *esc, bus_space_handle_t bsh,
    bus_size_t reg, uint32_t clr, uint32_t set)
{
	bus_space_tag_t bst = esc->sc_core.sc_bst;

	bus_space_write_4(bst, bsh, reg,
	    (bus_space_read_4(bst, bsh, reg) & ~clr) | set);
}

/* The RGMII transmit clock: 125, 25 or 2.5 MHz out of the 500 MHz PLL. */
static void
esp_gmac_ref_clock(struct esp_gmac_softc *esc, u_int rate)
{
	const u_int div = S31_MPLL_HZ / rate;

	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_REF_CTRL,
	    EMAC_REF_CLK_SEL | EMAC_REF_CLK_DIV,
	    __SHIFTIN(div - 1, EMAC_REF_CLK_DIV) | EMAC_REF_CLK_EN);
}

static void
esp_gmac_set_speed(struct dwc_gmac_softc *sc, int speed)
{
	struct esp_gmac_softc * const esc = (struct esp_gmac_softc *)sc;

	switch (speed) {
	case IFM_1000_T:
		esp_gmac_ref_clock(esc, 125000000);
		break;
	case IFM_100_TX:
		esp_gmac_ref_clock(esc, 25000000);
		break;
	case IFM_10_T:
		esp_gmac_ref_clock(esc, 2500000);
		break;
	}
}

static void
esp_gmac_pins(struct esp_gmac_softc *esc)
{
	bus_space_tag_t bst = esc->sc_core.sc_bst;
	const uint32_t pad = IOMUX_DRV | IOMUX_IE | IOMUX_PU | IOMUX_PD |
	    IOMUX_MODE;

	/* Management clock and data go through the signal matrix. */
	esp_gmac_update(esc, esc->sc_iomux, IOMUX_PAD(PIN_MDC), pad,
	    __SHIFTIN(IOMUX_MODE_GPIO, IOMUX_MODE) |
	    __SHIFTIN(PAD_DRV_20MA, IOMUX_DRV) | IOMUX_IE);
	bus_space_write_4(bst, esc->sc_gpio, GPIO_MATRIX_OUT(PIN_MDC),
	    SIG_GMII_MDC_OUT);
	esp_gmac_update(esc, esc->sc_iomux, IOMUX_PAD(PIN_MDIO), pad,
	    __SHIFTIN(IOMUX_MODE_GPIO, IOMUX_MODE) |
	    __SHIFTIN(PAD_DRV_20MA, IOMUX_DRV) | IOMUX_IE);
	bus_space_write_4(bst, esc->sc_gpio, GPIO_MATRIX_IN(SIG_GMII_MDI_IN),
	    PIN_MDIO | GPIO_MATRIX_IN_SEL);
	bus_space_write_4(bst, esc->sc_gpio, GPIO_MATRIX_OUT(PIN_MDIO),
	    SIG_GMII_MDO_OUT);

	/* Transmit pads take their IO_MUX function. */
	for (u_int pin = PIN_TX_FIRST; pin <= PIN_TX_LAST; pin++) {
		esp_gmac_update(esc, esc->sc_iomux, IOMUX_PAD(pin), pad,
		    __SHIFTIN(IOMUX_MODE_GMAC, IOMUX_MODE) |
		    __SHIFTIN(PAD_DRV_20MA, IOMUX_DRV) | IOMUX_IE);
	}

	/* The rest are dedicated pads with their own settings. */
	for (u_int pin = PIN_DED_FIRST; pin <= PIN_DED_LAST; pin++) {
		esp_gmac_update(esc, esc->sc_cnntpad, CNNT_PAD(pin),
		    CNNT_PAD_DRV | CNNT_PAD_PU | CNNT_PAD_PD,
		    __SHIFTIN(PAD_DRV_20MA, CNNT_PAD_DRV) | CNNT_PAD_IE);
	}
	esp_gmac_update(esc, esc->sc_cnntpad, CNNT_PAD_CTRL, 0,
	    CNNT_PAD_CTRL_GMAC_DED);
}

static void
esp_gmac_phy_reset(struct esp_gmac_softc *esc)
{
	bus_space_tag_t bst = esc->sc_core.sc_bst;
	const uint32_t bit = __BIT(PIN_PHY_RESET);

	esp_gmac_update(esc, esc->sc_iomux, IOMUX_PAD(PIN_PHY_RESET),
	    IOMUX_MODE | IOMUX_PU | IOMUX_PD,
	    __SHIFTIN(IOMUX_MODE_GPIO, IOMUX_MODE));
	bus_space_write_4(bst, esc->sc_gpio, GPIO_MATRIX_OUT(PIN_PHY_RESET),
	    SIG_GPIO_OUT);

	/* The line is active low: 10 ms down, then let the PHY start. */
	bus_space_write_4(bst, esc->sc_gpio, GPIO_OUT_CLR, bit);
	bus_space_write_4(bst, esc->sc_gpio, GPIO_ENABLE_SET, bit);
	delay(10000);
	bus_space_write_4(bst, esc->sc_gpio, GPIO_OUT_SET, bit);
	delay(500000);
}

static void
esp_gmac_clocks(struct esp_gmac_softc *esc)
{

	esp_gmac_update(esc, esc->sc_clkrst, CLKRST_EMAC_CTRL0, 0,
	    EMAC_SYS_CLK_EN);
	esp_gmac_ref_clock(esc, 25000000);
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_PTP_CTRL, 0,
	    EMAC_PTP_REF_CLK_EN);

	/* Pulse the reset of the block. */
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_CTRL, 0, EMAC_RST_EN);
	delay(1000);
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_CTRL, EMAC_RST_EN, 0);
	delay(1000);

	/* RGMII, clocks taken from and given to the pads. */
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_GMAC_CTRL0,
	    GMAC_PHY_INTF_SEL,
	    __SHIFTIN(GMAC_PHY_INTF_RGMII, GMAC_PHY_INTF_SEL));
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_RMII_PAD_CTRL,
	    EMAC_RMII_PAD_CLK_EN | EMAC_RMII_PAD_CLK_INV, 0);
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_RMII_CTRL,
	    EMAC_RMII_CLK_SEL | EMAC_RMII_CLK_EN | EMAC_RMII_PAD_OUT_CLK,
	    EMAC_RMII_PAD_OUT_CLK);
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_RX_CTRL,
	    EMAC_RX_PAD_CLK_INV | EMAC_RX_PAD_CLK_EN | EMAC_RX_CLK_SEL |
	    EMAC_RX_180_CLK_EN,
	    EMAC_RX_PAD_CLK_EN | EMAC_RX_CLK_SEL | EMAC_RX_180_CLK_EN);
	esp_gmac_update(esc, esc->sc_cnntsys, CNNT_EMAC_TX_CTRL,
	    EMAC_TX_PAD_CLK_EN | EMAC_TX_CLK_SEL | EMAC_TX_180_CLK_EN,
	    EMAC_TX_180_CLK_EN);
}

static int
esp_gmac_intr(void *arg)
{

	return dwc_gmac_intr(arg);
}

static int
esp_gmac_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
esp_gmac_attach(device_t parent, device_t self, void *aux)
{
	struct esp_gmac_softc * const esc = device_private(self);
	struct dwc_gmac_softc * const sc = &esc->sc_core;
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	uint8_t enaddr[ETHER_ADDR_LEN];
	char intrstr[128];
	bus_addr_t addr;
	bus_size_t size;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}
	if (bus_space_map(sc->sc_bst, S31_IOMUX_BASE, S31_IOMUX_SIZE, 0,
		&esc->sc_iomux) != 0 ||
	    bus_space_map(sc->sc_bst, S31_GPIO_BASE, S31_GPIO_SIZE, 0,
		&esc->sc_gpio) != 0 ||
	    bus_space_map(sc->sc_bst, S31_CLKRST_BASE, S31_CLKRST_SIZE, 0,
		&esc->sc_clkrst) != 0 ||
	    bus_space_map(sc->sc_bst, S31_CNNT_PAD_BASE, S31_CNNT_PAD_SIZE, 0,
		&esc->sc_cnntpad) != 0 ||
	    bus_space_map(sc->sc_bst, S31_CNNT_SYS_BASE, S31_CNNT_SYS_SIZE, 0,
		&esc->sc_cnntsys) != 0) {
		aprint_error(": couldn't map system registers\n");
		return;
	}
	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": failed to decode interrupt\n");
		return;
	}

	aprint_naive("\n");
	aprint_normal(": GMAC\n");

	esp_gmac_pins(esc);
	esp_gmac_clocks(esc);
	esp_gmac_phy_reset(esc);

	if (of_hasprop(phandle, "snps,force_thresh_dma_mode"))
		sc->sc_flags |= DWC_GMAC_FORCE_THRESH_DMA_MODE;
	sc->sc_set_speed = esp_gmac_set_speed;

	if (OF_getprop(phandle, "local-mac-address", enaddr,
	    sizeof(enaddr)) == sizeof(enaddr)) {
		prop_dictionary_set_data(device_properties(self),
		    "mac-address", enaddr, sizeof(enaddr));
	}

	/* The management clock derives from the 40 MHz crystal. */
	if (dwc_gmac_attach(sc, MII_PHY_ANY, GMAC_MII_CLK_35_60M_DIV26) != 0)
		return;

	if (fdtbus_intr_establish_xname(phandle, 0, IPL_NET, FDT_INTR_MPSAFE,
	    esp_gmac_intr, sc, device_xname(self)) == NULL) {
		aprint_error_dev(self, "failed to establish interrupt on %s\n",
		    intrstr);
		return;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);
}

CFATTACH_DECL_NEW(esp_gmac, sizeof(struct esp_gmac_softc),
    esp_gmac_match, esp_gmac_attach, NULL, NULL);
