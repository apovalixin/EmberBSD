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
 * Allwinner A733 Gigabit Ethernet: glue for the DesignWare EQOS core.
 *
 * The glue selects RGMII and the clock delays in the controller's own
 * system-control register; the MAC, not the PHY, delays the clocks.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/cprng.h>
#include <sys/device.h>
#include <sys/hash.h>
#include <sys/rndsource.h>
#include <sys/systm.h>

#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_media.h>

#include <dev/mii/miivar.h>

#include <dev/fdt/fdtvar.h>
#include <dev/fdt/syscon.h>

#include <dev/ic/dwc_eqos_var.h>

#include <prop/proplib.h>

#define	EMAC_CLK_REG		0x00
#define	 EMAC_CLK_ETXDC_H	__BITS(17,16)	/* TX delay, high bits */
#define	 EMAC_CLK_RMII_EN	__BIT(13)
#define	 EMAC_CLK_ETXDC		__BITS(12,10)	/* TX delay, low bits */
#define	 EMAC_CLK_ERXDC		__BITS(9,5)	/* RX delay */
#define	 EMAC_CLK_EPIT		__BIT(2)	/* 1: RGMII */
#define	 EMAC_CLK_ETCS		__BITS(1,0)
#define	  EMAC_CLK_ETCS_INT_GMII	2

#define	EMAC_DELAY_STEP_PS	100
#define	EMAC_DELAY_MAX		31

/* The chip ID inside the security ID block. */
#define	SID_CHIPID		0x200
#define	SID_CHIPID_WORDS	4

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-gmac210" },
	DEVICE_COMPAT_EOL
};

static int
sun60i_a733_eqos_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

/* A delay property in picoseconds as register steps, or -1. */
static int
sun60i_a733_eqos_delay(int phandle, const char *prop)
{
	u_int ps;

	if (of_getprop_uint32(phandle, prop, &ps) != 0)
		return 0;
	if (ps % EMAC_DELAY_STEP_PS != 0 ||
	    ps / EMAC_DELAY_STEP_PS > EMAC_DELAY_MAX)
		return -1;

	return ps / EMAC_DELAY_STEP_PS;
}

/*
 * The controller holds no address after reset and the boot loader passes
 * none. Derive one from the chip ID, so that it is the same on every boot.
 */
static bool
sun60i_a733_eqos_chip_address(struct eqos_softc *sc, uint8_t *enaddr)
{
	uint32_t id[SID_CHIPID_WORDS], lo, hi;
	bus_space_handle_t bsh;
	bus_addr_t addr;
	bus_size_t size;
	u_int n;

	const int sid = of_find_bycompat(OF_finddevice("/"),
	    "allwinner,sun60i-a733-sid");
	if (sid <= 0 || fdtbus_get_reg(sid, 0, &addr, &size) != 0 ||
	    size < SID_CHIPID + sizeof(id))
		return false;
	if (bus_space_map(sc->sc_bst, addr + SID_CHIPID, sizeof(id), 0,
	    &bsh) != 0)
		return false;
	for (n = 0; n < SID_CHIPID_WORDS; n++)
		id[n] = bus_space_read_4(sc->sc_bst, bsh, 4 * n);
	bus_space_unmap(sc->sc_bst, bsh, sizeof(id));

	lo = hash32_buf(id, sizeof(id), HASH32_BUF_INIT);
	hi = hash32_buf(id, sizeof(id), lo);
	if (lo == 0 && hi == 0)
		return false;

	enaddr[0] = 0x02;		/* locally administered, unicast */
	enaddr[1] = hi & 0xff;
	enaddr[2] = (lo >> 24) & 0xff;
	enaddr[3] = (lo >> 16) & 0xff;
	enaddr[4] = (lo >> 8) & 0xff;
	enaddr[5] = lo & 0xff;

	return true;
}

static void
sun60i_a733_eqos_set_address(struct eqos_softc *sc, int phandle)
{
	prop_dictionary_t dict = device_properties(sc->sc_dev);
	uint8_t enaddr[ETHER_ADDR_LEN];
	static const uint8_t zero[ETHER_ADDR_LEN];
	const uint8_t *addr;
	int len;

	addr = fdtbus_get_prop(phandle, "local-mac-address", &len);
	if (addr != NULL && len == ETHER_ADDR_LEN &&
	    memcmp(addr, zero, ETHER_ADDR_LEN) != 0) {
		memcpy(enaddr, addr, ETHER_ADDR_LEN);
	} else if (!sun60i_a733_eqos_chip_address(sc, enaddr)) {
		cprng_strong(kern_cprng, enaddr, sizeof(enaddr), 0);
		enaddr[0] = (enaddr[0] & ~0x01) | 0x02;
	}
	prop_dictionary_set_data(dict, "mac-address", enaddr, sizeof(enaddr));
}

