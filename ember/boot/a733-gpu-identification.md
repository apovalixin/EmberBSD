# Firmware-ready A733 GPU identification

The native `sun60igpuid` consumer can identify an already prepared A733 GPU.
The current A733 DT enables its diagnostic `netbsd,observe-only` stage, which
never maps or reads GPU registers, even when the queried providers are ready.
It does not initialize or power the GPU, load firmware, establish interrupts,
allocate DMA, submit commands, expose a DRM device, or provide acceleration.
The [driver](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/sun60i_a733_gpu.c)
is owned by EmberBSD. Software contracts, the complete GCC16 kernel build
and physical attachment on Zero 3W are verified. Physical GPU identification
has not yet been accepted. The physical result below applies to #5; the new
RTC/DCXO and GPU_CORE observations have passed software contracts and focused
cross-compilation, but still require a matched kernel boot.

## Physical result, 2026-10-08

A 4 GiB Orange Pi Zero 3W booted the matched `EMBER64 #5` kernel, four
modules and A733 DTBs from `f625fd9a0dea8be54146a08f7771714b8f4ade56`.
The board revision was not recorded. The existing vendor boot0/U-Boot and
boot script were preserved. Eight CPUs, microSD root and Wi-Fi/SSH returned.
Installed hashes matched the bundle; the previous #4 boot files were backed
up on the board and development host before installation.

`sun60igpuid0` attached and reported `GPU module gated (error 16)`, with
DCDC4 programmed to 800000 microvolts and GPU_TOP statically ON. Both CCU
snapshots were identical (`changed 0x000`). The fixed-clock provider reported
24 MHz twice; this is a device-tree value, not a crystal measurement. Selected
raw values explain why a firmware-ready probe cannot access the GPU:

| Register | Value | Observation |
|---|---|---|
| GPU_CLK, `0xb20` | `0x00000000` | Module clock gate is clear |
| GPU_BGR, `0xb24` | `0x00000000` | Bus gate is clear and reset is asserted |
| PLL_GPU0, `0x0e0` | `0x41104500` | Raw dedicated GPU PLL state; it is not changed |

The driver did not map or read GPU registers and made no clock/reset writes.
The prior #4 result stopped with the same `EBUSY`, without identifying its
cause. The new observation establishes the gated/reset state, not the actual
PBVNC value or a working accelerator. The programmed regulator voltage is not
a physical measurement. Active preparation of the GPU remains separate work.

The new CCU/GPU software contracts pass 14,643 and 786 assertions on the
board with fake hardware, and on the host with ASan/UBSan. The existing
native kernel-source suite passed with GCC 16.2 and Python 3.14.8 before #4;
all 14 source inputs are unchanged in #5. These results do not substitute
for hardware identification. Native image SHA256:
`26a4e43ec4a0132c345212b2aef11d6a975548bdf9093c473b971391adc778fa`;
Zero 3W DTB SHA256:
`9b37b1833a718733616900aaceb1263807c8dd80efcf2133702bb0ea9439bea3`.
This was a short boot check, not a sustained run.

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
the identification consumer does not acquire or operate them. CCU readiness
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
provider as readiness. This identification consumer requires the opt-in and
only queries state; it never invokes enable, disable, reset or voltage writes.

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
finish. The consumer has no cold-start path or retry policy; firmware that
leaves the GPU OFF produces an explicit unavailable result. GPU_CORE domain 6
is not requested by the pinned BSP GPU binding and is not operated here.
The PCK provider separately reports its raw state at attachment. That earlier
observation is not consumed as a GPU readiness guarantee or power capability.
The pinned DDK selects a live `USE_FPGA` path that operates CORE6 directly;
this must be resolved before a native cold-start implementation.

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
