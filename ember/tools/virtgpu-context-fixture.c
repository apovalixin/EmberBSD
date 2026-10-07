/* Origin: EmberBSD; AI-assisted production VirtGPU context lifetime fixtures. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint64_t u64;
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

#define HZ 100
#define GFP_KERNEL 0
#define TASK_COMM_LEN 16
#define MAX_INLINE_CMD_SIZE 96
#define MAX_INLINE_RESP_SIZE 24
#define BUG_ON(c) assert(!(c))
static int
fixture_warn_on(bool condition)
{
	assert(!condition);
	return condition;
}
#define WARN_ON(c) fixture_warn_on(c)
#define cpu_to_le32(x) (x)
#define le32_to_cpu(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ERR_CAST(p) ((void *)(p))
typedef uint32_t u32;
typedef atomic_int atomic_t;
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
struct mutex {
	pthread_mutex_t value;
};
#define mutex_lock(m) assert(pthread_mutex_lock(&(m)->value)==0)
#define mutex_unlock(m) assert(pthread_mutex_unlock(&(m)->value)==0)
#define linux_mutex_init(m) assert(pthread_mutex_init(&(m)->value,NULL)==0)
#define linux_mutex_destroy(m) assert(pthread_mutex_destroy(&(m)->value)==0)
typedef struct {
	struct mutex lock;
	pthread_cond_t cv;
} wait_queue_head_t;
#define DRM_WAKEUP_ALL(cv,lock) assert(pthread_cond_broadcast(cv)==0)
static unsigned int wait_msec;
/* Kernel predicates and interlocking are unchanged; only time is shortened. */
#define wait_event_timeout(q,condition,ticks) ({ \
    wait_queue_head_t *_q=&(q); struct timespec _deadline; int _err=0; \
    long _ret; (void)(ticks); timespec_get(&_deadline,TIME_UTC); \
    _deadline.tv_sec+=wait_msec/1000; _deadline.tv_nsec+=(wait_msec%1000)*1000000L; \
    if(_deadline.tv_nsec>=1000000000L){_deadline.tv_sec++;_deadline.tv_nsec-=1000000000L;} \
    mutex_lock(&_q->lock); \
    while(!(condition) && _err==0) _err=pthread_cond_timedwait(&_q->cv,&_q->lock.value,&_deadline); \
    assert(_err==0 || _err==ETIMEDOUT); _ret=(condition)?1:0; \
    mutex_unlock(&_q->lock); _ret; \
})
static void
wake_up_all(wait_queue_head_t *q)
{
	mutex_lock(&q->lock);
	DRM_WAKEUP_ALL(&q->cv, &q->lock);
	mutex_unlock(&q->lock);
}
struct list_head { struct list_head *next, *prev; };
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
#define list_empty(h) ((h)->next==(h))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry_safe(p,n,h,m) \
    for(p=container_of((h)->next,__typeof__(*p),m), n=container_of(p->m.next,__typeof__(*p),m); \
    &p->m!=(h); p=n,n=container_of(n->m.next,__typeof__(*n),m))
