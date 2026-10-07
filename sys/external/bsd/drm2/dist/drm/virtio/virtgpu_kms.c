/* Origin: EmberBSD native VirtGPU integration of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_kms.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

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
__KERNEL_RCSID(0, "$NetBSD: virtgpu_kms.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <linux/virtio.h>
#include <linux/virtio_config.h>

#include <drm/drm_file.h>

#include "virtgpu_drv.h"

static void virtio_gpu_config_changed_work_func(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device,
			     config_changed_work);
	u32 events_read, events_clear = 0;

	/* read the config space */
	virtio_cread(vgdev->vdev, struct virtio_gpu_config,
		     events_read, &events_read);
	if (events_read & VIRTIO_GPU_EVENT_DISPLAY) {
		if (vgdev->has_edid)
			virtio_gpu_cmd_get_edids(vgdev);
		virtio_gpu_cmd_get_display_info(vgdev);
		drm_helper_hpd_irq_event(vgdev->ddev);
		events_clear |= VIRTIO_GPU_EVENT_DISPLAY;
	}
	virtio_cwrite(vgdev->vdev, struct virtio_gpu_config,
		      events_clear, &events_clear);
}

/* A separate thread drains completions; it must never wait on itself. */
static void virtio_gpu_reset_work(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev = container_of(work,
	    struct virtio_gpu_device, reset_work);

	/* Join construction without holding the lock over worker drains. */
	mutex_lock(&vgdev->submit_lock);
	mutex_unlock(&vgdev->submit_lock);
	/* An immediate rejection still owns its cookie after that barrier. */
	while (!wait_event_timeout(vgdev->ctrlq.ack_queue,
	    atomic_read(&vgdev->submitters) == 0, 5 * HZ))
		continue;
	flush_work(&vgdev->ctrlq.dequeue_work);
	flush_work(&vgdev->cursorq.dequeue_work);
	vgdev->vdev->config->del_vqs(vgdev->vdev);
	virtio_gpu_fail_fences(vgdev, vgdev->fence_drv.stop_error);
	/* These workers may be waiting for one of the retired fences. */
	virtgpu_console_drain(vgdev);
	flush_work(&vgdev->config_changed_work);
	flush_work(&vgdev->obj_free_work);
}

void virtio_gpu_stop(struct virtio_gpu_device *vgdev, int error)
{
	virtio_gpu_fence_stop(vgdev, error);
	vgdev->vqs_ready = false;
	virtgpu_console_stop(vgdev);
	vgdev->submit_error = error;
	virtio_gpu_fail_capsets(vgdev, error);
	vgdev->vdev->config->reset(vgdev->vdev);
	wake_up_all(&vgdev->ctrlq.ack_queue);
	wake_up_all(&vgdev->cursorq.ack_queue);
	wake_up_all(&vgdev->resp_wq);
	queue_work(vgdev->cleanup_wq, &vgdev->reset_work);
}

static void virtio_gpu_destroy_sync(struct virtio_gpu_device *vgdev)
{
	virtgpu_wait_destroy(&vgdev->ctrlq.ack_queue);
	virtgpu_wait_destroy(&vgdev->cursorq.ack_queue);
	virtgpu_wait_destroy(&vgdev->resp_wq);
	spin_lock_destroy(&vgdev->ctrlq.qlock);
	spin_lock_destroy(&vgdev->cursorq.qlock);
	spin_lock_destroy(&vgdev->display_info_lock);
	spin_lock_destroy(&vgdev->fence_drv.lock);
	spin_lock_destroy(&vgdev->obj_free_lock);
	ida_destroy(&vgdev->resource_ida);
	ida_destroy(&vgdev->ctx_id_ida);
	linux_mutex_destroy(&vgdev->submit_lock);
}

