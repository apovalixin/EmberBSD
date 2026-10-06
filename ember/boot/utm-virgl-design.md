# VirtIO-GPU acceleration for EmberBSD in UTM

Status: proposed design, not implemented or validated.
Target: NetBSD 11/aarch64-based EmberBSD under UTM/QEMU on Apple Silicon.
Source baseline inspected: `7b60971e56e` (main on 2026-10-06).

## Intended outcome

Run the existing GNOME desktop with OpenGL rendering performed by the host
GPU through VirGL. A working renderer must identify the accelerated path;
llvmpipe, softpipe and software fallback do not satisfy acceptance.
Keep the existing framebuffer kernel and boot entry available for recovery.

The first target is this VM's OpenGL desktop. Physical-board GPUs, PCI
passthrough, Vulkan/Venus, compute and native GNOME Wayland are separate
results, not implied by a successful VirGL test.

## Observed gaps

- The current UTM configuration exposes `virtio-ramfb` with SPICE `gl=off`.
  UTM also supports GL-capable VirtIO devices and a VirGL/ANGLE host path.
- `sys/dev/pci/viogpu.c` implements a wscons console. Normal feature
  negotiation requests no GPU feature bits; its mmap access operation is
  absent. It supplies neither DRM render nodes nor the VirtGPU ioctl ABI.
- The Linux-derived `virtgpu_*` sources exist under
  `sys/external/bsd/drm2/dist/drm/virtio`, but are not wired into the NetBSD
  build or attachment framework. Their presence is not a working driver.
- Those sources require Linux VirtIO interfaces and GEM shmem helpers that
  are absent from this imported/ported subset. NetBSD's existing GEM uses
  UVM objects, making memory lifetime, fault handling and DMA adaptation
  substantive work.
- The installed base Mesa 21.3.7 has no `virtio_gpu_dri.so`/VirGL driver.
  Forcing `GALLIUM_DRIVER=virgl` currently fails context creation.
- The working Xorg `wsfb` screen is not DRI2 capable. Current GNOME uses
  llvmpipe. The DRM device nodes return ENODEV when opened by root.

## Chosen architecture

Use the standard VirtGPU DRM userspace ABI expected by Mesa. Keep the
VirtIO transport adapter local to the new DRM driver until another driver
requires a common Linux VirtIO compatibility interface.

The driver owns a single VirtIO GPU child and exposes DRM/KMS and render
nodes. The console-only viogpu driver must not claim the same device.
Use a separate experimental kernel configuration during development;
do not enable an incomplete driver in the normal EMBER64 configuration.

Adapt the existing imported driver to this tree's DRM version rather than
mixing a recent Linux/FreeBSD driver with incompatible DRM internals.
The FreeBSD port is a reference for VirtIO platform integration and UTM
quirks, not a patch series that can be applied to NetBSD unchanged.

Provide NetBSD-native attachment, DMA mapping and queue completion logic.
Map GEM buffer ownership onto UVM and the existing NetBSD DRM fault path.
Keep command buffers and page mappings alive until host completion.
Implement contexts, capsets, resources, command submission and fences
needed by classic VirGL; do not advertise unsupported features.

Build Mesa's VirGL driver in an isolated prefix against the selected DRM
ABI. Test direct rendering and KMS before changing the GNOME session.
Switch the VM to a GL-capable VirtIO display only for the test kernel.
Remove software-rendering overrides only when accelerated context creation
works, then verify the desktop uses the intended Mesa installation.

Preserve original authorship, licenses and source identifiers. Evaluate
each missing imported helper's license before adding it; do not silently
relicense existing kernel code. Record exact upstream revisions for any
newly imported files and explain native adaptations beside the code.

## Validation gates

1. Native kernel build and relevant existing EmberBSD contracts pass.
2. Driver attaches once, negotiates only implemented features, and opens
   the DRM nodes. Non-GL devices retain a clear supported fallback.
3. Query capsets; create, map, transfer and destroy resources; reject
   invalid sizes, offsets, contexts and user pointers. Test descriptor
   exhaustion, host errors and failed attachment cleanup.
4. Render a known image through VirGL and verify its pixels. Check fence
   completion, repeated allocation/destruction and process-exit cleanup.
5. Run a graphics test and record renderer/version/capabilities. Confirm
   hardware rendering, with no silent software fallback.
6. Start GNOME, test pointer/keyboard, redraw, resize, applications and
   login after reboot. Retest the framebuffer recovery boot path.

A successful build or capset query is intermediate evidence only. Do not
describe either as GPU acceleration support before rendering is verified.

## Alternatives

Keeping `genfb`/`wsfb` with llvmpipe preserves the verified desktop but does
not meet the hardware-acceleration request. The nested Qt Wayland test
proves a protocol/client path and also does not meet that request.

Forwarding OpenGL to a separate host service could demonstrate remote
rendering, but would not add the standard guest GPU/DRM path. It is not the
proposed implementation for this task.

## Sources

- [UTM display device documentation](https://docs.getutm.app/settings-qemu/devices/display/)
- [UTM graphics architecture](https://github.com/utmapp/UTM/blob/main/Documentation/Graphics.md)
- [NetBSD viogpu limitations](https://man.netbsd.org/NetBSD-11.x-BRANCH/viogpu.4)
- [FreeBSD VirtIO-GPU port discussion](https://github.com/freebsd/drm-kmod/pull/499)

The FreeBSD result is reported by that port's author; it is not an EmberBSD
hardware validation. The proposed design does not claim upstream acceptance.
