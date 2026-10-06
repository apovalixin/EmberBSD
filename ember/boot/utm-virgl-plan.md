# Native Wayland and VirtIO-GPU implementation

Goal: native Wayland on the current EmberBSD UTM guest, with rendering on
the host GPU, and a tested framebuffer/X11 recovery path.

Spec: [UTM VirGL design](utm-virgl-design.md).

Architecture: native NetBSD VirtIO transport, the existing Linux-derived
VirtGPU DRM core, UVM-backed GEM, pkgsrc Mesa/VirGL and labwc/seatd.

Global constraints: preserve ordinary EMBER64, use an experimental kernel
configuration, retain upstream licenses, use C/shell for new probes, never
claim acceleration from device nodes or nested rendering alone. Build from
committed sources. No new users, passwords, or automatic login. Public
artifacts contain no local network addresses or credentials.

## Task 1: VirtIO compatibility boundary

Files: new `sys/external/bsd/drm2/include/linux/virtio*.h` and
`sys/external/bsd/drm2/linux/linux_virtio.c`; narrowly scoped headers and
contract probes if needed. Do not wire the default kernel yet.

Implement only the API consumed by the existing `dist/drm/virtio` driver:
feature/config access, queue allocation, SG submission, kick, completion,
interrupt rearming and reset. Define a native-prefixed queue wrapper and
explicit initialization API for the later autoconfiguration child. Avoid
colliding with native `struct virtqueue`. Use native bus_dma maps and sync;
preserve readable/writable SG boundaries. Map errors to negative Linux errno.
Bound oversized requests before native queue assertions. An impossible
request must not become an infinite ENOSPC retry. Serialize reset against
callbacks and reject submissions after reset. No DRM policy in transport.

Audit the driver calls and native queue semantics before implementation.
Add C/shell contract checks for descriptor counting, direction boundaries,
overflow, error conversion and reset state where practical. Compile against
the native NetBSD headers. Expected: those checks pass and no unused API is
advertised. Record interfaces and remaining integration checks; commit.

## Task 2: UVM GEM and driver integration

Files: native shmem helper under `drm2/drm`, corresponding include header,
`drm2/virtio` autoconfiguration/build files, existing VirtGPU sources as
required, and `sys/arch/evbarm/conf/EMBERGPU`.

Consume Task 1's transport. Use native GEM pages and pager operations.
Retain pinning until detach/unref completion. Use DMA addresses from maps,
not assumed physical addresses. Audit every VirtGPU source against Linux
v5.6 and record meaningful differences. Preserve imported licensing.
Enable 2D first. Register DRM only after initialization and successful
queue setup. Ensure exclusive ownership versus viogpu and complete unwind.

Compile a clean experimental kernel. Expected: all objects link, normal
EMBER64 remains buildable, and software contracts pass. Commit fixes before
exporting each test kernel. Test attach, card/render nodes, dumb buffer
mmap and visible scanout. A compile alone does not complete this task.

## Task 3: Native Wayland software session

Files: reproducible build/session/probe scripts and README in
EmberBSD-Examples `desktop/wayland-utm`; OS notes only reference that example.

Pin pkgsrc dependencies and build labwc/wlroots with NetBSD input and seatd.
Keep a private, documented graphics prefix where a Mesa replacement is
needed. Run the compositor as the existing user from a console entry.
Verify KMS without an X parent, keyboard/modifiers/pointer, VT handoff,
normal exit and crash recovery. Expected: a saved client file and working
input; X11 recovery still works. Record versions and library paths.

## Task 4: VirGL and buffer sharing

Enable only classic VirGL capabilities after 2D passes. Validate standard
ioctl permissions, real capsets, context/resource creation, transfers,
EXECBUFFER and fenced readback. Exercise same-device PRIME export/import
across processes, rejecting unsupported cross-device imports clearly.

Use an actual GL-capable UTM display, recording the host renderer/backend.
Run an EGL/GBM known-image probe and labwc GLES; then native clients and
Xwayland. Expected: correct pixels, VirGL renderer, host GPU evidence and
the intended libraries, without software-forcing overrides in this session.

## Task 5: Reliability, recovery and delivery

Run 30 minutes of rendering/allocation, kill in-flight clients, test invalid
handles/sizes/pointers and queue exhaustion. Inspect memory, fences and
kernel diagnostics. Compare the same workload against llvmpipe. Reboot into
native Wayland and test recovery of both kernel and display settings.

Review the whole change, fix material findings, update current support
boundaries in the wiki, commit and push the owning repositories. Mark
unverified stages explicitly; GNOME Wayland is a separate integration.

Review focus: asynchronous object lifetime, non-coherent DMA, impossible
queue submissions, reset/workqueue races, malicious ioctl input, GEM mmap
reference ownership, driver attach failure and two-driver ownership.
