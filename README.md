# EmberBSD

**Unix for intelligent devices.**

EmberBSD is an independent fork of [NetBSD](https://github.com/NetBSD/src),
based on version 11, for single-board computers and embedded systems.
It adds hardware support for Raspberry Pi 5 and related boards, for
the Allwinner A733 and for the ESP32-S31, a 32-bit RISC-V chip with
16 MB of memory. The OS and its companion projects bring together board
support, local AI, robotics, graphical interfaces, and the system work
needed for GPU acceleration. Capabilities and validation limits are
summarized below; application stacks are installed separately.

## Purpose and getting started

This is the central EmberBSD project. It owns the operating system, device
drivers, board adaptations, boot and firmware integration, system builds
and OS validation. Application dependencies, examples and developer tools
are maintained in the related repositories below.

- For a board or OS build, start with
  [hardware support and build instructions](README.ember.md).
- For an application, start with
  [EmberBSD-Examples](https://github.com/neonix20b/EmberBSD-Examples) and its
  documented dependencies in [EmberBSD-Ports](https://github.com/neonix20b/EmberBSD-Ports).
- For an AI coding assistant, connect the developer skills described below.

## EmberBSD ecosystem

| Repository | Purpose | Current scope |
| --- | --- | --- |
| [EmberBSD](https://github.com/apovalixin/EmberBSD) | Central OS project: kernel, drivers, boards, boot, firmware and system validation | Source tree and board-specific evidence; see the support tables |
| [EmberBSD-Ports](https://github.com/neonix20b/EmberBSD-Ports) | Third-party build recipes, portability patches and native package profiles | pkgsrc recipes and experimental probes, each with its own validation limits |
| [EmberBSD-Examples](https://github.com/neonix20b/EmberBSD-Examples) | Standalone applications and reproducible demonstrations | Local AI, robotics and desktop scenarios with requirements and checks |
| [EmberBSD-Runtime](https://github.com/neonix20b/EmberBSD-Runtime) | Application execution, lifecycle, permissions and shared device operations | Design stage; no released runtime implementation |
| [EmberBSD-SDK](https://github.com/neonix20b/EmberBSD-SDK) | Application interfaces, package contracts, developer tools and compatibility checks | Design stage; no stable application API or released SDK tools |
| [Ember-Agent-Skills](https://github.com/neonix20b/Ember-Agent-Skills) | Instructions for EmberBSD users and AI coding assistants | Installable Codex skills for applications, ports and tested contributions |

Runtime will execute applications on the device; SDK will define their
interfaces and development tools. Examples demonstrate usable scenarios;
Ports owns adaptations to third-party software. Agent-Skills helps developers
use these projects and contribute fixes. A repository's intended purpose is
not a claim that all of its planned features are implemented.

## Connect developer skills

With a Codex CLI that supports plugins (commands checked with 0.160.1):

```sh
codex plugin marketplace add neonix20b/Ember-Agent-Skills --ref main
codex plugin list --marketplace ember-agent-skills --available --json
codex plugin add emberbsd-development@ember-agent-skills
```

Start a new conversation in your project and ask:

> Use $emberbsd-repository-guide to find the EmberBSD interfaces and examples
> for my application, test the result, and document its requirements.

See the [skills installation and update guide](https://github.com/neonix20b/Ember-Agent-Skills#install-in-codex)
for expected results, updates and other assistant environments. The plugin
provides developer instructions; it does not install software on a board.
Reusable fixes and ports are contributed through focused PRs after relevant
testing, following the accepting project's rules.

## What this fork adds to NetBSD 11

EmberBSD builds on NetBSD 11 to make useful device workflows reproducible:
local inference, robot telemetry, vision and positioning, and graphical
controls. This tree owns the OS changes; Ports and Examples supply optional
application stacks, adaptations and runnable checks. Small installations
can keep only the components they need. Upstream components retain their
own licenses and authorship.

Status updated **2026-10-07**; linked component documents record their own
validation dates. Application tests on an AArch64 VM do not establish support
on every board in the hardware catalog.

### Local AI and robotics

- **Local text and speech models:** pkgsrc recipes for llama.cpp 0.6.0 and
  whisper.cpp 1.9.4, with separately supplied models. Native AArch64 VM tests
  cover package installation, text generation, a loopback HTTP completion
  service and WAV transcription on CPU. This is a usable starting point for
  local assistants; microphone capture, image understanding and GPU/NPU
  inference still need validation. See the [AI packages](https://github.com/neonix20b/EmberBSD-Ports/tree/main/profiles/ai-cpu)
  and [model execution example](https://github.com/neonix20b/EmberBSD-Examples/tree/main/ai/local-inference).
- **Answers from local documents:** a C example uses SQLite 3.53.4 FTS5 to
  retrieve evidence and the existing llama.cpp CPU server to select a quotation.
  It checks the source and quoted text before displaying them, and handles
  missing evidence and server failures. Native AArch64 tests include actual
  model execution, SQLite transactions, concurrent readers and process-crash
  recovery. This is extractive retrieval, not arbitrary answer-quality or
  physical power-loss validation. See the [document example](https://github.com/neonix20b/EmberBSD-Examples/tree/main/ai/local-knowledge)
  and [SQLite checks](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/sqlite).
- **Robot telemetry and commands:** Zenoh-Pico 1.10.1 packages and a C device
  controller exchange typed data and commands with ROS 2 Jazzy through a
  C++ bridge. The two-VM test covers acknowledgements, stale-command rejection
  and reconnection. This connects EmberBSD devices to ROS systems; it is
  bridge interoperability, not a native ROS 2 distribution. See the
  [Zenoh/ROS 2 example](https://github.com/neonix20b/EmberBSD-Examples/tree/main/robotics/zenoh-ros2).
- **Model APIs and speech processing:** ONNX Runtime 1.30.0 and ncnn 20260526
  provide installed C/C++ CPU inference libraries. RNNoise 0.2 processes audio,
  and Silero VAD 6.2.3 detects speech through the same ONNX Runtime library.
  Seven AArch64 VM checks cover numerical results, recurrent stream state,
  speech/silence boundaries and invalid inputs; ORT worker affinity is checked
  separately. Ports preserves the NetBSD adaptations and
  [build and test instructions](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/ai-engines).
  These source probes support application development; microphone capture,
  real-world audio quality and GPU/NPU execution remain unverified.
- **Vision, geometry and positioning:** OpenCV 5.0.0, Eigen 5.0.1 and gpsd
  3.27.5 build and pass installed-consumer tests on AArch64. Checks include
  image processing, features and camera-pose recovery, numerical solvers,
  transforms, and a real gpsd process receiving synthetic GNSS data.
  These are [experimental source builds](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/robotics-foundations);
  physical cameras/GNSS receivers and accelerated vision remain unverified.
- **Video processing for applications:** FFmpeg 9.0.2, GStreamer 1.28.7 and
  OpenCV 5.0.0 videoio share one tested media installation. AArch64 VM checks
  cover exact decoded frames and timestamps, application-buffer pipelines,
  file capture through both OpenCV backends, seeking, image processing and
  lossless output. NetBSD filesystem support and FFmpeg compatibility fixes
  are preserved in the [media port](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/media).
  This is a CPU file-processing profile; camera capture, network streaming
  and hardware codecs remain outside its verified scope.

### Graphical interfaces

- **Desktop and touch-oriented applications:** [GNOME/X11](https://github.com/neonix20b/EmberBSD-Examples/tree/main/desktop/gnome-utm)
  provides a tested desktop scenario. [Phosh 0.58.0](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/phosh)
  runs inside it with software-rendered Wayland, application switching,
  GTK applications and the Stevia English/Russian screen keyboard.
  These VM checks provide interface prototypes, not validated phone images.
- **Additional X11 desktops:** [Openbox 3.6.1](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/openbox)
  and [Enlightenment 0.27.1/EFL 1.28.1](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/enlightenment)
  pass software X11 window management, two application windows, keyboard
  input through XTEST, text editing/saving and clean session exit.
  The [common launcher and checks](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/x11-desktops)
  isolate session configuration while preserving the user's HOME.
  [awesomeWM 4.3](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/awesome)
  also builds and passes the same workflow with system Lua and patched LGI.
  [Xfce 4.20](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/xfce)
  has a source recipe and checked session profile; native build and runtime
  remain pending. These checks do not validate physical input, touch or GPU
  acceleration.
- **Current KDE/Qt integration:** KWin 6.7.5 runs a nested Qt Wayland window
  with software rendering and tested keyboard input. Plasma Mobile 6.7.5
  builds and installs with checked library loading and QML components.
  A complete mobile shell workflow, native display and power management
  still need validation. See the [Plasma Mobile port](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/plasma-mobile).

### GPU foundations and NPU direction

- **Native graphics infrastructure:** experimental VirtIO DRM/KMS, UVM-backed
  GEM buffers and PRIME sharing make direct Wayland sessions possible.
  UTM tests cover visible KMS output, 32 cross-process buffer-lifetime cycles
  and labwc/Pixman displaying Kate without Xorg. The tree also includes DRM
  device-identity, fence-validation, bounded capset-query, context creation/retirement,
  console-recovery and partial-page memfd
  fixes; their individual build/runtime boundaries are documented separately.
  [Resource-lifetime fixes](sys/external/bsd/drm2/virtio/resource-lifetime.md)
  retain shared buffers across duplicate handles and failed host responses.
  [Asynchronous submission](sys/external/bsd/drm2/virtio/submit-ownership.md)
  reports immediate queue errors and publishes fence descriptors only after
  acceptance, with balanced cleanup on failure.
  [Completion ordering and reset](sys/external/bsd/drm2/virtio/completion-lifetime.md)
  prevent a later reply from prematurely signaling an older command and
  drain accepted requests before publishing terminal reset results.
  [Owned DMA backing checks](sys/external/bsd/drm2/virtio/dma-eligibility.md)
  reject unsuitable ARM64 maps before a future 3D context can use them,
  including resources shared through PRIME or duplicate handles.
  [Backing lifetime and retirement](sys/external/bsd/drm2/virtio/backing-lifetime.md)
  keep eligible memory pinned beyond individual request completion, until
  fenced resource retirement or completed reset makes release safe.
  [Whole-context EXEC ownership](sys/external/bsd/drm2/virtio/exec-ownership.md)
  covers every attached buffer even when an application supplies incomplete
  hints, and retains each pending operation through retries and overlapping
  completion. A [classic fence limit](sys/external/bsd/drm2/virtio/classic-fence-range.md)
  stops the device safely before sequence numbers can wrap or become ambiguous.
  Native contracts and object builds pass; transfers, CPU access,
  full kernel/runtime acceptance and live DMA qualification remain pending.
  See the [VirtGPU implementation](sys/external/bsd/drm2/virtio/README.md),
  [DRM identity checks](ember/boot/drm-native-identity.md) and
  [graphics probes](https://github.com/neonix20b/EmberBSD-Examples/tree/main/desktop/wayland-utm).
  Ports owns the [current Mesa 26.2.4 source adaptation](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/wayland-utm),
  with native DSO-lifetime/numeric regressions under GCC 16.2 and
  common-toolchain staging rules;
  its complete build and consumer migration remain pending. VirGL remains
  disabled; GPU rendering, reliable console recovery and Vulkan Compute
  are not yet established.
- **Physical GPU and NPU porting targets:** CIX P1 is the first selected
  direction: Mali-G720 through Panthor/[Mesa PanVK](https://docs.mesa3d.org/drivers/panfrost.html),
  and Zhouyi v3/X2 through the [Compass driver/runtime sources](https://github.com/Arm-China/Compass_NPU_Driver).
  The [Compass Ports probe](https://github.com/neonix20b/EmberBSD-Ports/tree/main/probes/compass-umd)
  fixes descriptor ownership during initialization and cleanup, and rejects
  out-of-range partition/cluster queries while preserving legacy NPU counts.
  On NetBSD/AArch64 with GCC 16.2, 13 descriptor cases and 58 core-count
  cases pass against isolated production methods; a full runtime build and
  NPU execution remain unverified.
  A733 PowerVR/Vivante integration is a separate investigation. These are
  porting targets, not available EmberBSD acceleration. Board bring-up,
  driver/DMA integration, compatible runtime and real model execution must
  all pass before an accelerated AI workflow is claimed.

### Board support and system builds

- **Allwinner A733** (Orange Pi Zero 4 and Zero 3W), a chip the base
  system has no support for: eight cores, the card at SDR104 speed,
  gigabit Ethernet, the AIC8800 Wi-Fi and Bluetooth module, processor
  frequency control, temperature sensors, watchdog and clock.
- **ESP32-S31**, a 32-bit RISC-V chip with 16 MB of memory and a new
  platform for NetBSD: the system boots from flash and works over Wi-Fi.
- **Raspberry Pi 5 and Compute Module 5**: UEFI firmware built from
  this tree, and drivers for the on-board Wi-Fi, Ethernet behind the
  RP1 chip, Bluetooth, the power button, the fan, the watchdog, I2C
  and a WM8960 audio codec.
- **Raspberry Pi Zero 2 W**: Wi-Fi on its BCM43436 chip.
- **Kernel fixes found on these boards** that are not specific to
  them: the `bwfm` Wi-Fi driver stalling on large transfers and after
  an access point asks the client to change band, idle cores on
  aarch64 missing a reschedule, and the SD host controller driver.
  Source changes preserve their provenance; `ember/patches` records the
  original board adaptations already applied to this tree.
- **AArch64 numerical correctness:** an [initial FP state correction](ember/boot/aarch64-fp-state.md)
  addresses lost subnormal values and NaN payloads on CPUs without AArch32.
  Production contracts and native object compilation pass; verification
  after booting the corrected kernel remains pending.
- **AArch64 atomic correctness:** [narrow outlined CAS helpers](ember/boot/aarch64-outlined-cas.md)
  now normalize expected arguments so matching byte/halfword updates are
  not skipped. All 850 native production checks and an isolated unchanged
  GCC atomic regression pass with the repaired objects. Installing the
  corrected libc and accepting the complete compiler suite remain pending.
- **Reproducible builds and development:** pinned kernel/UEFI inputs and
  checked firmware assets, pkgsrc overlays, versioned source probes and
  standalone examples. The [Ports development toolchain](https://github.com/neonix20b/EmberBSD-Ports/tree/main/profiles/development-toolchain)
  provides a GCC 16.2 candidate built and installed on the AArch64 VM, with
  native C11/C++20 thread, TLS and shared-library checks passing. The full
  upstream suite has exposed platform compatibility failures; their repair
  and a coherent Qt/LLVM runtime rebuild remain required before adopting
  it as the default compiler in new images. The [common build-tools profile](https://github.com/neonix20b/EmberBSD-Ports/tree/main/profiles/common-build-tools)
  prepares Python 3.14.8 and Meson 1.12.1 with checked portability patches
  and explicit interpreter/LLVM selection. Source checks and focused native
  macro/selection contracts pass; complete packages, installed consumers
  and the LLVM 23/Mesa 26 stack remain pending. [Developer skills](#connect-developer-skills) help
  AI coding assistants find the owning project, test changes and prepare
  contributions. A general validated installation image is not yet released.

The costs are equally plain. Fixes from upstream are merged by hand.
Only what the linked board pages mark "Tested" is claimed on physical
hardware. Each application or graphics result has its own test environment
and limits in the linked documentation. Wi-Fi on the A733 boards and the ESP32-S31
needs vendor files that are not open.

## Supported boards

Choose a board below for its boot path, feature results, known limitations
and evidence. A physical test validates only the listed functions on the
listed revision; it does not imply full peripheral or long-run support.

| Board | Architecture / SoC | Confirmed scope and main limits |
| --- | --- | --- |
| [Raspberry Pi 5 (C1)](ember/boards/raspberry-pi-5.md) | AArch64 / BCM2712 | microSD/UEFI boot, Ethernet, Wi-Fi, classic Bluetooth, cooling and WM8960 audio; USB and graphics not validated here |
| [Compute Module 5 (D0)](ember/boards/compute-module-5.md) | AArch64 / BCM2712 | eMMC/UEFI boot, serial console and Wi-Fi; most peripherals not validated |
| [Raspberry Pi Zero 2 W](ember/boards/raspberry-pi-zero-2-w.md) | AArch64 / BCM2710A1 | microSD boot, all cores, serial and 2.4 GHz Wi-Fi; large Wi-Fi transfers stall |
| [Orange Pi Zero 4](ember/boards/orange-pi-zero-4.md) | AArch64 / Allwinner A733 | Eight cores, SDR104, Ethernet, Wi-Fi, classic Bluetooth, thermal/frequency control and USB 2.0 data; SuperSpeed unconfirmed |
| [Orange Pi Zero 3W](ember/boards/orange-pi-zero-3w.md) | AArch64 / Allwinner A733 | Eight cores, SDR104, Wi-Fi, Bluetooth inquiry and thermal/frequency control; USB devices and Bluetooth pairing not tested |
| [ESP32-S31 development board](ember/boards/esp32-s31.md) | RISC-V 32 / ESP32-S31 | Flash boot, Ethernet and WPA2 Wi-Fi in 16 MB; one core, vendor radio libraries, no BLE/USB |

The [board catalog](ember/boards/README.md) defines validation terms and links
to VM and research targets. To contribute another board, follow
[adding a board](ember/boards/adding-a-board.md) and the
[developer skill](https://github.com/neonix20b/Ember-Agent-Skills#add-your-board).
Add a catalog row and a board page; keep detailed feature matrices on those pages.

## Wi-Fi work in progress

See the board pages for published Wi-Fi support. The following work has not
been integrated into the tree yet.

- **Roaming in `bwfm`** between access points and between bands. The
  host, not the radio firmware, decides on a transition: the tree
  already disables the firmware's own handling of 802.11v requests,
  and `wpa_supplicant` takes it over. With WPA2-PSK this has been
  validated on a Raspberry Pi 5 from a local build: the client moved
  from 2.4 to 5 GHz at the access point's request, reconnecting in
  0.20 s (4.22 s with the scan), and refused a transition it was
  configured not to make. A seamless handover is not claimed.
- **802.11r fast transition and WPA3** (SAE with protected management
  frames) are being implemented on top of it. A network that offers
  only WPA3 cannot be joined today. WPA3 is expected to remove several
  workarounds in the driver; no throughput gain has been measured.
- **Defects found on the way**, with fixes in preparation:
  `wpa_cli disconnect` changes the state NetBSD keeps but sends no
  disconnect command to the firmware; concurrent firmware commands
  could receive each other's replies; the command transport fails
  after 65,536 requests; a short reply is handled incorrectly.

Changes to association and command handling in `bwfm` should be
coordinated with this work until it lands.

See [hardware support, build instructions, and limitations](README.ember.md)
for validation details and source provenance. Changes in this fork should
not be treated as changes accepted into upstream NetBSD.

This repository contains OS sources and hardware support. Applications and
device-specific configuration are added during deployment. Credentials and
personalized device images are not published in this repository.

The original NetBSD reference follows below. Links to official binary
releases refer to upstream NetBSD and do not include this fork's changes.

NetBSD
======

NetBSD is a free, fast, secure, and highly portable Unix-like Open
Source operating system.  It is available for a [wide range of
platforms](https://wiki.NetBSD.org/ports/), from large-scale servers
and powerful desktop systems to handheld and embedded devices.

Building
--------

You can cross-build NetBSD from most UNIX-like operating systems.
To build for amd64 (x86_64), in the src directory:

    ./build.sh -U -u -j4 -m amd64 -O ~/obj release

Additional build information available in the [BUILDING](BUILDING) file.

Binaries
--------

- [Daily builds](https://nycdn.NetBSD.org/pub/NetBSD-daily/HEAD/latest/)
- [Releases](https://cdn.NetBSD.org/pub/NetBSD/)

Testing
-------

On a running NetBSD system:

    cd /usr/tests; atf-run | atf-report

Troubleshooting
---------------

- Send bugs and patches [via web form](https://www.NetBSD.org/cgi-bin/sendpr.cgi?gndb=netbsd).
- Subscribe to the [mailing lists](https://www.NetBSD.org/mailinglists/).
  The [netbsd-users](https://www.NetBSD.org/mailinglists/#netbsd-users) list is a good choice for many problems; watch [current-users](https://www.NetBSD.org/mailinglists/#current-users) if you follow the bleeding edge of NetBSD-current.
- Join the community IRC channel [#netbsd @ libera.chat](https://web.libera.chat/#netbsd).

Latest sources
--------------

To fetch the main CVS repository:

    cvs -d anoncvs@anoncvs.NetBSD.org:/cvsroot checkout -P src

To work in the Git mirror, which is updated every few hours from CVS:

    git clone https://github.com/NetBSD/src.git

Additional Links
----------------

- [The NetBSD Guide](https://www.NetBSD.org/docs/guide/en/)
- [NetBSD manual pages](https://man.NetBSD.org/)
- [NetBSD Cross-Reference](https://nxr.NetBSD.org/)
