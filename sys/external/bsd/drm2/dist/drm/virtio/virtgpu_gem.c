/* Origin: EmberBSD native VirtGPU integration of Linux v5.6, 2026-10-06. */
/*	$NetBSD: virtgpu_gem.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $	*/

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
__KERNEL_RCSID(0, "$NetBSD: virtgpu_gem.c,v 1.3 2021/12/18 23:45:45 riastradh Exp $");

#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>

#include "virtgpu_drv.h"

int virtio_gpu_gem_create(struct drm_file *file,
			  struct drm_device *dev,
			  struct virtio_gpu_object_params *params,
			  struct drm_gem_object **obj_p,
			  uint32_t *handle_p)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtio_gpu_object *obj;
	int ret;
	u32 handle;

	ret = virtio_gpu_object_create(vgdev, params, &obj, NULL);
	if (ret < 0)
		return ret;

	ret = drm_gem_handle_create(file, &obj->base.base, &handle);
	if (ret) {
		drm_gem_object_put_unlocked(&obj->base.base);
		return ret;
	}

	*obj_p = &obj->base.base;

	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put_unlocked(&obj->base.base);

	*handle_p = handle;
	return 0;
}

int virtio_gpu_mode_dumb_create(struct drm_file *file_priv,
				struct drm_device *dev,
				struct drm_mode_create_dumb *args)
{
	struct drm_gem_object *gobj;
	struct virtio_gpu_object_params params = { 0 };
	int ret;
	uint32_t pitch;

	if (args->bpp != 32)
		return -EINVAL;

	if (!args->width || !args->height || args->width > UINT32_MAX / 4)
		return -EINVAL;
	pitch = args->width * 4;
	args->size = (uint64_t)pitch * args->height;
	if (args->size > SIZE_MAX - (PAGE_SIZE - 1))
		return -EINVAL;
	args->size = roundup(args->size, PAGE_SIZE);

	params.format = virtio_gpu_translate_format(DRM_FORMAT_HOST_XRGB8888);
	params.width = args->width;
	params.height = args->height;
	params.size = args->size;
	params.dumb = true;
	ret = virtio_gpu_gem_create(file_priv, dev, &params, &gobj,
				    &args->handle);
	if (ret)
		goto fail;

	args->pitch = pitch;
	return ret;

fail:
	return ret;
}

int virtio_gpu_mode_dumb_mmap(struct drm_file *file_priv,
			      struct drm_device *dev,
			      uint32_t handle, uint64_t *offset_p)
{
	struct drm_gem_object *gobj;

	BUG_ON(!offset_p);
	gobj = drm_gem_object_lookup(file_priv, handle);
	if (gobj == NULL)
		return -ENOENT;
	*offset_p = drm_vma_node_offset_addr(&gobj->vma_node);
	drm_gem_object_put_unlocked(gobj);
	return 0;
}

int virtio_gpu_gem_object_open(struct drm_gem_object *obj,
			       struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_attachment *entry;
	int ret = 0;

	if (!vgdev->has_virgl_3d)
		return 0;
	if (!vfpriv)
		return -ENODEV;

	/* PRIME may hold prime.lock; release this lock before returning to core. */
	mutex_lock(&vfpriv->attachment_lock);
	if (!vgdev->vqs_ready || vfpriv->closing) {
		ret = -ENODEV;
		goto out;
	}
	if (!virtio_gpu_object_dma_admitted(vgdev, obj)) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	list_for_each_entry(entry, &vfpriv->attachments, node) {
		if (entry->obj != obj)
			continue;
		if (entry->handles == UINT_MAX)
			ret = -EOVERFLOW;
		else
			entry->handles++;
		goto out;
	}
	if (vfpriv->attachment_count == VIRTGPU_EXEC_MAX_OBJECTS) {
		ret = -ENOMEM;
		goto out;
	}
	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		ret = -ENOMEM;
		goto out;
	}

	objs = virtio_gpu_array_alloc(1);
	if (!objs) {
		kfree(entry);
		ret = -ENOMEM;
		goto out;
	}
	/* The attachment and the request cookie each own a reference. */
	drm_gem_object_get(obj);
	virtio_gpu_array_add_obj(objs, obj);

	ret = virtio_gpu_cmd_context_attach_resource(vgdev, vfpriv->ctx_id,
	    objs);
	if (ret) {
		/* Core does not call close after a failed open callback. */
		virtio_gpu_stop(vgdev, ret);
		drm_gem_object_put_unlocked(obj);
		kfree(entry);
		goto out;
	}
	entry->obj = obj;
	entry->handles = 1;
	list_add_tail(&entry->node, &vfpriv->attachments);
	vfpriv->attachment_count++;
