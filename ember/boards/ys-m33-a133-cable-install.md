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
# For a factory image with the inspected vendor su implementation:
ruby ember/tools/a133-install-preflight.rb --adb /absolute/path/to/adb --su
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
The explicit `--su` mode prefixes the same read commands with
`/system/xbin/su 0` and still verifies UID 0. It does not restart adbd.

On 2026-10-08, a second unopened factory sample passed this inspection with
`--su`: Android 10 build `a133-10.0-20231211.133302`, locked flash and green
verified-boot state. Its full 17-partition layout matches the reference
sample. Applying the production FDT adapter to a private copy of its exported
factory tree passed the audio, touch, eMMC, USB-A and opt-in SDIO guards and
created the framebuffer node. These are read-only identification and copied
FDT checks, not an EmberBSD boot or a completed cable installer.

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

The offline environment editor operates on a copied 128 KiB file only:

```sh
ruby ember/tools/a133-env-edit.rb env-copy.bin NEW-env.bin 'bootcmd=run boot_recovery'
ruby ember/tools/a133-env-edit-test.rb
```

It validates CRC32, termination, variable names, uniqueness and capacity;
preserves opaque values and the vendor's leading empty entry; and creates
a new private output without overwriting any existing path. It never writes
a device. Against the actual original factory `env-orig.bin`, changing
`bootcmd` and restoring its old value reproduced every original byte.
This proves offline encoding, not the proposed unlock/recovery bootstrap.
The second sample instead has `bootdelay=0`,
`bootcmd=run setargs_mmc boot_normal` and
`boot_normal=run ${hook};run boot_android`. Do not substitute the first sample's
environment or erase its hook. Recovery access should be established while
still locked before attempting an unlock. A temporary recovery command needs
a demonstrated return to the original Android boot when USB recovery fails;
a persistent recovery loop is not a fleet recovery mechanism. This factory
normal script provides an unused `hook`. A copied-file candidate keeps both
normal boot scripts intact, sets the hook to two `run` variables, first clears
the hook and saves the normal environment, then selects USB device mode and
enters recovery. Restore the exact original per-device environment after
recovery ADB is available. Do not execute it before checking the full backup.

On the reference sample, volatile command trials established that the vendor
`run` expands both hook variables before the first removes the hook, that a
failed first variable skips the second, and that the normal script's Android
fallback remains reachable. The save step was mocked: this validates the
vendor parser and ordering, not a physical environment save or the second
sample's recovery boot. The reference environment and installed boot hash
were unchanged after returning to EmberBSD.

## Android backup and QEMU

The private sample backup covers original boot/recovery, boot hardware areas,
environment, all eMMC bytes before UDISK, the overwritten first 4 GiB of UDISK
and the disk tail. It is not a complete standalone 31,037,849,600-byte eMMC
dump. Untouched UDISK data remains on the physical device. On 2026-10-08,
the boot/recovery partition copies matched their ranges in the original
3,361,734,656-byte head backup. Original snapshots are never VM write targets.

QEMU 11.1.1 lists no A133 board model. A bounded TCG `virt` trial with
factory ARM64 kernel/ramdisk and a read-only head snapshot exited without
Android console output or evidence of Android startup. Factory config
selects `ARCH_SUNXI`; this trial does not emulate the vendor boot chain,
secure storage, USB role hardware, radio or MCU. QEMU's
[Arm board requirements](https://www.qemu.org/docs/master/system/target-arm.html)
explain why a matching CPU cannot substitute for the board model.
Use copied-file and controlled ADB tests for installer behavior; full
hardware boot, unlock and recovery acceptance remains on the real tablet.

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
