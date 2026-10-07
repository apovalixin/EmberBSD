#!/bin/sh
# Origin: EmberBSD; AI-assisted production VirtGPU capset regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-capsets.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v type="$3" '
        $0 ~ "(^|[ *])" name "[(]" && $0 !~ /;[[:space:]]*$/ {
            print type; sub("^.*" name "[(]", name "("); copying = 1;
        }
        copying { print }
        copying && /^}/ { found = 1; exit }
        END { if (!found) exit 1 }
    ' "$2"
}
vq="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_vq.c"
kms="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_kms.c"
hdr="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_drv.h"
mkdir "$work/linux"
cat > "$work/linux/types.h" <<'C'
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16, __le16;
typedef uint32_t __u32, __le32;
typedef uint64_t __u64, __le64;
C
# Retain the production protocol, result/cache layouts and pending value.
sed -n '/^#define VIRTGPU_CAP_PENDING /p' "$hdr" > "$work/capsets-layout.h"
if ! grep -q '^virtio_gpu_complete_transfer(' "$vq"; then
    printf '#define CONTROLLED_2D_FOUNDATION 1\n' >> "$work/capsets-layout.h"
fi
sed -n '/^enum virtgpu_dma_lease /,/^};/p' "$hdr" >> "$work/capsets-layout.h"
if [ -f "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" ]; then
    printf '#define DMA_LEASE_SOURCE 1\n' >> "$work/capsets-layout.h"
fi
for name in virtio_gpu_capset_result virtio_gpu_drv_capset virtio_gpu_drv_cap_cache; do
    sed -n "/^struct $name {/,/^};/p" "$hdr" >> "$work/capsets-layout.h"
done
sed -n '/^struct drm_virtgpu_get_caps {/,/^};/p' \
    "$src/sys/external/bsd/drm2/dist/include/uapi/drm/virtgpu_drm.h" >> "$work/capsets-layout.h"
sed -n '/^struct virtio_gpu_wait {/,/^};/p' "$vq" >> "$work/capsets-layout.h"
sed -n '/^struct virtio_gpu_vbuffer {/,/^};/p' "$hdr" >> "$work/capsets-layout.h"
: > "$work/capsets-production.h"
for name in virtio_gpu_wait_put virtio_gpu_wait_done; do
    extract "$name" "$vq" 'static void' >> "$work/capsets-production.h"
done
extract virtio_gpu_get_vbuf "$vq" 'static struct virtio_gpu_vbuffer *' >> "$work/capsets-production.h"
extract virtio_gpu_alloc_cmd_resp "$vq" 'static void *' >> "$work/capsets-production.h"
for name in free_vbuf virtio_gpu_finish_vbuf virtio_gpu_cancel_vbuf; do
    extract "$name" "$vq" 'static void' >> "$work/capsets-production.h"
done
for name in virtio_gpu_response_error virtio_gpu_queue_sync; do
    extract "$name" "$vq" 'static int' >> "$work/capsets-production.h"
done
extract virtio_gpu_fail_capsets "$vq" 'static void' >> "$work/capsets-production.h"
for name in virtio_gpu_capset_finish virtio_gpu_capset_wait; do
    extract "$name" "$vq" 'static int' >> "$work/capsets-production.h"
done
for name in virtio_gpu_cmd_get_capset_info_cb virtio_gpu_cmd_capset_cb; do
    extract "$name" "$vq" 'static void' >> "$work/capsets-production.h"
done
for name in virtio_gpu_cmd_get_capset_info virtio_gpu_cmd_get_capset; do
    extract "$name" "$vq" 'static int' >> "$work/capsets-production.h"
done
extract virtio_gpu_get_capsets "$kms" 'static int' >> "$work/capsets-production.h"
extract virtio_gpu_cleanup_cap_cache "$kms" 'static void' >> "$work/capsets-production.h"
extract virtio_gpu_get_caps_ioctl \
    "$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_ioctl.c" 'static int' >> "$work/capsets-production.h"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -pthread \
    ${CAPSET_TEST_CFLAGS:-} -I"$work" \
    -I"$src/sys/external/bsd/drm2/include" \
    -I"$src/sys/external/bsd/drm2/virtio" \
    "$src/ember/tools/virtgpu-capsets-fixture.c" -o "$work/test"
"$work/test"