static int virtio_gpu_context_create(struct virtio_gpu_device *vgdev,
				      uint32_t nlen, const char *name)
{
	/* The exclusive upper bound keeps handle + 1 a positive int. */
	int handle = ida_simple_get(&vgdev->ctx_id_ida, 0, INT_MAX, GFP_KERNEL);
	int ret;

	if (handle < 0)
		return handle;
	ret = virtio_gpu_cmd_context_create(vgdev, handle + 1, nlen, name);
	if (ret) {
		/* The command helper resets any uncertain host ownership. */
		ida_free(&vgdev->ctx_id_ida, handle);
		return ret;
	}
	return handle + 1;
}

static void virtio_gpu_context_destroy(struct virtio_gpu_device *vgdev,
				      uint32_t ctx_id)
{
	int ret;

	ret = virtio_gpu_cmd_context_destroy(vgdev, ctx_id);
	if (ret) {
		/* Close cannot report failure; retire the host context by reset. */
		virtio_gpu_stop(vgdev, ret);
	}
	ida_free(&vgdev->ctx_id_ida, ctx_id - 1);
}

static void virtio_gpu_init_vq(struct virtio_gpu_queue *vgvq,
			       void (*work_func)(struct work_struct *work))
{
	spin_lock_init(&vgvq->qlock);
	init_waitqueue_head(&vgvq->ack_queue);
	INIT_WORK(&vgvq->dequeue_work, work_func);
}

static int
virtio_gpu_get_capsets(struct virtio_gpu_device *vgdev, uint32_t num_capsets)
{
	struct virtio_gpu_drv_capset *capsets;
	uint32_t i, j;
	bool classic = false;
	int ret;

	if (num_capsets > VIRTGPU_MAX_CAPSETS)
		return -E2BIG;
	if (num_capsets == 0)
		return -ENODEV;

	capsets = kcalloc(num_capsets, sizeof(*capsets), GFP_KERNEL);
	/* Reset workers are already live; publish their pointer/count atomically. */
	mutex_lock(&vgdev->resp_wq.lock);
	ret = vgdev->capset_error;
	if (!ret && !capsets)
		ret = -ENOMEM;
	if (!ret) {
		vgdev->capsets = capsets;
		vgdev->capsets_allocated = num_capsets;
	}
	mutex_unlock(&vgdev->resp_wq.lock);
	if (ret) {
		kfree(capsets);
		return ret;
	}
	/* Retain this table until deinit drains every completion/reset cookie. */
	for (i = 0; i < num_capsets; i++) {
		ret = virtio_gpu_cmd_get_capset_info(vgdev, i);
		if (ret)
			return ret;
		for (j = 0; j < i; j++)
			if (vgdev->capsets[j].id == vgdev->capsets[i].id)
				return -EINVAL;
		if (vgdev->capsets[i].id == VIRTIO_GPU_CAPSET_VIRGL ||
		    vgdev->capsets[i].id == VIRTIO_GPU_CAPSET_VIRGL2)
			classic = true;
		DRM_INFO("cap set %u: id %u, max-version %u, max-size %u\n",
			 i, vgdev->capsets[i].id,
			 vgdev->capsets[i].max_version,
			 vgdev->capsets[i].max_size);
	}
	if (!classic)
		return -ENODEV;
	/* Unknown IDs remain metadata only; GET_CAPS admits classic IDs 1/2. */
	mutex_lock(&vgdev->resp_wq.lock);
	ret = vgdev->capset_error;
	if (!ret)
		vgdev->num_capsets = num_capsets;
	mutex_unlock(&vgdev->resp_wq.lock);
	return ret;
}

int virtio_gpu_init(struct drm_device *dev, struct virtio_device *vdev,
    struct netbsd_virtqueue **vqs)
{
	struct virtio_gpu_device *vgdev;
	u32 num_scanouts, num_capsets;
	int ret;

	if (!virtio_has_feature(vdev, LINUX_VIRTIO_F_VERSION_1) ||
	    vdev->state != LINUX_VIRTIO_QUEUES || !vqs || !vqs[0] || !vqs[1])
		return -ENODEV;

