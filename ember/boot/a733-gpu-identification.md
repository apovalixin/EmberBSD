# Firmware-ready A733 GPU identification

The native `sun60igpuid` consumer can identify an already prepared A733 GPU.
The current A733 DT enables its diagnostic `netbsd,observe-only` stage, which
never maps or reads GPU registers, even when the queried providers are ready.
This default mode does not initialize or power the GPU, load firmware, establish interrupts,
allocate DMA, submit commands, expose a DRM device, or provide acceleration.
The [driver](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/sun60i_a733_gpu.c)
is owned by EmberBSD. Software contracts, the complete GCC16 kernel build
and physical attachment on Zero 3W are verified. Physical GPU identification
has not yet been accepted. Read-only provider observations are verified;
the separate #8 clock-only experiment timed out before GPU access.

## Physical result, 2026-10-08

A 4 GiB Orange Pi Zero 3W booted matched `EMBER64 #8`, four modules and the
separate experimental DTB from `94ba2f5a99532e2f02720cb4172bcc9441e002ff`.
The board revision was not recorded. Vendor boot0/U-Boot and the boot script
were preserved. MicroSD root and Wi-Fi/SSH returned. The accepted #7 boot
files were backed up on the board and development host before installation.

The consumer reached its bounded CORE waiter after all four GPU-local CCU
write/readback checks succeeded. The waiter returned `ETIMEDOUT` (60), with
last CORE `PWPR=0x8`, `PWSR=0`, `MISR=0`. Resources were retained until reboot;
there was no rollback or retry, and no GPU mapping or PBVNC read occurred.
Clock-only preparation did not complete the pending transition in this test.
The result does not distinguish a stalled PCSM phase from Q-Channel exit.
Further sequencing needs verified A733 firmware and I/O integration facts;
it does not justify a PWCR override, CORE power-off or relaxed readiness.
The subsequent #8 reboot with the normal DTB restored observe-only operation
and Wi-Fi/SSH. GPU_CLK/GPU_BGR returned to zero; PLL, RTC and AHB observations
matched the baseline. Reboot ended the reservation; no GPU access occurred.

The preceding #7 read-only baseline and #8 pre-attempt diagnostic agree:

RTC status was stable at `0x183fb0f7`, classifying DCXO as 26 MHz. Both RTC
queries bracketing the CCU snapshot agreed. The global fixed-hosc provider
still reports its unchanged 24 MHz DT value. With the hardware-classified
26 MHz input, PLL_REF `0xf8675f00` (N=96, M=104, P=1) normalizes exactly to
24 MHz. This is register decoding, not an independent frequency measurement.

