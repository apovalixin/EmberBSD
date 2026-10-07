/* Origin: EmberBSD; AI-assisted production VirtGPU capset boundary fixtures. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <time.h>
#include <linux/virtio_gpu.h>
#include "virtgpu_limits.h"

#define HZ 100
#define GFP_KERNEL 0
#define MAX_INLINE_CMD_SIZE 96
#define MAX_INLINE_RESP_SIZE 24
#define DRM_ERROR(...) ((void)0)
static void test_info(void);
#define DRM_INFO(...) test_info()
#define BUG_ON(c) assert(!(c))
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define min(a,b) ((a)<(b)?(a):(b))
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ERR_CAST(p) ((void *)(p))
#define u64_to_user_ptr(p) ((void *)(uintptr_t)(p))
typedef uint32_t u32;
typedef atomic_int atomic_t;
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
struct mutex {
	pthread_mutex_t value;
};
enum publication_boundary { ALLOCATION, RESULT_LOCK };
static void pause_boundary(enum publication_boundary, struct mutex *);
static void
mutex_lock(struct mutex *m)
{
	pause_boundary(RESULT_LOCK, m);
	assert(pthread_mutex_lock(&m->value) == 0);
}
static void
mutex_unlock(struct mutex *m)
{
	assert(pthread_mutex_unlock(&m->value) == 0);
}
typedef struct {
	struct mutex lock;
	pthread_cond_t cv;
} wait_queue_head_t;
#define DRM_WAKEUP_ALL(cv,lock) assert(pthread_cond_broadcast(cv)==0)
static atomic_int waiters;
/* Real interlocked waits; only the timeout duration is shortened for the test. */
#define wait_event_timeout(q,condition,ticks) ({ \
    wait_queue_head_t *_q=&(q); struct timespec _deadline; int _err=0; \
    long _ret; (void)(ticks); timespec_get(&_deadline,TIME_UTC); \
    _deadline.tv_nsec+=200000000L; \
    if(_deadline.tv_nsec>=1000000000L){_deadline.tv_sec++;_deadline.tv_nsec-=1000000000L;} \
    mutex_lock(&_q->lock); atomic_fetch_add(&waiters,1); \
    while(!(condition) && _err==0) _err=pthread_cond_timedwait(&_q->cv,&_q->lock.value,&_deadline); \
    assert(_err==0 || _err==ETIMEDOUT); _ret=(condition)?1:0; \
    atomic_fetch_sub(&waiters,1); mutex_unlock(&_q->lock); _ret; \
})
struct list_head {
	struct list_head *next, *prev;
};
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
    for(p=container_of((h)->next,__typeof__(*p),m); &p->m!=(h); p=container_of(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) \
    for(p=container_of((h)->next,__typeof__(*p),m), n=container_of(p->m.next,__typeof__(*p),m); \
    &p->m!=(h); p=n,n=container_of(n->m.next,__typeof__(*n),m))
static void
list_add_tail(struct list_head *n, struct list_head *h)
{
	n->prev = h->prev;
	n->next = h;
	h->prev->next = n;
	h->prev = n;
}
struct virtio_gpu_device;
struct virtio_gpu_vbuffer;
typedef void (*virtio_gpu_resp_cb) (struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
#include "capsets-layout.h"
struct virtio_device {
	size_t max_request;
};
struct virtio_gpu_device {
	wait_queue_head_t resp_wq;
	struct virtio_device *vdev;
	bool vqs_ready;
	void *vbufs;
	struct virtio_gpu_drv_capset *capsets;
	uint32_t capsets_allocated, num_capsets;
	struct list_head cap_cache;
	size_t cap_cache_bytes;
	uint32_t cap_cache_entries;
	int capset_error;
};
struct drm_device {
	struct virtio_gpu_device *dev_private;
};
struct drm_file {
	int unused;
};
struct dma_fence {
	atomic_int refs;
};
struct virtio_gpu_fence {
	struct dma_fence f;
};
static atomic_int allocations, allocation_calls;
static int allocation_fail_at;
/* Pause only real allocator/lock API boundaries, never a copied algorithm. */
static pthread_mutex_t boundary_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t boundary_cv = PTHREAD_COND_INITIALIZER;
static pthread_t boundary_thread;
static enum publication_boundary boundary_kind;
static struct mutex *boundary_mutex;
static bool boundary_active, boundary_reached, boundary_release;

static void
pause_boundary(enum publication_boundary kind, struct mutex *lock)
{
	assert(pthread_mutex_lock(&boundary_lock) == 0);
	if (boundary_active && boundary_kind == kind &&
	    (kind == ALLOCATION || lock == boundary_mutex) &&
	    pthread_equal(pthread_self(), boundary_thread)) {
		boundary_reached = true;
		assert(pthread_cond_broadcast(&boundary_cv) == 0);
		while (!boundary_release)
			assert(pthread_cond_wait(&boundary_cv, &boundary_lock) == 0);
		boundary_active = false;
	}
	assert(pthread_mutex_unlock(&boundary_lock) == 0);
}

static void *
test_alloc(size_t size, bool zero)
{
	int n = atomic_fetch_add(&allocation_calls, 1) + 1;
	if (n == allocation_fail_at)
		return NULL;
	void *p = zero ? calloc(1, size) : malloc(size);
	assert(p);
	atomic_fetch_add(&allocations, 1);
	pause_boundary(ALLOCATION, NULL);
	return p;
}
static void
test_free(void *p)
{
	if (p) {
		atomic_fetch_sub(&allocations, 1);
		free(p);
	}
}
#define kzalloc(n,f) test_alloc(n,true)
#define kmalloc(n,f) test_alloc(n,false)
#define kcalloc(n,s,f) test_alloc((n)*(s),true)
#define kfree(p) test_free(p)
#define kvfree(p) test_free(p)
#define kmem_cache_zalloc(c,f) test_alloc(sizeof(struct virtio_gpu_vbuffer)+MAX_INLINE_CMD_SIZE+MAX_INLINE_RESP_SIZE,true)
#define kmem_cache_free(c,p) test_free(p)
static struct virtio_gpu_fence *
virtio_gpu_fence_alloc(struct virtio_gpu_device *d)
{
	struct virtio_gpu_fence *f = test_alloc(sizeof(*f), true);
	if (f)
		atomic_init(&f->f.refs, 1);
	return f;
}
static void
dma_fence_put(struct dma_fence *f)
{
	if (atomic_fetch_sub(&f->refs, 1) == 1)
		test_free(f);
}
static void
virtio_gpu_release_object(struct virtio_gpu_object *o)
{
	assert(!o);
}
static void
virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *d, struct virtio_gpu_object_array *a)
{
	assert(!a);
}
static void
virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a)
{
	assert(!a);
}
static int
virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *a)
{
	assert(!a);
	return 0;
}
static int copies;
static bool copy_fault;
static int
copy_to_user(void *to, const void *from, size_t size)
{
	copies++;
	if (copy_fault)
		return 1;
	memcpy(to, from, size);
	return 0;
}
static void virtio_gpu_fail_capsets(struct virtio_gpu_device *, int);
static void
virtio_gpu_stop(struct virtio_gpu_device *d, int error)
{
	d->vqs_ready = false;
	virtio_gpu_fail_capsets(d, error);
}
/* Fence publication itself is exercised by the completion contract. */
static void virtio_gpu_fence_complete(struct virtio_gpu_fence *f, int error) { }
static void virtio_gpu_cancel_vbuf(void *);
static int
virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *,
    struct virtio_gpu_vbuffer *, struct virtio_gpu_ctrl_hdr *, struct virtio_gpu_fence *);
