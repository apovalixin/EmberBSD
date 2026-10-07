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
	    bo->dma_retire_refs || bo->operation_pending || bo->dma_lease == VIRTGPU_LEASE_OPEN ||
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

/* Direction belongs to the immutable request record, not the wire reply. */
static int
virtgpu_operation_sync_ops(enum virtgpu_operation_kind kind, bool post)
{
	switch (kind) {
	case VIRTGPU_OPERATION_TO_HOST:
		return post ? BUS_DMASYNC_POSTWRITE : BUS_DMASYNC_PREWRITE;
	case VIRTGPU_OPERATION_FROM_HOST:
		return post ? BUS_DMASYNC_POSTREAD : BUS_DMASYNC_PREREAD;
	default:
		KASSERT(kind == VIRTGPU_OPERATION_EXEC);
		return post ? BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE :
		    BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE;
	}
}

/* Resvs held; registered producer protects PRE against reset closure. */
static int
virtgpu_operation_prepare(struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtio_gpu_object_array *objs = vbuf->objs;
	struct virtio_gpu_object *bo;
	u32 i;
	int ret = 0;

	spin_lock(&vgdev->dma_lock);
	if (vgdev->dma_stopped) {
		ret = -ENODEV;
		goto out;
	}
	KASSERT(!objs->prepared);
	for (i = 0; i < objs->nents; i++) {
		bo = gem_to_virtio_gpu_obj(objs->objs[i]);
		if (bo->dma_lease != VIRTGPU_LEASE_OPEN || bo->release_pending) {
			ret = -ENODEV;
			goto out;
		}
		if (bo->dma_members == UINT_MAX || (!objs->registered &&
		    bo->operation_pending >= vgdev->fence_drv.limit)) {
			ret = -EOVERFLOW;
			goto out;
		}
	}
	if (!objs->registered) {
		objs->operation_fence = dma_fence_get(&vbuf->fence->f);
		for (i = 0; i < objs->nents; i++) {
			bo = gem_to_virtio_gpu_obj(objs->objs[i]);
			objs->members[i].bo = bo;
			objs->members[i].fence = objs->operation_fence;
			list_add_tail(&objs->members[i].node, &bo->operation_members);
			bo->operation_pending++;
		}
		objs->registered = true;
	}
	for (i = 0; i < objs->nents; i++)
		objs->members[i].bo->dma_members++;
	objs->prepared = true;
	spin_unlock(&vgdev->dma_lock);
	/* No failure or allocation after validation: POST will cover every PRE. */
	for (i = 0; i < objs->nents; i++)
		virtgpu_dma_sync(vgdev, objs->members[i].bo,
		    virtgpu_operation_sync_ops(objs->operation, false));
	return 0;
out:
	spin_unlock(&vgdev->dma_lock);
	return ret;
}

static void
virtgpu_operation_post(struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtio_gpu_object_array *objs = vbuf->objs;
	struct virtio_gpu_object *bo;
	u32 i;

	spin_lock(&vgdev->dma_lock);
	if (!objs->prepared) {
		spin_unlock(&vgdev->dma_lock);
		return;
	}
	objs->prepared = false;
	/* One post owner per cookie; each pin is bounded by an active member. */
	for (i = 0; i < objs->nents; i++) {
		bo = objs->members[i].bo;
		KASSERT(bo->dma_retire_refs < bo->dma_members);
		bo->dma_retire_refs++;
	}
	spin_unlock(&vgdev->dma_lock);
	for (i = 0; i < objs->nents; i++)
		virtgpu_dma_sync(vgdev, objs->members[i].bo,
		    virtgpu_operation_sync_ops(objs->operation, true));
	spin_lock(&vgdev->dma_lock);
	for (i = 0; i < objs->nents; i++) {
		bo = objs->members[i].bo;
		KASSERT(bo->dma_lease == VIRTGPU_LEASE_OPEN);
		bo->dma_members--;
		bo->dma_retire_refs--;
	}
	spin_unlock(&vgdev->dma_lock);
}

static void
virtgpu_operation_finish(struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_device *vgdev = vbuf->vgdev;
	struct virtio_gpu_object_array *objs = vbuf->objs;
	struct dma_fence *fence = NULL;
	u32 i;

	virtgpu_operation_post(vbuf);
	spin_lock(&vgdev->dma_lock);
	if (objs->registered) {
		for (i = 0; i < objs->nents; i++) {
			list_del_init(&objs->members[i].node);
			objs->members[i].bo->operation_pending--;
		}
		objs->registered = false;
		fence = objs->operation_fence;
		objs->operation_fence = NULL;
	}
	spin_unlock(&vgdev->dma_lock);
	if (fence)
		dma_fence_put(fence);
}

