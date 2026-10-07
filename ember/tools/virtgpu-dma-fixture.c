/* Origin: EmberBSD; AI-assisted production ARM64 map proof vectors. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <errno.h>
#include <sys/types.h>
#define PAGE_SIZE 4096U
#define MIN(a,b) ((a)<(b)?(a):(b))
#define _BUS_DMAMAP_COHERENT 0x10000
#define _BUS_DMAMAP_IS_BOUNCING 0x20000
#define _BUS_DMA_BUFTYPE_LINEAR 1
#define KVA UINT64_C(0x100000)
#define PA UINT64_C(0x200000)
#define BUS UINT64_C(0x800000)
typedef uint64_t bus_addr_t, bus_size_t, paddr_t;
typedef uintptr_t vaddr_t;
struct proc; struct mbuf; struct uio;
struct vmspace { int unused; };
struct page { paddr_t pa; };
typedef struct arm32_bus_dma_tag *bus_dma_tag_t;
typedef struct arm32_bus_dmamap *bus_dmamap_t;
typedef struct arm32_bus_dma_segment bus_dma_segment_t;
#include "dma-native-layout.h"
static int _bus_dmamap_create(bus_dma_tag_t t, bus_size_t n, int c,
    bus_size_t s, bus_size_t b, int f, bus_dmamap_t *m) { return 0; }
static void _bus_dmamap_destroy(bus_dma_tag_t t, bus_dmamap_t m) { }
static int _bus_dmamap_load(bus_dma_tag_t t, bus_dmamap_t m, void *b,
    bus_size_t n, struct proc *p, int f) { return 0; }
static int unexpected_load(bus_dma_tag_t t, bus_dmamap_t m, void *b,
    bus_size_t n, struct proc *p, int f) { return 0; }
static void _bus_dmamap_unload(bus_dma_tag_t t, bus_dmamap_t m) { }
static void _bus_dmamap_sync(bus_dma_tag_t t, bus_dmamap_t m,
    bus_addr_t a, bus_size_t n, int f) { }
static int unexpected_bounce(bus_dma_tag_t t, bus_dmamap_t m, int f,
    int *r) { return 0; }
static struct vmspace kernel_vm;
static struct vmspace *vmspace_kernel(void) { return &kernel_vm; }
static void *pmap_kernel(void) { return &kernel_vm; }
static struct page pg[2];
static paddr_t physical[2];
static bool alias_coherent, missing_page;
static unsigned int extracts;
static paddr_t page_to_phys(struct page *p) { return p->pa; }
static bool
pmap_extract_coherency(void *pmap, vaddr_t va, paddr_t *pa, bool *coherent)
{
	assert(pmap == pmap_kernel() && va >= KVA && va < KVA + 2 * PAGE_SIZE);
	extracts++;
	*pa = physical[(va - KVA) / PAGE_SIZE] + (va & (PAGE_SIZE - 1));
	*coherent = alias_coherent;
	return !missing_page;
}
#include "dma-production.h"
#include "dma-stub.h"

static const char *const names[] = {
	"translated coalesced adjacent ranges", "idle bounce cookie",
	"false private paddr is ignored", "subpage adjacent range crossing",
	"split segments", "unknown create", "unknown destroy", "unknown load",
	"unknown unload", "unknown sync pre", "missing sync post", "may bounce",
	"missing ranges", "zero ranges", "mixed coherence", "zero range length",
	"physical range overflow", "bus range overflow", "physical overlap",
	"bus overlap", "physical hole", "translation discontinuity",
	"map not coherent", "active bounce", "IOMMU", "NC kernel alias",
	"missing pmap page", "different owned page", "forged segment paddr",
	"unaligned KVA", "unaligned size", "KVA overflow", "wrong page count",
	"zero segment count", "capacity before segment access", "wrong capacity",
	"truncated mapsize", "too small allocation", "wrong original KVA",
	"wrong vmspace", "nonlinear map", "zero segment length",
	"segment exceeds maximum", "truncated segment tail", "extra segment tail",
	"segment address overflow", "noncoherent segment", "null owned page",
	"zero size", "null map", "null KVA", "null pages", "null tag",
	"unaligned physical page", "too many retained segments",
	"nonstandard nonnull loader"
};

static void
check(unsigned int which)
{
	struct arm32_dma_range ranges[2] = {
	    {PA, BUS, PAGE_SIZE, _BUS_DMAMAP_COHERENT},
	    {PA + PAGE_SIZE, BUS + PAGE_SIZE, PAGE_SIZE, _BUS_DMAMAP_COHERENT}};
	struct arm32_bus_dma_tag tag = {
	    ._ranges=ranges, ._nranges=2, ._dmamap_create=_bus_dmamap_create,
	    ._dmamap_destroy=_bus_dmamap_destroy, ._dmamap_load=_bus_dmamap_load,
	    ._dmamap_unload=_bus_dmamap_unload,
	    ._dmamap_sync_pre=_bus_dmamap_sync, ._dmamap_sync_post=_bus_dmamap_sync};
	bus_dmamap_t m = calloc(1, sizeof(*m) + sizeof(m->dm_segs[0]));
	struct page *pages[2] = {&pg[0], &pg[1]};
	bus_dmamap_t argmap = m;
	bus_dma_tag_t argtag = &tag;
	struct page **argpages = pages;
	void *kva = (void *)(uintptr_t)KVA;
	size_t size = 2 * PAGE_SIZE;
	unsigned int count = 2, capacity = 2;
	int ret;

	assert(m);
	alias_coherent = missing_page = false;
	extracts = 0;
	pg[0].pa = physical[0] = PA;
	pg[1].pa = physical[1] = PA + PAGE_SIZE;
	m->_dm_size = m->dm_mapsize = size;
	m->_dm_segcnt = 2;
	m->dm_nsegs = 1;
	m->dm_maxsegsz = size;
	m->_dm_origbuf = kva;
	m->_dm_buftype = _BUS_DMA_BUFTYPE_LINEAR;
	m->_dm_vmspace = vmspace_kernel();
	m->_dm_flags = _BUS_DMAMAP_COHERENT;
	m->dm_segs[0] = (bus_dma_segment_t){BUS, size, _BUS_DMAMAP_COHERENT, BUS};
	switch (which) {
	case 0: break;
	case 1: m->_dm_cookie = m; break;
	case 2: m->dm_segs[0]._ds_paddr = UINT64_MAX; break;
	case 3:
		ranges[0].dr_len = PAGE_SIZE / 2;
		ranges[1].dr_sysbase = PA + PAGE_SIZE / 2;
		ranges[1].dr_busbase = BUS + PAGE_SIZE / 2;
		ranges[1].dr_len = 3 * PAGE_SIZE / 2;
		break;
	case 4:
		m->dm_nsegs = 2; m->dm_segs[0].ds_len = PAGE_SIZE;
		m->dm_segs[1] = (bus_dma_segment_t){BUS + PAGE_SIZE, PAGE_SIZE,
		    _BUS_DMAMAP_COHERENT, 0}; break;
	case 5: tag._dmamap_create = NULL; break;
	case 6: tag._dmamap_destroy = NULL; break;
	case 7: tag._dmamap_load = NULL; break;
	case 8: tag._dmamap_unload = NULL; break;
	case 9: tag._dmamap_sync_pre = NULL; break;
	case 10: tag._dmamap_sync_post = NULL; break;
	case 11: tag._may_bounce = unexpected_bounce; break;
	case 12: tag._ranges = NULL; break;
	case 13: tag._nranges = 0; break;
	case 14: ranges[1].dr_flags = 0; break;
	case 15: ranges[0].dr_len = 0; break;
	case 16: ranges[0].dr_sysbase = UINT64_MAX; break;
	case 17: ranges[0].dr_busbase = UINT64_MAX; break;
	case 18: ranges[1].dr_sysbase--; break;
	case 19: ranges[1].dr_busbase--; break;
	case 20: ranges[0].dr_len--; break;
	case 21: ranges[1].dr_busbase++; break;
	case 22: m->_dm_flags = 0; break;
	case 23: m->_dm_flags |= _BUS_DMAMAP_IS_BOUNCING; break;
	case 24: m->_dm_iommu = m; break;
	case 25: alias_coherent = true; break;
	case 26: missing_page = true; break;
	case 27: pg[1].pa += PAGE_SIZE; break;
	case 28: m->dm_segs[0].ds_addr++; break;
	case 29: kva = (void *)(uintptr_t)(KVA + 1); break;
	case 30: size--; break;
	case 31: kva = (void *)(uintptr_t)(UINTPTR_MAX & ~(PAGE_SIZE - 1)); break;
	case 32: count = 1; break;
	case 33: m->dm_nsegs = 0; break;
	case 34: m->_dm_segcnt = m->dm_nsegs = INT_MAX; break;
	case 35: capacity = 1; break;
	case 36: m->dm_mapsize--; break;
	case 37: m->_dm_size--; break;
	case 38: m->_dm_origbuf = m; break;
	case 39: m->_dm_vmspace = NULL; break;
	case 40: m->_dm_buftype++; break;
	case 41: m->dm_segs[0].ds_len = 0; break;
	case 42: m->dm_maxsegsz--; break;
	case 43: m->dm_segs[0].ds_len--; break;
	case 44: m->dm_nsegs = 2; m->dm_segs[1].ds_len = 1; break;
	case 45: m->dm_segs[0].ds_addr = UINT64_MAX; break;
	case 46: m->dm_segs[0]._ds_flags = 0; break;
	case 47: pages[1] = NULL; break;
	case 48: size = 0; break;
	case 49: argmap = NULL; break;
	case 50: kva = NULL; break;
	case 51: argpages = NULL; break;
	case 52: argtag = NULL; break;
	case 53: physical[0]++; break;
	case 54: m->dm_nsegs = 3; break;
	case 55: tag._dmamap_load = unexpected_load; break;
	default: abort();
	}
	ret = virtio_gpu_dma_eligible(argtag, argmap, kva, size, argpages, count, capacity);
	assert(which < 5 ? ret == 0 : ret < 0);
	if (which == 34 || which == 35 || which == 54)
		assert(!extracts);
	free(m);
	printf("PASS %s\n", names[which]);
}
int
main(void)
{
	unsigned int n = sizeof(names) / sizeof(names[0]);

	assert(virtio_gpu_dma_unavailable(NULL, NULL, NULL, 0, NULL, 0, 0) == -EOPNOTSUPP);
	for (unsigned int i = 0; i < n; i++)
		check(i);
	printf("%u loaded-map vectors passed\n", n);
	return 0;
}
