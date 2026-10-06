/* Origin: EmberBSD native VirtIO transport, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <sys/errno.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/systm.h>

#include <dev/pci/virtiovar.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>

struct linux_virtio_mapping {
	bus_dmamap_t map;
	bool readable;
	bool loaded;
};

struct linux_virtio_request {
	void *cookie;
	unsigned int capacity;
	unsigned int nmaps;
	unsigned int descriptors;
	unsigned int cost;
	bool synced;
	struct linux_virtio_mapping maps[];
};

static void linux_virtio_reset(struct virtio_device *);
static void linux_virtio_del_vqs(struct virtio_device *);

static const struct virtio_config_ops linux_virtio_config = {
	.reset = linux_virtio_reset,
	.del_vqs = linux_virtio_del_vqs,
};

static size_t
linux_virtio_request_size(unsigned int capacity)
{

	return sizeof(struct linux_virtio_request) +
	    capacity * sizeof(struct linux_virtio_mapping);
}

static void
linux_virtio_request_free(struct virtio_device *vdev,
    struct linux_virtio_request *req)
{
	struct linux_virtio_mapping *m;
	unsigned int i;

	for (i = 0; i < req->nmaps; i++) {
		m = &req->maps[i];
		if (m->loaded) {
			if (req->synced)
				bus_dmamap_sync(vdev->dmat, m->map, 0,
				    m->map->dm_mapsize, m->readable ?
				    BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_POSTREAD);
			bus_dmamap_unload(vdev->dmat, m->map);
		}
		bus_dmamap_destroy(vdev->dmat, m->map);
	}
	kmem_intr_free(req, linux_virtio_request_size(req->capacity));
}

static int
linux_virtio_intr(void *arg)
{
	struct netbsd_virtqueue *vq = arg;
	struct virtio_device *vdev = vq->vdev;
	int handled = 0;

	mutex_enter(&vdev->lock);
	if (vdev->state == LINUX_VIRTIO_READY && vq->callbacks &&
	    vq->callback != NULL) {
		/* Consumer callbacks schedule work; they must not reenter us. */
		vq->callback(vq);
		handled = 1;
	}
	mutex_exit(&vdev->lock);
	return handled;
}

int
linux_virtio_init(struct virtio_device *vdev, struct virtio_softc *native,
    device_t dev, int ipl, size_t max_request,
    int (*config_change)(struct virtio_softc *),
    void (*config_callback)(struct virtio_device *), void (*cancel)(void *))
{

	if (native == NULL || dev == NULL || cancel == NULL ||
	    ((config_change == NULL) != (config_callback == NULL)) ||
	    max_request == 0 || max_request > INT_MAX)
		return -EINVAL;
	memset(vdev, 0, sizeof(*vdev));
	vdev->dev = dev;
	vdev->native = native;
	vdev->dmat = virtio_dmat(native);
	vdev->features = virtio_features(native);
	vdev->max_request = max_request;
	vdev->config_change = config_change;
	vdev->config_callback = config_callback;
	vdev->cancel = cancel;
	vdev->config = &linux_virtio_config;
	vdev->state = LINUX_VIRTIO_INIT;
	mutex_init(&vdev->lock, MUTEX_SPIN, ipl);
	return 0;
}

int
linux_virtio_config_interrupt(struct virtio_device *vdev)
{
	int handled = 0;

	mutex_enter(&vdev->lock);
	if (vdev->state == LINUX_VIRTIO_READY &&
	    vdev->config_callback != NULL) {
		vdev->config_callback(vdev);
		handled = 1;
	}
	mutex_exit(&vdev->lock);
	return handled;
}

