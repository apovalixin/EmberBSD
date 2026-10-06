#!/bin/sh
# Origin: EmberBSD native VirtIO transport contract, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
set -eu
src=${1:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/virtio-transport.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror \
    -I"$src/sys/external/bsd/drm2/include" \
    "$src/ember/tools/virtio-transport-contract.c" -o "$tmp/contract"
"$tmp/contract"
# Compile the production body with a deterministic model of the native API.
# Only includes are replaced: queue/mapping/reset logic comes from production.
for file in include/linux/virtio_sg.h include/linux/virtio.h \
    include/linux/virtio_config.h linux/linux_virtio.c; do
    awk '!/^#include/' "$src/sys/external/bsd/drm2/$file"
done > "$tmp/virtio-under-test.h"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -I"$tmp" \
    "$src/ember/tools/virtio-transport-test.c" -o "$tmp/lifecycle"
"$tmp/lifecycle"