out:
	mutex_unlock(&vfpriv->attachment_lock);
	return ret;
}

void virtio_gpu_gem_object_close(struct drm_gem_object *obj,
				 struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = obj->dev->dev_private;
	struct virtio_gpu_fpriv *vfpriv = file->driver_priv;
	struct virtio_gpu_object_array *objs;
	struct virtio_gpu_attachment *entry;
	int ret;

	if (!vgdev->has_virgl_3d)
		return;
	if (WARN_ON(!vfpriv)) {
		virtio_gpu_stop(vgdev, -EIO);
		return;
	}

	mutex_lock(&vfpriv->attachment_lock);
	list_for_each_entry(entry, &vfpriv->attachments, node) {
		if (entry->obj != obj)
			continue;
		if (--entry->handles != 0)
			goto out;
		objs = virtio_gpu_array_alloc(1);
		ret = -ENOMEM;
		if (objs) {
			virtio_gpu_array_add_obj(objs, obj);
			ret = virtio_gpu_cmd_context_detach_resource(vgdev,
			    vfpriv->ctx_id, objs);
		}
		/* A failed close cannot leave an untracked host attachment. */
		if (ret)
			virtio_gpu_stop(vgdev, ret);
		list_del(&entry->node);
		vfpriv->attachment_count--;
		drm_gem_object_put_unlocked(entry->obj);
		kfree(entry);
		goto out;
	}
	/* A mismatched callback must not permit uncertain host retirement. */
	WARN_ON(1);
	virtio_gpu_stop(vgdev, -EIO);
out:
	mutex_unlock(&vfpriv->attachment_lock);
}

struct virtio_gpu_object_array *virtio_gpu_array_alloc(u32 nents)
{
	struct virtio_gpu_object_array *objs;
	size_t size = sizeof(*objs) + sizeof(objs->objs[0]) * nents;

	objs = kzalloc(size, GFP_KERNEL);
	if (!objs)
		return NULL;

	objs->nents = 0;
	objs->total = nents;
	return objs;
}

static void virtio_gpu_array_free(struct virtio_gpu_object_array *objs)
{
	struct virtio_gpu_device *vgdev = objs->budget_dev;
	size_t bytes = objs->budget_bytes;

	kfree(objs);
	if (vgdev)
		virtio_gpu_exec_uncharge(vgdev, bytes);
}

struct virtio_gpu_object_array*
virtio_gpu_array_from_handles(struct drm_file *drm_file, u32 *handles, u32 nents)
{
	struct virtio_gpu_object_array *objs;
	u32 i;

	objs = virtio_gpu_array_alloc(nents);
	if (!objs)
		return NULL;

	for (i = 0; i < nents; i++) {
		objs->objs[i] = drm_gem_object_lookup(drm_file, handles[i]);
		if (!objs->objs[i]) {
			objs->nents = i;
			virtio_gpu_array_put_free(objs);
			return NULL;
		}
	}
	objs->nents = i;
	return objs;
}

void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *objs,
			      struct drm_gem_object *obj)
{
	if (WARN_ON_ONCE(objs->nents == objs->total))
		return;

	drm_gem_object_get(obj);
	objs->objs[objs->nents] = obj;
	objs->nents++;
}

int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *objs)
{
	int ret;

	if (objs->nents == 1) {
		ret = dma_resv_lock_interruptible(objs->objs[0]->resv, NULL);
	} else {
		ret = drm_gem_lock_reservations(objs->objs, objs->nents,
						&objs->ticket);
		if (ret)
			ww_acquire_fini(&objs->ticket);
	}
	return ret;
}

void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *objs)
{
	if (objs->nents == 1) {
		dma_resv_unlock(objs->objs[0]->resv);
	} else {
		drm_gem_unlock_reservations(objs->objs, objs->nents,
					    &objs->ticket);
	}
}

void virtio_gpu_array_add_fence(struct virtio_gpu_object_array *objs,
				struct dma_fence *fence)
{
	int i;

