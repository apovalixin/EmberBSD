/* Origin: EmberBSD; AI-assisted production A733 CCU regression fixture. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef size_t bus_size_t;
#define __BIT(n) (UINT32_C(1) << (n))
#define __BITS(h, l) ((UINT32_MAX >> (31 - (h))) & (UINT32_MAX << (l)))
#define __SHIFTOUT(v, m) (((v) & (m)) >> __builtin_ctz(m))
#define __SHIFTIN(v, m) (((v) << __builtin_ctz(m)) & (m))
#define __SHIFTOUT_MASK(m) __SHIFTOUT(m, m)
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#define howmany(x, y) (((x) + ((y) - 1)) / (y))
#define KASSERT(c) assert(c)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define CLK_SET_RATE_PARENT 1
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) (d)
#define aprint_error(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_naive(...) ((void)0)
#define delay(n) ((void)0)
struct clk_domain { int unused; };
struct clk { struct clk_domain *domain; const char *name; u_int flags; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
#include "sunxi_ccu.h"
#include "sun60i_a733_ccu.h"
#include "bindings.h"

static uint32_t registers[0x1500 / 4], last_write;
static unsigned writes, barriers, checks;
static bool reject_write, missing_hosc;
static struct sunxi_ccu_softc state;
static struct clk hosc = { .name = "hosc" };
static struct clk *clk_get_parent(struct clk *);
static u_int clk_get_rate(struct clk *);
static int clk_set_rate(struct clk *, u_int);
static int clk_enable(struct clk *);
static u_int clk_round_rate(struct clk *, u_int);
static struct clk *fdtbus_clock_get(int node, const char *name)
{
	return !missing_hosc && strcmp(name, "hosc") == 0 ? &hosc : NULL;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	return registers[off / 4];
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t val)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	writes++;
	last_write = val;
	if (!reject_write)
		registers[off / 4] = off == 0xb20 ? val & ~__BIT(27) : val;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, bus_size_t len, int flags)
{
	assert(len == 4 && flags == 3);
	barriers++;
}
static int
of_compatible_match(int node, const struct device_compatible_entry *compat)
{
	return strcmp(compat[0].compat, "allwinner,sun60i-a733-ccu") == 0;
}
int sunxi_ccu_attach(struct sunxi_ccu_softc *sc) { return 0; }
void sunxi_ccu_print(struct sunxi_ccu_softc *sc) {}
struct sunxi_ccu_clk *
sunxi_ccu_clock_find(struct sunxi_ccu_softc *sc, const char *name)
{
	for (u_int i = 0; i < sc->sc_nclks; i++)
		if (sc->sc_clks[i].base.name != NULL &&
		    strcmp(sc->sc_clks[i].base.name, name) == 0)
			return &sc->sc_clks[i];
	return NULL;
}
#include "dispatch.h"
static struct clk *clk_get_parent(struct clk *clk)
{
	return clk == &hosc ? NULL : sunxi_ccu_clock_get_parent(&state, clk);
}
static u_int clk_get_rate(struct clk *clk)
{
	return clk == &hosc ? 24000000 : sunxi_ccu_clock_get_rate(&state, clk);
}
static int clk_set_rate(struct clk *clk, u_int rate)
{
	return sunxi_ccu_clock_set_rate(&state, clk, rate);
}
static int clk_enable(struct clk *clk)
{
	return clk == &hosc ? 0 : sunxi_ccu_clock_enable(&state, clk);
}
static u_int clk_round_rate(struct clk *clk, u_int rate)
{
	return sunxi_ccu_clock_round_rate(&state, clk, rate);
}
static int clk_set_parent(struct clk *clk, struct clk *parent)
{
	return sunxi_ccu_clock_set_parent(&state, clk, parent);
}
#include "sunxi_ccu_gate.c"
#include "sunxi_ccu_div.c"
#ifdef __clang__
/* Unrelated upstream NM code uses abs() on an unsigned subtraction. */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wabsolute-value"
#endif
#include "sunxi_ccu_nm.c"
#ifdef __clang__
#pragma clang diagnostic pop
#endif
#include "sunxi_ccu_nkmp.c"
#include "sunxi_ccu_fixed_factor.c"
#include "sun60i_a733_ccu.c"
#define CHECK(c) do { assert(c); checks++; } while (0)
#define REG(r) registers[(r) / 4]
#define CLOCK(id) (&state.sc_clks[(id)].base)

