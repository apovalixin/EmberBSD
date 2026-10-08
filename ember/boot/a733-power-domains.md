# A733 PCK-600 power-domain prerequisite

EmberBSD supplies a native FDT power-domain provider for Allwinner A733.
It is a prerequisite for device drivers, including GPU/NPU drivers; it does
not expose an accelerator, submit commands or claim hardware acceleration.
The owner is this OS repository. Physical operation remains unverified.

## Interface and limits

The [driver](../../sys/arch/arm/sunxi/sun60i_a733_pck600.c) attaches as
`sun60ipck` to `allwinner,sun60i-a733-pck-600`. The register range is
`0x07060000` through `0x0706afff`, with eleven 4 KiB domain windows.
Its clock is `CLK_BUS_R_PPU` (19), R-CCU register `0x1ac`, bit 0, sourced
from the high-speed oscillator. **A733 has no PPU reset.** The A523 reset
sequence must not be used on A733.

The [A733 DT](../../sys/external/gpl2/dts/dist/arch/arm64/boot/dts/allwinner/sun60i-a733.dtsi)
contains the provider, with `#power-domain-cells = <1>` and its clock.
The imported [binding IDs](../../sys/external/gpl2/dts/dist/include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h)
preserve upstream numbering: NPU 4, GPU_TOP 5 and GPU_CORE 6.
The other IDs are VI 0, DE_SYS 1, VE_DEC 2, VE_ENC 3, PCIE 7, USB2 8,
VO 9 and VO1 10. This does not validate those peripherals.

Attachment enables only the provider's programming clock. It neither reads
nor writes domain registers. It publishes no GPU/NPU consumer node.
A later consumer must establish its supplies, device clocks, resets,
interrupts and memory ownership in the required order before using hardware.
Shared-domain lifetime management remains the consumer's responsibility;
this FDT interface is not a runtime-PM reference-counting framework.

On a consumer request the provider:

- Validates the domain ID before accessing registers; rejects GPU_CORE off,
  following upstream's always-on restriction.
- Serializes requests and rejects existing dynamic, locked or emulated
  policy and mismatched policy/status, without writing registers.
- Leaves a domain already at the requested policy/state untouched.
- Programs the five A733 delay values only for a needed transition,
  verifies their readback, and preserves unrelated policy bits.
- Drains the policy write, checks policy and status, and waits at most
  10,000 microseconds of polling delays, in 10-microsecond steps.
- Returns `EIO` for readback or reverted-policy failure, or `ETIMEDOUT`
  when status never reaches the requested state. Further requests to that
  domain return `EIO` until reboot. Other domains remain available.

The poll budget excludes CPU scheduling and MMIO access latency. A bus
access that traps or never completes cannot be converted into a timeout
by this software loop. Readback detects ignored writes, not every bus fault.
After a failed write/transition, a rollback could race an unfinished hardware
transition. The driver therefore makes no speculative rollback writes.
Failed attachment releases its mapping and any clock reference it acquired.

The [FDT power-domain API](../../sys/dev/fdt/fdt_powerdomain.c) now accepts an
optional error-returning `pdc_set` callback. Its errors reach consumers, and
an all-domain request stops at the first error. Earlier successful domains
are not automatically rolled back. Existing `pdc_enable` callbacks retain
their previous void-callback behavior; Apple PMGR is unchanged. Truncated
DT specifiers are rejected before entering either callback.

## FDT attachment failures

The FDT bus checks power-domain errors before attaching a matched consumer.
A missing `power-domains` property retains ordinary attachment. A present
malformed property, missing provider or failed transition prints the node
name and error, and prevents both attach and post-attach callbacks. The
node stays unattached so a later scan can retry it.

Unmatched nodes keep the default-pass `not configured` diagnostic without
pre-attach pinctrl or power operations. A late match during that diagnostic
cannot bypass pre-attach; a rescan must first select a matching driver.
Successfully attached nodes are not powered or attached again on a retry.

This preserves pinctrl-before-power ordering for matched consumers. A
failure does not undo the selected pinctrl state or earlier successful
domains in a multi-domain request. Supply/clock sequencing and shared
resource ownership still need a separate consumer design before GPU/NPU
nodes are enabled; this check introduces no sequencing opt-out interface.

