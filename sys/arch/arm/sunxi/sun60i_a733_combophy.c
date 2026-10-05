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
 * The serdes of the Type-C data port of the Allwinner A733: a Cadence
 * combo PHY that carries USB 3 SuperSpeed (and DisplayPort, not handled
 * here) on the four high speed lanes of the connector.
 *
 * The register values below are the ones the vendor's driver programs
 * for USB 3 with spread spectrum and a 100 MHz configuration clock.
 * Which pair of lanes carries USB depends on how the plug is turned;
 * nothing tells this driver yet, so it sets up the unflipped case only
 * ("allwinner,flipped" in the device tree selects the other one).
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/fdt/fdtvar.h>

/* The PHY's pins (register block 0). */
#define	CPHY_CTRL0		0x000
#define	 CPHY_CTRL0_FLIPPED	__BIT(12)
#define	 CPHY_CTRL0_DP_RSTN	__BIT(8)
#define	 CPHY_CTRL0_PIPE_MASK	__BIT(5)
#define	 CPHY_CTRL0_PIPE_RSTN	__BIT(4)
#define	 CPHY_CTRL0_RSTN	__BIT(0)
#define	CPHY_CTRL1		0x004
#define	 CPHY_CTRL1_PLL1_REFSEL	__BIT(24)
#define	 CPHY_CTRL1_PLL0_REFSEL	__BIT(20)
#define	 CPHY_CTRL1_DIG_REFSEL	__BIT(0)
#define	CPHY_CTRL2		0x008
#define	 CPHY_CTRL2_IDDQ_SEL	__BIT(1)
#define	 CPHY_CTRL2_IDDQ	__BIT(0)
#define	CPHY_CTRL3		0x00c
#define	 CPHY_CTRL3_POWER_REQ	__BITS(9,4)
#define	 CPHY_CTRL3_PLLCLK_EN	__BIT(0)
#define	CPHY_STATUS0		0x900
#define	 CPHY_STATUS0_READY	__BIT(0)

/* The PHY's own 16-bit registers (block 1), two bytes apart. */
#define	CPHY_REG(n)		((n) << 1)
#define	CPHY_PMA_ISO		0xc008
#define	 CPHY_PMA_ISO_LANES	__BITS(11,8)

/* Lane routing of the serdes block (block 2). */
#define	SDS_DP_CONV		0x004
#define	 SDS_DP_CONV_ON		0xff1
#define	SDS_PIPE_CLK_MAP	0x100
#define	SDS_PIPE_RXD_MAP	0x104
#define	 SDS_PIPE_MAP		__BITS(3,0)	/* 0: this PHY */

/* The high speed subsystem (block 3). */
#define	HSI_USB3_BGR		0x08
#define	 HSI_USB3_BGR_PIPE_CCU	__BIT(20)

/* The crystal oscillator's outputs, in the real-time clock (block 4). */
#define	DCXO_SERDES_ON		0x530

struct a733_combophy_regval {
	uint16_t	reg;
	uint16_t	val;
};

