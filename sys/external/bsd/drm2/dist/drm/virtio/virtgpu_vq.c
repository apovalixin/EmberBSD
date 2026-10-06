/* Origin: EmberBSD native VirtGPU integration of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_vq.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *    Gerd Hoffmann <kraxel@redhat.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: virtgpu_vq.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <linux/dma-mapping.h>
#include <linux/virtio.h>
#include <linux/virtio_config.h>
#include <linux/virtio_ring.h>

#include "virtgpu_drv.h"
#include <linux/virtio_sg.h>

#define MAX_INLINE_CMD_SIZE   96
#define MAX_INLINE_RESP_SIZE  24
#define VBUFFER_SIZE          (sizeof(struct virtio_gpu_vbuffer) \
			       + MAX_INLINE_CMD_SIZE		 \
			       + MAX_INLINE_RESP_SIZE)

struct virtio_gpu_wait {
	wait_queue_head_t queue;
	atomic_t refs;
	bool done;
	int error;
};
static void
virtio_gpu_wait_put(struct virtio_gpu_wait *wait)
{
	if (atomic_dec_and_test(&wait->refs)) {
		virtgpu_wait_destroy(&wait->queue);
		kfree(wait);
	}
}
static void
virtio_gpu_wait_done(struct virtio_gpu_vbuffer *vbuf, int error)
{
	struct virtio_gpu_wait *wait = vbuf->wait;

	if (wait) {
		mutex_lock(&wait->queue.lock);
		wait->error = error;
		wait->done = true;
		DRM_WAKEUP_ALL(&wait->queue.cv, &wait->queue.lock);
		mutex_unlock(&wait->queue.lock);
		virtio_gpu_wait_put(wait);
		vbuf->wait = NULL;
	}
}

static void convert_to_hw_box(struct virtio_gpu_box *dst,
			      const struct drm_virtgpu_3d_box *src)
{
	dst->x = cpu_to_le32(src->x);
	dst->y = cpu_to_le32(src->y);
	dst->z = cpu_to_le32(src->z);
	dst->w = cpu_to_le32(src->w);
	dst->h = cpu_to_le32(src->h);
	dst->d = cpu_to_le32(src->d);
}

void virtio_gpu_ctrl_ack(struct netbsd_virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->ctrlq.dequeue_work);
}

void virtio_gpu_cursor_ack(struct netbsd_virtqueue *vq)
{
	struct drm_device *dev = vq->vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->cursorq.dequeue_work);
}

int virtio_gpu_alloc_vbufs(struct virtio_gpu_device *vgdev)
{
	vgdev->vbufs = kmem_cache_create("virtio-gpu-vbufs",
					 VBUFFER_SIZE,
					 __alignof__(struct virtio_gpu_vbuffer),
					 0, NULL);
	if (!vgdev->vbufs)
		return -ENOMEM;
	return 0;
}

void virtio_gpu_free_vbufs(struct virtio_gpu_device *vgdev)
{
	kmem_cache_destroy(vgdev->vbufs);
	vgdev->vbufs = NULL;
}

static struct virtio_gpu_vbuffer*
virtio_gpu_get_vbuf(struct virtio_gpu_device *vgdev,
		    int size, int resp_size, void *resp_buf,
		    virtio_gpu_resp_cb resp_cb)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = kmem_cache_zalloc(vgdev->vbufs, GFP_KERNEL);
	if (!vbuf)
		return ERR_PTR(-ENOMEM);

	BUG_ON(size > MAX_INLINE_CMD_SIZE);
	vbuf->vgdev = vgdev;
	vbuf->buf = (void *)vbuf + sizeof(*vbuf);
	vbuf->size = size;

	vbuf->resp_cb = resp_cb;
	vbuf->resp_size = resp_size;
	if (resp_size <= MAX_INLINE_RESP_SIZE)
		vbuf->resp_buf = (void *)vbuf->buf + size;
	else
		vbuf->resp_buf = resp_buf;
	BUG_ON(!vbuf->resp_buf);
	return vbuf;
}

static void *virtio_gpu_alloc_cmd(struct virtio_gpu_device *vgdev,
				  struct virtio_gpu_vbuffer **vbuffer_p,
				  int size)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = virtio_gpu_get_vbuf(vgdev, size,
				   sizeof(struct virtio_gpu_ctrl_hdr),
				   NULL, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return vbuf->buf;
}

static struct virtio_gpu_update_cursor*
virtio_gpu_alloc_cursor(struct virtio_gpu_device *vgdev,
			struct virtio_gpu_vbuffer **vbuffer_p)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = virtio_gpu_get_vbuf
		(vgdev, sizeof(struct virtio_gpu_update_cursor),
		 0, NULL, NULL);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtio_gpu_update_cursor *)vbuf->buf;
}

static void *virtio_gpu_alloc_cmd_resp(struct virtio_gpu_device *vgdev,
				       virtio_gpu_resp_cb cb,
				       struct virtio_gpu_vbuffer **vbuffer_p,
				       int cmd_size, int resp_size,
				       void *resp_buf)
{
	struct virtio_gpu_vbuffer *vbuf;

	vbuf = virtio_gpu_get_vbuf(vgdev, cmd_size,
				   resp_size, resp_buf, cb);
	if (IS_ERR(vbuf)) {
		*vbuffer_p = NULL;
		return ERR_CAST(vbuf);
	}
	*vbuffer_p = vbuf;
	return (struct virtio_gpu_command *)vbuf->buf;
}

static void free_vbuf(struct virtio_gpu_device *vgdev,
		      struct virtio_gpu_vbuffer *vbuf)
{
	if (vbuf->release)
		virtio_gpu_release_object(vbuf->release);
	if (vbuf->resp_size > MAX_INLINE_RESP_SIZE)
		kfree(vbuf->resp_buf);
	kvfree(vbuf->data_buf);
	kmem_cache_free(vgdev->vbufs, vbuf);
}

void
virtio_gpu_cancel_vbuf(void *cookie)
{
	struct virtio_gpu_vbuffer *vbuf = cookie;
	struct virtio_gpu_device *vgdev = vbuf->vgdev;

	virtio_gpu_wait_done(vbuf, -ENODEV);
	if (vbuf->fence)
		dma_fence_put(&vbuf->fence->f);
	if (vbuf->objs)
		virtio_gpu_array_put_free_delayed(vgdev, vbuf->objs);
	free_vbuf(vgdev, vbuf);
}

static void reclaim_vbufs(struct netbsd_virtqueue *vq, struct list_head *reclaim_list)
{
	struct virtio_gpu_vbuffer *vbuf;
	unsigned int len;
	int freed = 0;

	while ((vbuf = virtqueue_get_buf(vq, &len))) {
		vbuf->resp_received = len;
		list_add_tail(&vbuf->list, reclaim_list);
		freed++;
	}
	if (freed == 0)
		DRM_DEBUG("Huh? zero vbufs reclaimed");
}

void virtio_gpu_dequeue_ctrl_func(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device,
			     ctrlq.dequeue_work);
	struct list_head reclaim_list;
	struct virtio_gpu_vbuffer *entry, *tmp;
	struct virtio_gpu_ctrl_hdr *resp;
	u64 fence_id = 0;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->ctrlq.qlock);
	do {
		virtqueue_disable_cb(vgdev->ctrlq.vq);
		reclaim_vbufs(vgdev->ctrlq.vq, &reclaim_list);

	} while (!virtqueue_enable_cb(vgdev->ctrlq.vq));
	spin_unlock(&vgdev->ctrlq.qlock);

	list_for_each_entry(entry, &reclaim_list, list) {
		resp = (struct virtio_gpu_ctrl_hdr *)entry->resp_buf;

		struct virtio_gpu_ctrl_hdr *cmd = (void *)entry->buf;
		u32 expected = VIRTIO_GPU_RESP_OK_NODATA;
		int error = 0;

		switch (le32_to_cpu(cmd->type)) {
		case VIRTIO_GPU_CMD_GET_DISPLAY_INFO:
			expected = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
			break;
		case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
			expected = VIRTIO_GPU_RESP_OK_CAPSET_INFO;
			break;
		case VIRTIO_GPU_CMD_GET_CAPSET:
			expected = VIRTIO_GPU_RESP_OK_CAPSET;
			break;
		case VIRTIO_GPU_CMD_GET_EDID:
			expected = VIRTIO_GPU_RESP_OK_EDID;
			break;
		}
		if (entry->resp_received < entry->resp_size ||
		    entry->resp_received > entry->resp_size ||
		    le32_to_cpu(resp->type) != expected)
			error = -EIO;
		if (entry->fence &&
		    (!(le32_to_cpu(resp->flags) & VIRTIO_GPU_FLAG_FENCE) ||
		    resp->fence_id != cmd->fence_id))
			error = -EIO;
		if (error) {
			vgdev->submit_error = error;
			DRM_ERROR("invalid GPU response (%u bytes, type 0x%x)\n",
			    entry->resp_received, le32_to_cpu(resp->type));
			if (entry->fence)
				virtio_gpu_fence_fail(entry->fence, error);
			if (entry->release) {
				vgdev->vqs_ready = false;
				vgdev->vdev->config->reset(vgdev->vdev);
				virtio_gpu_fail_fences(vgdev, error);
				wake_up_all(&vgdev->cursorq.ack_queue);
			}
		} else {
			/* Complete DMA visibility before exposing the GPU fence. */
			if (entry->objs &&
			    (le32_to_cpu(cmd->type) == VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D ||
			    le32_to_cpu(cmd->type) == VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D ||
			    le32_to_cpu(cmd->type) == VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D)) {
				unsigned int i;
				for (i = 0; i < entry->objs->nents; i++) {
					struct virtio_gpu_object *bo =
					    gem_to_virtio_gpu_obj(entry->objs->objs[i]);
					bus_dmamap_sync(vgdev->vdev->dmat,
					    bo->pages->sgl->sg_dmamap, 0,
					    bo->base.base.size,
					    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
				}
			}
			if (le32_to_cpu(resp->flags) & VIRTIO_GPU_FLAG_FENCE)
				fence_id = max(fence_id, le64_to_cpu(resp->fence_id));
			if (entry->resp_cb)
				entry->resp_cb(vgdev, entry);
		}
		virtio_gpu_wait_done(entry, error);

	}
	wake_up(&vgdev->ctrlq.ack_queue);

	if (fence_id)
		virtio_gpu_fence_event_process(vgdev, fence_id);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, list) {
		if (entry->objs)
			virtio_gpu_array_put_free_delayed(vgdev, entry->objs);
		list_del(&entry->list);
		if (entry->fence)
			dma_fence_put(&entry->fence->f);
		free_vbuf(vgdev, entry);
	}
}

