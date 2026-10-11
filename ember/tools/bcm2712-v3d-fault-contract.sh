#!/bin/sh
# Origin: EmberBSD native V3D fault-recovery contract, 2026-10-11.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-fault.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_fault.c" > "$work/fault-body.h"
compile_contract()
{
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-fault-contract.c" -o "$1"
}
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
compile_contract "$work/contract"
if [ -n "${V3D_FAULT_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_FAULT_OUTPUT"
fi
if [ "${V3D_FAULT_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_FAULT_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/fault-body.h" "$work/fault-good.h"
for mutation in skip-latch-clear skip-flush fixup-without-sync \
    release-always retry-faulted-job; do
    cp "$work/fault-good.h" "$work/fault-body.h"
    awk -v mutation="$mutation" '
        mutation == "skip-latch-clear" &&
        /error = bcmv3d_takeover_hub_poke\(FV3D_MMU_CTL, value\);/ {
            print "\terror = 0;"
            changed = 1
            next
        }
        mutation == "skip-flush" {
            changed += gsub(/FV3D_MMUC_ENABLE \| FV3D_MMUC_FLUSH/,
                "FV3D_MMUC_ENABLE")
        }
        mutation == "fixup-without-sync" {
            changed += gsub(/fv3d.obj\[FV3D_OBJ_PT\].map/,
                "fv3d.obj[FV3D_OBJ_SCRATCH].map")
            changed += gsub(/fv3d.obj\[FV3D_OBJ_PT\].size/,
                "fv3d.obj[FV3D_OBJ_SCRATCH].size")
        }
        mutation == "release-always" {
            changed += sub(/if \(!fv3d\.published\)/, "if (1)")
        }
        mutation == "retry-faulted-job" &&
        /fv3d.stage = "valid destination alias";/ {
            print "\t(void)bcmv3d_takeover_hub_poke(FV3D_TFU_ICFG, FV3D_TFU_ICFG_VALUE);"
            changed = 1
        }
        { print }
        END { exit changed > 0 ? 0 : 1 }
    ' "$work/fault-good.h" > "$work/fault-body.h" || {
        echo "mutant $mutation: source pattern not found" >&2
        exit 1
    }
    if ! compile_contract "$work/contract-mutant"; then
        echo "mutant $mutation failed to compile" >&2
        exit 1
    fi
    if run_contract "$work/contract-mutant" >/dev/null 2>&1; then
        echo "mutant $mutation was not rejected" >&2
        exit 1
    fi
done
echo "PASS: fault contract; 5 causal mutants rejected"
