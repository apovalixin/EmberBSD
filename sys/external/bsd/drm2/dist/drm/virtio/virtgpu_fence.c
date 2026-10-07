/* Origin: EmberBSD native VirtGPU integration of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_fence.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT OWNER(S) AND/OR ITS SUPPLIERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: virtgpu_fence.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <trace/events/dma_fence.h>

#include "virtgpu_drv.h"

#define to_virtio_fence(x) \
	container_of(x, struct virtio_gpu_fence, f)

static const char *virtio_get_driver_name(struct dma_fence *f)
{
	return "virtio_gpu";
}

static const char *virtio_get_timeline_name(struct dma_fence *f)
{
	return "controlq";
}

static void virtio_fence_value_str(struct dma_fence *f, char *str, int size)
{
	snprintf(str, size, "%llu", (unsigned long long)f->seqno);
}

static void virtio_timeline_value_str(struct dma_fence *f, char *str, int size)
{
	struct virtio_gpu_fence *fence = to_virtio_fence(f);

	snprintf(str, size, "%llu", (unsigned long long)atomic64_read(&fence->drv->last_seq));
}

static const struct dma_fence_ops virtio_fence_ops = {
	.get_driver_name     = virtio_get_driver_name,
	.get_timeline_name   = virtio_get_timeline_name,
};

struct virtio_gpu_fence *virtio_gpu_fence_alloc(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	struct virtio_gpu_fence *fence = kzalloc(sizeof(struct virtio_gpu_fence),
							GFP_KERNEL);
	if (!fence)
		return fence;

	fence->drv = drv;
	INIT_LIST_HEAD(&fence->node);

	/* This only partially initializes the fence because the seqno is
	 * unknown yet.  The fence must not be used outside of the driver
	 * until virtio_gpu_fence_emit is called.
	 */
	dma_fence_init(&fence->f, &virtio_fence_ops, &drv->lock, drv->context, 0);

	return fence;
}

/* Only the contiguous ready prefix can leave the shared timeline. */
static void
virtio_gpu_fence_publish(struct virtio_gpu_fence_driver *drv)
{
	struct virtio_gpu_fence *fence, *tmp;

	if (drv->stopped && !drv->drained)
		return;
	list_for_each_entry_safe(fence, tmp, &drv->fences, node) {
		if (!fence->ready)
			break;
		if (fence->result)
			dma_fence_set_error(&fence->f, fence->result);
		dma_fence_signal_locked(&fence->f);
		atomic64_set(&drv->last_seq, fence->f.seqno);
		list_del_init(&fence->node);
		(void)WARN_ON(!drv->pending);
		drv->pending--;
		dma_fence_put(&fence->f);
	}
}

bool
virtio_gpu_fence_space(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	bool space;

	spin_lock(&drv->lock);
	space = drv->stopped || drv->sync_seq >= VIRTGPU_CLASSIC_FENCE_MAX ||
	    (drv->limit && drv->pending < drv->limit);
	spin_unlock(&drv->lock);
	return space;
}

/* Stop seals registration under the same lock before cleanup can join it. */
bool
virtio_gpu_submit_begin(struct virtio_gpu_device *vgdev)
{
	bool admitted;

	spin_lock(&vgdev->fence_drv.lock);
	admitted = !vgdev->fence_drv.stopped;
	if (admitted)
		atomic_inc(&vgdev->submitters);
	spin_unlock(&vgdev->fence_drv.lock);
	return admitted;
}

int virtio_gpu_fence_emit(struct virtio_gpu_device *vgdev,
			 struct virtio_gpu_ctrl_hdr *cmd_hdr,
			 struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	unsigned long irq_flags;
	int ret = 0;

	spin_lock_irqsave(&drv->lock, irq_flags);
	if (drv->stopped)
		ret = -ENODEV;
	else if (drv->sync_seq >= VIRTGPU_CLASSIC_FENCE_MAX) {
		/* Seal atomically; normal reset drains without allocating more IDs. */
		drv->stopped = true;
		drv->stop_error = -EOVERFLOW;
		ret = -EOVERFLOW;
	} else if (!drv->limit || drv->pending >= drv->limit)
		ret = -ENOSPC;
	else if (fence->f.seqno != 0)
		ret = -EINVAL;
	else {
		fence->f.seqno = ++drv->sync_seq;
		dma_fence_get(&fence->f);
		list_add_tail(&fence->node, &drv->fences);
		drv->pending++;
	}
	spin_unlock_irqrestore(&drv->lock, irq_flags);
	if (ret)
		return ret;

	trace_dma_fence_emit(&fence->f);
	cmd_hdr->flags |= cpu_to_le32(VIRTIO_GPU_FLAG_FENCE);
	cmd_hdr->fence_id = cpu_to_le64(fence->f.seqno);
	return 0;
}