static void list_del(struct list_head *p) { p->next->prev=p->prev; p->prev->next=p->next; }
struct drm_gem_object;
static void drm_gem_object_put_unlocked(struct drm_gem_object *obj) { assert(!obj); }
struct virtio_gpu_device;
struct virtio_gpu_vbuffer;
typedef void (*virtio_gpu_resp_cb)(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
#include "context-layout.h"
struct virtio_device;
struct config_ops { void (*reset)(struct virtio_device *); };
struct native_queue { bool callbacks; };
#define LINUX_VIRTIO_STOPPED 0
struct virtio_device {
	struct mutex lock;
	int state;
	unsigned int nvqs;
	struct native_queue queues[2];
	void *native;
	struct config_ops *config;
};
struct ida { int unused; };
struct work_struct { int unused; };
struct virtio_gpu_device {
	wait_queue_head_t resp_wq;
	struct { wait_queue_head_t ack_queue; } ctrlq, cursorq;
	struct virtio_device *vdev;
	atomic_bool vqs_ready;
	void *vbufs, *cleanup_wq;
	struct work_struct reset_work;
	struct ida ctx_id_ida;
	bool has_virgl_3d;
	atomic_int submit_error;
};
struct drm_device { struct virtio_gpu_device *dev_private; };
struct drm_file { void *driver_priv; };
struct dma_fence { atomic_int refs; };
struct virtio_gpu_fence { struct dma_fence f; };
static struct { char p_comm[64]; } process;
#define current (&process)
static size_t
context_strlcpy(char *dst, const char *src, size_t len)
{
	size_t n = strlen(src);
	assert(len != 0);
	memcpy(dst, src, n < len ? n : len - 1);
	dst[n < len ? n : len - 1] = 0;
	return n;
}
#undef strlcpy
#define strlcpy context_strlcpy

/* This lock belongs to the fake allocator/transport, never to production. */
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t state_cv = PTHREAD_COND_INITIALIZER;
#define state_enter() assert(pthread_mutex_lock(&state_lock)==0)
#define state_exit() assert(pthread_mutex_unlock(&state_lock)==0)
#define state_wake() assert(pthread_cond_broadcast(&state_cv)==0)
#define state_wait(c) do { \
    struct timespec _end; timespec_get(&_end,TIME_UTC); _end.tv_sec+=5; \
    while(!(c)) assert(pthread_cond_timedwait(&state_cv,&state_lock,&_end)==0); \
} while(0)
static bool allocated[16], host_live[16];
static unsigned int ids_freed, native_resets, reset_entries;
static bool reset_hold, reset_entered, reset_release;
static int id_error;
static bool high_id;
static atomic_int allocations, allocation_calls;
static int allocation_fail_at;
static unsigned int
slot(uint32_t id)
{
	assert((id >= 1 && id <= 8) || id == INT_MAX);
	return id == INT_MAX ? 15 : id - 1;
}
static int
ida_simple_get(struct ida *ida, unsigned int start, unsigned int end, int flags)
{
	int ret = -ENOSPC;
	assert(start == 0 && end == INT_MAX);
	state_enter();
	if (id_error) {
		ret = id_error;
	} else if (high_id) {
		assert(!allocated[15]);
		allocated[15] = true;
		ret = INT_MAX - 1;
	} else {
		for (unsigned int i = 0; i < 8; i++) {
			if (!allocated[i]) {
				allocated[i] = true;
				ret = (int)i;
				break;
			}
		}
	}
	state_exit();
	return ret;
}
static void
ida_free(struct ida *ida, unsigned int id)
{
	unsigned int s = slot(id + 1);
	state_enter();
	assert(allocated[s]);
	/* Every release must follow a GPU acknowledgement or completed reset. */
	assert(!host_live[s]);
	allocated[s] = false;
	ids_freed++;
	state_wake();
	state_exit();
}
static void *
test_alloc(size_t size)
{
	int n = atomic_fetch_add(&allocation_calls, 1) + 1;
	if (n == allocation_fail_at)
		return NULL;
	void *p = calloc(1, size);
	assert(p);
	atomic_fetch_add(&allocations, 1);
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
#define kzalloc(n,f) test_alloc(n)
#define kfree(p) test_free(p)
#define kvfree(p) test_free(p)
#define kmem_cache_zalloc(c,f) test_alloc(sizeof(struct virtio_gpu_vbuffer)+MAX_INLINE_CMD_SIZE+MAX_INLINE_RESP_SIZE)
#define kmem_cache_free(c,p) test_free(p)
static struct virtio_gpu_fence *
virtio_gpu_fence_alloc(struct virtio_gpu_device *d)
{
	struct virtio_gpu_fence *f = test_alloc(sizeof(*f));
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
static void virtio_gpu_release_object(struct virtio_gpu_object *o) { assert(!o); }
static void
virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *d, struct virtio_gpu_object_array *a)
{
	assert(!a);
}
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *a) { assert(!a); }
static int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *a) { assert(!a); return 0; }
static void virtgpu_console_stop(struct virtio_gpu_device *d) { }
static void virtio_gpu_fence_stop(struct virtio_gpu_device *d, int error) { }
/* Actual capset failure wakes resp_wq before transport reset returns. */
static void virtio_gpu_fail_capsets(struct virtio_gpu_device *d, int error) { wake_up_all(&d->resp_wq); }
static void queue_work(void *wq, struct work_struct *w) { }
static void
mutex_enter(struct mutex *m)
{
	state_enter();
	reset_entries++;
	state_wake();
	state_exit();
	mutex_lock(m);
}
#define mutex_exit(m) mutex_unlock(m)
/* Native hardware seam; the production reset lock/idempotence is extracted. */
static void
virtio_reset(void *native)
{
	state_enter();
	reset_entered = true;
	state_wake();
	if (reset_hold)
		state_wait(reset_release);
	memset(host_live, 0, sizeof(host_live));
	native_resets++;
	state_exit();
}
static void virtio_gpu_stop(struct virtio_gpu_device *, int);
/* Fence publication itself is exercised by the completion contract. */
static void virtio_gpu_fence_complete(struct virtio_gpu_fence *f, int error) { }
#ifdef DMA_LEASE_SOURCE
static void virtio_gpu_dma_stop(struct virtio_gpu_device *d) { }
/* These unrelated contracts have no qualified backing operations. */
static void virtio_gpu_dma_finish(struct virtio_gpu_vbuffer *b, int error) { }
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *d, struct virtio_gpu_vbuffer *b) { }
#endif
static void virtio_gpu_cancel_vbuf(void *);
static int virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *,
    struct virtio_gpu_vbuffer *, struct virtio_gpu_ctrl_hdr *, struct virtio_gpu_fence *);