/* Lane defaults, the same for either plug orientation. */
static const struct a733_combophy_regval a733_combophy_lanes[] = {
	{ 0xe005, 0x0003 }, { 0xe006, 0x1124 }, { 0x4100, 0x00ff },
	{ 0x4300, 0x00ff }, { 0x4102, 0x04ae }, { 0x4302, 0x04ae },
	{ 0x4103, 0x04ae }, { 0x4303, 0x04ae }, { 0x8000, 0x0000 },
	{ 0x8200, 0x091d }, { 0x8002, 0x0000 }, { 0x8202, 0x0900 },
	{ 0x8003, 0x0000 }, { 0x8203, 0x0000 }, { 0x8006, 0x0000 },
	{ 0x8206, 0x010f }, { 0x40ea, 0x00ff }, { 0x42ea, 0x00ff },
	{ 0x8108, 0x0000 }, { 0x8308, 0x0009 }, { 0x8110, 0x0000 },
	{ 0x8310, 0x0009 }, { 0x8118, 0x0000 }, { 0x8318, 0x000d },
	{ 0x4040, 0x2a84 }, { 0x4240, 0x2a84 }, { 0x40c6, 0x00a3 },
	{ 0x42c6, 0x00a3 }, { 0x4050, 0x0000 }, { 0x4250, 0x0000 },
	{ 0x404c, 0x001c }, { 0x424c, 0x001c }, { 0x4500, 0x00ff },
	{ 0x4700, 0x00ff }, { 0x4502, 0x04ae }, { 0x4702, 0x04ae },
	{ 0x4503, 0x04ae }, { 0x4703, 0x04ae }, { 0x8400, 0x091d },
	{ 0x8600, 0x0000 }, { 0x8402, 0x0900 }, { 0x8602, 0x0000 },
	{ 0x8403, 0x0000 }, { 0x8603, 0x0000 }, { 0x8406, 0x010f },
	{ 0x8606, 0x0000 }, { 0x44ea, 0x00ff }, { 0x46ea, 0x00ff },
	{ 0x8508, 0x0009 }, { 0x8708, 0x0000 }, { 0x8510, 0x0009 },
	{ 0x8710, 0x0000 }, { 0x8518, 0x000d }, { 0x8718, 0x0000 },
	{ 0x4440, 0x2a84 }, { 0x4640, 0x2a84 }, { 0x44c6, 0x00a3 },
	{ 0x46c6, 0x00a3 }, { 0x4450, 0x0000 }, { 0x4650, 0x0000 },
	{ 0x444c, 0x001c }, { 0x464c, 0x001c },
};

/* Common block and PLL setup, first part. */
static const struct a733_combophy_regval a733_combophy_common1[] = {
	{ 0x0022, 0x001a }, { 0x002a, 0x0034 }, { 0x002c, 0x00da },
	{ 0x0032, 0x0034 }, { 0x0034, 0x00da }, { 0x0064, 0x0082 },
	{ 0x0065, 0x0082 }, { 0x0074, 0x001a }, { 0x0104, 0x0020 },
	{ 0x0105, 0x0007 }, { 0x010c, 0x0020 }, { 0x010d, 0x0007 },
	{ 0x0114, 0x030c }, { 0x0115, 0x0007 }, { 0x0124, 0x0007 },
	{ 0x0125, 0x0003 }, { 0x0126, 0x000f }, { 0x0128, 0x0132 },
	{ 0x8044, 0x001a }, { 0x8244, 0x001a }, { 0x8444, 0x001a },
	{ 0x8644, 0x001a }, { 0x8045, 0x0082 }, { 0x8245, 0x0082 },
	{ 0x8445, 0x0082 }, { 0x8645, 0x0082 }, { 0x804c, 0x001a },
	{ 0x824c, 0x001a }, { 0x844c, 0x001a }, { 0x864c, 0x001a },
	{ 0x804d, 0x0082 }, { 0x824d, 0x0082 }, { 0x844d, 0x0082 },
	{ 0x864d, 0x0082 }, { 0x4123, 0x0a28 }, { 0x4323, 0x0a28 },
	{ 0x4523, 0x0a28 }, { 0x4723, 0x0a28 }, { 0x01a1, 0x8600 },
	{ 0x01c1, 0x0601 },
};

/* Lane roles, plug not flipped. */
static const struct a733_combophy_regval a733_combophy_norm1[] = {
	{ 0x44e5, 0x0012 }, { 0x46e5, 0x0012 }, { 0x40e6, 0x0000 },
	{ 0x42e6, 0x0000 }, { 0x40e7, 0x0001 }, { 0x42e7, 0x0001 },
	{ 0x40e5, 0x0012 }, { 0x42e5, 0x0012 }, { 0x40e5, 0x0041 },
	{ 0x42e5, 0x0041 }, { 0x44e6, 0x0001 }, { 0x46e6, 0x0001 },
	{ 0x44e7, 0x0000 }, { 0x46e7, 0x0000 }, { 0x44e5, 0x0019 },
	{ 0x46e5, 0x0019 },
};

