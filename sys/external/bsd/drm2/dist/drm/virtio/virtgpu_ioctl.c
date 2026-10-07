/* Origin: EmberBSD native VirtGPU ioctl adaptation of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_ioctl.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

/*
 * Copyright (C) 2015 Red Hat, Inc.
 * All Rights Reserved.
 *
 * Authors:
 *    Dave Airlie
 *    Alon Levy
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: virtgpu_ioctl.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <linux/file.h>
#include <linux/uaccess.h>
#include <linux/sync_file.h>

#include <drm/drm_file.h>
#include <drm/virtgpu_drm.h>

#include "virtgpu_drv.h"

static int virtio_gpu_map_ioctl(struct drm_device *dev, void *data,
				struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_map *virtio_gpu_map = data;

	return virtio_gpu_mode_dumb_mmap(file_priv, vgdev->ddev,
					 virtio_gpu_map->handle,
					 &virtio_gpu_map->offset);
}

/*
 * Usage of execbuffer:
 * Relocations need to take into account the full VIRTIO_GPUDrawable size.
 * However, the command as passed from user space must *not* contain the initial
 * VIRTIO_GPUReleaseInfo struct (first XXX bytes)
 */
static int virtio_gpu_execbuffer_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *drm_file)
{
	struct drm_virtgpu_execbuffer *exbuf = data;
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = drm_file->driver_priv;
	struct virtio_gpu_fence *out_fence = NULL;
	struct virtio_gpu_object_array *buflist = NULL, *hints = NULL;
	struct sync_file *sync_file = NULL;
	struct dma_fence **scratch = NULL, *in_fence;
	struct file *fp = NULL;
	uint32_t *bo_handles = NULL, count;
	size_t hint_bytes = 0, scratch_bytes = 0;
	unsigned int started = (unsigned int)jiffies;
	int in_fence_fd = exbuf->fence_fd, out_fence_fd = -1, ret;
	bool attached = false, locked = false;
	void *buf = NULL;

	if (!vgdev->has_virgl_3d)
		return -ENOSYS;
	exbuf->fence_fd = -1;
	if (!vfpriv || !vfpriv->ctx_id)
		return -EINVAL;
	if (vgdev->vdev->max_request <= 256 || exbuf->size == 0 ||
	    exbuf->size > vgdev->vdev->max_request - 256 ||
	    exbuf->num_bo_handles > VIRTGPU_EXEC_MAX_OBJECTS ||
	    (exbuf->flags & ~VIRTGPU_EXECBUF_FLAGS))
		return -EINVAL;

	if (exbuf->flags & VIRTGPU_EXECBUF_FENCE_FD_IN) {
		in_fence = sync_file_get_fence(in_fence_fd);
		if (!in_fence)
			return -EINVAL;
		ret = virtio_gpu_exec_dependency(vgdev, in_fence,
		    vfpriv->software_key, started, false);
		dma_fence_put(in_fence);
		if (ret)
			return ret;
	}
	if (exbuf->flags & VIRTGPU_EXECBUF_FENCE_FD_OUT) {
		ret = -fd_allocfile(&fp, &out_fence_fd);
		if (ret)
			return ret;
	}

	/* Count is capped; still check all arithmetic before budget reservation. */
	if (exbuf->num_bo_handles) {
		size_t unit = sizeof(*bo_handles) + sizeof(hints->objs[0]);
		if (exbuf->num_bo_handles > (SIZE_MAX - sizeof(*hints)) / unit) {
			ret = -ENOMEM;
			goto out;
		}
		hint_bytes = sizeof(*hints) + exbuf->num_bo_handles * unit;
		ret = virtio_gpu_exec_charge(vgdev, hint_bytes);
		if (ret) {
			hint_bytes = 0;
			goto out;
		}
		bo_handles = kvmalloc_array(exbuf->num_bo_handles,
		    sizeof(*bo_handles), GFP_KERNEL);
		if (!bo_handles) {
			ret = -ENOMEM;
			goto out;
		}
		if (copy_from_user(bo_handles, u64_to_user_ptr(exbuf->bo_handles),
		    exbuf->num_bo_handles * sizeof(*bo_handles))) {
			ret = -EFAULT;
			goto out;
		}
		/* Separate allocation from lookup so OOM is not reported as ENOENT. */
		hints = virtio_gpu_array_alloc(exbuf->num_bo_handles);
		if (!hints) {
			ret = -ENOMEM;
			goto out;
		}
		for (count = 0; count < exbuf->num_bo_handles; count++) {
			hints->objs[count] = drm_gem_object_lookup(drm_file, bo_handles[count]);
			if (!hints->objs[count]) {
				ret = -ENOENT;
				goto out;
			}
			hints->nents++;
		}
	}
	buf = kvmalloc(exbuf->size, GFP_KERNEL);
	if (!buf) {
		ret = -ENOMEM;
		goto out;
	}
	if (copy_from_user(buf, u64_to_user_ptr(exbuf->command), exbuf->size)) {
		ret = -EFAULT;
		goto out;
	}
	mutex_lock(&vfpriv->attachment_lock);
	count = vfpriv->attachment_count;
	ret = vfpriv->closing || !vgdev->vqs_ready ? -ENODEV : 0;
	mutex_unlock(&vfpriv->attachment_lock);
	if (ret)
		goto out;
	buflist = virtio_gpu_exec_array_alloc(vgdev, count);
	if (!buflist) {
		ret = -ENOMEM;
		goto out;
	}
	if (vgdev->fence_drv.limit != 0 &&
	    sizeof(*scratch) > SIZE_MAX / vgdev->fence_drv.limit) {
		ret = -ENOMEM;
		goto out;
	}
	scratch_bytes = vgdev->fence_drv.limit * sizeof(*scratch);
	ret = virtio_gpu_exec_charge(vgdev, scratch_bytes);
	if (ret) {
		scratch_bytes = 0;
		goto out;
	}
	scratch = kvmalloc(scratch_bytes, GFP_KERNEL);
	if (!scratch) {
		ret = -ENOMEM;
		goto out;
	}
	out_fence = virtio_gpu_fence_alloc(vgdev);
	if (!out_fence) {
		ret = -ENOMEM;
		goto out;
	}
	out_fence->exec = true;
	out_fence->software_key = vfpriv->software_key;
	if (out_fence_fd >= 0) {
		sync_file = sync_file_create(&out_fence->f, fp);
		if (!sync_file) {
			ret = -ENOMEM;
			goto out;
		}
	}
	mutex_lock(&vfpriv->attachment_lock);
	attached = true;
	ret = virtio_gpu_exec_snapshot(vgdev, vfpriv, buflist, hints);
	if (ret)
		goto out;
	ret = virtio_gpu_array_lock_resv(buflist);
	if (ret)
		goto out;
	locked = true;
	ret = virtio_gpu_exec_dependencies(vgdev, buflist, scratch,
	    vgdev->fence_drv.limit, vfpriv->software_key, started);
	if (ret)
		goto out;
	/* Queue consumes command and locked array on every return. */
	ret = virtio_gpu_cmd_submit(vgdev, buf, exbuf->size,
	    vfpriv->ctx_id, buflist, out_fence);
	buf = NULL;
	buflist = NULL;
	locked = false;
out:
	if (locked)
		virtio_gpu_array_unlock_resv(buflist);
	if (attached)
		mutex_unlock(&vfpriv->attachment_lock);
	if (buflist)
		virtio_gpu_array_put_free(buflist);
	if (hints)
		virtio_gpu_array_put_free(hints);
	kvfree(bo_handles);
	kvfree(scratch);
	kvfree(buf);
	virtio_gpu_exec_uncharge(vgdev, hint_bytes + scratch_bytes);
	if (out_fence)
		dma_fence_put(&out_fence->f);
	if (out_fence_fd >= 0) {
		if (!ret) {
			fd_set_exclose(curlwp, out_fence_fd, true);
			fd_install(out_fence_fd, sync_file->file);
			exbuf->fence_fd = out_fence_fd;
		} else {
			/* fd_abort does not call fo_close on the private file. */
			if (sync_file)
				(void)fp->f_ops->fo_close(fp);
			fd_abort(curproc, fp, out_fence_fd);
		}
	}
	return ret;
}