void virtio_gpu_dequeue_cursor_func(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device,
			     cursorq.dequeue_work);
	struct list_head reclaim_list;
	struct virtio_gpu_vbuffer *entry, *tmp;

	INIT_LIST_HEAD(&reclaim_list);
	spin_lock(&vgdev->cursorq.qlock);
	do {
		virtqueue_disable_cb(vgdev->cursorq.vq);
		reclaim_vbufs(vgdev->cursorq.vq, &reclaim_list);
	} while (!virtqueue_enable_cb(vgdev->cursorq.vq));
	spin_unlock(&vgdev->cursorq.qlock);

	list_for_each_entry_safe(entry, tmp, &reclaim_list, list) {
		list_del(&entry->list);
		free_vbuf(vgdev, entry);
	}
	wake_up(&vgdev->cursorq.ack_queue);
}

/* Serialize submitters across descriptor-pressure waits and fence emission. */
static int
virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_vbuffer *vbuf, struct virtio_gpu_ctrl_hdr *hdr,
    struct virtio_gpu_fence *fence)
{
	struct netbsd_virtqueue *vq = vgdev->ctrlq.vq;
	struct linux_virtio_sg cmd = { vbuf->buf, vbuf->size, NULL };
	struct linux_virtio_sg data = { vbuf->data_buf, vbuf->data_size, NULL };
	struct linux_virtio_sg resp = { vbuf->resp_buf, vbuf->resp_size, NULL };
	struct linux_virtio_sg *sgs[3];
	unsigned int out = 1, before;
	int ret;

