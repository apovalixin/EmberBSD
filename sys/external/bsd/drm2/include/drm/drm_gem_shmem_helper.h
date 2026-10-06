/* Origin: EmberBSD native GEM/UVM helper, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _DRM_GEM_SHMEM_HELPER_H_
#define _DRM_GEM_SHMEM_HELPER_H_
#include <drm/drm_gem.h>
#include <linux/scatterlist.h>
#include <linux/mutex.h>

/* Pages remain wired throughout the host resource's lifetime. */
struct drm_gem_shmem_object {
	struct drm_gem_object base;
	struct page **pages;
	struct mutex lock;
	void *vaddr;
	unsigned int vmap_count;
	unsigned int pin_count;
};
struct drm_gem_shmem_object *drm_gem_shmem_create(struct drm_device *, size_t);
void drm_gem_shmem_free_object(struct drm_gem_object *);
int drm_gem_shmem_pin(struct drm_gem_object *);
void drm_gem_shmem_unpin(struct drm_gem_object *);
struct sg_table *drm_gem_shmem_get_sg_table(struct drm_gem_object *);
void *drm_gem_shmem_vmap(struct drm_gem_object *);
void drm_gem_shmem_vunmap(struct drm_gem_object *, void *);
void drm_gem_shmem_print_info(struct drm_printer *, unsigned int,
    const struct drm_gem_object *);
int drm_gem_shmem_prime_mmap(struct drm_gem_object *, off_t *, size_t, int,
    int *, int *, struct uvm_object **, int *);
extern const struct uvm_pagerops drm_gem_shmem_uvm_ops;
#endif