static int virtio_gpu_getparam_ioctl(struct drm_device *dev, void *data,
				     struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_getparam *param = data;
	int value;

	switch (param->param) {
	case VIRTGPU_PARAM_3D_FEATURES:
		value = vgdev->has_virgl_3d == true ? 1 : 0;
		break;
	case VIRTGPU_PARAM_CAPSET_QUERY_FIX:
		value = 1;
		break;
	default:
		return -EINVAL;
	}
	if (copy_to_user(u64_to_user_ptr(param->value), &value, sizeof(int)))
		return -EFAULT;

	return 0;
}

static int virtio_gpu_resource_create_ioctl(struct drm_device *dev, void *data,
					    struct drm_file *file_priv)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_resource_create *rc = data;
	struct virtio_gpu_fence *fence;
	int ret;
	struct virtio_gpu_object *qobj;
	struct drm_gem_object *obj;
	uint32_t handle = 0;
	struct virtio_gpu_object_params params = { 0 };

	if (vgdev->has_virgl_3d == false) {
		if (rc->format != VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM &&
		    rc->format != VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM)
			return -EINVAL;
		if (rc->depth > 1)
			return -EINVAL;
		if (rc->nr_samples > 1)
			return -EINVAL;
		if (rc->last_level > 1)
			return -EINVAL;
		if (rc->target != 2)
			return -EINVAL;
		if (rc->array_size > 1)
			return -EINVAL;
	}

	params.format = rc->format;
	params.width = rc->width;
	params.height = rc->height;
	params.size = rc->size;
	if (vgdev->has_virgl_3d) {
		params.virgl = true;
		params.target = rc->target;
		params.bind = rc->bind;
		params.depth = rc->depth;
		params.array_size = rc->array_size;
		params.last_level = rc->last_level;
		params.nr_samples = rc->nr_samples;
		params.flags = rc->flags;
	}
	/* allocate a single page size object */
	if (params.size == 0)
		params.size = PAGE_SIZE;

	fence = virtio_gpu_fence_alloc(vgdev);
	if (!fence)
		return -ENOMEM;
	ret = virtio_gpu_object_create(vgdev, &params, &qobj, fence);
	dma_fence_put(&fence->f);
	if (ret < 0)
		return ret;
	obj = &qobj->base.base;

	ret = drm_gem_handle_create(file_priv, obj, &handle);
	if (ret) {
		drm_gem_object_put_unlocked(obj);
		return ret;
	}
	drm_gem_object_put_unlocked(obj);

	rc->res_handle = qobj->hw_res_handle; /* similiar to a VM address */
	rc->bo_handle = handle;
	return 0;
}

