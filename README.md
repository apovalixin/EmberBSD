# EmberBSD

**Unix for intelligent devices.**

EmberBSD is an independent fork of [NetBSD](https://github.com/NetBSD/src),
based on version 11, for single-board computers and embedded systems.
It adds hardware support for Raspberry Pi 5 and related boards, for
the Allwinner A733 and for the ESP32-S31, a 32-bit RISC-V chip with
16 MB of memory: device drivers, kernel fixes, device trees, and tools
for building the kernel and UEFI firmware.

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

EmberBSD stays NetBSD 11 in everything except hardware support: the
same userland, the same pkgsrc packages, the same licence. The
additions are:

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
  aarch64 missing a reschedule, the SD host controller driver. Each
  is a separate diff in `ember/patches` with its origin stated.
- **A reproducible build**: the kernel, the firmware and a card image
  come from a pinned revision; one image boots several of the boards.

The costs are equally plain. Fixes from upstream are merged by hand.
Only what the table below marks "Tested" has been seen working, and
only on the boards we have. Wi-Fi on the A733 boards and the ESP32-S31
needs vendor files that are not open.

## Supported boards

Every "Tested" below is a result on physical hardware, on the board
revision named in the column. "Not validated" means no hardware result is
claimed, whether or not driver code is present. "No" means there is no
driver.

| Function | Raspberry Pi 5 (C1) | Compute Module 5 (D0) | Raspberry Pi Zero 2 W | Orange Pi Zero 4 | ESP32-S31 development board | Orange Pi Zero 3W |
|---|---|---|---|---| --- | --- |
| SoC | BCM2712 | BCM2712 | BCM2710A1 | Allwinner A733 | ESP32-S31 (RV32IMAFC, 16 MB PSRAM) | Allwinner A733 |
| Boot path | UEFI and ACPI, firmware built from this tree | UEFI and ACPI | Native firmware and device tree | Vendor boot0 and U-Boot, device tree | Flash: vendor SPL, OpenSBI, U-Boot, a stub that copies the kernel to PSRAM; device tree | Vendor boot0 and U-Boot, device tree; the boot script tells the board from the Zero 4 |
| Boot storage | microSD: Tested | eMMC: Tested | microSD: Tested | microSD: Tested, 1.8 V signalling in SDR104 at 150 MHz | SPI flash, read-only root mapped from it: Tested | microSD: Tested, SDR104 at 150 MHz (47 MB/s read) |
| Serial console | Tested | Tested | Tested | Tested | Tested (firmware console) | Tested |
| All CPU cores | Tested | Not validated | Tested | Tested (8 cores) | One of two cores is used | Tested (8 cores, a minute of full load at 62 degC with the kit's cooler) |
| Ethernet | Tested | Not validated | No port | Tested (gigabit) | Tested (100 Mbit/s) | No port |
| Wi-Fi | Tested | Tested | Tested at 2.4 GHz; large transfers stall | Tested: WPA2 on 2.4 and 5 GHz with 802.11n in channels up to 40 MHz (30-48 Mbit/s on 5 GHz through SSH); needs the vendor firmware file | Tested: WPA2 on 2.4 GHz, 6000 pings in 100 minutes without a loss; needs the vendor radio libraries, linked into the kernel | Tested: WPA2 on 2.4 and 5 GHz with 802.11n, 32 MiB transfers each way with matching checksums; needs the vendor firmware file |
| Bluetooth | Classic: Tested; BLE: No | Not validated | Not validated | Classic: Tested (inquiry, service query, pairing with Secure Connections and reconnection to a MacBook); BLE: No; needs the vendor patch files | No | Classic: inquiry Tested, pairing not tried; BLE: No; needs the vendor patch files |
| Temperature sensor | Tested | Not validated | Tested | Tested (five sensors, factory calibration) | No | Tested (five sensors) |
| Fan control | Tested | Not validated | No fan | No | No fan | No |
| Watchdog | Tested | Not validated | Not validated | Tested | No | Tested: resets the board |
| Power button | Tested: a press powers the board off, the next one powers it on | Not validated | None on the board | No | None on the board | No |
| I2C | Tested (WM8960 codec) | Not validated | Not validated | Tested (power management chip) | No | Tested (power management chip, Type-C controller) |
| Audio | WM8960 HAT: Tested | Not validated | Not validated | No | No | No |
| Pin multiplexing and GPIO | Not validated | Not validated | Not validated | Pin multiplexing and GPIO output: Tested; no pin interrupts | No | Pin multiplexing: Tested; no pin interrupts |
| Real-time clock | Not validated | Not validated | None on the board | Tested across a reboot | No; the clock is set from the network | Tested across a reboot |
| Processor frequency control | Tested | Not validated | Not validated | Tested: 408 MHz to 1.8 GHz (little cores) and 2.0 GHz (big cores) with the supply voltage following, by the chip's speed grade; drops to 408 MHz at 85 degC | No | Tested: both clusters switch between 408 MHz and 1.8 or 2.0 GHz |
| Voltage regulators | Not validated | Not validated | None on the board | Tested: all outputs of the AXP8191 read; the two core supplies are driven; the chip's own switch for the card pins is driven | No | All outputs of the AXP8191 read at boot; not measured |
| USB | Not validated | Not validated | Not validated | Type-C data port at USB 2.0 rates: a flash drive reads at 22 MB/s, the port is powered only while a device is plugged in (FUSB302, polled); the serdes starts and clocks the controller, SuperSpeed itself not tried (no USB 3 device, one plug orientation only); the two USB 2.0 hosts attach, no device tried | No | Controllers and the Type-C controller attach; no device tried |
| Hardware random numbers | Not validated | Not validated | Not validated | Tested: the crypto engine's generator seeds the kernel at boot | No | Tested |

Long-run stability has not been established on any of these boards.

## Wi-Fi work in progress

None of this is in the tree yet; the table above describes what is.

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
