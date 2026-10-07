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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE.
 */

/* Translate the vendor A133 MMC resources before FDT node offsets are cached. */
#include <sys/types.h>
#include <libfdt.h>

#include "sun50i_a100_ccu.h"

#define SOC "/soc@03000000"
#define PIO SOC "/pinctrl@0300b000"
#define MMC SOC "/sdmmc@04022000"
#define CCU SOC "/clock-controller@03001000"
#define PINS PIO "/ember-mmc2-pins"

int sun50i_a133_fdt_fixup(void *);

static int
a133_node(void *fdt, const char *parent, const char *name, uint32_t *phandle)
{
	int node, error;

	node = fdt_path_offset(fdt, parent);
	if (node < 0)
		return node;
	error = fdt_add_subnode(fdt, node, name);
	if (error == -FDT_ERR_EXISTS)
		node = fdt_subnode_offset(fdt, node, name);
	else
		node = error;
	if (node < 0)
		return node;
	*phandle = fdt_get_phandle(fdt, node);
	if (*phandle == 0) {
		error = fdt_generate_phandle(fdt, phandle);
		if (error != 0)
			return error;
		error = fdt_setprop_u32(fdt, node, "phandle", *phandle);
		if (error != 0)
			return error;
	}
	return node;
}

static int
a133_prop(void *fdt, const char *path, const char *name,
    const void *data, int len)
{
	int node;

	node = fdt_path_offset(fdt, path);
	if (node < 0)
		return node;
	return fdt_setprop(fdt, node, name, data, len);
}

static int
a133_clock(void *fdt, const char *name, uint32_t rate, uint32_t *phandle)
{
	int node, error;

	node = a133_node(fdt, "/", name, phandle);
	if (node < 0)
		return node;
	error = fdt_setprop_string(fdt, node, "compatible", "fixed-clock");
	if (error != 0)
		return error;
	error = fdt_setprop_u32(fdt, node, "#clock-cells", 0);
	if (error != 0)
		return error;
	return fdt_setprop_u32(fdt, node, "clock-frequency", rate);
}

/* Every property update can move other node offsets; resolve each path again. */
#define A133_SET(path, name, data, len) do { \
	error = a133_prop(fdt, path, name, data, len); \
	if (error != 0) return error; \
} while (0)
#define A133_STRING(path, name, value) A133_SET(path, name, value, sizeof(value))
#define A133_CELL(path, name, value) do { \
	cell = cpu_to_fdt32(value); \
	A133_SET(path, name, &cell, sizeof(cell)); \
} while (0)

int
sun50i_a133_fdt_fixup(void *fdt)
{
	static const char mmc_pins[] = "PC0\0PC1\0PC5\0PC6\0PC8\0PC9\0"
	    "PC10\0PC11\0PC13\0PC14\0PC15\0PC16";
	static const char *fast_modes[] = {
		"mmc-hs400-1_8v", "mmc-hs200-1_8v", "mmc-ddr-1_8v"
	};
	fdt32_t cells[6], cell;
	uint32_t hosc, losc, iosc, ccu, pins;
	int node, error;

	error = fdt_check_header(fdt);
	if (error != 0)
		return error;
	if (fdt_node_check_compatible(fdt, 0, "allwinner,a133") != 0)
		return 0;
	if (fdt_path_offset(fdt, MMC) < 0 || fdt_path_offset(fdt, PIO) < 0)
		return -FDT_ERR_NOTFOUND;

	error = a133_clock(fdt, "ember-osc24m", 24000000, &hosc);
	if (error != 0)
		return error;
	error = a133_clock(fdt, "ember-osc32k", 32768, &losc);
	if (error != 0)
		return error;
	error = a133_clock(fdt, "ember-iosc", 16000000, &iosc);
	if (error != 0)
		return error;
	node = a133_node(fdt, SOC, "clock-controller@03001000", &ccu);
	if (node < 0)
		return node;
	A133_STRING(CCU, "compatible", "allwinner,sun50i-a100-ccu");
	cells[0] = 0;
	cells[1] = cpu_to_fdt32(0x03001000);
	cells[2] = 0;
	cells[3] = cpu_to_fdt32(0x1000);
	A133_SET(CCU, "reg", cells, 4 * sizeof(cells[0]));
	cells[0] = cpu_to_fdt32(hosc);
	cells[1] = cpu_to_fdt32(losc);
	cells[2] = cpu_to_fdt32(iosc);
	A133_SET(CCU, "clocks", cells, 3 * sizeof(cells[0]));
	A133_STRING(CCU, "clock-names", "hosc\0losc\0iosc");
	A133_CELL(CCU, "#clock-cells", 1);
	A133_CELL(CCU, "#reset-cells", 1);

	A133_STRING(PIO, "compatible", "allwinner,sun50i-a100-pinctrl");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_CLK_APB1);
	A133_SET(PIO, "clocks", cells, 2 * sizeof(cells[0]));
	node = a133_node(fdt, PIO, "ember-mmc2-pins", &pins);
	if (node < 0)
		return node;
	A133_SET(PINS, "pins", mmc_pins, sizeof(mmc_pins));
	A133_STRING(PINS, "function", "mmc2");
	A133_CELL(PINS, "drive-strength", 30);
	A133_SET(PINS, "bias-pull-up", NULL, 0);

	A133_STRING(MMC, "compatible", "allwinner,sun50i-a100-emmc");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_CLK_BUS_MMC2);
	cells[2] = cpu_to_fdt32(ccu);
	cells[3] = cpu_to_fdt32(A100_CLK_MMC2);
	A133_SET(MMC, "clocks", cells, 4 * sizeof(cells[0]));
	A133_STRING(MMC, "clock-names", "ahb\0mmc");
	cells[0] = cpu_to_fdt32(ccu);
	cells[1] = cpu_to_fdt32(A100_RST_BUS_MMC2);
	A133_SET(MMC, "resets", cells, 2 * sizeof(cells[0]));
	A133_STRING(MMC, "reset-names", "ahb");
	A133_CELL(MMC, "pinctrl-0", pins);
	A133_STRING(MMC, "pinctrl-names", "default");
	A133_CELL(MMC, "max-frequency", 25000000);
	for (unsigned int i = 0; i < sizeof(fast_modes) / sizeof(fast_modes[0]); i++) {
		node = fdt_path_offset(fdt, MMC);
		error = fdt_delprop(fdt, node, fast_modes[i]);
		if (error != 0 && error != -FDT_ERR_NOTFOUND)
			return error;
	}
	A133_STRING(MMC, "status", "okay");
	/*
	 * The vendor firmware leaves S_TWI0 on PL0/PL1 running from HOSC:
	 * R_APB2_CFG is zero and R_TWI_BGR is 0x00010001 on the YS-M33.
	 * Preserve that clock/reset/pin route while attaching the common
	 * A31-compatible TWI controller.  Its children include the board
	 * MCU7502 watchdog and the PCF8563 clock.
	 */
	if (fdt_path_offset(fdt, SOC "/s_twi@0x07081400") >= 0) {
		A133_STRING(SOC "/s_twi@0x07081400", "compatible",
		    "allwinner,sun6i-a31-i2c");
		cells[0] = cpu_to_fdt32(hosc);
		A133_SET(SOC "/s_twi@0x07081400", "clocks", cells,
		    sizeof(cells[0]));
	}
	return 0;
}
