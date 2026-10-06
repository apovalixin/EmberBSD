/* Origin: EmberBSD native VirtGPU ordinary console, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#include <sys/atomic.h>
#include <sys/device.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/wsdisplay_pci.h>
#include <dev/fdt/simplefbvar.h>
#include <drm/drm_drv.h>
#include <drm/drm_fourcc.h>
#include <drm/drmfb.h>
#include "virtgpu_drv.h"
#include "locators.h"
#include "opt_virtgpu_console.h"

#ifdef VIRTGPU_CONSOLE

struct virtgpu_console {
	struct drm_fb_helper helper;
	struct drm_fb_helper_surface_size sizes;
	struct virtio_gpu_device *vgdev;
	struct drmfb_softc *fbdev;
	struct mutex lock; /* Uploads and master/mode ownership transitions. */
	spinlock_t schedule_lock; /* Stop versus periodic requeue. */
	struct workqueue_struct *wq;
	struct delayed_work work;
	void *shadow;
	size_t size;
	volatile unsigned dirty;
	bool emul, master, restore, stopped;
	int error; /* Nonzero rejects a new graphics master during recovery. */
};

static void virtgpu_console_work(struct work_struct *);

static size_t
virtgpu_console_geometry(uint32_t width, uint32_t height)
{
	/* drmfb's linebytes property is uint16_t; the shadow is XRGB8888. */
	if (width < 32 || width > UINT16_MAX / 4 || height < 32 || height > 8192)
		return 0;
	if ((uint64_t)width * 4 * height > VIRTGPU_MAX_OBJECT_SIZE)
		return 0;
	return (size_t)width * 4 * height;
}

static int
virtgpu_console_probe(struct drm_fb_helper *helper,
    struct drm_fb_helper_surface_size *sizes)
{
	struct virtgpu_console *vc = container_of(helper,
	    struct virtgpu_console, helper);
	struct virtio_gpu_object_params params = { 0 };
	struct drm_mode_fb_cmd2 mode = { 0 };
	struct virtio_gpu_framebuffer *fb;
	struct virtio_gpu_object *bo;
	int error;

	vc->size = virtgpu_console_geometry(sizes->surface_width,
	    sizes->surface_height);
	if (vc->size == 0)
		return -EINVAL;
	vc->shadow = kzalloc(vc->size, GFP_KERNEL);
	if (vc->shadow == NULL)
		return -ENOMEM;
	params.width = sizes->surface_width;
	params.height = sizes->surface_height;
	params.size = vc->size;
	params.dumb = true;
	params.format = virtio_gpu_translate_format(DRM_FORMAT_HOST_XRGB8888);
	error = virtio_gpu_object_create(vc->vgdev, &params, &bo, NULL);
	if (error)
		goto fail_shadow;
	fb = kzalloc(sizeof(*fb), GFP_KERNEL);
	if (fb == NULL) {
		error = -ENOMEM;
		goto fail_bo;
	}
	mode.width = params.width;
	mode.height = params.height;
	mode.pitches[0] = params.width * 4;
	mode.pixel_format = DRM_FORMAT_HOST_XRGB8888;
	error = virtio_gpu_framebuffer_init(helper->dev, fb, &mode, &bo->base.base);
	if (error) {
		kfree(fb);
		goto fail_bo;
	}
	/* object_attach retains the wired pages, vmap and bus_dma map. */
	memset(bo->dma_vaddr, 0, vc->size);
	helper->fb = &fb->base;
	sizes->surface_bpp = 32;
	sizes->surface_depth = 24;
	vc->sizes = *sizes;
	return 0;
fail_bo:
	drm_gem_object_put_unlocked(&bo->base.base);
fail_shadow:
	kfree(vc->shadow);
	vc->shadow = NULL;
	return error;
}

static const struct drm_fb_helper_funcs virtgpu_console_funcs = {
	.fb_probe = virtgpu_console_probe,
};

/* CPU drawing never submits, sleeps, allocates, or touches DMA-owned pages. */
static void
virtgpu_console_damage(struct drmfb_softc *sc)
{
	struct virtgpu_console *vc = container_of(sc->sc_da.da_fb_helper,
	    struct virtgpu_console, helper);

	membar_producer();
	atomic_swap_uint(&vc->dirty, 1);
}

