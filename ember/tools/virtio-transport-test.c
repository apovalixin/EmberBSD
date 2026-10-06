/* Origin: EmberBSD native VirtIO transport lifecycle contract, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

/* Native API model; the script appends the unmodified production body. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#define KM_SLEEP 1
#define KM_NOSLEEP 2
#define MUTEX_SPIN 0
#define BUS_DMA_NOWAIT 1
#define BUS_DMA_READ 2
#define BUS_DMA_WRITE 4
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_PREREAD 2
#define BUS_DMASYNC_POSTWRITE 4
#define BUS_DMASYNC_POSTREAD 8
#define VIRTIO_F_INTR_MPSAFE 1
#define KASSERT assert
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define CTASSERT(x) _Static_assert(x, #x)

typedef void *device_t;
typedef void *bus_dma_tag_t;
typedef int gfp_t;
typedef struct { bool held; } kmutex_t;
struct bus_dmamap {
	unsigned int dm_nsegs;
	size_t dm_mapsize;
	int direction;
	int sync;
};
typedef struct bus_dmamap *bus_dmamap_t;
struct virtio_softc { bool stopped; };
struct virtqueue {
	unsigned int vq_num;
	struct { bool use_indirect; } vq_descx[8];
	bool reserved[8];
	unsigned int cost[8];
	unsigned int free;
	int (*handler)(void *);
	void *arg;
	bool pending;
	bool allocated;
};
static unsigned int maps_live, maps_split = 1;
static unsigned int cancellations, queue_callbacks, config_callbacks;
static bool reserve_full, no_indirect, fail_load, fail_finish;
static unsigned int enqueue_directions[8], enqueues, sync_post;
static unsigned int publications, queue_frees, detaches;
static int missing_queue = -1;

static void mutex_init(kmutex_t *m, int type, int ipl)
{ (void)type; (void)ipl; m->held = false; }
static void mutex_enter(kmutex_t *m) { assert(!m->held); m->held = true; }
static void mutex_exit(kmutex_t *m) { assert(m->held); m->held = false; }
static void mutex_destroy(kmutex_t *m) { assert(!m->held); }
static void *kmem_zalloc(size_t n, int flags)
{ (void)flags; return calloc(1, n); }
#define kmem_intr_zalloc kmem_zalloc
static void kmem_free(void *p, size_t n) { (void)n; free(p); }
#define kmem_intr_free kmem_free
#define panic(...) abort()
static bus_dma_tag_t virtio_dmat(struct virtio_softc *sc) { return sc; }
static uint64_t virtio_features(struct virtio_softc *sc)
{ (void)sc; return UINT64_C(1) << 32; }
static void virtio_init_vq(struct virtio_softc *sc, struct virtqueue *vq,
    unsigned int i, int (*handler)(void *), void *arg)
{
	(void)sc; (void)i;
	vq->vq_num = (int)i == missing_queue ? 0 : 8;
	vq->free = 8;
	vq->handler = handler;
	vq->arg = arg;
}
static int virtio_alloc_vq(struct virtio_softc *sc, struct virtqueue *vq,
    size_t maxsize, unsigned int maxsegs, const char *name)
{
	(void)sc; (void)maxsize; (void)maxsegs; (void)name;
	vq->allocated = true;
	return 0;
}
static int virtio_child_attach_finish(struct virtio_softc *sc,
    struct virtqueue *vqs, unsigned int n,
    int (*config)(struct virtio_softc *), int flags)
{
	(void)sc; (void)vqs; (void)n; (void)config; (void)flags;
	return fail_finish ? 1 : 0;
}
static void virtio_child_detach(struct virtio_softc *sc)
{ assert(sc->stopped); detaches++; }
static int virtio_free_vq(struct virtio_softc *sc, struct virtqueue *vq)
{
	if (!vq->allocated)
		return 0;
	assert(sc->stopped && vq->free == 8);
	queue_frees++;
	vq->allocated = false;
	return 0;
}
static void virtio_reset(struct virtio_softc *sc) { sc->stopped = true; }
static uint32_t virtio_read_device_config_le_4(struct virtio_softc *sc,
    unsigned int offset) { (void)sc; return offset + 4; }
static void virtio_write_device_config_le_4(struct virtio_softc *sc,
    unsigned int offset, uint32_t value)
{ (void)sc; (void)offset; (void)value; }
static int bus_dmamap_create(bus_dma_tag_t dmat, size_t size,
    unsigned int nsegs, size_t maxsize, int boundary, int flags,
    bus_dmamap_t *map)
{
	(void)dmat; (void)size; (void)nsegs; (void)maxsize;
	(void)boundary; (void)flags;
	*map = calloc(1, sizeof(**map));
	maps_live++;
	return 0;
}
static int bus_dmamap_load(bus_dma_tag_t dmat, bus_dmamap_t map,
    void *buffer, size_t length, void *proc, int flags)
{
	(void)dmat; (void)buffer; (void)proc;
	if (fail_load)
		return EFBIG;
	map->dm_nsegs = maps_split;
	map->dm_mapsize = length;
	map->direction = flags & (BUS_DMA_READ | BUS_DMA_WRITE);
	return 0;
}
static void bus_dmamap_sync(bus_dma_tag_t dmat, bus_dmamap_t map,
    size_t start, size_t length, int sync)
{
	(void)dmat; (void)start; (void)length;
	if (sync == BUS_DMASYNC_POSTREAD || sync == BUS_DMASYNC_POSTWRITE) {
		assert(map->sync == (sync == BUS_DMASYNC_POSTREAD ?
		    BUS_DMASYNC_PREREAD : BUS_DMASYNC_PREWRITE));
		sync_post++;
	} else {
		assert(sync == (map->direction == BUS_DMA_WRITE ?
		    BUS_DMASYNC_PREWRITE : BUS_DMASYNC_PREREAD));
	}
	map->sync = sync;
}
static void bus_dmamap_unload(bus_dma_tag_t dmat, bus_dmamap_t map)
{ (void)dmat; (void)map; }
static void bus_dmamap_destroy(bus_dma_tag_t dmat, bus_dmamap_t map)
{ (void)dmat; assert(maps_live > 0); maps_live--; free(map); }
static int virtio_enqueue_prep(struct virtio_softc *sc, struct virtqueue *vq,
    int *slot)
{
	unsigned int i;
	assert(!sc->stopped);
	if (vq->free == 0)
		return EAGAIN;
	for (i = 0; i < 8; i++) {
		if (!vq->reserved[i]) {
			vq->reserved[i] = true;
			vq->free--;
			*slot = i;
			return 0;
		}
	}
	abort();
}
static int virtio_enqueue_reserve(struct virtio_softc *sc,
    struct virtqueue *vq, int slot, unsigned int n)
{
	(void)sc;
	assert(n >= 1 && n <= 8);
	vq->vq_descx[slot].use_indirect = !no_indirect && n >= 2;
	if (reserve_full || (no_indirect && n - 1 > vq->free)) {
		vq->reserved[slot] = false;
		vq->free++;
		return EAGAIN;
	}
	vq->cost[slot] = vq->vq_descx[slot].use_indirect ? 1 : n;
	vq->free -= vq->cost[slot] - 1;
	return 0;
}
static int virtio_enqueue(struct virtio_softc *sc, struct virtqueue *vq,
    int slot, bus_dmamap_t map, bool readable)
{
	(void)sc; (void)vq; (void)slot;
	assert(map->direction == (readable ? BUS_DMA_WRITE : BUS_DMA_READ));
	enqueue_directions[enqueues++ % 8] = readable;
	return 0;
}
static int virtio_enqueue_commit(struct virtio_softc *sc, struct virtqueue *vq,
    int slot, bool notify)
{
	(void)vq; (void)slot; assert(!sc->stopped);
	if (notify)
		publications++;
	return 0;
}
static bool virtio_vq_is_enqueued(struct virtio_softc *sc, struct virtqueue *vq)
{ (void)sc; return vq->pending; }
static int virtio_dequeue(struct virtio_softc *sc, struct virtqueue *vq,
    int *slot, int *len)
{
	(void)sc;
	assert(vq->pending && vq->reserved[0]);
	vq->pending = false;
	*slot = 0; *len = 17;
	return 0;
}
static int virtio_dequeue_commit(struct virtio_softc *sc, struct virtqueue *vq,
    int slot)
{
	(void)sc;
	assert(vq->reserved[slot]);
	vq->reserved[slot] = false;
	vq->free += vq->cost[slot];
	return 0;
}
static void virtio_stop_vq_intr(struct virtio_softc *sc, struct virtqueue *vq)
{ (void)sc; (void)vq; }
static int virtio_start_vq_intr(struct virtio_softc *sc, struct virtqueue *vq)
{ (void)sc; return vq->pending; }

#include "virtio-under-test.h"

static void
cancel_cookie(void *cookie)
{

	assert(cookie != NULL);
	assert(maps_live == 0); /* Sync/unload before the owner frees buffers. */
	cancellations++;
}

