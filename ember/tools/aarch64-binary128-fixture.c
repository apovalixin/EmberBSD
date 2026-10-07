/* Origin: EmberBSD; AI-assisted production binary128 comparison regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include "comparetf2-production.h"

struct operand {
	const char *name;
	uint64_t low, high;
	int rank, nan;
};

/* Independent IEEE encodings, ordered ranks and NaN classes (1 quiet, 2 sNaN). */
static const struct operand operands[] = {
	{ "-inf", 0, UINT64_C(0xffff000000000000), -3, 0 },
	{ "-one", 0, UINT64_C(0xbfff000000000000), -1, 0 },
	{ "-zero", 0, UINT64_C(0x8000000000000000), 0, 0 },
	{ "+zero", 0, 0, 0, 0 },
	{ "one", 0, UINT64_C(0x3fff000000000000), 1, 0 },
	{ "two", 0, UINT64_C(0x4000000000000000), 2, 0 },
	{ "+inf", 0, UINT64_C(0x7fff000000000000), 3, 0 },
	{ "qNaN", UINT64_C(0x0123456789abcdef), UINT64_C(0x7fff800000000042), 0, 1 },
	{ "sNaN", UINT64_C(0x0123456789abcdef), UINT64_C(0x7fff000000000042), 0, 2 },
	{ "-qNaN-min", 0, UINT64_C(0xffff800000000000), 0, 1 },
	{ "-sNaN-min", 1, UINT64_C(0xffff000000000000), 0, 2 },
	{ "sNaN-low-min", 1, UINT64_C(0x7fff000000000000), 0, 2 },
	{ "sNaN-high-min", 0, UINT64_C(0x7fff000000000001), 0, 2 },
	{ "sNaN-max", UINT64_MAX, UINT64_C(0x7fff7fffffffffff), 0, 2 },
	{ "qNaN-max", UINT64_MAX, UINT64_C(0xffffffffffffffff), 0, 1 }
};

static int
call_helper(unsigned h, fp_t a, fp_t b)
{
	switch (h) {
	case 0: return __eqtf2(a, b);
	case 1: return __netf2(a, b);
	case 2: return __unordtf2(a, b);
	case 3: return __gttf2(a, b);
	case 4: return __getf2(a, b);
	case 5: return __lttf2(a, b);
	case 6: return __letf2(a, b);
	case 7: return __cmptf2(a, b);
	default: abort();
	}
}

static int
predicate(unsigned h, int value, const struct operand *a,
    const struct operand *b)
{
	int unordered = a->nan || b->nan;
	int cmp = (a->rank > b->rank) - (a->rank < b->rank);

	switch (h) {
	case 0: case 1: return (value == 0) == (!unordered && cmp == 0);
	case 2: return (value != 0) == unordered;
	case 3: return (value > 0) == (!unordered && cmp > 0);
	case 4: return (value >= 0) == (!unordered && cmp >= 0);
	case 5: return (value < 0) == (!unordered && cmp < 0);
	case 6: case 7: return (value <= 0) == (!unordered && cmp <= 0);
	default: abort();
	}
}

int
main(void)
{
	unsigned h, a, b, seed, pred_fail = 0, policy_fail = 0, state_fail = 0;
	unsigned rows = 0;

	for (h = 0; h < 8; h++) {
		for (a = 0; a < sizeof(operands) / sizeof(operands[0]); a++) {
			for (b = 0; b < sizeof(operands) / sizeof(operands[0]); b++) {
				int invalid = h < 3 ? operands[a].nan == 2 ||
				    operands[b].nan == 2 : operands[a].nan || operands[b].nan;
				fp_t x = ((rep_t)operands[a].high << 64) | operands[a].low;
				fp_t y = ((rep_t)operands[b].high << 64) | operands[b].low;
				int value;

				for (seed = 0; seed < 2; seed++) {
					model_calls = 0;
					model_fpsr = UINT64_C(0x08000002) | seed;
					model_fpcr = UINT64_C(0x03000000);
					value = call_helper(h, x, y);
					pred_fail += !predicate(h, value, &operands[a], &operands[b]);
#ifdef TEST_LEGACY_POLICY
					invalid = 0;
#endif
					policy_fail += model_calls != (unsigned)!!invalid;
					state_fail += model_fpsr != (UINT64_C(0x08000002) |
					    seed | !!invalid) || model_fpcr != UINT64_C(0x03000000);
					rows++;
				}
			}
		}
	}
	printf("rows=%u predicate_fail=%u invalid_policy_fail=%u model_state_fail=%u\n",
	    rows, pred_fail, policy_fail, state_fail);
	return pred_fail || policy_fail || state_fail ? 1 : 0;
}
