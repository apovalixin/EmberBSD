/* Origin: EmberBSD; AI-assisted production completion/reset cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static void (*sealed_unlock_hook)(void);
static unsigned int native_resets, cleanup_queued, drains, joins, transfers;
static unsigned int wait_mode, wait_count, stop_on_unlock, stop_in_transport;
static struct virtio_gpu_vbuffer *cookies[16], *used[16];
static unsigned int used_len[16], used_count;
static struct virtio_gpu_fence *held[32];
static unsigned int held_count;
static void virtio_gpu_dequeue_ctrl_func(struct work_struct *);

static unsigned int
owned(void)
{
	unsigned int n=0;
	for(unsigned int i=0;i<16;i++) n+=cookies[i]!=NULL;
	return n;
}

static void
completion_unlock(struct mutex *m)
{
	if(m==&device.submit_lock && sealed_unlock_hook) {
		void (*hook)(void)=sealed_unlock_hook; sealed_unlock_hook=NULL; hook();
	}
	if(m==&device.submit_lock && stop_on_unlock) {
		stop_on_unlock=0;
#ifdef COMPLETION_FOUNDATION
		assert(device.submitters==1);
#endif
		virtio_gpu_stop(&device,-ENODEV);
	}
}

static void virtgpu_console_stop(struct virtio_gpu_device *d) { }
static void virtgpu_console_drain(struct virtio_gpu_device *d) {
	/* A console waiter cannot retire until the older timeline gap closes. */
	assert(!owned() && list_empty(&d->fence_drv.fences));
	drains++;
}
static void virtio_gpu_fail_capsets(struct virtio_gpu_device *d, int e) { assert(e<0); }
static void wake_up_all(wait_queue_head_t *q) { assert(!device.fence_drv.lock.held); }
static void wake_up(wait_queue_head_t *q) { wake_up_all(q); }
static void queue_work(void *wq, struct work_struct *w) { cleanup_queued++; }
static void flush_work(struct work_struct *w) {
	assert(!device.submit_lock.held && !device.fence_drv.lock.held);
	if(w==&device.ctrlq.dequeue_work || w==&device.cursorq.dequeue_work) {
#ifdef COMPLETION_FOUNDATION
		assert(!device.submitters);
#endif
		joins++;
	} else {
		/* Config/system workers may also be waiting on a fence. */
		assert(!owned() && list_empty(&device.fence_drv.fences));
	}
}
static void native_reset(struct virtio_device *d) {
#ifdef FENCE_CONTRACT
	assert(!device.submit_lock.held && !device.dma_lock.held && !device.fence_drv.lock.held);
	assert(!resv[0].locked && !resv[1].locked);
#endif
	native_resets++;
}
static void native_del_vqs(struct virtio_device *d) {
	assert(native_resets && joins>=2);
	for(unsigned i=0;i<16;i++) {
		struct virtio_gpu_vbuffer *b=cookies[i];
		if(!b) continue;
		cookies[i]=NULL;
		virtio_gpu_cancel_vbuf(b);
#ifdef COMPLETION_FOUNDATION
		/* Cancellation records readiness; the global drain gate holds it. */
		assert(!device.fence_drv.drained);
#endif
	}
}
static void virtqueue_disable_cb(struct netbsd_virtqueue *q) { }
static bool virtqueue_enable_cb(struct netbsd_virtqueue *q) { return true; }
static void *virtqueue_get_buf(struct netbsd_virtqueue *q,unsigned *len) {
	struct virtio_gpu_vbuffer *b;
	if(!used_count) return NULL;
	b=used[0]; *len=used_len[0];
	used_count--;
	memmove(used,used+1,used_count*sizeof(*used));
	memmove(used_len,used_len+1,used_count*sizeof(*used_len));
	return b;
}
static void virtio_gpu_complete_transfer(struct virtio_gpu_device *d,struct virtio_gpu_vbuffer *b) {
	/* A's ordering seam only: no backing PRE/POST implementation is claimed. */
	assert(!b->fence || !b->fence->f.signaled);
	transfers++;
}
#include "completion-production.h"

static void
respond(unsigned index, int error)
{
	struct virtio_gpu_vbuffer *b=cookies[index];
	struct virtio_gpu_ctrl_hdr *resp;

	assert(b && used_count<16);
	cookies[index]=NULL;
	resp=(void *)b->resp_buf;
	*resp=*(struct virtio_gpu_ctrl_hdr *)(void *)b->buf;
	resp->type=error?VIRTIO_GPU_RESP_ERR_UNSPEC:VIRTIO_GPU_RESP_OK_NODATA;
	used[used_count]=b;
	used_len[used_count++]=error==-EINVAL ? sizeof(*resp)-1 : sizeof(*resp);
	virtio_gpu_dequeue_ctrl_func(&device.ctrlq.dequeue_work);
}

