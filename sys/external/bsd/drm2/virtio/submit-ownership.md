<!-- Origin: EmberBSD; AI-assisted native EXECBUFFER ownership contract. -->
# Asynchronous EXECBUFFER acceptance

VIRGL remains disabled. This driver-source contract makes immediate
EXECBUFFER errors observable and prevents publishing an unsubmitted fence.
It is not runtime GPU or acceleration acceptance.

`virtio_gpu_cmd_submit` consumes command storage and its locked BO array
on every call. It returns the exact allocation or queue error. A zero
return means transport acceptance, without waiting for GPU completion.
Accepted cookies own their storage; queue cancellation owns rejected cookies.
The caller retains its own fence reference throughout the call.
The subsequent [completion foundation](completion-lifetime.md) bounds retained
fences and delays terminal publication until their exact cookies retire.
It preserves this asynchronous acceptance contract.

EXECBUFFER finishes copies, handle lookup, reservations and private
sync-file preparation before sending. After transport acceptance it sets
CLOEXEC, installs the descriptor, then publishes the output field. Descriptor
zero is valid. Completion or reset may happen before descriptor publication;
the retained fence references keep that sequence safe.

Before affix, native `fd_allocfile` leaves `f_count=0`. Native `fd_abort`
frees that file and descriptor reservation but does not call `fo_close`.
A rejected submission therefore closes its private sync_file through its
file operation before aborting the descriptor. It never temporarily installs
a failed descriptor, uses `closef` on an unreferenced file, or changes generic
file/sync APIs. The private file cannot yet have poll callbacks.

Input dependencies use an interruptible driver-local `15 * HZ` timeout,
matching the existing WAIT timeout policy. A completed wait must also have
a successful terminal fence status, including fence arrays. A negative
terminal status propagates unchanged; an unexpectedly pending status fails
with EIO. Timeout returns ETIMEDOUT and interruption retains its errno.
No new command has been sent at this point, so a foreign dependency timeout
does not reset the GPU. Callers can explicitly retry. Same-context fences
are not exempted from dependency checking.

## Production regression

From the repository root:

```sh
sh ember/tools/virtgpu-submit-contract.sh
SUBMIT_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer -g' \
    sh ember/tools/virtgpu-submit-contract.sh
```

`SUBMIT_SOURCE_ROOT=/absolute/baseline-export` runs the same fixture against
an immutable older source tree. Required files are enumerated directly in
the extraction script; retain the kernel `sys/kern/kern_descrip.c` input.

The fixture extracts the real ioctl/submit, vbuf allocation/free/cancel,
queue-pressure handling, fence allocation/emission/failure/completion,
BO-array lookup/reservation/reference paths, native sync-file create/close,
fd_abort, terminal status and fence-array error callbacks. It retains
production wire/UAPI, array, fence, private-file and vbuffer structures.
The generic dma_fence, device and kernel file structures have only their
used fields modeled; they are not ABI layout tests.

Doubles cover allocators, usercopy, GEM/reservation primitives, generic fence
refcount/signaling, scheduling, descriptor allocation/publication and transport.
Native KM_SLEEP sync-file creation is no-fail; a separate wrapper exercises
a hypothetical fallible-create unwind without modifying that production API.
The array's real error callbacks run against controlled scheduling. Poll,
kqueue, IDR internals, true transport DMA and generic fence wait internals
are not extracted. A narrow diagnostic pragma preserves an existing signed
loop-index warning in the unchanged array-add-fence function; all other code
retains `-Wall -Wextra -Werror` (unused seam parameters are excluded).

Host tests cover validation, fd zero, input statuses, failed arrays, every
allocation/copy/lookup/reservation boundary, private close-before-abort,
queue error/pressure/retry and completion/reset before publication. Conservation
checks include allocations, BO/fence references and lists, reservation locks,
file reservations, close/abort/install counts and nonzero emitted fence IDs.
Late-error cases combine the actual response validator and fence failure
paths with controlled cookie retirement; they do not exercise the complete
IRQ dequeue/reset path.

On 2026-10-07, source `caaec17158cb0eb342f0c0b8e7bb3ac3f714d577`
passed all 53 groups on the NetBSD 11/AArch64 UTM guest with GCC 16.2.
The unchanged baseline failed 34 groups on the host; repaired host tests
also passed with ASan/UBSan. Native resource, context, capset and earlier
VirtGPU contracts passed another 28 groups. Fresh `virtgpu_vq.o` and
`virtgpu_ioctl.o` compiled with the base GCC 12.5 and `-Werror` in 0.93 seconds,
with 66,148 KiB peak RSS and no swaps. Independent review approved the
change and retained evidence. No complete kernel was linked, installed or
booted for this check; the partial object directory is not a kernel artifact.

## Remaining boundaries

This task does not make every later GPU failure a userspace ioctl errno.
The native sync_file lacks Linux SYNC_FILE_INFO, and existing WAIT error
reporting is unchanged. Terminal driver fence errors remain retained.
Transfer/backing DMA ownership, early IDR membership races, full memory
ordering, modern rings/blob/context-init and full kernel/live validation
remain pre-enable work. No generic DRM, dma_fence, sync_file or VirtIO
transport implementation changes are part of this contract.
