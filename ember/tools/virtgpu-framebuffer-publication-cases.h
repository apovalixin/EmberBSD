/* Origin: EmberBSD; AI-assisted actual GETFB/core private-publication regression. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* At real core VMA allow, interleave real MAP and native locked mmap before
 * the real .open callback. Permission/IDR/offset lookup are controlled seams;
 * mmap's persistent GEM/UVM reference is taken by the actual native function. */
static void
publication_interleave(struct drm_file *f)
{
	struct drm_virtgpu_map args = {.handle=1};
	struct file fp = {.f_data=f};
	voff_t offset;
	publication_map_status=virtio_gpu_map_ioctl(&dev,&args,f);
	assert(publication_map_status==0);
	publication_offset=args.offset;
	mutex_lock(&dev.struct_mutex);
	assert(drm_gem_mmap_object_locked(&dev,args.offset,PAGE_SIZE,
	    PROT_READ|PROT_WRITE,&publication_mapping,&offset,&fp)==0);
	mutex_unlock(&dev.struct_mutex);
	assert(publication_mapping==&publication_bo->gemo_uvmobj && offset==0);
}
static void
publication_case(unsigned which)
{
	init();
	bool virgl=(which&1)!=0, private=which<2 || which>=4;
	gpu.has_virgl_3d=virgl;
	linux_mutex_init(&dev.struct_mutex);
	driver.gem_uvm_ops=&drm_gem_shmem_uvm_ops;
	struct drm_file file;
	file_init(&file);
	struct virtio_gpu_object_params p=params(false);
	p.dumb=true; p.private_console=private;
	struct virtio_gpu_object *bo=NULL;
	assert(virtio_gpu_object_create(&gpu,&p,&bo,NULL)==0);
	publication_bo=&bo->base.base;
	struct virtio_gpu_framebuffer fb={0};
	struct drm_mode_fb_cmd2 cmd={32,32,{128}};
	assert(virtio_gpu_framebuffer_init(&dev,&fb,&cmd,publication_bo)==0);
	publication_fb=&fb.base; publication_mapping=NULL;
	publication_idr=publication_allow=publication_puts=0;
	publication_map_status=-ENOENT; publication_offset=0;
	publication_master=which<4; publication_watch=true;
	struct drm_mode_fb_cmd args={.fb_id=7,.handle=0xdead};
	int ret=drm_mode_getfb(&dev,&args,&file);
	publication_watch=false;
	bool denied=private && publication_master;
	if(denied) {
		/* A final EACCES/empty IDR is insufficient: test transient effects. */
		printf("private mode=%u ret=%d IDR=%u VMA=%u MAP=%d mapping=%u refs=%u\n",
		    virgl,ret,publication_idr,publication_allow,publication_map_status,
		    publication_mapping!=NULL,refs(bo));fflush(stdout);
		assert(ret==-EACCES && args.handle==0xdead && !file.vmas);
		assert(!publication_idr && !publication_allow && !publication_mapping);
		assert(publication_map_status==-ENOENT && !publication_offset);
		assert(refs(bo)==1 && !bo->base.base.handle_count);
	} else if(!private) {
		assert(ret==0 && args.handle==1 && publication_idr==1 && publication_allow==1);
		assert(publication_mapping && file.vmas==1 && publication_offset==PAGE_SIZE);
		assert(drm_gem_handle_delete(&file,args.handle)==0 && !file.vmas);
	} else {
		assert(ret==0 && args.handle==0 && !publication_idr && !publication_allow);
	}
	assert(publication_puts==1);
	publication_master=false;
	/* Mapping reference outlives handle deletion and master drop in ordinary
	 * path; failed private publication must never obtain such a reference. */
	if(publication_mapping) {
		assert(publication_mapping==&bo->base.base.gemo_uvmobj && refs(bo)==2);
		drm_gem_object_put_unlocked(&bo->base.base);
	}
	file_fini(&file);
	drm_gem_object_put_unlocked(&bo->base.base);
	linux_mutex_destroy(&dev.struct_mutex);
	fini();
}
int
main(void)
{
	resource_contract_main();
	fflush(NULL);
	const char *names[]={"private GETFB non-VIRGL before publication",
	    "private GETFB VirGL before publication","ordinary GETFB non-VIRGL",
	    "ordinary GETFB VirGL","private non-master non-VIRGL",
	    "private non-master VirGL"};
	unsigned failed=0;
	for(unsigned i=0;i<6;i++) {
		int status;pid_t pid=fork();assert(pid>=0);
		if(!pid){publication_case(i);exit(0);}
		assert(waitpid(pid,&status,0)==pid);
		bool ok=WIFEXITED(status)&&!WEXITSTATUS(status);failed+=!ok;
		printf("%s %s\n",ok?"PASS":"FAIL",names[i]);fflush(stdout);
	}
	printf("6 framebuffer publication groups, %u failed; 8 existing callback groups passed\n",failed);
	return failed?1:0;
}
