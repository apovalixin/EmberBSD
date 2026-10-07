/* Origin: EmberBSD; AI-assisted production backing lease and retirement cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static void backing_init(void) {
	init();
	backing_vq.num_free = 32;
#ifdef FENCE_BACKING_CONTRACT
	gpu.fence_drv.limit=32;
	INIT_LIST_HEAD(&gpu.fence_drv.fences);
#endif
	gpu.ctrlq.vq = &backing_vq;
	ops.del_vqs = backing_del_vqs;
	pressure_rejects = pressure_waits = joined = terminal = fixture_ticks = 0;
	immediate = NULL; callback_checks = 0;
}
static void backing_reset(void) {
	virtio_gpu_stop(&gpu, -EIO);
	virtio_gpu_reset_work(&gpu.reset_work);
	assert(!atomic_read(&gpu.submitters));
}
static unsigned hook_case, hook_count;
static void backing_sync_hook(int flags) {
	/* Production sync must hold none of the DMA/queue/fence spin locks. */
	assert(pthread_mutex_trylock(&gpu.dma_lock.value) == 0);
	assert(pthread_mutex_unlock(&gpu.dma_lock.value) == 0);
	assert(pthread_mutex_trylock(&gpu.ctrlq.qlock.value) == 0);
	assert(pthread_mutex_unlock(&gpu.ctrlq.qlock.value) == 0);
	if (hook_case == 15 && rw_pre == 1 && !rw_post) {
		hook_count++;
		sync_hook = NULL;
		virtio_gpu_stop(&gpu, -EIO);
	} else if (hook_case == 16 && rw_pre == 2 && !rw_post) {
		hook_count++;
		sync_hook = NULL;
		virtio_gpu_stop(&gpu, -EIO);
	} else if (hook_case == 17 && rw_post == 2) {
		hook_count++;
		sync_hook = NULL;
#ifdef DMA_LEASE_SOURCE
		assert(last_bo->dma_lease == VIRTGPU_LEASE_CLOSING);
		assert(last_bo->dma_retire_refs == 1);
#endif
		/* Zero-ref destructor arrives while reset owns the raw POST pin. */
		drm_gem_object_put_unlocked(&last_bo->base.base);
		assert(!ids_freed && last_bo);
#ifdef DMA_LEASE_SOURCE
		assert(last_bo->release_pending);
#endif
	} else if ((hook_case == 18 && rw_post == 1) || (hook_case == 23 && rw_post == 2)) {
		hook_count++;
		sync_hook = NULL;
		virtio_gpu_stop(&gpu, -EIO);
	}
}
static void backing_case(unsigned which) {
	struct virtio_gpu_object *bo = NULL;
	struct virtio_gpu_object_params p = params(true);
	backing_init();
	if (which == 0) {
		bo = make_bo(true);
		assert(rw_pre == 2 && rw_post == 1 && !backing_pre && !backing_post && callback_checks == 1);
#ifdef DMA_LEASE_SOURCE
		assert(bo->dma_lease == VIRTGPU_LEASE_OPEN && !bo->dma_members);
#endif
		/* A retained host write remains possible after short token POST. */
		assert(host_backing[0] && pins == 1 && refs(bo) == 1);
		*(unsigned char *)bo->dma_vaddr = 0x5a;
		assert(*(unsigned char *)bo->dma_vaddr == 0x5a);
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(rw_pre == 3 && rw_post == 3 && ids_freed == 1);
	} else if (which == 1) {
		/* Command, array, wait and fence allocation all precede PRE. */
		fini();
		for (unsigned n = 9; n <= 12; n++) {
			backing_init();
			fail_next(n);
			assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ENOMEM);
			assert(!bo && !rw_pre && !rw_post && !backing_post);
			assert(ids_freed == 1);
			fini();
		}
		return;
	} else if (which == 2) {
		/* A rejected attempt POST is balanced before the retry PRE. */
		bo = make_bo(true);
		pressure_rejects = 1;
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(pressure_waits == 1 && !resets && rw_pre == 4 && rw_post == 4);
	} else if (which >= 3 && which <= 8) {
		enum response_mode m = (enum response_mode)(which - 2);
		set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, m);
		submission_error = -ENOMEM;
		wait_msec = 1;
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) < 0 && !bo);
#ifdef DMA_LEASE_SOURCE
		assert(last_bo && !last_bo->dma_eligible && last_bo->dma_required);
		assert(last_bo->dma_lease == VIRTGPU_LEASE_OPEN);