	sgs[0] = &cmd;
	if (vbuf->data_size)
		sgs[out++] = &data;
	sgs[out] = &resp;
	mutex_lock(&vgdev->submit_lock);
	if (hdr && fence) {
		virtio_gpu_fence_emit(vgdev, hdr, fence);
		vbuf->fence = fence;
		dma_fence_get(&fence->f);
		if (vbuf->objs) {
			virtio_gpu_array_add_fence(vbuf->objs, &fence->f);
			virtio_gpu_array_unlock_resv(vbuf->objs);
		}
	}
	for (;;) {
		spin_lock(&vgdev->ctrlq.qlock);
		before = vq->num_free;
		ret = vgdev->vqs_ready ? virtqueue_add_sgs(vq, sgs, out,
		    vbuf->resp_size ? 1 : 0, vbuf, GFP_ATOMIC) : -ENODEV;
		spin_unlock(&vgdev->ctrlq.qlock);
		if (ret != -ENOSPC)
			break;
		/* The transport first bounds actual DMA descriptors. */
		if (!wait_event_timeout(vgdev->ctrlq.ack_queue,
		    !vgdev->vqs_ready || vq->num_free != before, 5 * HZ)) {
			ret = -ETIMEDOUT;
			break;
		}
	}
	mutex_unlock(&vgdev->submit_lock);
	if (ret) {
		vgdev->submit_error = ret;
		DRM_ERROR("control submission failed: %d\n", ret);
		if (fence)
			virtio_gpu_fence_fail(fence, ret);
		if (vbuf->release)
			vgdev->vdev->config->reset(vgdev->vdev);
		virtio_gpu_cancel_vbuf(vbuf);
	}
	return ret;
}