static void
reset(void)
{
	struct fdt_attach_args args = { 0 };
	memset(registers, 0, sizeof(registers));
	writes = barriers = 0;
	reject_write = missing_hosc = false;
	sun60i_a733_ccu_attach(NULL, &state, &args);
	CHECK(writes == 0);
	/* Firmware peripheral VCO=2400 MHz, 2x=/2, 800M=/3, 480M=/5. */
	REG(PLL_PERIPH0_CTRL_REG) = __BIT(31) | (99 << 8) |
	    (1 << 20) | (2 << 16) | (4 << 2);
}

int
main(void)
{
	const u_int gate_ids[] = { A733_CLK_AHB_NPU, A733_CLK_AHB_GPU0,
	    A733_CLK_MBUS_GPU0, A733_CLK_MBUS_NPU, A733_CLK_BUS_NPU,
	    A733_CLK_BUS_GPU0 };
	const u_int reset_ids[] = { A733_RST_BUS_NPU_CORE,
	    A733_RST_BUS_NPU_AXI, A733_RST_BUS_NPU_AHB,
	    A733_RST_BUS_NPU_SRAM, A733_RST_BUS_GPU0 };
	const u_int divs[] = { 1, 2, 4, 8, 16 };
	const u_int raw[] = { 0, 8, 12, 14, 15 };
	uint32_t val, old;
	u_int before;

	reset();
	CHECK(sun60i_a733_ccu_match(NULL, NULL,
	    &(struct fdt_attach_args){ 0 }) != 0);
	for (u_int i = 0; i < __arraycount(gate_ids); i++) {
		struct sunxi_ccu_clk *c = &state.sc_clks[gate_ids[i]];
		REG(c->u.gate.reg) = 0xa510551a & ~c->u.gate.mask;
		old = REG(c->u.gate.reg);
		CHECK(clk_enable(&c->base) == 0);
		CHECK(REG(c->u.gate.reg) == (old | c->u.gate.mask));
		CHECK(sunxi_ccu_clock_disable(&state, &c->base) == 0);
		CHECK(REG(c->u.gate.reg) == old);
		CHECK(c->base.flags == 0);
	}
	for (u_int i = 0; i < __arraycount(reset_ids); i++) {
		struct sunxi_ccu_reset *r = &state.sc_resets[reset_ids[i]];
		CHECK(r->reg == (i == 4 ? 0xb24 : 0xb04));
		CHECK(r->mask == __BIT(i == 4 ? 16 : 16 + i));
		REG(r->reg) = 0xa5a5a5a5;
		CHECK(sunxi_ccu_reset_assert(&state, r) == 0);
		CHECK(REG(r->reg) == (0xa5a5a5a5 & ~r->mask));
		CHECK(sunxi_ccu_reset_deassert(&state, r) == 0);
		CHECK(REG(r->reg) == (0xa5a5a5a5 | r->mask));
	}
	reset();
	for (u_int sel = 6; sel < 8; sel++) {
		REG(GPU0_CLK_REG) = sel << 24;
		CHECK(clk_get_parent(CLOCK(A733_CLK_GPU0)) == NULL);
		CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 0);
		CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == ENXIO);
	}
	REG(NPU_CLK_REG) = 7 << 24;
	CHECK(clk_get_parent(CLOCK(A733_CLK_NPU)) == NULL);
	CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == ENXIO);
	CHECK(writes == 0);
	for (u_int sel = 4; sel < 7; sel++) {
		REG(NPU_CLK_REG) = sel << 24;
		CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == ENXIO);
		CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 100000000) == ENXIO);
	}
	CHECK(writes == 0);

	REG(GPU0_CLK_REG) = 0x00500000;
	CHECK(sunxi_ccu_clock_set_parent(&state, CLOCK(A733_CLK_GPU0),
	    CLOCK(A733_CLK_PLL_PERIPH0_800M)) == 0);
	CHECK((last_write & GPU_CLK_UPDATE) != 0);
	CHECK(REG(GPU0_CLK_REG) == 0x01500000);
	for (u_int i = 0; i < __arraycount(divs); i++) {
		CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 800000000 / divs[i]) == 0);
		CHECK((REG(GPU0_CLK_REG) & 15) == raw[i]);
		CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == 0);
		CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 800000000 / divs[i]);
		CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 800000000 / divs[i]) == 0);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(A733_CLK_GPU0)) == 0);
	}
	before = writes;
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 0) == EINVAL);
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 123456789) == ERANGE);
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), UINT_MAX) == ERANGE);
	CHECK(writes == before);
	CHECK(clk_enable(CLOCK(A733_CLK_GPU0)) == 0);
	before = writes;
	CHECK(clk_set_rate(CLOCK(A733_CLK_GPU0), 400000000) == EBUSY);
	CHECK(sunxi_ccu_clock_set_parent(&state, CLOCK(A733_CLK_GPU0),
	    CLOCK(A733_CLK_PLL_PERIPH0_600M)) == EBUSY);
	CHECK(writes == before);
	/* Firmware may use fractional GPU M: report it without integer rounding. */
	REG(GPU0_CLK_REG) = __BIT(31) | (1 << 24) | 3;
	CHECK(clk_get_rate(CLOCK(A733_CLK_GPU0)) == 650000000);

	REG(NPU_CLK_REG) = 0x00500000 | (2 << 24);
	for (u_int m = 1; m <= 32; m *= 2) {
		CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 600000000 / m) == 0);
		CHECK((REG(NPU_CLK_REG) & 31) == m - 1);
		CHECK((REG(NPU_CLK_REG) & ~31U) == 0x02500000);
		CHECK(clk_enable(CLOCK(A733_CLK_NPU)) == 0);
		CHECK(clk_get_rate(CLOCK(A733_CLK_NPU)) == 600000000 / m);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(A733_CLK_NPU)) == 0);
	}
	reject_write = true;
	CHECK(clk_set_rate(CLOCK(A733_CLK_NPU), 300000000) == EIO);
	CHECK(clk_enable(CLOCK(A733_CLK_BUS_NPU)) == EIO);
	reject_write = false;

	reset();
	val = ACCEL_PLL_ENABLE | ACCEL_PLL_LDO | ACCEL_PLL_LOCK_ENABLE |
	    ACCEL_PLL_LOCK | ACCEL_PLL_OUTPUT | (99 << 8) | (1 << 20);
	for (u_int id = A733_CLK_PLL_GPU0; id <= A733_CLK_PLL_NPU;
	    id += A733_CLK_PLL_NPU - A733_CLK_PLL_GPU0) {
		bus_size_t r = state.sc_clks[id].u.nkmp.reg;
		REG(r) = val;
		CHECK(clk_get_rate(CLOCK(id)) == 1200000000);
		CHECK(clk_enable(CLOCK(id)) == 0);
		REG(r) |= ACCEL_PLL_P;
		CHECK(clk_get_rate(CLOCK(id)) == 600000000);
		CHECK(clk_set_rate(CLOCK(id), 300000000) == ENXIO);
		CHECK(sunxi_ccu_clock_disable(&state, CLOCK(id)) == EBUSY);
		for (u_int bit = 27; bit <= 31; bit++) {
			REG(r) = val & ~__BIT(bit);
			CHECK(clk_get_rate(CLOCK(id)) == 0);
			CHECK(clk_enable(CLOCK(id)) == ENXIO);
		}
		REG(r) = val;
		REG(r + 8) = __BIT(31);
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r + 8) = 0;
		REG(r + 12) = __BIT(27);
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r + 12) = 0;
		REG(r) = val & ~ACCEL_PLL_N;
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		REG(r) = val;
		missing_hosc = true;
		CHECK(clk_enable(CLOCK(id)) == ENXIO);
		missing_hosc = false;
	}
	CHECK(writes == 0 && barriers == 0);
	printf("A733 accelerator clocks: %u production checks passed\n", checks);
	return 0;
}