void
virtio_gpu_fence_complete(struct virtio_gpu_fence *fence, int result)
{
	struct virtio_gpu_fence_driver *drv = fence->drv;

	spin_lock(&drv->lock);
	if (!list_empty(&fence->node) && !fence->ready) {
		fence->result = result;
		fence->ready = true;
		virtio_gpu_fence_publish(drv);
	}
	spin_unlock(&drv->lock);
	/* Do not nest the wait interlock inside the fence lock. */
	wake_up_all(&drv->vgdev->ctrlq.ack_queue);
}

void
virtio_gpu_fence_stop(struct virtio_gpu_device *vgdev, int error)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;

	spin_lock(&drv->lock);
	if (!drv->stopped) {
		drv->stopped = true;
		drv->stop_error = error;
	}
	spin_unlock(&drv->lock);
}

/* Called only after submit/dequeue joins and transport cookie cancellation. */
void virtio_gpu_fail_fences(struct virtio_gpu_device *vgdev, int error)
{
	struct virtio_gpu_fence_driver *drv = &vgdev->fence_drv;
	struct virtio_gpu_fence *fence, *tmp;

	spin_lock(&drv->lock);
	drv->drained = true;
	list_for_each_entry_safe(fence, tmp, &drv->fences, node) {
		if (!fence->ready) {
			fence->ready = true;
			fence->result = error;
		}
	}
	virtio_gpu_fence_publish(drv);
	spin_unlock(&drv->lock);
	wake_up_all(&vgdev->ctrlq.ack_queue);
}

/* Exact known cookie errors may precede contiguous timeline publication. */
int
virtio_gpu_dependency_status(struct dma_fence *f)
{
	struct virtio_gpu_fence *native;
	int status;

	if (f->ops == &virtio_fence_ops) {
		native = to_virtio_fence(f);
		spin_lock(f->lock);
		status = native->ready ? native->result : 0;
		spin_unlock(f->lock);
		if (status < 0)
			return status;
	}
	return dma_fence_get_status(f);
}

/* One wrap-safe dependency deadline; explicit input never takes the skip path. */
int
virtio_gpu_exec_dependency(struct virtio_gpu_device *vgdev, struct dma_fence *f,
    u64 key, unsigned int started, bool implicit)
{
	struct virtio_gpu_fence *native;
	unsigned int elapsed;
	long waited;
	int status;
	bool skip = false;

	if (!f)
		return 0;
	if (f->ops == &virtio_fence_ops) {
		native = to_virtio_fence(f);
		spin_lock(f->lock);
		status = native->ready ? native->result : 0;
		skip = implicit && native->drv == &vgdev->fence_drv &&
		    native->exec && native->software_key == key;
		spin_unlock(f->lock);
		/* A known cookie error must not hide behind the timeline prefix. */
		if (status < 0)
			return status;
		if (skip)
			return 0;
	}
	elapsed = (unsigned int)jiffies - started;
	waited = dma_fence_wait_timeout(f, true,
	    elapsed < 15 * HZ ? 15 * HZ - elapsed : 0);
	if (waited <= 0)
		return waited < 0 ? waited : -ETIMEDOUT;
	status = dma_fence_get_status(f);
	return status > 0 ? 0 : (status < 0 ? status : -EIO);
}

/* WAIT distinguishes its own unfinished budget from a stored timeout error. */
int
virtio_gpu_wait_dependency(struct dma_fence *f, unsigned int started)
{
	unsigned int elapsed;
	long waited;
	int status = virtio_gpu_dependency_status(f);

	if (status)
		return status > 0 ? 0 : status;
	elapsed = (unsigned int)jiffies - started;
	waited = dma_fence_wait_timeout(f, true,
	    elapsed < 15 * HZ ? 15 * HZ - elapsed : 0);
	if (waited < 0)
		return waited;
	status = virtio_gpu_dependency_status(f);
	return status > 0 ? 0 : (status < 0 ? status : -EBUSY);
}
