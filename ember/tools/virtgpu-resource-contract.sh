#!/bin/sh
# Origin: EmberBSD; AI-assisted production VirtGPU resource ownership regressions.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=${RESOURCE_SOURCE_ROOT:-$(CDPATH= cd -- "$tools/../.." && pwd)}
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-resource.XXXXXXXX")
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
drm="$src/sys/external/bsd/drm2/dist/drm"
vq="$drm/virtio/virtgpu_vq.c"
gem="$drm/virtio/virtgpu_gem.c"
kms="$drm/virtio/virtgpu_kms.c"
obj="$drm/virtio/virtgpu_object.c"
hdr="$drm/virtio/virtgpu_drv.h"
mkdir "$work/linux"
cat > "$work/linux/types.h" <<'C'
#include <stdint.h>
typedef uint8_t __u8;
typedef uint16_t __u16, __le16;
typedef uint32_t __u32, __le32;
typedef uint64_t __u64, __le64;
C
sed -n '/^struct virtio_gpu_wait {/,/^};/p' "$vq" > "$work/resource-layout.h"
sed -n '/^enum virtgpu_dma_lease /,/^};/p' "$hdr" >> "$work/resource-layout.h"
for name in virtio_gpu_vbuffer virtio_gpu_attachment virtio_gpu_fpriv \
    virtio_gpu_object_params virtio_gpu_object virtio_gpu_object_array; do
    sed -n "/^struct $name {/,/^};/p" "$hdr" >> "$work/resource-layout.h"
done
sed -n '/^struct drm_virtgpu_resource_create {/,/^};/p' \
    "$src/sys/external/bsd/drm2/dist/include/uapi/drm/virtgpu_drm.h" >> "$work/resource-layout.h"
if grep -q '^virtio_gpu_object_dma_check(' "$obj"; then
    printf '#define DMA_ELIGIBILITY_SOURCE 1\n' >> "$work/resource-layout.h"
fi
if [ -f "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" ]; then
    printf '#define DMA_LEASE_SOURCE 1\n' >> "$work/resource-layout.h"
fi
prod="$work/resource-production.h"
extract linux_virtio_reset "$src/sys/external/bsd/drm2/linux/linux_virtio.c" 'static void' > "$prod"
if [ -f "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" ]; then
    sed '/^#include /d' "$src/sys/external/bsd/drm2/virtio/virtgpu_dma.c" >> "$prod"
fi
extract virtio_gpu_stop "$kms" 'static void' >> "$prod"
for name in virtio_gpu_wait_put virtio_gpu_wait_done; do extract "$name" "$vq" 'static void' >> "$prod"; done
extract virtio_gpu_get_vbuf "$vq" 'static struct virtio_gpu_vbuffer *' >> "$prod"
extract virtio_gpu_alloc_cmd "$vq" 'static void *' >> "$prod"
for name in free_vbuf virtio_gpu_finish_vbuf virtio_gpu_cancel_vbuf; do extract "$name" "$vq" 'static void' >> "$prod"; done
for name in virtio_gpu_response_error virtio_gpu_queue_sync virtio_gpu_cmd_context_create \
    virtio_gpu_cmd_context_destroy virtio_gpu_cmd_context_attach_resource \
    virtio_gpu_cmd_context_detach_resource virtio_gpu_cmd_create_resource \
    virtio_gpu_cmd_resource_create_3d virtio_gpu_cmd_resource_attach_backing \
    virtio_gpu_object_attach; do extract "$name" "$vq" 'static int' >> "$prod"; done
for name in virtio_gpu_object_detach virtio_gpu_queue_unref; do extract "$name" "$vq" 'static void' >> "$prod"; done
extract virtio_gpu_array_alloc "$gem" 'static struct virtio_gpu_object_array *' >> "$prod"
for name in virtio_gpu_array_free virtio_gpu_array_add_obj virtio_gpu_array_unlock_resv \
    virtio_gpu_array_put_free; do extract "$name" "$gem" 'static void' >> "$prod"; done
extract virtio_gpu_array_lock_resv "$gem" 'static int' >> "$prod"
extract virtio_gpu_resource_id_get "$obj" 'static int' >> "$prod"
if grep -q 'void virtio_gpu_finalize_object(' "$obj"; then
    extract virtio_gpu_finalize_object "$obj" 'static void' >> "$prod"
fi
for name in virtio_gpu_resource_id_put virtio_gpu_release_object virtio_gpu_free_object; do extract "$name" "$obj" 'static void' >> "$prod"; done
if grep -q '^virtio_gpu_object_dma_check(' "$obj"; then
    extract virtio_gpu_object_owned "$obj" 'static bool' >> "$prod"
    extract virtio_gpu_object_dma_check "$obj" 'static int' >> "$prod"
    extract virtio_gpu_object_dma_admitted "$obj" 'static bool' >> "$prod"
fi
extract virtio_gpu_object_create "$obj" 'static int' >> "$prod"
extract virtio_gpu_gem_object_open "$gem" 'static int' >> "$prod"
extract virtio_gpu_gem_object_close "$gem" 'static void' >> "$prod"
extract virtio_gpu_context_create "$kms" 'static int' >> "$prod"
extract virtio_gpu_context_destroy "$kms" 'static void' >> "$prod"
extract virtio_gpu_driver_open "$kms" 'static int' >> "$prod"
extract virtio_gpu_driver_postclose "$kms" 'static void' >> "$prod"
for name in drm_gem_remove_prime_handles drm_gem_object_handle_free \
    drm_gem_object_exported_dma_buf_free drm_gem_object_handle_put_unlocked; do
    extract "$name" "$drm/drm_gem.c" 'static void' >> "$prod"
done
for name in drm_gem_object_release_handle drm_gem_handle_delete \
    drm_gem_handle_create_tail drm_gem_handle_create drm_gem_open_ioctl; do
    extract "$name" "$drm/drm_gem.c" 'static int' >> "$prod"
done
extract drm_gem_release "$drm/drm_gem.c" 'static void' >> "$prod"
extract drm_gem_prime_fd_to_handle "$drm/drm_prime.c" 'static int' >> "$prod"
extract virtio_gpu_resource_create_ioctl "$drm/virtio/virtgpu_ioctl.c" 'static int' >> "$prod"
if [ "${BACKING_CONTRACT:-0}" = 1 ]; then
    extract virtio_gpu_submit_begin "$drm/virtio/virtgpu_fence.c" 'static bool' > "$work/backing-queue-production.h"
    extract virtio_gpu_submit_done "$vq" 'static void' >> "$work/backing-queue-production.h"
    extract virtio_gpu_queue_remaining "$vq" 'static long' >> "$work/backing-queue-production.h"
    extract virtio_gpu_queue_fenced_ctrl_buffer "$vq" 'static int' >> "$work/backing-queue-production.h"
    extract virtio_gpu_reset_work "$kms" 'static void' >> "$work/backing-queue-production.h"
    extract reclaim_vbufs "$vq" 'static void' >> "$work/backing-queue-production.h"
    extract virtio_gpu_dequeue_ctrl_func "$vq" 'static void' >> "$work/backing-queue-production.h"
fi
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter -pthread \
    ${RESOURCE_TEST_CFLAGS:-} -I"$work" -I"$src/sys/external/bsd/drm2/include" \
    -I"$src/sys/external/bsd/drm2/virtio" \
    -I"$tools" "$tools/virtgpu-resource-fixture.c" -o "$work/test"
"$work/test"
