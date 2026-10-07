#!/bin/sh
# Origin: EmberBSD; AI-assisted extracted CAN mbuf ownership regression.
# SPDX-License-Identifier: BSD-2-Clause
#
# Usage: sh ember/tools/canfd-ownership-check.sh [source-root | path/to/can.c]
# An older can.c can be supplied for the negative control. Public CAN headers
# come from CAN_OWNERSHIP_SOURCE_ROOT, which defaults to this checkout.
# This fault contract complements the real rump tests; it is not a kernel run.

set -eu

tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=${CAN_OWNERSHIP_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
[ "$#" -le 1 ] || {
	printf 'usage: %s [source-root | path/to/can.c]\n' "$0" >&2
	exit 2
}
if [ "$#" -eq 1 ] && [ -d "$1" ]; then
	root=$1
	source=$root/sys/netcan/can.c
else
	source=${1:-$root/sys/netcan/can.c}
fi
[ -f "$source" ] || { printf 'missing source: %s\n' "$source" >&2; exit 2; }
work=$(mktemp -d "${TMPDIR:-/tmp}/canfd-ownership.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

extract()
{
	awk -v name="$1" '
	    $0 ~ "^" name "[(]" {
	        print previous;
	        copying = 1;
	    }
	    copying { print }
	    copying && /^}/ { found = 1; exit }
	    { previous = $0 }
	    END { if (!found) exit 1 }
	' "$source"
}

# Preserve the original licence, authors and upstream identifier with the
# extracted functions. Their bodies are copied without textual substitution.
awk '/^#include/ { exit } { print }' "$source" > "$work/production.h"
if grep -q '^can_mbuf_free(' "$source"; then
	printf '#define CAN_OWNERSHIP_HELPERS 1\n' >> "$work/production.h"
	for name in can_frame_len_valid can_frame_valid can_mbuf_free \
	    can_mbuf_pullup; do
		extract "$name" >> "$work/production.h"
	done
fi
extract can_output >> "$work/production.h"
extract can_send >> "$work/production.h"

# Use the source ABI definitions. libc headers have already selected their
# feature set; omit only NetBSD's unavailable feature-selection include on
# non-NetBSD hosts. Both public headers retain their full original licences.
sed '/^#include <sys\/featuretest.h>$/d' "$root/sys/netcan/can.h" \
    > "$work/can-layout.h"
cp "$root/sys/netcan/can_link.h" "$work/can-link-layout.h"
awk '/^#ifndef/ { exit } { print }' "$root/sys/netcan/can_pcb.h" \
    > "$work/can-pcb-flags.h"
sed -n '/^#define[[:space:]]CANP_FD_FRAMES[[:space:]]/p' \
    "$root/sys/netcan/can_pcb.h" >> "$work/can-pcb-flags.h"

printf 'CAN ownership source: %s\n' "$source"
if command -v sha256 >/dev/null 2>&1; then
	sha256 "$source"
elif command -v shasum >/dev/null 2>&1; then
	shasum -a 256 "$source"
fi
${CC:-cc} -std=c11 -O2 -Wall -Wextra -Werror -Wno-unused-parameter \
    -Wno-sign-compare -I"$work" "$tools/canfd-ownership-fixture.c" \
    -o "$work/check"
"$work/check"
