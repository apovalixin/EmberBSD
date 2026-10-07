<!-- Origin: EmberBSD; AI-assisted eligible backing lease and retirement contract. -->
# Eligible backing lifetime

VIRGL remains disabled. This source contract covers qualified backing and
single-BO ATTACH_BACKING/RESOURCE_UNREF only. It does not certify encoded
EXEC, query results, context attachment/detachment, explicit transfers,
WAIT, console copies, or a live coherent host topology. Those gates remain
necessary before feature-on. The current required=false 2D path retains
its existing PREWRITE/POSTWRITE policy.

## Two independent phases

The [owned ARM64 gate](dma-eligibility.md) must accept the loaded map before
any eligible PRE. Its specific coherent, direct, non-bounced backend uses
barriers without cache maintenance, bounce copying, or mutable sync state.
Only that proof permits overlapping bidirectional phases. This is not a
permission to overlap arbitrary bus_dma maps.

A persistent lease opens with PREREAD|PREWRITE before host backing exposure.
Each ATTACH or UNREF attempt separately performs PREREAD|PREWRITE and
POSTREAD|POSTWRITE. A short token POST leaves the lease open: retained host
writes remain possible, and neither CPU ownership nor query completion is
inferred. Fenced UNREF success closes the lease; fatal reset closes it only
after producer/dequeue joins and outstanding-cookie recovery.

`dma_required` records the proven policy before enqueue. `dma_eligible`
still publishes admission only after successful ATTACH. A failed ATTACH
can have an open lease while admission remains false. Retirement checks the
lease itself, so this error cannot lose its POST or unload its map early.
An ACKed CREATE retains its resource ID even if ATTACH was rejected locally.
The unused raw-ID UNREF and DETACH helpers were removed; BO-owned UNREF and
final object release remain the only retirement route.

## Ownership and ordering

The operation record is embedded in the existing command cookie; the BO
contains its lease and registry node. No separate operation allocation or
registry GEM reference exists. ATTACH allocates a one-object array and takes
an ordinary GEM reference before enqueue. Command, array, wait and fence
allocation and reservation locking precede PRE. UNREF starts at GEM count
zero and borrows the existing cookie release owner; it never resurrects GEM.

PRE runs inside the registered producer and submission barrier. ENOSPC
closes only the rejected attempt token outside the queue lock, then waits
without an active token and rearms the same record. The lease remains open
and the original total five-second pressure budget is preserved. Existing
nonblocking transport allocations remain a transport boundary.

Central finish performs the token POST before response callbacks, wait
completion, array release, cookie release and own-fence readiness. A host
error first completes synchronous transport reset. A local queue rejection
has never exposed that attempt. Final map unload, ID release and GEM free
require lease NONE/CLOSED, no members and no retirement pins.

The DMA lock owns claims and counters, not bus_dmamap_sync or freeing.
Reset claims one lease and a temporary raw retirement pin, performs POST
outside locks, then drops the pin. Concurrent zero-ref release only sets
release_pending while the pin survives. The last owner claims finalization
under the lock, removes the registry entry, and frees outside the lock.
A late zero-ref UNREF after the reset pass observes the closed lease and
can finalize exactly once. The registry does not create a reference cycle.

C2 has at most one operation member per BO: the ATTACH array keeps GEM alive
until its token has finished, and UNREF is emitted only at zero references.
Each operation POST uses one pin. Reset joins admitted producers, dequeue
workers and cookie cancellation before claiming a lease pin. Successful
UNREF closure runs inside those joined owners; rejected late callers never
close a lease. Thus operation POST cannot overlap reset lease POST, and the
pin count is at most one. A CLOSING entry at reset traversal is an invariant
violation, not a condition to spin on. Extending membership beyond these two
commands requires revisiting this bound.

Reset closes the remaining leases before terminal fence publication, then
keeps the existing console/config/object drains. Stopped kernel callers
remain covered by those drains and the no-live-detach premise; this is not
a new generic DRM lifetime guarantee.

## Reproduction and evidence boundary

```sh
sh ember/tools/virtgpu-backing-contract.sh
BACKING_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-backing-contract.sh
RESOURCE_SOURCE_ROOT=/absolute/baseline-export \
    sh ember/tools/virtgpu-backing-contract.sh
```

The fixture extracts actual lease helpers, admission registration, common
queue, dequeue/reclaim, finish/cancel, reset worker, ATTACH/UNREF, object release and backing
unload. It shares actual resource creation and GEM array helpers with the
resource fixture. Transport descriptors, fence emission/publication,
workqueue joins, MD eligibility, allocation and bus synchronization are
controlled seams. The separate completion contract checks the retained
fence algorithm; the MD contract checks the native eligibility predicate.

Twenty-four groups exercise early completion before enqueue returns,
metadata OOM, ENOSPC retry, malformed/error/lost/late replies, stop inside
PRE/POST, zero-ref destruction during reset POST, delayed release, duplicate
token POST, repeated reset and the legacy control. Phase, map, ID and
reference counters must conserve resources. Sync must run outside DMA,
queue and fence locks; callbacks precede wait completion only after POST.
Final release precedes successful UNREF fence readiness. Reset terminal
publication requires all remaining registered leases closed.

Host checks and ASan/UBSan pass. The pre-C2 baseline fails causal checks for
missing bidirectional phases and premature retirement; the new token-helper
case is explicitly skipped there. Native compilation/contract review of
this C2 change, matched kernel build, boot and live backing checks remain
separate gates. No installed kernel or system runtime changes follow from
this host contract.
