#!/bin/sh
# Origin: EmberBSD; AI-assisted production AArch64 FP state regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aarch64-fp-state.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
fpu=${FP_STATE_SOURCE:-$src/sys/arch/aarch64/aarch64/fpu.c}
sed -n '/^union fpelem {/,/^} __aligned(16);/p' \
    "$src/sys/arch/aarch64/include/reg.h" > "$work/fp-layout.h"
awk '/^#define[[:space:]]+(FPCR_|MVFR1_|CPACR_FPEN)/ { print }' \
    "$src/sys/arch/aarch64/include/armreg.h" > "$work/fp-controls.h"
awk '/^#define[[:space:]]+PCU_(VALID|REENABLE)[[:space:]]/ { print }' \
    "$src/sys/sys/pcu.h" >> "$work/fp-controls.h"
awk '
    /^fpu_state_load\(lwp_t \*l, unsigned int flags\)/ {
        print "static void"; copying = 1;
    }
    copying { print }
    copying && /^}/ { found = 1; exit }
    END { if (!found) exit 1 }
' "$fpu" > "$work/fp-production.h"
${CC:-cc} -std=c11 -Wall -Wextra -Werror ${FP_STATE_TEST_CFLAGS:-} \
    -I"$work" "$src/ember/tools/aarch64-fp-state-fixture.c" -o "$work/test"
"$work/test"