static int
virtio_gpu_queue_sync(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_vbuffer *vbuf, struct virtio_gpu_ctrl_hdr *hdr,
    struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_wait *wait;
	int ret;

	wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait) {
		if (fence && vbuf->objs)
			virtio_gpu_array_unlock_resv(vbuf->objs);
		virtio_gpu_cancel_vbuf(vbuf);
		return -ENOMEM;
	}
	init_waitqueue_head(&wait->queue);
	atomic_set(&wait->refs, 2);
	vbuf->wait = wait;
	ret = virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, hdr, fence);
	if (!ret && !wait_event_timeout(wait->queue, wait->done, 5 * HZ)) {
		vgdev->vqs_ready = false;
		vgdev->vdev->config->reset(vgdev->vdev);
		virtio_gpu_fail_fences(vgdev, -ETIMEDOUT);
		wake_up_all(&vgdev->ctrlq.ack_queue);
		wake_up_all(&vgdev->cursorq.ack_queue);
		ret = -ETIMEDOUT;
	}
	if (!ret)
		ret = wait->error;
	virtio_gpu_wait_put(wait);
	return ret;
}

void virtio_gpu_disable_notify(struct virtio_gpu_device *vgdev)
{
	vgdev->disable_notify = true;
}

void virtio_gpu_enable_notify(struct virtio_gpu_device *vgdev)
{
	vgdev->disable_notify = false;

	if (!vgdev->pending_notify)
		return;
	vgdev->pending_notify = false;
	virtqueue_notify(vgdev->ctrlq.vq);
}

static int virtio_gpu_queue_ctrl_buffer(struct virtio_gpu_device *vgdev,
					 struct virtio_gpu_vbuffer *vbuf)
{
	return virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, NULL, NULL);
}

static void virtio_gpu_queue_cursor(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_vbuffer *vbuf)
{
	struct netbsd_virtqueue *vq = vgdev->cursorq.vq;
	struct linux_virtio_sg cmd = { vbuf->buf, vbuf->size, NULL };
	struct linux_virtio_sg *sgs[] = { &cmd };
	unsigned int before;
	int ret;

	mutex_lock(&vgdev->submit_lock);
	for (;;) {
		spin_lock(&vgdev->cursorq.qlock);
		before = vq->num_free;
		ret = vgdev->vqs_ready ?
		    virtqueue_add_sgs(vq, sgs, 1, 0, vbuf, GFP_ATOMIC) : -ENODEV;
		spin_unlock(&vgdev->cursorq.qlock);
		if (ret != -ENOSPC)
			break;
		if (!wait_event_timeout(vgdev->cursorq.ack_queue,
		    !vgdev->vqs_ready || vq->num_free != before, 5 * HZ)) {
			ret = -ETIMEDOUT;
			break;
		}
	}
	mutex_unlock(&vgdev->submit_lock);
	if (ret) {
		vgdev->submit_error = ret;
		DRM_ERROR("cursor submission failed: %d\n", ret);
		virtio_gpu_cancel_vbuf(vbuf);
	}
}

/* just create gem objects for userspace and long lived objects,
 * just use dma_alloced pages for the queue objects?
 */

/* create a basic resource */
int virtio_gpu_cmd_create_resource(struct virtio_gpu_device *vgdev,
				    struct virtio_gpu_object *bo,
				    struct virtio_gpu_object_params *params,
				    struct virtio_gpu_object_array *objs,
				    struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_create_2d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_2D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->format = cpu_to_le32(params->format);
	cmd_p->width = cpu_to_le32(params->width);
	cmd_p->height = cpu_to_le32(params->height);

	int ret = virtio_gpu_queue_sync(vgdev, vbuf, &cmd_p->hdr, fence);
	bo->created = ret == 0;
	return ret;
}

void virtio_gpu_cmd_unref_resource(struct virtio_gpu_device *vgdev,
				   uint32_t resource_id)
{
	struct virtio_gpu_resource_unref *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_UNREF);
	cmd_p->resource_id = cpu_to_le32(resource_id);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_queue_unref(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object *bo)
{
	struct virtio_gpu_resource_unref *cmd;
	struct virtio_gpu_vbuffer *vbuf;

	cmd = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd));
	if (IS_ERR(cmd)) {
		vgdev->vdev->config->reset(vgdev->vdev);
		virtio_gpu_release_object(bo);
		return;
	}
	cmd->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_UNREF);
	cmd->resource_id = cpu_to_le32(bo->hw_res_handle);
	vbuf->release = bo;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

static void virtio_gpu_cmd_resource_inval_backing(struct virtio_gpu_device *vgdev,
						  uint32_t resource_id,
						  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_detach_backing *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING);
	cmd_p->resource_id = cpu_to_le32(resource_id);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
}

void virtio_gpu_cmd_set_scanout(struct virtio_gpu_device *vgdev,
				uint32_t scanout_id, uint32_t resource_id,
				uint32_t width, uint32_t height,
				uint32_t x, uint32_t y)
{
	struct virtio_gpu_set_scanout *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SET_SCANOUT);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->scanout_id = cpu_to_le32(scanout_id);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_resource_flush(struct virtio_gpu_device *vgdev,
				   uint32_t resource_id,
				   uint32_t x, uint32_t y,
				   uint32_t width, uint32_t height)
{
	struct virtio_gpu_resource_flush *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_FLUSH);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *vgdev,
					uint64_t offset,
					uint32_t width, uint32_t height,
					uint32_t x, uint32_t y,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_to_host_2d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	bus_dmamap_sync(vgdev->vdev->dmat, bo->pages->sgl->sg_dmamap,
	    0, bo->base.base.size, BUS_DMASYNC_PREWRITE);

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->r.width = cpu_to_le32(width);
	cmd_p->r.height = cpu_to_le32(height);
	cmd_p->r.x = cpu_to_le32(x);
	cmd_p->r.y = cpu_to_le32(y);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
}

