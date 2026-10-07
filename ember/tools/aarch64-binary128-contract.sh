#!/bin/sh
# Origin: EmberBSD; AI-assisted actual-source binary128 comparison contract.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aarch64-binary128.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
production=${BINARY128_SOURCE:-$src/sys/external/bsd/compiler_rt/dist/lib/builtins/comparetf2.c}
# Compile the complete production file, substituting only its bit-container
# header and the delimited hardware hook. No comparator body is copied here.
awk '
    /^#include "fp_lib.h"$/ { headers++; print "#include \"host-bits.h\""; next }
    /^\/\* EMBER_BINARY128_INVALID_BEGIN \*\/$/ {
        starts++; replacing = 1;
        print "static void binary128_raise_invalid(void) { model_calls++; model_fpsr |= 1; }";
        next
    }
    /^\/\* EMBER_BINARY128_INVALID_END \*\/$/ { ends++; replacing = 0; next }
    /COMPILER_RT_NETBSD_AARCH64_FENV/ { optin = 1 }
    !replacing { print }
    END {
        if (headers != 1 || replacing || starts != ends ||
            (optin && starts != 1) || (!optin && starts != 0)) exit 2;
    }
' "$production" > "$work/comparetf2-production.h"
cat > "$work/host-bits.h" <<'EOF'
#include <stdint.h>
#ifdef TEST_NON_NETBSD
#undef __NetBSD__
#else
#define __NetBSD__ 1
#endif
#ifdef TEST_NON_AARCH64
#undef __aarch64__
#else
#define __aarch64__ 1
#endif
#undef __ARM_FP
#ifdef TEST_NO_DOUBLE
#define __ARM_FP 0
#else
#define __ARM_FP 8
#endif
#define __ELF__ 1
#define CRT_HAS_128BIT 1
#define CRT_LDBL_128BIT 1
#define COMPILER_RT_ABI
typedef unsigned __int128 rep_t;
typedef __int128 srep_t;
typedef rep_t fp_t;
#define toRep(x) (x)
#define absMask (((rep_t)1 << 127) - 1)
#define infRep ((rep_t)0x7fff << 112)
#define quietBit ((rep_t)1 << 111)
/* Mach-O has no ELF aliases: model the alias call, check real ELF separately. */
#define FNALIAS(alias, original) enum LE_RESULT alias(fp_t a, fp_t b) { return original(a, b); }
static unsigned model_calls;
static uint64_t model_fpsr, model_fpcr;
EOF
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${BINARY128_TEST_CFLAGS:-} \
    ${BINARY128_POLICY_FLAGS:--DCOMPILER_RT_NETBSD_AARCH64_FENV} \
    -I"$work" "$src/ember/tools/aarch64-binary128-fixture.c" -o "$work/test"
"$work/test"
