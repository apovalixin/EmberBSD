/* Origin: EmberBSD; AI-assisted extracted-production FP state regression. */
/* SPDX-License-Identifier: BSD-2-Clause */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef __aligned
#define __aligned(n) __attribute__((aligned(n)))
#endif
#ifndef __BIT
#define __BIT(n) (UINT64_C(1) << (n))
#endif
#ifndef __BITS
#define __BITS(hi, lo) ((__BIT(hi) - 1 + __BIT(hi)) & ~(__BIT(lo) - 1))
#endif
#ifndef __SHIFTIN
#define __SHIFTIN(v, mask) ((uint64_t)(v) * ((mask) & -(mask)))
#endif
#ifndef __SHIFTOUT
#define __SHIFTOUT(v, mask) (((v) & (mask)) / ((mask) & -(mask)))
#endif
#ifndef __predict_false
#define __predict_false(v) (v)
#endif
#define KASSERT(v) assert(v)

#include "fp-layout.h"
#include "fp-controls.h"

/* Only the kernel interfaces used by the production function are doubled. */
struct pcb {
	struct fpreg pcb_fpregs;
};
typedef struct lwp {
	struct pcb pcb;
	struct {
		uint64_t md_cpacr;
	} l_md;
} lwp_t;
struct cpu_info {
	struct {
		unsigned int ev_count;
	} ci_vfp_use, ci_vfp_reuse;
};
static lwp_t thread;
static lwp_t *curlwp = &thread;
static struct cpu_info cpu;
static struct fpreg loaded;
static uint64_t mvfr1, cpacr;
static unsigned int loads, barriers, writes, event_count, failures;
static char events[8];

static struct pcb *
lwp_getpcb(lwp_t *l)
{

	return &l->pcb;
}

static struct cpu_info *
curcpu(void)
{

	return &cpu;
}

/* Required by the baseline source; deliberately irrelevant after the fix. */
static uint64_t __attribute__((unused))
reg_mvfr1_el1_read(void)
{

	return mvfr1;
}

static void
reg_cpacr_el1_write(uint64_t value)
{

	cpacr = value;
	writes++;
	events[event_count++] = 'C';
}

static void
isb(void)
{

	barriers++;
	events[event_count++] = 'I';
}

static void
load_fpregs(const struct fpreg *regs)
{

	memcpy(&loaded, regs, sizeof(loaded));
	loads++;
	events[event_count++] = 'L';
}

#include "fp-production.h"

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: FAIL: %s\n", __func__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

static void
reset(uint64_t legacy_features)
{

	memset(&thread, 0xa5, sizeof(thread));
	memset(&loaded, 0x5a, sizeof(loaded));
	memset(&cpu, 0, sizeof(cpu));
	memset(events, 0, sizeof(events));
	mvfr1 = legacy_features;
	cpacr = loads = barriers = writes = event_count = 0;
}

static void
check_enable(bool restore)
{

	CHECK(thread.l_md.md_cpacr == CPACR_FPEN_ALL);
	CHECK(cpacr == CPACR_FPEN_ALL);
	CHECK(writes == 1 && barriers == 1);
	CHECK(loads == (restore ? 1U : 0U));
	CHECK(strcmp(events, restore ? "CIL" : "CI") == 0);
}

static void
check_initial(uint64_t legacy_features)
{
	static const struct fpreg zero;

	reset(legacy_features);
	fpu_state_load(&thread, 0);
	CHECK(memcmp(&thread.pcb.pcb_fpregs, &zero, sizeof(zero)) == 0);
	CHECK(memcmp(&loaded, &zero, sizeof(zero)) == 0);
	CHECK(cpu.ci_vfp_use.ev_count == 1);
	CHECK(cpu.ci_vfp_reuse.ev_count == 0);
	check_enable(true);
}

static void
check_valid(unsigned int flags)
{
	struct fpreg saved, not_loaded;
	unsigned int i;

	reset(0);
	for (i = 0; i < 32; i++) {
		thread.pcb.pcb_fpregs.fp_reg[i].u64[0] = UINT64_MAX - i;
		thread.pcb.pcb_fpregs.fp_reg[i].u64[1] = UINT64_C(1) << i;
	}
	/* Include FP16 FZ16/AHP and AFP FIZ/AH/NEP, with no feature mask. */
	thread.pcb.pcb_fpregs.fpcr = UINT32_C(0x07480007);
	thread.pcb.pcb_fpregs.fpsr = UINT32_C(0xf800009f);
	memcpy(&saved, &thread.pcb.pcb_fpregs, sizeof(saved));
	memcpy(&not_loaded, &loaded, sizeof(not_loaded));
	fpu_state_load(&thread, flags);
	CHECK(memcmp(&thread.pcb.pcb_fpregs, &saved, sizeof(saved)) == 0);
	CHECK(memcmp(&loaded, (flags & PCU_REENABLE) ? &not_loaded :
	    &saved, sizeof(saved)) == 0);
	CHECK(cpu.ci_vfp_use.ev_count == 0);
	CHECK(cpu.ci_vfp_reuse.ev_count == 1);
	check_enable((flags & PCU_REENABLE) == 0);
}

int
main(void)
{

	check_initial(0); /* AArch64-only implementation: MVFR is UNKNOWN. */
	check_initial(UINT64_C(0x11)); /* AArch32 IEEE feature fields. */
	check_initial(UINT64_C(0x03000000)); /* FP16, no AArch32 IEEE fields. */
	check_initial(UINT64_MAX); /* Reserved/unknown legacy feature values. */
	check_valid(PCU_VALID);
	check_valid(PCU_VALID | PCU_REENABLE);
	if (failures != 0) {
		fprintf(stderr, "AArch64 FP state: %u failed assertions\n", failures);
		return 1;
	}
	puts("AArch64 FP state: 6 production contract groups PASS");
	return 0;
}
