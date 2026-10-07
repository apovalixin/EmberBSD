<!-- Origin: EmberBSD; AI-assisted bounded classic EXEC framing contract. -->
# Classic EXEC framing

VIRGL remains disabled. EXEC rejects malformed packet boundaries before
operation PRE, queue submission or output-fd publication. This is a source
contract for classic command framing. It does not establish complete 3D
request validation, native runtime acceptance or hardware support.

## Copied command boundary

Scalar admission keeps zero bytes forbidden, caps size against the existing
transport request limit and now requires a multiple of four bytes. Alignment
rejection precedes the explicit input-fence wait. Aligned calls preserve that
wait's status and shared deadline before copying commands or checking framing.

After the single command copy succeeds, an allocation-aligned `uint32_t`
walker examines native unsigned headers. Bits 0..7 are the command, bits 8..15
the object and bits 16..31 the payload-dword count. Each iteration consumes
one header, checks the payload against remaining words, and skips that payload.
It ends exactly at the supplied boundary. A zero-payload packet advances by
one word; absent or overlong payloads return EINVAL.

`END_TRANSFERS` padding is opaque packet payload. It can contain nonzero words
that look like impossible nested headers. The walker skips it and validates
any real packets following it. There is no opcode/object allowlist, packet
alignment rule or Mesa transfer-chunk size policy. Payload bytes are unchanged.
The layout matches Mesa's [classic protocol header](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-26.2.4/src/virtio/virtio-gpu/virgl_protocol.h)
and [END_TRANSFERS encoder](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-26.2.4/src/gallium/drivers/virgl/virgl_encode.c).

Classic command words are copied unchanged through Mesa, QEMU and the renderer;
the walker applies no little-endian conversion. Outer VirtIO fields retain
their separate little-endian representation. Acceptance targets the selected
AArch64 little-endian guest/host; mixed-endian compatibility is unqualified.

Hints may already hold references and budget, and an output descriptor may
already be reserved. Framing runs before attachment/snapshot allocation,
reservation locks or fence construction. The existing error unwind frees the
command copy, releases hints and their charge, and aborts a reserved descriptor
including fd zero. It never installs the descriptor or calls `fo_close` on an
uninitialized private file. A command copy fault still returns EFAULT.
The [whole-context ownership](exec-ownership.md), [loaded-map gate](dma-eligibility.md),
[backing lease](backing-lifetime.md) and [completion rules](completion-lifetime.md)
remain the authorities for handles, context membership, backing and lifetime.

## Host obligations before activation

The selected renderer/QEMU must validate command semantics, context wire-ID
authority, formats, targets, boxes, strides, inline payloads and every actual
IOV/host allocation access with checked wide arithmetic. These checks include
retained or delayed query/output paths and all downstream narrow copy helpers.
Framing supplies none of this semantic proof; a second guest format table or
partial command decoder is not part of this change.

Host qualification must also repair truthful error delivery: QEMU's ignored
CREATE/transfer/EXEC renderer results and the renderer's overlong-packet
break-then-success path. Partial command execution is not atomic rollback.
Terminal errors must reach the guest fence/reset contract. Delayed query writes
need bounds and lifetime checks at the eventual write; UNREF/reset must quiesce
guest IOV access before backing reuse. The known swallowed-error and narrowing
defects still block VIRGL. Legitimate Mesa traffic and actual runtime acceptance
must pass against the selected patched host, with its feature limits recorded.

## Reproduction and evidence

Run `sh ember/tools/virtgpu-exec-framing-contract.sh` from the source root.
Set `FRAMING_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`
for focused host sanitizers. `SUBMIT_SOURCE_ROOT` selects an immutable source
export. The fixture extracts the actual helper when present and the actual ioctl,
queue, budget, descriptor-abort and existing ownership/lifetime functions.
It contains no copied framing implementation. Older sources compile through
the same ioctl cases without requiring a missing helper.

The 28 groups cover sizes 1/2/3 and aligned-plus-1/2/3, absent/truncated payloads,
remaining-boundary and one-word-over packets, the unsigned maximum 16-bit payload,
zero-payload sequences, opaque END_TRANSFERS padding followed by valid/truncated
packets, multiple packets and byte-preserving submission. Rejections prove no
PRE/queue/fd install, and conserve references, budget, reservations and lifetime.
Valid hints with malformed commands or copy faults abort fd zero exactly once
without private-file close. Valid fd, queue-error and completion-error cases
retain the existing error/lifetime behavior. Usercopy is performed once per
command buffer, including an unaligned user address.

On 2026-10-07, all 28 host groups and focused ASan/UBSan passed. The compiled
`89a17172d7f4bebe0232a0abba15c69042092bdd` baseline failed 13 causal groups:
malformed calls reached PRE/queue, and the fd-zero case installed its output.
Shared submit fixtures use real zero-payload NOP words. The old minimum request
boundary case now uses four bytes plus the existing 256-byte overhead, retaining
its exact transport-boundary target. All affected old contract groups pass with
normal warnings. Native object/contract checks and VM execution are separate;
this host source gate does not claim either.