	vgdev = kzalloc(sizeof(struct virtio_gpu_device), GFP_KERNEL);
	if (!vgdev)
		return -ENOMEM;

	vgdev->ddev = dev;
	dev->dev_private = vgdev;
	vgdev->vdev = vdev;
	vgdev->dev = dev->dev;

	linux_mutex_init(&vgdev->submit_lock);
	atomic_set(&vgdev->submitters, 0);
	spin_lock_init(&vgdev->display_info_lock);
	ida_init(&vgdev->ctx_id_ida);
	ida_init(&vgdev->resource_ida);
	init_waitqueue_head(&vgdev->resp_wq);
	virtio_gpu_init_vq(&vgdev->ctrlq, virtio_gpu_dequeue_ctrl_func);
	virtio_gpu_init_vq(&vgdev->cursorq, virtio_gpu_dequeue_cursor_func);

	vgdev->fence_drv.vgdev = vgdev;
	vgdev->fence_drv.context = dma_fence_context_alloc(1);
	spin_lock_init(&vgdev->fence_drv.lock);
	INIT_LIST_HEAD(&vgdev->fence_drv.fences);
	INIT_LIST_HEAD(&vgdev->cap_cache);
	INIT_WORK(&vgdev->config_changed_work,
		  virtio_gpu_config_changed_work_func);

	INIT_WORK(&vgdev->obj_free_work,
		  virtio_gpu_array_put_free_work);
	INIT_LIST_HEAD(&vgdev->obj_free_list);
	spin_lock_init(&vgdev->obj_free_lock);
	INIT_WORK(&vgdev->reset_work, virtio_gpu_reset_work);
	vgdev->dequeue_wq = alloc_ordered_workqueue("virtgpuack", 0);
	vgdev->cleanup_wq = alloc_ordered_workqueue("virtgpuclr", 0);
	if (!vgdev->dequeue_wq || !vgdev->cleanup_wq) {
		ret = -ENOMEM;
		goto err_vqs;
	}

#ifdef __LITTLE_ENDIAN
	if (virtio_has_feature(vgdev->vdev, VIRTIO_GPU_F_VIRGL))
		vgdev->has_virgl_3d = true;
#endif
	if (virtio_has_feature(vgdev->vdev, VIRTIO_GPU_F_EDID)) {
		vgdev->has_edid = true;
	}

	DRM_INFO("features: %cvirgl %cedid\n",
		 vgdev->has_virgl_3d ? '+' : '-',
		 vgdev->has_edid     ? '+' : '-');

	vgdev->ctrlq.vq = vqs[0];
	vgdev->fence_drv.limit = vqs[0]->size;
	vgdev->cursorq.vq = vqs[1];
	ret = virtio_gpu_alloc_vbufs(vgdev);
	if (ret) {
		DRM_ERROR("failed to alloc vbufs\n");
		goto err_vbufs;
	}

	/* get display info */
	virtio_cread(vgdev->vdev, struct virtio_gpu_config,
		     num_scanouts, &num_scanouts);
	vgdev->num_scanouts = min_t(uint32_t, num_scanouts,
				    VIRTIO_GPU_MAX_SCANOUTS);
	if (!vgdev->num_scanouts) {
		DRM_ERROR("num_scanouts is zero\n");
		ret = -EINVAL;
		goto err_scanouts;
	}
	DRM_INFO("number of scanouts: %d\n", num_scanouts);

	virtio_cread(vgdev->vdev, struct virtio_gpu_config,
		     num_capsets, &num_capsets);
	DRM_INFO("number of cap sets: %d\n", num_capsets);

	ret = virtio_gpu_modeset_init(vgdev);
	if (ret)
		goto err_scanouts;

	virtio_device_ready(vgdev->vdev);
	vgdev->vqs_ready = true;

