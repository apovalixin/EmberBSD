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
It verifies two equal CCU register snapshots, then checks the GPU module gate
and update bit, bus gate, deasserted GPU reset, and GPU AHB master gate.
Missing handles or the oscillator provider return `ENXIO`; a foreign clock
provider returns `EOPNOTSUPP`. Changing or inactive required hardware returns
`EBUSY`. Invalid or aliased output pointers return `EINVAL`.

`sun60i_a733_ccu_gpu_inspect(gpu_clock, &state)` separates observation from
readiness. A nonzero return leaves `state` unchanged. Zero means a complete
observation: `readiness_error` still determines whether the path is ready.
The result includes two samples of eleven CCU registers, their changed-bit
mask, two oscillator rates, and the first failed condition. Derived core and
bus rates are valid only when `readiness_error` is zero; otherwise they are
zero. The existing `gpu_ready` wrapper retains its errors and output rules.

After handle/provider and 24 MHz oscillator validation, inspection always
performs exactly 22 CCU reads and a second oscillator-rate query. It neither
polls nor writes, including when the samples differ. Snapshot changes take
priority, followed by oscillator changes, module gate/update, bus gate/reset,
AHB master gate, PLL_REF, GPU parent/divider, and AHB parent/divider checks.
The raw samples retain evidence of other failed conditions as well.

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
absence of writes during queries. The CCU contract also checks each refusal
reason, simultaneous failures, complete differing samples, unchanged outputs
on acquisition errors, and the compatibility wrapper. Existing transition and
attach tests remain controls. Passing these contracts and focused cross-compilation is separate
from physical identification or accelerator workload acceptance.
