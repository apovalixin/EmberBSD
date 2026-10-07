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
if grep -q '^enum virtgpu_operation_kind ' "$hdr"; then
    # Keep older baseline fixtures source-compatible with renamed ledger fields.
    cat >> "$layout" <<'C'
#define TRANSFER_FOUNDATION 1
#define virtgpu_exec_member virtgpu_operation_member
#define exec_members operation_members
#define exec_pending operation_pending
#define exec_fence operation_fence
#define virtgpu_exec_prepare virtgpu_operation_prepare
#define virtgpu_exec_post virtgpu_operation_post
#define virtgpu_exec_finish virtgpu_operation_finish
C
    sed -n '/^enum virtgpu_operation_kind /,/^$/p; /^struct virtgpu_operation_member {/,/^};/p' "$hdr" >> "$layout"
else
    printf '#define operation exec\n' >> "$layout"
fi
sed -n '/^#define VIRTGPU_CLASSIC_FENCE_MAX /p' "$hdr" >> "$layout"
if grep -q '^#define VIRTGPU_EXEC_MAX_OBJECTS' "$hdr"; then
    printf '#define EXEC_FOUNDATION 1\n' >> "$layout"
    sed -n '/^#define VIRTGPU_EXEC_/p; /^struct virtgpu_exec_member {/,/^};/p; /^struct virtio_gpu_attachment {/,/^};/p' "$hdr" >> "$layout"
fi
sed -n '/^enum virtgpu_dma_lease /,/^};/p' "$hdr" >> "$layout"
if [ -f "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" ]; then
    printf '#define DMA_LEASE_SOURCE 1\n' >> "$layout"
fi
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
native="$work/submit-native-resv.h"
: > "$native"
for name in ww_acquire_init ww_acquire_done ww_acquire_fini; do
    extract "$name" "$base/linux/linux_ww_mutex.c" 'static void' >> "$native"
done
extract drm_gem_lock_reservations "$base/dist/drm/drm_gem.c" 'static int' >> "$native"
extract drm_gem_unlock_reservations "$base/dist/drm/drm_gem.c" 'static void' >> "$native"
extract dma_resv_add_excl_fence "$base/linux/linux_dma_resv.c" 'static void' >> "$native"
extract dma_resv_get_excl "$base/linux/linux_dma_resv.c" 'static struct dma_fence * __attribute__((unused))' >> "$native"
extract dma_resv_get_list "$base/linux/linux_dma_resv.c" 'static struct dma_resv_list * __attribute__((unused))' >> "$native"
prod="$work/submit-production.h"
: > "$prod"

if [ "${COMPLETION_CONTRACT:-0}" = 1 ]; then
    extract virtio_gpu_reset_work "$base/dist/drm/virtio/virtgpu_kms.c" 'static void' > "$work/completion-production.h"
    extract virtio_gpu_stop "$base/dist/drm/virtio/virtgpu_kms.c" 'static void' >> "$work/completion-production.h"
    extract reclaim_vbufs "$vq" 'static void' >> "$work/completion-production.h"
    extract virtio_gpu_dequeue_ctrl_func "$vq" 'static void' >> "$work/completion-production.h"
    extract virtio_gpu_queue_cursor "$vq" 'static void' >> "$work/completion-production.h"
fi
extract fd_abort "$src/sys/kern/kern_descrip.c" 'static void' > "$work/submit-fd-abort.h"
if [ "${FENCE_CONTRACT:-0}" = 1 ]; then
    extract __dma_fence_is_later "$base/linux/linux_dma_fence.c" 'static bool' >> "$prod"
fi
extract dma_fence_get_status "$base/linux/linux_dma_fence.c" 'static int' >> "$prod"
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

if grep -q '^#define VIRTGPU_EXEC_MAX_OBJECTS' "$hdr"; then
    extract virtio_gpu_exec_charge "$gem" 'static int' >> "$prod"
    extract virtio_gpu_exec_uncharge "$gem" 'static void' >> "$prod"
    extract virtio_gpu_context_key "$gem" 'static int __attribute__((unused))' >> "$prod"
    if grep -q '^virtio_gpu_dependency_status(' "$fence"; then
        extract virtio_gpu_dependency_status "$fence" 'static int' >> "$prod"
        extract virtio_gpu_wait_dependency "$fence" 'static int' >> "$prod"
    fi
    extract virtio_gpu_exec_dependency "$fence" 'static int' >> "$prod"