static bool
virtgpu_console_mode(struct drmfb_softc *sc, int mode)
{
	struct virtgpu_console *vc = container_of(sc->sc_da.da_fb_helper,
	    struct virtgpu_console, helper);

	mutex_lock(&vc->lock);
	vc->emul = mode == WSDISPLAYIO_MODE_EMUL;
	vc->restore = vc->emul;
	atomic_swap_uint(&vc->dirty, 1);
	mutex_unlock(&vc->lock);
	return true;
}

static int
virtgpu_console_ioctl(struct drmfb_softc *sc, unsigned long cmd,
    void *data, int flag, struct lwp *l)
{
	device_t gpu = sc->sc_da.da_fb_helper->dev->dev;
	device_t transport = device_parent(gpu);
	device_t bus = device_parent(transport);
	struct pci_softc *psc;

	switch (cmd) {
	case WSDISPLAYIO_GTYPE:
		*(unsigned int *)data = WSDISPLAY_TYPE_GENFB;
		return 0;
	case WSDISPLAYIO_GET_BUSID:
		if (!bus || !device_is_a(bus, "pci"))
			return ENODEV;
		psc = device_private(bus);
		return wsdisplayio_busid_pci(transport, psc->sc_pc,
		    pci_make_tag(psc->sc_pc, psc->sc_bus,
		    device_locator(transport, PCICF_DEV),
		    device_locator(transport, PCICF_FUNCTION)), data);
	case WSDISPLAYIO_SMODE:
		if (*(int *)data != WSDISPLAYIO_MODE_EMUL &&
		    *(int *)data != WSDISPLAYIO_MODE_MAPPED &&
		    *(int *)data != WSDISPLAYIO_MODE_DUMBFB)
			return EINVAL;
		return EPASSTHROUGH;
	default:
		return EPASSTHROUGH;
	}
}

static const struct drmfb_params virtgpu_console_params = {
	.dp_ioctl = virtgpu_console_ioctl,
	.dp_damage = virtgpu_console_damage,
	.dp_setmode = virtgpu_console_mode,
	/* wsdisplay mmap is deliberately absent: all drawing uses the shadow. */
};

static int
virtiodrmfb_match(device_t parent, cfdata_t cf, void *aux)
{
	return 1;
}

static void
virtiodrmfb_attach(device_t parent, device_t self, void *aux)
{
	aprint_naive("\n");
	aprint_normal(": native shadow console\n");
}

static void
virtgpu_console_attach_fb(struct virtgpu_console *vc, device_t self)
{
	struct drmfb_softc *sc = device_private(self);
	struct drmfb_attach_args da = {
		.da_dev = self,
		.da_fb_helper = &vc->helper,
		.da_fb_sizes = &vc->sizes,
		.da_fb_vaddr = vc->shadow,
		.da_fb_linebytes = vc->sizes.surface_width * 4,
		.da_params = &virtgpu_console_params,
	};

	/* Our own CPU shadow is sufficient; avoid a second hidden copy. */
	prop_dictionary_set_bool(device_properties(self), "enable_shadowfb", false);
	vc->fbdev = sc;
	(void)drmfb_attach(sc, &da);
	vc->helper.fbdev = self;
}

CFATTACH_DECL_NEW(virtiodrmfb, sizeof(struct drmfb_softc),
    virtiodrmfb_match, virtiodrmfb_attach, NULL, NULL);

static int
virtgpu_console_upload(struct virtgpu_console *vc, bool restore)
{
	struct virtio_gpu_object *bo =
	    gem_to_virtio_gpu_obj(vc->helper.fb->obj[0]);
	struct virtio_gpu_object_array *objs;
	int error;

	/*
	 * Consume damage BEFORE copying. Concurrent shadow writes may tear this
	 * snapshot, but their trailing notification guarantees another upload.
	 * Only this worker writes the separate DMA mapping, never CPU rasops.
	 */
	if (!atomic_swap_uint(&vc->dirty, 0) && !restore)
		return 0;
	membar_consumer();
	memcpy(bo->dma_vaddr, vc->shadow, vc->size);
	objs = virtio_gpu_array_alloc(1);
	if (objs == NULL)
		return -ENOMEM;
	virtio_gpu_array_add_obj(objs, &bo->base.base);
	error = virtio_gpu_cmd_transfer_to_host_2d(vc->vgdev, 0,
	    bo->width, bo->height, 0, 0, objs, NULL);
	if (error) {
		/* Submission failed or reset ended host access; close PREWRITE. */
		bus_dmamap_sync(vc->vgdev->vdev->dmat,
		    bo->pages->sgl->sg_dmamap, 0, bo->base.base.size,
		    BUS_DMASYNC_POSTWRITE);
		return error;
	}
	if (restore) {
		error = virtio_gpu_cmd_set_scanout(vc->vgdev, 0,
		    bo->hw_res_handle, bo->width, bo->height, 0, 0);
		if (error)
			return error;
	}
	return virtio_gpu_cmd_resource_flush(vc->vgdev, bo->hw_res_handle,
	    0, 0, bo->width, bo->height);
}

