/* Origin: EmberBSD native VirtGPU integration of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_object.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

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
__KERNEL_RCSID(0, "$NetBSD: virtgpu_object.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <linux/moduleparam.h>

#include "virtgpu_drv.h"

static int virtio_gpu_resource_id_get(struct virtio_gpu_device *vgdev,
    uint32_t *resid)
{
	int handle = ida_simple_get(&vgdev->resource_ida, 0, 0, GFP_KERNEL);

	if (handle < 0)
		return handle;
	*resid = handle + 1;
	return 0;
}

static void virtio_gpu_resource_id_put(struct virtio_gpu_device *vgdev,
    uint32_t id)
{
	ida_free(&vgdev->resource_ida, id - 1);
}

void virtio_gpu_release_object(struct virtio_gpu_object *bo)
{
	struct drm_gem_object *obj = &bo->base.base;
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;

	virtio_gpu_object_detach(vgdev, bo);
	virtio_gpu_resource_id_put(vgdev, bo->hw_res_handle);
	drm_gem_shmem_free_object(obj);
}

static void virtio_gpu_free_object(struct drm_gem_object *obj)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;

	if (bo->created && vgdev->vqs_ready)
		virtio_gpu_queue_unref(vgdev, bo);
	else
		virtio_gpu_release_object(bo);
}

static const struct drm_gem_object_funcs virtio_gpu_gem_funcs = {
	.free = virtio_gpu_free_object,
	.open = virtio_gpu_gem_object_open,
	.close = virtio_gpu_gem_object_close,

	.print_info = drm_gem_shmem_print_info,
	.pin = drm_gem_shmem_pin,
	.unpin = drm_gem_shmem_unpin,
	.get_sg_table = drm_gem_shmem_get_sg_table,
	.vmap = drm_gem_shmem_vmap,
	.vunmap = drm_gem_shmem_vunmap,
	.mmap = &drm_gem_shmem_mmap,
};

struct drm_gem_object *virtio_gpu_create_object(struct drm_device *dev,
						size_t size)
{
	struct virtio_gpu_object *bo;

	bo = kzalloc(sizeof(*bo), GFP_KERNEL);
	if (!bo)
		return NULL;

	bo->base.base.funcs = &virtio_gpu_gem_funcs;
	return &bo->base.base;
}

int virtio_gpu_object_create(struct virtio_gpu_device *vgdev,
			     struct virtio_gpu_object_params *params,
			     struct virtio_gpu_object **bo_ptr,
			     struct virtio_gpu_fence *fence)
{
	struct virtio_gpu_object_array *objs = NULL;
	struct drm_gem_shmem_object *shmem_obj;
	struct virtio_gpu_object *bo;
	int ret;

	*bo_ptr = NULL;

	if (!params->size || params->size > 256 * 1024 * 1024 ||
	    params->size > SIZE_MAX - PAGE_MASK)
		return -EINVAL;
	params->size = roundup(params->size, PAGE_SIZE);
	shmem_obj = drm_gem_shmem_create(vgdev->ddev, params->size);
	if (IS_ERR(shmem_obj))
		return PTR_ERR(shmem_obj);
	bo = gem_to_virtio_gpu_obj(&shmem_obj->base);

	ret = virtio_gpu_resource_id_get(vgdev, &bo->hw_res_handle);
	if (ret < 0)
		goto err_free_gem;

	bo->dumb = params->dumb;

	if (fence) {
		ret = -ENOMEM;
		objs = virtio_gpu_array_alloc(1);
		if (!objs)
			goto err_put_id;
		virtio_gpu_array_add_obj(objs, &bo->base.base);

		ret = virtio_gpu_array_lock_resv(objs);
		if (ret != 0)
			goto err_put_objs;
	}

	if (params->virgl) {
		virtio_gpu_cmd_resource_create_3d(vgdev, bo, params,
						  objs, fence);
	} else {
		ret = virtio_gpu_cmd_create_resource(vgdev, bo, params,
					       objs, fence);
		if (ret)
			goto err_put_id;
	}

	ret = virtio_gpu_object_attach(vgdev, bo, NULL);
	if (ret != 0) {
		drm_gem_object_put_unlocked(&shmem_obj->base);
		return ret;
	}

	*bo_ptr = bo;
	return 0;

err_put_objs:
	virtio_gpu_array_put_free(objs);
err_put_id:
	drm_gem_object_put_unlocked(&shmem_obj->base);
	return ret;
err_free_gem:
	drm_gem_shmem_free_object(&shmem_obj->base);
	return ret;
}