static void
queue_callback(struct netbsd_virtqueue *vq)
{

	assert(vq->vdev->lock.held);
	queue_callbacks++;
}

static void
config_callback(struct virtio_device *vdev)
{

	assert(vdev->lock.held);
	config_callbacks++;
}

static int
native_config_callback(struct virtio_softc *sc)
{

	(void)sc;
	return 0; /* Child-specific lookup is outside this transport test. */
}

int
main(void)
{
	struct virtio_softc sc = { 0 };
	struct virtio_device vdev;
	struct netbsd_virtqueue *vqs[2];
	vq_callback_t *callbacks[] = { queue_callback, queue_callback };
	const char *names[] = { "control", "cursor" };
	char data[3];
	struct linux_virtio_sg resp = { &data[2], 1, NULL };
	struct linux_virtio_sg payload = { &data[1], 1, NULL };
	struct linux_virtio_sg cmd = { &data[0], 1, &payload };
	struct linux_virtio_sg *sgs[] = { &cmd, &resp };
	struct netbsd_virtqueue *vq;
	unsigned int len;

	assert(linux_virtio_init(&vdev, &sc, &sc, 0, 4096,
	    native_config_callback, config_callback, cancel_cookie) == 0);
	assert(virtio_has_feature(&vdev, LINUX_VIRTIO_F_VERSION_1));
	assert(!virtio_has_feature(&vdev, 64));
	assert(virtio_has_iommu_quirk(&vdev));
	assert(virtio_find_vqs(&vdev, 2, vqs, callbacks, names, NULL) == 0);
	vq = vqs[0];
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == -ENODEV);
	virtio_device_ready(&vdev);
	maps_split = 3;
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == -EMSGSIZE);
	assert(maps_live == 0 && vq->num_free == 8 && vq->native->free == 8);
	maps_split = 1;
	reserve_full = true;
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == -ENOSPC);
	assert(maps_live == 0 && vq->num_free == 8 && vq->native->free == 8);
	reserve_full = false;
	fail_load = true;
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == -EMSGSIZE);
	assert(maps_live == 0);
	fail_load = false;
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == 0);
	assert(vq->num_free == 7 && maps_live == 3);
	assert(enqueue_directions[0] && enqueue_directions[1] &&
	    !enqueue_directions[2]);
	assert(virtqueue_kick_prepare(vq));
	assert(publications == 1); /* Batch progress before a later notify. */
	assert(virtqueue_notify(vq) && publications == 2);
	assert(vq->native->handler(vq->native->arg) == 1);
	assert(queue_callbacks == 1);
	assert(linux_virtio_config_interrupt(&vdev) == 1);
	assert(config_callbacks == 1);
	virtqueue_disable_cb(vq);
	assert(vq->native->handler(vq->native->arg) == 0);
	vq->native->pending = true;
	assert(!virtqueue_enable_cb(vq));
	assert(virtqueue_get_buf(vq, &len) == data && len == 17);
	assert(maps_live == 0 && sync_post == 3 && vq->num_free == 8);
	assert(virtqueue_enable_cb(vq));
	no_indirect = true;
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == 0);
	assert(vq->num_free == 5);
	vdev.config->reset(&vdev);
	assert(sc.stopped && vdev.state == LINUX_VIRTIO_STOPPED);
	assert(vq->native->handler(vq->native->arg) == 0);
	assert(linux_virtio_config_interrupt(&vdev) == 0);
	assert(queue_callbacks == 1 && config_callbacks == 1);
	assert(virtqueue_add_sgs(vq, sgs, 1, 1, data, 0) == -ENODEV);
	assert(!virtqueue_notify(vq) && publications == 3);
	assert(virtqueue_get_buf(vq, &len) == NULL);
	assert(virtqueue_enable_cb(vq));
	vdev.config->del_vqs(&vdev);
	assert(maps_live == 0 && cancellations == 1 && sync_post == 6);
	assert(queue_frees == 2 && detaches == 1);
	linux_virtio_fini(&vdev); /* Idempotent queue cleanup. */
	assert(queue_frees == 2 && detaches == 1);
	sc.stopped = false;
	fail_finish = true;
	assert(linux_virtio_init(&vdev, &sc, &sc, 0, 4096,
	    NULL, NULL, cancel_cookie) == 0);
	assert(virtio_find_vqs(&vdev, 2, vqs, callbacks, names, NULL) == -EIO);
	assert(vqs[0] == NULL && vqs[1] == NULL && vdev.native_queues == NULL);
	linux_virtio_fini(&vdev);
	assert(queue_frees == 4 && detaches == 2);
	/* A missing second queue must also release the first allocated queue. */
	sc.stopped = false;
	fail_finish = false;
	missing_queue = 1;
	assert(linux_virtio_init(&vdev, &sc, &sc, 0, 4096,
	    NULL, NULL, cancel_cookie) == 0);
	assert(virtio_find_vqs(&vdev, 2, vqs, callbacks, names, NULL) == -EINVAL);
	linux_virtio_fini(&vdev);
	assert(queue_frees == 5 && detaches == 2);
	puts("VirtIO production transport lifecycle contracts passed");
	return 0;
}