/* Lane roles, plug flipped. */
static const struct a733_combophy_regval a733_combophy_flip1[] = {
	{ 0x40e5, 0x0012 }, { 0x42e5, 0x0012 }, { 0x44e6, 0x0000 },
	{ 0x46e6, 0x0000 }, { 0x44e7, 0x0001 }, { 0x46e7, 0x0001 },
	{ 0x44e5, 0x0012 }, { 0x46e5, 0x0012 }, { 0x44e5, 0x0041 },
	{ 0x46e5, 0x0041 }, { 0x40e6, 0x0001 }, { 0x42e6, 0x0001 },
	{ 0x40e7, 0x0000 }, { 0x42e7, 0x0000 }, { 0x40e5, 0x0019 },
	{ 0x42e5, 0x0019 },
};

/* PLL setup with spread spectrum, PIPE layer. */
static const struct a733_combophy_regval a733_combophy_common2[] = {
	{ 0x0094, 0x0004 }, { 0x00d4, 0x0004 }, { 0x01a4, 0x0509 },
	{ 0x01c4, 0x0509 }, { 0x01a5, 0x0f00 }, { 0x01c5, 0x0f00 },
	{ 0x01a6, 0x0f08 }, { 0x01c6, 0x0f08 }, { 0x0090, 0x0180 },
	{ 0x00d0, 0x019f }, { 0x0091, 0x9d8a }, { 0x00d1, 0x6276 },
	{ 0x0092, 0x0002 }, { 0x00d2, 0x0002 }, { 0x0093, 0x0102 },
	{ 0x00d3, 0x0116 }, { 0x01a0, 0x0002 }, { 0x01c0, 0x0002 },
	{ 0x0098, 0x0001 }, { 0x00d8, 0x0001 }, { 0x0099, 0x045f },
	{ 0x00d9, 0x04c4 }, { 0x009a, 0x006b }, { 0x00da, 0x006a },
	{ 0x009b, 0x0004 }, { 0x00db, 0x0004 }, { 0x0084, 0x0104 },
	{ 0x00c4, 0x0104 }, { 0x0085, 0x0005 }, { 0x00c5, 0x0005 },
	{ 0x0086, 0x0337 }, { 0x00c6, 0x0337 }, { 0x0088, 0x0335 },
	{ 0x00c8, 0x0335 }, { 0x0082, 0x0003 }, { 0x00c2, 0x0003 },
	{ 0x009c, 0x00cf }, { 0x00dc, 0x00cf }, { 0x009e, 0x00ce },
	{ 0x00de, 0x00ce }, { 0x009f, 0x0005 }, { 0x00df, 0x0005 },
	{ 0xc010, 0x5100 }, { 0xc011, 0x0100 }, { 0xc011, 0x010f },
	{ 0xc018, 0x0a0a }, { 0xc01a, 0x1008 }, { 0xc01b, 0x0010 },
	{ 0x0041, 0x8200 }, { 0x0047, 0x8200 },
};

/* Transmitter and receiver tuning, plug not flipped. */
static const struct a733_combophy_regval a733_combophy_norm2[] = {
	{ 0x4700, 0x00ff }, { 0x4701, 0x04af }, { 0x4702, 0x04ae },
	{ 0x4703, 0x04ae }, { 0x8400, 0x091d }, { 0x8401, 0x091d },
	{ 0x8402, 0x0900 }, { 0x8403, 0x0000 }, { 0x4640, 0x2a84 },
	{ 0x464d, 0x0011 }, { 0x8490, 0x000c }, { 0x8508, 0x0009 },
	{ 0x8549, 0x0c02 }, { 0x8577, 0x06f6 }, { 0x8578, 0x4606 },
	{ 0x44eb, 0x0006 }, { 0x8571, 0x0519 }, { 0x8572, 0x0519 },
	{ 0x85e8, 0x1002 }, { 0x85e5, 0x0b98 }, { 0x85e2, 0x0c01 },
	{ 0x85e3, 0x0000 }, { 0x85f5, 0x0000 }, { 0x85f4, 0x0311 },
	{ 0x85ff, 0x0000 }, { 0x87ff, 0x0000 }, { 0x8480, 0x010a },
	{ 0x8482, 0x0003 }, { 0x46ea, 0x00ff }, { 0x44ea, 0x00ff },
	{ 0x4100, 0x02ff }, { 0x4101, 0x06af }, { 0x4102, 0x06ae },
	{ 0x4103, 0x06ae }, { 0x8200, 0x0d1d }, { 0x8201, 0x0d1d },
	{ 0x8202, 0x0d00 }, { 0x8203, 0x0500 }, { 0x4040, 0x2a82 },
	{ 0x404d, 0x0014 }, { 0x8290, 0x0013 }, { 0x8308, 0x0000 },
	{ 0x8349, 0x0c02 }, { 0x8377, 0x0330 }, { 0x8378, 0x0300 },
	{ 0x42eb, 0x0003 }, { 0x8371, 0x0019 }, { 0x8372, 0x0019 },
	{ 0x83e8, 0x1004 }, { 0x83e5, 0x00f9 }, { 0x83e2, 0x0c01 },
	{ 0x83e3, 0x0002 }, { 0x83f5, 0x0000 }, { 0x83f4, 0x0031 },
	{ 0x83ff, 0x0001 }, { 0x81ff, 0x0002 }, { 0x8280, 0x018c },
	{ 0x8282, 0x0003 }, { 0x40ea, 0x000f }, { 0x42ea, 0x00f0 },
};