static int
virtio_gpu_cmd_resource_attach_backing(struct virtio_gpu_device *vgdev,
				       uint32_t resource_id,
				       struct virtio_gpu_mem_entry *ents,
				       uint32_t nents,
				       struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_attach_backing *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		kfree(ents);
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING);
	cmd_p->resource_id = cpu_to_le32(resource_id);
	cmd_p->nr_entries = cpu_to_le32(nents);

	vbuf->data_buf = ents;
	vbuf->data_size = sizeof(*ents) * nents;

	return virtio_gpu_queue_sync(vgdev, vbuf, NULL, NULL);
}

static void virtio_gpu_cmd_get_display_info_cb(struct virtio_gpu_device *vgdev,
					       struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_resp_display_info *resp =
		(struct virtio_gpu_resp_display_info *)vbuf->resp_buf;
	int i;

	spin_lock(&vgdev->display_info_lock);
	for (i = 0; i < vgdev->num_scanouts; i++) {
		vgdev->outputs[i].info = resp->pmodes[i];
		if (resp->pmodes[i].enabled) {
			DRM_DEBUG("output %d: %dx%d+%d+%d", i,
				  le32_to_cpu(resp->pmodes[i].r.width),
				  le32_to_cpu(resp->pmodes[i].r.height),
				  le32_to_cpu(resp->pmodes[i].r.x),
				  le32_to_cpu(resp->pmodes[i].r.y));
		} else {
			DRM_DEBUG("output %d: disabled", i);
		}
	}

	vgdev->display_info_pending = false;
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);

	if (!drm_helper_hpd_irq_event(vgdev->ddev))
		drm_kms_helper_hotplug_event(vgdev->ddev);
}

static void virtio_gpu_cmd_get_capset_info_cb(struct virtio_gpu_device *vgdev,
					      struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_get_capset_info *cmd =
		(struct virtio_gpu_get_capset_info *)vbuf->buf;
	struct virtio_gpu_resp_capset_info *resp =
		(struct virtio_gpu_resp_capset_info *)vbuf->resp_buf;
	int i = le32_to_cpu(cmd->capset_index);

	spin_lock(&vgdev->display_info_lock);
	vgdev->capsets[i].id = le32_to_cpu(resp->capset_id);
	vgdev->capsets[i].max_version = le32_to_cpu(resp->capset_max_version);
	vgdev->capsets[i].max_size = le32_to_cpu(resp->capset_max_size);
	spin_unlock(&vgdev->display_info_lock);
	wake_up(&vgdev->resp_wq);
}

static void virtio_gpu_cmd_capset_cb(struct virtio_gpu_device *vgdev,
				     struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_get_capset *cmd =
		(struct virtio_gpu_get_capset *)vbuf->buf;
	struct virtio_gpu_resp_capset *resp =
		(struct virtio_gpu_resp_capset *)vbuf->resp_buf;
	struct virtio_gpu_drv_cap_cache *cache_ent;

	spin_lock(&vgdev->display_info_lock);
	list_for_each_entry(cache_ent, &vgdev->cap_cache, head) {
		if (cache_ent->version == le32_to_cpu(cmd->capset_version) &&
		    cache_ent->id == le32_to_cpu(cmd->capset_id)) {
			memcpy(cache_ent->caps_cache, resp->capset_data,
			       cache_ent->size);
			/* Copy must occur before is_valid is signalled. */
			smp_wmb();
			atomic_set(&cache_ent->is_valid, 1);
			break;
		}
	}
	spin_unlock(&vgdev->display_info_lock);
	wake_up_all(&vgdev->resp_wq);
}

static int virtio_get_edid_block(void *data, u8 *buf,
				 unsigned int block, size_t len)
{
	struct virtio_gpu_resp_edid *resp = data;
	size_t start = block * EDID_LENGTH;

	if (start + len > le32_to_cpu(resp->size))
		return -1;
	memcpy(buf, resp->edid + start, len);
	return 0;
}

static void virtio_gpu_cmd_get_edid_cb(struct virtio_gpu_device *vgdev,
				       struct virtio_gpu_vbuffer *vbuf)
{
	struct virtio_gpu_cmd_get_edid *cmd =
		(struct virtio_gpu_cmd_get_edid *)vbuf->buf;
	struct virtio_gpu_resp_edid *resp =
		(struct virtio_gpu_resp_edid *)vbuf->resp_buf;
	uint32_t scanout = le32_to_cpu(cmd->scanout);
	struct virtio_gpu_output *output;
	struct edid *new_edid, *old_edid;

