/* Origin: EmberBSD; AI-assisted production VirtGPU resource lifetime fixtures. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <sys/wait.h>
#include <unistd.h>
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
#define GFP_NOWAIT 0
#define GFP_ATOMIC 0
#define TASK_COMM_LEN 16
#define PAGE_SIZE 4096
#define PAGE_SHIFT 12
#define MAX_INLINE_CMD_SIZE 96
#define MAX_INLINE_RESP_SIZE 24
#define BUG_ON(c) assert(!(c))
#define KASSERT(c) assert(c)
static int
fixture_warn_on(bool condition)
{
	assert(!condition);
	return condition;
}
#define WARN_ON(c) fixture_warn_on(c)
#define WARN_ON_ONCE(c) WARN_ON(c)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)
#define le32_to_cpu(x) (x)
#define IS_ERR(p) ((uintptr_t)(p) >= (uintptr_t)-4095)
#define PTR_ERR(p) ((int)(intptr_t)(p))
#define ERR_PTR(e) ((void *)(intptr_t)(e))
#define ERR_CAST(p) ((void *)(p))
#define IS_ERR_OR_NULL(p) (!(p) || IS_ERR(p))
#define roundup(n,a) (((n)+(a)-1)&~((a)-1))
typedef uint32_t u32;
typedef uint64_t u64;
typedef atomic_int atomic_t;
#define atomic_read(p) atomic_load(p)
#define atomic_inc(p) atomic_fetch_add(p,1)
#define atomic_dec(p) atomic_fetch_sub(p,1)
#define atomic_set(p,v) atomic_store(p,v)
#define atomic_dec_and_test(p) (atomic_fetch_sub(p,1)==1)
struct mutex {
	pthread_mutex_t value;
};
static void test_mutex_lock(struct mutex *);
#define mutex_lock(m) test_mutex_lock(m)
static void test_mutex_unlock(struct mutex *);
#define mutex_unlock(m) test_mutex_unlock(m)
#define mutex_is_locked(m) true
#define linux_mutex_init(m) assert(pthread_mutex_init(&(m)->value,NULL)==0)
#define linux_mutex_destroy(m) assert(pthread_mutex_destroy(&(m)->value)==0)
#define spin_lock(m) mutex_lock(m)
#define spin_unlock(m) mutex_unlock(m)
#define spin_lock_destroy(m) linux_mutex_destroy(m)
typedef struct {
	struct mutex lock;
	pthread_cond_t cv;
} wait_queue_head_t;
#define DRM_WAKEUP_ALL(cv,lock) assert(pthread_cond_broadcast(cv)==0)
#ifdef BACKING_CONTRACT
static void backing_wait_check(wait_queue_head_t *);
#define WAIT_CHECK(q) backing_wait_check(q)
#else
#define WAIT_CHECK(q) ((void)0)
#endif
static unsigned int wait_msec;
#define wait_event_timeout(q,condition,ticks) ({ \
    wait_queue_head_t *_q=&(q); struct timespec _end; int _err=0; long _ret; \
    WAIT_CHECK(_q); (void)(ticks); timespec_get(&_end,TIME_UTC); _end.tv_sec+=wait_msec/1000; \
    _end.tv_nsec+=(wait_msec%1000)*1000000L; \
    if(_end.tv_nsec>=1000000000L){_end.tv_sec++;_end.tv_nsec-=1000000000L;} \
    mutex_lock(&_q->lock); \
    while(!(condition) && !_err) _err=pthread_cond_timedwait(&_q->cv,&_q->lock.value,&_end); \
    assert(!_err || _err==ETIMEDOUT); _ret=(condition)?1:0; \
    mutex_unlock(&_q->lock); _ret; \
})
static void
wake_up_all(wait_queue_head_t *q)
{
	mutex_lock(&q->lock);
	DRM_WAKEUP_ALL(&q->cv, &q->lock);
	mutex_unlock(&q->lock);
}
struct list_head {
	struct list_head *next, *prev;
};
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
#define list_del_init(h) (list_del(h), INIT_LIST_HEAD(h))
#define list_empty(h) ((h)->next==(h))
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define list_for_each_entry(p,h,m) \
    for(p=container_of((h)->next,__typeof__(*p),m); &p->m!=(h); p=container_of(p->m.next,__typeof__(*p),m))
#define list_for_each_entry_safe(p,n,h,m) \
    for(p=container_of((h)->next,__typeof__(*p),m), n=container_of(p->m.next,__typeof__(*p),m); \
    &p->m!=(h); p=n,n=container_of(n->m.next,__typeof__(*n),m))
static void
list_del(struct list_head *p)
{
	p->next->prev = p->prev;
	p->prev->next = p->next;
}
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
struct drm_file;
struct drm_device;
struct drm_gem_object;
struct object_funcs {
	int (*open) (struct drm_gem_object *, struct drm_file *);
	void (*close) (struct drm_gem_object *, struct drm_file *);
};
struct dma_buf {
	struct drm_gem_object *obj;
	unsigned int refs;
};
struct drm_gem_object {
	struct drm_device *dev;
	const struct object_funcs *funcs;
	atomic_int refs;
	unsigned int handle_count, name;
	size_t size;
	struct mutex reservation;
	struct mutex *resv;
	struct dma_buf *dma_buf;
	void *import_attach;
	struct { const void *pgops; } gemo_uvmobj;
	int vma_node;
};
struct drm_gem_shmem_object {
	struct drm_gem_object base;
	unsigned int pin_count, vmap_count;
	void *vaddr;
	struct page **pages;
};
static void ww_acquire_fini(void *c) { }
struct ww_acquire_ctx {
	int unused;
};
typedef struct dma_map {
	unsigned int dm_nsegs;
	struct {
		uint64_t ds_addr, ds_len;
	}      dm_segs[2];
	bool loaded;
}      *bus_dmamap_t;
struct sg_table {
	struct {
		int sg_dmat;
		bus_dmamap_t sg_dmamap;
	}      sgl[1];
	unsigned int nents;
};
typedef void (*virtio_gpu_resp_cb) (struct virtio_gpu_device *, struct virtio_gpu_vbuffer *);
#include "resource-layout.h"
#define gem_to_virtio_gpu_obj(p) container_of(p,struct virtio_gpu_object,base.base)
struct virtio_device;
struct netbsd_virtqueue { unsigned int num_free; };
struct linux_virtio_sg { void *ptr; unsigned int len; void *unused; };
struct config_ops {
	void (*reset) (struct virtio_device *);
	void (*del_vqs) (struct virtio_device *);
};
struct virtio_device {
	struct mutex lock;
	int state, dmat;
	unsigned int nvqs;
	struct {
		bool callbacks;
	}      queues[2];
	void *native;
	struct config_ops *config;
	size_t max_request;
};
#define LINUX_VIRTIO_STOPPED 0
struct ida {
	bool used[16];
	bool high;
};
struct idr {
	void *ptr[16];
	bool used[16];
};
struct work_struct {
	int unused;
};
struct virtio_gpu_device {
	wait_queue_head_t resp_wq;
	struct {
		wait_queue_head_t ack_queue;
		struct mutex qlock;
		struct netbsd_virtqueue *vq;
		struct work_struct dequeue_work;
	}      ctrlq, cursorq;
	struct virtio_device *vdev;
	struct drm_device *ddev;
	atomic_bool vqs_ready;
	void *vbufs, *cleanup_wq;
	struct work_struct reset_work, config_changed_work, obj_free_work;
	struct mutex submit_lock;
	atomic_t submitters;
	struct { unsigned int limit; int stop_error; bool stopped; struct mutex lock; } fence_drv;
	struct ida ctx_id_ida, resource_ida;
	bool has_virgl_3d;
	atomic_int submit_error;
	struct mutex dma_lock;
	struct list_head dma_leases;
	bool dma_stopped;
	size_t exec_bytes;
	u64 next_context_key;
};
struct driver {
	int (*gem_open_object) (struct drm_gem_object *, struct drm_file *);
	void (*gem_close_object) (struct drm_gem_object *, struct drm_file *);
struct drm_gem_object *(*gem_prime_import) (struct drm_device *, struct dma_buf *);
};
struct drm_device {
	struct virtio_gpu_device *dev_private;
	struct mutex object_name_lock;
	struct idr object_name_idr;
	struct driver *driver;
};
struct prime_private {
	struct mutex lock;
	struct dma_buf *buf;
	uint32_t handle;
};
struct drm_file {
	struct virtio_gpu_fpriv *driver_priv;
	struct mutex table_lock;
	struct idr object_idr;
	struct prime_private prime;
	atomic_uint vmas;
};
struct drm_gem_open {
	uint64_t name, size;
	uint32_t handle;
};
struct dma_fence {
	atomic_int refs;
};
struct virtio_gpu_fence {
	struct dma_fence f;
	bool unref;
	unsigned int prior_ids;
};
static struct {
	char p_comm[16];
}      process = {"resource-test"};
#define current (&process)
static size_t
test_strlcpy(char *dst, const char *src, size_t len){
	size_t n = strlen(src);
	assert(len);
	memcpy(dst, src, n < len ? n : len - 1);
	dst[n < len ? n : len - 1] = 0;
	return n;
}
#undef strlcpy
#define strlcpy test_strlcpy

static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
#define enter() assert(pthread_mutex_lock(&gate)==0)
#define leave() assert(pthread_mutex_unlock(&gate)==0)
#define announce() assert(pthread_cond_broadcast(&cv)==0)
#define await(c) do { struct timespec _end;timespec_get(&_end,TIME_UTC);_end.tv_sec+=5; \
    while(!(c))assert(pthread_cond_timedwait(&cv,&gate,&_end)==0); } while(0)
static struct virtio_device transport;
static struct virtio_gpu_device gpu;
static struct driver driver;
static struct drm_device dev;
static atomic_int allocations, allocation_calls;
static int allocation_fail_at, handle_error, vma_error, prime_error, reservation_error,
    dma_error;
static int resource_id_error;
static struct virtio_gpu_object *last_bo;
static unsigned int dma_nents;
static bool long_segment, high_id, reset_hold, reset_entered, reset_release;
static unsigned int resets, reset_entries, ids_freed, bos_freed, pins, maps,
    vmaps, unrefs;
static bool host_context[16], host_resource[16], host_backing[16], attached[16][16];
static unsigned int attach_commands, detach_commands, create_commands, backing_commands;
static struct dma_buf dmabuf;
static struct mutex *watched_lock;
static unsigned int watched_entries;
static void
test_mutex_lock(struct mutex *m)
{
	enter();
	if (m == watched_lock) {
		watched_entries++;
		announce();
	}
	leave();
	assert(pthread_mutex_lock(&m->value) == 0);
}
static unsigned int
resource_slot(uint32_t id)
{
	assert((id >= 1 && id <= 8) || id == UINT32_C(0x80000000));
	return id == UINT32_C(0x80000000) ? 15 : id - 1;
}
static void *
test_alloc(size_t n)
{
	int c = atomic_fetch_add(&allocation_calls, 1) + 1;
	if (c == allocation_fail_at)
		return NULL;
	void *p = calloc(1, n);
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
#define kmalloc(n,f) test_alloc(n)
#define kcalloc(n,s,f) test_alloc((n)*(s))
#define kfree(p) test_free(p)
#define kvfree(p) test_free(p)
#define kmem_cache_zalloc(c,f) test_alloc(sizeof(struct virtio_gpu_vbuffer)+MAX_INLINE_CMD_SIZE+MAX_INLINE_RESP_SIZE)
#define kmem_cache_free(c,p) test_free(p)
static int
ida_simple_get(struct ida *ida, unsigned int start, unsigned int end, int flags)
{
	int id = -ENOSPC;
	assert(start == 0 && (ida == &gpu.resource_ida ? end == 0 : end == INT_MAX));
	enter();

	if (ida == &gpu.resource_ida && resource_id_error) {
		id = resource_id_error;
	} else if (ida == &gpu.resource_ida && high_id) {
		assert(!ida->high);
		ida->high = true;
		id = INT_MAX;
	} else
		for (unsigned int i = 0; i < 8; i++)
			if (!ida->used[i]) {
				ida->used[i] = true;
				id = i;
				break;
			}
	leave();
	return id;
}
static void
ida_free(struct ida *ida, unsigned int id)
{
	enter();

	if (ida == &gpu.resource_ida) {
		unsigned int s = resource_slot(id + 1);
		assert(!host_resource[s] && !host_backing[s]);
		for (unsigned int i = 0; i < 16; i++)
			assert(!attached[i][s]);
		ids_freed++;
	} else
		assert(id < 8 && !host_context[id]);

	if (id == INT_MAX) {
		assert(ida->high);
		ida->high = false;
	} else {
		assert(id < 8 && ida->used[id]);
		ida->used[id] = false;
	}
	announce();
	leave();
}
static void
idr_preload(int flags)
{
}
static void
idr_preload_end(void)
{
}
static int
idr_alloc(struct idr *id, void *p, int start, int end, int flags)
{
	if (handle_error)
		return handle_error;
	for (int i = start; i < 16; i++)
		if (!id->used[i]) {
			id->used[i] = true;
			id->ptr[i] = p;
			return i;
		}
	return -ENOSPC;
}
static void *
idr_replace(struct idr *id, void *p, int n)
{
	if (n < 0 || n >= 16 || !id->used[n])
		return ERR_PTR(-ENOENT);
	void *old = id->ptr[n];
	id->ptr[n] = p;
	return old;
}
static void *
idr_remove(struct idr *id, int n)
{
	assert(n >= 0 && n < 16 && id->used[n]);
	void *p = id->ptr[n];
	id->used[n] = false;
	id->ptr[n] = NULL;
	return p;
}
static void *
idr_find(struct idr *id, int n)
{
	return n >= 0 && n < 16 && id->used[n] ? id->ptr[n] : NULL;
}
static int
    idr_for_each(struct idr *id, int (*cb) (int, void *, void *), void *arg){
	for (int i = 0; i < 16; i++)
		if (id->used[i]) {
			int r = cb(i, id->ptr[i], arg);
			if (r)
				return r;
		}
	return 0;
}
static void
idr_destroy(struct idr *id)
{
	memset(id, 0, sizeof(*id));
}
static int
drm_vma_node_allow(int *node, struct drm_file *file)
{
	if (vma_error)
		return vma_error;
	file->vmas++;
	return 0;
}
static void
drm_vma_node_revoke(int *node, struct drm_file *file)
{
	assert(file->vmas);
	file->vmas--;
}
#define DRIVER_GEM 1
static bool
drm_core_check_feature(struct drm_device *d, int feature)
{
	return true;
}
static void
drm_gem_object_get(struct drm_gem_object *o)
{
	assert(atomic_fetch_add(&o->refs, 1) > 0);
}
static void virtio_gpu_free_object(struct drm_gem_object *);
static void
drm_gem_object_put_unlocked(struct drm_gem_object *o)
{
	int n = atomic_fetch_sub(&o->refs, 1);
	assert(n > 0);
	if (n == 1)
		virtio_gpu_free_object(o);
}
static struct dma_buf *
dma_buf_get(int fd)
{
	assert(fd == 7);
	dmabuf.refs++;
	return &dmabuf;
}
static void
get_dma_buf(struct dma_buf *b)
{
	b->refs++;
}
static void
dma_buf_put(struct dma_buf *b)
{
	assert(b->refs);
	b->refs--;
}
static struct drm_gem_object *
drm_gem_prime_import(struct drm_device *d, struct dma_buf *b)
{
	drm_gem_object_get(b->obj);
	return b->obj;
}
static int
drm_prime_lookup_buf_handle(struct prime_private *p, struct dma_buf *b, uint32_t *h)
{
	if (p->buf != b)
		return -ENOENT;
	*h = p->handle;
	return 0;
}
static int
drm_prime_add_buf_handle(struct prime_private *p, struct dma_buf *b, uint32_t h)
{
	if (prime_error)
		return prime_error;
	assert(!p->buf);
	p->buf = b;
	p->handle = h;
	get_dma_buf(b);
	return 0;
}
static void
drm_prime_remove_buf_handle_locked(struct prime_private *p, struct dma_buf *b)
{
	if (p->buf == b) {
		p->buf = NULL;
		dma_buf_put(b);
	}
}
static int
dma_resv_lock_interruptible(struct mutex *m, void *ctx)
{
	if (reservation_error)
		return reservation_error;
	mutex_lock(m);
	return 0;
}
static void
dma_resv_unlock(struct mutex *m)
{
	mutex_unlock(m);
}
static int
drm_gem_lock_reservations(struct drm_gem_object **o, unsigned int n, struct ww_acquire_ctx *c)
{
	assert(n == 1);
	return dma_resv_lock_interruptible(o[0]->resv, NULL);
}
static void
drm_gem_unlock_reservations(struct drm_gem_object **o, unsigned int n, struct ww_acquire_ctx *c)
{
	assert(n == 1);
	dma_resv_unlock(o[0]->resv);
}
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
static struct dma_fence *dma_fence_get(struct dma_fence *f) { atomic_fetch_add(&f->refs,1); return f; }
struct dma_resv_list { unsigned shared_count; struct dma_fence **shared; };
#define dma_resv_get_excl(r) ((struct dma_fence *)NULL)
#define dma_resv_get_list(r) ((struct dma_resv_list *)NULL)
static int virtio_gpu_exec_dependency(struct virtio_gpu_device *d, struct dma_fence *f,
    u64 key, unsigned int start, bool implicit) { assert(!"unrelated EXEC path"); return -EINVAL; }
static void virtio_gpu_array_put_free(struct virtio_gpu_object_array *);

static void
virtio_gpu_array_put_free_delayed(struct virtio_gpu_device *d, struct virtio_gpu_object_array *a)
{
	virtio_gpu_array_put_free(a);
}
static void
virtgpu_console_stop(struct virtio_gpu_device *d)
{
}
static void
virtio_gpu_fence_stop(struct virtio_gpu_device *d, int error)
{
	d->fence_drv.stopped = true;
	d->fence_drv.stop_error = error;
}
static void
virtio_gpu_fail_capsets(struct virtio_gpu_device *d, int error)
{
	wake_up_all(&d->resp_wq);
}
static void
queue_work(void *wq, struct work_struct *w)
{
}
static void
mutex_enter(struct mutex *m)
{
	enter();
	reset_entries++;
	announce();
	leave();
	mutex_lock(m);
}
#define mutex_exit(m) mutex_unlock(m)
static void
virtio_reset(void *native)
{
	enter();
	reset_entered = true;
	announce();
	if (reset_hold)
		await(reset_release);
	memset(host_context, 0, sizeof(host_context));
	memset(host_resource, 0, sizeof(host_resource));
	memset(host_backing, 0, sizeof(host_backing));
	memset(attached, 0, sizeof(attached));
	resets++;
	leave();
}
#define BUS_DMA_WAITOK 1
#define BUS_DMA_WRITE 2
#define BUS_DMA_READ 4
#define BUS_DMASYNC_PREREAD 4
#define BUS_DMASYNC_POSTREAD 8
#define BUS_DMASYNC_PREWRITE 1
#define BUS_DMASYNC_POSTWRITE 2
static int virtio_gpu_gem_object_open(struct drm_gem_object *, struct drm_file *);
static void virtio_gpu_gem_object_close(struct drm_gem_object *, struct drm_file *);
static const int drm_gem_shmem_uvm_ops;
static struct page *owned_pages[2];
#define virtio_gpu_gem_funcs object_ops
static const struct object_funcs object_ops = {virtio_gpu_gem_object_open, virtio_gpu_gem_object_close};
static struct drm_gem_shmem_object *
drm_gem_shmem_create(struct drm_device *d, size_t n)
{
	struct virtio_gpu_object *bo = test_alloc(sizeof(*bo));
	if (!bo)
		return ERR_PTR(-ENOMEM);
	bo->base.base.dev = d;
	bo->base.base.size = n;
	bo->base.base.funcs = &object_ops;
	bo->base.base.gemo_uvmobj.pgops = &drm_gem_shmem_uvm_ops;
	bo->base.pages = owned_pages;
	bo->base.base.resv = &bo->base.base.reservation;
	linux_mutex_init(bo->base.base.resv);
	atomic_init(&bo->base.base.refs, 1);
	last_bo = bo;
	return &bo->base;
}
static void
drm_gem_shmem_free_object(struct drm_gem_object *o)
{
	struct virtio_gpu_object *bo = gem_to_virtio_gpu_obj(o);
	assert(!bo->base.pin_count && !bo->base.vmap_count && !bo->pages);
	assert(pthread_mutex_trylock(&o->resv->value) == 0);
	mutex_unlock(o->resv);
	linux_mutex_destroy(o->resv);
	bos_freed++;
	if (last_bo == bo)
		last_bo = NULL;
	test_free(bo);
}
static int
drm_gem_shmem_pin(struct drm_gem_object *o)
{
	if (dma_error == 1)
		return -ENOMEM;
	gem_to_virtio_gpu_obj(o)->base.pin_count++;
	pins++;
	return 0;
}
static void
drm_gem_shmem_unpin(struct drm_gem_object *o)
{
	assert(gem_to_virtio_gpu_obj(o)->base.pin_count && pins);
	gem_to_virtio_gpu_obj(o)->base.pin_count--;
	pins--;
}
static struct sg_table *
drm_gem_shmem_get_sg_table(struct drm_gem_object *o)
{
	if (dma_error == 2)
		return ERR_PTR(-ENOMEM);
	struct sg_table *s = test_alloc(sizeof(*s));
	if (!s)
		return ERR_PTR(-ENOMEM);
	s->nents = 2;
	return s;
}
static int
bus_dmamap_create(int tag, size_t size, unsigned int n, uint32_t maxseg, int boundary, int flags, bus_dmamap_t *out)
{
	if (dma_error == 3)
		return ENOMEM;
	*out = test_alloc(sizeof(**out));
	if (!*out)
		return ENOMEM;
	maps++;
	return 0;
}
static void
bus_dmamap_destroy(int tag, bus_dmamap_t m)
{
	assert(!m->loaded && maps);
	maps--;
	test_free(m);
}
static void *
drm_gem_shmem_vmap(struct drm_gem_object *o)
{
	if (dma_error == 4)
		return ERR_PTR(-ENOMEM);
	void *p = test_alloc(1);
	if (!p)
		return ERR_PTR(-ENOMEM);
	gem_to_virtio_gpu_obj(o)->base.vaddr = p;
	gem_to_virtio_gpu_obj(o)->base.vmap_count++;
	vmaps++;
	return p;
}
static void
drm_gem_shmem_vunmap(struct drm_gem_object *o, void *p)
{
	assert(gem_to_virtio_gpu_obj(o)->base.vmap_count && vmaps);
	gem_to_virtio_gpu_obj(o)->base.vaddr = NULL;
	gem_to_virtio_gpu_obj(o)->base.vmap_count--;
	vmaps--;
	test_free(p);
}
static int
bus_dmamap_load(int tag, bus_dmamap_t m, void *addr, size_t size, void *proc, int flags)
{
	if (dma_error == 5)
		return ENOMEM;
	m->loaded = true;
	m->dm_nsegs = dma_nents;
	for (unsigned int i = 0; i < 2; i++) {
		m->dm_segs[i].ds_addr = 0xabc000 + i * PAGE_SIZE;
		m->dm_segs[i].ds_len = long_segment ? UINT64_C(0x100000000) : PAGE_SIZE;
	}
	return 0;
}
static void
bus_dmamap_unload(int tag, bus_dmamap_t m)
{
#ifdef DMA_LEASE_SOURCE
	if (last_bo && last_bo->pages && last_bo->pages->sgl->sg_dmamap == m) {
		assert(last_bo->dma_lease == VIRTGPU_LEASE_NONE ||
		    last_bo->dma_lease == VIRTGPU_LEASE_CLOSED);
		assert(!last_bo->dma_members && !last_bo->dma_retire_refs);
	}
#endif
	assert(m->loaded);
	m->loaded = false;
}
static unsigned int backing_pre, backing_post, eligibility_checks;
static unsigned int rw_pre, rw_post;
static void (*sync_hook)(int);
#ifdef BACKING_CONTRACT
static void backing_sync_check(void);
#endif
static void
bus_dmamap_sync(int tag, bus_dmamap_t m, size_t start, size_t n, int flags)
{
	assert(m->loaded);
#ifdef BACKING_CONTRACT
	backing_sync_check();
#endif
	if (flags == (BUS_DMASYNC_PREREAD | BUS_DMASYNC_PREWRITE)) {
		rw_pre++;
		if (sync_hook) sync_hook(flags);
		return;
	}
	if (flags == (BUS_DMASYNC_POSTREAD | BUS_DMASYNC_POSTWRITE)) {
		rw_post++;
		if (sync_hook) sync_hook(flags);
		return;
	}
	assert(flags == BUS_DMASYNC_PREWRITE || flags == BUS_DMASYNC_POSTWRITE);
	if (flags == BUS_DMASYNC_PREWRITE)
		backing_pre++;
	else
		backing_post++;
}
static void
sg_free_table(struct sg_table *s)
{
	if (s->sgl->sg_dmamap)
		bus_dmamap_destroy(s->sgl->sg_dmat, s->sgl->sg_dmamap);
}

static int eligibility_error;
#ifdef DMA_ELIGIBILITY_SOURCE
/* Native MD arithmetic is exercised separately with the actual predicate. */
static int
virtio_gpu_dma_eligible(int tag, bus_dmamap_t map, void *kva, size_t size,
    struct page **pages, unsigned int npages, unsigned int capacity)
{
	assert(map->loaded && kva && pages && npages == size / PAGE_SIZE);
	assert(capacity == 2);
	eligibility_checks++;
	return eligibility_error;
}
static int virtio_gpu_object_dma_check(struct virtio_gpu_device *,
    struct drm_gem_object *, bus_dmamap_t, unsigned int);
