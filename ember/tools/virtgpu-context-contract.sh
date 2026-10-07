#!/bin/sh
# Origin: EmberBSD; AI-assisted production VirtGPU context lifetime regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-context.XXXXXXXX")
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
sed -n '/^struct virtio_gpu_wait {/,/^};/p' "$vq" > "$work/context-layout.h"
sed -n '/^enum virtgpu_dma_lease /,/^};/p' "$hdr" >> "$work/context-layout.h"
if [ -f "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" ]; then
    printf '#define DMA_LEASE_SOURCE 1\n' >> "$work/context-layout.h"
fi
for name in virtio_gpu_vbuffer virtio_gpu_attachment virtio_gpu_fpriv; do
    sed -n "/^struct $name {/,/^};/p" "$hdr" >> "$work/context-layout.h"
done
extract linux_virtio_reset "$src/sys/external/bsd/drm2/linux/linux_virtio.c" \
    'static void' > "$work/context-production.h"
extract virtio_gpu_stop "$kms" 'static void' >> "$work/context-production.h"
for name in virtio_gpu_wait_put virtio_gpu_wait_done; do
    extract "$name" "$vq" 'static void' >> "$work/context-production.h"
done
extract virtio_gpu_get_vbuf "$vq" 'static struct virtio_gpu_vbuffer *' >> "$work/context-production.h"
extract virtio_gpu_alloc_cmd "$vq" 'static void *' >> "$work/context-production.h"
for name in free_vbuf virtio_gpu_finish_vbuf virtio_gpu_cancel_vbuf; do
    extract "$name" "$vq" 'static void' >> "$work/context-production.h"
done
for name in virtio_gpu_response_error virtio_gpu_queue_sync \
    virtio_gpu_cmd_context_create virtio_gpu_cmd_context_destroy; do
    extract "$name" "$vq" 'static int' >> "$work/context-production.h"
done
extract virtio_gpu_context_create "$kms" 'static int' >> "$work/context-production.h"
extract virtio_gpu_context_destroy "$kms" 'static void' >> "$work/context-production.h"
extract virtio_gpu_driver_open "$kms" 'static int' >> "$work/context-production.h"
extract virtio_gpu_driver_postclose "$kms" 'static void' >> "$work/context-production.h"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -pthread \
    ${CONTEXT_TEST_CFLAGS:-} -I"$work" -I"$src/sys/external/bsd/drm2/include" \
    "$src/ember/tools/virtgpu-context-fixture.c" -o "$work/test"
"$work/test"
