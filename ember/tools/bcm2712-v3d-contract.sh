#!/bin/sh
# Origin: EmberBSD passive BCM2712 V3D observation contract, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_acpi.c" > "$work/driver.h"
awk '/^#define[ \t]+(VCPROP_CLK_V3D|VCPROP_REQ_SUCCESS|VCPROPTAG_RESPONSE|VCPROPTAG_GET_CLOCKSTATE|VCPROPTAG_GET_CLOCKRATE|VCPROPTAG_GET_CLOCK_MEASURED)[ \t]/' \
    "$src/sys/arch/evbarm/rpi/vcprop.h" > "$work/properties.h"
awk '/^#define BCM2712_PM_GRAFX[ \t]/' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/properties.h"
printf 'int\n' > "$work/pm-accessor.h"
sed -n '/^bcmpmwdog_v3d_status(/,$p' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/pm-accessor.h"
# Compile the production driver and PM accessor, replacing only host services.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${CFLAGS:-} -I"$work" "$src/ember/tools/bcm2712-v3d-contract.c" \
    -o "$work/contract"
if [ -n "${V3D_OUTPUT:-}" ]; then
    cp "$work/contract" "$V3D_OUTPUT"
fi
run_contract()
{
    if [ -n "${TEST_RUNNER:-}" ]; then
        "$TEST_RUNNER" "$1"
    else
        "$1"
    fi
}
if [ "${V3D_COMPILE_ONLY:-0}" = 1 ]; then
    exit 0
fi
run_contract "$work/contract"
if [ "${V3D_SKIP_MUTANTS:-0}" = 1 ]; then
    exit 0
fi
cp "$work/driver.h" "$work/driver-good.h"
for mutation in clock-state echoed-id sms-state cleanup; do
    awk -v mutation="$mutation" '
        mutation == "clock-state" {
            changed += sub(/sc->sc_clock_state != 1/, "false")
        }
        mutation == "echoed-id" {
            changed += sub(/le32toh\(request\[5\]\) != VCPROP_CLK_V3D/, "false")
        }
        mutation == "sms-state" {
            changed += sub(/sc->sc_sms\[0\] != 0 \|\| sc->sc_sms\[1\] != 0/, "false")
        }
        mutation == "cleanup" {
            changed += sub(/if \(mapped\[i\]\)/, "if (!mapped[i])")
        }
        { print }
        END { if (changed != 1) exit 1 }
    ' "$work/driver-good.h" > "$work/driver.h"
    # Each mutant must compile and then fail a behavioral assertion.
    # shellcheck disable=SC2086
    "${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
        ${CFLAGS:-} -I"$work" "$src/ember/tools/bcm2712-v3d-contract.c" \
        -o "$work/mutant"
    if (ulimit -c 0; run_contract "$work/mutant") > "$work/mutant.log" 2>&1; then
        echo "surviving BCM2712 V3D causal mutant: $mutation" >&2
        exit 1
    fi
    echo "BCM2712 V3D causal mutant rejected: $mutation"
done