	if (scanout >= vgdev->num_scanouts)
		return;
	output = vgdev->outputs + scanout;

	new_edid = drm_do_get_edid(&output->conn, virtio_get_edid_block, resp);
	drm_connector_update_edid_property(&output->conn, new_edid);

	spin_lock(&vgdev->display_info_lock);
	old_edid = output->edid;
	output->edid = new_edid;
	spin_unlock(&vgdev->display_info_lock);

	kfree(old_edid);
	wake_up(&vgdev->resp_wq);
}

int virtio_gpu_cmd_get_display_info(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_ctrl_hdr *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_display_info),
			   GFP_KERNEL);
	if (!resp_buf)
		return -ENOMEM;

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_get_display_info_cb, &vbuf,
		 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_display_info),
		 resp_buf);
	if (IS_ERR(cmd_p)) {
		kfree(resp_buf);
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	vgdev->display_info_pending = true;
	cmd_p->type = cpu_to_le32(VIRTIO_GPU_CMD_GET_DISPLAY_INFO);
	int ret = virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	if (ret)
		vgdev->display_info_pending = false;
	return ret;
}

int virtio_gpu_cmd_get_capset_info(struct virtio_gpu_device *vgdev, int idx)
{
	struct virtio_gpu_get_capset_info *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_capset_info),
			   GFP_KERNEL);
	if (!resp_buf)
		return -ENOMEM;

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_get_capset_info_cb, &vbuf,
		 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_capset_info),
		 resp_buf);
	if (IS_ERR(cmd_p)) {
		kfree(resp_buf);
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_CAPSET_INFO);
	cmd_p->capset_index = cpu_to_le32(idx);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	return 0;
}

int virtio_gpu_cmd_get_capset(struct virtio_gpu_device *vgdev,
			      int idx, int cap_version,
			      struct virtio_gpu_drv_cap_cache **cache_p)
{
	struct virtio_gpu_get_capset *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	int max_size;
	struct virtio_gpu_drv_cap_cache *cache_ent;
	struct virtio_gpu_drv_cap_cache *search_ent;
	void *resp_buf;

	*cache_p = NULL;

	if (idx >= vgdev->num_capsets)
		return -EINVAL;

	if (cap_version > vgdev->capsets[idx].max_version)
		return -EINVAL;

	cache_ent = kzalloc(sizeof(*cache_ent), GFP_KERNEL);
	if (!cache_ent)
		return -ENOMEM;

	max_size = vgdev->capsets[idx].max_size;
	cache_ent->caps_cache = kmalloc(max_size, GFP_KERNEL);
	if (!cache_ent->caps_cache) {
		kfree(cache_ent);
		return -ENOMEM;
	}

	resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_capset) + max_size,
			   GFP_KERNEL);
	if (!resp_buf) {
		kfree(cache_ent->caps_cache);
		kfree(cache_ent);
		return -ENOMEM;
	}

	cache_ent->version = cap_version;
	cache_ent->id = vgdev->capsets[idx].id;
	atomic_set(&cache_ent->is_valid, 0);
	cache_ent->size = max_size;
	spin_lock(&vgdev->display_info_lock);
	/* Search while under lock in case it was added by another task. */
	list_for_each_entry(search_ent, &vgdev->cap_cache, head) {
		if (search_ent->id == vgdev->capsets[idx].id &&
		    search_ent->version == cap_version) {
			*cache_p = search_ent;
			break;
		}
	}
	if (!*cache_p)
		list_add_tail(&cache_ent->head, &vgdev->cap_cache);
	spin_unlock(&vgdev->display_info_lock);

	if (*cache_p) {
		/* Entry was found, so free everything that was just created. */
		kfree(resp_buf);
		kfree(cache_ent->caps_cache);
		kfree(cache_ent);
		return 0;
	}

	cmd_p = virtio_gpu_alloc_cmd_resp
		(vgdev, &virtio_gpu_cmd_capset_cb, &vbuf, sizeof(*cmd_p),
		 sizeof(struct virtio_gpu_resp_capset) + max_size,
		 resp_buf);
	if (IS_ERR(cmd_p)) {
		kfree(resp_buf);
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_CAPSET);
	cmd_p->capset_id = cpu_to_le32(vgdev->capsets[idx].id);
	cmd_p->capset_version = cpu_to_le32(cap_version);
	*cache_p = cache_ent;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);

	return 0;
}

