#!/bin/sh
# Origin: EmberBSD native VirtIO transport compile contract, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
# Copyright (c) 2026 EmberBSD contributors.
# Usage: sh virtio-transport-native.sh NATIVE_SOURCE KERNEL_HEADERS [OVERLAY]
# NATIVE_SOURCE is a complete NetBSD checkout; KERNEL_HEADERS is an existing
# configured AArch64 kernel directory.  Neither is modified by this probe.
set -eu
native=$(CDPATH= cd -- "$1" && pwd)
headers=$(CDPATH= cd -- "$2" && pwd)
overlay=${3:-$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)}
overlay=$(CDPATH= cd -- "$overlay" && pwd)
tmp=$(mktemp -d "${TMPDIR:-/tmp}/virtio-native.XXXXXXXX")
trap 'rm -rf "$tmp"' EXIT HUP INT TERM
"${CC:-cc}" -march=armv8-a+nofp+nosimd -ffreestanding \
    -fno-strict-aliasing -fno-common -std=gnu11 -Werror -Wall \
    -Wpointer-arith -Wmissing-prototypes -Wstrict-prototypes \
    -Wold-style-definition -Wswitch -Wshadow -Wcast-qual -Wwrite-strings \
    -Wno-attributes -Wno-type-limits -Wno-sign-compare \
    -I"$overlay/sys/external/bsd/drm2/include" -I"$headers" \
    -I"$native/common/include" -I"$native/sys/arch" -I"$native/sys" \
    -nostdinc -DAARCH64 -D_KERNEL -D_KERNEL_OPT \
    -I"$native/sys/external/bsd/common/include" \
    -I"$native/sys/external/bsd/drm2/include" \
    -c "$overlay/sys/external/bsd/drm2/linux/linux_virtio.c" \
    -o "$tmp/linux_virtio.o"
printf '%s\n' 'VirtIO transport native AArch64 compilation passed'