static long
completion_wait(long ticks)
{
	assert(ticks>0 && ticks<=5*HZ);
	assert(device.submit_lock.held && !device.ctrlq.qlock.held);
	wait_count++;
	if(wait_mode==1 || (wait_mode==4 && wait_count==1)) {
		unsigned spent=wait_mode==4?3*HZ:HZ;
		fixture_ticks+=spent;
		respond(0,0);
#ifdef FENCE_CONTRACT
		queue_error=0;
#endif
		return ticks-spent;
	}
	if(wait_mode==3) {
		virtio_gpu_stop(&device,-ENODEV);
		return ticks;
	}
	if(wait_mode==4) assert(ticks==2*HZ);
	fixture_ticks+=(unsigned)ticks;
	return 0;
}

static int
completion_transport(struct netbsd_virtqueue *q,void *cookie)
{
	queue_calls++;
	if(stop_in_transport) {
		stop_in_transport=0;
		virtio_gpu_stop(&device,-ENODEV);
		return -ENODEV;
	}
	if(queue_error) return queue_error;
	for(unsigned i=0;i<16;i++) {
		if(cookies[i]) continue;
		cookies[i]=cookie;
		accepted++;
		return 0;
	}
	assert(!"transport fixture capacity exhausted");
	return -ENOSPC;
}

static struct virtio_gpu_fence *
submit(int expected, bool with_array)
{
	struct virtio_gpu_vbuffer *b;
	struct virtio_gpu_ctrl_hdr *h;
	struct virtio_gpu_fence *f=virtio_gpu_fence_alloc(&device);

	assert(f && held_count<32);
	held[held_count++]=f;
	h=virtio_gpu_alloc_cmd(&device,&b,sizeof(*h)); assert(!IS_ERR(h));
	h->type=VIRTIO_GPU_CMD_SUBMIT_3D;
	b->data_buf=test_alloc(4); b->data_size=4;
	if(with_array) {
		struct drm_file client={0};
		uint32_t handle=1;
		b->objs=virtio_gpu_array_from_handles(&client,&handle,1); assert(b->objs);
		assert(virtio_gpu_array_lock_resv(b->objs)==0);
	}
	int ret=virtio_gpu_queue_fenced_ctrl_buffer(&device,b,h,f);
	if(ret!=expected) fprintf(stderr,"submit: expected %d got %d\n",expected,ret);
	assert(ret==expected);
#ifdef COMPLETION_FOUNDATION
	assert(!device.submitters);
#endif
	if(with_array) assert(!resv[0].locked && bos[0].refs==(expected?1U:2U));
	return f;
}

static void
mark(struct virtio_gpu_fence *f,int error)
{
#ifdef COMPLETION_FOUNDATION
	virtio_gpu_fence_complete(f,error);
#else
	if(error) virtio_gpu_fence_fail(f,error);
	else virtio_gpu_fence_event_process(&device,f->f.seqno);
#endif
}

static void
finish_all(void)
{
	virtio_gpu_stop(&device,-ENODEV);
	virtio_gpu_reset_work(&device.reset_work);
	for(unsigned i=0;i<2;i++) {
		assert(bos[i].refs==1 && !resv[i].locked);
		if(resv[i].fence_excl) dma_fence_put(resv[i].fence_excl);
	}
	for(unsigned i=0;i<held_count;i++) dma_fence_put(&held[i]->f);
	assert(!owned() && !used_count && !live);
	assert(list_empty(&device.fence_drv.fences));
#ifdef COMPLETION_FOUNDATION
	assert(!device.fence_drv.pending && !device.submitters);
#endif
}

static const char *const completion_names[] = {
	"higher response cannot complete unseen lower cookie",
	"reset holds terminal status until cookie drain",
	"lower error preserves higher known success",
	"duplicate and late results preserve first terminal result",
	"transient full admission drains without reset",
	"persistent gap times out with bounded list and preemit cleanup",
	"stop wakes admission without emission",
	"stop between unlock and rejected-cookie cancellation",
	"stop inside queue construction",
	"one deadline across admission and descriptor pressure",
	"stop during descriptor pressure",
	"sealed late producer cannot extend registered join",
	"cursor construction and rejected-cookie cleanup join",
	"short response resets before terminal publication",
	"ordered responses conserve count and references",
	"host error response resets before terminal publication"
};

