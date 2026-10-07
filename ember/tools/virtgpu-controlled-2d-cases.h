/* Origin: EmberBSD; AI-assisted real 2D/copy paths with causal host seams. */
/* SPDX-License-Identifier: BSD-2-Clause */
static unsigned char controlled_pixels[4096], controlled_shadow[4096];
static unsigned controlled_seen, controlled_expected;
static void controlled_setup(bool required)
{
	transfer_setup(); transfer_attachment=NULL; early_mode=1;
	memset(controlled_pixels,0xa5,sizeof(controlled_pixels));
	memset(controlled_shadow,0x5a,sizeof(controlled_shadow));
	backing[0].dma_vaddr=controlled_pixels; backing[0].mapped=1;
	backing[0].width=backing[0].height=32;
	backing[0].base.base.refs=1; backing[0].base.base.resv=&resv[0];
	backing[0].dma_required=required; backing[0].private_console=true;
	if(!required) backing[0].dma_lease=VIRTGPU_LEASE_NONE;
	controlled_seen=0;
}
static int controlled_upload(bool copy, struct virtio_gpu_fence *supplied)
{
#ifdef CONTROLLED_2D_FOUNDATION
	if(copy) return virtio_gpu_console_copy_upload(&device,&backing[0],
	    controlled_shadow,sizeof(controlled_shadow));
#endif
	struct virtio_gpu_object_array *a=virtio_gpu_operation_array_alloc(&device,1,
	    VIRTGPU_OPERATION_TO_HOST);
	if(!a) return -ENOMEM;
	virtio_gpu_array_add_obj(a,&bos[0]);
	if(supplied) assert(!virtio_gpu_array_lock_resv(a));
#ifndef CONTROLLED_2D_FOUNDATION
	if(copy) memcpy(controlled_pixels,controlled_shadow,sizeof(controlled_shadow));
#endif
	return virtio_gpu_cmd_transfer_to_host_2d(&device,0,32,32,0,0,a,supplied);
}
static void controlled_sync_check(int ops)
{
	if(ops!=BUS_DMASYNC_PREWRITE) return;
	assert(backing[0].exec_pending && backing[0].dma_members);
	assert(backing[0].pre_read==0);
	for(unsigned i=0;i<sizeof(controlled_pixels);i++)
		assert(controlled_pixels[i]==controlled_expected);
	controlled_seen++;
}
static long controlled_dependency(struct dma_fence *f, long ticks)
{
	assert(resv[0].locked && !device.submit_lock.held && ticks>0);
	for(unsigned i=0;i<sizeof(controlled_pixels);i++) assert(controlled_pixels[i]==0xa5);
	assert(backing[0].pre==1 && !backing[0].post);
	assert(exec_pending[0] && f==&exec_pending[0]->fence->f);
	exec_finish(0);
	assert(backing[0].post==1 && f->signaled);
	return ticks;
}
static long controlled_reset_wait(struct dma_fence *f, long ticks)
{
	device.vqs_ready=false; device.dma_stopped=true;
	return ticks; /* readiness only; not terminal POST or copy authorization */
}
static long controlled_complete_wait(long ticks)
{
	assert(exec_pending[0]);
	retire(exec_pending[0],-ENODEV); exec_pending[0]=NULL;
	device.vqs_ready=false; device.dma_stopped=true;
	return ticks;
}
static int controlled_prior(void)
{
	struct virtio_gpu_object_array *a=virtio_gpu_operation_array_alloc(&device,1,VIRTGPU_OPERATION_TO_HOST);
	struct virtio_gpu_vbuffer *b;
	struct virtio_gpu_transfer_to_host_2d *cmd=virtio_gpu_alloc_cmd(&device,&b,sizeof(*cmd));
	struct virtio_gpu_fence *f=virtio_gpu_fence_alloc(&device);
	assert(a && !IS_ERR(cmd) && f);
	virtio_gpu_array_add_obj(a,&bos[0]);assert(!virtio_gpu_array_lock_resv(a));
	cmd->hdr.type=VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;b->objs=a;
	int ret=virtio_gpu_queue_fenced_ctrl_buffer(&device,b,&cmd->hdr,f);
	dma_fence_put(&f->f);return ret;
}
static void controlled_case(unsigned which)
{
	bool required=which==1;
	controlled_setup(required);
	struct virtio_gpu_fence *f=NULL;
	int ret, expected=0;
	if(which==0 || which==1) {
		controlled_expected=0x5a; dma_sync_hook=controlled_sync_check;
		assert(!controlled_upload(true,NULL));
		assert(controlled_seen==1 && backing[0].pre_write==1 && backing[0].post_write==1);
		assert(!backing[0].pre_read && !backing[0].post_read);
		assert(backing[0].dma_lease==(required?VIRTGPU_LEASE_OPEN:VIRTGPU_LEASE_NONE));
	} else if(which>=2 && which<=6) {
		/* Array/command/wait/scratch/fence fail before any copy or PRE. */
		fail_alloc=alloc_calls+(which-1);
		ret=controlled_upload(true,NULL);
		assert(ret==-ENOMEM && !backing[0].pre && !backing[0].post);
		assert(controlled_pixels[0]==0xa5);
	} else if(which>=7 && which<=10) {
		queue_error=which==7?-ENOMEM:which==8?-EMSGSIZE:which==9?-ENODEV:-EIO;
		assert(controlled_upload(true,NULL)==queue_error);
		assert(backing[0].pre==1 && backing[0].post==1);
	} else if(which==11 || which==12) {
		pressure=which==11?2:1;
		assert(controlled_upload(true,NULL)==(which==11?0:-ETIMEDOUT));
		assert(backing[0].pre==(which==11?2:1) && backing[0].post==backing[0].pre);
	} else if(which==13 || which==14) {
		/* A durable prior member survives replacement of the resv slot. */
		early_mode=0;
		assert(!controlled_prior());
		dma_fence_put(resv[0].fence_excl); resv[0].fence_excl=NULL;
		assert(backing[0].exec_pending==1 && !exec_pending[0]->fence->exec);
		transfer_wait_hook=controlled_dependency; early_mode=1;
		if(which==14) {
			f=virtio_gpu_fence_alloc(&device); assert(f);
			controlled_expected=0xa5; dma_sync_hook=controlled_sync_check;
		}
		assert(!controlled_upload(which==13,f));
		assert(backing[0].pre==2 && backing[0].post==2);
		assert(controlled_pixels[0]==(which==13?0x5a:0xa5));
	} else if(which==15) {
		spinlock_t lock={0};
		struct dma_fence foreign={.refs=1,.lock=&lock,.f_magic=FENCE_MAGIC_GOOD};
		resv[0].fence_excl=dma_fence_get(&foreign);
		transfer_wait_hook=controlled_reset_wait;
		assert(controlled_upload(true,NULL)==-EIO);
		assert(!backing[0].pre && controlled_pixels[0]==0xa5);
		dma_fence_put(resv[0].fence_excl);resv[0].fence_excl=NULL;
		assert(foreign.refs==1);
	} else if(which==16) {
		early_mode=0; controlled_pressure_hook=controlled_complete_wait;
		assert(controlled_upload(true,NULL)==-ENODEV);
		assert(backing[0].pre==1 && backing[0].post==1);
	} else if(which==17) {
		defer_free=true;
		assert(!controlled_upload(true,NULL));
		assert(device.exec_bytes && backing[0].base.base.refs==2 && backing[0].post==1);
		virtio_gpu_array_put_free_work(&device.obj_free_work);
		assert(backing[0].base.base.refs==1);
	} else if(which==18) {
		lock_error=-EINTR; expected=-EINTR;
		assert(controlled_upload(true,NULL)==expected && !backing[0].pre && controlled_pixels[0]==0xa5);
	} else if(which==19) {
		backing[0].exec_pending=device.fence_drv.limit;
		assert(controlled_upload(false,NULL)==-EOVERFLOW && !backing[0].pre);
		backing[0].exec_pending=0;
	} else if(which==20) {
		device.fence_drv.sync_seq=INT32_MAX;
		assert(controlled_upload(true,NULL)==-EOVERFLOW && !backing[0].pre);
	} else if(which==21) {
		device.vqs_ready=false;
		assert(controlled_upload(true,NULL)==-ENODEV && controlled_pixels[0]==0xa5 && !backing[0].pre);
	} else if(which==22) {
		f=virtio_gpu_fence_alloc(&device); assert(f); fail_alloc=alloc_calls+2;
		assert(controlled_upload(false,f)==-ENOMEM && !backing[0].pre);
	} else if(which==23) {
		/* retained host bounce is stale for BOTH non-VIRGL host classes */
		for(unsigned cls=0;cls<2;cls++) {
			unsigned char host_bounce[4096]; memcpy(host_bounce,controlled_pixels,sizeof(host_bounce));
			device.has_virgl_3d=false;
			assert(!controlled_upload(true,NULL));
			assert(host_bounce[0]!=controlled_pixels[0]);
			memset(controlled_pixels,0xa5,sizeof(controlled_pixels));
		}
	} else if(which==24) {
		backing[0].private_console=false;
		assert(controlled_upload(true,NULL)==-EACCES && !backing[0].pre);
	} else abort();
	if(f) dma_fence_put(&f->f);
	dma_sync_hook=NULL; transfer_wait_hook=NULL; controlled_pressure_hook=NULL;
	transfer_cleanup();
	assert(backing[0].base.base.refs==1);
}
static int controlled_2d_main(void)
{
	static const char *const names[]={"private legacy copy","private qualified C2 copy",
	 "array OOM","command OOM","wait OOM","scratch OOM","fence OOM",
	 "queue ENOMEM","queue EMSGSIZE","queue ENODEV","queue EIO",
	 "ENOSPC retry","ENOSPC timeout","copy waits replaced ledger POST",
	 "prelocked supplied fence waits POST","reset readiness forbids copy",
	 "joined cancellation pairs POST","delayed array retirement","reservation EINTR",
	 "member cap before PRE","exhausted timeline no PRE","disabled before copy",
	 "prelocked failure consumption","two stale bounce host classes remain negative","ordinary BO copy rejected"};
	unsigned failed=0;
	for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
		int status;pid_t pid=fork();assert(pid>=0);
		if(!pid){ controlled_case(i);exit(0); }
		assert(waitpid(pid,&status,0)==pid);
		bool ok=WIFEXITED(status)&&WEXITSTATUS(status)==0; failed+=!ok;
		printf("%s %s\n",ok?"PASS":"FAIL",names[i]);fflush(stdout);
	}
	printf("25 controlled 2D groups, %u failed\n",failed);
	return failed?1:0;
}
