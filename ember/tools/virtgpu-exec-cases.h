/* Origin: EmberBSD; AI-assisted whole-context EXEC causal ownership cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static struct virtio_gpu_fpriv exec_priv;
static struct virtio_gpu_attachment exec_attach[2];
static struct drm_device exec_drm = { &device };
static struct drm_file exec_client = { &exec_priv };
static struct virtio_device exec_native = { .max_request=4096 };
static struct netbsd_virtqueue exec_queue = { 16 };
static uint32_t exec_handles[4] = {1,2,1,2}, exec_command;
static struct drm_virtgpu_execbuffer exec_args;
static unsigned attach_locks;
static int change_count;

static void
exec_count_hook(struct mutex *m)
{
	if(m != &exec_priv.attachment_lock || ++attach_locks != 2) return;
	if(change_count > 0) exec_priv.attachment_count++;
	else if(change_count < 0) {
		list_del(&exec_attach[1].node); exec_priv.attachment_count--;
	}
}

static void
exec_setup(void)
{
	advanced=true;
	device.has_virgl_3d=true; device.vqs_ready=true;
	device.vdev=&exec_native; device.ctrlq.vq=&exec_queue;
	device.fence_drv.vgdev=&device; device.fence_drv.limit=16;
	INIT_LIST_HEAD(&device.fence_drv.fences);
	INIT_LIST_HEAD(&device.obj_free_list);
	exec_priv.ctx_id=3; exec_priv.software_key=1; exec_priv.attachment_count=2;
	INIT_LIST_HEAD(&exec_priv.attachments);
	for(unsigned i=0;i<2;i++) {
		bos[i].refs=1; bos[i].resv=&resv[i]; resv[i].fence=&resv[i].shared;
		backing[i].dma_lease=VIRTGPU_LEASE_OPEN;
		backing[i].dma_required=true; backing[i].dma_eligible=true;
		INIT_LIST_HEAD(&backing[i].exec_members);
		exec_attach[i].obj=&bos[i]; exec_attach[i].handles=1;
		list_add_tail(&exec_attach[i].node,&exec_priv.attachments);
	}
	for(unsigned i=0;i<8;i++) fdtable.dt_ff[i]=filedesc.fd_dfdfile[i]=&fdslots[i];
	filedesc.fd_dt=&fdtable; process.p_fd=&filedesc;
	exec_args=(struct drm_virtgpu_execbuffer){.size=4,.command=(uintptr_t)&exec_command,
	    .bo_handles=(uintptr_t)exec_handles,.num_bo_handles=0,.fence_fd=-1};
}

static int
exec_submit(void)
{
	return virtio_gpu_execbuffer_ioctl(&exec_drm,&exec_args,&exec_client);
}

static void
exec_finish(unsigned n)
{
	assert(exec_pending[n]);
	retire(exec_pending[n],0); exec_pending[n]=NULL;
}

static void
exec_cleanup(void)
{
	for(unsigned i=0;i<exec_queued;i++) if(exec_pending[i]) exec_finish(i);
	virtio_gpu_array_put_free_work(&device.obj_free_work);
	for(unsigned i=0;i<2;i++) {
		if(resv[i].fence_excl) { dma_fence_put(resv[i].fence_excl); resv[i].fence_excl=NULL; }
		for(unsigned j=0;j<resv[i].shared.shared_count;j++) dma_fence_put(resv[i].shared.shared[j]);
		assert(bos[i].refs==1 && !resv[i].locked);
		assert(!backing[i].dma_members && !backing[i].exec_pending && !backing[i].dma_retire_refs);
		assert(backing[i].pre==backing[i].post && list_empty(&backing[i].exec_members));
	}
	assert(!device.exec_bytes && !live && !device.submitters);
	assert(!device.submit_lock.held && !device.dma_lock.held && !exec_priv.attachment_lock.held);
	assert(list_empty(&device.fence_drv.fences));
}

static void
exec_interleaved_post(int ops)
{
	if(ops!=12) return;
	dma_sync_hook=NULL;
	assert(backing[0].dma_retire_refs==1 && backing[0].dma_members==2);
	exec_finish(1);
	assert(backing[0].dma_retire_refs==1 && backing[0].dma_members==1);
}

static const char *const exec_names[]
 = {
	"empty hints snapshot", "partial hints snapshot", "duplicate hints unique members",
	"foreign hint rejected", "missing hint rejected", "growth returns EAGAIN",
	"shrink uses current set", "closing rejects", "ineligible BO rejects",
	"lease closed rejects", "byte budget rejects without reset", "capacity overflow",
	"charge retained until delayed free", "same-key overlap and reversed replies",
	"replaced reservation does not erase native ledger", "foreign exclusive waits",
	"shared dependencies checked", "same key known error behind prefix rejects",
	"different software key despite reused wire ID waits", "unknown native type waits",
	"one deadline includes explicit input", "expired shared deadline",
	"signal returns without reset", "ENOSPC preserves ledger between attempts",
	"persistent pressure balances tokens", "stop during PRE closes entire token",
	"early completion does not touch freed cookie", "key exhaustion never wraps",
	"WW zero", "WW single", "WW EDEADLK retry", "WW slow EINTR",
	"WW multi EINTR fini", "WW duplicate rejected and balanced",
	"allocation failure 1", "allocation failure 2", "allocation failure 3",
	"allocation failure 4", "allocation failure 5", "allocation failure 6",
	"allocation failure 7", "member overflow no partial PRE",
	"multi-cookie cancellation before terminal reset fences",
	"interleaved POST raw pins", "zero actual attachments",
	"known foreign error", "pending member bound"
};

static void
exec_case(unsigned which)
{
	struct dma_fence *scratch[16];
	struct virtio_gpu_object_array *a;
	struct virtio_gpu_fence *first;
	struct dma_fence foreign={.refs=1,.f_magic=FENCE_MAGIC_GOOD};
	struct dma_fence explicit={.refs=1,.f_magic=FENCE_MAGIC_GOOD,.signaled=true};
	spinlock_t foreign_lock={0};
	u64 key;
	int ret, expected=0;

	exec_setup(); foreign.lock=&foreign_lock;
		if(which==42 || which==43) {
		assert(!exec_submit() && !exec_submit());
		first=exec_pending[0]->fence; dma_fence_get(&first->f);
		if(which==42) {
			virtio_gpu_fence_stop(&device,-ENODEV); device.dma_stopped=true;
			for(unsigned i=0;i<2;i++) {
				virtio_gpu_finish_vbuf(exec_pending[i],-ENODEV); exec_pending[i]=NULL;
				assert(!first->f.signaled);
			}
			assert(!backing[0].dma_members && !backing[0].exec_pending);
			virtio_gpu_fail_fences(&device,-ENODEV);
			assert(dma_fence_get_status(&first->f)==-ENODEV);
		} else { dma_sync_hook=exec_interleaved_post; exec_finish(0); }
		dma_fence_put(&first->f); exec_cleanup(); return;
	}
	if(which==44) {
		list_del(&exec_attach[0].node); list_del(&exec_attach[1].node);
		exec_priv.attachment_count=0;
		assert(!exec_submit() && !exec_pending[0]->objs->nents);
		exec_cleanup(); return;
	}
	if(which==45) {
		foreign.signaled=true; foreign.error=-EIO;
		resv[0].fence_excl=dma_fence_get(&foreign); expected=-EIO;
	}
	if(which==46) { backing[1].exec_pending=device.fence_drv.limit; expected=-EOVERFLOW; }
	if(which==1) exec_args.num_bo_handles=1;

	if(which==2) exec_args.num_bo_handles=4;
	if(which==3) { list_del(&exec_attach[1].node); exec_priv.attachment_count=1;
		exec_args.num_bo_handles=2; expected=-EINVAL; }
	if(which==4) { exec_args.num_bo_handles=2; fail_lookup=2; expected=-ENOENT; }
	if(which==5 || which==6) { change_count=which==5?1:-1; lock_hook=exec_count_hook; if(which==5) expected=-EAGAIN; }
	if(which==7) { exec_priv.closing=true; expected=-ENODEV; }
	if(which==8) { deny_bo=2; expected=-EOPNOTSUPP; }
	if(which==9) { backing[1].dma_lease=VIRTGPU_LEASE_CLOSED; expected=-EOPNOTSUPP; }
	if(which==10) { assert(!virtio_gpu_exec_charge(&device,VIRTGPU_EXEC_BUDGET)); expected=-ENOMEM; }
	if(which==11) {
		assert(!virtio_gpu_exec_array_alloc(&device,UINT_MAX));
		a=virtio_gpu_exec_array_alloc(&device,VIRTGPU_EXEC_MAX_OBJECTS); assert(a);
		virtio_gpu_array_put_free(a); exec_cleanup(); return;
	}
	if(which==12) defer_free=true;
	if(which>=13 && which<=14) {
		assert(exec_submit()==0); first=exec_pending[0]->fence;
		assert(exec_submit()==0 && !wait_calls && backing[0].exec_pending==2);
		if(which==13) {
			exec_finish(1); assert(!first->f.signaled && device.fence_drv.pending==2);
			assert(backing[0].exec_pending==1 && backing[0].dma_members==1);
		} else {
			a=virtio_gpu_exec_array_alloc(&device,1); assert(a); virtio_gpu_array_add_obj(a,&bos[0]);
			assert(!virtio_gpu_array_lock_resv(a));
			dma_resv_add_excl_fence(&resv[0],NULL);
			input_wait=0;
			assert(virtio_gpu_exec_dependencies(&device,a,scratch,16,2,0)==-ETIMEDOUT);
			assert(wait_calls==1 && first->f.refs>0);
			virtio_gpu_array_unlock_resv(a); virtio_gpu_array_put_free(a);
		}
		exec_cleanup(); return;
	}
	if(which==15 || which==16 || which==22) {
		if(which==16) { resv[0].shared.shared[0]=dma_fence_get(&foreign); resv[0].shared.shared_count=1; }
		else resv[0].fence_excl=dma_fence_get(&foreign);
		input_wait=which==22?-EINTR:0; expected=which==22?-EINTR:-ETIMEDOUT;
	}
	if(which>=17 && which<=19) {
		assert(exec_submit()==0); first=exec_pending[0]->fence;
		if(which==17) {
			queue_error=-ENOMEM; assert(exec_submit()==-ENOMEM); queue_error=0;
			assert(!first->f.signaled && device.fence_drv.pending==2);
			expected=-ENOMEM;
		}
		if(which==18) { exec_priv.software_key=2; input_wait=0; expected=-ETIMEDOUT; }
		if(which==19) { first->exec=false; input_wait=0; expected=-ETIMEDOUT; }
		ret=exec_submit(); assert(ret==expected && accepted==1 && !stops);
		first->ready=false; first->result=0;
		exec_cleanup(); return;
	}
	if(which==20 || which==21) {
		explicit.lock=&foreign_lock; input=&explicit;
		exec_args.flags=VIRTGPU_EXECBUF_FENCE_FD_IN; exec_args.fence_fd=0;
		dependency_advance=which==20?400:1501;
		resv[0].fence_excl=dma_fence_get(&foreign);
		input_wait=0; expected=-ETIMEDOUT;
	}
	if(which==23 || which==24) { pressure=which==23?2:1; if(which==24) expected=-ETIMEDOUT; }
	if(which==25) { stop_pre=1; expected=-ENODEV; }
	if(which==26) early_mode=1;
	if(which==27) {
		device.next_context_key=UINT64_MAX-1;
		assert(!virtio_gpu_context_key(&device,&key) && key==UINT64_MAX);
		assert(virtio_gpu_context_key(&device,&key)==-EOVERFLOW);
		assert(device.next_context_key==UINT64_MAX); exec_cleanup(); return;
	}
	if(which>=28 && which<=33) {
		a=virtio_gpu_array_alloc(2); assert(a);
		if(which!=28) virtio_gpu_array_add_obj(a,&bos[0]);
		if(which>=30) virtio_gpu_array_add_obj(a,which==33?&bos[0]:&bos[1]);
		if(which==30 || which==31) ww_deadlock_at=2;
		if(which==31) { ww_slow_error=-EINTR; expected=-EINTR; }
		if(which==32) { lock_error=-EINTR; expected=-EINTR; }
		if(which==33) expected=-EALREADY;
		ret=virtio_gpu_array_lock_resv(a); assert(ret==expected);
		if(!ret) virtio_gpu_array_unlock_resv(a);
		if(a->nents!=1) assert(a->ticket.wwx_owner==NULL && a->ticket.wwx_acquired==~0U);
		if(which==30 || which==31) assert(ww_slow_calls==1);
		virtio_gpu_array_put_free(a); exec_cleanup(); return;
	}
	if(which>=34 && which<=40) { exec_args.num_bo_handles=2; fail_alloc=which-33; expected=-ENOMEM; }
	if(which==41) { backing[1].dma_members=UINT_MAX; expected=-EOVERFLOW; }
	ret=exec_submit();
	if(ret!=expected) fprintf(stderr,"case %u got %d expected %d\n",which,ret,expected);
	assert(ret==expected);
	if(expected) assert(!accepted && !stops);
	else if(which!=26) {
		assert(exec_pending[0]->objs->nents==(which==6?1:2));
		assert(!wait_calls);
	}
	if(which==20 || which==21) {
		assert(wait_calls==2 && dependency_ticks==(which==20?1100:0));
		assert(explicit.refs==1);
	}
	if(which==10) virtio_gpu_exec_uncharge(&device,VIRTGPU_EXEC_BUDGET);
	if(which==12) {
		exec_finish(0); assert(device.exec_bytes && live);
		assert(!backing[0].exec_pending && !backing[0].dma_members);
	}
	if(which==23) assert(queue_calls==2 && backing[0].pre==2 && backing[0].post==1);
	if(which==25) assert(backing[0].pre==1 && backing[1].pre==1);
	if(which==41) { assert(!backing[0].pre && !backing[1].pre); backing[1].dma_members=0; }
	if(which==46) { assert(!backing[0].pre); backing[1].exec_pending=0; }
	exec_cleanup();
	assert(foreign.refs==1);
}

static int
exec_contract_main(void)
{
	unsigned failed=0, count=sizeof(exec_names)/sizeof(exec_names[0]);
	for(unsigned i=0;i<count;i++) {
		pid_t pid=fork(); int status; assert(pid>=0);
		if(pid==0) { exec_case(i); exit(0); }
		assert(waitpid(pid,&status,0)==pid);
		bool pass=WIFEXITED(status) && WEXITSTATUS(status)==0;
		if(!pass) failed++;
		printf("%s %s\n",pass?"PASS":"FAIL",exec_names[i]); fflush(stdout);
	}
	printf("%u EXEC groups, %u failed\n",count,failed);
	return failed?1:0;
}
