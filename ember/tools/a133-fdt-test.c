/* Exercise the actual early FDT adapter without accessing hardware. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libfdt.h>

int sun50i_a133_fdt_fixup(void *);

static void
fixture(void *fdt, int size, const char *compat)
{
	int soc, pio, mmc;

	assert(fdt_create_empty_tree(fdt, size) == 0);
	assert(fdt_setprop_string(fdt, 0, "compatible", compat) == 0);
	soc = fdt_add_subnode(fdt, 0, "soc@03000000");
	assert(soc >= 0);
	assert(fdt_setprop_u32(fdt, soc, "#address-cells", 2) == 0);
	assert(fdt_setprop_u32(fdt, soc, "#size-cells", 2) == 0);
	pio = fdt_add_subnode(fdt, soc, "pinctrl@0300b000");
	assert(pio >= 0);
	assert(fdt_setprop_string(fdt, pio, "compatible",
	    "allwinner,sun50i-pinctrl") == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	mmc = fdt_add_subnode(fdt, soc, "sdmmc@04022000");
	assert(mmc >= 0);
	assert(fdt_setprop_string(fdt, mmc, "compatible",
	    "allwinner,sunxi-mmc-v4p6x") == 0);
	assert(fdt_setprop_string(fdt, mmc, "status", "disabled") == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-hs400-1_8v", NULL, 0) == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-hs200-1_8v", NULL, 0) == 0);
	assert(fdt_setprop(fdt, mmc, "mmc-ddr-1_8v", NULL, 0) == 0);
	soc = fdt_path_offset(fdt, "/soc@03000000");
	int twi = fdt_add_subnode(fdt, soc, "s_twi@0x07081400");
	assert(twi >= 0);
	assert(fdt_setprop_string(fdt, twi, "compatible",
	    "allwinner,sun50i-twi") == 0);
}

int
main(void)
{
	unsigned char *fdt = malloc(16384), *before = malloc(16384);
	const fdt32_t *cells;
	int mmc, provider, len;

	assert(fdt != NULL && before != NULL);
	fixture(fdt, 16384, "allwinner,a133");
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	int twi = fdt_path_offset(fdt, "/soc@03000000/s_twi@0x07081400");
	assert(fdt_node_check_compatible(fdt, twi,
	    "allwinner,sun6i-a31-i2c") == 0);
	cells = fdt_getprop(fdt, twi, "clocks", &len);
	assert(cells != NULL && len == 4);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells));
	assert(provider >= 0);
	cells = fdt_getprop(fdt, provider, "clock-frequency", &len);
	assert(cells != NULL && len == 4 && fdt32_to_cpu(*cells) == 24000000);
	mmc = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
	assert(fdt_node_check_compatible(fdt, mmc,
	    "allwinner,sun50i-a100-emmc") == 0);
	assert(strcmp(fdt_getprop(fdt, mmc, "status", NULL), "okay") == 0);
	cells = fdt_getprop(fdt, mmc, "clocks", &len);
	assert(cells != NULL && len == 16);
	provider = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[0]));
	assert(provider >= 0 && fdt32_to_cpu(cells[2]) ==
	    fdt32_to_cpu(cells[0]));
	assert(fdt_node_check_compatible(fdt, provider,
	    "allwinner,sun50i-a100-ccu") == 0);
	assert(fdt32_to_cpu(cells[1]) == 68 && fdt32_to_cpu(cells[3]) == 64);
	cells = fdt_getprop(fdt, provider, "clocks", &len);
	assert(cells != NULL && len == 12);
	for (int i = 0; i < 3; i++) {
		int osc = fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(cells[i]));
		assert(osc >= 0 && fdt_node_check_compatible(fdt, osc,
		    "fixed-clock") == 0);
	}
	cells = fdt_getprop(fdt, mmc, "max-frequency", NULL);
	assert(cells != NULL && fdt32_to_cpu(*cells) == 25000000);
	assert(fdt_getprop(fdt, mmc, "mmc-hs400-1_8v", NULL) == NULL);
	assert(fdt_getprop(fdt, mmc, "mmc-hs200-1_8v", NULL) == NULL);
	assert(fdt_getprop(fdt, mmc, "mmc-ddr-1_8v", NULL) == NULL);
	cells = fdt_getprop(fdt, mmc, "pinctrl-0", &len);
	assert(cells != NULL && len == 4);
	assert(fdt_node_offset_by_phandle(fdt, fdt32_to_cpu(*cells)) >= 0);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);

	fixture(fdt, 16384, "allwinner,sun50i-a64");
	memcpy(before, fdt, 16384);
	assert(sun50i_a133_fdt_fixup(fdt) == 0);
	assert(memcmp(before, fdt, 16384) == 0);

	fixture(fdt, 16384, "allwinner,a133");
	assert(fdt_pack(fdt) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOSPACE);
	fixture(fdt, 16384, "allwinner,a133");
	mmc = fdt_path_offset(fdt, "/soc@03000000/sdmmc@04022000");
	assert(fdt_del_node(fdt, mmc) == 0);
	assert(sun50i_a133_fdt_fixup(fdt) == -FDT_ERR_NOTFOUND);
	free(before);
	free(fdt);
	puts("A133 FDT adapter: vendor MMC resources, safe modes, isolation and errors passed");
	return 0;
}