/*
 * One BO's ledger snapshot; reservations stabilize all new additions.
 * Shared reservation fences have their own count, independent of our limit.
 */
int
virtio_gpu_object_dependencies(struct virtio_gpu_device *vgdev,
    struct drm_gem_object *obj, struct dma_fence **scratch, unsigned int capacity,
    u64 key, unsigned int started, bool implicit, bool nowait, bool wait_request)
{
	struct virtgpu_operation_member *member;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct dma_resv_list *shared;
	struct dma_fence *f, *exclusive;
	unsigned int n = 0, j, pass;
	int ret = 0, status;
	bool busy = false;

	spin_lock(&vgdev->dma_lock);
	list_for_each_entry(member, &bo->operation_members, node) {
		KASSERT(n < capacity);
		scratch[n++] = dma_fence_get(member->fence);
	}
	spin_unlock(&vgdev->dma_lock);
	exclusive = dma_resv_get_excl(obj->resv);
	shared = dma_resv_get_list(obj->resv);
	/* Check every already-known error before waiting on any pending fence. */
	for (pass = 0; pass < (nowait ? 1U : 2U); pass++) {
		for (j = 0; j < n; j++) {
			status = virtio_gpu_dependency_status(scratch[j]);
			if (status < 0) {
				ret = status;
				goto out;
			}
			busy |= status == 0;
			if (pass) {
				ret = wait_request ?
				    virtio_gpu_wait_dependency(scratch[j], started) :
				    virtio_gpu_exec_dependency(vgdev, scratch[j],
				    key, started, implicit);
				if (ret)
					goto out;
			}
		}
		if (exclusive) {
			f = dma_fence_get(exclusive);
			status = virtio_gpu_dependency_status(f);
			busy |= status == 0;
			ret = status < 0 ? status : 0;
			if (!ret && pass)
				ret = wait_request ?
				    virtio_gpu_wait_dependency(f, started) :
				    virtio_gpu_exec_dependency(vgdev, f, key, started,
				    implicit);
			dma_fence_put(f);
			if (ret)
				goto out;
		}
		for (j = 0; shared && j < shared->shared_count; j++) {
			f = dma_fence_get(shared->shared[j]);
			status = virtio_gpu_dependency_status(f);
			busy |= status == 0;
			ret = status < 0 ? status : 0;
			if (!ret && pass)
				ret = wait_request ?
				    virtio_gpu_wait_dependency(f, started) :
				    virtio_gpu_exec_dependency(vgdev, f, key, started,
				    implicit);
			dma_fence_put(f);
			if (ret)
				goto out;
		}
	}
	if (nowait && busy)
		ret = -EBUSY;
out:
	for (j = 0; j < n; j++)
		dma_fence_put(scratch[j]);
	return ret;
}

/* All snapshot reservations held. Only positively typed EXEC may skip waits. */
int
virtio_gpu_exec_dependencies(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object_array *objs, struct dma_fence **scratch,
    unsigned int capacity, u64 key, unsigned int started)
{
	u32 i;
	int ret;

	for (i = 0; i < objs->nents; i++) {
		ret = virtio_gpu_object_dependencies(vgdev, objs->objs[i], scratch,
		    capacity, key, started, true, false, false);
		if (ret)
			return ret;
	}
	return 0;
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

	if (vbuf->objs && vbuf->objs->operation)
		return virtgpu_operation_prepare(vbuf);
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

	if (vbuf->objs && vbuf->objs->operation) {
		virtgpu_operation_post(vbuf);
		return;
	}
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

	if (vbuf->objs && vbuf->objs->operation) {
		virtgpu_operation_finish(vbuf);
		return;
	}
	virtio_gpu_dma_post(vbuf);
	if (bo == NULL || op->kind != VIRTGPU_DMA_UNREF || error)
		return;
	/* Only verified fenced UNREF success closes here; errors use reset. */
	spin_lock(&vgdev->dma_lock);
	if (bo->dma_lease == VIRTGPU_LEASE_OPEN) {
		KASSERT(!bo->dma_members && !bo->operation_pending && !bo->dma_retire_refs);
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
			KASSERT(!bo->dma_members && !bo->operation_pending && !bo->dma_retire_refs);
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
