#!/bin/sh
# Origin: EmberBSD native V3D interrupt contract, 2026-10-10.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-irq.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_irq.c" > "$work/irq-body.h"
compile_contract()
{
    # Compile the actual production probe body; replace only host services.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" \
        "$src/ember/tools/bcm2712-v3d-irq-contract.c" -o "$1"
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
if [ -n "${V3D_IRQ_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_IRQ_OUTPUT"
fi
if [ "${V3D_IRQ_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_IRQ_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/irq-body.h" "$work/irq-good.h"
for mutation in no-ack no-remask unbounded-wait unmask-before-establish \
    never-wake ack-unhandled release-always skip-clean-status; do
    cp "$work/irq-good.h" "$work/irq-body.h"
    awk -v mutation="$mutation" '
        mutation == "no-ack" && /iv3d_hub_intr/ && /ack/ { next }
        mutation == "no-ack" &&
        /bcmv3d_takeover_hub_poke\(IV3D_INT_CLR, IV3D_HUB_TFUC\)/ {
            changed = 1
            next
        }
        mutation == "no-remask" &&
        /bcmv3d_takeover_hub_poke\(IV3D_MSK_SET, IV3D_HUB_TFUC\)/ {
            changed = 1
            next
        }
        mutation == "unbounded-wait" &&
        /^#define[ \t]*IV3D_WAIT_SLICES[ \t]+5$/ {
            sub(/5$/, "1000000")
            changed = 1
        }
        mutation == "unmask-before-establish" &&
        /iv3d.stage = "establishing handlers";/ {
            print "\tbcmv3d_takeover_hub_poke(IV3D_MSK_CLR, IV3D_HUB_TFUC);"
            changed = 1
        }
        mutation == "never-wake" {
            changed += sub(/wakeup\(&iv3d\.hub_delivered\);/, ";")
        }
        mutation == "ack-unhandled" {
            changed += sub(/IV3D_INT_CLR, ours\)/,
                "IV3D_INT_CLR, 0xffffffffu)")
        }
        mutation == "release-always" {
            changed += sub(/if \(!iv3d\.published\)/, "if (1)")
        }
        mutation == "skip-clean-status" &&
        /if \(hub_sts != 0 \|\| core_sts != 0\) \{/ {
            sub(/hub_sts != 0 \|\| core_sts != 0/, "0")
            changed = 1
        }
        { print }
        END { exit changed > 0 ? 0 : 1 }
    ' "$work/irq-good.h" > "$work/irq-body.h" || {
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
echo "PASS: IRQ contract; 8 causal mutants rejected"