#endif
		assert(!ids_freed && pins && maps && !backing_post);
		backing_reset();
		assert(rw_pre == 2 && rw_post == 2 && ids_freed == 1);
	} else if (which == 9) {
		set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, SUBMIT_FAIL);
		submission_error = -ENOMEM;
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ENOMEM);
		/* A rejected ATTACH still retires the ACKed CREATE by fenced UNREF. */
		assert(!bo && rw_pre == 3 && rw_post == 3 && ids_freed == 1);
	} else if (which == 10) {
		bo = make_bo(true);
		/* No GEM ref in registry: original ref reaches zero and emits UNREF. */
		set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF, HOLD);
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(pending && refs(bo) == 0 && !ids_freed && rw_pre == 3 && rw_post == 1);
		backing_reset();
		assert(rw_post == 3 && ids_freed == 1);
	} else if (which == 11 || which == 12) {
		bo = make_bo(true);
		fail_next(which == 11 ? 1 : 2); /* UNREF command / fence. */
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(resets && !ids_freed && rw_pre == 2 && rw_post == 1);
		backing_reset();
		assert(rw_post == 2 && ids_freed == 1);
	} else if (which == 13) {
		bo = make_bo(true);
		backing_reset();
		assert(rw_post == 2 && !ids_freed && refs(bo) == 1);
		backing_reset();
		assert(rw_post == 2);
		/* Late zero-ref UNREF after the entire reset pass. */
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(ids_freed == 1 && rw_post == 2 && rw_pre == 2);
	} else if (which == 14) {
		gpu.has_virgl_3d = false;
		bo = make_bo(false);
		assert(backing_pre == 1 && !rw_pre);
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(backing_post == 1 && !rw_post && ids_freed == 1);
	} else if (which == 15 || which == 16 || which == 18) {
		hook_case = which;
		hook_count = 0;
		sync_hook = backing_sync_hook;
		int ret = virtio_gpu_object_create(&gpu, &p, &bo, NULL);
		assert(which == 18 ? ret == 0 && bo : ret < 0 && !bo);
		assert(hook_count == 1 && !ids_freed && rw_pre == 2 && rw_post == 1);
		backing_reset();
		if (bo) drm_gem_object_put_unlocked(&bo->base.base);
		assert(rw_post == 2 && ids_freed == 1);
	} else if (which == 17) {
		bo = make_bo(true);
		hook_case = which;
		hook_count = 0;
		sync_hook = backing_sync_hook;
		backing_reset();
		assert(hook_count == 1 && ids_freed == 1 && rw_post == 2);
	} else if (which == 19) {
		bo = make_bo(true);
#ifdef DMA_LEASE_SOURCE
		struct virtio_gpu_vbuffer vbuf = {.vgdev = &gpu,
		    .dma_op = {.bo = bo, .kind = VIRTGPU_DMA_ATTACH}};
		assert(virtio_gpu_dma_prepare(&vbuf) == 0);
		virtio_gpu_dma_post(&vbuf);
		virtio_gpu_dma_post(&vbuf);
		virtio_gpu_dma_finish(&vbuf, 0);
		assert(rw_pre == 3 && rw_post == 2 && !bo->dma_members);
#endif
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(ids_freed == 1);
	} else if (which == 20) {
		bo = make_bo(true);
		backing_reset();
		assert(rw_pre == 2 && rw_post == 2);
		/* A stopped submit cannot start another token. */
#ifdef DMA_LEASE_SOURCE
		struct virtio_gpu_vbuffer vbuf = {.vgdev = &gpu,
		    .dma_op = {.bo = bo, .kind = VIRTGPU_DMA_ATTACH}};
		assert(virtio_gpu_dma_prepare(&vbuf) == -ENODEV);
#endif
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(rw_pre == 2 && rw_post == 2 && ids_freed == 1);
	} else if (which == 21) {
		set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, HOLD);
		wait_msec = 1;
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ETIMEDOUT);
		assert(!bo && pending && !ids_freed);
		/* Valid but late response after synchronous transport reset. */
		finish_pending(false);
		assert(!ids_freed && rw_post == 1);
		backing_reset();
		assert(rw_pre == 2 && rw_post == 2 && ids_freed == 1);
	} else if (which == 22) {
		bo = make_bo(true);
		set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF, BAD_TYPE);
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(resets && !ids_freed && rw_pre == 3 && rw_post == 2);
		backing_reset();
		assert(rw_post == 3 && ids_freed == 1);
	} else if (which == 23) {
		bo = make_bo(true);
		hook_case = which;
		hook_count = 0;
		sync_hook = backing_sync_hook;
		drm_gem_object_put_unlocked(&bo->base.base);
		assert(hook_count == 1 && ids_freed == 1 && rw_pre == 3 && rw_post == 3);
		backing_reset();
		assert(rw_post == 3 && ids_freed == 1);
	} else abort();
	fini();
}
#ifdef FENCE_BACKING_CONTRACT
#include "virtgpu-fence-backing-cases.h"
#endif
int main(void) {
#ifdef FENCE_BACKING_CONTRACT
	return fence_backing_main();
#endif
	static const char *const names[] = {
	    "lease survives early successful ATTACH/token POST",
	    "all four ATTACH metadata allocation failures precede PRE",
	    "UNREF ENOSPC closes token before retry without closing lease",
	    "ATTACH error response resets before POST",
	    "ATTACH short response resets before POST",
	    "ATTACH oversized response resets before POST",
	    "ATTACH missing fence resets before POST",
	    "ATTACH wrong fence resets before POST",
	    "lost ATTACH reply retains cookie until reset drain",
	    "rejected ATTACH still retires ACKed CREATE with UNREF",
	    "zero-ref UNREF cookie survives until reset",
	    "UNREF command OOM defers final release until reset POST",
	    "UNREF fence OOM defers final release until reset POST",
	    "late zero-ref UNREF after repeated reset closes once",
	    "required=false preserves legacy 2D phases",
	    "stop during lease PRE rejects before host exposure",
	    "stop during token PRE rejects before host exposure",
	    "zero-ref destructor during reset lease POST defers to pin",
	    "stop during ATTACH token POST joins before reset lease POST",
	    "duplicate token POST is inert and record can finish",
	    "stopped prepare cannot start a new phase",
	    "late valid ATTACH after timeout retains lease until reset drain",
	    "UNREF error POST precedes reset lease closure and free",
	    "stop during successful UNREF POST cannot finalize twice"
	};
	unsigned failed = 0, skipped = 0, count = sizeof(names) / sizeof(names[0]);
	for (unsigned i = 0; i < count; i++) {
#ifndef DMA_LEASE_SOURCE
		if (i == 19) { puts("SKIP new token-state helper absent from baseline"); skipped++; fflush(stdout); continue; }
#endif
		int result;
		pid_t pid = fork();
		assert(pid >= 0);
		if (!pid) { backing_case(i); exit(0); }
		assert(waitpid(pid, &result, 0) == pid);
		bool ok = WIFEXITED(result) && !WEXITSTATUS(result);
		failed += !ok;
		printf("%s %s\n", ok ? "PASS" : "FAIL", names[i]);
		fflush(stdout);
	}
	printf("%u backing groups, %u failed, %u skipped\n", count, failed, skipped);
	return failed ? 1 : 0;
}