/* Transmitter and receiver tuning, plug flipped. */
static const struct a733_combophy_regval a733_combophy_flip2[] = {
	{ 0x4100, 0x00ff }, { 0x4101, 0x04af }, { 0x4102, 0x04ae },
	{ 0x4103, 0x04ae }, { 0x8200, 0x091d }, { 0x8201, 0x091d },
	{ 0x8202, 0x0900 }, { 0x8203, 0x0000 }, { 0x4040, 0x2a84 },
	{ 0x404d, 0x0011 }, { 0x8290, 0x000c }, { 0x8308, 0x0009 },
	{ 0x8349, 0x0c02 }, { 0x8377, 0x06f6 }, { 0x8378, 0x4606 },
	{ 0x42eb, 0x0006 }, { 0x8371, 0x0519 }, { 0x8372, 0x0519 },
	{ 0x83e8, 0x1002 }, { 0x83e5, 0x0b98 }, { 0x83e2, 0x0c01 },
	{ 0x83e3, 0x0000 }, { 0x83f5, 0x0000 }, { 0x83f4, 0x0311 },
	{ 0x83ff, 0x0000 }, { 0x81ff, 0x0000 }, { 0x8280, 0x010a },
	{ 0x8282, 0x0003 }, { 0x40ea, 0x00ff }, { 0x42ea, 0x00ff },
	{ 0x4700, 0x02ff }, { 0x4701, 0x06af }, { 0x4702, 0x06ae },
	{ 0x4703, 0x06ae }, { 0x8400, 0x0d1d }, { 0x8401, 0x0d1d },
	{ 0x8402, 0x0d00 }, { 0x8403, 0x0500 }, { 0x4640, 0x2a82 },
	{ 0x464d, 0x0014 }, { 0x8490, 0x0013 }, { 0x8508, 0x0000 },
	{ 0x8549, 0x0c02 }, { 0x8577, 0x0330 }, { 0x8578, 0x0300 },
	{ 0x44eb, 0x0003 }, { 0x8571, 0x0019 }, { 0x8572, 0x0019 },
	{ 0x85e8, 0x1004 }, { 0x85e5, 0x00f9 }, { 0x85e2, 0x0c01 },
	{ 0x85e3, 0x0002 }, { 0x85f5, 0x0000 }, { 0x85f4, 0x0031 },
	{ 0x85ff, 0x0001 }, { 0x87ff, 0x0002 }, { 0x8480, 0x018c },
	{ 0x8482, 0x0003 }, { 0x46ea, 0x000f }, { 0x44ea, 0x00f0 },
};

/* Closing writes. */
static const struct a733_combophy_regval a733_combophy_last[] = {
	{ 0xe003, 0x0001 }, { 0x0103, 0x007f }, { 0x010b, 0x007f },
};

static const struct device_compatible_entry compat_data[] = {
	{ .compat = "allwinner,sun60i-a733-combophy" },
	DEVICE_COMPAT_EOL
};

struct a733_combophy_softc {
	device_t		sc_dev;
	bus_space_tag_t		sc_bst;
	bus_space_handle_t	sc_pins_bsh;
	bus_space_handle_t	sc_phy_bsh;
	bus_space_handle_t	sc_sds_bsh;
	bus_space_handle_t	sc_hsi_bsh;
	bool			sc_flipped;
};