#include "capsets-production.h"

static struct virtio_gpu_device *stop_at_info;
static void
test_info(void)
{
	if (stop_at_info) {
		struct virtio_gpu_device *d = stop_at_info;
		stop_at_info = NULL;
		virtio_gpu_stop(d, -EIO);
	}
}

/* Transport-only models call the real response validator, callbacks and waits. */
enum response_mode {
	GOOD, BAD_TYPE, SHORT_REPLY, LONG_REPLY, NO_FENCE, WRONG_FENCE, HOLD, SUBMIT_FAIL
};
static enum response_mode mode;
static struct virtio_gpu_vbuffer *pending;
static atomic_int submissions;
static uint32_t host_id[VIRTGPU_MAX_CAPSETS], host_size, host_version;
static bool corrupt_index;
static void
complete(struct virtio_gpu_vbuffer *b, enum response_mode m)
{
	struct virtio_gpu_device *d = b->vgdev;
	struct virtio_gpu_ctrl_hdr *cmd = (void *)b->buf, *resp = (void *)b->resp_buf;
	resp->flags = VIRTIO_GPU_FLAG_FENCE;
	resp->fence_id = cmd->fence_id;
	b->resp_received = b->resp_size;
	if (cmd->type == VIRTIO_GPU_CMD_GET_CAPSET_INFO) {
		struct virtio_gpu_get_capset_info *c = (void *)cmd;
		struct virtio_gpu_resp_capset_info *r = (void *)resp;
		resp->type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;
		r->capset_id = host_id[c->capset_index];
		r->capset_max_size = host_size;
		r->capset_max_version = host_version;
		if (corrupt_index)
			c->capset_index = UINT32_MAX;
	} else {
		assert(cmd->type == VIRTIO_GPU_CMD_GET_CAPSET);
		resp->type = VIRTIO_GPU_RESP_OK_CAPSET;
		memset(((struct virtio_gpu_resp_capset *)resp)->capset_data, 0xa5,
		    b->resp_size - sizeof(*resp));
	}
	if (m == BAD_TYPE)
		resp->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
	if (m == SHORT_REPLY)
		b->resp_received--;
	if (m == LONG_REPLY)
		b->resp_received++;
	if (m == NO_FENCE)
		resp->flags = 0;
	if (m == WRONG_FENCE)
		resp->fence_id++;
	int error = virtio_gpu_response_error(b);
	if (error)
		virtio_gpu_stop(d, error);
	else
		b->resp_cb(d, b);
	virtio_gpu_wait_done(b, error);
	dma_fence_put(&b->fence->f);
	free_vbuf(d, b);
}
static int
virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *d,
    struct virtio_gpu_vbuffer *b, struct virtio_gpu_ctrl_hdr *cmd, struct virtio_gpu_fence *f)
{
	atomic_fetch_add(&submissions, 1);
	assert(f && cmd);
	cmd->flags = VIRTIO_GPU_FLAG_FENCE;
	cmd->fence_id = 42;
	b->fence = f;
	atomic_fetch_add(&f->f.refs, 1);
	if (mode == SUBMIT_FAIL) {
		virtio_gpu_cancel_vbuf(b);
		return -ENOSPC;
	}
	if (mode == HOLD) {
		assert(!pending);
		pending = b;
		return 0;
	}
	complete(b, mode);
	return 0;
}
static struct virtio_device transport = {.max_request = 1024 * 1024};
static void
init(struct virtio_gpu_device *d)
{
	assert(atomic_load(&allocations) == 0);
	memset(d, 0, sizeof(*d));
	d->vdev = &transport;
	d->vqs_ready = true;
	assert(pthread_mutex_init(&d->resp_wq.lock.value, NULL) == 0);
	assert(pthread_cond_init(&d->resp_wq.cv, NULL) == 0);
	d->cap_cache.next = d->cap_cache.prev = &d->cap_cache;
	mode = GOOD;
	pending = NULL;
	host_size = 64;
	host_version = UINT32_MAX;
	for (unsigned i = 0; i < VIRTGPU_MAX_CAPSETS; i++)
		host_id[i] = i + 1;
	atomic_store(&submissions, 0);
	atomic_store(&allocation_calls, 0);
	allocation_fail_at = 0;
	copy_fault = false;
	copies = 0;
	corrupt_index = false;
	transport.max_request = 1024 * 1024;
}
static void
drain(struct virtio_gpu_device *d)
{
	virtio_gpu_stop(d, -ENODEV);
	if (pending) {
		virtio_gpu_cancel_vbuf(pending);
		pending = NULL;
	}
	virtio_gpu_cleanup_cap_cache(d);
	test_free(d->capsets);
	assert(atomic_load(&allocations) == 0 && atomic_load(&waiters) == 0);
	assert(pthread_mutex_destroy(&d->resp_wq.lock.value) == 0);
	assert(pthread_cond_destroy(&d->resp_wq.cv) == 0);
}
static void
discovery(void)
{
	struct virtio_gpu_device d;
	init(&d);
	assert(virtio_gpu_get_capsets(&d, 0) == -ENODEV);
	drain(&d);
	init(&d);
	assert(virtio_gpu_get_capsets(&d, UINT32_MAX) == -E2BIG);
	assert(!d.capsets && !atomic_load(&allocation_calls));
	drain(&d);
	init(&d);
	assert(virtio_gpu_get_capsets(&d, VIRTGPU_MAX_CAPSETS) == 0);
	assert(d.num_capsets == VIRTGPU_MAX_CAPSETS);
	drain(&d);
	init(&d);
	host_id[0] = 9;
	assert(virtio_gpu_get_capsets(&d, 1) == -ENODEV);
	assert(!d.num_capsets);
	drain(&d);
	init(&d);
	host_id[1] = 1;
	assert(virtio_gpu_get_capsets(&d, 2) == -EINVAL);
	assert(!d.num_capsets);
	drain(&d);
	const uint32_t sizes[] = {0, VIRTGPU_MAX_CAPSET_SIZE + 1, UINT32_MAX};
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		init(&d);
		host_size = sizes[i];
		assert(virtio_gpu_get_capsets(&d, 1) == -EINVAL);
		assert(!d.num_capsets);
		drain(&d);
	}
	init(&d);
	host_id[0] = 0;
	assert(virtio_gpu_get_capsets(&d, 1) == -EINVAL);
	drain(&d);
	init(&d);
	corrupt_index = true;
	assert(virtio_gpu_get_capsets(&d, 1) == -EINVAL);
	drain(&d);
	init(&d);
	transport.max_request = host_size + sizeof(struct virtio_gpu_get_capset) + sizeof(struct virtio_gpu_resp_capset) - 1;
	assert(virtio_gpu_get_capsets(&d, 1) == -EINVAL);
	drain(&d);
	for (int m = BAD_TYPE; m <= WRONG_FENCE; m++) {
		init(&d);
		mode = m;
		assert(virtio_gpu_get_capsets(&d, 1) == -EIO);
		assert(!d.num_capsets);
		drain(&d);
	}
	init(&d);
	mode = SUBMIT_FAIL;
	assert(virtio_gpu_get_capsets(&d, 1) == -ENOSPC);
	drain(&d);
	for (int n = 1; n <= 5; n++) {
		init(&d);
		allocation_fail_at = n;
		assert(virtio_gpu_get_capsets(&d, 1) == -ENOMEM);
		assert(!d.num_capsets);
		drain(&d);
	}
	init(&d);
	mode = HOLD;
	assert(virtio_gpu_get_capsets(&d, 1) == -ETIMEDOUT);
	assert(d.capsets && !d.num_capsets && pending);
	complete(pending, GOOD);
	pending = NULL;
	assert(d.capsets[0].id == 0 && d.capsets[0].result.status == -ETIMEDOUT);
	drain(&d);
	puts("PASS: discovery limits, IDs, sizes, allocation/submission errors, exact replies, fenced completion and late callback");
}