/* Key policy is tested by exec-contract; this fixture covers context transport. */
static int virtio_gpu_context_key(struct virtio_gpu_device *d, u64 *key) { *key=1; return 0; }
#include "context-production.h"

/* Only submission and host response injection are models, not driver logic. */
enum response_mode { GOOD, BAD_TYPE, SHORT_REPLY, LONG_REPLY, NO_FENCE, WRONG_FENCE, HOLD, SUBMIT_FAIL };
static enum response_mode mode;
static int submission_error;
static struct virtio_gpu_vbuffer *pending;
static unsigned int submissions;
static uint64_t next_fence;
static void
complete(struct virtio_gpu_vbuffer *b, enum response_mode m)
{
	struct virtio_gpu_device *d = b->vgdev;
	struct virtio_gpu_ctrl_hdr *cmd = (void *)b->buf, *resp = (void *)b->resp_buf;
	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	resp->flags = VIRTIO_GPU_FLAG_FENCE;
	resp->fence_id = cmd->fence_id;
	b->resp_received = b->resp_size;
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
	if (error) {
		virtio_gpu_stop(d, error);
	} else if (cmd->type == VIRTIO_GPU_CMD_CTX_DESTROY) {
		state_enter();
		host_live[slot(cmd->ctx_id)] = false;
		state_exit();
	}
	virtio_gpu_wait_done(b, error);
	dma_fence_put(&b->fence->f);
	free_vbuf(d, b);
}
static int
virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *d,
    struct virtio_gpu_vbuffer *b, struct virtio_gpu_ctrl_hdr *cmd, struct virtio_gpu_fence *f)
{
	assert(f && cmd && b->resp_size == sizeof(*cmd));
	state_enter();
	submissions++;
	cmd->flags = VIRTIO_GPU_FLAG_FENCE;
	cmd->fence_id = ++next_fence;
	b->fence = f;
	atomic_fetch_add(&f->f.refs, 1);
	int error = !d->vqs_ready ? -ENODEV : mode == SUBMIT_FAIL ? submission_error : 0;
	if (error) {
		state_exit();
		virtio_gpu_cancel_vbuf(b);
		return error;
	}
	unsigned int s = slot(cmd->ctx_id);
	assert(allocated[s]);
	if (cmd->type == VIRTIO_GPU_CMD_CTX_CREATE) {
		struct virtio_gpu_ctx_create *c = (void *)cmd;
		assert(b->size == sizeof(*c));
		assert(c->nlen == strlen(c->debug_name));
		assert(c->nlen < TASK_COMM_LEN && c->padding == 0);
		assert(!host_live[s]);
		host_live[s] = true;
	} else {
		assert(cmd->type == VIRTIO_GPU_CMD_CTX_DESTROY);
		assert(b->size == sizeof(struct virtio_gpu_ctx_destroy));
	}
	enum response_mode m = mode;
	if (m == HOLD) {
		assert(!pending);
		pending = b;
		state_wake();
	}
	state_exit();
	if (m != HOLD)
		complete(b, m);
	return 0;
}
static struct config_ops ops = { linux_virtio_reset };
static struct virtio_device transport;
static struct virtio_gpu_device gpu;
static struct drm_device dev = { &gpu };
static void
wait_init(wait_queue_head_t *q)
{
	assert(pthread_mutex_init(&q->lock.value, NULL) == 0);
	assert(pthread_cond_init(&q->cv, NULL) == 0);
}
static void
wait_fini(wait_queue_head_t *q)
{
	assert(pthread_cond_destroy(&q->cv) == 0);
	assert(pthread_mutex_destroy(&q->lock.value) == 0);
}
static void
init(void)
{
	assert(!atomic_load(&allocations));
	memset(&gpu, 0, sizeof(gpu));
	memset(&transport, 0, sizeof(transport));
	assert(pthread_mutex_init(&transport.lock.value, NULL) == 0);
	transport.state = 1;
	transport.nvqs = 2;
	transport.queues[0].callbacks = transport.queues[1].callbacks = true;
	transport.config = &ops;
	gpu.vdev = &transport;
	gpu.has_virgl_3d = true;
	gpu.vqs_ready = true;
	/* Unrelated legacy errors must never become this operation's status. */
	gpu.submit_error = -EPERM;
	wait_init(&gpu.resp_wq);
	wait_init(&gpu.ctrlq.ack_queue);
	wait_init(&gpu.cursorq.ack_queue);
	strcpy(process.p_comm, "context-name-longer-than-task-comm");
	memset(allocated, 0, sizeof(allocated));
	memset(host_live, 0, sizeof(host_live));
	ids_freed = native_resets = reset_entries = submissions = 0;
	reset_hold = reset_entered = reset_release = high_id = false;
	id_error = allocation_fail_at = 0;
	atomic_store(&allocation_calls, 0);
	pending = NULL;
	mode = GOOD;
	wait_msec = 2000;
}
static void
fini(void)
{
	assert(!pending && !atomic_load(&allocations));
	for (unsigned int i = 0; i < 16; i++)
		assert(!allocated[i] && !host_live[i]);
	wait_fini(&gpu.resp_wq);
	wait_fini(&gpu.ctrlq.ack_queue);
	wait_fini(&gpu.cursorq.ack_queue);
	assert(pthread_mutex_destroy(&transport.lock.value) == 0);
}
static void
allocation_tests(void)
{
	for (unsigned int n = 1; n <= 4; n++) {
		struct drm_file f = {0};
		init();
		allocation_fail_at = n; /* private, command, wait, fence */
		assert(virtio_gpu_driver_open(&dev, &f) == -ENOMEM);
		assert(!f.driver_priv && submissions == 0);
		assert(ids_freed == (n == 1 ? 0u : 1u));
		assert(native_resets == (n <= 2 ? 0u : 1u));
		virtio_gpu_driver_postclose(&dev, &f);
		fini();
	}
	for (unsigned int n = 1; n <= 3; n++) {
		struct drm_file f = {0};
		init();
		assert(virtio_gpu_driver_open(&dev, &f) == 0);
		allocation_fail_at = atomic_load(&allocation_calls) + n;
		virtio_gpu_driver_postclose(&dev, &f);
		assert(!f.driver_priv && ids_freed == 1 && native_resets == 1);
		fini();
	}
	for (unsigned int i = 0; i < 2; i++) {
		struct drm_file f = {0};
		init();
		id_error = i ? -ENOSPC : -ENOMEM;
		assert(virtio_gpu_driver_open(&dev, &f) == id_error);
		assert(!f.driver_priv && !ids_freed && !submissions);
		fini();
	}
	puts("PASS allocation failures: private/ID/command/wait/fence, open and close");
}
static void
reply_tests(void)
{
	for (unsigned int close = 0; close <= 1; close++) {
		for (enum response_mode m = BAD_TYPE; m <= WRONG_FENCE; m++) {
			struct drm_file f = {0};
			init();
			if (close)
				assert(virtio_gpu_driver_open(&dev, &f) == 0);
			mode = m;
			if (close)
				virtio_gpu_driver_postclose(&dev, &f);
			else
				assert(virtio_gpu_driver_open(&dev, &f) == -EIO);
			assert(!f.driver_priv && ids_freed == 1 && native_resets == 1);
			assert(!transport.queues[0].callbacks && !transport.queues[1].callbacks);
			fini();
		}
		const int errors[] = { -ENOSPC, -ENOMEM, -ENODEV, -ETIMEDOUT };
		for (unsigned int i = 0; i < sizeof(errors)/sizeof(errors[0]); i++) {
			struct drm_file f = {0};
			init();
			if (close)
				assert(virtio_gpu_driver_open(&dev, &f) == 0);
			mode = SUBMIT_FAIL;
			submission_error = errors[i];
			if (close)
				virtio_gpu_driver_postclose(&dev, &f);
			else
				assert(virtio_gpu_driver_open(&dev, &f) == submission_error);
			assert(!f.driver_priv && ids_freed == 1 && native_resets == 1);
			fini();
		}
	}
	puts("PASS submission errors and malformed/type/length/fence replies");
}
struct operation { struct drm_file file; bool close; int ret; };
static void *
operate(void *arg)
{
	struct operation *op = arg;
	if (op->close)
		virtio_gpu_driver_postclose(&dev, &op->file);
	else
		op->ret = virtio_gpu_driver_open(&dev, &op->file);
	return NULL;
}
static struct virtio_gpu_vbuffer *
wait_pending(void)
{
	state_enter();
	state_wait(pending != NULL);
	struct virtio_gpu_vbuffer *b = pending;
	state_exit();
	return b;
}
static void
finish_pending(bool cancel)
{
	state_enter();
	struct virtio_gpu_vbuffer *b = pending;
	assert(b);
	pending = NULL;
	state_exit();
	if (cancel)
		virtio_gpu_cancel_vbuf(b);
	else
		complete(b, GOOD);
}
static void
identity_tests(void)
{
	struct operation op = {0};
	struct drm_file other = {0}, reused = {0};
	pthread_t worker;
	init();
	mode = HOLD;
	assert(pthread_create(&worker, NULL, operate, &op) == 0);
	(void)wait_pending();
	assert(!op.file.driver_priv && ids_freed == 0);
	finish_pending(false);
	assert(pthread_join(worker, NULL) == 0);
	assert(op.ret == 0 && op.file.driver_priv);
	uint32_t id = ((struct virtio_gpu_fpriv *)op.file.driver_priv)->ctx_id;
	op.close = true;
	assert(pthread_create(&worker, NULL, operate, &op) == 0);
	(void)wait_pending();
	assert(op.file.driver_priv && !ids_freed);
	state_enter();
	mode = GOOD;
	state_exit();
	assert(virtio_gpu_driver_open(&dev, &other) == 0);
	assert(((struct virtio_gpu_fpriv *)other.driver_priv)->ctx_id != id);
	finish_pending(false);
	assert(pthread_join(worker, NULL) == 0);
	assert(!op.file.driver_priv && ids_freed == 1 && !native_resets);
	assert(virtio_gpu_driver_open(&dev, &reused) == 0);
	assert(((struct virtio_gpu_fpriv *)reused.driver_priv)->ctx_id == id);
	virtio_gpu_driver_postclose(&dev, &other);
	virtio_gpu_driver_postclose(&dev, &reused);
	assert(ids_freed == 3);
	fini();
	puts("PASS pending open publication, concurrent close/open, reuse after acknowledgement");
}
static void *
stop_thread(void *arg)
{
	virtio_gpu_stop(&gpu, -EIO);
	return NULL;
}
static void
reset_tests(void)
{
	for (unsigned int close = 0; close <= 1; close++) {
		struct operation op = { .close = close };
		pthread_t worker, stopper;
		init();
		if (close)
			assert(virtio_gpu_driver_open(&dev, &op.file) == 0);
		mode = HOLD;
		reset_hold = true;
		assert(pthread_create(&worker, NULL, operate, &op) == 0);
		(void)wait_pending();
		assert(pthread_create(&stopper, NULL, stop_thread, NULL) == 0);
		state_enter();
		/* The waiter must join a reset which is still holding its lock. */
		state_wait(reset_entered && reset_entries >= 2);
		assert(!gpu.vqs_ready && native_resets == 0 && ids_freed == 0);
		assert(allocated[0] && host_live[0]);
		assert(close ? op.file.driver_priv != NULL : op.file.driver_priv == NULL);
		reset_release = true;
		state_wake();
		state_exit();
		assert(pthread_join(stopper, NULL) == 0);
		assert(pthread_join(worker, NULL) == 0);
		assert(!op.file.driver_priv && (close || op.ret == -ENODEV));
		assert(native_resets == 1 && ids_freed == 1);
		/* Caller reference is gone; cookie still retains wait and fence. */
		assert(atomic_load(&allocations) == 3);
		finish_pending(true);
		fini();
	}
	puts("PASS overlapping native reset: no release at vqs_ready=false; late cancellation");
}
static void
timeout_tests(void)
{
	for (unsigned int close = 0; close <= 1; close++) {
		struct drm_file f = {0};
		init();
		if (close)
			assert(virtio_gpu_driver_open(&dev, &f) == 0);
		mode = HOLD;
		wait_msec = 50;
		if (close)
			virtio_gpu_driver_postclose(&dev, &f);
		else
			assert(virtio_gpu_driver_open(&dev, &f) == -ETIMEDOUT);
		assert(!f.driver_priv && native_resets == 1 && ids_freed == 1);
		assert(atomic_load(&allocations) == 3);
		finish_pending(false);
		fini();
	}
	puts("PASS timeout and late response retain wait/fence storage");
}
static void
boundary_tests(void)
{
	struct drm_file f = {0};
	init();
	high_id = true;
	assert(virtio_gpu_driver_open(&dev, &f) == 0);
	assert(((struct virtio_gpu_fpriv *)f.driver_priv)->ctx_id == INT_MAX);
	assert(gpu.submit_error == -EPERM);
	virtio_gpu_driver_postclose(&dev, &f);
	virtio_gpu_driver_postclose(&dev, &f);
	assert(ids_freed == 1 && !native_resets);
	fini();
	init();
	gpu.has_virgl_3d = false;
	assert(virtio_gpu_driver_open(&dev, &f) == 0);
	virtio_gpu_driver_postclose(&dev, &f);
	assert(!f.driver_priv && !submissions && !atomic_load(&allocation_calls));
	fini();
	puts("PASS signed ID boundary, debug-name wire bounds, sticky-error isolation, disabled feature");
}
int
main(void)
{
	allocation_tests();
	reply_tests();
	identity_tests();
	reset_tests();
	timeout_tests();
	boundary_tests();
	return 0;
}