#endif
static void virtio_gpu_stop(struct virtio_gpu_device *, int);
#ifdef DMA_LEASE_SOURCE
static void virtio_gpu_finalize_object(struct virtio_gpu_object *);
static void virtio_gpu_resource_id_put(struct virtio_gpu_device *, uint32_t);
#endif
#if defined(DMA_LEASE_SOURCE) || defined(BACKING_CONTRACT)
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *d,
    struct virtio_gpu_vbuffer *b) { }
#endif
/* Fence publication itself is exercised by the completion contract. */
static void virtio_gpu_fence_complete(struct virtio_gpu_fence *f, int error) {
#ifdef BACKING_CONTRACT
	assert(rw_pre >= rw_post && rw_pre - rw_post <= 1);
	if (f->unref && !error) assert(ids_freed == f->prior_ids + 1);
#endif
}
static void virtio_gpu_cancel_vbuf(void *);
static void virtio_gpu_release_object(struct virtio_gpu_object *);
static struct virtio_gpu_object_array *virtio_gpu_array_alloc(u32);
static void virtio_gpu_array_add_obj(struct virtio_gpu_object_array *, struct drm_gem_object *);
static int virtio_gpu_array_lock_resv(struct virtio_gpu_object_array *);
static void virtio_gpu_array_unlock_resv(struct virtio_gpu_object_array *);
static int virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *, struct virtio_gpu_vbuffer *, struct virtio_gpu_ctrl_hdr *, struct virtio_gpu_fence *);
/* Compile the native core branches, after all host system headers. */
#ifndef __NetBSD__
#define __NetBSD__ 1
#endif
#include "resource-production.h"
#ifdef BACKING_CONTRACT
static void backing_unlock(struct mutex *);
#endif
static void
test_mutex_unlock(struct mutex *m)
{
	assert(pthread_mutex_unlock(&m->value) == 0);
#ifdef BACKING_CONTRACT
	backing_unlock(m);
#endif
}