Run `sh ember/tools/fdt-power-attach-contract.sh` for the production scan,
pre/post-attach and power API regression. Use
`FDT_ATTACH_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for sanitizer checks. The contract covers quiet/default passes, absent
and malformed properties, missing providers, checked and legacy callbacks,
errors, retries, unmatched diagnostics and preserved side effects.

## Reproduce the software checks

The contract compiles the actual driver and FDT implementation, with bus,
clock and FDT primitives replaced by deterministic C fixtures. It needs a
host C compiler and shell; it does not need Python or a board.

```sh
sh ember/tools/a733-power-contract.sh
POWER_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-power-contract.sh
```

The 38 scenarios cover enable/disable, delayed completion, idempotence,
preserved policy bits, timeout, denial, each ignored register write,
quarantine, invalid IDs, GPU_CORE protection, firmware policy protection,
attachment failure cleanup, truncated FDT data, checked errors and the
legacy callback path. These are software contracts, not physical MMIO tests.

On 2026-10-08 both runs passed on Apple Silicon macOS. Separate `EMBER64`
object directories cross-built the new driver, R-CCU, FDT power-domain code
and unchanged Apple PMGR using GCC 12.5 and the corrected Ports GCC 16.2.
The GCC16 objects retain DWARF5 and CTF, with strict kernel warnings enabled.
Both Zero 3W and Zero 4 DTBs compiled and decoded with the provider present.
The existing `/soc` unit-address warning remains; no new DT warning appeared.

For a complete candidate build use the [cross-build instructions](cross-build.md)
from a clean commit, including matched board modules and DTBs. A focused
object build does not establish a linked/booted kernel. No board was deployed,
rebooted, or subjected to accelerator MMIO in this check. Supplies, GPU/NPU
clocks/resets, identification, DMA/MMU, IRQs and command execution remain
separate hardware acceptance steps.

## Provenance

Register facts were checked on 2026-10-08 against these primary sources:

- Arm [Power Policy Unit Architecture Specification v1.1, DEN0051E](https://documentation-service.arm.com/static/5f873177f86e16515cdb6d99),
  notably PWPR/PWSR, dynamic/lock/emulation fields and static-policy denial.
- Arm [CoreLink PCK-600 TRM, 101150_0004_00](https://documentation-service.arm.com/static/5f23d3b1da9f9552000f9cf6),
  for the 4 KiB PPU window and common registers. Allwinner's `0xc00`,
  `0xc04` and `0xc10` delay registers come from its BSP, not the Arm TRM.
- Orange Pi Linux BSP commit `2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`:
  [PCK600](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/pm_domain/pck600_domains.c),
  [R-CCU](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/drivers/clk/sunxi-ng/ccu-sun60iw2-r.c),
  and [SoC DT](https://github.com/orangepi-xunlong/linux-orangepi/blob/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f/bsp/configs/linux-6.6/sun60iw2p1.dtsi).
  The BSP identifies eleven A733 windows, their delay values, and no PPU reset.
- Linux commit `0c2669a9f4a1d607e7591ae50ccf3c432a0aff08`:
  [PCK600](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/drivers/pmdomain/sunxi/sun55i-pck600.c)
  and [A733 IDs](https://github.com/torvalds/linux/blob/0c2669a9f4a1d607e7591ae50ccf3c432a0aff08/include/dt-bindings/power/allwinner,sun60i-a733-pck-600.h).
  These agree with the BSP and add the GPU_CORE always-on restriction.

Linux/Allwinner driver code is GPL-2.0-only and remains credited to
Chen-Yu Tsai/Allwinner respectively. It was consulted for hardware facts;
no Linux driver source is imported here. The native implementation and
fixtures are new BSD-2-Clause code, with AI assistance declared in-source.
The imported upstream ID header retains its original dual license unchanged.

| Downloaded input | SHA256 |
| --- | --- |
| BSP PCK600 driver | `2d9c2b60b3b264d1732538815d12570e45386d30b280a1d0b4f5b9780bdc49ee` |
| BSP R-CCU driver | `9285e1b35bfae975e2c1257d477d843ad277916b9c555c12c5f88e1c7ea02554` |
| BSP A733 DT | `d68616db1d2755fbf13730d34e1edadb474cf7e5deb93ca982295b6e2a5edba4` |
| Linux PCK600 driver | `1e31b18cc776afcbb9ad3c7f52c317f4c2023560671db4690fb51b76cfbf7daf` |
| Imported Linux A733 IDs | `74b1088fbc582ec093096d3876c10cf2eece260e665e89e87ae855c18c7b9f13` |
| Arm DEN0051E PDF | `fa4ec504e4ffb98f5b35bda644e65c08884d5a5f77d2f853e1eb4d55ec1889fb` |
