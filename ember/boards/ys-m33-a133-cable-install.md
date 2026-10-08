# YS-M33 cable installation and boot-loader boundaries

Installation must use a cable, a closed enclosure and no UART. The installed
tablet then boots autonomously from eMMC. This fleet procedure is not yet
validated on a factory tablet; the current sample was unlocked with UART.

## Read-only first connection

The host needs Ruby and Android platform-tools. Connect the tablet's USB-C
device port and separate DC power. Cable/port negotiation must select the
USB device role. FEL detection alone does not establish ADB or fastboot access.

```sh
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb
# With multiple Android devices, explicitly select the intended USB device:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --serial DEVICE
ruby ember/tools/a133-install-preflight-test.rb
```

The inspector issues only bounded ADB reads. It never restarts adbd as root,
unlocks, reboots, transfers files or writes storage. It requires one selected
ready USB ADB device, root read access, `allwinner,a133` in the device tree
and the inspected boot target/bounds. JSON does not print the device serial.
Missing access, multiple devices, foreign boards, wrong targets, timeouts
and command errors stop inspection. The fixture contract executes the real
tool against an ADB boundary rejecting all other commands.

`inspection_complete` describes a candidate only. The result always states
`installation_ready: false`. Exact board identification, complete partition
inventory, per-device backups, unlock/recovery and image validation precede
an installer's first write. Non-root factory adbd may require `adb root` or
vendor `su`; neither capability is inferred from an eng build label.

## Candidate bootstrap through Android

A read-only snapshot on 2026-10-08 verified the inspected sample's first
128 KiB of `env`: CRC32, `bootcmd=run boot_normal`, `bootdelay=3` and stored
normal/recovery scripts. When factory Android provides root storage access,
a candidate bootstrap can preserve this per-device environment and generate
a temporary boot command to run the vendor unlock operation, select USB
device mode in the loader FDT and enter signed recovery. The recovery
installer would restore the intended normal environment before completion.

These primitives were observed separately: vendor U-Boot's
`pst write fastboot_status_flag unlocked`, volatile `usb_port_type <0>`,
recovery ADB root and verified partition writes. They have not been tested
as an autonomous bootstrap on an unopened factory tablet. Prior unlock
does not prove another factory image's root ADB, environment format or
secure-storage behavior. Userspace fastbootd enumerated in earlier tests
but did not implement the attempted OEM/flashing unlock operations.

## Own boot loader

The sample's 2026-10-08 log reports `secure enable bit: 1` and loading
root-key, monitor, boot, vbmeta and recovery key/image hashes. Its unlock
permits an unsigned Android boot payload; it does not establish permission
to replace boot0, BL31 or the TOC1 U-Boot image without signing. Replacement
warning artwork does not change secure-boot state.

Upstream U-Boot `e1dcad5512b` has A133 support and DDR4/LPDDR4 options.
Liontron's defconfig uses LPDDR4 at 792 MHz; this sample logs DDR4 at 744 MHz.
That defconfig is not YS-M33 validation. The generic
[Allwinner installation recipe](https://docs.u-boot-project.org/en/latest/board/allwinner/sunxi.html)
does not prove a replacement of this secure vendor chain.

The proposed first own-boot experiment is a RAM-loaded second stage after
retained vendor boot0/BL31/OP-TEE. Mainline ARM64 entry supports
`CONFIG_LINUX_KERNEL_IMAGE_HEADER`; wrapper, board handoff and physical
execution still require validation. Preserve DRAM initialization, MCU7502
keepalive, reserved memory and framebuffer. First prove recovery and boot
of the existing kernel without persistent boot-loader changes. Then evaluate
persistent second-stage installation. Full boot0/TOC1 replacement requires
a separately demonstrated authenticated loading/recovery path.

## Fleet acceptance

Test the complete procedure on the next unopened factory tablet with no UART:
USB identification, per-device backup, unlock, recovery, boot/root writes
with full readback hashes, autonomous cold boot and cable-only recovery.
Include recovery from a failed installation. Device keys, tokens, identities
and personalized files must not be cloned from the working sample. Network
credentials are operator inputs outside Git. Preserve the working sample
as the independent reference system.
