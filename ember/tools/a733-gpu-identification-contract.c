/* Origin: EmberBSD; causal checks of the production GPU identity consumer. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <sys/queue.h>

typedef unsigned int u_int;
typedef struct device *device_t;
typedef void *cfdata_t;
typedef void *bus_space_tag_t;
typedef uintptr_t bus_space_handle_t;
typedef uintptr_t bus_addr_t;
typedef size_t bus_size_t;
#ifndef __arraycount
#define __arraycount(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define CFATTACH_DECL_NEW(n, s, m, a, d, r)
#define DEVICE_COMPAT_EOL { .compat = NULL }
#define device_private(d) ((d)->dv_private)
#define aprint_naive(...) ((void)0)
static void aprint_normal(const char *fmt, ...) {}
static void aprint_debug(const char *fmt, ...) {}
#define printf_nolog(...) ((void)0)
#define KASSERT(c) assert(c)
#define KERNEL_LOCKED_P() true
#define KERNEL_LOCK(n, l) ((void)0)
#define KERNEL_UNLOCK_ONE(l) ((void)0)
#define KM_SLEEP 0
#define AB_QUIET 1
#define AB_SILENT 2
#define AB_VERBOSE 4
#define mstohz(n) (n)
#define device_xname(d) "fixture"
#ifndef __printflike
#define __printflike(a, b)
#endif
struct device {
	device_t dv_parent;
	void *dv_private;
	unsigned int dv_pending;
	TAILQ_ENTRY(device) dv_pending_list;
};
#include "autoconf-structs.h"
TAILQ_HEAD(deferred_config_head, deferred_config);
static struct deferred_config_head deferred_config_queue =
    TAILQ_HEAD_INITIALIZER(deferred_config_queue);
static TAILQ_HEAD(, finalize_hook) config_finalize_list =
    TAILQ_HEAD_INITIALIZER(config_finalize_list);
static TAILQ_HEAD(, device) config_pending =
    TAILQ_HEAD_INITIALIZER(config_pending);
static int config_finalize_done, config_misc_lock, config_misc_cv;
static int config_do_twiddle, boothowto;
static const unsigned int hz = 100;
struct pdevinit { void (*pdev_attach)(int); int pdev_count; };
struct pdevinit pdevinit[] = { { NULL, 0 } };
static struct device root_device, soc_device, gpu_device;

static unsigned getticks(void) { return 0; }
static int aprint_get_error_count(void) { return 0; }
static void *kmem_alloc(size_t size, int flags) { return calloc(1, size); }
static void kmem_free(void *ptr, size_t size) { free(ptr); }
static void panic(const char *msg) { assert(!msg); }
static void mutex_enter(int *lock) { assert(!*lock); *lock = 1; }
static void mutex_exit(int *lock) { assert(*lock); *lock = 0; }
static int
cv_timedwait(int *cv, int *lock, int ticks)
{
	/* Turn a stranded pending device into a bounded, causal failure. */
	assert(!"configuration would wait for an orphaned callback");
	return 0;
}
static void
config_pending_incr(device_t dev)
{
	if (dev->dv_pending++ == 0)
		TAILQ_INSERT_TAIL(&config_pending, dev, dv_pending_list);
}
static void
config_pending_decr(device_t dev)
{
	assert(dev->dv_pending > 0);
	if (--dev->dv_pending == 0)
		TAILQ_REMOVE(&config_pending, dev, dv_pending_list);
}
struct fdt_attach_args { int faa_phandle; bus_space_tag_t faa_bst; };
struct device_compatible_entry { const char *compat; };
struct fdtbus_regulator { int acquired; };
struct clk { int acquired; };
#include "sun60i_a733_ccu.h"

static uint32_t pd[2], clocks[2], power_cells, clock_cells;
static int pd_len, clock_len, managed_len, supply_len;
static bool bad_power_compat, bad_pmic, bad_supply;
static bool missing_supply, missing_clock;
static int reg_error, state_error, voltage_error, power_error, ready_error;
static int map_error, peek_error, match_result;
static bus_addr_t binding_addr;
static bus_size_t binding_size;
static bool supply_on, power_on;
static u_int voltage, core_hz, bus_hz;
static uint64_t pbvnc;
static struct fdtbus_regulator regulator;
static struct clk clock_fixture;
static unsigned int maps, peeks, unmaps, releases, clock_puts, checks;
static char events[128], normal_output[256], error_output[256];
static size_t nevents;

static void
record(char event)
{
	assert(nevents + 1 < sizeof(events));
	events[nevents++] = event;
	events[nevents] = '\0';
}