static void
completion_case(unsigned which)
{
	struct submit_config_ops config={native_reset,native_del_vqs};
	struct virtio_device native={.max_request=4096,.config=&config};
	struct netbsd_virtqueue queue={16};
	struct virtio_gpu_fence *a,*b,*c;

	device.vdev=&native; device.vqs_ready=true;
	device.ctrlq.vq=device.cursorq.vq=&queue;
	INIT_LIST_HEAD(&device.fence_drv.fences);
	INIT_LIST_HEAD(&device.obj_free_list);
	for(unsigned i=0;i<2;i++) { bos[i].refs=1; bos[i].resv=&resv[i]; }
#ifdef COMPLETION_FOUNDATION
	device.fence_drv.vgdev=&device;
	device.fence_drv.limit=2;
#endif
	if(which==7 || which==8 || which==10 || which==12) {
		if(which==7 || which==12) { stop_on_unlock=1; queue_error=-ENOMEM; }
		if(which==8) stop_in_transport=1;
		if(which==10) { queue_error=-ENOSPC; wait_mode=3; }
		if(which==12) {
			struct virtio_gpu_vbuffer *v;
			void *p=virtio_gpu_alloc_cmd(&device,&v,16); assert(!IS_ERR(p));
			virtio_gpu_queue_cursor(&device,v);
			assert(!owned());
		} else {
			c=submit(which==7?-ENOMEM:-ENODEV,true);
			assert(!c->f.signaled);
		}
		assert(native_resets && cleanup_queued);
		finish_all(); return;
	}
	a=submit(0,false); b=submit(0,false);
	if(which==0 || which==2 || (which>=4 && which<=6) || which==9) {
		respond(1,0);
		assert(!a->f.signaled && !b->f.signaled);
#ifdef COMPLETION_FOUNDATION
		assert(device.fence_drv.pending==2 && b->ready && !a->ready);
#endif
	}
	switch(which) {
	case 0:
		respond(0,0); assert(a->f.signaled && b->f.signaled); break;
	case 1:
		virtio_gpu_stop(&device,-ENODEV);
		assert(native_resets && owned()==2 && !a->f.signaled && !b->f.signaled);
		break;
	case 2:
		respond(0,-EIO);
		assert(!a->f.signaled && !b->f.signaled && native_resets);
		virtio_gpu_reset_work(&device.reset_work);
		assert(a->f.error==-EIO && b->f.error==0 && a->f.signaled && b->f.signaled);
		break;
	case 3:
		respond(0,0); mark(a,-EIO); mark(a,0);
		assert(a->f.signaled && a->f.error==0); break;
	case 4:
		wait_mode=1; c=submit(0,false);
		assert(wait_count==1 && !native_resets && c->f.seqno==3);
		break;
	case 5:
		wait_mode=2; c=submit(-ETIMEDOUT,true);
		assert(!c->f.seqno && native_resets && owned()==1);
#ifdef COMPLETION_FOUNDATION
		assert(device.fence_drv.pending==2);
#endif
		break;
	case 6:
		wait_mode=3; c=submit(-ENODEV,true);
		assert(!c->f.seqno && wait_count==1); break;
	case 9:
		wait_mode=4; queue_error=-ENOSPC;
		c=submit(-ETIMEDOUT,true);
		assert(wait_count==2 && fixture_ticks==5*HZ && !native_resets);
		assert(c->f.signaled && c->f.error==-ETIMEDOUT); break;
	case 11:
		virtio_gpu_stop(&device,-ENODEV);
		c=submit(-ENODEV,true); assert(!c->f.seqno);
		assert(!a->f.signaled && !b->f.signaled); break;
	case 13:
	case 15:
		respond(0,which==13?-EINVAL:-EIO);
		assert(native_resets && !a->f.signaled && owned()==1); break;
	case 14:
		respond(0,0); assert(a->f.signaled && !b->f.signaled);
		respond(1,0); assert(b->f.signaled && transfers==2); break;
	default: assert(!"unknown completion case");
	}
	finish_all();
}

#ifdef FENCE_CONTRACT
#include "virtgpu-fence-exhaustion-cases.h"
#endif

int
main(void)
{
#ifdef FENCE_CONTRACT
	return exhaustion_contract_main();
#endif
	unsigned failed=0, count=sizeof(completion_names)/sizeof(completion_names[0]);
	for(unsigned i=0;i<count;i++) {
		pid_t pid=fork(); int status;
		assert(pid>=0);
		if(pid==0) { completion_case(i); exit(0); }
		assert(waitpid(pid,&status,0)==pid);
		if(!WIFEXITED(status)||WEXITSTATUS(status)) failed++;
		printf("%s %s\n",WIFEXITED(status)&&!WEXITSTATUS(status)?"PASS":"FAIL",completion_names[i]);
		fflush(stdout);
	}
	printf("%u groups, %u failed\n",count,failed);
	return failed?1:0;
}
