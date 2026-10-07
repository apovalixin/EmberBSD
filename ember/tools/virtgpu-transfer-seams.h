/* Origin: EmberBSD; AI-assisted controlled transfer/WAIT native API seams. */
/* SPDX-License-Identifier: BSD-2-Clause */
static void virtio_gpu_array_put_free(struct virtio_gpu_object_array *);
#ifndef CONTROLLED_2D_FOUNDATION
static void bus_dmamap_sync(void *tag, void *map, size_t off, size_t size, int ops)
{
	assert(!off && size==4096);
	virtgpu_dma_sync(&device,map,ops);
}
#endif
#ifndef CONTROLLED_2D_CONTRACT
static int virtio_gpu_cmd_transfer_to_host_2d(struct virtio_gpu_device *d,
    uint64_t offset,u32 w,u32 h,u32 x,u32 y,struct virtio_gpu_object_array *objs,
    struct virtio_gpu_fence *f)
{
	virtio_gpu_array_put_free(objs);
	return -EOPNOTSUPP; /* Untouched 2D implementation has its own contract. */
}
#endif
#ifdef CONTROLLED_2D_CONTRACT
#include "virtgpu_limits.h"
#endif
#ifndef TRANSFER_FOUNDATION
/* Old generic API reports readiness, not terminal errors. All old WAIT cases
 * below use this deterministic completed snapshot; no replacement WAIT logic. */
static int dma_resv_test_signaled_rcu(struct dma_resv *r,bool all) { return 1; }
static long dma_resv_wait_timeout_rcu(struct dma_resv *r,bool all,bool intr,long t)
{ return 1; }
#endif
#ifdef TRANSFER_FOUNDATION
#define GFP_NOWAIT 1
static bool dma_resv_trylock(struct dma_resv *r)
{
	if(r->locked) return false;
	r->locked=true; return true;
}
#endif
