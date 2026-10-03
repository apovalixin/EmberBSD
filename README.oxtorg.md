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
| Orange Pi Zero 4 (Allwinner A733) | Boots from microSD to multi-user through the vendor boot loader: eight cores, clocks, pin multiplexing, SD card, gigabit Ethernet with a Motorcomm YT8531 PHY, five temperature sensors calibrated from the EFUSE, watchdog. Tested on physical hardware; RGMII delays were measured on the board with random data. The AXP8191 regulators are registered as the boot loader left them (`axp8191reg`); the two core supplies are driven. `sun60icpupll` retunes each cluster's PLL, and the operating points for the chip's speed grade (read from the EFUSE) take the little cores from 408 MHz to 1.8 GHz and the big ones to 2.0 GHz, with the voltage raised before the rate and lowered after it. The thermal driver asks for the lowest rate at 85 degC and releases it at 75 degC. Without a heat sink and with all cores busy the board spends most of the time at 408 MHz with short runs at full speed and peaks of up to 90 degC; held at 792 MHz it still reaches 84 degC, so 408 MHz stays the lowest point. Only speed grade 0x04 has been run. The card's supply switch is not driven, so the card stays at 3.3 V signalling. Wi-Fi through the AIC8800D80 module with the `aicwf` driver: WPA2 with CCMP on 2.4 and 5 GHz at 802.11a/g rates (10-20 Mbit/s through SSH; 802.11n is not announced, because the firmware leaves frame reordering to the host). The driver loads the vendor firmware `fmacfw_8800d80_u02.bin` from `/libdata/firmware/if_aicwf`; the file is not part of this tree. The two USB 2.0 host controllers attach (EHCI and OHCI), no device has been tried on them. The always-on domain clocks and its I2C bus work; the AXP8191 power management chip answers and its power key is reported to powerd (polled; not tried with a key, the board has only the pads). Power-off goes through PSCI. The real-time clock keeps the time across a reboot. Bluetooth of the same module: `aicwf` uploads the controller's patches before the wireless firmware, the controller then answers HCI on UART1 at 1.5 Mbaud with automatic RTS/CTS through `btuart`; inquiry and a service query to a MacBook work alongside Wi-Fi on both bands, and so do pairing with Secure Connections and reconnection with the stored key. The four vendor patch files are read from `/libdata/firmware/if_aicwf` and are not part of this tree. The true random number generator of the crypto engine (`sun60icrypto`) seeds the kernel, so the first boot no longer waits for entropy. No USB 3 yet |
| AirPods | Connection confirmed; reliable audio quality and headset microphone operation have not been confirmed |
| ELM327 | Testing with a physical adapter is still pending |
| BLE | Not implemented in the added Bluetooth management tools |

## Building

The native build environment is NetBSD 11/aarch64 with gcc, config, dtc,
and Python 3.13 from pkgsrc. From a clean checkout of a pinned commit:

```sh
sh oxtorg/build-kernel.sh /absolute/output OXTORG64
```

The build produces an ELF kernel, a native kernel image, device trees for
Zero 2 W and Orange Pi Zero 4, and Ethernet, Bluetooth UART, and WM8960
modules. Validate new
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

Orange Pi Zero 4 starts through the board vendor's boot0 and U-Boot. They
are closed binaries and are not included in the repository: an image
builder copies them from the vendor's own card image. U-Boot then runs
`oxtorg/boot/orangepi-zero4-boot.cmd`, which loads the native kernel and
the device tree from the first partition.

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
