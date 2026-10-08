/* Origin: EmberBSD; AI-assisted tests executing the production FDT and PCK600 code. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/queue.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

typedef unsigned int u_int;
typedef void *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef unsigned int bus_space_handle_t;
typedef size_t bus_size_t;
typedef uintptr_t bus_addr_t;
typedef struct { bool initialized, held; } kmutex_t;
#ifndef __BIT
#define __BIT(n) (UINT32_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) ((__BIT((hi) + 1) - 1) & ~(__BIT(lo) - 1))
#endif
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define KASSERT(c) assert(c)
#define BUS_SPACE_BARRIER_READ 1
#define BUS_SPACE_BARRIER_WRITE 2
#define MUTEX_DEFAULT 0
#define IPL_NONE 0
#define KM_SLEEP 0
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) (d)
#define aprint_debug_dev(...) ((void)0)
#define aprint_error_dev(...) ((void)0)
#define aprint_error(...) ((void)0)
#define aprint_normal(...) ((void)0)
#define aprint_naive(...) ((void)0)
static uint32_t
swap32(uint32_t v)
{
	const uint8_t probe[] = { 1, 0, 0, 0 };
	uint32_t native;
	memcpy(&native, probe, sizeof(native));
	return native == 1 ? __builtin_bswap32(v) : v;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif

struct clk { int enabled; };
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
#include "power-interface.h"

static uint32_t registers[0xb000 / 4], specifier[8];
static uint32_t binding_cells;
static int prop_len, getprop_error, reg_error, map_error, clock_error;
static int register_error, missing_clock;
static bus_size_t binding_size;
static unsigned int reads, writes, barriers, mapped, unmapped, delay_us;
static unsigned int poll_reads, complete_after, deny_after, policy_written;
static bus_size_t fail_write;
static int legacy_calls, checked_calls, checked_error;
static int checked_get_calls;
static bool checked_state;
static kmutex_t *required_read_lock;
static bus_size_t unstable_reg;
static unsigned int unstable_read, state_checks;
static struct clk clock_fixture;

static void *kmem_alloc(size_t n, int flags) { return calloc(1, n); }
static int
of_getprop_uint32(int node, const char *name, uint32_t *v)
{
	*v = binding_cells;
	return getprop_error;
}
static const void *
fdtbus_get_prop(int node, const char *name, int *len)
{
	*len = prop_len;
	return prop_len == -1 ? NULL : specifier;
}
static int fdtbus_get_phandle_from_native(int v) { return v; }
#include "power-fdt.h"

static int
fixture_register(device_t dev, int node,
    const struct fdtbus_powerdomain_controller_func *funcs)
{
	return register_error ? register_error :
	    fdtbus_register_powerdomain_controller(dev, node, funcs);
}
#define fdtbus_register_powerdomain_controller fixture_register
static int
fdtbus_get_reg(int node, u_int index, bus_addr_t *addr, bus_size_t *size)
{
	*addr = 0x07060000;
	*size = binding_size;
	return reg_error;
}
static struct clk *
fdtbus_clock_get_index(int node, u_int index)
{
	return missing_clock ? NULL : &clock_fixture;
}
static int clk_enable(struct clk *c)
{
	if (!clock_error) c->enabled++;
	return clock_error;
}
static int clk_disable(struct clk *c) { return --c->enabled; }
static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	if (!map_error) mapped++;
	*handle = 0;
	return map_error;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	unmapped++;
}
static uint32_t
bus_space_read_4(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t off)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	if (required_read_lock != NULL)
		assert(required_read_lock->held);
	reads++;
	if (reads == unstable_read && off == unstable_reg)
		registers[off / 4] ^= __BIT(3);
	if (policy_written && off % 0x1000 == 8) {
		poll_reads++;
		if (complete_after && poll_reads >= complete_after)
			registers[off / 4] = registers[(off - 8) / 4] & 0xf;
		if (deny_after && poll_reads >= deny_after)
			registers[(off - 8) / 4] = registers[off / 4];
	}
	return registers[off / 4];
}
static void
bus_space_write_4(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, uint32_t value)
{
	assert(off < sizeof(registers) && off % 4 == 0);
	writes++;
	if (off == fail_write) return;
	registers[off / 4] = value;
	if (off % 0x1000 == 0) policy_written++;
}
static void
bus_space_barrier(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t off, bus_size_t size, int flags)
{
	assert(flags == (BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE));
	barriers++;
}
static void delay(unsigned int us) { delay_us += us; }
static void mutex_init(kmutex_t *m, int type, int ipl) { m->initialized = true; }
static void mutex_destroy(kmutex_t *m) { assert(!m->held); m->initialized = false; }
static void mutex_enter(kmutex_t *m) { assert(m->initialized && !m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }
static bool mutex_owned(kmutex_t *m) { return m->held; }
static int
of_compatible_match(int node, const struct device_compatible_entry *data)
{
	return strcmp(data[0].compat, "allwinner,sun60i-a733-pck-600") == 0;
}
#include "power-driver.h"
#undef fdtbus_register_powerdomain_controller

static struct sun60i_pck600_softc sc;
static struct fdt_attach_args attach_args = { .faa_phandle = 1 };
static unsigned int cases;
static void
reset_fixture(void)
{
	struct fdtbus_powerdomain_controller *pdc;
	while ((pdc = LIST_FIRST(&fdtbus_powerdomain_controllers)) != NULL) {
		LIST_REMOVE(pdc, pdc_next);
		free(pdc);
	}
	memset(&sc, 0, sizeof(sc));
	memset(registers, 0, sizeof(registers));
	memset(specifier, 0, sizeof(specifier));
	binding_cells = 1;
	binding_size = sizeof(registers);
	getprop_error = reg_error = map_error = clock_error = 0;
	register_error = missing_clock = 0;
	reads = writes = barriers = mapped = unmapped = delay_us = 0;
	poll_reads = complete_after = deny_after = policy_written = 0;
	clock_fixture.enabled = 0;
	legacy_calls = checked_calls = checked_error = 0;
	checked_get_calls = 0;
	checked_state = false;
	required_read_lock = NULL;
	unstable_reg = SIZE_MAX;
	unstable_read = 0;
	fail_write = SIZE_MAX;
	specifier[0] = swap32(1);
	specifier[1] = swap32(4);
	prop_len = 8;
	cases++;
}
static void
attach_controller(void)
{
	sun60i_pck600_attach(NULL, &sc, &attach_args);
	assert(mapped == 1 && unmapped == 0 && clock_fixture.enabled == 1);
	assert(LIST_FIRST(&fdtbus_powerdomain_controllers) != NULL);
	assert(reads == 0 && writes == 0); /* attach leaves every domain alone */
}
static void
legacy_set(device_t dev, const uint32_t *data, bool enable)
{
	legacy_calls++;
}
static int
checked_set(device_t dev, const uint32_t *data, bool enable)
{
	checked_calls++;
	return checked_error;
}

