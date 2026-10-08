# A733 read-only provider observations

These interfaces prepare a firmware-ready GPU identification consumer. They
observe the existing configuration; none enables the GPU, changes a supply,
programs a PLL, releases reset, or establishes hardware acceleration.

## Supply

Use the acquired supply's
[regulator state query](regulator-state.md). AXP8191 reads its enable bit and
preserves I2C errors. Its voltage query returns the programmed setting,
not a physical measurement. Querying or acquiring a regulator does not reserve
it against another consumer.

## Power domain

`fdtbus_powerdomain_is_enabled_index(phandle, index, &enabled)` is a strict
query of one `power-domains` entry. Missing providers return `ENXIO`; providers
without the optional `pdc_get` callback return `EOPNOTSUPP`. Invalid arguments
or malformed specifiers return `EINVAL`; a missing index returns `ENOENT`.
Errors leave the result unchanged. The automatic attach firmware fallback does
not apply to this query, and no enable callback is called.

PCK600 reads policy, emulation and status twice under its existing mutex.
Only matching, stable static ON (8) and OFF (0) produce a boolean result.
An inconsistent snapshot returns `EBUSY`; dynamic policies, emulation and
other modes return `EOPNOTSUPP`. A previously failed transition remains
quarantined and returns `EIO` without MMIO. A locked static policy can be
observed. Queries never alter timing registers or clear quarantine.

## GPU register clock path

`sun60i_a733_ccu_gpu_ready(gpu_clock, &core_hz, &bus_hz)` accepts the native
A733 GPU module clock handle. Both results are published only on success.
It checks the GPU module gate and update bit, bus gate, deasserted GPU reset,
and GPU AHB master gate, then verifies two equal CCU register snapshots.
Missing handles or the oscillator provider return `ENXIO`; a foreign clock
provider returns `EOPNOTSUPP`. Changing or inactive required hardware returns
`EBUSY`. Invalid or aliased output pointers return `EINVAL`.

The observation supports the current 24 MHz crystal and verifies PLL_REF's
actual normalization. It decodes integer PLL_GPU0 and PLL_PERIPH0 input and
output dividers, lock/enables, and the selected output gate. Both SDM enable
locations must be clear for each required PLL. All six GPU parents and the
five documented GPU dividers are supported. AHB may use SYS24M or the decoded
peripheral 600M branch; LOSC/IOSC readiness is unsupported. Unsupported modes
or fractional-Hz results return `EOPNOTSUPP`; rates beyond `u_int` return
`ERANGE`. Nominal clock names do not determine the returned frequencies.

The register facts come from the [pinned BSP and Linux v5 submission](a733-accelerator-clocks.md#sources-and-validation).
In particular, PLL_PERIPH0's input divider and separate output gates are
included here; the generic clock tree's rate calculation is not used.

CCU observations do not serialize existing generic clock/reset writers.
Equal snapshots cannot detect every intervening change or reserve resources
against a later change. A future consumer must complete all supply, power and
clock checks before accelerator MMIO and must not use this result to justify
active resource ownership or cleanup. MBUS/DMA, IRQs, firmware, GPU_CORE policy,
and cold-start sequencing remain separate work.

## Software checks

Run `regulator-state-contract.sh`, `a733-power-contract.sh`,
`fdt-power-attach-contract.sh`, and `a733-accelerator-clock-contract.sh` from
`ember/tools`. They execute the production bodies with fake I2C/MMIO and
check success, failure propagation, state preservation, stable reads, and
absence of writes during queries. Existing transition and attach tests remain
controls. Passing these contracts and focused cross-compilation is separate
from physical identification or accelerator workload acceptance.
