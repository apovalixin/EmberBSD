# VirtIO-GPU acceleration for EmberBSD in UTM

Status: revised proposal; no GPU implementation or hardware validation yet.
Reviewed on 2026-10-06 against EmberBSD `7b1c2b3e4e9` and UTM 4.7.5.
Target: the current NetBSD 11/aarch64-based EmberBSD installation.

## Outcome and scope

Run the existing GNOME X11 desktop with rendering executed by the Mac GPU.
The path is Mesa VirGL, the standard VirtGPU DRM ABI, VirtIO, UTM's
virglrenderer, and an accelerated ANGLE backend. Keep one display initially.
Preserve the working framebuffer kernel, display configuration and login.

Native GNOME Wayland remains a subsequent integration result: the installed
Mutter disables Wayland and its native backend. A nested Wayland client does
not establish KMS or GPU support. Physical-board GPUs, Vulkan/Venus, compute,
multi-head and live driver unloading are outside this first result.

## Evidence that changes the first proposal

Linux introduced VirtIO KMS before VirGL 3D. Its established stack separates
kernel buffer/display management from Mesa rendering and desktop integration
([Mesa history](https://docs.mesa3d.org/drivers/virgl.html)). Adopt that order.

FreeBSD [PR 499](https://github.com/freebsd/drm-kmod/pull/499) was closed,
unmerged, in favour of [PR 517](https://github.com/freebsd/drm-kmod/pull/517).
The replacement is still open on the review date. It moves VirtIO compatibility
out of the GPU module after maintainer review. Its author reports UTM/aarch64
VirGL tests; an independent tester reports accelerated Xorg and sway on
amd64/QEMU. These are external results, not EmberBSD validation.

OpenBSD's [viogpu](https://man.openbsd.org/viogpu.4) is a wscons console driver.
Adding framebuffer mmap/damage updates to that design does not supply the
VirtGPU rendering ABI. Keep it as a console reference, not the 3D foundation.

Android's [gfxstream](https://android.googlesource.com/platform/hardware/google/gfxstream/+/refs/heads/main/README.md)
uses a different graphics protocol. [UTM 4.7.5](https://github.com/utmapp/UTM/blob/v4.7.5/Documentation/Graphics.md)
marks gfxstream, Venus and MoltenVK as future work. Do not base this port on
features documented only for newer upstream QEMU or another VMM.

## Current gaps and baseline audit

The working VM uses virtio-ramfb with GL off, genfb/wsfb and llvmpipe.
The console driver has no DRM interface; opening the existing DRM device
nodes returns ENODEV. Installed Mesa 21.3.7 lacks the VirGL DRI driver.

The imported `dist/drm/virtio` sources are not built or attached. They require
missing Linux VirtIO and GEM shmem interfaces. NetBSD already has UVM-backed
GEM allocation, page pinning and a native mmap fault path; reuse that base.

Do not assume the imported driver is an exact Linux release. After removing
NetBSD metadata and blank lines, `virtgpu_drv.c` and `virtgpu_vq.c` match
Linux v5.6, while `virtgpu_object.c` differs in resource-ID allocation and
cached-map initialization. Audit the full file set and relevant upstream
fixes before enabling it. Record the selected revision, local delta and
backports; a wholesale DRM upgrade is not a prerequisite assumed by this plan.

## Component boundaries

### VirtIO compatibility and attachment

Put the minimum Linux VirtIO API required by this driver beside the existing
`drm2/linux` compatibility code, with headers under `drm2/include/linux`.
Use explicitly prefixed native types at the boundary to avoid collisions
with NetBSD's `struct virtqueue`. Keep DRM, capsets and rendering out of this
transport layer. Do not build a general bus framework beyond this first user.

Adapt to NetBSD autoconfiguration and `dev/pci/virtio.c`; do not copy FreeBSD
newbus, DMA assumptions or platform workarounds. Reuse native feature
negotiation, queue allocation, bus_dma and interrupt handling. Gate ownership
so viogpu and the DRM child never attach to the same device. Initialize DRM
before registering its nodes; leave the ordinary EMBER64 configuration intact.

Define contracts for readable/writable SG boundaries, errno conversion,
interrupt rearming, queue backpressure and reset/callback exclusion.
NetBSD's `virtio_enqueue_reserve()` asserts `nsegs <= vq_num` even when indirect
descriptors exist. Bound or reshape backing-list submissions accordingly;
an oversized request must fail rather than wait forever for impossible space.
Test any shared transport change against existing VirtIO consumers.

### GEM, UVM and DMA

Implement the missing shmem contract around NetBSD's GEM/UVM ownership and
`gem_uvm_ops`. Keep pages pinned while the host may access them. Distinguish
queue completion from GPU fence completion and resource detach/unref lifetime.
Do not release mappings merely because a command entered the used ring.

Specify CPU/device synchronization for commands, responses, backing pages and
readback through NetBSD's DMA APIs. Do not equate physical and DMA addresses
or discard cache maintenance based on a FreeBSD-only implementation detail.
Exercise mmap faults, fork/close, failed submissions and interrupted clients.

Preserve every imported license and identifier. The missing Linux shmem
helper is GPL-2.0; importing it needs an explicit, correctly placed licensed
component. Do not copy it into a BSD-labelled subtree or label adaptations
permissive. Prefer existing native primitives where they provide the contract.

### DRM/KMS and classic VirGL

Keep the standard VirtGPU ioctl ABI used by Mesa. First implement dumb
buffers, scanout, flush, cursor and page-flip completion with 3D disabled.
Then enable contexts, capsets, 3D resources/transfers, EXECBUFFER and fences.
Only advertise implemented capabilities; blob, host-visible and context-init
features stay disabled until separately implemented and tested.

Support same-device PRIME export/import and cross-process descriptor lifetime
for desktop buffer sharing. Reject unsupported cross-device imports clearly;
that restriction must not break same-device round trips. Primary-node KMS
and render-node permissions remain distinct. Do not allow CREATE_DUMB on a
render node simply to make a software compositor start.

### Mesa, Xorg and session integration

First prove an EGL/GBM render and readback, then visible KMS rendering.
For the desktop replace wsfb with Xorg modesetting, validate glamor, DRI3 and
Present, then start GNOME. A render-node triangle alone is insufficient.
See [modesetting(4)](https://man.netbsd.org/modesetting.4).

Pin a compatible Mesa/libdrm build with VirGL, EGL/GBM and GLX in an isolated
prefix. Start from the installed Mesa version for the initial ABI check;
upgrade only for an identified missing feature or fix. Record exact library
paths for Xorg, test clients and Mutter so base X11 and pkgsrc Mesa are not
silently mixed. Remove software overrides only in the explicit GPU test session.

## Validation sequence

Each stage produces a small reproducible result before the next is enabled.
New contract probes use C and shell; upstream tools retain their real build
dependencies. Record source revisions, flags, hashes and host display settings.

| Stage | Required evidence |
|---|---|
| Host preflight | Record the actual GL-capable UTM device and ANGLE backend. Where available, verify a known Linux guest on the same UTM build; an external FreeBSD report is a reference only. |
| Build and ownership | Clean native kernel build, one GPU child, DRM initialization, feature mask and reachable nodes. Boot with both GL and non-GL devices. |
| Memory and 2D | Create/map/draw/scan out a known pattern, repeated flips and cursor updates; test a buffer whose backing list exceeds direct-ring capacity. No 3D claim at this stage. |
| VirGL | Query real capsets, create contexts, render a known image, wait for its fence and verify pixels. Run a visible EGL/GBM test such as kmscube. |
| Sharing and Xorg | Same-device PRIME round trip between processes, modesetting/glamor, DRI3/Present and an accelerated GLX client. |
| Desktop | GNOME redraw, pointer/keyboard, resize, applications, logout/login and a cold reboot without software overrides. |
| Reliability | Thirty-minute rendering/allocation workload; kill clients with submissions in flight; malformed sizes/handles/pointers; queue exhaustion and injected attach failures. Check memory, completion counters and kernel diagnostics. |
| Recovery | Restore both the saved UTM display configuration and framebuffer kernel/session. Confirm visible login and input. A kernel-only rollback is insufficient if the virtual device changed. |

Add a regression for each failure found. Validate the normal framebuffer
configuration after changes to shared VirtIO or DRM compatibility code.
Keep diagnostics for pending requests, fences and live buffer objects.
Reject detach while clients or console mappings remain; hot removal and live
unloading are not promised by the initial supported configuration.

Acceptance requires correct images, a VirGL renderer backed by the Mac GPU,
the expected loaded libraries, and a working desktop after reboot. A Metal
display window, `direct rendering: Yes`, a device node or high FPS alone
does not prove guest GPU acceleration. UTM also uses Metal to present CPU frames.
Compare frame time and CPU use against the saved llvmpipe case at the same
resolution/workload; report measurements without inventing a speed target.

## Alternatives and decision

Retaining wsfb is the recovery path. Extending console mmap would help 2D but
leave the rendering ABI missing. Forwarding GL to a separate host service
would add a different deployment path. Importing FreeBSD's Linux 6.13 stack
wholesale would enlarge this task into a DRM upgrade. None is the first choice.

Proceed with a narrow VirtIO compatibility layer, NetBSD-native memory glue,
the audited existing VirtGPU core, then staged KMS, VirGL and Xorg integration.
This replaces the original module-local shim proposal and makes buffer sharing
and presentation explicit. It remains a design, not a statement of support.

## Pinned references

- [FreeBSD v2 source a62ec6b](https://github.com/borovikovd/drm-kmod/tree/a62ec6b1674bbcd4cf301fbd5cd575b8c00d2e90); [VirtIO adapter](https://github.com/borovikovd/drm-kmod/blob/a62ec6b1674bbcd4cf301fbd5cd575b8c00d2e90/drivers/virtio/linuxkpi_virtio.c).
- [Independent FreeBSD test, revision 83e5f9dc19](https://github.com/freebsd/drm-kmod/pull/517#issuecomment-5974708741); this predates the reviewed head.
- [Linux v5.6 VirtGPU](https://github.com/torvalds/linux/tree/v5.6/drivers/gpu/drm/virtio); [shmem helper](https://github.com/torvalds/linux/blob/v5.6/drivers/gpu/drm/drm_gem_shmem_helper.c).
- [DRM buffer sharing and lifetime](https://www.kernel.org/doc/html/latest/gpu/drm-mm.html#prime-buffer-sharing).
- [QEMU GPU backends](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html); latest QEMU features are not assumed available in UTM 4.7.5.
