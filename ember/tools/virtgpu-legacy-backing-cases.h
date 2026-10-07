/* Origin: EmberBSD; AI-assisted finite ATTACH/privacy causal regressions. */
/* SPDX-License-Identifier: BSD-2-Clause */
static unsigned legacy_class, legacy_reads, legacy_pre_checks;
static unsigned char legacy_host[8192];
static void legacy_read(void)
{
	assert(!gpu.has_virgl_3d && last_bo && last_bo->dma_vaddr);
	/* Both software and GL CREATE_2D use map-time TO_DEVICE reads. */
	assert(legacy_class<2);
	memcpy(legacy_host,last_bo->dma_vaddr,last_bo->base.base.size);
	for(unsigned i=0;i<sizeof(legacy_host);i++) assert(legacy_host[i]==0);
	legacy_reads++;
}
static void legacy_clear_pre(int flags)
{
	if(flags!=BUS_DMASYNC_PREWRITE) return;
	assert(last_bo && last_bo->dma_vaddr && !legacy_reads);
	for(unsigned i=0;i<last_bo->base.base.size;i++)
		assert(((unsigned char *)last_bo->dma_vaddr)[i]==0);
	legacy_pre_checks++;
}
static void legacy_pressure(int flags)
{
	if(flags==BUS_DMASYNC_PREWRITE && backing_pre==1) {
		pressure_rejects=1; sync_hook=NULL;
	}
}
static void legacy_post_pin(int flags)
{
	if(flags!=BUS_DMASYNC_POSTWRITE) return;
	assert(last_bo && last_bo->dma_members && last_bo->dma_retire_refs);
	assert(last_bo->dma_lease==VIRTGPU_LEASE_NONE && !ids_freed);
}
static void legacy_case(unsigned which)
{
	struct virtio_gpu_object *bo=NULL;
	struct virtio_gpu_object_params p=params(false);
	backing_init();gpu.has_virgl_3d=false;legacy_map_read=NULL;
	p.dumb=true;p.private_console=true;
	if(which<2) {
		legacy_class=which;legacy_reads=legacy_pre_checks=0;
		legacy_map_read=legacy_read;sync_hook=legacy_clear_pre;
		assert(!virtio_gpu_object_create(&gpu,&p,&bo,NULL));
		assert(legacy_reads==1 && legacy_pre_checks==1 && backing_pre==1 && backing_post==1);
		assert(bo->dma_lease==VIRTGPU_LEASE_NONE && !bo->dma_members && maps && pins);
	} else if(which>=2 && which<=3) {
		gpu.has_virgl_3d=which==3;
		assert(!virtio_gpu_object_create(&gpu,&p,&bo,NULL));
		struct drm_file file;file_init(&file);uint32_t handle=0xdead;
		assert(drm_gem_handle_create(&file,&bo->base.base,&handle)==-EACCES);
		assert(handle==0xdead && !file.vmas && !bo->base.base.handle_count && refs(bo)==1);
		dmabuf.obj=&bo->base.base;
		assert(drm_gem_prime_fd_to_handle(&dev,&file,7,&handle)==-EACCES);
		assert(handle==0xdead && !file.prime.buf && refs(bo)==1);
		struct virtio_gpu_object_array *a=virtio_gpu_array_alloc(1);assert(a);
		virtio_gpu_array_add_obj(a,&bo->base.base);
		assert(virtio_gpu_cmd_context_attach_resource(&gpu,1,a)==-EACCES);
		assert(!virtio_gpu_object_dma_admitted(&gpu,&bo->base.base));
		struct virtio_gpu_fpriv priv={0};
		struct virtio_gpu_attachment entry={.obj=&bo->base.base,.handles=1};
		INIT_LIST_HEAD(&priv.attachments);priv.attachment_count=1;
		linux_mutex_init(&priv.attachment_lock);mutex_lock(&priv.attachment_lock);
		list_add_tail(&entry.node,&priv.attachments);
		a=virtio_gpu_operation_array_alloc(&gpu,1,VIRTGPU_OPERATION_EXEC);assert(a);
		assert(virtio_gpu_exec_snapshot(&gpu,&priv,a,NULL)==-EOPNOTSUPP);
		assert(!a->nents);virtio_gpu_array_put_free(a);list_del(&entry.node);
		mutex_unlock(&priv.attachment_lock);linux_mutex_destroy(&priv.attachment_lock);
		assert(!attach_commands && refs(bo)==1);
		assert(bo->dma_lease==(which==3?VIRTGPU_LEASE_OPEN:VIRTGPU_LEASE_NONE));
		file_fini(&file);
	} else if(which>=4 && which<=7) {
		fail_next(which+5); /* command, ATTACH array, wait, fence: 9..12 */
		assert(virtio_gpu_object_create(&gpu,&p,&bo,NULL)==-ENOMEM && !bo);
		assert(!backing_pre && !backing_post && !rw_pre && !rw_post);
	} else if(which==8) {
		sync_hook=legacy_pressure;
		assert(!virtio_gpu_object_create(&gpu,&p,&bo,NULL));
		assert(pressure_waits==1 && backing_pre==2 && backing_post==2);
		assert(bo->dma_lease==VIRTGPU_LEASE_NONE && maps && pins);
	} else if(which>=9 && which<=15) {
		enum response_mode m=which==15?SUBMIT_FAIL:(enum response_mode)(which-8);
		set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING,m);
		submission_error=-EMSGSIZE;wait_msec=1;sync_hook=legacy_post_pin;
		int ret=virtio_gpu_object_create(&gpu,&p,&bo,NULL);
		assert(ret==(which==14?-ETIMEDOUT:which==15?-EMSGSIZE:-EIO) && !bo);
		if(which!=15) backing_reset();
		assert(backing_pre==1 && backing_post==1 && !rw_pre && !rw_post && ids_freed==1);
	} else if(which==16 || which==17) {
		assert(!virtio_gpu_object_create(&gpu,&p,&bo,NULL));
		set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF,HOLD);
		if(which==17) fail_next(2); /* exhausted cleanup metadata path uses reset */
		drm_gem_object_put_unlocked(&bo->base.base);bo=NULL;
		assert(!ids_freed && last_bo && refs(last_bo)==0);
		backing_reset();
		assert(ids_freed==1 && backing_pre==1 && backing_post==1 && !rw_pre && !rw_post);
	} else if(which==18) {
		assert(!virtio_gpu_object_create(&gpu,&p,&bo,NULL));
		struct virtio_gpu_vbuffer b={.vgdev=&gpu,
		 .dma_op={.bo=bo,.kind=VIRTGPU_DMA_ATTACH}};
		assert(!virtio_gpu_dma_prepare(&b));virtio_gpu_dma_post(&b);
		virtio_gpu_dma_post(&b);virtio_gpu_dma_finish(&b,0);
		assert(backing_pre==2 && backing_post==2 && !bo->dma_members);
	} else if(which==19 || which==20) {
		/* ACKed CREATE + ATTACH metadata OOM + cleanup OOM cannot reuse ID. */
		fail_next(9);allocation_fail_again=which==19?10:11;
		assert(virtio_gpu_object_create(&gpu,&p,&bo,NULL)==-ENOMEM && !bo);
		assert(create_commands==1 && !backing_commands && !backing_pre && !backing_post);
		assert(last_bo && refs(last_bo)==0 && !ids_freed && maps && pins);
		backing_reset();
		assert(ids_freed==1 && !maps && !pins && !backing_pre && !backing_post);

} else abort();
	sync_hook=NULL;legacy_map_read=NULL;
	if(bo) drm_gem_object_put_unlocked(&bo->base.base);
	fini();
}
int main(void)
{
	static const char *const names[]={"software initial clear before map-time read",
	 "GL no-VIRGL initial clear before map-time read","legacy private GEM/PRIME/context refusal",
	 "VirGL private refusal retains C2","ATTACH command OOM","ATTACH array OOM",
	 "ATTACH wait OOM","ATTACH fence OOM","ATTACH ENOSPC finite retries",
	 "ATTACH host error","ATTACH short response","ATTACH oversized response",
	 "ATTACH missing fence","ATTACH wrong fence","ATTACH timeout/reset join",
	 "ATTACH EMSGSIZE rejection","zero-ref UNREF/reset retirement","zero-ref cleanup fence OOM",
	 "finite ATTACH duplicate POST","ATTACH metadata plus UNREF command OOM","ATTACH metadata plus UNREF fence OOM"};
	unsigned failed=0;
	for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++) {
		int status;pid_t pid=fork();assert(pid>=0);
		if(!pid){legacy_case(i);exit(0);}
		assert(waitpid(pid,&status,0)==pid);
		bool ok=WIFEXITED(status)&&!WEXITSTATUS(status);failed+=!ok;
		printf("%s %s\n",ok?"PASS":"FAIL",names[i]);fflush(stdout);
	}
	printf("21 finite ATTACH/privacy groups, %u failed\n",failed);
	return failed?1:0;
}