struct discovery_query {
	struct virtio_gpu_device *dev;
	int ret;
};

static void *
discovery_thread(void *arg)
{
	struct discovery_query *q = arg;

	assert(pthread_mutex_lock(&boundary_lock) == 0);
	boundary_thread = pthread_self();
	assert(pthread_mutex_unlock(&boundary_lock) == 0);
	q->ret = virtio_gpu_get_capsets(q->dev, 1);
	return NULL;
}

static void
discovery_publication(void)
{
	struct virtio_gpu_device d;
	pthread_t thread;

	for (unsigned stage = ALLOCATION; stage <= RESULT_LOCK; stage++) {
		struct discovery_query q = { .dev = &d };
		struct timespec deadline;

		init(&d);
		boundary_kind = stage;
		boundary_mutex = &d.resp_wq.lock;
		boundary_active = true;
		boundary_reached = boundary_release = false;
		assert(pthread_create(&thread, NULL, discovery_thread, &q) == 0);
		assert(timespec_get(&deadline, TIME_UTC) == TIME_UTC);
		deadline.tv_sec += 2;
		assert(pthread_mutex_lock(&boundary_lock) == 0);
		while (!boundary_reached)
			assert(pthread_cond_timedwait(&boundary_cv, &boundary_lock,
			    &deadline) == 0);
		assert(pthread_mutex_unlock(&boundary_lock) == 0);

		/* A reset reader must not see storage before its publication lock. */
		mutex_lock(&d.resp_wq.lock);
		assert(!d.capsets && d.capsets_allocated == 0);
		mutex_unlock(&d.resp_wq.lock);
		virtio_gpu_stop(&d, -EIO);
		assert(pthread_mutex_lock(&boundary_lock) == 0);
		boundary_release = true;
		assert(pthread_cond_broadcast(&boundary_cv) == 0);
		assert(pthread_mutex_unlock(&boundary_lock) == 0);
		assert(pthread_join(thread, NULL) == 0);
		assert(q.ret == -EIO && d.capset_error == -EIO);
		assert(!d.capsets && !d.capsets_allocated && !d.num_capsets);
		assert(atomic_load(&allocations) == 0);
		assert(atomic_load(&submissions) == 0);
		drain(&d);
	}

	/* A reset after the last reply must also prevent usable-table publication. */
	init(&d);
	stop_at_info = &d;
	assert(virtio_gpu_get_capsets(&d, 1) == -EIO);
	assert(d.capsets && d.capsets_allocated == 1 && !d.num_capsets);
	assert(d.capset_error == -EIO);
	drain(&d);
	puts("PASS: discovery allocation/publication/reset interlock and final error latch");
}
static void
cache_errors(void)
{
	struct virtio_gpu_device d;
	struct virtio_gpu_drv_cap_cache *out;
	for (int n = 1; n <= 6; n++) {
		init(&d);
		assert(virtio_gpu_get_capsets(&d, 1) == 0);
		atomic_store(&allocation_calls, 0);
		allocation_fail_at = n;
		assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -ENOMEM && !out);
		allocation_fail_at = 0;
		if (n >= 3)
			assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -ENOMEM && !out);
		else
			assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == 0 && out);
		drain(&d);
	}
	for (int m = BAD_TYPE; m <= WRONG_FENCE; m++) {
		init(&d);
		assert(virtio_gpu_get_capsets(&d, 1) == 0);
		mode = m;
		assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -EIO && !out);
		drain(&d);
	}
	init(&d);
	assert(virtio_gpu_get_capsets(&d, 1) == 0);
	mode = SUBMIT_FAIL;
	assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -ENOSPC && !out);
	mode = GOOD;
	assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -ENOSPC && !out);
	drain(&d);
	init(&d);
	assert(virtio_gpu_get_capsets(&d, 1) == 0);
	mode = HOLD;
	assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == -ETIMEDOUT && !out);
	struct virtio_gpu_drv_cap_cache *entry = pending->capset_cache;
	assert(!entry->result.received);
	complete(pending, GOOD);
	pending = NULL;
	assert(entry->result.status == -ETIMEDOUT && !entry->result.received);
	drain(&d);
	puts("PASS: cache allocation/submission failures persist, errors never expose a pointer, late success cannot overwrite timeout");
}
struct query {
	struct virtio_gpu_device *dev;
	int ret;
	struct virtio_gpu_drv_cap_cache *entry;
};
static void *
query_thread(void *arg)
{
	struct query *q = arg;
	q->ret = virtio_gpu_cmd_get_capset(q->dev, 0, 0, &q->entry);
	return NULL;
}
static void
wait_for(unsigned count)
{
	for (unsigned n = 0; n < 100; n++) {
		if ((unsigned)atomic_load(&waiters) >= count)
			return;
		struct timespec pause = {0, 1000000};
		nanosleep(&pause, NULL);
	}
	assert(!"waiter failed to arrive");
}
static void
concurrent(void)
{
	for (unsigned reset = 0; reset < 3; reset++) {
		struct virtio_gpu_device d;
		pthread_t a, b;
		init(&d);
		assert(virtio_gpu_get_capsets(&d, 1) == 0);
		mode = HOLD;
		struct query first = {.dev = &d}, second = {.dev = &d};
		assert(pthread_create(&a, NULL, query_thread, &first) == 0);
		wait_for(1);
		assert(pthread_create(&b, NULL, query_thread, &second) == 0);
		wait_for(2);
		assert(atomic_load(&submissions) == 2 && d.cap_cache_entries == 1);
		if (reset == 0) {
			complete(pending, GOOD);
			pending = NULL;
		} else if (reset == 1)
			virtio_gpu_stop(&d, -ENODEV);
		/*
		 * reset==2 lets the interlocked response timeout fail both
		 * waiters.
		 */
		assert(pthread_join(a, NULL) == 0 && pthread_join(b, NULL) == 0);
		int expected = reset == 0 ? 0 : (reset == 1 ? -ENODEV : -ETIMEDOUT);
		assert(first.ret == expected && second.ret == expected);
		if (!reset)
			assert(first.entry && first.entry == second.entry);
		else {
			assert(!first.entry && !second.entry);
			complete(pending, GOOD);
			pending = NULL;
		}
		drain(&d);
	}
	puts("PASS: concurrent same-key requests share one result and wake on success, reset and timeout");
}
static void
ioctls_and_budget(void)
{
	struct virtio_gpu_device d;
	struct virtio_gpu_drv_cap_cache *out;
	unsigned char bytes[128];
	struct drm_device dev = {.dev_private = &d};
	struct drm_virtgpu_get_caps args = {.cap_set_id = 1,.cap_set_ver = 0,.addr = (uintptr_t) bytes,.size = sizeof(bytes)};
	init(&d);
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -ENOSYS);
	assert(virtio_gpu_get_capsets(&d, 3) == 0);
	assert(virtio_gpu_cmd_get_capset(&d, 0, UINT32_MAX, &out) == 0 && out);
	assert(virtio_gpu_cmd_get_capset(&d, UINT32_MAX, 0, &out) == -EINVAL && !out);
	args.cap_set_id = 3;
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -EINVAL);
	args.cap_set_id = 1;
	args.size = 0;
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -EINVAL);
	args.size = sizeof(bytes);
	memset(bytes, 0xcc, sizeof(bytes));
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == 0);
	for (unsigned i = 0; i < sizeof(bytes); i++)
		assert(bytes[i] == (i < host_size ? 0xa5 : 0xcc));
	args.size = 7;
	memset(bytes, 0xcc, sizeof(bytes));
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == 0);
	for (unsigned i = 0; i < sizeof(bytes); i++)
		assert(bytes[i] == (i < 7 ? 0xa5 : 0xcc));
	copy_fault = true;
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -EFAULT);
	drain(&d);
	init(&d);
	host_version = 0;
	assert(virtio_gpu_get_capsets(&d, 1) == 0);
	args.cap_set_ver = UINT32_MAX;
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -EINVAL);
	args.cap_set_ver = 0;
	allocation_fail_at = atomic_load(&allocation_calls) + 4;
	assert(virtio_gpu_get_caps_ioctl(&dev, &args, NULL) == -ENOMEM && copies == 0);
	drain(&d);
	for (unsigned tiny = 0; tiny < 2; tiny++) {
		init(&d);
		host_size = tiny ? 1 : VIRTGPU_MAX_CAPSET_SIZE;
		assert(virtio_gpu_get_capsets(&d, 1) == 0);
		unsigned n = 0;
		int ret;
		while ((ret = virtio_gpu_cmd_get_capset(&d, 0, n, &out)) == 0)
			n++;
		assert(ret == -ENOSPC && !out && n > 0);
		size_t charge = sizeof(struct virtio_gpu_drv_cap_cache) + host_size;
		assert(d.cap_cache_bytes == n * charge && d.cap_cache_bytes <= VIRTGPU_CAP_CACHE_BUDGET);
		assert(n == (tiny ? VIRTGPU_CAP_CACHE_ENTRIES : VIRTGPU_CAP_CACHE_BUDGET / charge));
		assert(virtio_gpu_cmd_get_capset(&d, 0, 0, &out) == 0 && out);
		drain(&d);
	}
	puts("PASS: GET_CAPS bounds/error/copyout, unknown IDs, uint32 versions, aggregate byte and entry budgets");
}
int
main(void)
{
	discovery_publication();
	discovery();
	cache_errors();
	concurrent();
	ioctls_and_budget();
	return 0;
}