int virtio_gpu_cmd_get_edids(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_cmd_get_edid *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;
	void *resp_buf;
	int scanout;

	if (WARN_ON(!vgdev->has_edid))
		return -EINVAL;

	for (scanout = 0; scanout < vgdev->num_scanouts; scanout++) {
		resp_buf = kzalloc(sizeof(struct virtio_gpu_resp_edid),
				   GFP_KERNEL);
		if (!resp_buf)
			return -ENOMEM;

		cmd_p = virtio_gpu_alloc_cmd_resp
			(vgdev, &virtio_gpu_cmd_get_edid_cb, &vbuf,
			 sizeof(*cmd_p), sizeof(struct virtio_gpu_resp_edid),
			 resp_buf);
	if (IS_ERR(cmd_p)) {
		kfree(resp_buf);
		vgdev->submit_error = PTR_ERR(cmd_p);
		return PTR_ERR(cmd_p);
	}
		cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_GET_EDID);
		cmd_p->scanout = cpu_to_le32(scanout);
		virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
	}

	return 0;
}

void virtio_gpu_cmd_context_create(struct virtio_gpu_device *vgdev, uint32_t id,
				   uint32_t nlen, const char *name)
{
	struct virtio_gpu_ctx_create *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_CREATE);
	cmd_p->hdr.ctx_id = cpu_to_le32(id);
	cmd_p->nlen = cpu_to_le32(nlen);
	strncpy(cmd_p->debug_name, name, sizeof(cmd_p->debug_name) - 1);
	cmd_p->debug_name[sizeof(cmd_p->debug_name) - 1] = 0;
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_context_destroy(struct virtio_gpu_device *vgdev,
				    uint32_t id)
{
	struct virtio_gpu_ctx_destroy *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_DESTROY);
	cmd_p->hdr.ctx_id = cpu_to_le32(id);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void virtio_gpu_cmd_context_attach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_ctx_resource *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);

}

void virtio_gpu_cmd_context_detach_resource(struct virtio_gpu_device *vgdev,
					    uint32_t ctx_id,
					    struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_ctx_resource *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	virtio_gpu_queue_ctrl_buffer(vgdev, vbuf);
}

void
virtio_gpu_cmd_resource_create_3d(struct virtio_gpu_device *vgdev,
				  struct virtio_gpu_object *bo,
				  struct virtio_gpu_object_params *params,
				  struct virtio_gpu_object_array *objs,
				  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_resource_create_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	cmd_p->format = cpu_to_le32(params->format);
	cmd_p->width = cpu_to_le32(params->width);
	cmd_p->height = cpu_to_le32(params->height);

	cmd_p->target = cpu_to_le32(params->target);
	cmd_p->bind = cpu_to_le32(params->bind);
	cmd_p->depth = cpu_to_le32(params->depth);
	cmd_p->array_size = cpu_to_le32(params->array_size);
	cmd_p->last_level = cpu_to_le32(params->last_level);
	cmd_p->nr_samples = cpu_to_le32(params->nr_samples);
	cmd_p->flags = cpu_to_le32(params->flags);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
	bo->created = true;
}

void virtio_gpu_cmd_transfer_to_host_3d(struct virtio_gpu_device *vgdev,
					uint32_t ctx_id,
					uint64_t offset, uint32_t level,
					struct drm_virtgpu_3d_box *box,
					struct virtio_gpu_object_array *objs,
					struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_host_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	bus_dmamap_sync(vgdev->vdev->dmat, bo->pages->sgl->sg_dmamap,
	    0, bo->base.base.size, BUS_DMASYNC_PREWRITE);

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	convert_to_hw_box(&cmd_p->box, box);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->level = cpu_to_le32(level);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
}

void virtio_gpu_cmd_transfer_from_host_3d(struct virtio_gpu_device *vgdev,
					  uint32_t ctx_id,
					  uint64_t offset, uint32_t level,
					  struct drm_virtgpu_3d_box *box,
					  struct virtio_gpu_object_array *objs,
					  struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(objs->objs[0]);
	struct virtio_gpu_transfer_host_3d *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->resource_id = cpu_to_le32(bo->hw_res_handle);
	convert_to_hw_box(&cmd_p->box, box);
	cmd_p->offset = cpu_to_le64(offset);
	cmd_p->level = cpu_to_le32(level);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
}

void virtio_gpu_cmd_submit(struct virtio_gpu_device *vgdev,
			   void *data, uint32_t data_size,
			   uint32_t ctx_id,
			   struct virtio_gpu_object_array *objs,
			   struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_cmd_submit *cmd_p;
	struct virtio_gpu_vbuffer *vbuf;

	cmd_p = virtio_gpu_alloc_cmd(vgdev, &vbuf, sizeof(*cmd_p));
	if (IS_ERR(cmd_p)) {
		if (objs) {
			if (fence)
				virtio_gpu_array_unlock_resv(objs);
			virtio_gpu_array_put_free(objs);
		}
		vgdev->submit_error = PTR_ERR(cmd_p);
		return;
	}
	memset(cmd_p, 0, sizeof(*cmd_p));

	vbuf->data_buf = data;
	vbuf->data_size = data_size;
	vbuf->objs = objs;

	cmd_p->hdr.type = cpu_to_le32(VIRTIO_GPU_CMD_SUBMIT_3D);
	cmd_p->hdr.ctx_id = cpu_to_le32(ctx_id);
	cmd_p->size = cpu_to_le32(data_size);

	virtio_gpu_queue_fenced_ctrl_buffer(vgdev, vbuf, &cmd_p->hdr, fence);
}