static void
virtgpu_console_work(struct work_struct *work)
{
	struct virtgpu_console *vc = container_of(work,
	    struct virtgpu_console, work.work);
	struct virtio_gpu_device *vgdev = vc->vgdev;
	int error = 0;
	unsigned delay = 20;

	mutex_lock(&vc->lock);
	if (vc->emul && !vc->master && READ_ONCE(vgdev->vqs_ready)) {
		if (vc->restore) {
			vgdev->submit_error = 0;
			error = drm_fb_helper_restore_fbdev_mode_unlocked(&vc->helper);
			if (!error)
				error = READ_ONCE(vgdev->submit_error);
			atomic_swap_uint(&vc->dirty, 1);
		}
		if (!error)
			error = virtgpu_console_upload(vc, vc->restore);
		if (!error)
			vc->restore = false;
		vc->error = error;
		if (error) {
			vc->restore = true;
			atomic_swap_uint(&vc->dirty, 1);
		}
	}
	mutex_unlock(&vc->lock);
	if (error == -ENOMEM || error == -EAGAIN || error == -ENOSPC) {
		/* Retain native ownership; retry transient allocations at 1 Hz. */
		delay = 1000;
	} else if (error) {
		/* Retain the framebuffer and wired BO after reset; no stale reuse. */
		virtio_gpu_stop(vgdev, error);
		return;
	}
	spin_lock(&vc->schedule_lock);
	if (!vc->stopped)
		queue_delayed_work(vc->wq, &vc->work, msecs_to_jiffies(delay));
	spin_unlock(&vc->schedule_lock);
}

int
virtgpu_console_master_set(struct drm_device *dev, struct drm_file *file,
    bool new_master)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtgpu_console *vc = vgdev->console;

	if (vc != NULL) {
		/* Wait for any upload/restore before granting userspace ownership. */
		mutex_lock(&vc->lock);
		if (!READ_ONCE(vgdev->vqs_ready) || vc->error) {
			int error = vc->error ? vc->error : -ENODEV;
			mutex_unlock(&vc->lock);
			return error;
		}
		vc->master = true;
		mutex_unlock(&vc->lock);
	}
	return 0;
}

void
virtgpu_console_master_drop(struct drm_device *dev, struct drm_file *file)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtgpu_console *vc = vgdev->console;

	if (vc != NULL) {
		mutex_lock(&vc->lock);
		vc->master = false;
		vc->restore = true;
		mutex_unlock(&vc->lock);
	}
}

void
virtgpu_console_lastclose(struct drm_device *dev)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtgpu_console *vc = vgdev->console;

	if (vc != NULL && vc->fbdev != NULL) {
		KERNEL_LOCK(1, NULL);
		genfb_restore_console(&vc->fbdev->sc_genfb);
		KERNEL_UNLOCK_ONE(NULL);
	}
}

void
virtgpu_console_stop(struct virtio_gpu_device *vgdev)
{
	struct virtgpu_console *vc = vgdev->console;

	if (vc != NULL) {
		spin_lock(&vc->schedule_lock);
		vc->stopped = true;
		spin_unlock(&vc->schedule_lock);
	}
}

void
virtgpu_console_drain(struct virtio_gpu_device *vgdev)
{
	struct virtgpu_console *vc = vgdev->console;

	/* Called only by reset cleanup, after waiters have been woken. */
	if (vc != NULL)
		cancel_delayed_work_sync(&vc->work);
}