/* Process context; the child serializes attach/detach and drains its work. */
int
virtio_find_vqs(struct virtio_device *vdev, unsigned int nvqs,
    struct netbsd_virtqueue **vqs, vq_callback_t **callbacks,
    const char *const *names, void *affinity)
{
	struct netbsd_virtqueue *vq;
	unsigned int i;
	int error;

	if (vdev->state != LINUX_VIRTIO_INIT || nvqs == 0 ||
	    nvqs > LINUX_VIRTIO_MAX_QUEUES || vqs == NULL ||
	    callbacks == NULL || names == NULL || affinity != NULL)
		return -EINVAL;
	for (i = 0; i < nvqs; i++) {
		vqs[i] = NULL;
		if (callbacks[i] == NULL || names[i] == NULL)
			return -EINVAL;
	}
	vdev->native_queues = kmem_zalloc(nvqs * sizeof(struct virtqueue),
	    KM_SLEEP);
	vdev->nvqs = nvqs;
	for (i = 0; i < nvqs; i++) {
		vq = &vdev->queues[i];
		vq->vdev = vdev;
		vq->native = &vdev->native_queues[i];
		vq->callback = callbacks[i];
		virtio_init_vq(vdev->native, vq->native, i,
		    linux_virtio_intr, vq);
		/* Native descriptor allocator uses 32768 as its sentinel. */
		if (vq->native->vq_num == 0 || vq->native->vq_num > 32768) {
			error = -EINVAL;
			goto fail;
		}
		vq->size = vq->native->vq_num;
		error = virtio_alloc_vq(vdev->native, vq->native,
		    vdev->max_request, MIN(vq->size, 128), names[i]);
		if (error != 0) {
			/* The native allocator exposes only -1 for allocation failure. */
			error = -ENOMEM;
			goto fail;
		}
		vq->requests = kmem_zalloc(vq->size * sizeof(*vq->requests),
		    KM_SLEEP);
		vq->num_free = vq->size;
		vq->callbacks = true;
	}
	mutex_enter(&vdev->lock);
	vdev->state = LINUX_VIRTIO_QUEUES;
	mutex_exit(&vdev->lock);
	/* Even a failed finish leaves native sc_vqs referring to our array. */
	vdev->attached = true;
	if (virtio_child_attach_finish(vdev->native, vdev->native_queues,
	    nvqs, vdev->config_change, VIRTIO_F_INTR_MPSAFE) != 0) {
		error = -EIO;
		goto fail;
	}
	for (i = 0; i < nvqs; i++)
		vqs[i] = &vdev->queues[i];
	return 0;

fail:
	linux_virtio_del_vqs(vdev);
	return error;
}

void
virtio_device_ready(struct virtio_device *vdev)
{

	mutex_enter(&vdev->lock);
	if (vdev->state == LINUX_VIRTIO_QUEUES)
		vdev->state = LINUX_VIRTIO_READY;
	mutex_exit(&vdev->lock);
}

bool
virtio_has_feature(struct virtio_device *vdev, unsigned int bit)
{

	return bit < 64 && (vdev->features & (UINT64_C(1) << bit)) != 0;
}

bool
virtio_has_iommu_quirk(struct virtio_device *vdev)
{

	return !virtio_has_feature(vdev, LINUX_VIRTIO_F_ACCESS_PLATFORM);
}

uint32_t
linux_virtio_cread_4(struct virtio_device *vdev, unsigned int offset)
{
	uint32_t value = 0;

	mutex_enter(&vdev->lock);
	if (vdev->state != LINUX_VIRTIO_STOPPED)
		value = virtio_read_device_config_le_4(vdev->native, offset);
	mutex_exit(&vdev->lock);
	return value;
}

void
linux_virtio_cwrite_4(struct virtio_device *vdev, unsigned int offset,
    uint32_t value)
{

	mutex_enter(&vdev->lock);
	if (vdev->state != LINUX_VIRTIO_STOPPED)
		virtio_write_device_config_le_4(vdev->native, offset, value);
	mutex_exit(&vdev->lock);
}

