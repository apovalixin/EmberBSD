<!-- Origin: EmberBSD; AI-assisted per-cookie completion and reset contract. -->
# Bounded completion and reset

VIRGL remains disabled. This foundation prevents an out-of-order response
from exposing an older, still-owned command as complete. It does not establish
correct backing DMA for transfers, encoded EXEC commands, or CPU mappings.
Those paths require their own ownership and visibility work before 3D use.

Each emitted fence retains its own ready bit and result. Only its verified
response cookie, definitive local rejection, or reset cancellation can mark
it ready. The shared timeline publishes the contiguous ready prefix.
`last_seq` records that prefix; it cannot independently signal a fence.
Duplicate results preserve the first result, including known success behind
a failed earlier command. Each list reference is released exactly once.

The retained list is bounded by the saved native control-ring size. Ready
records behind a gap still count. Admission waits outside spinlocks for at
most `5 * HZ`, sharing its deadline with subsequent descriptor pressure.
A transient full list can recover without reset. Admission timeout returns
ETIMEDOUT and stops the device after dropping the submit mutex. Stop wakes
admission and returns ENODEV. Normal asynchronous submission does not wait
for its own GPU completion. Pre-emission rejection unlocks BO reservations
and never publishes a zero-sequence fence.

## Ownership and teardown

A successful response runs its callback and existing transfer completion
step before retiring cookie storage and marking its exact fence ready.
The current transfer step is preserved, not certified as DMA-correct here.
A local queue rejection disposes its cookie before publishing its result.
Malformed or failed responses stop/reset the transport before uncertain
storage is released. Stop seals fence admission before reset; terminal
fence publication remains closed until cleanup recovers all request cookies.

Cleanup joins accepted producers, then both dequeue workers. Native
`del_vqs` joins interrupt callbacks and cancels outstanding cookies. Only
then does cleanup close eligible backing leases, publish terminal results,
and drain console, configuration,
and delayed-object workers. Console/config waiters can therefore observe
terminal fences before cleanup waits for their exit. No submit/spin lock is
held over these joins, and completion never takes the submit mutex.

Producer registration and stop use the same fence lock. The registered count
includes cancellation after the submit mutex is released, for control and
cursor queues. Its final decrement/wakeup holds the admission wait interlock:
cleanup cannot pass zero before the producer's last queue-state access.
Already-stopped callers do not register or extend that join. Internal late
callers remain owned by the subsequent console/config/object worker drains.
Deinit repeats object/reset drains after modeset cleanup, which can release
final object references, before freeing queue storage and synchronization.

This relies on the existing no-live-detach contract (`EBUSY`): external DRM
callers cannot race final device destruction. It is not a generic device
lifetime implementation. The driver remains stopped after fatal reset;
this foundation does not restart queues or recover a renderer context.

The [eligible backing lease](backing-lifetime.md) extends central finish with
token POST and extends reset drain with lease POST before terminal fences.
Its scope is ATTACH/UNREF; other DMA ownership remains unverified.

## Reproduction and evidence limits

From the repository root:

```sh
sh ember/tools/virtgpu-completion-contract.sh
COMPLETION_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-completion-contract.sh
```

The harness shares the submit fixture and extracts production fence
allocation, emission, prefix publication, stop, completion, reset cleanup,
control/cursor submission, response validation, dequeue and cookie retirement.
It retains the production fence/vbuffer/wire structures. The older baseline
can be selected with `SUBMIT_SOURCE_ROOT=/absolute/baseline-export`.

Sixteen deterministic groups cover higher-before-lower responses, retained
gaps, known success/error, duplicate/late results, ordered replies, transient
and persistent admission pressure, shared deadlines, locked pre-emission
arrays, stop during construction/pressure/post-unlock cancellation, sealed
late producers, cursor cancellation, malformed replies and host errors.
Assertions conserve fences, cookies, arrays, allocations and BO references.
Repeated reset cleanup and console/config wait dependencies are included.

Generic fence primitives, allocators, locks, transport, worker scheduling,
waits and transfer completion are controlled seams. Reset does not signal
fences in the seam: actual production cleanup must recover cookies first.
These deterministic interleavings do not prove every scheduler race, native
IRQ/DMA behavior, full modeset deinit, or live hardware operation. Existing
resource/context/capset fixtures retain their separate modeled fence seams;
the new fixture exercises the actual fence implementation.

Host GREEN passes all sixteen groups with and without ASan/UBSan; the same
fixture fails fourteen groups against the unchanged baseline. On 2026-10-07,
production revision `8ff0fac41ed03f6f7d38bc7dd611a97f113b555d` passed these
sixteen and the 53 submit, eight resource, six context and five capset groups
on NetBSD 11/AArch64 with GCC 16.2. The earlier VirtGPU fixture then failed
compilation on a misleading-indentation diagnostic. Revision
`af32f4b343c4c2049865ba95eba2ed90db7a89df` reformatted only modeled helpers,
preserving `-Werror`, assertions and production code. Its nine affected groups
passed; the other 88 unchanged groups were not rerun.

All twelve configured VirtGPU objects using the changed header were rebuilt
fresh with base GCC 12.5 and the existing `-Werror` flags. They passed in
2.18 seconds, with 67,012 KiB peak RSS and no swaps. No full kernel was linked,
installed or booted; the partial object directory is not a kernel artifact.
These 97 contract groups and object builds do not establish live reset,
IRQ safety, backing DMA correctness or GPU acceleration.
