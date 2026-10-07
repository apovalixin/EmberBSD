#!/bin/sh
# Origin: EmberBSD; AI-assisted actual console creation/privacy regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${CONSOLE_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-console-private.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
console="$src/sys/external/bsd/drm2/virtio/virtgpu_console.c"
cat > "$work/test.c" <<'C'
#include "virtgpu-console-private-cases.h"
C
for name in virtgpu_console_geometry virtgpu_console_probe; do
    awk -v name="$name" '
      $0 ~ "^" name "[(]" { copying=1; print (name=="virtgpu_console_geometry"?"static size_t":"static int") }
      copying { print }
      copying && /^}/ { exit }
    ' "$console" >> "$work/test.c"
done
cat >> "$work/test.c" <<'C'
int main(void) { return console_private_cases(); }
C
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
 ${CONTROLLED_2D_TEST_CFLAGS:-} -I"$tools" -I"$src/sys/external/bsd/drm2/virtio" \
 "$work/test.c" -o "$work/test"
"$work/test"