static int
checked_get(device_t dev, const uint32_t *data, bool *enabled)
{
	checked_get_calls++;
	*enabled = checked_state;
	return checked_error;
}

static void
expect_state(int index, int error, bool state)
{
	bool enabled;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		enabled = i != 0;
		assert(fdtbus_powerdomain_is_enabled_index(2, index,
		    &enabled) == error);
		assert(enabled == (error == 0 ? state : i != 0));
		assert(writes == 0 && barriers == 0 && delay_us == 0);
		assert(!sc.sc_lock.held);
		state_checks++;
	}
}

static void
test_read_state(void)
{
	static const struct fdtbus_powerdomain_controller_func legacy = {
		.pdc_enable = legacy_set,
	}, checked = { .pdc_set = checked_set, .pdc_get = checked_get };
	const int errors[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP };
	u_int id, mode, lock, i;
	bool enabled = true;

	reset_fixture();
	expect_state(0, ENXIO, false);
	assert(fdtbus_powerdomain_is_enabled_index(2, 0, NULL) == EINVAL);
	expect_state(-1, EINVAL, false);
	for (i = 0; i < 4; i++) {
		prop_len = (int[]){ -1, 0, 3, 4 }[i];
		expect_state(0, EINVAL, false);
	}
	prop_len = 8;
	binding_cells = UINT_MAX;
	expect_state(0, EINVAL, false);
	binding_cells = 1;
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &legacy) == 0);
	expect_state(0, EOPNOTSUPP, false);
	expect_state(1, ENOENT, false);
	assert(legacy_calls == 0 && checked_calls == 0 && reads == 0);

	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &checked) == 0);
	for (i = 0; i < __arraycount(errors); i++) {
		checked_error = errors[i];
		checked_state = i % 2 != 0;
		expect_state(0, errors[i], false);
	}
	checked_error = 0;
	for (i = 0; i < 2; i++) {
		checked_state = i != 0;
		expect_state(0, 0, checked_state);
	}
	assert(legacy_calls == 0 && checked_calls == 0 && reads == 0);
	/* Skip an unregistered earlier domain using its binding metadata. */
	specifier[0] = swap32(3);
	specifier[2] = swap32(1);
	specifier[3] = swap32(5);
	prop_len = 16;
	i = checked_get_calls;
	expect_state(0, ENXIO, false);
	assert(checked_get_calls == (int)i);
	expect_state(1, 0, true);
	/* A provider registered later makes the same strict query usable. */
	assert(fdtbus_register_powerdomain_controller(NULL, 3, &checked) == 0);
	expect_state(0, 0, true);
	prop_len = 12;
	expect_state(1, EINVAL, false);

	reset_fixture();
	binding_cells = 0;
	prop_len = 4;
	assert(fdtbus_register_powerdomain_controller(NULL, 1, &checked) == 0);
	expect_state(0, 0, false);

	for (id = 0; id < PCK600_NDOMAINS; id++) {
		const bus_size_t base = id * PCK600_DOMAIN_SIZE;

		reset_fixture();
		attach_controller();
		required_read_lock = &sc.sc_lock;
		specifier[1] = swap32(id);
		for (mode = 0; mode < 16; mode++) {
			for (lock = 0; lock < 4; lock++) {
				registers[base / 4] = mode |
				    (lock & 1 ? PCK600_LOCK : 0);
				registers[(base + 8) / 4] = mode |
				    (lock & 2 ? PCK600_LOCK : 0);
				expect_state(0, mode == PCK600_ON ||
				    mode == PCK600_OFF ? 0 : EOPNOTSUPP,
				    mode == PCK600_ON);
			}
		}
		registers[base / 4] = PCK600_ON;
		registers[(base + 8) / 4] = PCK600_OFF;
		expect_state(0, EBUSY, false);
		registers[(base + 8) / 4] = PCK600_ON;
		for (i = 0; i < 4; i++) {
			const bus_size_t reg = base + (i < 2 ? 0 : 8);
			const uint32_t bit = __BIT(i % 2 ? 24 : 8);

			registers[reg / 4] |= bit;
			expect_state(0, EOPNOTSUPP, false);
			registers[reg / 4] &= ~bit;
		}
		registers[(base + 4) / 4] = PCK600_EMULATION;
		expect_state(0, EOPNOTSUPP, false);
		registers[(base + 4) / 4] = 0;
		for (i = 0; i < 3; i++) {
			unstable_reg = base + i * 4;
			unstable_read = i + 4;
			reads = 0;
			enabled = true;
			assert(fdtbus_powerdomain_is_enabled_index(2, 0,
			    &enabled) == EBUSY && enabled);
			assert(reads == unstable_read && writes == 0);
			registers[unstable_reg / 4] ^= __BIT(3);
			unstable_read = 0;
			state_checks++;
		}
		assert(!sc.sc_failed[id]);
		expect_state(0, 0, true);
		sc.sc_failed[id] = true;
		reads = 0;
		expect_state(0, EIO, false);
		assert(reads == 0 && sc.sc_failed[id]);
	}
	reset_fixture();
	attach_controller();
	specifier[1] = swap32(PCK600_NDOMAINS);
	expect_state(0, EINVAL, false);
	specifier[1] = swap32(UINT_MAX);
	expect_state(0, EINVAL, false);
	assert(reads == 0);
	printf("PASS: %u PCK600/FDT read-only state checks\n", state_checks);
}

