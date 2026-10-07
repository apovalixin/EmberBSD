//===-- lib/comparetf2.c - Quad-precision comparisons -------------*- C -*-===//
// Origin: EmberBSD; AI-assisted AArch64 libc binary128 INVALID policy.
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// // This file implements the following soft-float comparison routines:
//
//   __eqtf2   __getf2   __unordtf2
//   __letf2   __gttf2
//   __lttf2
//   __netf2
//
// The integer results of routines grouped in each column are identical.
// NetBSD AArch64 libc equality and unordered comparisons raise INVALID only
// for signaling NaNs; ordered comparisons raise it for every NaN.
//
// The main routines behave as follows:
//
//   __letf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                         1 if either a or b is NaN
//
//   __getf2(a,b) returns -1 if a < b
//                         0 if a == b
//                         1 if a > b
//                        -1 if either a or b is NaN
//
//   __unordtf2(a,b) returns 0 if both a and b are numbers
//                           1 if either a or b is NaN
//
// Note that __letf2( ) and __getf2( ) are identical except in their handling of
// NaN values.
//
//===----------------------------------------------------------------------===//

#define QUAD_PRECISION
#include "fp_lib.h"

#if defined(COMPILER_RT_NETBSD_AARCH64_FENV)
#if !defined(__NetBSD__) || !defined(__aarch64__) || defined(_KERNEL) || \
    defined(_STANDALONE) || defined(__SOFT_FP__) || defined(__SOFTFP__)
#error NetBSD AArch64 binary128 FENV requires userland hard-float libc
#endif
#if !defined(__lint__) && (!defined(__ARM_FP) || (__ARM_FP & 8) == 0)
#error NetBSD AArch64 binary128 FENV requires hardware double precision
#endif
#if !defined(CRT_HAS_128BIT) || !defined(CRT_LDBL_128BIT)
#error NetBSD AArch64 binary128 FENV requires IEEE binary128 long double
#endif
#endif

#if defined(CRT_HAS_128BIT) && defined(CRT_LDBL_128BIT)
enum LE_RESULT {
    LE_LESS      = -1,
    LE_EQUAL     =  0,
    LE_GREATER   =  1,
    LE_UNORDERED =  1
};

enum GE_RESULT {
    GE_LESS      = -1,
    GE_EQUAL     =  0,
    GE_GREATER   =  1,
    GE_UNORDERED = -1
};

#if defined(COMPILER_RT_NETBSD_AARCH64_FENV)
/* EMBER_BINARY128_INVALID_BEGIN */
static void
binary128_raise_invalid(void)
{
#if defined(__lint__)
	/* Lint-only syntax stub; this does not validate an executable object. */
#else
	double scratch;
	const uint64_t nan = UINT64_C(0x7ff8000000000000);

	/* FCMPE raises IOC, or traps with IOE, without changing FPCR. */
	__asm__ volatile("fmov %d0, %1\n\tfcmpe %d0, %d0"
	    : "=&w" (scratch) : "r" (nan) : "cc");
#endif
}
/* EMBER_BINARY128_INVALID_END */
#endif

static int
binary128_unordered(rep_t aAbs, rep_t bAbs, int ordered)
{
	const int aNaN = aAbs > infRep;
	const int bNaN = bAbs > infRep;

#if defined(COMPILER_RT_NETBSD_AARCH64_FENV)
	/* Test both operands: a quiet NaN must not hide a signaling NaN. */
	if ((ordered && (aNaN || bNaN)) ||
	    (aNaN && (aAbs & quietBit) == 0) ||
	    (bNaN && (bAbs & quietBit) == 0))
		binary128_raise_invalid();
#else
	(void)ordered;
#endif
	return aNaN || bNaN;
}

static int
binary128_compare(fp_t a, fp_t b, int unordered, int ordered)
{
	const srep_t aInt = toRep(a);
	const srep_t bInt = toRep(b);
	const rep_t aAbs = aInt & absMask;
	const rep_t bAbs = bInt & absMask;

	if (binary128_unordered(aAbs, bAbs, ordered))
		return unordered;
	if ((aAbs | bAbs) == 0)
		return 0;
	/* Signed integer ordering agrees unless both operands are negative. */
	if ((aInt & bInt) >= 0) {
		if (aInt < bInt)
			return -1;
		if (aInt == bInt)
			return 0;
		return 1;
	}
	if (aInt > bInt)
		return -1;
	if (aInt == bInt)
		return 0;
	return 1;
}

COMPILER_RT_ABI enum LE_RESULT
__letf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, LE_UNORDERED, 1);
}

#if defined(__ELF__)
// Alias for libgcc compatibility
FNALIAS(__cmptf2, __letf2);
#endif

COMPILER_RT_ABI enum GE_RESULT
__getf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, GE_UNORDERED, 1);
}

COMPILER_RT_ABI int
__unordtf2(fp_t a, fp_t b)
{
	return binary128_unordered(toRep(a) & absMask, toRep(b) & absMask, 0);
}

COMPILER_RT_ABI enum LE_RESULT
__eqtf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, LE_UNORDERED, 0);
}

COMPILER_RT_ABI enum LE_RESULT
__lttf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, LE_UNORDERED, 1);
}

COMPILER_RT_ABI enum LE_RESULT
__netf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, LE_UNORDERED, 0);
}

COMPILER_RT_ABI enum GE_RESULT
__gttf2(fp_t a, fp_t b)
{
	return binary128_compare(a, b, GE_UNORDERED, 1);
}
#endif
