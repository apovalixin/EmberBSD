/* Origin: EmberBSD native VirtGPU attachment, 2026-10-06. */
/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright (c) 2026 EmberBSD contributors. */
#include <sys/cdefs.h>
#include <sys/device.h>
#include <sys/errno.h>
#include <dev/pci/virtiovar.h>
#include <drm/drm_drv.h>
#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include "virtgpu_drv.h"

struct virtiodrm_softc {
	device_t sc_dev;
	struct virtio_device sc_vdev;
	struct drm_device *sc_drm;
};
static int virtiodrm_match(device_t, cfdata_t, void *);
static void virtiodrm_attach(device_t, device_t, void *);
static void virtiodrm_attach_deferred(device_t);
static int virtiodrm_detach(device_t, int);
CFATTACH_DECL_NEW(virtiodrm, sizeof(struct virtiodrm_softc),
    virtiodrm_match, virtiodrm_attach, virtiodrm_detach, NULL);

static int
virtiodrm_match(device_t parent, cfdata_t cf, void *aux)
{
	struct virtio_attach_args *va = aux;

	return va->sc_childdevid == VIRTIO_DEVICE_ID_GPU &&
	    virtio_child(device_private(parent)) == NULL ? 2 : 0;
}

static int
virtiodrm_config_bridge(struct virtio_softc *vsc)
{
	struct virtiodrm_softc *sc = device_private(virtio_child(vsc));

	return linux_virtio_config_interrupt(&sc->sc_vdev);
}

static void
virtiodrm_config_changed(struct virtio_device *vdev)
{
	struct drm_device *dev = vdev->priv;
	struct virtio_gpu_device *vgdev = dev->dev_private;

	schedule_work(&vgdev->config_changed_work);
}

static void
virtiodrm_attach(device_t parent, device_t self, void *aux)
{
	struct virtiodrm_softc *sc = device_private(self);
	struct virtio_softc *vsc = device_private(parent);
	int error;

	aprint_naive("\n");
	aprint_normal(": experimental VirtIO GPU DRM (2D only)\n");
	sc->sc_dev = self;
	/* No VIRGL, EDID, blob or context-init feature is negotiated yet. */
	virtio_child_attach_start(vsc, self, IPL_VM, 0, VIRTIO_COMMON_FLAG_BITS);
	if (!virtio_version_1(vsc)) {
		aprint_error_dev(self, "requires modern VirtIO\n");
		virtio_child_attach_failed(vsc);
		return;
	}
	error = linux_virtio_init(&sc->sc_vdev, vsc, self, IPL_VM,
	    1024 * 1024, virtiodrm_config_bridge, virtiodrm_config_changed,
	    virtio_gpu_cancel_vbuf);
	if (error) {
		aprint_error_dev(self, "transport initialization: %d\n", error);
		virtio_child_attach_failed(vsc);
		return;
	}
	config_interrupts(self, virtiodrm_attach_deferred);
}

static void
virtiodrm_attach_deferred(device_t self)
{
	struct virtiodrm_softc *sc = device_private(self);
	struct drm_device *dev;
	extern int drm_guarantee_initialized(void);
	int ret;

	ret = -drm_guarantee_initialized();
	if (ret)
		goto failed;
	dev = drm_dev_alloc(&virtio_gpu_driver, self);
	if (IS_ERR(dev)) {
		ret = PTR_ERR(dev);
		goto failed;
	}
	dev->bus_dmat = dev->bus_dmat32 = dev->dmat = sc->sc_vdev.dmat;
	sc->sc_vdev.priv = dev;
	ret = drm_dev_set_unique(dev, device_xname(self));
	if (ret)
		goto put;
	ret = virtio_gpu_init(dev, &sc->sc_vdev);
	if (ret)
		goto put;
	ret = drm_dev_register(dev, 0);
	if (ret) {
		virtio_gpu_deinit(dev);
		goto put;
	}
	sc->sc_drm = dev;
	return;
put:
	drm_dev_put(dev);
failed:
	aprint_error_dev(self, "DRM initialization: %d\n", ret);
	linux_virtio_fini(&sc->sc_vdev);
	virtio_child_attach_failed(sc->sc_vdev.native);
}

static int
virtiodrm_detach(device_t self, int flags)
{
	/* Hot removal/unload is deliberately unsupported in this experiment. */
	return EBUSY;
}
