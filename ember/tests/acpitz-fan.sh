#!/bin/sh
# Check the actual callback that handles thermal trip/device-list notifications.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/acpitz-fan.XXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
awk '/^acpitz_get_zone_quiet\(void \*opaque\)/ { print "static void"; copy = 1 }
copy { print }
copy && /^}/ { exit }' "${1:-$src/sys/dev/acpi/acpi_tz.c}" > "$work/acpitz-zone-callback.h"
${CC:-cc} -std=c99 -Wall -Wextra -Werror -I"$work" \
    "$src/ember/tools/acpitz-fan-contract.c" -o "$work/check"
"$work/check"