static uint32_t
swap32(uint32_t value)
{
	const uint32_t one = 1;

	return *(const unsigned char *)&one == 1 ? __builtin_bswap32(value) : value;
}
#ifndef be32toh
#define be32toh(v) swap32(v)
#endif

static int
OF_getproplen(int phandle, const char *name)
{
	if (strcmp(name, "netbsd,consumer-managed-power") == 0)
		return managed_len;
	assert(strcmp(name, "gpu-supply") == 0);
	return supply_len;
}

static int
fdtbus_get_reg(int phandle, u_int index, bus_addr_t *addr, bus_size_t *size)
{
	assert(index == 0);
	*addr = binding_addr;
	*size = binding_size;
	return reg_error;
}

static const void *
fdtbus_get_prop(int phandle, const char *name, int *len)
{
	if (strcmp(name, "power-domains") == 0) {
		*len = pd_len;
		return pd_len < 0 ? NULL : pd;
	}
	assert(strcmp(name, "clocks") == 0);
	*len = clock_len;
	return clock_len < 0 ? NULL : clocks;
}

static int fdtbus_get_phandle_from_native(int phandle) { return phandle; }
static int
of_getprop_uint32(int phandle, const char *name, uint32_t *value)
{
	if (strcmp(name, "#power-domain-cells") == 0) {
		*value = power_cells;
		return phandle == 2 ? 0 : -1;
	}
	assert(strcmp(name, "#clock-cells") == 0);
	*value = clock_cells;
	return phandle == 3 ? 0 : -1;
}

static int
of_compatible_match(int phandle, const struct device_compatible_entry *compat)
{
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-gpu") == 0)
		return match_result;
	if (strcmp(compat[0].compat, "allwinner,sun60i-a733-pck-600") == 0)
		return phandle == 2 && !bad_power_compat;
	assert(strcmp(compat[0].compat, "x-powers,axp8191") == 0);
	return phandle == 6 && !bad_pmic;
}

static int
fdtbus_get_phandle(int phandle, const char *name)
{
	assert(strcmp(name, "gpu-supply") == 0);
	return 4;
}
static const char *
fdtbus_get_string(int phandle, const char *name)
{
	assert(phandle == 4 && strcmp(name, "name") == 0);
	return bad_supply ? "dcdc2" : "dcdc4";
}
static int OF_parent(int phandle) { return phandle + 1; }

static struct fdtbus_regulator *
fdtbus_regulator_acquire(int phandle, const char *name)
{
	record('A');
	assert(!regulator.acquired);
	if (missing_supply)
		return NULL;
	regulator.acquired = 1;
	return &regulator;
}
static int
fdtbus_regulator_is_enabled(struct fdtbus_regulator *reg, bool *enabled)
{
	assert(reg->acquired);
	record('S');
	*enabled = supply_on;
	return state_error;
}
static int
fdtbus_regulator_get_voltage(struct fdtbus_regulator *reg, u_int *uvol)
{
	assert(reg->acquired);
	record('V');
	*uvol = voltage;
	return voltage_error;
}
static int
fdtbus_powerdomain_is_enabled_index(int phandle, int index, bool *enabled)
{
	assert(index == 0);
	record('P');
	*enabled = power_on;
	return power_error;
}
static struct clk *
fdtbus_clock_get_index(int phandle, u_int index)
{
	assert(index == 0 && !clock_fixture.acquired);
	record('C');
	if (missing_clock)
		return NULL;
	clock_fixture.acquired = 1;
	return &clock_fixture;
}
int
sun60i_a733_ccu_gpu_ready(struct clk *clock, u_int *core, u_int *bus)
{
	assert(clock->acquired);
	record('Q');
	*core = core_hz;
	*bus = bus_hz;
	return ready_error;
}
static int
bus_space_map(bus_space_tag_t tag, bus_addr_t addr, bus_size_t size,
    int flags, bus_space_handle_t *handle)
{
	assert(addr == 0x01800000 && size == 0x28 && flags == 0);
	record('M');
	maps++;
	*handle = 0x1234;
	return map_error;
}
static int
bus_space_peek_8(bus_space_tag_t tag, bus_space_handle_t handle,
    bus_size_t offset, uint64_t *value)
{
	assert(handle == 0x1234 && offset == 0x20);
	record('I');
	peeks++;
	*value = pbvnc;
	return peek_error;
}
static void
bus_space_unmap(bus_space_tag_t tag, bus_space_handle_t handle, bus_size_t size)
{
	assert(handle == 0x1234 && size == 0x28);
	record('U');
	unmaps++;
}
static void
clk_put(struct clk *clock)
{
	assert(clock->acquired);
	clock->acquired = 0;
	clock_puts++;
	record('c');
}
static void
fdtbus_regulator_release(struct fdtbus_regulator *reg)
{
	assert(reg->acquired);
	reg->acquired = 0;
	releases++;
	record('s');
}
static void
aprint_normal_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(normal_output, sizeof(normal_output), fmt, ap);
	va_end(ap);
}
static void
aprint_error_dev(device_t dev, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(error_output, sizeof(error_output), fmt, ap);
	va_end(ap);
}
#include "autoconf.h"
#include "driver.h"
#define CHECK(c) do { assert(c); checks++; } while (0)
static struct sun60i_gpu_softc sc;

