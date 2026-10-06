/* Origin: EmberBSD experimental VirtGPU validation, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _VIRTGPU_LIMITS_H_
#define _VIRTGPU_LIMITS_H_
#define VIRTGPU_MAX_OBJECT_SIZE (256U * 1024U * 1024U)
/* Local defensive limits, not VirtIO protocol constants. */
#define VIRTGPU_MAX_CAPSETS 64U
#define VIRTGPU_MAX_CAPSET_SIZE (64U * 1024U)
#define VIRTGPU_CAP_CACHE_BUDGET (1024U * 1024U)
#define VIRTGPU_CAP_CACHE_ENTRIES 128U
static inline bool
virtgpu_2d_size_valid(uint32_t width, uint32_t height, uint64_t size)
{
	return width != 0 && height != 0 && width <= UINT32_MAX / 4 &&
	    size <= VIRTGPU_MAX_OBJECT_SIZE &&
	    (uint64_t)width * height * 4 <= size;
}
static inline bool
virtgpu_transfer_valid(uint32_t bw, uint32_t bh, uint64_t size,
    uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint64_t offset)
{
	return w != 0 && h != 0 && x <= bw && w <= bw - x &&
	    y <= bh && h <= bh - y && offset <= size &&
	    (uint64_t)(h - 1) * bw * 4 + (uint64_t)w * 4 <= size - offset;
}
#endif
