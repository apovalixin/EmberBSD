# Orange Pi Zero 3W

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

The capability table preserves earlier published hardware results. The
current-kernel check below records a separate physical run.
Board revision: not recorded in the original support table.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | Allwinner A733 |
| Boot path | Vendor boot0 and U-Boot, device tree; the boot script tells the board from the Zero 4 |
| Boot storage | microSD: Tested, SDR104 at 150 MHz (47 MB/s read) |
| Serial console | Tested |
| All CPU cores | Tested (8 cores, a minute of full load at 62 degC with the kit's cooler) |
| Ethernet | No port |
| Wi-Fi | Tested: WPA2 on 2.4 and 5 GHz with 802.11n, 32 MiB transfers each way with matching checksums; needs the vendor firmware file |
| Bluetooth | Classic: inquiry Tested, pairing not tried; BLE: No; needs the vendor patch files |
| Temperature sensor | Tested (five sensors) |
| Fan control | No |
| Watchdog | Tested: resets the board |
| Power button | No |
| I2C | Tested (power management chip, Type-C controller) |
| Audio | No |
| Pin multiplexing and GPIO | Pin multiplexing: Tested; no pin interrupts |
| Real-time clock | Tested across a reboot |
| Processor frequency control | Tested: both clusters switch between 408 MHz and 1.8 or 2.0 GHz |
| Voltage regulators | All outputs of the AXP8191 read at boot; not measured |
| USB | Controllers and the Type-C controller attach; no device tried |
| Hardware random numbers | Tested |
| GPU/NPU power domains | Native PCK600 provider passes software contracts and GCC16 cross object builds; physical transitions and acceleration unverified ([guide](../boot/a733-power-domains.md)) |
| GPU/NPU clocks and resets | Native main CCU providers pass 199 software assertions and GCC12/GCC16 object builds; firmware PLLs stay unchanged, physical sequencing unverified ([guide](../boot/a733-accelerator-clocks.md)) |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Hardware notes

The Zero 4's processor, power management chip, wireless module and Type-C data port
on a smaller board without an Ethernet PHY; its tree is the Zero 4 one with the MAC
disabled. The same card starts both boards.

Tested on physical hardware: boot from microSD in SDR104, eight cores, five
temperature sensors, watchdog reset, frequency switching on both clusters, Wi-Fi on
2.4 and 5 GHz, Bluetooth inquiry, the real-time clock across a reboot. Not tried: USB
devices, Bluetooth pairing, a long run

## Evidence

### Current kernel check, 2026-10-07

A physical Zero 3W with 4 GiB RAM booted `EMBER64 #0` from commit
`aed986038b1da44cbc9cb8b124b25ec58587bd11`. The board revision was not
recorded. Native GCC 12.5 built the kernel and three matching modules from
a clean export in 55 minutes 54 seconds. The highest sampled temperature
was 57.1 degC. The existing NetBSD Python build contracts passed separately
in an AArch64 VM; the board did not require a Python installation.

After the update, all eight cores, microSD root and Wi-Fi returned. A 32 MiB
file transferred in each direction with matching SHA256. All 16 memfd ATF
cases passed using the same test binary that reproduced two partial-page
mapping/seal failures on the previous kernel. FP defaults and preservation
across threads, signals, fork and exec also passed. Those FP cases already
passed on this board before the update; this does not reproduce the
initial-state bug seen on CPUs without AArch32.

The native image SHA256 is
`5f5c9bd1abef2dda00606ad42d0dce4de5beb58868994bdfd473adc181b351ef`;
the ELF kernel SHA256 is
`aaaa0443d4e63311091267ffcc8e69a941757b92962720296468936cc695f600`.
Both newly built A733 DTBs matched the installed files byte for byte.
Vendor boot files, firmware, storage layout and network settings were
preserved. The old kernel and module tree remain available for rollback;
the active module directory contains the three modules from this build.

The NetBSD 11 userland now includes a complete rebuilt shared/static libc
from `cffffd40`, with the [outlined CAS](../boot/aarch64-outlined-cas.md) and
[binary128 comparison](../tools/aarch64-binary128.md) fixes. Native CAS 850,
binary128 3,600-row/16-mode checks and 27 existing libc/thread cases pass;
hardware IOE is unavailable, so trap cases skip. After installation, fresh
processes pass smoke, CAS and the masked matrix through the default loader.
The previous libraries are retained for rollback; other userland and headers
were not rebuilt as a complete release.

These short boot, network and regression checks do not establish a full release,
sustained uptime,
hardware GPU/NPU execution or a physical Wayland session. Accelerators and
the newer common development packages still need their own acceptance.

### Earlier support matrix

Results were migrated from the [published support matrix](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
