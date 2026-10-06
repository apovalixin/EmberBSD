/* Origin: EmberBSD local native VirtGPU wait adaptation, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#ifndef _VIRTGPU_WAIT_H_
#define _VIRTGPU_WAIT_H_
#include <drm/drm_wait_netbsd.h>

/* Local to this driver: every wake pairs with the condition interlock. */
typedef struct {
	struct mutex lock;
	drm_waitqueue_t cv;
} wait_queue_head_t;

static inline void
init_waitqueue_head(wait_queue_head_t *q)
{
	linux_mutex_init(&q->lock);
	DRM_INIT_WAITQUEUE(&q->cv, "virtgpu");
}
static inline void
virtgpu_wait_destroy(wait_queue_head_t *q)
{
	DRM_DESTROY_WAITQUEUE(&q->cv);
	linux_mutex_destroy(&q->lock);
}
static inline void
wake_up(wait_queue_head_t *q)
{
	mutex_lock(&q->lock);
	DRM_WAKEUP_ALL(&q->cv, &q->lock);
	mutex_unlock(&q->lock);
}
#define wake_up_all wake_up
#define wait_event_timeout(q, condition, ticks) ({ \
	wait_queue_head_t *const _vgq = &(q); \
	long _vgret; \
	mutex_lock(&_vgq->lock); \
	DRM_TIMED_WAIT_NOINTR_UNTIL(_vgret, &_vgq->cv, &_vgq->lock, \
	    (ticks), (condition)); \
	mutex_unlock(&_vgq->lock); \
	_vgret; \
})
#endif