#define	PINS_READ(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_pins_bsh, (reg))
#define	PINS_WRITE(sc, reg, val)	\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_pins_bsh, (reg), (val))
#define	SDS_READ(sc, reg)		\
	bus_space_read_4((sc)->sc_bst, (sc)->sc_sds_bsh, (reg))
#define	SDS_WRITE(sc, reg, val)		\
	bus_space_write_4((sc)->sc_bst, (sc)->sc_sds_bsh, (reg), (val))

static void
a733_combophy_load(struct a733_combophy_softc *sc,
    const struct a733_combophy_regval *tab, size_t n)
{
	for (size_t i = 0; i < n; i++)
		bus_space_write_2(sc->sc_bst, sc->sc_phy_bsh,
		    CPHY_REG(tab[i].reg), tab[i].val);
}

#define	LOAD(sc, tab)	a733_combophy_load((sc), (tab), __arraycount(tab))

static bool
a733_combophy_ready(struct a733_combophy_softc *sc)
{
	for (int retry = 2000; retry > 0; retry--) {
		if (PINS_READ(sc, CPHY_STATUS0) & CPHY_STATUS0_READY)
			return true;
		delay(5);
	}
	return false;
}

static int
a733_combophy_start(struct a733_combophy_softc *sc)
{
	uint32_t val;
	uint16_t iso;

	/* Out of power-down; the reference comes from the oscillator. */
	val = PINS_READ(sc, CPHY_CTRL2);
	PINS_WRITE(sc, CPHY_CTRL2,
	    val & ~(CPHY_CTRL2_IDDQ_SEL | CPHY_CTRL2_IDDQ));
	val = PINS_READ(sc, CPHY_CTRL1);
	PINS_WRITE(sc, CPHY_CTRL1, val | CPHY_CTRL1_PLL1_REFSEL |
	    CPHY_CTRL1_PLL0_REFSEL | CPHY_CTRL1_DIG_REFSEL);
	SDS_WRITE(sc, SDS_PIPE_CLK_MAP,
	    SDS_READ(sc, SDS_PIPE_CLK_MAP) & ~SDS_PIPE_MAP);
	SDS_WRITE(sc, SDS_PIPE_RXD_MAP,
	    SDS_READ(sc, SDS_PIPE_RXD_MAP) & ~SDS_PIPE_MAP);

	val = PINS_READ(sc, CPHY_CTRL0);
	PINS_WRITE(sc, CPHY_CTRL0, val & ~CPHY_CTRL0_PIPE_MASK);

	LOAD(sc, a733_combophy_lanes);
	LOAD(sc, a733_combophy_common1);
	if (sc->sc_flipped)
		LOAD(sc, a733_combophy_flip1);
	else
		LOAD(sc, a733_combophy_norm1);
	val = PINS_READ(sc, CPHY_CTRL0);
	if (sc->sc_flipped)
		val |= CPHY_CTRL0_FLIPPED;
	else
		val &= ~CPHY_CTRL0_FLIPPED;
	PINS_WRITE(sc, CPHY_CTRL0, val);
	LOAD(sc, a733_combophy_common2);
	if (sc->sc_flipped)
		LOAD(sc, a733_combophy_flip2);
	else
		LOAD(sc, a733_combophy_norm2);
	LOAD(sc, a733_combophy_last);

	/* Pulse the reset, then bring the PIPE side and the PLL up. */
	val = PINS_READ(sc, CPHY_CTRL0);
	PINS_WRITE(sc, CPHY_CTRL0, val & ~CPHY_CTRL0_RSTN);
	delay(1000);
	PINS_WRITE(sc, CPHY_CTRL0, val | CPHY_CTRL0_RSTN);
	SDS_WRITE(sc, SDS_DP_CONV, SDS_READ(sc, SDS_DP_CONV) | SDS_DP_CONV_ON);
	delay(1000);

	val = PINS_READ(sc, CPHY_CTRL0);
	PINS_WRITE(sc, CPHY_CTRL0, val | CPHY_CTRL0_PIPE_RSTN);
	val = PINS_READ(sc, CPHY_CTRL3);
	PINS_WRITE(sc, CPHY_CTRL3, val | CPHY_CTRL3_PLLCLK_EN);
	delay(1000);
	val = PINS_READ(sc, CPHY_CTRL3);
	PINS_WRITE(sc, CPHY_CTRL3, val | __SHIFTIN(1, CPHY_CTRL3_POWER_REQ));
	val = PINS_READ(sc, CPHY_CTRL0);
	PINS_WRITE(sc, CPHY_CTRL0, val | CPHY_CTRL0_DP_RSTN);

	if ((PINS_READ(sc, CPHY_STATUS0) & CPHY_STATUS0_READY) == 0) {
		delay(10000);
		iso = bus_space_read_2(sc->sc_bst, sc->sc_phy_bsh,
		    CPHY_REG(CPHY_PMA_ISO));
		bus_space_write_2(sc->sc_bst, sc->sc_phy_bsh,
		    CPHY_REG(CPHY_PMA_ISO), iso & ~CPHY_PMA_ISO_LANES);
		if (!a733_combophy_ready(sc)) {
			aprint_error_dev(sc->sc_dev, "PLL did not lock\n");
			return EIO;
		}
	}

	/* The controller now takes its pipe clock from this PHY. */
	val = bus_space_read_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR);
	bus_space_write_4(sc->sc_bst, sc->sc_hsi_bsh, HSI_USB3_BGR,
	    val & ~HSI_USB3_BGR_PIPE_CCU);

	return 0;
}

