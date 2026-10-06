#!/bin/sh
# Origin: EmberBSD production VirtGPU queue and memory contracts, 2026-10-06.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-contract.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
extract() {
    awk -v name="$1" -v prefix="$3" '
        $0 ~ "^((static )?(void|int) )?" name "\\(" {
            if (prefix != "") print prefix;
            copying = 1;
        }
        copying { print }
        copying && /^}/ { exit }
    ' "$2"
}
vq="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_vq.c"
cat > "$work/queue.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include "virtgpu_limits.h"
#define HZ 100
#define GFP_ATOMIC 0
#define DRM_ERROR(...) ((void)0)
struct mutex { int held; };
struct dma_fence { int refs, error; bool signaled; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint64_t id; };
struct virtio_gpu_object { int alive; };
struct virtio_gpu_object_array { bool locked; };
struct linux_virtio_sg { void *buffer; size_t length; struct linux_virtio_sg *next; };
struct netbsd_virtqueue { unsigned int num_free; };
struct virtio_gpu_vbuffer {
    char *buf, *resp_buf; int size, resp_size;
    void *data_buf; unsigned int data_size;
    struct virtio_gpu_fence *fence;
    struct virtio_gpu_object_array *objs;
    struct virtio_gpu_object *release;
};
struct virtio_gpu_device {
    struct { struct netbsd_virtqueue *vq; struct mutex qlock; int ack_queue; } ctrlq;
    struct mutex submit_lock;
    bool vqs_ready; int submit_error;
};
static int attempts, canceled, stops, wait_calls;
static int planned_error, first_enospc, progress;
static unsigned int captured_out, captured_in;
static size_t captured_lengths[3];
static void mutex_lock(struct mutex *m) { assert(!m->held); m->held = 1; }
static void mutex_unlock(struct mutex *m) { assert(m->held); m->held = 0; }
#define spin_lock mutex_lock
#define spin_unlock mutex_unlock
static void dma_fence_get(struct dma_fence *f) { f->refs++; }
static void dma_fence_put(struct dma_fence *f) { assert(f->refs > 1); f->refs--; }
static void virtio_gpu_fence_emit(struct virtio_gpu_device *v,
    struct virtio_gpu_ctrl_hdr *hdr, struct virtio_gpu_fence *f) {
    assert(v->submit_lock.held); hdr->id = 1; dma_fence_get(&f->f);
}
static void virtio_gpu_array_add_fence(struct virtio_gpu_object_array *a,
    struct dma_fence *f) { (void)f; assert(a->locked); }
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a) {
    assert(a->locked); a->locked = false;
}
static void virtio_gpu_fence_fail(struct virtio_gpu_fence *f, int error) {
    assert(!f->f.signaled); f->f.error = error; f->f.signaled = true;
    dma_fence_put(&f->f);
}
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    stops++; v->vqs_ready = false; v->submit_error = error;
}
static void virtio_gpu_cancel_vbuf(struct virtio_gpu_vbuffer *b) {
    canceled++; if (b->fence) dma_fence_put(&b->fence->f);
    if (b->release) { assert(stops); b->release->alive = 0; }
}
static int virtqueue_add_sgs(struct netbsd_virtqueue *vq,
    struct linux_virtio_sg **sgs, unsigned int out, unsigned int in,
    void *cookie, int gfp) {
    (void)vq; (void)cookie; (void)gfp;
    attempts++; captured_out = out; captured_in = in;
    for (unsigned int i = 0; i < out + in; i++) {
        assert(sgs[i] && sgs[i]->buffer && sgs[i]->length);
        assert(sgs[i]->next == NULL); captured_lengths[i] = sgs[i]->length;
    }
    if (first_enospc && attempts == 1) return -ENOSPC;
    return planned_error;
}
static int model_wait(struct netbsd_virtqueue *vq, unsigned int before) {
    wait_calls++; if (progress) { vq->num_free = before + 1; return 1; }
    return 0;
}
#define wait_event_timeout(q, cond, timeout) model_wait(vq, before)
C
extract virtio_gpu_queue_fenced_ctrl_buffer "$vq" 'static int' >> "$work/queue.c"
cat >> "$work/queue.c" <<'C'
static void queue_case(int error, bool ready, bool pressure, bool wake,
    bool resource) {
    char cmd[32], resp[24], data[16];
    struct netbsd_virtqueue q = { .num_free = 0 };
    struct virtio_gpu_device dev = { .ctrlq.vq = &q, .vqs_ready = ready };
    struct virtio_gpu_fence fence = { .f.refs = 1 };
    struct virtio_gpu_ctrl_hdr hdr = { 0 };
    struct virtio_gpu_object_array objs = { .locked = true };
    struct virtio_gpu_object bo = { .alive = 1 };
    struct virtio_gpu_vbuffer b = { .buf = cmd, .size = sizeof(cmd),
        .resp_buf = resp, .resp_size = sizeof(resp), .data_buf = data,
        .data_size = sizeof(data), .objs = &objs,
        .release = resource ? &bo : NULL };
    attempts = canceled = stops = wait_calls = 0;
    planned_error = error; first_enospc = pressure; progress = wake;
    int ret = virtio_gpu_queue_fenced_ctrl_buffer(&dev, &b, &hdr, &fence);
    int expected = !ready ? -ENODEV : (pressure && !wake ? -ETIMEDOUT : error);
    assert(ret == expected && !objs.locked && !dev.submit_lock.held);
    if (ready) {
        assert(captured_out == 2 && captured_in == 1);
        assert(captured_lengths[0] == 32 && captured_lengths[1] == 16 &&
            captured_lengths[2] == 24);
    } else assert(attempts == 0);
    if (ret) {
        assert(canceled == 1 && fence.f.refs == 1 && fence.f.error == ret);
        if (resource) assert(stops == 1 && bo.alive == 0);
    } else {
        assert(canceled == 0 && fence.f.refs == 3);
        if (resource) assert(bo.alive);
        virtio_gpu_fence_fail(&fence, -ECANCELED);
        if (resource) virtio_gpu_stop(&dev, -ECANCELED);
        virtio_gpu_cancel_vbuf(&b);
        assert(fence.f.refs == 1);
    }
    if (pressure && ready) assert(wait_calls == 1);
    if (pressure && wake && ready) assert(attempts == 2);
}
int main(void) {
    assert(virtgpu_2d_size_valid(1920, 1080, 1920ULL * 1080 * 4));
    assert(!virtgpu_2d_size_valid(0, 1080, 8192));
    assert(!virtgpu_2d_size_valid(UINT32_MAX, UINT32_MAX, UINT64_MAX));
    assert(!virtgpu_2d_size_valid(64, 64, 64 * 64 * 4 - 1));
    assert(!virtgpu_2d_size_valid(1, 1, VIRTGPU_MAX_OBJECT_SIZE + 1ULL));
    assert(virtgpu_transfer_valid(64, 64, 16384, 4, 8, 60, 56, 2064));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 63, 0, 2, 1, 0));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 1, 1, UINT64_MAX));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 64, 64, 1));
    assert(!virtgpu_transfer_valid(64, 64, 16384, 0, 0, 0, 1, 0));
    const int errors[] = { -EMSGSIZE, -ENODEV, -ENOMEM, -EINVAL, -EIO };
    for (unsigned int i = 0; i < sizeof(errors) / sizeof(errors[0]); i++)
        queue_case(errors[i], true, false, false, false);
    queue_case(0, false, false, false, false);
    queue_case(0, true, false, false, false);
    queue_case(0, true, true, true, false);
    queue_case(0, true, true, false, false);
    queue_case(-ENOMEM, true, false, false, true);
    queue_case(0, true, false, false, true);
    puts("VirtGPU production queue, fence/error ownership and size contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror \
    -I"$src/sys/external/bsd/drm2/virtio" "$work/queue.c" -o "$work/queue"
"$work/queue"
cat > "$work/lifetime.c" <<'C'
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define MAX_INLINE_RESP_SIZE 24
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x102
#define cpu_to_le32(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define container_of(p, t, m) ((t *)((char *)(p) - offsetof(t, m)))
#define gem_to_virtio_gpu_obj(p) container_of(p, struct virtio_gpu_object, base.base)
struct drm_device { void *dev_private; };
struct drm_gem_object { struct drm_device *dev; };
struct virtio_gpu_device { bool vqs_ready; void *vbufs; };
struct virtio_gpu_object {
    struct { struct drm_gem_object base; } base;
    bool created; unsigned int hw_res_handle; bool mapped, pinned;
};
struct dma_fence { int refs; };
struct virtio_gpu_fence { struct dma_fence f; };
struct virtio_gpu_ctrl_hdr { uint32_t type; };
struct virtio_gpu_resource_unref { struct virtio_gpu_ctrl_hdr hdr; uint32_t resource_id; };
struct virtio_gpu_vbuffer {
    struct virtio_gpu_device *vgdev;
    struct virtio_gpu_object *release;
    struct virtio_gpu_fence *fence;
    void *objs, *data_buf, *resp_buf;
    int resp_size; char *buf; char payload[64];
};
static struct virtio_gpu_vbuffer *pending;
static bool host_access;
static int frees, map_frees, id_frees, cookie_frees, resets;
static int allocation_error, submission_error;
static void virtio_gpu_release_object(struct virtio_gpu_object *);
static void virtio_gpu_queue_unref(struct virtio_gpu_device *, struct virtio_gpu_object *);
static void virtio_gpu_cancel_vbuf(void *);
static void free_vbuf(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
static void virtio_gpu_stop(struct virtio_gpu_device *v, int error) {
    assert(error < 0); resets++; host_access = false; v->vqs_ready = false;
}
static void virtio_gpu_object_detach(struct virtio_gpu_device *v,
    struct virtio_gpu_object *bo) {
    (void)v; assert(!host_access);
    if (bo->mapped) { map_frees++; bo->mapped = bo->pinned = false; }
}
static void virtio_gpu_resource_id_put(struct virtio_gpu_device *v, uint32_t id) {
    (void)v; assert(id == 7); id_frees++;
}
static void drm_gem_shmem_free_object(struct drm_gem_object *obj) {
    struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
    assert(!bo->mapped && !bo->pinned); frees++; free(bo);
}
static void *virtio_gpu_alloc_cmd(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer **bp, int size) {
    assert(size <= 64);
    if (allocation_error) return (void *)(intptr_t)-ENOMEM;
    struct virtio_gpu_vbuffer *b = calloc(1, sizeof(*b)); assert(b);
    b->vgdev = v; b->buf = b->payload; b->resp_size = 24;
    b->resp_buf = b->payload + 32; *bp = b; return b->buf;
}
static int virtio_gpu_queue_ctrl_buffer(struct virtio_gpu_device *v,
    struct virtio_gpu_vbuffer *b) {
    assert(((struct virtio_gpu_resource_unref *)b->buf)->resource_id == 7);
    if (submission_error) {
        virtio_gpu_stop(v, submission_error); virtio_gpu_cancel_vbuf(b);
        return submission_error;
    }
    assert(!pending); pending = b; return 0;
}
static void virtio_gpu_wait_done(struct virtio_gpu_vbuffer *b, int error) {
    (void)b; assert(error == -ENODEV);
}
static void virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *v, void *a) {
    (void)v; (void)a; assert(false);
}
static void dma_fence_put(struct dma_fence *f) { assert(f->refs > 0); f->refs--; }
#define kfree free
#define kvfree free
static void kmem_cache_free(void *pool, struct virtio_gpu_vbuffer *b) {
    assert(pool); cookie_frees++; free(b);
}
C
obj="$src/sys/external/bsd/drm2/dist/drm/virtio/virtgpu_object.c"
extract virtio_gpu_release_object "$obj" '' >> "$work/lifetime.c"
extract virtio_gpu_free_object "$obj" '' >> "$work/lifetime.c"
extract free_vbuf "$vq" '' >> "$work/lifetime.c"
extract virtio_gpu_cancel_vbuf "$vq" 'void' >> "$work/lifetime.c"
extract virtio_gpu_queue_unref "$vq" '' >> "$work/lifetime.c"
cat >> "$work/lifetime.c" <<'C'
static void lifetime_case(int how) {
    struct virtio_gpu_device dev = { .vqs_ready = true, .vbufs = &dev };
    struct drm_device drm = { .dev_private = &dev };
    struct virtio_gpu_object *bo = calloc(1, sizeof(*bo)); assert(bo);
    bo->base.base.dev = &drm; bo->hw_res_handle = 7;
    bo->created = true; bo->mapped = bo->pinned = true;
    frees = map_frees = id_frees = cookie_frees = resets = 0;
    allocation_error = how == 2; submission_error = how == 3 ? -ENOMEM : 0;
    host_access = true; pending = NULL;
    virtio_gpu_free_object(&bo->base.base);
    if (how < 2) {
        assert(frees == 0 && map_frees == 0 && id_frees == 0 && pending);
        assert(bo->mapped && bo->pinned && host_access);
        struct virtio_gpu_vbuffer *b = pending; pending = NULL;
        if (how == 0) { host_access = false; free_vbuf(&dev, b); }
        else { virtio_gpu_stop(&dev, -ENODEV); virtio_gpu_cancel_vbuf(b); }
    } else assert(resets == 1);
    assert(!host_access && frees == 1 && map_frees == 1 && id_frees == 1);
    assert(cookie_frees == (how == 2 ? 0 : 1));
}
int main(void) {
    for (int i = 0; i < 4; i++) lifetime_case(i);
    puts("VirtGPU production unref/ACK/reset/cancel GEM lifetime contracts passed");
    return 0;
}
C
${CC:-cc} -std=c11 -Wall -Wextra -Werror "$work/lifetime.c" -o "$work/lifetime"
"$work/lifetime"