	for (i = 0; i < objs->nents; i++)
		dma_resv_add_excl_fence(objs->objs[i]->resv, fence);
}

void virtio_gpu_array_put_free(struct virtio_gpu_object_array *objs)
{
	u32 i;

	for (i = 0; i < objs->nents; i++)
		drm_gem_object_put_unlocked(objs->objs[i]);
	virtio_gpu_array_free(objs);
}

void virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *vgdev,
				       struct virtio_gpu_object_array *objs)
{
	spin_lock(&vgdev->obj_free_lock);
	list_add_tail(&objs->next, &vgdev->obj_free_list);
	spin_unlock(&vgdev->obj_free_lock);
	schedule_work(&vgdev->obj_free_work);
}

void virtio_gpu_array_put_free_work(struct work_struct *work)
{
	struct virtio_gpu_device *vgdev =
		container_of(work, struct virtio_gpu_device, obj_free_work);
	struct virtio_gpu_object_array *objs;

	spin_lock(&vgdev->obj_free_lock);
	while (!list_empty(&vgdev->obj_free_list)) {
		objs = list_first_entry(&vgdev->obj_free_list,
					struct virtio_gpu_object_array, next);
		list_del(&objs->next);
		spin_unlock(&vgdev->obj_free_lock);
		virtio_gpu_array_put_free(objs);
		spin_lock(&vgdev->obj_free_lock);
	}
	spin_unlock(&vgdev->obj_free_lock);
}

/* Charged before allocation; the delayed array worker returns its own charge. */
int
virtio_gpu_exec_charge(struct virtio_gpu_device *vgdev, size_t bytes)
{
	int ret = 0;

	spin_lock(&vgdev->dma_lock);
	if (bytes > VIRTGPU_EXEC_BUDGET - vgdev->exec_bytes)
		ret = -ENOMEM;
	else
		vgdev->exec_bytes += bytes;
	spin_unlock(&vgdev->dma_lock);
	return ret;
}

void
virtio_gpu_exec_uncharge(struct virtio_gpu_device *vgdev, size_t bytes)
{
	spin_lock(&vgdev->dma_lock);
	KASSERT(bytes <= vgdev->exec_bytes);
	vgdev->exec_bytes -= bytes;
	spin_unlock(&vgdev->dma_lock);
}

int
virtio_gpu_context_key(struct virtio_gpu_device *vgdev, u64 *key)
{
	int ret = 0;

	spin_lock(&vgdev->dma_lock);
	if (vgdev->next_context_key == UINT64_MAX)
		ret = -EOVERFLOW;
	else
		*key = ++vgdev->next_context_key;
	spin_unlock(&vgdev->dma_lock);
	return ret;
}

struct virtio_gpu_object_array *
virtio_gpu_operation_array_alloc(struct virtio_gpu_device *vgdev, u32 count,
    enum virtgpu_operation_kind kind)
{
	struct virtio_gpu_object_array *objs;
	size_t bytes, unit = sizeof(objs->objs[0]) + sizeof(*objs->members);

	if (count > VIRTGPU_EXEC_MAX_OBJECTS ||
	    count > (SIZE_MAX - sizeof(*objs)) / unit)
		return NULL;
	bytes = sizeof(*objs) + count * unit;
	if (virtio_gpu_exec_charge(vgdev, bytes))
		return NULL;
	objs = kzalloc(bytes, GFP_KERNEL);
	if (!objs) {
		virtio_gpu_exec_uncharge(vgdev, bytes);
		return NULL;
	}
	objs->budget_dev = vgdev;
	objs->budget_bytes = bytes;
	objs->total = count;
	objs->operation = kind;
	objs->members = (void *)&objs->objs[count];
	return objs;
}

struct virtio_gpu_object_array *
virtio_gpu_exec_array_alloc(struct virtio_gpu_device *vgdev, u32 count)
{
	return virtio_gpu_operation_array_alloc(vgdev, count, VIRTGPU_OPERATION_EXEC);
}

/* In-place heapsort: no hidden sleep/allocation under the final mutex. */
static void
virtgpu_exec_sift(struct drm_gem_object **objects, u32 root, u32 count)
{
	struct drm_gem_object *tmp;
	u32 child;

	while (root < count / 2) {
		child = root * 2 + 1;
		if (child + 1 < count &&
		    (uintptr_t)objects[child] < (uintptr_t)objects[child + 1])
			child++;
		if ((uintptr_t)objects[root] >= (uintptr_t)objects[child])
			break;
		tmp = objects[root];
		objects[root] = objects[child];
		objects[child] = tmp;
		root = child;
	}
}

