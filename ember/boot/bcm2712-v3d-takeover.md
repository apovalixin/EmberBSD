# BCM2712 V3D reset takeover probe

This opt-in probe advances physical V3D identification to exclusive native
reset ownership. It does not allocate DMA memory, program the MMU, submit
TFU/CSD/CL work, expose DRM, or establish graphics acceleration.
The normal `EMBER64` configuration remains a passive, unclaimed observer.
`EMBERV3D` adds `BCM2712_V3D_TAKEOVER` explicitly.

## Design and boundary

The existing ACPI observer validates GPU resources, firmware clock state/rate,
PM readiness and V3D 7.1 identification first. The takeover then claims the
existing BCM2712 PM mapping; it never creates another PM mapping. The claim
requires the exact PM physical base and size used by the recorded CM5 ACPI.
Only one device can claim the reset interface. A second claimant is rejected.

Persistent HUB, CORE and SMS mappings belong to the claimant. Before writing,
the probe records identity, MMU, TFU, CSD, GMP and interrupt state. It requires
the observed initial SMS combination REE=0/TEE=0x50, no active TFU/CSD work,
and a disabled, non-flushing MMU. Other SMS modes are unsupported by this
first destructive probe, even if the passive observer can identify them.
These observations are eligibility checks, not proof that firmware cannot
submit future work. The explicit reset operation establishes the native
takeover; the probe does not claim a firmware ownership protocol exists.
CT0CS, CT1CS and PCS are inventory only: the pinned V3D 7.1 sources do not
define their active-state fields. There is no proven no-active-CL gate.
This opt-in initialization reset may cancel unknown firmware command-list
work and does not resume it. It is for controlled board bring-up without a
preceding user GPU session and with a verified boot recovery path.

Before the first write, the PM claim is sealed until reboot. The probe masks
only defined V3D 7.1 HUB/CORE interrupt fields through their W1S registers,
checks the mask readback and preserves other fields. It does not restore
previous masks through W1C registers. There is no interrupt handler.

SMS clear-power-off and REE reset commands have bounded polling and validated
mode/status fields. Documented SEQ_PC/HUBCORE_STATUS fields may show progress;
completion still requires exact REE=0/TEE=0x50. The existing PM owner then
asserts only V3DRSTN bit 6,
checks the complete value, waits at least one microsecond, and deasserts it.
PM writes include the hardware password and preserve all other readable
bits. Clock state must already be enabled with a nonzero configured rate;
this probe changes neither frequency nor clock state.

After reset the probe masks and checks interrupts again, records another
inventory, and verifies identity, SMS, PM and the defined MMU/TFU/CSD/GMP
eligibility fields. This does not prove CL inactivity, DMA leases, or future
firmware exclusion. A
failure after the first attempted write retains all mappings and ownership;
there is no retry, detach, power-off, or speculative rollback. Success also
retains ownership and mappings until reboot. No DMA lifetime is implied.
Fault-aware MMIO handles recoverable synchronous faults; asynchronous SError
and a stalled interconnect remain outside its guarantee.

The ACPI platform sets `faa_bst` to `arm_generic_bs_tag`; `acpi_fdt` passes
it through `aa_memt` to both drivers. In `aarch64/aarch64/bus_space.c`, that
tag's `bs_po_4` is `generic_bs_po_4`, which uses `cpu_set_onfault` around
`generic_dsb_bs_w_4`. The assembly writer executes `dsb sy` after its store
and before returning to `cpu_unset_onfault`. The explicit bus-space barrier
and readback follow each successful write. This is not the unused
`bs_notimpl_bs_po_4` entry.

## Source facts

The register and lifecycle reference is Raspberry Pi Linux
[`43c132e8863c3bff3647033b6a7d2bf87b15501c`](https://github.com/raspberrypi/linux/tree/43c132e8863c3bff3647033b6a7d2bf87b15501c):
`drivers/gpu/drm/v3d/{v3d_drv.c,v3d_gem.c,v3d_regs.h,v3d_drv.h}` and
`drivers/pmdomain/bcm/bcm2835-power.c`. The implementation here uses native
EmberBSD bus-space, locking and error propagation; it does not import Linux
driver code.

For BCM2712 the Linux PM node has no ASB mapping or domain clock. Its reset
path therefore changes PM_GRAFX_2712 bit 6, with the one-microsecond delay
before reset release. The firmware-clock ops have no enable/disable callback;
their existing prepared state is read from firmware. This does not justify
inventing a SET_CLOCKSTATE or power-domain mailbox transaction.

The first physical sample was V3D 7.1 on CM5 Rev 1.0, BCM2712 D0, 4 GiB.
HUB_IDENT1=0x81117 is also present in Mesa 26.2.4's `v3d_noop.c` V3D 7.1
profile. Its old WITH_TFU bit is not used as an absence gate. TFU remains
unexecuted. CORE_IDENT1 is read independently; HUB_IDENT1 is not substituted
for it. The complete identification receipt remains in
[the passive observer guide](bcm2712-v3d.md).

## Implementation checklist

- [ ] Keep option-free observation free of claims and all hardware writes.
- [ ] Add exact-resource PM ownership, seal and single reset APIs.
- [ ] Add persistent mappings, bounded SMS reset and validated PM readback.
- [ ] Preserve unrelated PM and interrupt-mask fields; never restore W1C.
- [ ] Exercise actual production sources with fake bus/ACPI/mailbox services.
- [ ] Check no opt-in writes, second/foreign owner, every map/read/write
  failure, SMS modes/states/timeouts, PM corruption, reset ordering/delay,
  quarantine and absence of DMA/submission.
- [ ] Run the passive regression and new contracts with ASan/UBSan on macOS.
- [ ] Independently review, then cross-build the complete clean-commit kernel.
- [ ] Record physical acceptance separately; no physical takeover is yet proven.

The next DMA experiment needs a separate design and acceptance. In particular,
_CCA=0 requires actual bus_dma mappings and CPU/GPU cache synchronization;
kernel-owned 16 KiB TFU copies need 64 bytes of source read-ahead padding
before an unmapped guard. Neither allocation nor this reset probe proves DMA.