static int virtio_gpu_resource_info_ioctl(struct drm_device *dev, void *data,
					  struct drm_file *file_priv)
{
	struct drm_virtgpu_resource_info *ri = data;
	struct drm_gem_object *gobj = NULL;
	struct virtio_gpu_object *qobj = NULL;

	gobj = drm_gem_object_lookup(file_priv, ri->bo_handle);
	if (gobj == NULL)
		return -ENOENT;

	qobj = gem_to_virtio_gpu_obj(gobj);

	ri->size = qobj->base.base.size;
	ri->res_handle = qobj->hw_res_handle;
	drm_gem_object_put_unlocked(gobj);
	return 0;
}

static int virtio_gpu_transfer_from_host_ioctl(struct drm_device *dev,
					       void *data,
					       struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_from_host *args = data;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_fence *fence;
	int ret;
	u32 offset = args->offset;

	if (vgdev->has_virgl_3d == false)
		return -ENOSYS;

	objs = virtio_gpu_array_from_handles(file, &args->bo_handle, 1);
	if (objs == NULL)
		return -ENOENT;

	ret = virtio_gpu_array_lock_resv(objs);
	if (ret != 0)
		goto err_put_free;

	fence = virtio_gpu_fence_alloc(vgdev);
	if (!fence) {
		ret = -ENOMEM;
		goto err_unlock;
	}
	virtio_gpu_cmd_transfer_from_host_3d
		(vgdev, vfpriv->ctx_id, offset, args->level,
		 &args->box, objs, fence);
	dma_fence_put(&fence->f);
	return 0;

err_unlock:
	virtio_gpu_array_unlock_resv(objs);
err_put_free:
	virtio_gpu_array_put_free(objs);
	return ret;
}

