# Compute Module 5 (D0)

[Board catalog](README.md) · [Build instructions](../../README.ember.md#building)

This page preserves the published hardware results; it is not a new test run.
Board revision: D0.

## Capabilities

| Function | Result and limits |
| --- | --- |
| SoC | BCM2712 |
| Boot path | UEFI and ACPI |
| Boot storage | eMMC: Tested |
| Serial console | Tested |
| All CPU cores | Not validated |
| Ethernet | Not validated |
| Wi-Fi | Tested |
| Bluetooth | Not validated |
| Temperature sensor | Not validated |
| Fan control | Not validated |
| Watchdog | Not validated |
| Power button | Not validated |
| I2C | Not validated |
| Audio | Not validated |
| Pin multiplexing and GPIO | Not validated |
| Real-time clock | Not validated |
| Processor frequency control | Not validated |
| Voltage regulators | Not validated |
| USB | Not validated |
| Hardware random numbers | Not validated |

Long-run stability has not been established. Application results from an
AArch64 VM do not validate this board's camera, audio or GPU/NPU paths.

## Evidence

Results were migrated from the [published support matrix](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.md#supported-boards)
and [hardware notes](https://github.com/apovalixin/EmberBSD/blob/fe2727f6ef675868eba642efa73c5a0e33f92191/README.ember.md#hardware-support-and-validation)
on 2026-10-07. These sources do not supply a complete per-check receipt
with tested OS/firmware revisions and dates. The source commit identifies
the documentation snapshot, not the kernel used in every original test.
Future validation should use the [evidence format](adding-a-board.md#record-the-result).
