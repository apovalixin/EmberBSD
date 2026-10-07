<!-- Origin: EmberBSD; AI-assisted native VirtGPU resource ownership notes. -->
# Classic resource lifetime

This source work prepares classic VirGL resource ownership. VIRGL negotiation
remains disabled. It does not establish an accelerated graphics session.

DRM calls the GEM open/close callbacks for each handle. Repeated GEM_OPEN of
one flink name can create multiple handles in one file; PRIME's cache does
not eliminate this case. VirtGPU keeps one attachment entry per file and BO,
with a bounded handle count and one retained GEM reference.
[Owned backing eligibility](dma-eligibility.md) is checked before duplicate
counting or new context attachment. The first open
publishes that entry after fenced ATTACH; duplicate opens only increment its
count. Only the final close sends fenced DETACH.

A sleepable file mutex serializes these transitions, including pending
responses. A failed open returns its own errno and removes provisional state;
DRM does not call close after a failed open callback. Failed retirement joins
synchronous transport reset before dropping ownership. This includes close
allocation failures, since close cannot return an error. Request arrays keep
their own independent references through completion or cancellation.

Core releases its handle-table and object-name locks before callbacks.
PRIME import can hold the PRIME mutex while entering the attachment mutex;
close releases the attachment mutex before core subsequently takes PRIME's
mutex. Completion and reset workers never acquire the attachment mutex or
access attachment entries. Normal file GEM release empties the list before
context postclose. Defensive leftover cleanup follows CTX_DESTROY/reset.

Both CREATE_2D and CREATE_3D return direct status and publish `created` after
a matching GPU-fence response. Their common object-creation producer now
retains a registry guard before possible CREATE exposure. The
[controlled console/2D contract](controlled-console.md) retires that guard only
after acknowledged UNREF or joined reset, including ATTACH/cleanup metadata OOM. Merely observing
`vqs_ready=false` is insufficient. No backing submission or successful create
ioctl output follows a failed CREATE. Resource IDs use defined unsigned
addition, including an allocator handle of INT_MAX.

Backing setup retains its pin, SG table, virtual mapping and native DMA map.
Eligibility, wire checks and entry allocation precede PRE and publication;
early failures release local backing without POST. The created host ID still
retires through acknowledged RESOURCE_UNREF or completed reset. After PRE,
backing itself remains retained through that retirement. Entry-count checks
bound command data size and avoid subtracting headers from an undersized
request capacity. Resource shape/format validation and live mapping qualification remain
separate gates. The linked controlled console stage owns finite 2D phases.

## Checks and limits

Run from the source root:

```sh
sh ember/tools/virtgpu-resource-contract.sh
RESOURCE_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-resource-contract.sh
```

The eight host groups compile actual DRM handle callbacks/unwinds, GEM_OPEN,
PRIME import unwind, VirtGPU attachment/creation/teardown, synchronous waits,
response validation and native reset. Boundary doubles supply allocation,
IDR/VMA, reservation, DMA and transport APIs. They verify duplicate handles,
concurrent transitions, allocation/queue/response errors, retained references,
timeout/late cookies, reset overlap and arithmetic limits. Host checks pass,
including ASan/UBSan. Existing context, capset and VirtGPU contracts also pass.

On 2026-10-07, NetBSD 11/AArch64 checks of source `737c8429f087` passed
all eight resource, six context, five capset and nine existing VirtGPU
groups with GCC 16.2. Fresh GEM, KMS, object, queue and ioctl objects also
compiled with base GCC 12.5 and the kernel's `-Werror` flags. A fixture
warning found by native GCC was repaired without weakening these flags.
No kernel link, installation or boot was performed for this change.

Structure changes require a later complete matched kernel
rebuild; do not link a partially updated object directory. The generic DRM
core inserts its internal IDR entry before its open callback; this driver
change does not repair or prove safe arbitrary guessed-handle races against
that internal publication. Tests model hardware execution and DMA ownership;
they do not establish actual GPU execution, interrupt/SMP behavior, DMA
coherency, full kernel boot or physical hardware support. EXEC, transfers,
modern protocol fields and feature negotiation remain separate work.
