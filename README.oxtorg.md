# NetBSD2: hardware adaptations and build instructions

NetBSD2 is an independent NetBSD 11 fork for single-board computers and
embedded systems. Its primary target is Raspberry Pi 5. The repository
preserves the NetBSD source tree and history, adding device drivers,
kernel fixes, and build tools for specific boards. Changes in this fork
should not be treated as changes accepted by the NetBSD developers.

The kernel and common sources are based on the verified NetBSD 11.0
`syssrc.tgz` release archive. The rest of the tree retains the upstream
`netbsd-11` snapshot recorded in `oxtorg/source.json`. The adaptation patches
are already applied to the source tree. Copies in `oxtorg/patches` document
their provenance and support future porting; do not apply them again.

## Hardware support and validation

| Platform or feature | Status |
|---|---|
| Raspberry Pi 5: Wi-Fi, Ethernet, cooling | Tested on physical hardware |
| Raspberry Pi 5: built-in Bluetooth over UART | H4 receive fix, SSP/Secure Connections, authentication, encryption, and reconnection with an iPhone tested on hardware |
| WM8960 Audio HAT over RP1 | Microphones and speaker output tested; output loudness depends on the attached speakers |
| USB audio | One USB Audio Module A showed repeated device disconnections during playback; stable operation has not been confirmed |
| Raspberry Pi Zero 2 W | Adaptations and kernel/device-tree builds are available; full device validation is not claimed |
| Compute Module 5 | Diagnostic configuration and boot changes; full support is not claimed |
| Orange Pi Zero 4 (Allwinner A733) | Boots from microSD to multi-user through the vendor boot loader: eight cores, clocks, pin multiplexing, SD card, gigabit Ethernet with a Motorcomm YT8531 PHY. Tested on physical hardware; RGMII delays were measured on the board with random data. No Wi-Fi or Bluetooth (AIC8800), PMIC, sensors, watchdog, USB or real-time clock yet |
| AirPods | Connection confirmed; reliable audio quality and headset microphone operation have not been confirmed |
| ELM327 | Testing with a physical adapter is still pending |
| BLE | Not implemented in the added Bluetooth management tools |

## Building

The native build environment is NetBSD 11/aarch64 with gcc, config, dtc,
and Python 3.13 from pkgsrc. From a clean checkout of a pinned commit:

```sh
sh oxtorg/build-kernel.sh /absolute/output OXTORG64
```

The build produces an ELF kernel, a native kernel image and device tree for
Zero 2 W, and Ethernet, Bluetooth UART, and WM8960 modules. Validate new
builds on the build environment before deployment. Building and booting
all components of a complete release requires separate validation; the
current installation recipe uses the official NetBSD 11.0 userland.

Build Raspberry Pi 5 UEFI firmware using Docker on macOS or Linux:

```sh
bash oxtorg/build-firmware.sh /absolute/cache --rp1-console
```

The eotics firmware source and submodules are pinned. The adaptation patch
order is defined in `oxtorg/firmware/series`. Standard, console, and diagnostic
variants use separate output directories. Radio firmware assets are fetched
according to `oxtorg/boot/image-assets.tsv`, with hash verification and
license preservation; their binaries are not included in the repository.

Starting a UEFI rebuild removes the previous output for the selected
variant. If Docker or compilation fails, an image builder will stop because
the required input is missing. The host-side contract check covers this
case: `python3 oxtorg/tools/firmware-contract.py`.

## Deploying to a device

This repository provides OS sources, hardware adaptations, and build tools.
It can serve as a base for devices running different applications.
Applications are installed separately and are not required to build the
kernel or UEFI firmware.

For reproducible builds, pin the fork commit and verify the hashes of the
resulting components. An installation image must contain compatible
kernels, modules, firmware, and NetBSD userland. A fully validated general
installation image of this fork has not yet been published.

Wi-Fi configuration, SSH keys, tokens, and other credentials are added by
the operator when preparing each device and remain outside Git. Bluetooth
bonds are tied to the controller: when moving to another board, use its
address and its own bonds. Personalized images containing these settings
are not intended for public distribution.

## Upstream and licensing

Upstream is [NetBSD/src](https://github.com/NetBSD/src), the official CVS
mirror. This fork's `main` branch maintains its own history; automatic
mirror updates do not alter already published fork revisions. Upstream
updates are incorporated through separate commits followed by rebuilds
and hardware validation.

NetBSD and third-party licenses remain in the source files. Additional
utilities under `oxtorg/` use the BSD-2-Clause license, except where a file
carries its own license.
