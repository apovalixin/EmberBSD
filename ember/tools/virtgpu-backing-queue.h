/* Origin: EmberBSD; AI-assisted transport seams for production backing paths. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* The A fence algorithm has its own contract. Here emission/retention is a seam. */
static struct netbsd_virtqueue backing_vq;
static struct virtio_gpu_vbuffer *immediate;
static enum response_mode immediate_mode;
static unsigned int pressure_rejects, pressure_waits, joined, terminal;
static unsigned int fixture_ticks, callback_checks;
static struct virtio_gpu_vbuffer *used_cookie, *current_response;
static void backing_sync_check(void) {
	if (current_response && current_response->error) assert(resets);
	assert(pthread_mutex_trylock(&gpu.dma_lock.value) == 0);
	assert(pthread_mutex_unlock(&gpu.dma_lock.value) == 0);
	assert(pthread_mutex_trylock(&gpu.ctrlq.qlock.value) == 0);
	assert(pthread_mutex_unlock(&gpu.ctrlq.qlock.value) == 0);
	assert(pthread_mutex_trylock(&gpu.fence_drv.lock.value) == 0);
	assert(pthread_mutex_unlock(&gpu.fence_drv.lock.value) == 0);
}
static void backing_wait_check(wait_queue_head_t *q) {
	if (q != &gpu.ctrlq.ack_queue) return;
	int result = pthread_mutex_trylock(&gpu.submit_lock.value);
	if (!result) {
		assert(pthread_mutex_unlock(&gpu.submit_lock.value) == 0);
		return; /* Reset joins outside the submission mutex. */
	}
	assert(result == EBUSY);
#ifdef DMA_LEASE_SOURCE
	if (last_bo) assert(!last_bo->dma_members);
#endif
}
static void backing_callback(struct virtio_gpu_device *d,
    struct virtio_gpu_vbuffer *b) {
	assert(!b->wait || !b->wait->done);
#ifdef DMA_LEASE_SOURCE
	assert(b->dma_op.state == VIRTGPU_DMA_IDLE);
	if (b->dma_op.bo) assert(!b->dma_op.bo->dma_members && rw_post);
#endif
	callback_checks++;
}
#define jiffies fixture_ticks
#define DRM_ERROR(...) do { if (0) fprintf(stderr, __VA_ARGS__); } while (0)
static bool virtio_gpu_fence_space(struct virtio_gpu_device *d) { return true; }

static int virtio_gpu_fence_emit(struct virtio_gpu_device *d,
    struct virtio_gpu_ctrl_hdr *h, struct virtio_gpu_fence *f) {
	f->unref = h->type == VIRTIO_GPU_CMD_RESOURCE_UNREF;
	f->prior_ids = ids_freed;
	h->flags = VIRTIO_GPU_FLAG_FENCE;
	h->fence_id = ++next_fence;
	return 0;
}
static void virtio_gpu_array_add_fence(struct virtio_gpu_object_array *a,
    struct dma_fence *f) { }
static void backing_unlock(struct mutex *m) {
	if (m != &gpu.ctrlq.qlock || !immediate) return;
	struct virtio_gpu_vbuffer *b = immediate;
	immediate = NULL;
	/* Dequeue can finish before enqueue returns, after the queue spin lock. */
	complete(b, immediate_mode);
}
static int virtqueue_add_sgs(struct netbsd_virtqueue *q,
    struct linux_virtio_sg **sgs, unsigned out, unsigned in,
    void *cookie, int flags) {
	struct virtio_gpu_vbuffer *b = cookie;
	struct virtio_gpu_ctrl_hdr *c = (void *)b->buf;
	enum response_mode m = c->type == fault_type ? mode : GOOD;
	if (pressure_rejects) {
		pressure_rejects--;
		/* Make progress visible, but only after this rejected attempt. */
		q->num_free++;
		pressure_waits++;
		return -ENOSPC;
	}
	if (m == SUBMIT_FAIL) return submission_error;
	if (c->type == VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING)
		b->resp_cb = backing_callback;
	host_accept(c);
	if (m == HOLD) {
		assert(!pending);
		pending = b;
	} else {
		assert(!immediate);
		immediate = b;
		immediate_mode = m;
	}
	return 0;
}
static void flush_work(struct work_struct *w) {
	assert(!atomic_read(&gpu.submitters));
	if (w == &gpu.ctrlq.dequeue_work || w == &gpu.cursorq.dequeue_work)
		joined++;
}
static void backing_del_vqs(struct virtio_device *v) {
	assert(resets && joined >= 2 && !immediate);
	if (pending) {
		struct virtio_gpu_vbuffer *b = pending;
		pending = NULL;
		virtio_gpu_cancel_vbuf(b);
	}
}
static void virtio_gpu_fail_fences(struct virtio_gpu_device *d, int error) {
	assert(error < 0 && joined >= 2 && !pending && !immediate);
#ifdef DMA_LEASE_SOURCE
	struct virtio_gpu_object *bo;
	list_for_each_entry(bo, &d->dma_leases, dma_registry)
		assert(bo->dma_lease == VIRTGPU_LEASE_CLOSED &&
		    !bo->dma_members && !bo->dma_retire_refs);
#endif
	terminal++;
}
static void virtgpu_console_drain(struct virtio_gpu_device *d) { assert(terminal); }
#define DRM_DEBUG(...) ((void)0)
static void wake_up(wait_queue_head_t *q) { wake_up_all(q); }
static void virtqueue_disable_cb(struct netbsd_virtqueue *q) { }
static bool virtqueue_enable_cb(struct netbsd_virtqueue *q) { return true; }
static void *virtqueue_get_buf(struct netbsd_virtqueue *q, unsigned int *len) {
	struct virtio_gpu_vbuffer *b = used_cookie;
	if (!b) return NULL;
	*len = b->resp_received;
	used_cookie = NULL;
	return b;
}
#include "backing-queue-production.h"
static void backing_dequeue(struct virtio_gpu_vbuffer *b) {
	assert(!used_cookie && !current_response);
	used_cookie = current_response = b;
	virtio_gpu_dequeue_ctrl_func(&gpu.ctrlq.dequeue_work);
	current_response = NULL;
}