int
main(void)
{
	static const struct fdtbus_powerdomain_controller_func legacy = {
		.pdc_enable = legacy_set,
	}, checked = { .pdc_enable = legacy_set, .pdc_set = checked_set }, empty = {0};
	const bus_size_t base = 4 * 0x1000;
	unsigned int old_writes, old_reads;

	test_read_state();
	cases = 0;

	reset_fixture();
	assert(sun60i_pck600_match(NULL, NULL, &attach_args));
	attach_controller();
	complete_after = 3;
	assert(fdtbus_powerdomain_enable(2) == 0);
	assert(registers[(base + 8) / 4] == 8 && delay_us == 20);
	assert(writes == 6 && barriers == writes);
	assert(registers[(base + 0x170) / 4] == 0x1f1f1f);
	assert(registers[(base + 0x174) / 4] == 0x1f1f);
	assert(registers[(base + 0xc00) / 4] == 0x08080808);
	assert(registers[(base + 0xc04) / 4] == 0x0808);
	assert(registers[(base + 0xc10) / 4] == 8);
	old_writes = writes;
	assert(fdtbus_powerdomain_enable(2) == 0 && writes == old_writes);
	assert(fdtbus_powerdomain_disable(2) == 0);
	assert(registers[(base + 8) / 4] == 0);

	reset_fixture(); attach_controller();
	registers[base / 4] = registers[(base + 8) / 4] = 0x30000;
	complete_after = 1;
	assert(fdtbus_powerdomain_enable(2) == 0);
	assert(registers[base / 4] == 0x30008); /* preserve operating mode */

	reset_fixture(); attach_controller();
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT);
	assert(delay_us == 10000 && poll_reads == 1001 && sc.sc_failed[4]);
	old_reads = reads; old_writes = writes;
	assert(fdtbus_powerdomain_enable(2) == EIO);
	assert(reads == old_reads && writes == old_writes && !sc.sc_lock.held);

	reset_fixture(); attach_controller(); deny_after = 2;
	assert(fdtbus_powerdomain_enable(2) == EIO);
	assert(delay_us == 20 && sc.sc_failed[4]);

	for (unsigned int n = 0; n < 6; n++) {
		static const bus_size_t offsets[] = { 0x170, 0x174, 0xc00, 0xc04, 0xc10, 0 };
		reset_fixture(); attach_controller(); fail_write = base + offsets[n];
		assert(fdtbus_powerdomain_enable(2) == EIO);
		assert(writes == n + 1 && delay_us == 0 && sc.sc_failed[4]);
	}
	for (unsigned int n = 0; n < 5; n++) {
		reset_fixture(); attach_controller();
		if (n == 0) registers[base / 4] = __BIT(8);
		if (n == 1) registers[(base + 8) / 4] = __BIT(24);
		if (n == 2) registers[(base + 8) / 4] = __BIT(12);
		if (n == 3) registers[(base + 4) / 4] = 1;
		if (n == 4) registers[base / 4] = 8;
		assert(fdtbus_powerdomain_enable(2) == (n == 4 ? EBUSY : EOPNOTSUPP));
		assert(writes == 0 && !sc.sc_failed[4]);
	}
	reset_fixture(); attach_controller(); specifier[1] = swap32(11);
	assert(fdtbus_powerdomain_enable(2) == EINVAL && reads == 0);
	reset_fixture(); attach_controller(); specifier[1] = swap32(UINT32_MAX);
	assert(fdtbus_powerdomain_enable(2) == EINVAL && reads == 0);
	reset_fixture(); attach_controller(); specifier[1] = swap32(6);
	assert(fdtbus_powerdomain_disable(2) == EOPNOTSUPP && reads == 0);
	complete_after = 1;
	assert(fdtbus_powerdomain_enable(2) == 0);

	for (unsigned int n = 0; n < 8; n++) {
		reset_fixture();
		if (n == 0) binding_cells = 0;
		if (n == 1) binding_size--;
		if (n == 2) getprop_error = EINVAL;
		if (n == 3) reg_error = EINVAL;
		if (n == 4) missing_clock = 1;
		if (n == 5) map_error = EIO;
		if (n == 6) clock_error = EIO;
		if (n == 7) register_error = EIO;
		sun60i_pck600_attach(NULL, &sc, &attach_args);
		assert(LIST_FIRST(&fdtbus_powerdomain_controllers) == NULL);
		assert(clock_fixture.enabled == 0 && mapped == unmapped);
		assert(writes == 0 && reads == 0 && !sc.sc_lock.initialized);
	}

	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &legacy) == 0);
	assert(fdtbus_powerdomain_enable_index(2, 0) == 0 && legacy_calls == 1);
	assert(fdtbus_powerdomain_disable(2) == 0 && legacy_calls == 2);
	reset_fixture(); checked_error = ETIMEDOUT;
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT);
	assert(checked_calls == 1 && legacy_calls == 0);
	specifier[2] = swap32(1); specifier[3] = swap32(5); prop_len = 16;
	assert(fdtbus_powerdomain_enable(2) == ETIMEDOUT && checked_calls == 2);
	checked_error = 0;
	assert(fdtbus_powerdomain_enable(2) == 0 && checked_calls == 4);
	assert(fdtbus_powerdomain_enable_index(2, 1) == 0 && checked_calls == 5);

	for (int n = -1; n <= 7; n++) {
		if (n == 4) continue; /* one phandle without its cell is tested too */
		reset_fixture();
		assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
		prop_len = n;
		assert(fdtbus_powerdomain_enable(2) == EINVAL && checked_calls == 0);
	}
	reset_fixture();
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == 0);
	prop_len = 4;
	assert(fdtbus_powerdomain_enable(2) == EINVAL && checked_calls == 0);
	reset_fixture();
	assert(fdtbus_powerdomain_enable(2) == ENXIO);
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &empty) == EINVAL);
	binding_cells = UINT32_MAX;
	assert(fdtbus_register_powerdomain_controller(&sc, 1, &checked) == EINVAL);
	printf("PASS: %u A733 PCK600/FDT production regression scenarios\n", cases);
	reset_fixture();
	return 0;
}