fi
extract virtio_gpu_array_alloc "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
for name in virtio_gpu_array_free virtio_gpu_array_put_free; do extract "$name" "$gem" 'static void' >> "$prod"; done
# Baseline ioctl and completion cases call this; whole-context EXEC does not.
if [ "${COMPLETION_CONTRACT:-0}" = 1 ] || [ "${TRANSFER_CONTRACT:-0}" = 1 ] ||
    ! grep -q '^#define VIRTGPU_EXEC_MAX_OBJECTS' "$hdr"; then
    extract virtio_gpu_array_from_handles "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
fi
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
if grep -q '^#define VIRTGPU_EXEC_MAX_OBJECTS' "$hdr"; then
    extract virtio_gpu_array_add_obj "$gem" 'static void' >> "$prod"
    if grep -q '^virtio_gpu_operation_array_alloc(' "$gem"; then
        extract virtio_gpu_operation_array_alloc "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
    fi
    extract virtio_gpu_exec_array_alloc "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
    extract virtgpu_exec_sift "$gem" 'static void' >> "$prod"
    extract virtio_gpu_exec_snapshot "$gem" 'static int' >> "$prod"
    dma="$base/virtio/virtgpu_dma.c"
if grep -q '^virtgpu_operation_prepare(' "$dma"; then
    extract virtgpu_operation_sync_ops "$dma" 'static int' >> "$prod"
    extract virtgpu_operation_prepare "$dma" 'static int' >> "$prod"
    extract virtgpu_operation_post "$dma" 'static void' >> "$prod"
    extract virtgpu_operation_finish "$dma" 'static void' >> "$prod"
    extract virtio_gpu_object_dependencies "$dma" 'static int' >> "$prod"
else
    extract virtgpu_exec_prepare "$dma" 'static int' >> "$prod"
    extract virtgpu_exec_post "$dma" 'static void' >> "$prod"
    extract virtgpu_exec_finish "$dma" 'static void' >> "$prod"
fi
    extract virtio_gpu_exec_dependencies "$dma" 'static int' >> "$prod"
fi
if [ "${TRANSFER_CONTRACT:-0}" = 1 ]; then
    sed -n '/^struct drm_virtgpu_3d_box {/,/^};/p; /^struct drm_virtgpu_3d_transfer_from_host {/,/^};/p; /^struct drm_virtgpu_3d_transfer_to_host {/,/^};/p; /^struct drm_virtgpu_3d_wait {/,/^};/p; /^#define VIRTGPU_WAIT_NOWAIT/p' "$base/dist/include/uapi/drm/virtgpu_drm.h" >> "$layout"
    printf 'static void\n' >> "$prod"
    sed -n '/^virtio_gpu_complete_transfer(/,/^}/p' "$vq" >> "$prod"
fi
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
if [ "${TRANSFER_CONTRACT:-0}" = 1 ]; then
    extract convert_to_hw_box "$vq" 'static void' >> "$prod"
    for name in virtio_gpu_cmd_transfer_to_host_3d virtio_gpu_cmd_transfer_from_host_3d; do
        type=$(awk -v name="$name" '$0 ~ "^(void|int) " name "[(]" { print "static " $1 }' "$vq")
        extract "$name" "$vq" "$type" >> "$prod"
    done
if grep -q '^virtio_gpu_transfer_3d_ioctl(' "$base/dist/drm/virtio/virtgpu_ioctl.c"; then
    extract virtio_gpu_dependency_alloc "$gem" 'static struct dma_fence **' >> "$prod"
    extract virtio_gpu_transfer_member "$gem" 'static int' >> "$prod"
    extract virtio_gpu_transfer_3d_ioctl "$base/dist/drm/virtio/virtgpu_ioctl.c" 'static int' >> "$prod"
fi
    for name in virtio_gpu_transfer_from_host_ioctl virtio_gpu_transfer_to_host_ioctl virtio_gpu_wait_ioctl; do
        extract "$name" "$base/dist/drm/virtio/virtgpu_ioctl.c" 'static int' >> "$prod"
    done
fi
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${SUBMIT_TEST_CFLAGS:-} -I"$work" -I"$base/include" \
    -I"$tools" "$tools/virtgpu-submit-fixture.c" -o "$work/test"
"$work/test"
