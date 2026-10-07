/* Origin: EmberBSD; AI-assisted private framebuffer publication seams. */
/* SPDX-License-Identifier: BSD-2-Clause */
#define DRIVER_MODESET 2
#define CAP_SYS_ADMIN 1
#define PROT_READ 1
#define PROT_WRITE 2
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#define uvm_object fixture_uvm_object
typedef uint64_t voff_t;
struct file { struct drm_file *f_data; };
struct drm_framebuffer;
struct drm_framebuffer_funcs {
	int (*create_handle)(struct drm_framebuffer *, struct drm_file *, unsigned *);
	void (*destroy)(struct drm_framebuffer *);
	int (*dirty)(void);
};
struct publication_format { unsigned num_planes, depth, cpp[1]; };
struct drm_framebuffer {
	struct drm_device *dev;
	struct drm_gem_object *obj[1];
	const struct drm_framebuffer_funcs *funcs;
	const struct publication_format *format;
	unsigned width, height, pitches[1];
};
struct virtio_gpu_framebuffer { struct drm_framebuffer base; };
struct drm_mode_fb_cmd2 { unsigned width, height, pitches[1]; };
struct drm_mode_fb_cmd { unsigned fb_id, height, width, depth, bpp, pitch, handle; };
struct drm_virtgpu_map { uint32_t handle; uint64_t offset; };
static struct drm_framebuffer *publication_fb;
static struct drm_gem_object *publication_bo;
static struct fixture_uvm_object *publication_mapping;
static bool publication_master;
static unsigned publication_puts;
static int publication_map_status;
static uint64_t publication_offset;
static void drm_gem_fb_destroy(struct drm_framebuffer *fb) { }
static int drm_atomic_helper_dirtyfb(void) { return 0; }
static void drm_helper_mode_fill_fb_struct(struct drm_device *d,
    struct drm_framebuffer *fb, const struct drm_mode_fb_cmd2 *cmd)
{
	static const struct publication_format format = {1,24,{4}};
	fb->dev=d; fb->width=cmd->width; fb->height=cmd->height;
	fb->pitches[0]=cmd->pitches[0]; fb->format=&format;
}
static int drm_framebuffer_init(struct drm_device *d, struct drm_framebuffer *fb,
    const struct drm_framebuffer_funcs *funcs) { fb->funcs=funcs; return 0; }
static struct drm_framebuffer *drm_framebuffer_lookup(struct drm_device *d,
    struct drm_file *f, unsigned id) { return id==7?publication_fb:NULL; }
static void drm_framebuffer_put(struct drm_framebuffer *fb) { publication_puts++; }
static bool drm_is_current_master(struct drm_file *f) { return publication_master; }
static bool capable(int cap) { return false; }
static uint64_t drm_vma_node_offset_addr(struct drm_vma_offset_node *node)
{ assert(node==&publication_bo->vma_node); return PAGE_SIZE; }
static struct drm_vma_offset_node *drm_vma_offset_exact_lookup(void *m,
    unsigned long start, unsigned long pages)
{ return start==1 && pages==1?&publication_bo->vma_node:NULL; }
static bool drm_vma_node_is_allowed(const struct drm_vma_offset_node *node,
    struct drm_file *f) { return node==&publication_bo->vma_node && f->vmas!=0; }