int virtio_gpu_object_attach(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object *obj, struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_mem_entry *ents;
	struct sg_table *sgt;
	bus_dmamap_t map;
	unsigned int i, nents;
	int ret;

	ret = drm_gem_shmem_pin(&obj->base.base);
	if (ret)
		return ret;
	sgt = drm_gem_shmem_get_sg_table(&obj->base.base);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto unpin;
	}
	/* Native DMA map retains all pinned pages until RESOURCE_UNREF. */
	ret = bus_dmamap_create(vgdev->vdev->dmat, obj->base.base.size,
	    sgt->nents, UINT32_MAX, 0, BUS_DMA_WAITOK, &map);
	if (ret) {
		ret = -ret;
		goto free_sg;
	}
	obj->dma_vaddr = drm_gem_shmem_vmap(&obj->base.base);
	if (IS_ERR(obj->dma_vaddr)) {
		ret = PTR_ERR(obj->dma_vaddr);
		obj->dma_vaddr = NULL;
	} else {
		ret = -bus_dmamap_load(vgdev->vdev->dmat, map, obj->dma_vaddr,
		    obj->base.base.size, NULL,
		    BUS_DMA_WAITOK | BUS_DMA_WRITE | BUS_DMA_READ);
	}
	if (ret) {
		if (obj->dma_vaddr) {
			drm_gem_shmem_vunmap(&obj->base.base, obj->dma_vaddr);
			obj->dma_vaddr = NULL;
		}
		bus_dmamap_destroy(vgdev->vdev->dmat, map);
		goto free_sg;
	}
	sgt->sgl->sg_dmat = vgdev->vdev->dmat;
	sgt->sgl->sg_dmamap = map;
	obj->pages = sgt;
	obj->mapped = map->dm_nsegs;
	nents = map->dm_nsegs;
	if (nents > (vgdev->vdev->max_request - MAX_INLINE_CMD_SIZE -
	    MAX_INLINE_RESP_SIZE) / sizeof(*ents))
		return -EMSGSIZE;
	ents = kcalloc(nents, sizeof(*ents), GFP_KERNEL);
	if (!ents)
		return -ENOMEM;
	for (i = 0; i < nents; i++) {
		if (map->dm_segs[i].ds_len > UINT32_MAX) {
			kfree(ents);
			return -EMSGSIZE;
		}
		ents[i].addr = cpu_to_le64(map->dm_segs[i].ds_addr);
		ents[i].length = cpu_to_le32(map->dm_segs[i].ds_len);
	}
	bus_dmamap_sync(vgdev->vdev->dmat, map, 0, obj->base.base.size,
	    BUS_DMASYNC_PREWRITE | BUS_DMASYNC_PREREAD);
	return virtio_gpu_cmd_resource_attach_backing(vgdev,
	    obj->hw_res_handle, ents, nents, NULL);
free_sg:
	sg_free_table(sgt);
	kfree(sgt);
unpin:
	drm_gem_shmem_unpin(&obj->base.base);
	return ret;
}

void virtio_gpu_object_detach(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_object *obj)
{
	/* Only after host unref acknowledgement, or a device reset. */
	if (obj->pages) {
		bus_dmamap_sync(vgdev->vdev->dmat, obj->pages->sgl->sg_dmamap,
		    0, obj->base.base.size,
		    BUS_DMASYNC_POSTWRITE | BUS_DMASYNC_POSTREAD);
		bus_dmamap_unload(vgdev->vdev->dmat, obj->pages->sgl->sg_dmamap);
		drm_gem_shmem_vunmap(&obj->base.base, obj->dma_vaddr);
		obj->dma_vaddr = NULL;
		sg_free_table(obj->pages);
		kfree(obj->pages);
		obj->pages = NULL;
		obj->mapped = 0;
		drm_gem_shmem_unpin(&obj->base.base);
	}
}

void virtio_gpu_cursor_ping(struct virtio_gpu_device *vgdev,
			    struct virtio_gpu_output *output)
{
	struct virtio_gpu_vbuffer *vbuf;
	struct virtio_gpu_update_cursor *cur_p;

	output->cursor.pos.scanout_id = cpu_to_le32(output->index);
	cur_p = virtio_gpu_alloc_cursor(vgdev, &vbuf);
	if (IS_ERR(cur_p)) {
		vgdev->submit_error = PTR_ERR(cur_p);
		return;
	}
	memcpy(cur_p, &output->cursor, sizeof(output->cursor));
	virtio_gpu_queue_cursor(vgdev, vbuf);
}