int
virtqueue_add_sgs(struct netbsd_virtqueue *vq, struct linux_virtio_sg **sgs,
    unsigned int out, unsigned int in, void *cookie, gfp_t gfp)
{
	struct virtio_device *vdev = vq->vdev;
	struct linux_virtio_request *req;
	struct linux_virtio_mapping *m;
	struct linux_virtio_sg *sg;
	unsigned int entries, i, j;
	size_t bytes;
	int error, slot;

	/* Always nonblocking: VirtGPU submits while holding a spin lock. */
	(void)gfp;
	mutex_enter(&vdev->lock);
	error = linux_virtio_submission_error(vdev->state);
	if (error != 0)
		goto unlock;
	if (cookie == NULL) {
		error = -EINVAL;
		goto unlock;
	}
	error = linux_virtio_sg_count(sgs, out, in, vq->size,
	    vdev->max_request, &entries, &bytes);
	if (error != 0)
		goto unlock;
	req = kmem_intr_zalloc(linux_virtio_request_size(entries), KM_NOSLEEP);
	if (req == NULL) {
		error = -ENOMEM;
		goto unlock;
	}
	req->capacity = entries;
	req->cookie = cookie;
	for (i = 0; i < out + in; i++) {
		for (sg = sgs[i]; sg != NULL; sg = sg->next) {
			m = &req->maps[req->nmaps];
			m->readable = linux_virtio_sg_is_readable(i, out);
			error = bus_dmamap_create(vdev->dmat, sg->length,
			    vq->size, vdev->max_request, 0, BUS_DMA_NOWAIT,
			    &m->map);
			if (error != 0) {
				error = linux_virtio_errno(error);
				goto free_request;
			}
			req->nmaps++;
			error = bus_dmamap_load(vdev->dmat, m->map, sg->buffer,
			    sg->length, NULL, BUS_DMA_NOWAIT |
			    (m->readable ? BUS_DMA_WRITE : BUS_DMA_READ));
			if (error != 0) {
				error = error == EFBIG ? -EMSGSIZE :
				    linux_virtio_errno(error);
				goto free_request;
			}
			m->loaded = true;
			error = linux_virtio_count_descriptors(&req->descriptors,
			    m->map->dm_nsegs, vq->size);
			if (error != 0)
				goto free_request;
		}
	}
	/* Every impossible request has failed before the native assertions. */
	error = virtio_enqueue_prep(vdev->native, vq->native, &slot);
	if (error != 0) {
		error = error == EAGAIN ? -ENOSPC : linux_virtio_errno(error);
		goto free_request;
	}
	error = virtio_enqueue_reserve(vdev->native, vq->native, slot,
	    req->descriptors);
	if (error != 0) {
		/* Reserve failure returns the head itself.  Do not abort again. */
		error = error == EAGAIN ? -ENOSPC : linux_virtio_errno(error);
		goto free_request;
	}
	req->cost = vq->native->vq_descx[slot].use_indirect ?
	    1 : req->descriptors;
	KASSERT(vq->requests[slot] == NULL);
	KASSERT(req->cost <= vq->num_free);
	for (j = 0; j < req->nmaps; j++) {
		m = &req->maps[j];
		bus_dmamap_sync(vdev->dmat, m->map, 0, m->map->dm_mapsize,
		    m->readable ? BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD);
		/* Native 'write' means memory -> device (host-readable). */
		virtio_enqueue(vdev->native, vq->native, slot, m->map,
		    m->readable);
	}
	req->synced = true;
	vq->requests[slot] = req;
	vq->num_free -= req->cost;
	/*
	 * Native commit(false) leaves avail->idx unpublished.  Publish here
	 * so a driver waiting for room in a notification batch cannot stall.
	 * Native event suppression still applies; kicks may precede notify().
	 */
	virtio_enqueue_commit(vdev->native, vq->native, slot, true);
	error = 0;
	goto unlock;

free_request:
	linux_virtio_request_free(vdev, req);
unlock:
	mutex_exit(&vdev->lock);
	return error;
}

void *
virtqueue_get_buf(struct netbsd_virtqueue *vq, unsigned int *len)
{
	struct virtio_device *vdev = vq->vdev;
	struct linux_virtio_request *req;
	void *cookie = NULL;
	int slot, length;

	mutex_enter(&vdev->lock);
	if (vdev->state != LINUX_VIRTIO_READY ||
	    !virtio_vq_is_enqueued(vdev->native, vq->native) ||
	    virtio_dequeue(vdev->native, vq->native, &slot, &length) != 0)
		goto out;
	req = vq->requests[slot];
	KASSERT(req != NULL);
	cookie = req->cookie;
	if (len != NULL)
		*len = (unsigned int)length;
	vq->num_free += req->cost;
	vq->requests[slot] = NULL;
	/* Return ownership of response memory before returning its cookie. */
	linux_virtio_request_free(vdev, req);
	virtio_dequeue_commit(vdev->native, vq->native, slot);
out:
	mutex_exit(&vdev->lock);
	return cookie;
}

