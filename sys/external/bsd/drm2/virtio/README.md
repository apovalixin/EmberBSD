<!-- Origin: EmberBSD native experimental VirtGPU integration, 2026-10-06. -->
# Experimental native VirtGPU

`EMBERGPU` replaces the existing framebuffer-only `viogpu` attachment with
native `virtiodrm` DRM/KMS. The ordinary `EMBER64` configuration is unchanged.
This is an experimental 2D integration, not a claim of a validated graphics
session. On 2026-10-06, revision `67a611acb9b` booted under UTM 4.7.5 with
virtio-ramfb and GL disabled. Native PCI discovery, 32 cross-process dumb
GEM/PRIME mapping lifetimes (one page and 8 MiB), malformed size/handle
rejection and visible 800x600 KMS color bars passed. labwc/Pixman displayed
a native Kate window without Xorg, but keyboard/pointer integration still
needs correction. This does not establish VirGL, reliable input, reset
stress or physical-board support. Retest against each exact kernel and
virtual hardware configuration.
The configuration disables default module autoload. Its VirtGPU, DRM,
Linux compatibility and VirtIO dependencies are built into the kernel;
do not load modules from a different build during the experiment.

The attachment requires modern VirtIO and negotiates no optional GPU
features. VIRGL, EDID, blob resources and context-init remain disabled.
The standard VirtGPU ioctl numbers and permission flags are retained;
unsupported 3D requests fail through their existing feature checks.
The retained 3D code needs native error propagation and readback DMA
preparation before VIRGL can be enabled and tested.

## Provenance

The existing `dist/drm/virtio` import matches Linux `v5.6` after removing
NetBSD RCS/cdefs metadata and blank lines, except the original object file.
That file used a plain integer in place of upstream's atomic resource-ID
counter and omitted cached-map initialization. Native IDA allocation and
cached UVM mapping now supply those contracts.

The comparison covered Kconfig, Makefile, debugfs, display, driver, header,
fence, GEM, ioctl, KMS, plane, PRIME, queue and object sources. Original
licences, authors and NetBSD identifiers remain in place. Adaptations are
marked with an Origin line; this is not a new BSD licence on imported code.

The added `include/linux/virtio_gpu.h` is the BSD-licensed
[Linux v5.6 protocol header](https://github.com/torvalds/linux/blob/v5.6/include/uapi/linux/virtio_gpu.h).
It has only an additional Origin comment. The original file's SHA256 is
`1183f4b9eea4a8048ec704bfb8c578c00ab26d9e8157d799c88a1d6e161b9da6`.
The native GEM/UVM helper and local wait/limit adapters are newly written
BSD-licensed code. They do not import Linux's GPL shmem helper.

## Native ownership

The UVM helper wires native GEM pages until the final host-resource release.
Its pager takes ordinary GEM references and maps those pages. Native
`drm_gem_put_pages` unwires pages but does not free the page-pointer array;
the helper separately frees that allocation, including its error unwind.

Backing entries come from a retained linear `bus_dmamap_load` map's
`dm_segs`, rather than raw physical addresses. 2D host reads pair PREWRITE
with POSTWRITE; they do not invalidate or copy back CPU memory with POSTREAD.
The DMA map and wire outlive a GPU-fenced RESOURCE_UNREF response. A failed
UNREF allocation/submission or invalid response resets the device before
local backing release. Disabling submissions alone never permits release.

All synchronous control operations request and validate a GPU fence,
including create, attach, transfer, scanout and flush. A returned descriptor
without the requested fence is an error, not proof that the GPU retired the
command. This follows the
[VirtIO 1.1 GPU completion contract](https://docs.oasis-open.org/virtio/virtio/v1.1/virtio-v1.1.pdf).
Cursor requests retain their GEM object until cursor-ring completion.

Primary-node mappings use native `drm_gem_mmap_object`. The direct native
PRIME hook accepts an object-relative byte offset and acquires exactly one
pager reference. It bypasses the generic PRIME helper's fake-offset hook.
Same-device fd/handle import uses the DRM core self-import path;
cross-device SG imports explicitly fail with ENODEV.

Native system work executes on one ordered worker. Control/cursor ACKs
therefore use a separate completion workqueue, and fatal cleanup has its
own workqueue. Reset stops DMA, fails fences and wakes submission/response
waiters before draining workers and cancelling remaining request cookies.
Normal live detach is rejected with EBUSY.

## Build and checks

On a native NetBSD/aarch64 build host with its normal tool PATH:

```sh
cd sys/arch/evbarm/conf
config EMBERGPU
cd ../compile/EMBERGPU
make depend
make -j8
```

Run `sh ember/tools/virtgpu-contract.sh` and
`sh ember/tools/virtio-transport-contract.sh` from the source root.
The VirtGPU regressions compile production function bodies against
deterministic native-API models. They cover queue errors/pressure, actual
GPU-fence response validation, reset/UNREF lifetime, PRIME offsets/references,
page-array release, cursor failures before fence emission, and stale bounce
copyback. They do not prove actual DMA coherency, UVM/MMU behavior, SMP
interrupt safety, visible scanout or an accelerated userland session.
