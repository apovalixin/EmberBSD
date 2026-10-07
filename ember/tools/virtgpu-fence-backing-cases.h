/* Origin: EmberBSD; AI-assisted actual emitter/C2 retirement exhaustion cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static void
fence_backing_seal(void)
{
	struct virtio_gpu_fence *f=virtio_gpu_fence_alloc(&gpu);
	struct virtio_gpu_ctrl_hdr h={0};
	assert(f);
	gpu.fence_drv.sync_seq=INT32_MAX;
	assert(backing_actual_fence_emit(&gpu,&h,f)==-EOVERFLOW);
	assert(!f->f.seqno && list_empty(&f->node));
	dma_fence_put(&f->f);
	virtio_gpu_stop(&gpu,-EOVERFLOW);
}
static void
fence_backing_case(unsigned which)
{
	struct virtio_gpu_object *bo=NULL;
	struct virtio_gpu_object_params p=params(true);
	backing_init();
	if(which==6) {
		bo=make_bo(true); gpu.fence_drv.sync_seq=INT32_MAX-1;
		pressure_rejects=1;
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(gpu.fence_drv.sync_seq==INT32_MAX && !gpu.fence_drv.pending);
		assert(!resets && !gpu.fence_drv.stopped && pressure_waits==1);
		assert(rw_pre==4 && rw_post==4 && ids_freed==1);
		fini(); return;
	}
	if(which==0) {
		gpu.fence_drv.sync_seq=INT32_MAX-1;
		assert(virtio_gpu_object_create(&gpu,&p,&bo,NULL)==-EOVERFLOW);
		assert(!bo && create_commands==1 && !backing_commands);
		assert(!rw_pre && !rw_post && !ids_freed && resets);
		virtio_gpu_reset_work(&gpu.reset_work);
		assert(ids_freed==1);
	} else {
	if(which==5) gpu.fence_drv.sync_seq=INT32_MAX-2;
		bo=make_bo(which!=5);
	if(which==1 || which==5) {
			gpu.fence_drv.sync_seq=INT32_MAX;
			drm_gem_object_put_unlocked(&bo->base.base);
			assert(resets && gpu.fence_drv.stop_error==-EOVERFLOW);
			if(which==1) assert(!ids_freed && rw_pre==2 && rw_post==1);
			virtio_gpu_reset_work(&gpu.reset_work);
		} else {
			fence_backing_seal();
			if(which==2) {
				fail_next(1);
				drm_gem_object_put_unlocked(&bo->base.base);
				assert(!ids_freed && bo->release_pending);
			} else if(which==3) {
				hook_case=17; hook_count=0; sync_hook=backing_sync_hook;
			}
			virtio_gpu_reset_work(&gpu.reset_work);
			if(which==3) assert(hook_count==1);
			if(which==4) {
				assert(bo->dma_lease==VIRTGPU_LEASE_CLOSED && !ids_freed);
				drm_gem_object_put_unlocked(&bo->base.base);
				virtio_gpu_reset_work(&gpu.reset_work);
			}
		}
		assert(ids_freed==1 && bos_freed==1);
	if(which!=5) assert(rw_pre==rw_post && rw_pre==2);
	}
	assert(gpu.fence_drv.sync_seq==INT32_MAX && !gpu.fence_drv.pending);
	assert(list_empty(&gpu.fence_drv.fences) && gpu.fence_drv.stop_error==-EOVERFLOW);
	fini();
}
static int
fence_backing_main(void)
{
	static const char *const names[]={"last-ID CREATE then ATTACH rejected before PRE",
	    "zero-ref UNREF exhaustion retains open lease", "UNREF OOM after seal",
	    "zero-ref release during reset POST", "late last release after reset",
	    "ordinary 2D control uses same exhaustion boundary",
	    "last safe UNREF retries with balanced lease and token"};
	unsigned failed=0;
	for(unsigned i=0;i<7;i++) {
		int result; pid_t pid=fork(); assert(pid>=0);
		if(!pid) { fence_backing_case(i); exit(0); }
		assert(waitpid(pid,&result,0)==pid);
		bool pass=WIFEXITED(result) && !WEXITSTATUS(result);
		failed+=!pass; printf("%s %s\n",pass?"PASS":"FAIL",names[i]); fflush(stdout);
	}
	printf("7 fence/backing groups, %u failed\n",failed);
	return failed?1:0;
}
