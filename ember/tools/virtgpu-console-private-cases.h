/* Origin: EmberBSD; AI-assisted console probe creation contract seams. */
/* SPDX-License-Identifier: BSD-2-Clause */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "virtgpu_limits.h"
#define container_of(p,t,m) ((t *)((char *)(p)-offsetof(t,m)))
#define GFP_KERNEL 0
#define DRM_FORMAT_HOST_XRGB8888 1
struct drm_framebuffer { int unused; };
struct drm_device { int unused; };
struct drm_fb_helper { struct drm_device *dev; struct drm_framebuffer *fb; };
struct drm_fb_helper_surface_size { unsigned surface_width,surface_height,surface_bpp,surface_depth; };
struct virtio_gpu_device { int unused; };
struct virtgpu_console {
    struct drm_fb_helper helper; struct virtio_gpu_device *vgdev;
    struct drm_fb_helper_surface_size sizes; size_t size; void *shadow;
};
struct virtio_gpu_object_params { uint32_t width,height,format; size_t size; bool dumb,private_console; };
struct virtio_gpu_object { struct { struct { int unused; } base; } base; void *dma_vaddr; };
struct virtio_gpu_framebuffer { struct drm_framebuffer base; };
struct drm_mode_fb_cmd2 { uint32_t width,height,pitches[4],pixel_format; };
static unsigned allocation,fail_at,creates,puts_count;
static int create_error,fb_error;
static unsigned char pixels[4096];
static struct virtio_gpu_object console_bo;
static void *kzalloc(size_t n,int flags) { if(++allocation==fail_at)return NULL;return calloc(1,n); }
#define kfree free
static uint32_t virtio_gpu_translate_format(uint32_t f) { return f; }
static int virtio_gpu_object_create(struct virtio_gpu_device *v,
    struct virtio_gpu_object_params *p,struct virtio_gpu_object **out,void *f) {
    assert(p->private_console && p->dumb && p->size==4096 && p->width==32 && p->height==32);
    assert(!f);creates++;
    if(create_error)return create_error;
    /* The real pre-ATTACH clear is checked in the finite backing fixture.
     * Return a marker to detect ANY late CPU store by the real probe. */
    memset(pixels,0x85,sizeof(pixels));console_bo.dma_vaddr=pixels;*out=&console_bo;return 0;
}
static int virtio_gpu_framebuffer_init(struct drm_device *d,struct virtio_gpu_framebuffer *fb,
    struct drm_mode_fb_cmd2 *mode,void *bo) { assert(mode->pitches[0]==128 && bo==&console_bo.base.base);return fb_error; }
static void drm_gem_object_put_unlocked(void *bo) { assert(bo==&console_bo.base.base);puts_count++; }
static int virtgpu_console_probe(struct drm_fb_helper *,struct drm_fb_helper_surface_size *);
static int console_private_cases(void)
{
    struct virtio_gpu_device gpu={0};struct drm_device drm={0};
    for(unsigned i=0;i<5;i++) {
        struct virtgpu_console vc={.helper={.dev=&drm},.vgdev=&gpu};
        struct drm_fb_helper_surface_size sizes={.surface_width=32,.surface_height=32};
        allocation=creates=puts_count=0;fail_at=i==1?1:i==3?2:0;
        create_error=i==2?-EINTR:0;fb_error=i==4?-EIO:0;
        int ret=virtgpu_console_probe(&vc.helper,&sizes);
        assert(ret==(i==0?0:i==2?-EINTR:i==4?-EIO:-ENOMEM));
        assert(creates==(i==1?0U:1U));assert(puts_count==(i>=3?1U:0U));
        if(i==0) {
            for(unsigned j=0;j<sizeof(pixels);j++) assert(pixels[j]==0x85);
            assert(vc.helper.fb && sizes.surface_bpp==32 && sizes.surface_depth==24);
            free(vc.helper.fb);free(vc.shadow);
        } else assert(!vc.shadow);
    }
    puts("5 production private-console creation and unwind groups passed");return 0;
}