static void
reset(void)
{
	assert(!regulator.acquired && !clock_fixture.acquired);
	assert(TAILQ_EMPTY(&config_pending));
	assert(TAILQ_EMPTY(&deferred_config_queue));
	assert(TAILQ_EMPTY(&config_finalize_list));
	memset(&sc, 0, sizeof(sc));
	root_device = (struct device){ 0 };
	soc_device = (struct device){ .dv_parent = &root_device };
	gpu_device = (struct device){ .dv_parent = &soc_device, .dv_private = &sc };
	config_finalize_done = 0;
	sc.sc_dev = &gpu_device;
	sc.sc_phandle = 1;
	pd[0] = swap32(2);
	pd[1] = swap32(5);
	clocks[0] = swap32(3);
	clocks[1] = swap32(A733_CLK_GPU0);
	power_cells = clock_cells = 1;
	pd_len = clock_len = 8;
	managed_len = 0;
	supply_len = 4;
	bad_power_compat = bad_pmic = bad_supply = false;
	missing_supply = missing_clock = false;
	reg_error = state_error = voltage_error = power_error = ready_error = 0;
	map_error = peek_error = 0;
	match_result = 1;
	binding_addr = 0x01800000;
	binding_size = 0x8ffff;
	supply_on = power_on = true;
	voltage = 800000;
	core_hz = 400000000;
	bus_hz = 200000000;
	pbvnc = UINT64_C(0x00240038006800b7);
	maps = peeks = unmaps = releases = clock_puts = 0;
	nevents = 0;
	events[0] = normal_output[0] = error_output[0] = '\0';
}

static unsigned legacy_callbacks, extra_passes;
static void legacy_probe(device_t dev) { legacy_callbacks++; }
static int request_extra_pass(device_t dev) { return extra_passes++ == 0; }

static void
unavailable(int error)
{
	CHECK(sun60i_gpu_identify(&sc) == error);
	CHECK(maps == 0 && peeks == 0 && unmaps == 0);
	CHECK(!sc.sc_have_id);
	CHECK(!regulator.acquired && !clock_fixture.acquired);
}

