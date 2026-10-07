<!-- Origin: EmberBSD; AI-assisted whole-context EXEC ownership contract. -->
# Whole-context EXEC ownership

VIRGL remains disabled. This source contract snapshots actual context
attachments before EXEC, so empty or partial BO hints cannot omit backing
reachable through encoded commands. Host and native software contracts and
object compilation pass; live graphics qualification remains a separate gate.

## Admission and dependencies

A context contains at most 65536 unique attached BOs. Duplicate handles
share an entry. Commands and referenced hints are copied before the final
attachment mutex. Allocation follows a count/read/unlock/relock sequence:
new growth beyond capacity returns EAGAIN; shrink uses the current set.
An in-place heapsort and binary search validate hints by object identity.
Missing handles return ENOENT, foreign-context hints EINVAL, and backing
without the [C1 loaded-map proof](dma-eligibility.md) and open lease
EOPNOTSUPP. CTX_ATTACH/DETACH retain their synchronous ordering; they do not
copy backing in the audited classic renderer and gain no DMA token.

The lock order is core file/PRIME locks, attachment, unique WW reservations,
then submit. Completion takes neither attachment nor reservations. Hint
lookup finishes before attachment locking. The driver closes the native
WW acquire context after failed multi-object acquisition; generic helpers
already released its locks. Count zero and one preserve their own paths.
The attachment mutex lasts through queue acceptance, not GPU completion.
No delayed-free worker is joined under attachment or reservation locks.

One 15*HZ dependency budget spans explicit input and implicit exclusive,
shared and native-ledger fences. WW acquisition remains interruptible but
untimed; this is not a hard ioctl wall-clock bound. Only a positively typed
same-device EXEC fence with the same non-reused software context key may
skip an implicit pending GPU wait. Known cookie errors behind the contiguous
publication prefix still reject. Foreign, unknown and other-key fences wait.
Timeout, signal and dependency error return before PRE without resetting
an otherwise healthy device. Explicit input always uses the wait path.

The software key is device-private and does not change wire IDs or UAPI.
A checked u64 counter reserves zero and fails with EOVERFLOW before IDA or
host context creation when exhausted. Failed opens burn their key. A fence
retains its key without a pointer to a potentially freed file-private object.

## Ledger and lifetime

A charged array owns snapshot references and immutable per-BO members.
After producer admission and fence emission, all members are validated and
registered before reservation replacement/unlock. Each attempt then issues
full-map PREWRITE|PREREAD. No fallible metadata allocation occurs after PRE.
The common queue tracks reservation-lock ownership separately from emission.

Each BO ledger retains outstanding EXEC and [explicit 3D transfer](transfer-wait.md)
members, independently of the
reservation slot. Descriptor pressure closes only the rejected attempt's
POSTWRITE|POSTREAD token before the existing bounded wait. The original
record and fence survive; retry opens a new token without another emission.
A successful enqueue can retire its cookie before returning, so submission
does not access that cookie afterward.

Finish claims the cookie's token, POSTs every member outside DMA locks,
then removes its ledger membership and fence reference. All POSTs precede
callbacks, wait completion, delayed BO puts and own-fence readiness. Separate
same-context operations can overlap only on C1's coherent, direct backend.
Operation POST does not close the persistent lease, grant whole-BO CPU
ownership, or imply query DONE. The [C2 lease](backing-lifetime.md) still ends
only after verified UNREF or reset with joined producers and recovered cookies.

Temporary raw retirement pins are bounded by active FINISHING members;
checked member additions and retained-fence capacity bound their number.
Reset joins every producer and dequeue owner before canceling recovered
cookies, draining ledger members and closing leases. It cannot overlap a
remaining operation POST. EXEC uses ordinary snapshot GEM references;
C2's zero-ref UNREF retains its separate raw-owner path, without resurrection
or a permanent registry GEM reference.

New snapshot/member, hint and dependency scratch storage shares a 16 MiB
per-device budget. Checked arithmetic and charging precede allocations.
The snapshot's charge remains until actual array free, including delayed
work. Budget exhaustion returns ENOMEM without PRE or reset. This cap does
not cover preexisting command payloads, descriptors, BOs or driver storage.
Dependencies use one BO's bounded fence scratch, not a BO-by-operation matrix.

## Reproduction and boundaries

Run `sh ember/tools/virtgpu-exec-contract.sh` for 47 production-linked groups.
Set `EXEC_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for host sanitizers. `SUBMIT_SOURCE_ROOT` selects an immutable source export;
the pre-C3 baseline compiles and fails the empty/partial-hint assertions.

Extraction covers the actual ioctl, snapshot/sort/budget/key functions,
queue, ledger, fence status and finish paths. It also compiles the native
reservation replacement/getters, DRM WW retry/unlock and native WW
init/done/fini bodies. Low-level wait/wound arbitration, fence waits,
transport, bus sync, allocation, usercopy and descriptor publication are
controlled seams. Deterministic interleaving tests cover concurrent token
POST ownership; these do not prove hardware scheduling or native DMA.
The separate resource contracts exercise real core open/close/PRIME paths
with pthread scheduling, plus duplicate-count, unique-cap and closing gates.

Tests include omitted/duplicate hints, growth/shrink, invalid membership,
byte/capacity/member limits, every fallible allocation, delayed charging,
overlap/reversed replies, reservation replacement, shared dependencies,
known errors behind the prefix, reused wire IDs, combined deadlines,
WW errors, ENOSPC rearm, stop during PRE, immediate completion, multi-cookie
cancellation and reference/pin/fence/budget conservation. Existing submit
contracts retain fd zero, private-file unwind and malformed-response coverage.

On 2026-10-07, NetBSD 11/AArch64 with GCC 16.2 passed all 47 EXEC groups
and 132 prior groups. The first native run stopped before executing tests:
GCC rejected an unused legacy helper extracted by the fixture. A narrow
fixture correction passed 154 groups but omitted the same helper still
used by completion cases. Both failures and their real exit statuses were
retained. The final extraction condition includes it for the pre-C3 ioctl
or completion mode, without suppressing warnings or changing production.
The remaining 16 completion and nine legacy groups then passed; the 154
unchanged groups were not rerun.

Production is `386cf6ac8a4916e486d6ed391e94b72b33254b8e`; fixture corrections
end at `f80823c2ef636ff2a60fde06939b1dc6b16781f0`. Its source export SHA256 is
`4b5b6099f7884f716b5aac55f2b5843d3e8b3a3b9d84d96f04329558d2ec689e`.
Native 154-group log SHA256:
`38b890f7b1b8286dba8cf6e23d3ef4c473c6e7c4936f0d4e3c697b66b5113fb6`.
Remaining contracts/object-build log SHA256:
`6b4cf258dd75d85b382b6a5a06558bb1d457cbdff320fbf18dd18f86d0fd2654`.
Configuration and dependencies were regenerated. Fourteen fresh driver
objects built with base GCC 12.5 and normal `-Werror` in 2.39 seconds,
with 67,412 KiB peak RSS and no swaps. No kernel was linked, installed or
booted; the partial object directory is not a matched kernel artifact.

[Explicit 3D transfer and WAIT](transfer-wait.md) now share this typed ledger
and metadata budget. The linked [controlled console/2D stage](controlled-console.md)
adds finite legacy phases, private console copies and synchronous cursor status.
Full packet/cap enforcement and live loaded-map/host qualification remain separate.
[Classic wire-fence exhaustion](classic-fence-range.md) now has a separate
native software/object contract preserving this ledger and rejection ownership.
This stage does not enable acceleration or certify arbitrary bus_dma backends.