static bool
virtgpu_console_fallback_safe(struct virtio_gpu_device *vgdev)
{
	/* A submitted scanout can take effect even when its reply is lost. */
	return !vgdev->console_takeover;
}

int
virtgpu_console_init(struct drm_device *dev)
{
	struct virtio_gpu_device *vgdev = dev->dev_private;
	struct virtgpu_console *vc;
	bool selected = false;
	device_t child;
	int error;

	prop_dictionary_get_bool(device_properties(device_parent(dev->dev)),
	    "is_console", &selected);
	if (vgdev->num_scanouts != 1 || !selected ||
	    !simplefb_console_reserve(dev->dev))
		return -ENODEV;
	prop_dictionary_set_bool(device_properties(dev->dev), "is_console", true);
	vc = kzalloc(sizeof(*vc), GFP_KERNEL);
	if (vc == NULL)
		return -ENOMEM;
	vc->vgdev = vgdev;
	vc->emul = true;
	linux_mutex_init(&vc->lock);
	spin_lock_init(&vc->schedule_lock);
	INIT_DELAYED_WORK(&vc->work, virtgpu_console_work);
	vc->wq = alloc_ordered_workqueue("virtgpucon", 0);
	if (vc->wq == NULL) {
		error = -ENOMEM;
		goto free;
	}
	drm_fb_helper_prepare(dev, &vc->helper, &virtgpu_console_funcs);
	error = drm_fb_helper_init(dev, &vc->helper, 1);
	if (error)
		goto destroy;
	vgdev->console_preparing = true;
	error = drm_fb_helper_initial_config(&vc->helper, 32);
	if (error || vc->helper.fb == NULL) {
		if (!error)
			error = -ENODEV;
		goto fini;
	}
	child = config_found(dev->dev, vc, NULL, CFARGS(.iattr = "virtiodrmfbbus"));
	if (child == NULL) {
		error = -ENODEV;
		goto fini;
	}
	error = drm_fb_helper_restore_fbdev_mode_unlocked(&vc->helper);
	if (!error)
		error = READ_ONCE(vgdev->submit_error);
	if (!READ_ONCE(vgdev->vqs_ready) && !error)
		error = -ENODEV;
	if (error && virtgpu_console_fallback_safe(vgdev))
		goto fini;
	/*
	 * Once SET_SCANOUT could have reached the host, firmware fallback is
	 * unproven. Keep the native framebuffer/console and retry its upload.
	 */
	vc->error = error;
	vgdev->console_preparing = false;
	if (error)
		aprint_error_dev(dev->dev,
		    "console takeover incomplete (%d); retaining native recovery\n",
		    error);
	/* Final wsdisplay publication also commits native recovery ownership. */
	virtgpu_console_attach_fb(vc, child);
	simplefb_console_commit(dev->dev);
	vgdev->console = vc;
	if (!READ_ONCE(vgdev->vqs_ready))
		virtgpu_console_stop(vgdev);
	atomic_swap_uint(&vc->dirty, 1);
	spin_lock(&vc->schedule_lock);
	if (!vc->stopped)
		queue_delayed_work(vc->wq, &vc->work, 0);
	spin_unlock(&vc->schedule_lock);
	return 0;
fini:
	vgdev->console_preparing = false;
	/* No wsdisplay or worker owns these objects on pre-takeover failure. */
	drm_fb_helper_fini(&vc->helper);
	if (vc->helper.fb != NULL)
		drm_framebuffer_put(vc->helper.fb);
	kfree(vc->shadow);
destroy:
	destroy_workqueue(vc->wq);
free:
	spin_lock_destroy(&vc->schedule_lock);
	linux_mutex_destroy(&vc->lock);
	kfree(vc);
	return error;
}
#else
int
virtgpu_console_init(struct drm_device *dev)
{
	return -ENODEV;
}

int
virtgpu_console_master_set(struct drm_device *dev, struct drm_file *file,
    bool new_master)
{
	return 0;
}

void virtgpu_console_master_drop(struct drm_device *dev, struct drm_file *file) { }
void virtgpu_console_lastclose(struct drm_device *dev) { }
void virtgpu_console_stop(struct virtio_gpu_device *vgdev) { }
void virtgpu_console_drain(struct virtio_gpu_device *vgdev) { }
#endif