static void *
a733_combophy_acquire(device_t dev, const void *data, size_t len)
{
	return device_private(dev);
}

static void
a733_combophy_release(device_t dev, void *priv)
{
}

static int
a733_combophy_enable(device_t dev, void *priv, bool enable)
{
	struct a733_combophy_softc * const sc = priv;

	if (!enable)
		return 0;
	return a733_combophy_start(sc);
}

static const struct fdtbus_phy_controller_func a733_combophy_funcs = {
	.acquire = a733_combophy_acquire,
	.release = a733_combophy_release,
	.enable = a733_combophy_enable,
};

static int
a733_combophy_match(device_t parent, cfdata_t cf, void *aux)
{
	struct fdt_attach_args * const faa = aux;

	return of_compatible_match(faa->faa_phandle, compat_data);
}

static void
a733_combophy_attach(device_t parent, device_t self, void *aux)
{
	struct a733_combophy_softc * const sc = device_private(self);
	struct fdt_attach_args * const faa = aux;
	const int phandle = faa->faa_phandle;
	bus_space_handle_t *bshp[] = {
		&sc->sc_pins_bsh, &sc->sc_phy_bsh, &sc->sc_sds_bsh,
		&sc->sc_hsi_bsh
	};
	bus_space_handle_t bsh;
	struct fdtbus_reset *rst;
	struct clk *clk;
	bus_addr_t addr;
	bus_size_t size;
	u_int n;

	sc->sc_dev = self;
	sc->sc_bst = faa->faa_bst;

	for (n = 0; n < __arraycount(bshp); n++) {
		if (fdtbus_get_reg(phandle, n, &addr, &size) != 0 ||
		    bus_space_map(sc->sc_bst, addr, size, 0, bshp[n]) != 0) {
			aprint_error(": couldn't map registers #%u\n", n);
			return;
		}
	}

	/* The oscillator's outputs to the serdes are gated in the RTC. */
	if (fdtbus_get_reg(phandle, n, &addr, &size) != 0 ||
	    bus_space_map(sc->sc_bst, addr, size, 0, &bsh) != 0) {
		aprint_error(": couldn't map the oscillator gate\n");
		return;
	}
	bus_space_write_4(sc->sc_bst, bsh, 0, DCXO_SERDES_ON);
	bus_space_unmap(sc->sc_bst, bsh, size);

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

	sc->sc_flipped = of_hasprop(phandle, "allwinner,flipped");

	aprint_naive("\n");
	aprint_normal(": USB 3 serdes\n");

	fdtbus_register_phy_controller(self, phandle, &a733_combophy_funcs);
}

CFATTACH_DECL_NEW(sun60i_a733_combophy, sizeof(struct a733_combophy_softc),
	a733_combophy_match, a733_combophy_attach, NULL, NULL);
