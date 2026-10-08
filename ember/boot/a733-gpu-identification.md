# Firmware-ready A733 GPU identification

The native `sun60igpuid` consumer identifies an already prepared A733 GPU.
It does not initialize or power the GPU, load firmware, establish interrupts,
allocate DMA, submit commands, expose a DRM device, or provide acceleration.
The [driver](https://github.com/oxtech-ember/EmberBSD/blob/main/sys/arch/arm/sunxi/sun60i_a733_gpu.c)
is owned by EmberBSD. Software contracts, the complete GCC16 kernel build
and physical attachment on Zero 3W are verified. Physical GPU identification
has not yet been accepted.

## Physical result, 2026-10-08

A 4 GiB Orange Pi Zero 3W booted the matched `EMBER64 #4` kernel, four
modules and A733 DTBs from `4125fa28057fa1f9cdd19e825a03c458485aa3e6`.
The board revision was not recorded. The existing vendor boot0/U-Boot and
boot script were preserved. Eight CPUs, microSD root and Wi-Fi/SSH returned.

`sun60igpuid0` attached and reported `identification unavailable at
clock/reset state: 16; firmware state left unchanged`. The query reached
CCU readiness after observing the supply enabled and GPU_TOP statically ON.
That build's `EBUSY` did not identify which clock, gate, reset or snapshot
check failed.
The driver therefore did not map or read GPU registers. This establishes
the unavailable path on this firmware configuration, not the expected
PBVNC value or a working accelerator.

Before installation, the five provider/consumer software contracts passed
48,077 assertions on the board with fake hardware. The complete existing
native kernel-source suite also passed with GCC 16.2 and Python 3.14.8.
Neither suite substitutes for hardware identification. Native image SHA256:
`ad5de33eca8c2f356cf73588fbee6a2fdbe92fe311cc239cd8dde5926717a366`;
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
  bytes and performs one 64-bit fault-aware read.
- One `core` clock, `<&ccu CLK_GPU0>`, from the native A733 CCU.
- One domain, `<&pck600 PD_GPU_TOP>`, with domain ID 5 and one argument cell.
- `gpu-supply` referencing the AXP8191 `dcdc4` regulator node.
- The empty boolean property `netbsd,consumer-managed-power`.

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

After all checks pass, `bus_space_peek_8` reads PBVNC. The driver prints the
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
GPU mapping. These checks do not prove physical GPU identity or acceleration.