	if (vgdev->has_virgl_3d) {
		ret = virtio_gpu_get_capsets(vgdev, num_capsets);
		if (ret)
			goto err_ready;
	}
	if (vgdev->has_edid)
		virtio_gpu_cmd_get_edids(vgdev);
	ret = virtio_gpu_cmd_get_display_info(vgdev);
	if (!ret && !wait_event_timeout(vgdev->resp_wq,
	    !vgdev->display_info_pending, 5 * HZ))
		ret = -ETIMEDOUT;
	if (!ret)
		ret = vgdev->submit_error;
	if (ret)
		goto err_ready;
	return 0;

err_ready:
	virtio_gpu_deinit(dev);
	return ret;
err_scanouts:
	virtio_gpu_free_vbufs(vgdev);
err_vbufs:
	vgdev->vdev->config->del_vqs(vgdev->vdev);
err_vqs:
	if (vgdev->cleanup_wq)
		destroy_workqueue(vgdev->cleanup_wq);
	if (vgdev->dequeue_wq)
		destroy_workqueue(vgdev->dequeue_wq);
	virtio_gpu_destroy_sync(vgdev);
	dev->dev_private = NULL;
	kfree(vgdev);
	return ret;
}

static void virtio_gpu_cleanup_cap_cache(struct virtio_gpu_device *vgdev)
{
	struct virtio_gpu_drv_cap_cache *cache_ent, *tmp;

	list_for_each_entry_safe(cache_ent, tmp, &vgdev->cap_cache, head) {
		kfree(cache_ent->caps_cache);
		kfree(cache_ent);
	}
}

void virtio_gpu_deinit(struct drm_device *dev)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;

	virtio_gpu_stop(vgdev, -ENODEV);
	flush_work(&vgdev->reset_work);

	virtio_gpu_modeset_fini(vgdev);
	/* Final modeset references can queue stopped-device UNREF cleanup. */
	flush_work(&vgdev->obj_free_work);
	flush_work(&vgdev->reset_work);
	virtio_gpu_free_vbufs(vgdev);
	virtio_gpu_cleanup_cap_cache(vgdev);
	kfree(vgdev->capsets);
	destroy_workqueue(vgdev->cleanup_wq);
	destroy_workqueue(vgdev->dequeue_wq);
	virtio_gpu_destroy_sync(vgdev);
	dev->dev_private = NULL;
	kfree(vgdev);
}

int virtio_gpu_driver_open(struct drm_device *dev, struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv;
	int id;
	char dbgname[TASK_COMM_LEN];

	/* can't create contexts without 3d renderer */
	if (!vgdev->has_virgl_3d)
		return 0;

	/* allocate a virt GPU context for this opener */
	vfpriv = kzalloc(sizeof(*vfpriv), GFP_KERNEL);
	if (!vfpriv)
		return -ENOMEM;

	strlcpy(dbgname, current->p_comm, sizeof(dbgname));
	id = virtio_gpu_context_create(vgdev, strlen(dbgname), dbgname);
	if (id < 0) {
		kfree(vfpriv);
		return id;
	}

	vfpriv->ctx_id = id;
	linux_mutex_init(&vfpriv->attachment_lock);
	INIT_LIST_HEAD(&vfpriv->attachments);
	file->driver_priv = vfpriv;
	return 0;
}

void virtio_gpu_driver_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv;
	struct virtio_gpu_attachment *entry, *next;

	if (!vgdev->has_virgl_3d)
		return;

	vfpriv = file->driver_priv;
	if (!vfpriv)
		return;

	mutex_lock(&vfpriv->attachment_lock);
	virtio_gpu_context_destroy(vgdev, vfpriv->ctx_id);
	/* Core normally closed every handle before reaching postclose. */
	WARN_ON(!list_empty(&vfpriv->attachments));
	list_for_each_entry_safe(entry, next, &vfpriv->attachments, node) {
		list_del(&entry->node);
		drm_gem_object_put_unlocked(entry->obj);
		kfree(entry);
	}
	mutex_unlock(&vfpriv->attachment_lock);
	linux_mutex_destroy(&vfpriv->attachment_lock);
	kfree(vfpriv);
	file->driver_priv = NULL;
}
