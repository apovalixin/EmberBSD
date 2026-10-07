/* Origin: EmberBSD; AI-assisted production explicit transfer and WAIT cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static struct drm_virtgpu_3d_transfer_from_host transfer_from;
static struct drm_virtgpu_3d_transfer_to_host transfer_to;
static unsigned transfer_budget_calls;
static long transfer_budget_seen[4];
static const char *const transfer_names[] = {
	"FROM enqueue ENOMEM", "TO enqueue ENOMEM", "FROM PREREAD", "TO error POST",
	"WAIT unknown flags", "WAIT stored EIO", "WAIT stored ETIMEDOUT",
	"TO asynchronous acceptance and exact BO", "FROM successful POST once",
	"TO ENOSPC retry", "FROM ENOSPC retry", "enqueue EMSGSIZE", "enqueue ENODEV",
	"persistent pressure timeout", "missing fpriv", "zero context", "zero software key",
	"closing context", "stopped device", "missing acknowledged membership",
	"duplicate handles retain membership", "unqualified map", "closed lease",
	"release pending", "missing handle", "reservation signal",
	"array allocation failure", "scratch allocation failure", "fence allocation failure",
	"command allocation failure", "charge survives delayed array free", "callback before return",
	"transfer waits earlier same-key EXEC", "EXEC waits earlier transfer",
	"replaced reservation retains transfer ledger", "WAIT known error behind prefix",
	"shared error precedes exclusive pending wait", "exclusive error with shared success",
	"shared count exceeds ledger capacity", "NOWAIT pending", "NOWAIT busy reservation",
	"NOWAIT nonblocking allocation failure", "WAIT signal", "WAIT local timeout",
	"WAIT completed snapshot", "one deadline across dependencies", "transfer dependency deadline",
	"WAIT unfinished after positive wake", "reset cancellation POST before fence",
	"last reference held through POST", "u32 offset preserved", "FROM disabled",
	"legacy TO dispatch preserved", "WAIT missing handle", "member bound before PRE",
	"WAIT budget rejection", "transfer signal before PRE", "last safe transfer fence retry", "terminal error at WAIT budget boundary"
};
static void
transfer_setup(void)
{
	exec_setup(); transfer_attachment=&exec_priv.attachment_lock;
	for(unsigned i=0;i<2;i++) {
		backing[i].base.base.size=4096;
		backing[i].pages=calloc(1,sizeof(*backing[i].pages));
		backing[i].pages->sgl=calloc(1,sizeof(*backing[i].pages->sgl));
		backing[i].pages->sgl->sg_dmamap=&backing[i];
		backing[i].hw_res_handle=i+1;
	}
	transfer_from=(struct drm_virtgpu_3d_transfer_from_host){.bo_handle=1};
	transfer_to=(struct drm_virtgpu_3d_transfer_to_host){.bo_handle=1};
}
static int transfer_submit(bool from)
{
	return from ? virtio_gpu_transfer_from_host_ioctl(&exec_drm,&transfer_from,&exec_client)
	    : virtio_gpu_transfer_to_host_ioctl(&exec_drm,&transfer_to,&exec_client);
}
static void transfer_cleanup(void)
{
	exec_cleanup();
	for(unsigned i=0;i<2;i++) { free(backing[i].pages->sgl); free(backing[i].pages); }
}
static long transfer_budget_wait(struct dma_fence *f,long ticks)
{
	assert(!device.dma_lock.held && !device.submit_lock.held);
	assert(transfer_budget_calls<4);
	transfer_budget_seen[transfer_budget_calls++]=ticks;
	if(ticks<6*HZ) return 0;
	fixture_ticks+=6*HZ; f->signaled=true; return ticks-6*HZ;
}
static long transfer_error_wait(struct dma_fence *f,long ticks)
{
	f->signaled=true; f->error=-ETIMEDOUT; return 0;
}
static void transfer_case(unsigned which)
{
	struct drm_virtgpu_3d_wait args={.handle=1};
	spinlock_t fl={0};
	struct dma_fence foreign[4];
	struct virtio_gpu_fence *held;
	int expected=0, ret;
	bool from=true;

	transfer_setup();
	for(unsigned i=0;i<4;i++)
		foreign[i]=(struct dma_fence){.refs=1,.f_magic=FENCE_MAGIC_GOOD,.lock=&fl};
	if(which<2 || which==11 || which==12) {
		queue_error=which<2?-ENOMEM:which==11?-EMSGSIZE:-ENODEV;
		assert(transfer_submit(which!=1)==queue_error);
	} else if(which==2 || which==3 || which==7 || which==8) {
		from=which==2 || which==8;
		assert(!transfer_submit(from) && exec_queued==1);
		assert(backing[0].pre_read==from && backing[0].pre_write==!from);
		assert(!backing[1].pre && !backing[1].exec_pending);
		assert(!exec_pending[0]->fence->exec && !exec_pending[0]->fence->f.signaled);
		retire(exec_pending[0],which==3?-EIO:0); exec_pending[0]=NULL;
		assert(backing[0].post_read==from && backing[0].post_write==!from);
	} else if(which==4) {
		args.flags=2;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-EINVAL);
	} else if(which==5 || which==6) {
		foreign[0].signaled=true; foreign[0].error=which==5?-EIO:-ETIMEDOUT;
		resv[0].fence_excl=dma_fence_get(&foreign[0]);
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==foreign[0].error);
	} else if(which==9 || which==10 || which==13 || which==57) {
		pressure=which==13?1:2;
		if(which==57) device.fence_drv.sync_seq=INT32_MAX-1;
		assert(transfer_submit(which==10)==(which==13?-ETIMEDOUT:0));
		if(which!=13) {
			assert(backing[0].pre==2 && backing[0].post==1 && backing[0].exec_pending==1);
			if(which==57) assert(exec_pending[0]->fence->f.seqno==INT32_MAX);
		}
	} else if(which>=14 && which<=29) {
		switch(which) {
		case 14: exec_client.driver_priv=NULL; expected=-EINVAL; break;
		case 15: exec_priv.ctx_id=0; expected=-EINVAL; break;
		case 16: exec_priv.software_key=0; expected=-EINVAL; break;
		case 17: exec_priv.closing=true; expected=-ENODEV; break;
		case 18: device.vqs_ready=false; expected=-ENODEV; break;
		case 19: list_del_init(&exec_attach[0].node); exec_priv.attachment_count--; expected=-EINVAL; break;
		case 20: exec_attach[0].handles=2; break;
		case 21: deny_bo=1; expected=-EOPNOTSUPP; break;
		case 22: backing[0].dma_lease=VIRTGPU_LEASE_CLOSED; expected=-EOPNOTSUPP; break;
		case 23: backing[0].release_pending=true; expected=-EOPNOTSUPP; break;
		case 24: fail_lookup=1; expected=-ENOENT; break;
		case 25: lock_error=-EINTR; expected=-EINTR; break;
		default: fail_alloc=which-25; expected=-ENOMEM; break;
		}
		assert(transfer_submit(true)==expected);
		if(expected) assert(!backing[0].pre && !queue_calls && !stops);
	} else if(which==30 || which==31) {
		defer_free=which==30; early_mode=which==31;
		assert(!transfer_submit(true));
		if(which==30) { exec_finish(0); assert(device.exec_bytes && live); }
		else assert(!live || resv[0].fence_excl); /* Reservation retains its fence. */
	} else if(which==32 || which==33 || which==34 || which==56) {
		assert(!(which==32?exec_submit():transfer_submit(true)));
		if(which==34) {
			assert(!dma_resv_lock_interruptible(&resv[0],NULL));
			dma_resv_add_excl_fence(&resv[0],NULL);
			dma_resv_unlock(&resv[0]);
		}
		input_wait=which==56?-EINTR:0;
		ret=which==33?exec_submit():transfer_submit(true);
		assert(ret==(which==56?-EINTR:-ETIMEDOUT));
		assert(exec_queued==1 && backing[0].exec_pending==1 && !stops);
	} else if(which==35) {
		assert(!exec_submit() && !exec_submit());
		virtio_gpu_fence_complete(exec_pending[1]->fence,-EIO);
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-EIO && !wait_calls);
	} else if(which>=36 && which<=38) {
		device.fence_drv.limit=2;
		resv[0].fence_excl=dma_fence_get(&foreign[0]);
		unsigned count=which==38?3:1;
		for(unsigned i=0;i<count;i++) resv[0].shared.shared[i]=dma_fence_get(&foreign[i+1]);
		resv[0].shared.shared_count=count;
		unsigned error=which==37?0:count;
		foreign[error].signaled=true; foreign[error].error=-EIO;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-EIO && !wait_calls);
	} else if(which>=39 && which<=44) {
		if(which==39 || which==42 || which==43)
			resv[0].fence_excl=dma_fence_get(&foreign[0]);
		if(which<=41) args.flags=VIRTGPU_WAIT_NOWAIT;
		if(which==40) resv[0].locked=true;
		if(which==41) fail_alloc=1;
		input_wait=which==42?-EINTR:0;
		expected=which==41?-ENOMEM:which==42?-EINTR:which==44?0:-EBUSY;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==expected && !stops);
		if(which<=41) { assert(!wait_calls && !ww_calls); assert(transfer_last_flags==1); }
		if(which==40) resv[0].locked=false;
	} else if(which==45 || which==46) {
		resv[0].fence_excl=dma_fence_get(&foreign[0]);
		for(unsigned i=0;i<2;i++) resv[0].shared.shared[i]=dma_fence_get(&foreign[i+1]);
		resv[0].shared.shared_count=2;
		transfer_wait_hook=transfer_budget_wait;
		ret=which==45?virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client):transfer_submit(true);
		assert(ret==(which==45?-EBUSY:-ETIMEDOUT));
		assert(transfer_budget_calls==3 && transfer_budget_seen[0]==15*HZ &&
		    transfer_budget_seen[1]==9*HZ && transfer_budget_seen[2]==3*HZ);
		assert(!backing[0].pre && !stops);
	} else if(which==58) {
		resv[0].fence_excl=dma_fence_get(&foreign[0]);
		transfer_wait_hook=transfer_error_wait;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-ETIMEDOUT);
	} else if(which==47) {
		resv[0].fence_excl=dma_fence_get(&foreign[0]); input_wait=1;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-EBUSY);
	} else if(which==48) {
		assert(!transfer_submit(true)); held=exec_pending[0]->fence; dma_fence_get(&held->f);
		virtio_gpu_fence_stop(&device,-ENODEV); device.dma_stopped=true;
		virtio_gpu_finish_vbuf(exec_pending[0],-ENODEV); exec_pending[0]=NULL;
		assert(backing[0].pre_read==backing[0].post_read && !held->f.signaled);
		virtio_gpu_fail_fences(&device,-ENODEV); assert(held->f.error==-ENODEV);
		dma_fence_put(&held->f);
	} else if(which==49) {
		assert(!transfer_submit(true)); drm_gem_object_put_unlocked(&bos[0]);
		assert(bos[0].refs==1 && !transfer_zero_releases);
		exec_finish(0); assert(!bos[0].refs && transfer_zero_releases==1);
		bos[0].refs=1; /* Restore the fixture's static owner for common cleanup. */
	} else if(which==50) {
		transfer_from.offset=UINT32_MAX; assert(!transfer_submit(true));
		struct virtio_gpu_transfer_host_3d *cmd=(void *)exec_pending[0]->buf;
		assert(cmd->offset==UINT32_MAX);
	} else if(which==51 || which==52) {
		device.has_virgl_3d=false;
		assert(transfer_submit(which==51)==(which==51?-ENOSYS:-EOPNOTSUPP));
	} else if(which==53) {
		fail_lookup=1; assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-ENOENT);
	} else if(which==54) {
		backing[0].exec_pending=device.fence_drv.limit;
		assert(transfer_submit(true)==-EOVERFLOW && !backing[0].pre);
		backing[0].exec_pending=0;
	} else {
		assert(which==55); device.exec_bytes=VIRTGPU_EXEC_BUDGET;
		assert(virtio_gpu_wait_ioctl(&exec_drm,&args,&exec_client)==-ENOMEM && !ww_calls);
		device.exec_bytes=0;
	}
	transfer_cleanup();
	for(unsigned i=0;i<4;i++) assert(foreign[i].refs==1);
}
static int transfer_contract_main(void)
{
	unsigned failed=0, count=sizeof(transfer_names)/sizeof(transfer_names[0]);
	for(unsigned i=0;i<count;i++) {
		pid_t pid=fork(); int result; assert(pid>=0);
		if(!pid) { transfer_case(i); exit(0); }
		assert(waitpid(pid,&result,0)==pid);
		bool pass=WIFEXITED(result) && !WEXITSTATUS(result);
		failed+=!pass; printf("%s %s\n",pass?"PASS":"FAIL",transfer_names[i]); fflush(stdout);
	}
	printf("%u transfer/WAIT groups, %u failed\n",count,failed);
	return failed?1:0;
}
