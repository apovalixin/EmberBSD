/* Origin: EmberBSD; AI-assisted production DMA admission and cleanup cases. */
/* SPDX-License-Identifier: BSD-2-Clause */
static void
admission_case(unsigned int which)
{
	struct virtio_gpu_object *bo = NULL;
	struct virtio_gpu_object_params p = params(which != 1 && which != 2);
	struct drm_file f;
	uint32_t h = 0;

	p.dumb = which == 1 || which == 2;
	init();
	if (which <= 4) {
		if (which <= 2)
			eligibility_error = -EOPNOTSUPP;
		if (which == 2) {
			gpu.has_virgl_3d = false;
			assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == 0);
			assert(!eligibility_checks && backing_pre == 1);
			drm_gem_object_put_unlocked(&bo->base.base);
			assert(backing_post == 1);
		} else {
			if (which == 3)
				long_segment = true;
			if (which == 4)
				fail_next(8); /* Wire entries allocation, before PRE. */
			set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF, HOLD);
			assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) ==
			    (which < 2 ? -EOPNOTSUPP : which == 3 ? -EMSGSIZE : -ENOMEM));
			assert(!bo && !backing_pre && !backing_post && !backing_commands);
			assert(!attach_commands && !pins && !vmaps && !maps);
			assert(pending && !ids_freed && host_resource[0]);
			finish_pending(false);
			assert(ids_freed == 1 && unrefs == 1);
		}
		fini();
		return;
	}
	if (which == 5) {
		fini();
		for (enum response_mode m = BAD_TYPE; m <= SUBMIT_FAIL; m++) {
			init();
			set_fault(VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING, m);
			submission_error = -ENOMEM;
			if (m == HOLD)
				wait_msec = 40;
			assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) < 0);
			assert(!bo && backing_pre == 1 && backing_post == 1);
			assert(ids_freed == 1 && (unrefs == 1 || resets));
			if (m == HOLD)
				finish_pending(false);
			fini();
		}
		return;
	}
	if (which == 10) {
		fail_next(9); /* Command allocation after PRE, before queueing ATTACH. */
		set_fault(VIRTIO_GPU_CMD_RESOURCE_UNREF, HOLD);
		assert(virtio_gpu_object_create(&gpu, &p, &bo, NULL) == -ENOMEM);
		assert(!bo && pending && backing_pre == 1 && !backing_post);
		assert(pins == 1 && maps == 1 && vmaps == 1 && !ids_freed);
#ifdef DMA_ELIGIBILITY_SOURCE
		assert(last_bo && !last_bo->dma_eligible);
#endif
		finish_pending(false);
		assert(backing_post == 1 && ids_freed == 1);
		fini();
		return;
	}
	file_init(&f);

	if (which == 9)
		gpu.has_virgl_3d = false;
	bo = make_bo(which != 9);
	gpu.has_virgl_3d = true;
	if (which == 6) {
		struct virtio_gpu_attachment *entry;
		assert(drm_gem_handle_create(&f, &bo->base.base, &h) == 0);
		entry = container_of(f.driver_priv->attachments.next,
		    struct virtio_gpu_attachment, node);
		assert(entry->handles == 1);
		bo->base.base.import_attach = bo;
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		assert(entry->handles == 1 && attach_commands == 1);
		bo->base.base.import_attach = NULL;
	} else if (which == 7) {
		bo->base.base.import_attach = bo;
		dmabuf.obj = &bo->base.base;
		dmabuf.refs = 1;
		assert(drm_gem_prime_fd_to_handle(&dev, &f, 7, &h) == -EOPNOTSUPP);
		assert(!attach_commands && !bo->base.base.handle_count);
		bo->base.base.import_attach = NULL;
	} else if (which == 8) {
#ifdef DMA_ELIGIBILITY_SOURCE
		struct drm_gem_object foreign = {.dev=&dev};
		void *saved = bo->dma_vaddr;
		unsigned int checks = eligibility_checks;
		assert(!virtio_gpu_object_dma_admitted(&gpu, &foreign));
		assert(virtio_gpu_object_dma_check(&gpu, &foreign, NULL, 2) == -EOPNOTSUPP);
		bo->dma_vaddr = &foreign;
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		assert(virtio_gpu_object_dma_check(&gpu, &bo->base.base,
		    bo->pages->sgl->sg_dmamap, 2) == -EOPNOTSUPP);
		bo->dma_vaddr = saved;
		bo->base.base.dev = NULL;
		assert(!virtio_gpu_object_dma_admitted(&gpu, &bo->base.base));
		bo->base.base.dev = &dev;
		bo->base.base.gemo_uvmobj.pgops = NULL;
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		bo->base.base.gemo_uvmobj.pgops = &drm_gem_shmem_uvm_ops;
		bo->base.pin_count = 0;
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		bo->base.pin_count = 1;
		bo->base.vmap_count = 0;
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		bo->base.vmap_count = 1;
		bo->base.pages = NULL;
		assert(virtio_gpu_object_dma_check(&gpu, &bo->base.base,
		    bo->pages->sgl->sg_dmamap, 2) == -EOPNOTSUPP);
		bo->base.pages = owned_pages;
		assert(eligibility_checks == checks && !attach_commands);
#endif
	} else if (which == 9) {
		assert(virtio_gpu_gem_object_open(&bo->base.base, &f) == -EOPNOTSUPP);
		assert(!attach_commands && !bo->base.base.handle_count);
	} else {
		abort();
	}
	file_fini(&f);
	drm_gem_object_put_unlocked(&bo->base.base);
	fini();
}

int
main(void)
{
	static const char *const names[] = {
	    "unsuitable 3D before PRE retains CREATE ID until UNREF",
	    "future 3D device also gates nonvirgl/dumb backing",
	    "required false preserves normal 2D backing",
	    "wire length failure before PRE does not POST",
	    "wire allocation failure before PRE does not POST",
	    "after-PRE attach failures preserve POST and UNREF retirement",
	    "duplicate handle checks owned admission before increment",
	    "same-device PRIME new handle cannot bypass admission",
	    "owned wrapper checks core, pager, pin and vmap identity",
	    "unqualified retained backing cannot enter a context",
	    "after-PRE command allocation retains map until UNREF"};
	unsigned int failed = 0, count = sizeof(names) / sizeof(names[0]);

	for (unsigned int i = 0; i < count; i++) {
		int status;
		pid_t pid;
#ifndef DMA_ELIGIBILITY_SOURCE
		if (i == 8) {
			puts("SKIP new owned wrapper absent from baseline");
			fflush(stdout);
			continue;
		}
#endif
		pid = fork();
		assert(pid >= 0);
		if (!pid) {
			admission_case(i);
			exit(0);
		}
		assert(waitpid(pid, &status, 0) == pid);
		if (!WIFEXITED(status) || WEXITSTATUS(status))
			failed++;
		printf("%s %s\n", WIFEXITED(status) && !WEXITSTATUS(status) ?
		    "PASS" : "FAIL", names[i]);
		fflush(stdout);
	}
	printf("%u integration groups, %u failed\n", count, failed);
	return failed ? 1 : 0;
}
