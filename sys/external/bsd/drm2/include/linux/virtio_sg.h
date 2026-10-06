/* Origin: EmberBSD native VirtIO transport, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#ifndef _LINUX_VIRTIO_SG_H_
#define _LINUX_VIRTIO_SG_H_

#include <sys/types.h>
#include <sys/errno.h>

/* Command transport only; never substitute this for GEM's scatterlist. */
struct linux_virtio_sg {
	void *buffer;
	size_t length;
	struct linux_virtio_sg *next;
};

enum linux_virtio_state {
	LINUX_VIRTIO_INIT,
	LINUX_VIRTIO_QUEUES,
	LINUX_VIRTIO_READY,
	LINUX_VIRTIO_STOPPED
};

static inline int
linux_virtio_errno(int error)
{

	return error > 0 ? -error : error;
}

/* Bound traversal too: malformed cycles cannot keep a submitter spinning. */
static inline int
linux_virtio_sg_count(struct linux_virtio_sg *const *sgs,
    unsigned int out, unsigned int in, unsigned int limit, size_t maxsize,
    unsigned int *entries, size_t *bytes)
{
	const struct linux_virtio_sg *sg;
	unsigned int i, n = 0;
	size_t size = 0;

	if (out > limit || in > limit - out || out + in == 0 || sgs == NULL)
		return -EINVAL;
	for (i = 0; i < out + in; i++) {
		if (sgs[i] == NULL)
			return -EINVAL;
		for (sg = sgs[i]; sg != NULL; sg = sg->next) {
			if (sg->buffer == NULL || sg->length == 0)
				return -EINVAL;
			if (n == limit || sg->length > maxsize - size)
				return -EMSGSIZE;
			n++;
			size += sg->length;
		}
	}
	*entries = n;
	*bytes = size;
	return 0;
}

/* Count DMA segments, not SG heads or byte ranges. */
static inline int
linux_virtio_count_descriptors(unsigned int *count, unsigned int segments,
    unsigned int limit)
{

	if (segments == 0)
		return -EINVAL;
	if (*count > limit || segments > limit - *count)
		return -EMSGSIZE;
	*count += segments;
	return 0;
}

static inline int
linux_virtio_submission_error(enum linux_virtio_state state)
{

	return state == LINUX_VIRTIO_READY ? 0 : -ENODEV;
}

static inline int
linux_virtio_sg_is_readable(unsigned int head, unsigned int out)
{

	return head < out;
}

#endif /* _LINUX_VIRTIO_SG_H_ */