enum response_mode {
	GOOD, BAD_TYPE, SHORT_REPLY, LONG_REPLY, NO_FENCE, WRONG_FENCE, HOLD, SUBMIT_FAIL
};
static enum response_mode mode;
static uint32_t fault_type;
static int submission_error;
static uint64_t next_fence;
static struct virtio_gpu_vbuffer *pending;
static void
host_accept(struct virtio_gpu_ctrl_hdr *c)
{

	if (c->type == VIRTIO_GPU_CMD_CTX_CREATE) {
		assert(c->ctx_id > 0 && c->ctx_id <= 8);
		host_context[c->ctx_id - 1] = true;
		return;
	}

	if (c->type == VIRTIO_GPU_CMD_RESOURCE_CREATE_2D || c->type == VIRTIO_GPU_CMD_RESOURCE_CREATE_3D) {

		uint32_t id = ((struct virtio_gpu_resource_create_2d *)c)->resource_id;
		unsigned int s = resource_slot(id);
		assert(!host_resource[s]);
		host_resource[s] = true;
		create_commands++;

		if (c->type == VIRTIO_GPU_CMD_RESOURCE_CREATE_3D) {
			struct virtio_gpu_resource_create_3d *p = (void *)c;
			assert(p->format == 2 && p->width == 32 && p->height == 32 && p->target == 2 && p->bind == 3 && p->depth == 4 && p->array_size == 5 && p->last_level == 6 && p->nr_samples == 7 && p->flags == 8);
		}
		return;
	}

	if (c->type == VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE) {
		struct virtio_gpu_ctx_resource *p = (void *)c;
		unsigned int s = resource_slot(p->resource_id);
		assert(host_resource[s] && host_context[c->ctx_id - 1] && !attached[c->ctx_id - 1][s]);
		attached[c->ctx_id - 1][s] = true;
		attach_commands++;
	}
	if (c->type == VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE)
		detach_commands++;

	if (c->type == VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING) {
		struct virtio_gpu_resource_attach_backing *p = (void *)c;
		assert(host_resource[resource_slot(p->resource_id)]);
		host_backing[resource_slot(p->resource_id)] = true;
		backing_commands++;
	}
}
#ifdef BACKING_CONTRACT
static void backing_dequeue(struct virtio_gpu_vbuffer *);
#endif
static void
complete(struct virtio_gpu_vbuffer *b, enum response_mode m)
{
	struct virtio_gpu_device *d = b->vgdev;
	struct virtio_gpu_ctrl_hdr *c = (void *)b->buf, *r = (void *)b->resp_buf;
	r->type = VIRTIO_GPU_RESP_OK_NODATA;
	r->flags = VIRTIO_GPU_FLAG_FENCE;
	r->fence_id = c->fence_id;
	b->resp_received = b->resp_size;
	if (m == BAD_TYPE)
		r->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
	if (m == SHORT_REPLY)
		b->resp_received--;
	if (m == LONG_REPLY)
		b->resp_received++;
	if (m == NO_FENCE)
		r->flags = 0;
	if (m == WRONG_FENCE)
		r->fence_id++;
	int error = virtio_gpu_response_error(b);
#ifndef BACKING_CONTRACT
	if (error) virtio_gpu_stop(d, error);
#else
	(void)d;
#endif
	if (!error) {
		enter();

		if (c->type == VIRTIO_GPU_CMD_CTX_DESTROY) {
			host_context[c->ctx_id - 1] = false;
			memset(attached[c->ctx_id - 1], 0, sizeof(attached[0]));
		}

		if (c->type == VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE) {
			struct virtio_gpu_ctx_resource *p = (void *)c;
			attached[c->ctx_id - 1][resource_slot(p->resource_id)] = false;
		}

		if (c->type == VIRTIO_GPU_CMD_RESOURCE_UNREF) {
			struct virtio_gpu_resource_unref *p = (void *)c;
			unsigned int s = resource_slot(p->resource_id);
			host_resource[s] = host_backing[s] = false;
			for (unsigned int i = 0; i < 16; i++)
				assert(!attached[i][s]);
			unrefs++;
		}
		leave();
	}
#ifdef BACKING_CONTRACT
	backing_dequeue(b);
#else
	virtio_gpu_finish_vbuf(b, error);
#endif
}
#ifdef BACKING_CONTRACT
#include "virtgpu-backing-queue.h"
#else
static int
virtio_gpu_queue_fenced_ctrl_buffer(struct virtio_gpu_device *d, struct virtio_gpu_vbuffer *b, struct virtio_gpu_ctrl_hdr *c, struct virtio_gpu_fence *f)
{
	assert(f && c);
	c->flags = VIRTIO_GPU_FLAG_FENCE;
	b->fence = f;
	atomic_fetch_add(&f->f.refs, 1);
	if (b->objs)
		virtio_gpu_array_unlock_resv(b->objs);
#ifdef DMA_LEASE_SOURCE
	int dma_ret = d->vqs_ready ? virtio_gpu_dma_prepare(b) : -ENODEV;
#else
	int dma_ret = 0;
#endif
	enter();
	c->fence_id = ++next_fence;
	enum response_mode m = c->type == fault_type ? mode : GOOD;
	int error = dma_ret ? dma_ret : !d->vqs_ready ? -ENODEV : m == SUBMIT_FAIL ? submission_error : 0;

	if (!error) {


		if (c->type == VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING) {
			struct virtio_gpu_resource_attach_backing *p = (void *)c;
			struct virtio_gpu_mem_entry *e = b->data_buf;
			assert(p->nr_entries == 2 && b->data_size == 2 * sizeof(*e));
			for (unsigned int i = 0; i < 2; i++)
				assert(e[i].addr == UINT64_C(0xabc000) + i * PAGE_SIZE && e[i].length == PAGE_SIZE);
		}
		host_accept(c);

		if (m == HOLD) {
			assert(!pending);
			pending = b;
			announce();
		}
	}
	leave();

	if (error) {
		if (b->release)
			virtio_gpu_stop(d, error);
		virtio_gpu_cancel_vbuf(b);
		return error;
	}
	if (m != HOLD)
		complete(b, m);
	return 0;
}
#endif
static struct config_ops ops = {.reset = linux_virtio_reset};
static void
wait_init(wait_queue_head_t *q)
{
	linux_mutex_init(&q->lock);
	assert(pthread_cond_init(&q->cv, NULL) == 0);
}
static void
wait_fini(wait_queue_head_t *q)
{
	assert(pthread_cond_destroy(&q->cv) == 0);
	linux_mutex_destroy(&q->lock);
}
static void
init(void)
{
	assert(!atomic_load(&allocations));
	memset(&gpu, 0, sizeof(gpu));
	memset(&transport, 0, sizeof(transport));
	memset(&dev, 0, sizeof(dev));
	linux_mutex_init(&gpu.fence_drv.lock);
	linux_mutex_init(&gpu.submit_lock);
	linux_mutex_init(&gpu.ctrlq.qlock);
	linux_mutex_init(&gpu.cursorq.qlock);
	linux_mutex_init(&gpu.dma_lock);
	INIT_LIST_HEAD(&gpu.dma_leases);
	gpu.vdev = &transport;
	gpu.ddev = &dev;
	gpu.vqs_ready = true;
	gpu.has_virgl_3d = true;
	gpu.submit_error = -EPERM;
	dev.dev_private = &gpu;
	dev.driver = &driver;
	linux_mutex_init(&dev.object_name_lock);
	transport.config = &ops;
	transport.state = 1;
	transport.nvqs = 2;
	transport.dmat = 1;
	transport.max_request = 1024 * 1024;
	linux_mutex_init(&transport.lock);
	wait_init(&gpu.resp_wq);
	wait_init(&gpu.ctrlq.ack_queue);
	wait_init(&gpu.cursorq.ack_queue);
	memset(host_context, 0, sizeof(host_context));
	memset(host_resource, 0, sizeof(host_resource));
	memset(host_backing, 0, sizeof(host_backing));
	memset(attached, 0, sizeof(attached));
	memset(&dmabuf, 0, sizeof(dmabuf));
	allocation_fail_at = handle_error = vma_error = prime_error = reservation_error = dma_error = 0;
	resource_id_error = 0;
	eligibility_error = 0;
	backing_pre = backing_post = eligibility_checks = rw_pre = rw_post = 0;
	last_bo = NULL;
	sync_hook = NULL;
	atomic_store(&allocation_calls, 0);
	dma_nents = 2;
	long_segment = high_id = reset_hold = reset_entered = reset_release = false;
	resets = reset_entries = ids_freed = bos_freed = pins = maps = vmaps = unrefs = 0;
	attach_commands = detach_commands = create_commands = backing_commands = 0;
	fault_type = 0;
	mode = GOOD;
	pending = NULL;
	wait_msec = 2000;
	watched_lock = NULL;
	watched_entries = 0;
}
static void
fini(void)
{
#ifdef DMA_LEASE_SOURCE
	if (!gpu.vqs_ready)
		virtio_gpu_dma_reset(&gpu);
	assert(list_empty(&gpu.dma_leases));
#endif
	linux_mutex_destroy(&gpu.fence_drv.lock);
	linux_mutex_destroy(&gpu.submit_lock);
	linux_mutex_destroy(&gpu.ctrlq.qlock);
	linux_mutex_destroy(&gpu.cursorq.qlock);
	linux_mutex_destroy(&gpu.dma_lock);
	assert(!pending && !atomic_load(&allocations) && !pins && !maps && !vmaps);

	for (unsigned int i = 0; i < 16; i++) {
		assert(!gpu.ctx_id_ida.used[i] && !gpu.resource_ida.used[i] && !host_context[i] && !host_resource[i] && !host_backing[i]);
		for (unsigned int j = 0; j < 16; j++)
			assert(!attached[i][j]);
	}
	assert(!gpu.resource_ida.high);
	wait_fini(&gpu.resp_wq);
	wait_fini(&gpu.ctrlq.ack_queue);
	wait_fini(&gpu.cursorq.ack_queue);
	linux_mutex_destroy(&dev.object_name_lock);
	linux_mutex_destroy(&transport.lock);
}
static void
file_init(struct drm_file *f)
{
	memset(f, 0, sizeof(*f));
	linux_mutex_init(&f->table_lock);
	linux_mutex_init(&f->prime.lock);
	assert(virtio_gpu_driver_open(&dev, f) == 0);
}
static void
file_fini(struct drm_file *f)
{
	drm_gem_release(&dev, f);
	assert(!f->vmas && !f->prime.buf);
	virtio_gpu_driver_postclose(&dev, f);
	linux_mutex_destroy(&f->prime.lock);
}
static struct virtio_gpu_object_params
params(bool virgl)
{
	struct virtio_gpu_object_params p = {.format = 2,.width = 32,.height = 32,.size = 8192,.virgl = virgl,.target = 2,.bind = 3,.depth = 4,.array_size = 5,.last_level = 6,.nr_samples = 7,.flags = 8};
	return p;
}
static struct virtio_gpu_object *
make_bo(bool virgl)
{
	struct virtio_gpu_object *bo = NULL;
	struct virtio_gpu_object_params p = params(virgl);
	assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == 0 && bo && bo->created);
	return bo;
}
static unsigned int
refs(struct virtio_gpu_object *bo)
{
	return atomic_load(&bo->base.base.refs);
}
static void
fail_next(unsigned int n)
{
	allocation_fail_at = atomic_load(&allocation_calls) + n;
}
static void
set_fault(uint32_t type, enum response_mode m)
{
	enter();
	fault_type = type;
	mode = m;
	leave();
}
static void
wait_pending(void)
{
	enter();
	await(pending);
	leave();
}
static void
finish_pending(bool cancel)
{
	enter();
	struct virtio_gpu_vbuffer *b = pending;
	assert(b);
	pending = NULL;
	mode = GOOD;
	leave();
	if (cancel)
		virtio_gpu_cancel_vbuf(b);
	else
		complete(b, GOOD);
}
static void
duplicate_tests(void)
{
	struct drm_file f, g;
	uint32_t h1, h2;
	struct drm_gem_open open = {0};
	init();
	file_init(&f);
	file_init(&g);
	struct virtio_gpu_object *bo = make_bo(true);
	struct drm_gem_object *o = &bo->base.base;
	assert(drm_gem_handle_create(&f, o, &h1) == 0);
	mutex_lock(&dev.object_name_lock);
	o->name = idr_alloc(&dev.object_name_idr, o, 1, 0, 0);
	mutex_unlock(&dev.object_name_lock);
	open.name = o->name;
	assert(drm_gem_open_ioctl(&dev, &open, &f) == 0);
	h2 = open.handle;
	assert(f.driver_priv->attachment_count==1);
	assert(h1 != h2 && attach_commands == 1 && o->handle_count == 2 && f.vmas == 2);
	assert(drm_gem_handle_delete(&f, h1) == 0 && detach_commands == 0 && attached[0][0]);
	assert(f.driver_priv->attachment_count==1);
	assert(drm_gem_open_ioctl(&dev, &open, &g) == 0 && attach_commands == 2 && attached[1][0]);
	file_fini(&f);
	assert(detach_commands == 1 && attached[1][0] && refs(bo) == 3);
	file_fini(&g);
	assert(detach_commands == 2 && refs(bo) == 1);
	drm_gem_object_put_unlocked(o);
	assert(ids_freed == 1 && unrefs == 1);
	fini();
	puts("PASS actual GEM_OPEN duplicate handles, per-file attachments and full file release");
}
static void
core_unwind_tests(void)
{

	for (unsigned int n = 1; n <= 5; n++) {

		struct drm_file f;
		uint32_t h = 0xdead;
		init();
		file_init(&f);
		struct virtio_gpu_object *bo = make_bo(true);
		fail_next(n);
		assert(drm_gem_handle_create(&f, &bo->base.base, &h) == -ENOMEM);
		assert(h == 0xdead && bo->base.base.handle_count == 0 && f.vmas == 0 && refs(bo) == 1 && list_empty(&f.driver_priv->attachments));
		assert(resets == (n >= 3 ? 1u : 0u));
		allocation_fail_at = 0;
		file_fini(&f);
		drm_gem_object_put_unlocked(&bo->base.base);
		fini();
	}

	for (unsigned int n = 1; n <= 4; n++) {

		struct drm_file f;
		uint32_t h;
		init();
		file_init(&f);
		struct virtio_gpu_object *bo = make_bo(true);
		assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
		fail_next(n);
		assert(drm_gem_handle_delete(&f, h) == 0 && resets == 1 && refs(bo) == 1 && f.vmas == 0);
		allocation_fail_at = 0;
		file_fini(&f);
		drm_gem_object_put_unlocked(&bo->base.base);
		fini();
	}

	for (unsigned int stage = 0; stage < 3; stage++) {

		struct drm_file f;
		uint32_t h = 0xdead;
		init();
		file_init(&f);
		struct virtio_gpu_object *bo = make_bo(true);
		if (stage == 0)
			handle_error = -ENOSPC;
		else if (stage == 1)
			vma_error = -ENOMEM;
		else
			reservation_error = -EDEADLK;
		assert(drm_gem_handle_create(&f, &bo->base.base, &h) == (stage == 0 ? -ENOSPC : stage == 1 ? -ENOMEM : -EDEADLK));
		assert(h == 0xdead && refs(bo) == 1 && f.vmas == 0 && !bo->base.base.handle_count);
		handle_error = vma_error = reservation_error = 0;
		file_fini(&f);
		drm_gem_object_put_unlocked(&bo->base.base);
		fini();
	}
	struct drm_file f;
	uint32_t h, h2 = 0;
	init();
	file_init(&f);
	struct virtio_gpu_object *bo = make_bo(true);
	assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
	dmabuf.obj = &bo->base.base;
	prime_error = -ENOMEM;
	assert(drm_gem_prime_fd_to_handle(&dev, &f, 7, &h2) == -ENOMEM);
	assert(h2 != h && bo->base.base.handle_count == 1 && f.vmas == 1 && attach_commands == 1 && detach_commands == 0 && refs(bo) == 3);
	file_fini(&f);
	drm_gem_object_put_unlocked(&bo->base.base);
	fini();
	puts("PASS actual core callback unwind, PRIME post-create failure, allocation and reservation errors");
}
static void
attachment_error_tests(void)
{

	for (unsigned int close = 0; close <= 1; close++)
		for (enum response_mode m = BAD_TYPE; m <= SUBMIT_FAIL; m++) {

			struct drm_file f;
			uint32_t h = 0xdead;
			init();
			file_init(&f);
			struct virtio_gpu_object *bo = make_bo(true);
			if (close)
				assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
			set_fault(close ? VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE : VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, m);
			submission_error = -ENOSPC;
			if (m == HOLD)
				wait_msec = 40;
			if (close)
				assert(drm_gem_handle_delete(&f, h) == 0);
			else
				assert(drm_gem_handle_create(&f, &bo->base.base, &h) == (m == SUBMIT_FAIL ? -ENOSPC : m == HOLD ? -ETIMEDOUT : -EIO));
			assert(resets == 1 && list_empty(&f.driver_priv->attachments) && f.vmas == 0);

			if (m == HOLD) {
				assert(refs(bo) == 2);
				finish_pending(close);
			}
			assert(refs(bo) == 1);
			file_fini(&f);
			drm_gem_object_put_unlocked(&bo->base.base);
			fini();
		}
	puts("PASS ATTACH/DETACH malformed/fence replies, submit errors, timeout and late cookies");
}
struct operation {
	struct drm_file *file;
	struct virtio_gpu_object *bo;
	bool close, create, virgl;
	uint32_t handle;
	int ret;
};
static void *
operate(void *arg)
{
	struct operation *op = arg;
	if (op->create) {
		struct virtio_gpu_object_params p = params(op->virgl);
		op->ret = virtio_gpu_object_create(&gpu, &p, &op->bo, NULL);
	} else if (op->close)
		op->ret = drm_gem_handle_delete(op->file, op->handle);
	else
		op->ret = drm_gem_handle_create(op->file, &op->bo->base.base, &op->handle);
	return NULL;
}
static void
concurrency_tests(void)
{

	for (unsigned int close = 0; close <= 1; close++) {

		struct drm_file f;
		pthread_t t1, t2;
		init();
		file_init(&f);
		struct virtio_gpu_object *bo = make_bo(true);
		struct operation a = {.file = &f,.bo = bo,.close = close}, b = {.file = &f,.bo = bo};
		if (close)
			assert(drm_gem_handle_create(&f, &bo->base.base, &a.handle) == 0);
		set_fault(close ? VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE : VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, HOLD);
		watched_lock = &f.driver_priv->attachment_lock;
		assert(pthread_create(&t1, NULL, operate, &a) == 0);
		wait_pending();
		assert(refs(bo) == 4 && ids_freed == 0);
		if (!close)
			assert(list_empty(&f.driver_priv->attachments));
		assert(pthread_create(&t2, NULL, operate, &b) == 0);
		enter();
		await(watched_entries == 2);
		leave();
		finish_pending(false);
		assert(pthread_join(t1, NULL) == 0 && pthread_join(t2, NULL) == 0);
		assert(a.ret == 0 && b.ret == 0 && a.handle != b.handle);
		assert(attach_commands == (close ? 2u : 1u) && detach_commands == (close ? 1u : 0u));
		file_fini(&f);
		drm_gem_object_put_unlocked(&bo->base.base);
		fini();
	}
	puts("PASS concurrent first open and last-close/new-open retain attachment references");
}
static void *
stopper(void *arg)
{
	virtio_gpu_stop(&gpu, -EIO);
	return NULL;
}
static void
reset_overlap_tests(void)
{
	const uint32_t types[] = {VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE, VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE, VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, VIRTIO_GPU_CMD_RESOURCE_CREATE_3D, VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING};

	for (unsigned int i = 0; i < sizeof(types) / sizeof(types[0]); i++) {

		struct drm_file f;
		pthread_t worker, resetter;
		init();
		file_init(&f);
		struct operation op = {.file = &f,.close = i == 1,.create = i >= 2,.virgl = i != 2};
		struct virtio_gpu_object *existing = NULL;

		if (i < 2) {
			existing = op.bo = make_bo(true);
			if (i == 1)
				assert(drm_gem_handle_create(&f, &existing->base.base, &op.handle) == 0);
		}
		set_fault(types[i], HOLD);
		reset_hold = true;
		assert(pthread_create(&worker, NULL, operate, &op) == 0);
		wait_pending();
		assert(pthread_create(&resetter, NULL, stopper, NULL) == 0);
		enter();
		await(reset_entered && reset_entries >= (i == 4 ? 1u : 2u));
		assert(!gpu.vqs_ready && !resets && !ids_freed && !bos_freed && host_resource[0]);
		if (i == 4)
			assert(pins == 1 && maps == 1 && vmaps == 1 && host_backing[0]);
		reset_release = true;
		announce();
		leave();
		assert(pthread_join(resetter, NULL) == 0 && pthread_join(worker, NULL) == 0);
		assert(resets == 1 && op.ret == (i == 1 ? 0 : -ENODEV));
		if (i >= 2)
			assert(!op.bo && ids_freed == (i == 4 ? 0u : 1u));
		finish_pending(true);
		file_fini(&f);
		if (existing)
			drm_gem_object_put_unlocked(&existing->base.base);
		fini();
	}
	puts("PASS actual native reset interlock before attachment/2D/3D/backing retirement");
}
static void
creation_tests(void)
{
	/* Allocation and reservation failures before any host ownership. */
	for (unsigned int stage = 0; stage < 3; stage++) {
		struct virtio_gpu_object *bo = NULL;
		struct virtio_gpu_object_params p = params(true);
		struct virtio_gpu_fence *f;

		init();
		f = virtio_gpu_fence_alloc(&gpu);
		if (stage < 2)
			resource_id_error = stage ? -ENOSPC : -ENOMEM;
		else
			reservation_error = -EDEADLK;
		assert(virtio_gpu_object_create(&gpu, &p, &bo, f) ==
		    (stage == 0 ? -ENOMEM : stage == 1 ? -ENOSPC : -EDEADLK));
		dma_fence_put(&f->f);
		assert(!bo && !create_commands && !resets && bos_freed == 1);
		assert(ids_freed == (stage == 2 ? 1u : 0u));
		fini();
	}
	/* Host acceptance alone must not publish created or the BO output. */
	for (unsigned int virgl = 0; virgl <= 1; virgl++) {
		struct operation op = { .create = true, .virgl = virgl };
		pthread_t worker;

		init();
		set_fault(virgl ? VIRTIO_GPU_CMD_RESOURCE_CREATE_3D :
		    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, HOLD);
		assert(pthread_create(&worker, NULL, operate, &op) == 0);
		wait_pending();
		assert(last_bo && !last_bo->created && !op.bo && !backing_commands);
		finish_pending(false);
		assert(pthread_join(worker, NULL) == 0);
		assert(op.ret == 0 && op.bo && op.bo->created);
		drm_gem_object_put_unlocked(&op.bo->base.base);
		fini();
	}

	for (unsigned int virgl = 0; virgl <= 1; virgl++)
		for (unsigned int supplied = 0; supplied <= 1; supplied++)
			for (unsigned int n = 1; n <= 11; n++) {

				init();
				struct virtio_gpu_object *bo = (void *)1;
				struct virtio_gpu_object_params p = params(virgl);
				struct virtio_gpu_fence *f = supplied ? virtio_gpu_fence_alloc(&gpu) : NULL;
				fail_next(n);
				assert(virtio_gpu_object_create(&gpu, &p, &bo, f) == -ENOMEM && !bo);
				if (f)
					dma_fence_put(&f->f);
				assert(bos_freed == (n == 1 ? 0u : 1u) && ids_freed == (n == 1 ? 0u : 1u));
				fini();
			}

	for (unsigned int virgl = 0; virgl <= 1; virgl++)
		for (enum response_mode m = BAD_TYPE; m <= SUBMIT_FAIL; m++) {

			init();
			struct virtio_gpu_object *bo = NULL;
			struct virtio_gpu_object_params p = params(virgl);
			set_fault(virgl ? VIRTIO_GPU_CMD_RESOURCE_CREATE_3D : VIRTIO_GPU_CMD_RESOURCE_CREATE_2D, m);
			submission_error = -ENOSPC;
			if (m == HOLD)
				wait_msec = 40;
			assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == (m == SUBMIT_FAIL ? -ENOSPC : m == HOLD ? -ETIMEDOUT : -EIO));
			assert(!bo && !backing_commands && ids_freed == 1 && resets == 1);
			if (m == HOLD)
				finish_pending(false);
			fini();
		}
	init();
	struct drm_file file;
	file_init(&file);
	struct drm_virtgpu_resource_create rc = {.format = 2,.width = 32,.height = 32,.size = 8192,.target = 2,.bind = 3,.depth = 4,.array_size = 5,.last_level = 6,.nr_samples = 7,.flags = 8,.bo_handle = 0xdead,.res_handle = 0xbeef};
	set_fault(VIRTIO_GPU_CMD_RESOURCE_CREATE_3D, SUBMIT_FAIL);
	submission_error = -ENOMEM;
	assert(virtio_gpu_resource_create_ioctl(&dev, &rc, &file) == -ENOMEM && rc.bo_handle == 0xdead && rc.res_handle == 0xbeef && !backing_commands && !attach_commands);
	file_fini(&file);
	fini();
	puts("PASS 2D/3D creation allocations, supplied-fence unwind, wire fields and ioctl error publication");
}
static void
backing_tests(void)
{

	for (int stage = 1; stage <= 5; stage++) {

		init();
		dma_error = stage;
		struct virtio_gpu_object *bo = NULL;
		struct virtio_gpu_object_params p = params(true);
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ENOMEM && !bo && ids_freed == 1 && unrefs == 1);
		fini();
	}

	for (enum response_mode m = BAD_TYPE; m <= SUBMIT_FAIL; m++) {

		init();
		struct virtio_gpu_object *bo = NULL;
		struct virtio_gpu_object_params p = params(true);
		set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, m);
		submission_error = -ENOMEM;
		if (m == HOLD)
			wait_msec = 40;
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == (m == SUBMIT_FAIL ? -ENOMEM : m == HOLD ? -ETIMEDOUT : -EIO));
		assert(!bo);
		if (m == HOLD)
			finish_pending(false);