int
main(void)
{
	const struct fdt_attach_args faa = { .faa_phandle = 1 };
	const int failures[] = { ENXIO, EIO, ETIMEDOUT, EOPNOTSUPP, EINVAL };
	const int bad_lengths[] = { -1, 0, 3, 4, 7, 12 };
	const u_int bad_rates[] = { 0, 300000000, 401000000, 800000000 };
	const u_int bad_volts[] = { 0, 500000, 799999, 810000 };
	const uint64_t ids[] = { 0, UINT64_MAX, UINT64_C(0x00240037006800b7),
	    UINT64_C(0x00230038006800b7), UINT64_C(0x00240038006700b7),
	    UINT64_C(0x00240038006800b6) };

	reset();
	/* /soc finished before a later root FDT pass attaches its GPU child. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	config_defer(&gpu_device, legacy_probe);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(legacy_callbacks == 0 && gpu_device.dv_pending == 1);
	CHECK(!TAILQ_EMPTY(&config_pending));
	/* An artificial second /soc drain is needed to clean the old queue. */
	config_process_deferred(&deferred_config_queue, &soc_device);
	CHECK(legacy_callbacks == 1 && TAILQ_EMPTY(&config_pending));
	reset();
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 1);
	match_result = 0;
	CHECK(sun60i_gpu_match(NULL, NULL, (void *)&faa) == 0);
	CHECK(nevents == 0);
	missing_supply = true;
	config_process_deferred(&deferred_config_queue, &soc_device);
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	config_process_deferred(&deferred_config_queue, &gpu_device);
	config_process_deferred(&deferred_config_queue, &root_device);
	CHECK(!TAILQ_EMPTY(&config_finalize_list) && nevents == 0);
	CHECK(TAILQ_EMPTY(&config_pending));
	/* Remaining FDT passes attach the providers before finalization. */
	missing_supply = false;
	CHECK(config_finalize_register(&root_device, request_extra_pass) == 0);
	config_finalize();
	CHECK(config_finalize_done && extra_passes == 2);
	CHECK(TAILQ_EMPTY(&config_finalize_list) && TAILQ_EMPTY(&config_pending));
	CHECK(strcmp(events, "ASVPCQMIUcs") == 0);
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(normal_output, "0x00240038006800b7: 36.56.104.183") != NULL);
	CHECK(strstr(normal_output, "expected A733 GPU") != NULL);
	CHECK(error_output[0] == '\0');
	/* Late registration invokes the real hook immediately, still only once. */
	CHECK(config_finalize_register(&gpu_device, sun60i_gpu_finalize) == 0);
	CHECK(peeks == 1);
	reset();
	missing_supply = true;
	sun60i_gpu_attach(&soc_device, &gpu_device, (void *)&faa);
	config_finalize();
	CHECK(config_finalize_done && TAILQ_EMPTY(&config_pending));
	CHECK(TAILQ_EMPTY(&config_finalize_list));
	CHECK(maps == 0 && peeks == 0);
	CHECK(strstr(error_output, "supply provider") != NULL);
	reset();
	core_hz = 600000000;
	CHECK(sun60i_gpu_identify(&sc) == 0 && sc.sc_have_id && sc.sc_bvnc == pbvnc);

	for (u_int i = 0; i < __arraycount(bad_lengths); i++) {
		reset();
		pd_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
		reset();
		clock_len = bad_lengths[i];
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	for (int i = 0; i < 13; i++) {
		reset();
		switch (i) {
		case 0: managed_len = -1; break;
		case 1: managed_len = 4; break;
		case 2: binding_addr += 0x1000; break;
		case 3: binding_size = 0x27; break;
		case 4: pd[1] = swap32(6); break;
		case 5: power_cells = 0; break;
		case 6: bad_power_compat = true; break;
		case 7: clocks[1] = swap32(A733_CLK_NPU); break;
		case 8: clock_cells = 0; break;
		case 9: supply_len = 8; break;
		case 10: bad_supply = true; break;
		case 11: bad_pmic = true; break;
		case 12: clocks[0] = swap32(99); break;
		}
		unavailable(EINVAL);
		CHECK(nevents == 0);
	}
	reset(); reg_error = ENOENT; unavailable(ENOENT);
	reset(); missing_supply = true; unavailable(ENXIO);
	CHECK(releases == 0 && clock_puts == 0);
	reset(); missing_clock = true; unavailable(ENXIO);
	CHECK(releases == 1 && clock_puts == 0);
	reset(); supply_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASs") == 0);
	reset(); power_on = false; unavailable(EBUSY);
	CHECK(strcmp(events, "ASVPs") == 0);

	for (u_int i = 0; i < __arraycount(failures); i++) {
		for (u_int stage = 0; stage < 4; stage++) {
			reset();
			int *errors[] = { &state_error, &voltage_error,
			    &power_error, &ready_error };
			*errors[stage] = failures[i];
			unavailable(failures[i]);
			CHECK(releases == 1 && clock_puts == (stage == 3 ? 1U : 0U));
			*errors[stage] = 0;
			CHECK(sun60i_gpu_identify(&sc) == 0);
			CHECK(peeks == 1 && unmaps == 1 && releases == 2);
		}
	}
	for (u_int i = 0; i < __arraycount(bad_rates); i++) {
		reset(); core_hz = bad_rates[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
		reset(); voltage = bad_volts[i]; unavailable(EOPNOTSUPP);
		CHECK(clock_puts == 1 && releases == 1);
	}
	reset(); map_error = ENOMEM;
	CHECK(sun60i_gpu_identify(&sc) == ENOMEM);
	CHECK(maps == 1 && peeks == 0 && unmaps == 0 &&
	    clock_puts == 1 && releases == 1);
	reset(); peek_error = -1;
	CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
	CHECK(!sc.sc_have_id && normal_output[0] == '\0');
	CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
	    clock_puts == 1 && releases == 1);
	CHECK(strstr(error_output, "PBVNC read") != NULL);
	for (u_int i = 0; i < __arraycount(ids); i++) {
		reset(); pbvnc = ids[i];
		CHECK(sun60i_gpu_identify(&sc) == ENODEV);
		CHECK(sc.sc_have_id && sc.sc_bvnc == pbvnc);
		CHECK(maps == 1 && peeks == 1 && unmaps == 1 &&
		    clock_puts == 1 && releases == 1);
		reset(); pbvnc = ids[i];
		CHECK(sun60i_gpu_finalize(&gpu_device) == 0);
		CHECK(strstr(normal_output, "(unexpected)") != NULL);
		CHECK(strstr(normal_output, "(expected A733 GPU)") == NULL);
		CHECK(strstr(error_output, "PBVNC value") != NULL);
	}
	printf("PASS: %u A733 GPU identification checks\n", checks);
	return 0;
}