/* attachment_lock held. All storage exists, and hints already own references. */
int
virtio_gpu_exec_snapshot(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_fpriv *vfpriv, struct virtio_gpu_object_array *objs,
    struct virtio_gpu_object_array *hints)
{
	struct virtio_gpu_attachment *entry;
	struct virtio_gpu_object *bo;
	struct drm_gem_object *tmp;
	u32 i, lo, hi, mid;
	bool admitted;

	if (vfpriv->closing || !vgdev->vqs_ready)
		return -ENODEV;
	if (vfpriv->attachment_count > objs->total)
		return -EAGAIN;
	list_for_each_entry(entry, &vfpriv->attachments, node) {
		if (!virtio_gpu_object_dma_admitted(vgdev, entry->obj))
			return -EOPNOTSUPP;
		bo = gem_to_virtio_gpu_obj(entry->obj);
		spin_lock(&vgdev->dma_lock);
		admitted = bo->dma_lease == VIRTGPU_LEASE_OPEN &&
		    !bo->release_pending && !vgdev->dma_stopped;
		spin_unlock(&vgdev->dma_lock);
		if (!admitted)
			return -EOPNOTSUPP;
		virtio_gpu_array_add_obj(objs, entry->obj);
	}
	for (i = objs->nents / 2; i > 0; i--)
		virtgpu_exec_sift(objs->objs, i - 1, objs->nents);
	for (i = objs->nents; i > 1; i--) {
		tmp = objs->objs[0];
		objs->objs[0] = objs->objs[i - 1];
		objs->objs[i - 1] = tmp;
		virtgpu_exec_sift(objs->objs, 0, i - 1);
	}
	for (i = 0; hints && i < hints->nents; i++) {
		lo = 0;
		hi = objs->nents;
		while (lo < hi) {
			mid = lo + (hi - lo) / 2;
			if ((uintptr_t)objs->objs[mid] < (uintptr_t)hints->objs[i])
				lo = mid + 1;
			else
				hi = mid;
		}
		if (lo == objs->nents || objs->objs[lo] != hints->objs[i])
			return -EINVAL;
	}
	return 0;
}

/* Shared bounded ledger scratch, including nonblocking WAIT admission. */
struct dma_fence **
virtio_gpu_dependency_alloc(struct virtio_gpu_device *vgdev, bool nowait,
    size_t *bytes)
{
	struct dma_fence **scratch;
	size_t count = max(vgdev->fence_drv.limit, 1U), size;
	int ret;

	*bytes = 0;
	if (count > SIZE_MAX / sizeof(*scratch))
		return ERR_PTR(-ENOMEM);
	size = count * sizeof(*scratch);
	ret = virtio_gpu_exec_charge(vgdev, size);
	if (ret)
		return ERR_PTR(ret);
	scratch = kvmalloc(size, nowait ? GFP_NOWAIT : GFP_KERNEL);
	if (!scratch) {
		virtio_gpu_exec_uncharge(vgdev, size);
		return ERR_PTR(-ENOMEM);
	}
	*bytes = size;
	return scratch;
}

/* attachment_lock held, after all handle lookups and scratch allocations. */
int
virtio_gpu_transfer_member(struct virtio_gpu_device *vgdev,
    struct virtio_gpu_fpriv *vfpriv, struct drm_gem_object *obj)
{
	struct virtio_gpu_attachment *entry;
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(obj);
	bool admitted;

	if (!vfpriv->ctx_id || !vfpriv->software_key)
		return -EINVAL;
	if (vfpriv->closing || !vgdev->vqs_ready)
		return -ENODEV;
	list_for_each_entry(entry, &vfpriv->attachments, node) {
		if (entry->obj != obj)
			continue;
		if (!virtio_gpu_object_dma_admitted(vgdev, obj))
			return -EOPNOTSUPP;
		spin_lock(&vgdev->dma_lock);
		admitted = bo->dma_lease == VIRTGPU_LEASE_OPEN &&
		    !bo->release_pending && !vgdev->dma_stopped;
		spin_unlock(&vgdev->dma_lock);
		return admitted ? 0 : -EOPNOTSUPP;
	}
	return -EINVAL;
}
