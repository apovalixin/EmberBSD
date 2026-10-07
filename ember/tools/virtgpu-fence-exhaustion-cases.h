/* Origin: EmberBSD; AI-assisted actual classic emitter/comparator/queue cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static struct submit_config_ops exhaustion_config={native_reset,native_del_vqs};
static const char *const exhaustion_names[] = {
	"last safe wire ID and next rejection", "stale UINT32_MAX rejects",
	"stale UINT32_MAX plus one rejects", "stale UINT64_MAX rejects",
	"native 32-bit comparator retained seq1 boundary",
	"full pending list exhaustion bypasses pressure", "EXEC output fd zero",
	"EXEC positive output fd", "EXEC without output fd",
	"C3 older ledger survives exhaustion until reset",
	"last safe fence ENOSPC retry retains its ID",
	"ordinary low-ID pressure remains bounded", "later stopped preserves first error",
	"admitted fenced producer sees seal", "admitted unfenced producer sees seal",
	"admitted cursor producer sees seal", "late result retains original terminal status",
	"last safe EXEC retry preserves ledger and token balance"
};
static void
exhaustion_init(void)
{
	exec_setup();
	exec_native.config=&exhaustion_config;
	device.cursorq.vq=&exec_queue;
	advanced=false;
}
static void
exhaustion_cleanup(void)
{
	/* Actual reset worker drains accepted cookies before terminal publication. */
	finish_all();
	assert(!device.exec_bytes);
	for(unsigned i=0;i<2;i++) {
		assert(!backing[i].exec_pending && !backing[i].dma_members);
		assert(!backing[i].dma_retire_refs && backing[i].pre==backing[i].post);
	}
}
static void
exhaustion_sealed_observer(void)
{
	assert(!device.vqs_ready && device.dma_stopped && device.fence_drv.stopped);
	assert(!native_resets && device.submitters==2);
	assert(!resv[0].locked && !resv[1].locked);
}
static void
exhaustion_before_lock(struct mutex *m)
{
	if(m!=&device.submit_lock) return;
	before_lock_hook=NULL;
	assert(device.submitters==1);
	struct virtio_gpu_vbuffer *v;
	struct virtio_gpu_ctrl_hdr *h=virtio_gpu_alloc_cmd(&device,&v,sizeof(*h));
	struct virtio_gpu_fence *f=virtio_gpu_fence_alloc(&device);
	assert(!IS_ERR(h) && f); held[held_count++]=f;
	sealed_unlock_hook=exhaustion_sealed_observer;
	assert(virtio_gpu_queue_fenced_ctrl_buffer(&device,v,h,f)==-EOVERFLOW);
	assert(device.submitters==1 && !f->f.seqno && native_resets);
}
static void
exhaustion_case(unsigned which)
{
	struct virtio_gpu_ctrl_hdr hdr={.type=0x5a,.flags=0x30,.fence_id=0x55};
	struct virtio_gpu_ctrl_hdr before=hdr;
	struct virtio_gpu_fence *a,*b;
	uint64_t seq;
	int ret;

	exhaustion_init();
	if(which<=3) {
	if(which==0) {
			device.fence_drv.sync_seq=INT32_MAX-1;
			a=virtio_gpu_fence_alloc(&device); held[held_count++]=a;
			assert(!virtio_gpu_fence_emit(&device,&hdr,a));
			assert(a->f.seqno==INT32_MAX && hdr.fence_id==INT32_MAX && !device.fence_drv.stopped);
			hdr=before;
		} else device.fence_drv.sync_seq=which==1?UINT32_MAX:which==2?UINT64_C(0x100000000):UINT64_MAX;
		seq=device.fence_drv.sync_seq;
		unsigned pending_before=device.fence_drv.pending;
		b=virtio_gpu_fence_alloc(&device); held[held_count++]=b;
		assert(virtio_gpu_fence_emit(&device,&hdr,b)==-EOVERFLOW);
		assert(device.fence_drv.sync_seq==seq && !memcmp(&hdr,&before,sizeof(hdr)));
		assert(!b->f.seqno && b->f.refs==1 && list_empty(&b->node));
		assert(device.fence_drv.pending==pending_before && device.fence_drv.stopped);
		assert(device.fence_drv.stop_error==-EOVERFLOW);
		exhaustion_cleanup(); return;
	}
	if(which==4) {
		assert(__dma_fence_is_later(INT32_MAX,1,&virtio_fence_ops));
		assert(!__dma_fence_is_later(UINT64_C(0x80000000),1,&virtio_fence_ops));
		exhaustion_cleanup(); return;
	}
	if(which==5) {
		device.fence_drv.limit=1; device.fence_drv.sync_seq=INT32_MAX-1;
		a=submit(0,false); assert(!a->f.signaled);
		b=submit(-EOVERFLOW,true);
		assert(!wait_count && !b->f.seqno && !b->f.signaled && owned()==1);
		assert(native_resets && device.dma_stopped && !device.vqs_ready);
		assert(!a->f.signaled); exhaustion_cleanup(); return;
	}
	if(which>=6 && which<=9) {
	if(which==9) { assert(!exec_submit()); assert(backing[0].exec_pending==1); }
		device.fence_drv.sync_seq=INT32_MAX;
	if(which!=8 && which!=9) {
			exec_args.flags=VIRTGPU_EXECBUF_FENCE_FD_OUT;
			output_fd=which==6?0:7;
		}
		ret=exec_submit(); assert(ret==-EOVERFLOW);
		assert(!installs && exec_args.fence_fd==-1 && !reservations);
		assert(closes==sync_created && aborts==files_allocated);
		assert(!device.vqs_ready && device.dma_stopped && !device.submitters);
		assert(backing[0].exec_pending==(which==9?1U:0U));
		assert(backing[0].pre==(which==9?1:0));
		exhaustion_cleanup(); return;
	}
	if(which==10 || which==11) {
		device.fence_drv.sync_seq=which==10?INT32_MAX-2:0;
		a=submit(0,false);
		queue_error=-ENOSPC; wait_mode=1;
		/* completion_wait retires the old cookie, then permit the retry. */
		b=submit(0,false);
		assert(b->f.seqno==(which==10?(uint64_t)INT32_MAX:2));
		assert(wait_count==1 && !native_resets && !device.fence_drv.stopped);
		exhaustion_cleanup(); return;
	}
	if(which>=13 && which<=15) {
		device.fence_drv.sync_seq=INT32_MAX;
		before_lock_hook=exhaustion_before_lock;
	if(which==13) a=submit(-ENODEV,false);
		else {
			struct virtio_gpu_vbuffer *v;
			struct virtio_gpu_ctrl_hdr *h=virtio_gpu_alloc_cmd(&device,&v,sizeof(*h));
			assert(!IS_ERR(h));
			if(which==14) assert(virtio_gpu_queue_fenced_ctrl_buffer(&device,v,NULL,NULL)==-ENODEV);
			else virtio_gpu_queue_cursor(&device,v);
		}
		assert(!device.submitters && !queue_calls && device.fence_drv.stop_error==-EOVERFLOW);
		exhaustion_cleanup(); return;
	}
	if(which==16) {
		a=submit(0,false); b=submit(0,false); respond(1,0);
		device.fence_drv.sync_seq=INT32_MAX;
		(void)submit(-EOVERFLOW,false);
		assert(!a->f.signaled && !b->f.signaled);
		virtio_gpu_reset_work(&device.reset_work);
		assert(a->f.error==-ENODEV && b->f.error==0);
		mark(a,0); mark(b,-EIO);
		assert(a->f.error==-ENODEV && b->f.error==0);
		exhaustion_cleanup(); return;
	}
	if(which==17) {
		assert(!exec_submit());
		device.fence_drv.sync_seq=INT32_MAX-1;
		queue_error=-ENOSPC; wait_mode=1;
		assert(!exec_submit());
		assert(cookies[0] && cookies[0]->fence->f.seqno==INT32_MAX);
		assert(backing[0].exec_pending==1 && backing[0].pre==3 && backing[0].post==2);
		assert(!native_resets && !device.fence_drv.stopped && wait_count==1);
		exhaustion_cleanup(); return;
	}
	assert(which==12);

	device.fence_drv.sync_seq=INT32_MAX;
	a=submit(-EOVERFLOW,false); b=submit(-ENODEV,false);
	assert(!a->f.seqno && !b->f.seqno && device.fence_drv.stop_error==-EOVERFLOW);
	exhaustion_cleanup();
}
static int
exhaustion_contract_main(void)
{
	unsigned failed=0, count=sizeof(exhaustion_names)/sizeof(exhaustion_names[0]);
	for(unsigned i=0;i<count;i++) {
		pid_t pid=fork(); int status; assert(pid>=0);
		if(pid==0) { exhaustion_case(i); exit(0); }
		assert(waitpid(pid,&status,0)==pid);
		bool pass=WIFEXITED(status) && WEXITSTATUS(status)==0;
		if(!pass) failed++;
		printf("%s %s\n",pass?"PASS":"FAIL",exhaustion_names[i]); fflush(stdout);
	}
	printf("%u exhaustion groups, %u failed\n",count,failed);
	return failed?1:0;
}
