#!/bin/sh
# Origin: EmberBSD native V3D reset takeover contract, 2026-10-09.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/bcm2712-v3d-takeover.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
sed '/^#include /d; /^__KERNEL_RCSID(/d' \
    "$src/sys/arch/arm/broadcom/bcm2712_v3d_takeover.c" > "$work/takeover.h"
awk '/^#define BCM2712_PM_GRAFX[ \t]/' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" > "$work/properties.h"
printf 'int\n' > "$work/pm-accessor.h"
sed -n '/^bcmpmwdog_v3d_status(/,$p' \
    "$src/sys/arch/arm/broadcom/bcm2835_pmwdog_acpi.c" >> "$work/pm-accessor.h"
# Compile actual production takeover/PM bodies; replace only host services.
# shellcheck disable=SC2086
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter \
    -DBCM2712_V3D_TAKEOVER -pthread ${CFLAGS:-} -I"$work" \
    "$src/ember/tools/bcm2712-v3d-takeover-contract.c" -o "$work/contract"
"$work/contract"
