#!/bin/sh
# Origin: EmberBSD; AI-assisted production EXECBUFFER ownership regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${SUBMIT_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-submit.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v type="$3" '
        $0 ~ /^[A-Za-z_]/ && $0 ~ "(^|[ *])" name "[(]" && $0 !~ /;[[:space:]]*$/ {
            print type; sub("^.*" name "[(]", name "("); copying = 1;
        }
        copying { print }
        copying && /^}/ { found = 1; exit }
        END { if (!found) exit 1 }
    ' "$2"
}
base="$src/sys/external/bsd/drm2"
vq="$base/dist/drm/virtio/virtgpu_vq.c"
gem="$base/dist/drm/virtio/virtgpu_gem.c"
fence="$base/dist/drm/virtio/virtgpu_fence.c"
hdr="$base/dist/drm/virtio/virtgpu_drv.h"
mkdir "$work/linux"
cat > "$work/linux/types.h" <<'C'
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16, __le16;
typedef uint32_t __u32, __le32;
typedef int32_t __s32;
typedef uint64_t __u64, __le64;
C
layout="$work/submit-layout.h"
: > "$layout"
for name in virtio_gpu_fence_driver virtio_gpu_fence virtio_gpu_vbuffer virtio_gpu_fpriv virtio_gpu_object_array; do
    sed -n "/^struct $name {/,/^};/p" "$hdr" >> "$layout"
done
for name in sync_file; do
    sed -n "/^struct $name {/,/^};/p" "$base/include/linux/sync_file.h" >> "$layout"
done
for name in dma_fence_array_cb dma_fence_array; do
    sed -n "/^struct $name {/,/^};/p" "$base/include/linux/dma-fence-array.h" >> "$layout"
done
sed -n '/^struct drm_virtgpu_execbuffer {/,/^};/p; /^#define VIRTGPU_EXECBUF_FENCE_/p; /^#define VIRTGPU_EXECBUF_FLAGS/,/0)/p' \
    "$base/dist/include/uapi/drm/virtgpu_drm.h" >> "$layout"
prod="$work/submit-production.h"
if [ "${COMPLETION_CONTRACT:-0}" = 1 ]; then
    extract virtio_gpu_reset_work "$base/dist/drm/virtio/virtgpu_kms.c" 'static void' > "$work/completion-production.h"
    extract virtio_gpu_stop "$base/dist/drm/virtio/virtgpu_kms.c" 'static void' >> "$work/completion-production.h"
    extract reclaim_vbufs "$vq" 'static void' >> "$work/completion-production.h"
    extract virtio_gpu_dequeue_ctrl_func "$vq" 'static void' >> "$work/completion-production.h"
    extract virtio_gpu_queue_cursor "$vq" 'static void' >> "$work/completion-production.h"
fi
extract fd_abort "$src/sys/kern/kern_descrip.c" 'static void' > "$work/submit-fd-abort.h"
extract dma_fence_get_status "$base/linux/linux_dma_fence.c" 'static int' > "$prod"
for name in dma_fence_array_done1 dma_fence_array_done; do
    extract "$name" "$base/linux/linux_dma_fence_array.c" 'static void' >> "$prod"
done
extract sync_file_create "$base/linux/linux_sync_file.c" 'static struct sync_file *' > "$work/submit-sync-create.h"
extract sync_file_close "$base/linux/linux_sync_file.c" 'static int' >> "$prod"
extract virtio_gpu_fence_alloc "$fence" 'static struct virtio_gpu_fence *' >> "$prod"
if grep -q '^virtio_gpu_fence_complete(' "$fence"; then
    printf '#define COMPLETION_FOUNDATION 1\n' >> "$work/submit-layout.h"
    extract virtio_gpu_fence_publish "$fence" 'static void' >> "$prod"
    extract virtio_gpu_fence_space "$fence" 'static bool' >> "$prod"
    extract virtio_gpu_submit_begin "$fence" 'static bool' >> "$prod"
    extract virtio_gpu_fence_emit "$fence" 'static int' >> "$prod"
    extract virtio_gpu_fence_complete "$fence" 'static void' >> "$prod"
    extract virtio_gpu_fence_stop "$fence" 'static void' >> "$prod"
else
    extract virtio_gpu_fence_emit "$fence" 'static void' >> "$prod"
    extract virtio_gpu_fence_event_process "$fence" 'static void' >> "$prod"
fi
for name in virtio_gpu_fail_fences virtio_gpu_fence_fail; do
    if grep -q "^void $name(" "$fence"; then
        extract "$name" "$fence" 'static void' >> "$prod"
    fi
done

extract virtio_gpu_array_alloc "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
for name in virtio_gpu_array_free virtio_gpu_array_put_free; do extract "$name" "$gem" 'static void' >> "$prod"; done
extract virtio_gpu_array_from_handles "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
extract virtio_gpu_array_lock_resv "$gem" 'static int' >> "$prod"
for name in virtio_gpu_array_unlock_resv virtio_gpu_array_add_fence virtio_gpu_array_put_free_delayed virtio_gpu_array_put_free_work; do
    if [ "$name" = virtio_gpu_array_add_fence ]; then
        printf '#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wsign-compare"\n' >> "$prod"
    fi
    extract "$name" "$gem" 'static void' >> "$prod"
    if [ "$name" = virtio_gpu_array_add_fence ]; then
        printf '#pragma GCC diagnostic pop\n' >> "$prod"
    fi
done
extract virtio_gpu_get_vbuf "$vq" 'static struct virtio_gpu_vbuffer *' >> "$prod"
extract virtio_gpu_alloc_cmd "$vq" 'static void *' >> "$prod"
extract free_vbuf "$vq" 'static void' >> "$prod"
if grep -q '^virtio_gpu_finish_vbuf(' "$vq"; then
    extract virtio_gpu_finish_vbuf "$vq" 'static void' >> "$prod"
    extract virtio_gpu_submit_done "$vq" 'static void' >> "$prod"
    extract virtio_gpu_queue_remaining "$vq" 'static long' >> "$prod"
fi
extract virtio_gpu_cancel_vbuf "$vq" 'static void' >> "$prod"

extract virtio_gpu_response_error "$vq" 'static int' >> "$prod"
extract virtio_gpu_queue_fenced_ctrl_buffer "$vq" 'static int' >> "$prod"
# The baseline helper returned void; preserve its actual interface in RED.
type=$(awk '/^(void|int) virtio_gpu_cmd_submit\(/ { print "static " $1 }' "$vq")
extract virtio_gpu_cmd_submit "$vq" "$type" >> "$prod"
extract virtio_gpu_execbuffer_ioctl "$base/dist/drm/virtio/virtgpu_ioctl.c" 'static int' >> "$prod"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${SUBMIT_TEST_CFLAGS:-} -I"$work" -I"$base/include" \
    -I"$tools" "$tools/virtgpu-submit-fixture.c" -o "$work/test"
"$work/test"