static void
sun60i_a733_eqos_attach(device_t parent, device_t self, void *aux)
{
	struct eqos_softc * const sc = device_private(self);
	prop_dictionary_t dict = device_properties(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	struct syscon *syscon;
	struct fdtbus_reset *rst;
	struct clk *clk;
	char intrstr[128];
	const char *phy_mode;
	bus_addr_t addr;
	bus_size_t size;
	uint32_t val;
	int n;

	if (fdtbus_get_reg(phandle, 0, &addr, &size) != 0) {
		aprint_error(": couldn't get registers\n");
		return;
	}

	phy_mode = fdtbus_get_string(phandle, "phy-mode");
	if (phy_mode == NULL || strncmp(phy_mode, "rgmii", 5) != 0) {
		aprint_error(": unsupported phy-mode '%s'\n",
		    phy_mode ? phy_mode : "(none)");
		return;
	}

	const int tx_delay = sun60i_a733_eqos_delay(phandle,
	    "allwinner,tx-delay-ps");
	const int rx_delay = sun60i_a733_eqos_delay(phandle,
	    "allwinner,rx-delay-ps");
	if (tx_delay < 0 || rx_delay < 0) {
		aprint_error(": delays must be multiples of %d ps up to %d\n",
		    EMAC_DELAY_STEP_PS, EMAC_DELAY_MAX * EMAC_DELAY_STEP_PS);
		return;
	}

	syscon = fdtbus_syscon_acquire(phandle, "syscon");
	if (syscon == NULL) {
		aprint_error(": couldn't get syscon\n");
		return;
	}

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;
	sc->sc_dmat = faa->faa_dmat;
	if (bus_space_map(sc->sc_bst, addr, size, 0, &sc->sc_bsh) != 0) {
		aprint_error(": couldn't map registers\n");
		return;
	}

	if (!fdtbus_intr_str(phandle, 0, intrstr, sizeof(intrstr))) {
		aprint_error(": failed to decode interrupt\n");
		return;
	}

	fdtbus_clock_assign(phandle);
	for (n = 0; (clk = fdtbus_clock_get_index(phandle, n)) != NULL; n++) {
		if (clk_enable(clk) != 0) {
			aprint_error(": couldn't enable clock #%d\n", n);
			return;
		}
	}
	for (n = 0; (rst = fdtbus_reset_get_index(phandle, n)) != NULL; n++) {
		if (fdtbus_reset_deassert(rst) != 0) {
			aprint_error(": couldn't de-assert reset #%d\n", n);
			return;
		}
	}

	val = EMAC_CLK_EPIT |
	    __SHIFTIN(EMAC_CLK_ETCS_INT_GMII, EMAC_CLK_ETCS) |
	    __SHIFTIN(tx_delay & 0x7, EMAC_CLK_ETXDC) |
	    __SHIFTIN(tx_delay >> 3, EMAC_CLK_ETXDC_H) |
	    __SHIFTIN(rx_delay, EMAC_CLK_ERXDC);
	syscon_lock(syscon);
	syscon_write_4(syscon, EMAC_CLK_REG, val);
	syscon_unlock(syscon);

	sun60i_a733_eqos_set_address(sc, phandle);

	/* The MAC delays both clocks: the PHY must add nothing. */
	prop_dictionary_set_uint32(dict, "rx-internal-delay-ps", 0);
	prop_dictionary_set_uint32(dict, "tx-internal-delay-ps", 0);

	/*
	 * The PHY also answers at the broadcast address 0: attach only
	 * the one the device tree names.
	 */
	sc->sc_phy_id = MII_PHY_ANY;
	const int phy = fdtbus_get_phandle(phandle, "phy-handle");
	if (phy > 0 && of_getprop_uint32(phy, "reg", &val) == 0)
		sc->sc_phy_id = val;
	clk = fdtbus_clock_get(phandle, "stmmaceth");
	sc->sc_csr_clock = clk != NULL ? clk_get_rate(clk) : 0;

	if (eqos_attach(sc) != 0)
		return;

	if (fdtbus_intr_establish_xname(phandle, 0, IPL_NET, FDT_INTR_MPSAFE,
	    eqos_intr, sc, device_xname(self)) == NULL) {
		aprint_error_dev(self, "failed to establish interrupt on %s\n",
		    intrstr);
		return;
	}
	aprint_normal_dev(self, "interrupting on %s\n", intrstr);
}

CFATTACH_DECL_NEW(sun60i_a733_eqos, sizeof(struct eqos_softc),
	sun60i_a733_eqos_match, sun60i_a733_eqos_attach, NULL, NULL);