void
virtqueue_disable_cb(struct netbsd_virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;

	mutex_enter(&vdev->lock);
	vq->callbacks = false;
	if (vdev->state == LINUX_VIRTIO_READY)
		virtio_stop_vq_intr(vdev->native, vq->native);
	mutex_exit(&vdev->lock);
}

bool
virtqueue_enable_cb(struct netbsd_virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;
	bool empty = true;

	mutex_enter(&vdev->lock);
	if (vdev->state == LINUX_VIRTIO_READY) {
		vq->callbacks = true;
		empty = virtio_start_vq_intr(vdev->native, vq->native) == 0;
	}
	/* After reset, true lets a disable/drain/enable loop terminate. */
	mutex_exit(&vdev->lock);
	return empty;
}

bool
virtqueue_kick_prepare(struct netbsd_virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;
	bool ready;

	mutex_enter(&vdev->lock);
	ready = vdev->state == LINUX_VIRTIO_READY;
	mutex_exit(&vdev->lock);
	/* Native commit handles event suppression together with publication. */
	return ready;
}

bool
virtqueue_notify(struct netbsd_virtqueue *vq)
{
	struct virtio_device *vdev = vq->vdev;
	bool ready;

	mutex_enter(&vdev->lock);
	ready = vdev->state == LINUX_VIRTIO_READY;
	if (ready)
		virtio_enqueue_commit(vdev->native, vq->native, -1, true);
	mutex_exit(&vdev->lock);
	return ready;
}

static void
linux_virtio_reset(struct virtio_device *vdev)
{
	unsigned int i;

	mutex_enter(&vdev->lock);
	if (vdev->state != LINUX_VIRTIO_STOPPED) {
		vdev->state = LINUX_VIRTIO_STOPPED;
		for (i = 0; i < vdev->nvqs; i++)
			vdev->queues[i].callbacks = false;
		/* Waits for any running queue callback via the same lock. */
		virtio_reset(vdev->native);
	}
	mutex_exit(&vdev->lock);
}

static void
linux_virtio_del_vqs(struct virtio_device *vdev)
{
	struct netbsd_virtqueue *vq;
	struct linux_virtio_request *req;
	unsigned int i, slot;
	void *cookie;
	int error;

	linux_virtio_reset(vdev);
	/* Disestablish and drain native interrupts before freeing any ring. */
	if (vdev->attached) {
		virtio_child_detach(vdev->native);
		vdev->attached = false;
	}
	for (i = 0; i < vdev->nvqs; i++) {
		vq = &vdev->queues[i];
		if (vq->native == NULL)
			continue;
		if (vq->requests != NULL) {
			for (slot = 0; slot < vq->size; slot++) {
				req = vq->requests[slot];
				if (req == NULL)
					continue;
				vq->requests[slot] = NULL;
				/* DMA has stopped; cookies still belong to the driver. */
				virtio_dequeue_commit(vdev->native, vq->native, slot);
				cookie = req->cookie;
				linux_virtio_request_free(vdev, req);
				vdev->cancel(cookie);
			}
			kmem_free(vq->requests, vq->size * sizeof(*vq->requests));
			vq->requests = NULL;
		}
		error = virtio_free_vq(vdev->native, vq->native);
		if (error != 0)
			panic("linux_virtio: cannot free queue: %d", error);
		vq->native = NULL;
		vq->num_free = 0;
	}
	if (vdev->native_queues != NULL) {
		kmem_free(vdev->native_queues,
		    vdev->nvqs * sizeof(struct virtqueue));
		vdev->native_queues = NULL;
	}
	vdev->nvqs = 0;
}

void
linux_virtio_fini(struct virtio_device *vdev)
{

	linux_virtio_del_vqs(vdev);
	mutex_destroy(&vdev->lock);
}
