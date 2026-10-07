/* Origin: EmberBSD; AI-assisted native VirtGPU loaded-map eligibility. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifdef __aarch64__
#define _ARM32_BUS_DMA_PRIVATE
#endif
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/bus.h>
#include <uvm/uvm_extern.h>
#include <linux/mm.h>
#include "virtgpu_drv.h"

#ifdef __aarch64__
/* Static range translation is supported; unknown mapping methods are not. */
static bool
virtgpu_dma_tag_eligible(bus_dma_tag_t tag)
{
	const struct arm32_dma_range *a, *b;
	int i, j;

	if (tag == NULL || tag->_dmamap_create != _bus_dmamap_create ||
	    tag->_dmamap_destroy != _bus_dmamap_destroy ||
	    tag->_dmamap_load != _bus_dmamap_load ||
	    tag->_dmamap_unload != _bus_dmamap_unload ||
	    tag->_dmamap_sync_pre != _bus_dmamap_sync ||
	    tag->_dmamap_sync_post != _bus_dmamap_sync ||
	    tag->_may_bounce != NULL || tag->_ranges == NULL ||
	    tag->_nranges <= 0)
		return false;
	for (i = 0; i < tag->_nranges; i++) {
		a = &tag->_ranges[i];
		if (!a->dr_len || !(a->dr_flags & _BUS_DMAMAP_COHERENT) ||
		    a->dr_len > (bus_addr_t)-1 - a->dr_sysbase ||
		    a->dr_len > (bus_addr_t)-1 - a->dr_busbase)
			return false;
		for (j = 0; j < i; j++) {
			b = &tag->_ranges[j];
			if ((a->dr_sysbase < b->dr_sysbase + b->dr_len &&
			    b->dr_sysbase < a->dr_sysbase + a->dr_len) ||
			    (a->dr_busbase < b->dr_busbase + b->dr_len &&
			    b->dr_busbase < a->dr_busbase + a->dr_len))
				return false;
		}
	}
	return true;
}

/* Caller proves the owned cached alias policy and holds pin/vmap throughout. */
int
virtio_gpu_dma_eligible(bus_dma_tag_t tag, bus_dmamap_t map, void *kva,
    size_t size, struct page **pages, unsigned int npages,
    unsigned int capacity)
{
	const struct arm32_dma_range *range;
	const bus_dma_segment_t *seg;
	size_t offset, chunk, total = 0;
	bus_size_t consumed = 0;
	bus_addr_t delta;
	paddr_t pa;
	bool coherent;
	int i, n = 0;

	if (!virtgpu_dma_tag_eligible(tag))
		return -EOPNOTSUPP;
	if (map == NULL || kva == NULL || pages == NULL || !size ||
	    (size & (PAGE_SIZE - 1)) || ((vaddr_t)kva & (PAGE_SIZE - 1)) ||
	    size > (vaddr_t)-1 - (vaddr_t)kva ||
	    (bus_size_t)size != size || npages != size / PAGE_SIZE ||
	    !capacity || map->_dm_segcnt <= 0 ||
	    (unsigned int)map->_dm_segcnt != capacity || map->dm_nsegs <= 0 ||
	    map->dm_nsegs > map->_dm_segcnt || map->dm_mapsize != size ||
	    map->_dm_size < size || map->_dm_origbuf != kva ||
	    map->_dm_buftype != _BUS_DMA_BUFTYPE_LINEAR ||
	    map->_dm_vmspace != vmspace_kernel())
		return -EINVAL;
	if (map->_dm_iommu != NULL ||
	    !(map->_dm_flags & _BUS_DMAMAP_COHERENT) ||
	    (map->_dm_flags & _BUS_DMAMAP_IS_BOUNCING))
		return -EOPNOTSUPP;
	/* Check allocation bounds and the whole segment tail before traversal. */
	for (i = 0; i < map->dm_nsegs; i++) {
		seg = &map->dm_segs[i];
		if (!seg->ds_len || seg->ds_len > map->dm_maxsegsz ||
		    seg->ds_len > size - total ||
		    seg->ds_len > (bus_addr_t)-1 - seg->ds_addr)
			return -EINVAL;
		if (!(seg->_ds_flags & _BUS_DMAMAP_COHERENT))
			return -EOPNOTSUPP;
		total += seg->ds_len;
	}
	if (total != size)
		return -EINVAL;
	for (offset = 0; offset < size; offset += chunk) {
		if (pages[offset / PAGE_SIZE] == NULL ||
		    !pmap_extract_coherency(pmap_kernel(), (vaddr_t)kva + offset,
		    &pa, &coherent) || coherent ||
		    (pa & (PAGE_SIZE - 1)) != (offset & (PAGE_SIZE - 1)) ||
		    pa - (offset & (PAGE_SIZE - 1)) !=
		    page_to_phys(pages[offset / PAGE_SIZE]))
			return -EOPNOTSUPP;
		for (i = 0; i < tag->_nranges; i++) {
			range = &tag->_ranges[i];
			if (pa >= range->dr_sysbase &&
			    pa - range->dr_sysbase < range->dr_len)
				break;
		}
		if (i == tag->_nranges || n >= map->dm_nsegs)
			return -EOPNOTSUPP;
		range = &tag->_ranges[i];
		delta = pa - range->dr_sysbase;
		seg = &map->dm_segs[n];
		if (seg->ds_addr + consumed != range->dr_busbase + delta)
			return -EOPNOTSUPP;
		chunk = MIN(size - offset, PAGE_SIZE - (offset & (PAGE_SIZE - 1)));
		chunk = MIN(chunk, range->dr_len - delta);
		chunk = MIN(chunk, seg->ds_len - consumed);
		if (!chunk)
			return -EINVAL;
		consumed += chunk;
		if (consumed == seg->ds_len) {
			consumed = 0;
			n++;
		}
	}
	return n == map->dm_nsegs && consumed == 0 ? 0 : -EINVAL;
}
#else
int
virtio_gpu_dma_eligible(bus_dma_tag_t tag, bus_dmamap_t map, void *kva,
    size_t size, struct page **pages, unsigned int npages,
    unsigned int capacity)
{
	return -EOPNOTSUPP;
}
#endif
