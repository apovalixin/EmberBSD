/* Origin: EmberBSD native VirtIO transport contract, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */

#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <linux/virtio_sg.h>

int
main(void)
{
	char buffers[3];
	struct linux_virtio_sg response = { &buffers[2], 1, NULL };
	struct linux_virtio_sg payload = { &buffers[1], 1, NULL };
	struct linux_virtio_sg command = { &buffers[0], 1, &payload };
	struct linux_virtio_sg *sgs[] = { &command, &response };
	unsigned int n, descriptors;
	size_t bytes;

	assert(linux_virtio_sg_count(sgs, 1, 1, 8, 3, &n, &bytes) == 0);
	assert(n == 3 && bytes == 3);
	assert(linux_virtio_sg_is_readable(0, 1));
	assert(!linux_virtio_sg_is_readable(1, 1));
	/* One head has multiple entries and each DMA map can split further. */
	descriptors = 0;
	assert(linux_virtio_count_descriptors(&descriptors, 3, 8) == 0);
	assert(linux_virtio_count_descriptors(&descriptors, 4, 8) == 0);
	assert(descriptors == 7);
	assert(linux_virtio_count_descriptors(&descriptors, 2, 8) == -EMSGSIZE);
	assert(descriptors == 7);
	assert(linux_virtio_count_descriptors(&descriptors, 0, 8) == -EINVAL);
	descriptors = UINT_MAX - 1;
	assert(linux_virtio_count_descriptors(&descriptors, 2, UINT_MAX) == -EMSGSIZE);
	assert(linux_virtio_sg_count(sgs, UINT_MAX, 2, 8, 3, &n, &bytes) == -EINVAL);
	assert(linux_virtio_sg_count(sgs, 1, 1, 2, 3, &n, &bytes) == -EMSGSIZE);
	assert(linux_virtio_sg_count(sgs, 1, 1, 8, 2, &n, &bytes) == -EMSGSIZE);
	command.length = (size_t)-1;
	assert(linux_virtio_sg_count(sgs, 1, 1, 8, (size_t)-1, &n, &bytes) == -EMSGSIZE);
	command.length = 1;
	payload.next = &command;
	assert(linux_virtio_sg_count(sgs, 1, 1, 8, 100, &n, &bytes) == -EMSGSIZE);
	payload.next = NULL;
	response.length = 0;
	assert(linux_virtio_sg_count(sgs, 1, 1, 8, 3, &n, &bytes) == -EINVAL);
	response.length = 1;
	assert(linux_virtio_sg_count(NULL, 1, 1, 8, 3, &n, &bytes) == -EINVAL);
	assert(linux_virtio_errno(ENOMEM) == -ENOMEM);
	assert(linux_virtio_errno(0) == 0);
	assert(linux_virtio_errno(-EIO) == -EIO);
	assert(linux_virtio_submission_error(LINUX_VIRTIO_INIT) == -ENODEV);
	assert(linux_virtio_submission_error(LINUX_VIRTIO_QUEUES) == -ENODEV);
	assert(linux_virtio_submission_error(LINUX_VIRTIO_READY) == 0);
	assert(linux_virtio_submission_error(LINUX_VIRTIO_STOPPED) == -ENODEV);
	puts("VirtIO transport arithmetic contracts passed");
	return 0;
}
