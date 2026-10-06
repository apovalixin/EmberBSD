/* Origin: EmberBSD native GEM/UVM helper, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <uvm/uvm_extern.h>
#include <linux/err.h>
#include <linux/vmalloc.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#define to_shmem(obj) container_of(obj, struct drm_gem_shmem_object, base)

struct drm_gem_shmem_object *
drm_gem_shmem_create(struct drm_device *dev, size_t size)
{
	struct drm_gem_object *obj;
	struct drm_gem_shmem_object *shmem;
	int ret;

	if (size == 0 || size > SIZE_MAX - PAGE_MASK ||
	    (size >> PAGE_SHIFT) > UINT_MAX)
		return ERR_PTR(-EINVAL);
	size = roundup(size, PAGE_SIZE);
	obj = dev->driver->gem_create_object(dev, size);
	if (obj == NULL)
		return ERR_PTR(-ENOMEM);
	shmem = to_shmem(obj);
	linux_mutex_init(&shmem->lock);
	ret = drm_gem_object_init(dev, obj, size);
	if (ret)
		goto fail;
	shmem->pages = drm_gem_get_pages(obj);
	if (IS_ERR(shmem->pages)) {
		ret = PTR_ERR(shmem->pages);
		shmem->pages = NULL;
		goto release;
	}
	ret = drm_gem_create_mmap_offset(obj);
	if (ret)
		goto pages;
	return shmem;
pages:
	drm_gem_put_pages(obj, shmem->pages, false, false);
	kvfree(shmem->pages);
release:
	drm_gem_object_release(obj);
fail:
	linux_mutex_destroy(&shmem->lock);
	kfree(shmem);
	return ERR_PTR(ret);
}

void
drm_gem_shmem_free_object(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);

	KASSERT(shmem->pin_count == 0);
	KASSERT(shmem->vmap_count == 0);
	drm_gem_put_pages(obj, shmem->pages, true, true);
	kvfree(shmem->pages);
	drm_gem_free_mmap_offset(obj);
	drm_gem_object_release(obj);
	linux_mutex_destroy(&shmem->lock);
	kfree(shmem);
}

int
drm_gem_shmem_pin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);
	int ret = 0;

	mutex_lock(&shmem->lock);
	if (shmem->pin_count == UINT_MAX)
		ret = -EOVERFLOW;
	else
		shmem->pin_count++;
	mutex_unlock(&shmem->lock);
	return ret;
}

void
drm_gem_shmem_unpin(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);

	mutex_lock(&shmem->lock);
	KASSERT(shmem->pin_count != 0);
	shmem->pin_count--;
	mutex_unlock(&shmem->lock);
}

struct sg_table *
drm_gem_shmem_get_sg_table(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (sgt == NULL)
		return ERR_PTR(-ENOMEM);
	ret = sg_alloc_table_from_pages(sgt, shmem->pages,
	    obj->size >> PAGE_SHIFT, 0, obj->size, GFP_KERNEL);
	if (ret) {
		kfree(sgt);
		return ERR_PTR(ret);
	}
	return sgt;
}

void *
drm_gem_shmem_vmap(struct drm_gem_object *obj)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);
	void *addr;

	mutex_lock(&shmem->lock);
	if (shmem->vmap_count == 0)
		shmem->vaddr = vmap(shmem->pages, obj->size >> PAGE_SHIFT,
		    0, PAGE_KERNEL);
	addr = shmem->vaddr;
	if (addr)
		shmem->vmap_count++;
	mutex_unlock(&shmem->lock);
	return addr ?: ERR_PTR(-ENOMEM);
}

void
drm_gem_shmem_vunmap(struct drm_gem_object *obj, void *addr)
{
	struct drm_gem_shmem_object *shmem = to_shmem(obj);

	mutex_lock(&shmem->lock);
	KASSERT(addr == shmem->vaddr && shmem->vmap_count != 0);
	if (--shmem->vmap_count == 0) {
		vunmap(addr, obj->size >> PAGE_SHIFT);
		shmem->vaddr = NULL;
	}
	mutex_unlock(&shmem->lock);
}

void
drm_gem_shmem_print_info(struct drm_printer *p, unsigned int indent,
    const struct drm_gem_object *obj)
{
	drm_printf_indent(p, indent, "wired UVM size=%zu\n", obj->size);
}

int
drm_gem_shmem_prime_mmap(struct drm_gem_object *obj, off_t *offp, size_t size,
    int prot, int *flagsp, int *advicep, struct uvm_object **uobjp,
    int *maxprotp)
{
	/* Direct native PRIME hook: offset is in bytes within this object. */
	if (*offp < 0 || (uint64_t)*offp > obj->size ||
	    size > obj->size - (size_t)*offp)
		return -EINVAL;
	drm_gem_object_get(obj);
	*uobjp = &obj->gemo_uvmobj;
	*maxprotp = PROT_READ | PROT_WRITE;
	return 0;
}

static int
drm_gem_shmem_fault(struct uvm_faultinfo *ufi, vaddr_t va,
    struct vm_page **pps, int npages, int centeridx, vm_prot_t access,
    int flags)
{
	struct uvm_object *uobj = ufi->entry->object.uvm_obj;
	struct drm_gem_object *obj = container_of(uobj,
	    struct drm_gem_object, gemo_uvmobj);
	struct drm_gem_shmem_object *shmem = to_shmem(obj);
	voff_t offset = ufi->entry->offset + va - ufi->entry->start;
	vm_prot_t prot = ufi->entry->protection;
	int i, ret = 0;

	if (UVM_ET_ISCOPYONWRITE(ufi->entry)) {
		ret = EIO;
		goto out;
	}
	for (i = 0; i < npages; i++, va += PAGE_SIZE, offset += PAGE_SIZE) {
		if (((flags & PGO_ALLPAGES) == 0 && i != centeridx) ||
		    pps[i] == PGO_DONTCARE)
			continue;
		if (offset >= obj->size) {
			ret = EFAULT;
			break;
		}
		ret = pmap_enter(ufi->orig_map->pmap, va,
		    page_to_phys(shmem->pages[offset >> PAGE_SHIFT]), prot,
		    PMAP_CANFAIL | prot);
		if (ret)
			break;
	}
out:
	pmap_update(ufi->orig_map->pmap);
	uvmfault_unlockall(ufi, ufi->entry->aref.ar_amap, uobj);
	return ret;
}

const struct uvm_pagerops drm_gem_shmem_uvm_ops = {
	.pgo_reference = drm_gem_pager_reference,
	.pgo_detach = drm_gem_pager_detach,
	.pgo_fault = drm_gem_shmem_fault,
};