The bounded [PCK diagnostic](a733-power-domains.md#read-only-gpu-ppu-diagnostic)
read 20 registers twice for each GPU domain, with no changed values.
Both identify as PCK-600, PPU v1.1, with one Q-Channel: IDR0 `0x10130101`,
IDR1 `0x2`, IIDR `0x0b61143b`, AIDR `0x11`. The key states were:

| Domain | PWPR | PMER | PWSR | DISR | MISR | PWCR |
| --- | --- | --- | --- | --- | --- | --- |
| GPU_TOP (5) | `0x8` | `0` | `0x8` | `0` | `0x100` | `0x101` |
| GPU_CORE (6) | `0x8` | `0` | `0` | `0x1` | `0` | `0x101` |

CORE's static-ON policy and status remain inconsistent with the strict
reader's readiness contract. MISR reports sampled input levels, not the
controller's transition phase or its request output. These observations
do not establish the cause or justify overriding the handshake enables.
The provider diagnostic itself makes no register writes or GPU accesses.

In #7, `sun60igpuid0` reported `GPU module gated (error 16)`, with DCDC4
programmed to 800000 microvolts and GPU_TOP statically ON. Both CCU snapshots
match (`changed 0x000`), retaining the #5 values:

| Register | Value | Observation |
|---|---|---|
| GPU_CLK, `0xb20` | `0x00000000` | Module clock gate is clear |
| GPU_BGR, `0xb24` | `0x00000000` | Bus gate is clear and reset is asserted |
| PLL_GPU0, `0x0e0` | `0x41104500` | Raw dedicated GPU PLL state; it is not changed |

The #8 preconditions accepted enabled DCDC4 programmed to 800000 microvolts
and the existing 400 MHz source. Neither voltage nor frequency was physically
measured. These were short boot checks, not sustained runs or acceleration.

The three exact target contracts passed on the real Zero 3W CPU under #7:
15,188 CCU checks, 1,102 consumer checks, and 133 new PCK ownership/wait cases
plus its existing matrices. They use fake MMIO and do not test GPU hardware.
Host ASan/UBSan and eleven rejecting causal mutants also pass. The prior
native kernel suite is reused because all 14 source inputs are unchanged.
The #8 CTF decoder checks 20,802 types and its split-debug CRC matches.
Native image SHA256:
`eb1e35340f0869d0e532025cf74e93e0119c2966f249d82bcc26f6a7f35b0808`;
experimental Zero 3W DTB SHA256:
`7a840baec0c4c76455b1ce8b2252e63bfea7a121e88c38c43c413ab1575dab7c`.

## Local binding

The SoC node uses `allwinner,sun60i-a733-gpu`, followed by
`img,img-bxm-4-64` and `img,img-rogue`. The A733 compatible is a local EmberBSD
binding addition. It is not presented as an accepted Linux or NetBSD binding.
The product/family strings appear in the upstream PowerVR Rogue binding at
Linux revision `2a4d91142e538ff5580c6bf48b5e668d8131fd9a`; that revision does
not list A733. The pinned vendor BSP uses the less specific `img,gpu`.
The consumer matches only the A733 compatible, not either fallback.

The local binding requires:

- A single register range at `0x01800000`, covering PBVNC at `+0x20`.
  The DT preserves the BSP range size `0x8ffff`; the consumer maps only `0x28`
  bytes and performs one 64-bit fault-aware read only outside observe-only mode.
- One `core` clock, `<&ccu CLK_GPU0>`, from the native A733 CCU.
- One domain, `<&pck600 PD_GPU_TOP>`, with domain ID 5 and one argument cell.
- `gpu-supply` referencing the AXP8191 `dcdc4` regulator node.
- The empty boolean property `netbsd,consumer-managed-power`.
- The current DT also sets empty `netbsd,observe-only`, a local EmberBSD
  diagnostic opt-in. Nonempty values fail with `EINVAL`. It preserves provider
  error handling but exits after completed CCU observation, before OPP checks
  or GPU mapping, including when readiness succeeds.

The hardware description also records GPU reset and SPI interrupt 63, but
the normal identification path does not acquire or operate them. CCU readiness
checks the actual GPU reset bit. The SoC node is disabled by default.
Zero 4 supplies DCDC4 and enables the identification consumer; Zero 3W
inherits those declarations. Use these DTBs with their matching kernel.

## FDT power opt-in

`netbsd,consumer-managed-power` is an explicit EmberBSD extension to FDT
attachment. An empty property skips only automatic power-domain enabling
before attach. Pinctrl initialization and post-attach behavior remain intact.
A nonempty property fails pre-attach with `EINVAL`. Ordinary nodes retain
checked power error propagation and firmware fallback for unregistered
providers.

A consumer carrying this property owns all domain sequencing and error
handling. It must use strict provider APIs and must not interpret a missing
provider as readiness. This consumer requires the opt-in. Its normal path
only queries state; the separate experimental path below owns local CCU writes.

## Readiness and result

The probe uses an autoconfiguration finalizer, after real devices and their
interrupt-time configuration have completed. It runs once even if another
finalizer requests more passes. It does not use the parent-specific deferred
queue: FDT can attach this child after the `/soc` parent's attach has returned.
An unavailable provider is reported once without holding boot configuration
pending or retrying the probe.

The probe validates the exact supply, power-domain and clock references.
It requires the supply enabled, GPU_TOP
statically ON, and [CCU readiness](a733-provider-state.md). It accepts only
400 or 600 MHz at a programmed 800000 microvolts, the two pinned BSP operating
points with the same voltage across all listed speed bins. Other settings
return `EOPNOTSUPP`; they are neither changed nor declared electrically unsafe.
Supply/domain OFF, dynamic power mode, missing providers, unsupported clocks,
or query errors prevent GPU mapping and reads.

For a completed CCU observation that is not ready, the consumer reports the
first failed condition, the queried DCDC4 setting in microvolts, both
oscillator rates, and the eleven raw CCU registers. Differing registers show
both samples. This uses one bounded provider inspection, without a second
query, writes, or GPU MMIO. Acquisition errors do not print an unavailable
snapshot. The reported supply setting is not a physical voltage measurement;
the 800000-microvolt operating-point check follows CCU readiness.

Outside observe-only mode, after all checks pass, `bus_space_peek_8` reads PBVNC. The driver prints the
actual raw value and four 16-bit fields. Expected A733 identity is
`36.56.104.183`, raw `0x00240038006800b7`. Every other value, including zero
and all ones, is reported as unexpected and rejected with `ENODEV`; a bus
fault returns `EFAULT`. No expected-value fallback exists. Mapping and
acquired handles are released on both success and failure. Firmware-owned
hardware state is never unwound by disabling resources.

Queries are observations, not a reservation against later writers. Equal CCU
snapshots cannot exclude every intervening change. Fault-aware reads handle
synchronous faults but do not guarantee that a stalled bus transaction will
finish. The normal consumer has no cold-start path or retry policy; firmware that
leaves the GPU OFF produces an explicit unavailable result. GPU_CORE domain 6
is not requested by the pinned BSP GPU binding or operated by the normal path.
The PCK provider separately reports its raw state at attachment. That earlier
observation is not consumed as a GPU readiness guarantee or power capability.
The pinned DDK selects a live `USE_FPGA` path that operates CORE6 directly;
the experimental path below preserves that handshake instead of copying the write.

## Experimental clock preparation

This experiment has not achieved identification. Its separate, empty property is
`netbsd,experimental-clock-prepare`. It is mutually exclusive with
`netbsd,observe-only`; malformed or conflicting properties fail before any
provider action. The normal board DTS files remain observe-only. Build a
separate Zero 3W artifact, with the matching kernel sources and host tools:

```sh
CPP=/path/to/aarch64--netbsd-cpp DTC=/path/to/nbdtc \
    sh ember/tools/a733-gpu-experimental-dtb.sh /absolute/output
```

The builder produces `sun60i-a733-orangepi-zero3w-gpu-experimental.dtb`;
it neither replaces the normal DTB nor installs anything. Acceptance requires
a recoverable matched kernel/modules/DTB bundle and an explicitly selected
experimental boot. Zero 4 active preparation is not covered by this builder.

The one-shot path requires enabled DCDC4 programmed to 800000 microvolts,
TOP statically ON, the observed PCK-600 v1.1 single-Q configuration and CORE
policy ON/status OFF. Native CCU ownership excludes preexisting writers.
The actual RTC/DCXO and normalized REF must yield an already running 400 MHz
PERIPH0 branch and 200 MHz AHB. With the GPU gates off and reset asserted,
it selects mux 3/M=0, releases bus reset, enables the bus gate and then the
module gate. Only `GPU_CLK` and `GPU_BGR` are written. It never calls recursive
clock enable, changes a PLL/supply or writes a PCK register, including PWCR.

PCK then takes at most 1001 observations, separated by at most 10000 microseconds
of total delay. MMIO acquisition time is additional. Both domains must reach
strict stable ON with QACCEPTn high and QDENY low. Supply and clock checks repeat
before the single PBVNC read. This attempts to complete the pending transition;
the #7 snapshot cannot distinguish a stalled PCSM phase from Q exit. A timeout
is not evidence of which phase failed.

The clock lease is irreversible before its first attempted configuration write.
PCK ownership is retained before that call. Every later error retains resources
until reboot, including rejected writes, timeouts and an unexpected PBVNC.
No gate removal, reset assertion, CORE power-off, detach or automatic retry is
performed. Shared firmware sources are borrowed; native retune/disable and
GPU-local mutations are refused while reserved. External firmware writers
cannot be excluded by these native locks.

The production-body contracts cover reservation, ordering, failures at each
write, bounded waiting, conflicting opt-ins and retention. Run the three
existing A733 clock/power/identification contracts and
`sh ember/tools/a733-gpu-prepare-mutations.sh` for the causal negatives.
The physical #8 attempt timed out before identification. The experiment stays
disabled in normal board DTBs; further active sequencing requires new evidence.

## Sources and software checks

Hardware resources, supply and operating points come from Orange Pi BSP
[`2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f`](https://github.com/orangepi-xunlong/linux-orangepi/tree/2ac08e8c7cdc28abbdc5c9a9dd812f887ae9c79f):
`bsp/configs/linux-6.6/sun60iw2p1.dtsi` and the Zero 3W board DTS.
PBVNC offset/fields and the expected tuple come from the same BSP's
`img-bxm` DDK sources. Provider formulas and constraints are documented in
[provider observations](a733-provider-state.md). The upstream product strings
can be checked in the pinned [Rogue binding](https://github.com/torvalds/linux/blob/2a4d91142e538ff5580c6bf48b5e668d8131fd9a/Documentation/devicetree/bindings/gpu/img%2Cpowervr-rogue.yaml).

```sh
sh ember/tools/a733-gpu-identification-contract.sh
sh ember/tools/fdt-power-attach-contract.sh
GPU_ID_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/a733-gpu-identification-contract.sh
```

The contract executes the production driver with fake providers and MMIO,
and the production autoconfiguration queue and finalization functions.
It proves the order before MMIO, rejects malformed resources and provider
errors, tests both allowed operating points and unexpected identities, and
checks that every acquired handle and mapping is released. It reproduces the
late `/soc` child queue hazard and checks finalizer completion, provider
failure and repeated hook passes. Clock diagnostics test both acquisition and
readiness errors, actual queried voltage, raw samples, and rejection before
GPU mapping. Observe-only checks run the real finalizer with both ready and
unready fake providers and require zero GPU mapping and reads. These checks do not prove physical GPU identity or acceleration.