static int virtio_gpu_transfer_to_host_ioctl(struct drm_device *dev, void *data,
					     struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct drm_virtgpu_3d_transfer_to_host *args = data;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_fence *fence;
	int ret;
	u32 offset = args->offset;

	objs = virtio_gpu_array_from_handles(file, &args->bo_handle, 1);
	if (objs == NULL)
		return -ENOENT;

	if (!vgdev->has_virgl_3d) {
		return virtio_gpu_cmd_transfer_to_host_2d
			(vgdev, offset,
			 args->box.w, args->box.h, args->box.x, args->box.y,
			 objs, NULL);
	} else {
		ret = virtio_gpu_array_lock_resv(objs);
		if (ret != 0)
			goto err_put_free;

		ret = -ENOMEM;
		fence = virtio_gpu_fence_alloc(vgdev);
		if (!fence)
			goto err_unlock;

		virtio_gpu_cmd_transfer_to_host_3d
			(vgdev,
			 vfpriv ? vfpriv->ctx_id : 0, offset,
			 args->level, &args->box, objs, fence);
		dma_fence_put(&fence->f);
	}
	return 0;

err_unlock:
	virtio_gpu_array_unlock_resv(objs);
err_put_free:
	virtio_gpu_array_put_free(objs);
	return ret;
}

static int virtio_gpu_wait_ioctl(struct drm_device *dev, void *data,
				 struct drm_file *file)
{
	struct drm_virtgpu_3d_wait *args = data;
	struct drm_gem_object *obj;
	long timeout = 15 * HZ;
	int ret;

	obj = drm_gem_object_lookup(file, args->handle);
	if (obj == NULL)
		return -ENOENT;

	if (args->flags & VIRTGPU_WAIT_NOWAIT) {
		ret = dma_resv_test_signaled_rcu(obj->resv, true);
	} else {
		ret = dma_resv_wait_timeout_rcu(obj->resv, true, true,
						timeout);
	}
	if (ret == 0)
		ret = -EBUSY;
	else if (ret > 0)
		ret = 0;

	drm_gem_object_put_unlocked(obj);
	return ret;
}

static int virtio_gpu_get_caps_ioctl(struct drm_device *dev,
				void *data, struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct drm_virtgpu_get_caps *args = data;
	uint32_t i, size;
	int ret;
	struct virtio_gpu_drv_cap_cache *cache_ent;

	if (vgdev->num_capsets == 0)
		return -ENOSYS;

	/* don't allow userspace to pass 0 */
	if (args->size == 0)
		return -EINVAL;

	/* Discovery publishes this immutable table only after full validation. */
	for (i = 0; i < vgdev->num_capsets; i++) {
		if (vgdev->capsets[i].id == args->cap_set_id)
			break;
	}
	ret = virtio_gpu_cmd_get_capset(vgdev, i, args->cap_set_ver, &cache_ent);
	if (ret)
		return ret;
	/* The helper acquired the result interlock; successful bytes are immutable. */
	size = min(args->size, cache_ent->size);
	if (copy_to_user(u64_to_user_ptr(args->addr), cache_ent->caps_cache, size))
		return -EFAULT;

	return 0;
}

struct drm_ioctl_desc virtio_gpu_ioctls[DRM_VIRTIO_NUM_IOCTLS] = {
	DRM_IOCTL_DEF_DRV(VIRTGPU_MAP, virtio_gpu_map_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_EXECBUFFER, virtio_gpu_execbuffer_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GETPARAM, virtio_gpu_getparam_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_CREATE,
			  virtio_gpu_resource_create_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_RESOURCE_INFO, virtio_gpu_resource_info_ioctl,
			  DRM_RENDER_ALLOW),

	/* make transfer async to the main ring? - no sure, can we
	 * thread these in the underlying GL
	 */
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_FROM_HOST,
			  virtio_gpu_transfer_from_host_ioctl,
			  DRM_RENDER_ALLOW),
	DRM_IOCTL_DEF_DRV(VIRTGPU_TRANSFER_TO_HOST,
			  virtio_gpu_transfer_to_host_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_WAIT, virtio_gpu_wait_ioctl,
			  DRM_RENDER_ALLOW),

	DRM_IOCTL_DEF_DRV(VIRTGPU_GET_CAPS, virtio_gpu_get_caps_ioctl,
			  DRM_RENDER_ALLOW),
};
