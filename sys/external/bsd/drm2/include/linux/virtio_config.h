/* Origin: EmberBSD native VirtIO transport, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _LINUX_VIRTIO_CONFIG_H_
#define _LINUX_VIRTIO_CONFIG_H_

#include <sys/types.h>
#include <sys/stddef.h>
#include <linux/virtio.h>

/* Linux takes bit indices; native VIRTIO_F_* names denote bit masks. */
#define LINUX_VIRTIO_F_VERSION_1 32
#define LINUX_VIRTIO_F_ACCESS_PLATFORM 33

struct virtio_config_ops {
	void (*reset)(struct virtio_device *);
	void (*del_vqs)(struct virtio_device *);
};

uint32_t linux_virtio_cread_4(struct virtio_device *, unsigned int);
void linux_virtio_cwrite_4(struct virtio_device *, unsigned int, uint32_t);

/* GPU config members consumed by this port are all 32-bit little endian. */
#define virtio_cread(vdev, type, member, value) do {                    \
	CTASSERT(sizeof(((type *)0)->member) == sizeof(uint32_t));        \
	*(value) = linux_virtio_cread_4((vdev), offsetof(type, member));    \
} while (0)
#define virtio_cwrite(vdev, type, member, value) do {                   \
	CTASSERT(sizeof(((type *)0)->member) == sizeof(uint32_t));        \
	linux_virtio_cwrite_4((vdev), offsetof(type, member), *(value));    \
} while (0)

#endif /* _LINUX_VIRTIO_CONFIG_H_ */