#ifdef DMA_LEASE_SOURCE
		if (!gpu.vqs_ready) virtio_gpu_dma_reset(&gpu);
#endif
		assert(ids_freed == 1);
		fini();
	}
	init();
	struct virtio_gpu_object *bo = NULL;
	struct virtio_gpu_object_params p = params(true);
	fail_next(8);
	set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF, HOLD);
	assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ENOMEM && !bo);
	assert(pending && !ids_freed && !bos_freed && !pins && !maps && !vmaps && host_resource[0]);
	assert(!backing_pre && !backing_post && !backing_commands);
	finish_pending(false);
	assert(ids_freed == 1 && bos_freed == 1);
	fini();
	puts("PASS backing pin/SG/map/vmap/entries errors and retained DMA ownership until UNREF ACK");
}
static void
boundary_tests(void)
{
	init();
	high_id = true;
	struct virtio_gpu_object *bo = make_bo(true);
	assert(bo->hw_res_handle == UINT32_C(0x80000000));
	drm_gem_object_put_unlocked(&bo->base.base);
	assert(ids_freed == 1);
	fini();

	for (unsigned int test = 0; test < 6; test++) {

		init();
		struct virtio_gpu_object *obj = NULL;
		struct virtio_gpu_object_params p = params(true);
		if (test == 0)
			transport.max_request = 119;
		if (test == 1)
			transport.max_request = 151;
		if (test == 2)
			dma_nents = 0;

		if (test == 3) {
			dma_nents = UINT_MAX;
			transport.max_request = SIZE_MAX;
		}
		if (test == 4)
			long_segment = true;
		if (test == 5)
			transport.max_request = 152;
		assert(virtio_gpu_object_create(&gpu, &p, &obj, NULL) == (test == 5 ? 0 : -EMSGSIZE));
		if (obj)
			drm_gem_object_put_unlocked(&obj->base.base);
		assert(ids_freed == 1);
		fini();
	}
	init();
	struct drm_file f;
	file_init(&f);
	bo = make_bo(true);
	uint32_t h, h2 = 0xdead;
	assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
	struct virtio_gpu_attachment *entry = container_of(f.driver_priv->attachments.next, struct virtio_gpu_attachment, node);
	entry->handles = UINT_MAX;
	assert(drm_gem_handle_create(&f, &bo->base.base, &h2) == -EOVERFLOW && h2 == 0xdead && attach_commands == 1 && f.vmas == 1);
	entry->handles = 1;
	/* The unique limit rejects only new members, never duplicate handles. */
	f.driver_priv->attachment_count=VIRTGPU_EXEC_MAX_OBJECTS;
	assert(drm_gem_handle_create(&f,&bo->base.base,&h2)==0);
	assert(drm_gem_handle_delete(&f,h2)==0 && detach_commands==0);
	struct virtio_gpu_object *other=make_bo(true);
	assert(drm_gem_handle_create(&f,&other->base.base,&h2)==-ENOMEM);
	assert(attach_commands==1 && !other->base.base.handle_count);
	drm_gem_object_put_unlocked(&other->base.base);
	f.driver_priv->attachment_count=1;
	f.driver_priv->closing=true;
	assert(drm_gem_handle_create(&f,&bo->base.base,&h2)==-ENODEV);
	f.driver_priv->closing=false;
	file_fini(&f);

	drm_gem_object_put_unlocked(&bo->base.base);
	fini();
	init();
	gpu.has_virgl_3d = false;
	file_init(&f);
	bo = make_bo(false);
	assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
	file_fini(&f);
	assert(!attach_commands && !detach_commands);
	drm_gem_object_put_unlocked(&bo->base.base);
	fini();
	puts("PASS resource ID/count/request arithmetic, exact releases and disabled VIRGL");
}
int
#if defined(DMA_ELIGIBILITY_CONTRACT) || defined(BACKING_CONTRACT)
resource_contract_main(void)
#else
main(void)
#endif
{
	duplicate_tests();
	core_unwind_tests();
	attachment_error_tests();
	concurrency_tests();
	reset_overlap_tests();
	creation_tests();
	backing_tests();
	boundary_tests();
	return 0;
}

#ifdef DMA_ELIGIBILITY_CONTRACT
#include "virtgpu-dma-integration-cases.h"
#endif

#ifdef BACKING_CONTRACT
#include "virtgpu-backing-cases.h"
#endif
