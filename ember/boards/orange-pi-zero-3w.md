# Orange Pi Zero 3W

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

This page preserves the published hardware results; it is not a new test run.
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

Results were migrated from the [published support matrix](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
