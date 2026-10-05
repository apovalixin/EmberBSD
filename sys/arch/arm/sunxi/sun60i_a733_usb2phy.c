/* $NetBSD$ */

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
 * The USB 2.0 PHY of the USB 3 controller of the Allwinner A733.
 *
 * The controller and this PHY sit behind the bus gates of the high speed
 * subsystem (the block the serdes belongs to); until those are opened
 * both read as zeros. The gates are opened here, at attach, because the
 * controller driver reads its registers before it enables its PHYs.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

/* The PHY's own registers. */
#define	U2PHY_CTL		0x10
#define	 U2PHY_CTL_OTGDISABLE	__BIT(10)
#define	 U2PHY_CTL_VBUSVLDEXT	__BIT(5)
#define	 U2PHY_CTL_SIDDQ	__BIT(3)	/* power down */
#define	 U2PHY_CTL_COMMONONN	__BIT(2)
#define	U2PHY_TUNE		0x18

/* The high speed subsystem. */
#define	HSI_USB3_BGR		0x08
#define	 HSI_USB3_BGR_PIPE_CCU	__BIT(20)	/* pipe clock from the CCU */
#define	 HSI_USB3_BGR_ACLK	__BIT(17)
#define	 HSI_USB3_BGR_HCLK	__BIT(16)
#define	 HSI_USB3_BGR_PHY_RSTN	__BIT(4)	/* this PHY out of reset */
#define	 HSI_USB3_BGR_RESETS	0x23		/* the other resets released */
#define	HSI_DBG_CTL		0xf0
#define	 HSI_DBG_CTL_NORMAL	0x20000000	/* no test multiplexer */

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-usb3-u2phy" },
	DEVICE_COMPAT_EOL
};

struct a733_usb2phy_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_bsh;
	bus_space_handle_t	sc_hsi_bsh;
	struct fdtbus_regulator	*sc_vbus;
	uint32_t		sc_tune;
	bool			sc_has_tune;
};

#define	PHY_READ(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_bsh, (reg))
#define	PHY_WRITE(sc, reg, val)		\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_bsh, (reg), (val))

static void *
a733_usb2phy_acquire(device_t dev, const void *data, size_t len)
{
	return device_private(dev);
}

static void
a733_usb2phy_release(device_t dev, void *priv)
{
}

static int
a733_usb2phy_enable(device_t dev, void *priv, bool enable)
{
	struct a733_usb2phy_softc * const sc = priv;
	uint32_t val;

	/*
	 * The PHY must be powered before its reset is released, or it
	 * gives the controller no clock and the controller's own reset
	 * never ends.
	 */
	val = bus_space_read_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR);
	bus_space_write_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR,
	    val & ~HSI_USB3_BGR_PHY_RSTN);

	val = PHY_READ(sc, U2PHY_CTL);
	if (enable) {
		val |= U2PHY_CTL_OTGDISABLE | U2PHY_CTL_VBUSVLDEXT |
		    U2PHY_CTL_COMMONONN;
		val &= ~U2PHY_CTL_SIDDQ;
	} else {
		val &= ~(U2PHY_CTL_OTGDISABLE | U2PHY_CTL_VBUSVLDEXT);
		val |= U2PHY_CTL_SIDDQ;
	}
	PHY_WRITE(sc, U2PHY_CTL, val);

	if (enable) {
		if (sc->sc_has_tune)
			PHY_WRITE(sc, U2PHY_TUNE, sc->sc_tune);
		delay(1000);
		val = bus_space_read_4(sc->sc_bst, sc->sc_hsi_bsh,
		    HSI_USB3_BGR);
		bus_space_write_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR,
		    val | HSI_USB3_BGR_PHY_RSTN);
		delay(1000);
	}

	if (sc->sc_vbus == NULL)
		return 0;

	return enable ? fdtbus_regulator_enable(sc->sc_vbus) :
	    fdtbus_regulator_disable(sc->sc_vbus);
}

static const struct fdtbus_phy_controller_func a733_usb2phy_funcs = {
	.acquire = a733_usb2phy_acquire,
	.release = a733_usb2phy_release,
	.enable = a733_usb2phy_enable,
};

static int
a733_usb2phy_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
a733_usb2phy_attach(device_t parent, device_t self, void *aux)
{
	struct a733_usb2phy_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct fdtbus_reset *rst;
	struct clk *clk;
	bus_addr_t addr;
	bus_size_t size;
	u_int n;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;

	fdtbus_clock_assign(phandle);
	for (n = 0; (clk = fdtbus_clock_get_index(phandle, n)) != NULL; n++) {
		if (clk_enable(clk) != 0) {
			aprint_error(": couldn't enable clock #%u\n", n);
			return;
		}
	}
	for (n = 0; (rst = fdtbus_reset_get_index(phandle, n)) != NULL; n++) {
		if (fdtbus_reset_deassert(rst) != 0) {
			aprint_error(": couldn't de-assert reset #%u\n", n);
			return;
		}
	}

	/* Open the subsystem's gates for the controller and the PHY. */
	if (fdtbus_get_reg(phandle, 1, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_hsi_bsh) != 0) {
		aprint_error(": couldn't map the subsystem registers\n");
		return;
	}
	/*
	 * Without the serdes there is no pipe clock, and the controller
	 * does not leave its reset; it takes the one of the CCU instead.
	 */
	bus_space_write_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR,
	    HSI_USB3_BGR_PIPE_CCU | HSI_USB3_BGR_ACLK | HSI_USB3_BGR_HCLK |
	    HSI_USB3_BGR_RESETS);
	bus_space_write_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_DBG_CTL,
	    HSI_DBG_CTL_NORMAL);

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	sc->sc_has_tune = of_getprop_uint32(phandle, "allwinner,phy-tune",
	    &sc->sc_tune) == 0;
	sc->sc_vbus = fdtbus_regulator_acquire(phandle, "vbus-supply");

	aprint_naive("\n");
	aprint_normal(": USB 2.0 PHY of the USB 3 controller\n");

	fdtbus_register_phy_controller(self, phandle, &a733_usb2phy_funcs);
}

CFATTACH_DECL_NEW(sun60i_a733_usb2phy, sizeof(struct a733_usb2phy_softc),
	a733_usb2phy_match, a733_usb2phy_attach, NULL, NULL);
