/* Origin: EmberBSD native VirtIO transport, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _LINUX_VIRTIO_H_
#define _LINUX_VIRTIO_H_

#include <sys/types.h>
#include <sys/bus.h>
#include <sys/device.h>
#include <sys/mutex.h>

#include <linux/gfp.h>
#include <linux/virtio_sg.h>

struct virtio_softc;
struct virtqueue;
struct virtio_device;
struct linux_virtio_request;
struct virtio_config_ops;
struct netbsd_virtqueue;
typedef void vq_callback_t(struct netbsd_virtqueue *);

#define LINUX_VIRTIO_MAX_QUEUES 2

/* The native struct virtqueue remains untouched and is never macro renamed. */
struct netbsd_virtqueue {
	struct virtio_device *vdev;
	struct virtqueue *native;
	vq_callback_t *callback;
	struct linux_virtio_request **requests;
	unsigned int num_free;
	unsigned int size;
	bool callbacks;
};

struct virtio_device {
	device_t dev;
	void *priv;
	const struct virtio_config_ops *config;
	struct virtio_softc *native;
	bus_dma_tag_t dmat;
	kmutex_t lock;
	enum linux_virtio_state state;
	uint64_t features;
	size_t max_request;
	int (*config_change)(struct virtio_softc *);
	void (*config_callback)(struct virtio_device *);
	void (*cancel)(void *);
	struct virtqueue *native_queues;
	struct netbsd_virtqueue queues[LINUX_VIRTIO_MAX_QUEUES];
	unsigned int nvqs;
	bool attached;
};

/*
 * Called after virtio_child_attach_start.  IPL must match that call.  The
 * config_change belongs to the native child and calls
 * linux_virtio_config_interrupt with this vdev.  config_callback schedules
 * the driver's config work.  cancel releases a cookie after reset.
 * Queue callbacks run under lock at IPL and must only schedule work, never
 * call transport operations.  The owner drains that work before del_vqs/fini.
 */
int linux_virtio_init(struct virtio_device *, struct virtio_softc *, device_t,
    int, size_t, int (*)(struct virtio_softc *),
    void (*)(struct virtio_device *), void (*)(void *));
int linux_virtio_config_interrupt(struct virtio_device *);
void linux_virtio_fini(struct virtio_device *);

#define virtio_find_vqs linux_virtio_find_vqs
#define virtio_device_ready linux_virtio_device_ready
#define virtio_has_feature linux_virtio_has_feature
#define virtio_has_iommu_quirk linux_virtio_has_iommu_quirk
#define virtqueue_add_sgs linux_virtqueue_add_sgs
#define virtqueue_get_buf linux_virtqueue_get_buf
#define virtqueue_disable_cb linux_virtqueue_disable_cb
#define virtqueue_enable_cb linux_virtqueue_enable_cb
#define virtqueue_kick_prepare linux_virtqueue_kick_prepare
#define virtqueue_notify linux_virtqueue_notify

int virtio_find_vqs(struct virtio_device *, unsigned int,
    struct netbsd_virtqueue **, vq_callback_t **, const char *const *, void *);
void virtio_device_ready(struct virtio_device *);
bool virtio_has_feature(struct virtio_device *, unsigned int);
bool virtio_has_iommu_quirk(struct virtio_device *);
int virtqueue_add_sgs(struct netbsd_virtqueue *, struct linux_virtio_sg **,
    unsigned int, unsigned int, void *, gfp_t);
void *virtqueue_get_buf(struct netbsd_virtqueue *, unsigned int *);
void virtqueue_disable_cb(struct netbsd_virtqueue *);
bool virtqueue_enable_cb(struct netbsd_virtqueue *);
bool virtqueue_kick_prepare(struct netbsd_virtqueue *);
bool virtqueue_notify(struct netbsd_virtqueue *);

#endif /* _LINUX_VIRTIO_H_ */
