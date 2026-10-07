/* Origin: EmberBSD; AI-assisted eligible backing lease and request retirement. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#include "virtgpu_drv.h"

/* Caller holds dma_lock. No storage is freed while the claim is published. */
static bool
virtgpu_dma_finalize_claim(struct virtio_gpu_object *bo)
{
	if (!bo->release_pending || bo->dma_finalizing || bo->dma_members ||
	    bo->dma_retire_refs || bo->dma_lease == VIRTGPU_LEASE_OPEN ||
	    bo->dma_lease == VIRTGPU_LEASE_CLOSING)
		return false;
	bo->dma_finalizing = true;
	if (bo->dma_lease == VIRTGPU_LEASE_CLOSED)
		list_del_init(&bo->dma_registry);
	return true;
}

static void
virtgpu_dma_sync(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object *bo, int ops)
{
	bus_dmamap_sync(vgdev->vdev->dmat, bo->pages->sgl->sg_dmamap,
	    0, bo->base.base.size, ops);
}

void
virtio_gpu_dma_stop(struct virtio_gpu_device *vgdev)
{
	spin_lock(&vgdev->dma_lock);
	vgdev->dma_stopped = true;
	spin_unlock(&vgdev->dma_lock);
}

/* Inside a registered producer/submit barrier; metadata already exists. */
int
virtio_gpu_dma_prepare(struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtgpu_dma_op *op = &vbuf->dma_op;
	struct virtio_gpu_object *bo = op->bo;
	bool lease = false;
	int ret = 0;

	if (bo == NULL)
		return 0;
	spin_lock(&vgdev->dma_lock);
	KASSERT(bo->dma_required && op->state == VIRTGPU_DMA_IDLE);
	if (vgdev->dma_stopped || bo->release_pending ||
	    bo->dma_lease == VIRTGPU_LEASE_CLOSING) {
		ret = -ENODEV;
		goto out;
	}
	/* A never-exposed or already reset backing needs no UNREF DMA phase. */
	if (op->kind == VIRTGPU_DMA_UNREF &&
	    bo->dma_lease != VIRTGPU_LEASE_OPEN)
		goto out;
	if (bo->dma_members == UINT_MAX) {
		ret = -EOVERFLOW;
		goto out;
	}
	if (bo->dma_lease == VIRTGPU_LEASE_NONE) {
		KASSERT(op->kind == VIRTGPU_DMA_ATTACH);
		bo->dma_lease = VIRTGPU_LEASE_OPEN;
		list_add_tail(&bo->dma_registry, &vgdev->dma_leases);
		lease = true;
	}
	KASSERT(bo->dma_lease == VIRTGPU_LEASE_OPEN);
	bo->dma_members++;
	op->state = VIRTGPU_DMA_PREPARED;
	spin_unlock(&vgdev->dma_lock);
	/* Reset joins this producer before attempting lease closure. */
	if (lease)
		virtgpu_dma_sync(vgdev, bo,
	    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	virtgpu_dma_sync(vgdev, bo,
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE);
	return 0;
out:
	spin_unlock(&vgdev->dma_lock);
	return ret;
}

/* End one attempt only; ENOSPC may rearm the same preallocated record. */
void
virtio_gpu_dma_post(struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtgpu_dma_op *op = &vbuf->dma_op;
	struct virtio_gpu_object *bo = op->bo;

	if (bo == NULL)
		return;
	spin_lock(&vgdev->dma_lock);
	if (op->state != VIRTGPU_DMA_PREPARED) {
		spin_unlock(&vgdev->dma_lock);
		return;
	}
	op->state = VIRTGPU_DMA_FINISHING;
	bo->dma_retire_refs++;
	spin_unlock(&vgdev->dma_lock);
	virtgpu_dma_sync(vgdev, bo,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	spin_lock(&vgdev->dma_lock);
	KASSERT(bo->dma_members && bo->dma_retire_refs);
	/* The cookie still owns the BO; lease closure follows this token. */
	KASSERT(bo->dma_lease == VIRTGPU_LEASE_OPEN);
	bo->dma_members--;
	bo->dma_retire_refs--;
	op->state = VIRTGPU_DMA_IDLE;
	spin_unlock(&vgdev->dma_lock);
}

/* The claim already owns a temporary raw retirement pin, never a GEM ref. */
static void
virtgpu_dma_close_claimed(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object *bo)
{
	bool finalize;

	virtgpu_dma_sync(vgdev, bo,
	    BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE);
	spin_lock(&vgdev->dma_lock);
	KASSERT(bo->dma_lease == VIRTGPU_LEASE_CLOSING && bo->dma_retire_refs);
	bo->dma_lease = VIRTGPU_LEASE_CLOSED;
	bo->dma_retire_refs--;
	finalize = virtgpu_dma_finalize_claim(bo);
	spin_unlock(&vgdev->dma_lock);
	if (finalize)
		virtio_gpu_finalize_object(bo);
}

void
virtio_gpu_dma_finish(struct virtio_gpu_vbuffer *vbuf, int error)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtgpu_dma_op *op = &vbuf->dma_op;
	struct virtio_gpu_object *bo = op->bo;
	bool close = false;

	virtio_gpu_dma_post(vbuf);
	if (bo == NULL || op->kind != VIRTGPU_DMA_UNREF || error)
		return;
	/* Only verified fenced UNREF success closes here; errors use reset. */
	spin_lock(&vgdev->dma_lock);
	if (bo->dma_lease == VIRTGPU_LEASE_OPEN) {
		KASSERT(!bo->dma_members);
		bo->dma_lease = VIRTGPU_LEASE_CLOSING;
		bo->dma_retire_refs++;
		close = true;
	}
	spin_unlock(&vgdev->dma_lock);
	if (close)
		virtgpu_dma_close_claimed(vgdev, bo);
}

/* After producer/dequeue joins and del_vqs, before terminal fence publication. */
void
virtio_gpu_dma_reset(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_object *bo, *chosen;

	for (;;) {
		chosen = NULL;
		spin_lock(&vgdev->dma_lock);
		list_for_each_entry(bo, &vgdev->dma_leases, dma_registry) {
			/* All successful UNREF closers were joined above. */
			KASSERT(bo->dma_lease != VIRTGPU_LEASE_CLOSING);
			if (bo->dma_lease != VIRTGPU_LEASE_OPEN)
				continue;
			KASSERT(!bo->dma_members);
			bo->dma_lease = VIRTGPU_LEASE_CLOSING;
			bo->dma_retire_refs++;
			chosen = bo;
			break;
		}
		spin_unlock(&vgdev->dma_lock);
		if (chosen == NULL)
			break;
		virtgpu_dma_close_claimed(vgdev, chosen);
	}
}

void
virtio_gpu_dma_release(struct virtio_gpu_object *bo)
{
	struct virtio_gpu_device *vgdev = bo->base.base.dev->dev_private;
	bool finalize;

	spin_lock(&vgdev->dma_lock);
	bo->release_pending = true;
	finalize = virtgpu_dma_finalize_claim(bo);
	spin_unlock(&vgdev->dma_lock);
	if (finalize)
		virtio_gpu_finalize_object(bo);
}
