<!-- Origin: EmberBSD; AI-assisted classic wire-fence exhaustion contract. -->
# Classic fence range and retirement

The global classic timeline issues IDs from 1 through `INT32_MAX`. The next
allocation returns EOVERFLOW and permanently stops that device instance through
normal reset cleanup. IDs never wrap, restart or reserve a cleanup range.
The wire field remains u64 and the generic DRM/UAPI contracts are unchanged.
VIRGL remains disabled; this is a source and software-contract gate.

## Why this bound

The native `linux/linux_dma_fence.c` comparator uses unsigned 32-bit differences
when `use_64bit_seqno` is false, as in this driver's fence operations. A retained
fence with sequence 1 compares correctly with `INT32_MAX`, but not with
`0x80000000`: that difference equals `INT_MAX`. Pending-list capacity cannot
bound how long users retain old fences. Restricting issued IDs keeps every
forward difference below `INT_MAX` without changing the generic comparator.

Saved classic host sources provide a separate bound. QEMU's original u64
command ID is compared with a u32 renderer callback. The comparison is unsigned;
`0x80000000` is not evidence of a host comparator failure. Truncation at
`0x100000000` prevents retirement of the original u64 command. The renderer's
signed-int entry converts to u32, so our smaller range also fits both types.

The inspected QEMU revisions are
[`37ba092d59aff24900dfd0d5e01d4ed68441ba07`](https://github.com/utmapp/qemu/blob/37ba092d59aff24900dfd0d5e01d4ed68441ba07/hw/display/virtio-gpu-virgl.c)
and [`6601422e1fff2da1376faafb1e4c2c5cdb2d8003`](https://github.com/utmapp/qemu/blob/6601422e1fff2da1376faafb1e4c2c5cdb2d8003/hw/display/virtio-gpu-virgl.c).
The saved files have SHA256
`40f86e97e6cc26cc0a19c946a988d7553966f22ab69795cf3c4d47c8e2fc4088` and
`1e965a052c77eb663a6e90e4c815e8a2d38f186f3e130cb1c5acbac04c70dc03`.
The corresponding renderer source revisions are
`dc039d9ecd74fc671a85bfbe7c4e4bc552b7b855` and
`5d26f605f50f8e22002ec6db5fb775e1992d4e96`, in `src/virglrenderer.c`.
Their classic create-fence entry and ctx0 callback retain only 32 bits;
new context/ring fences are a separate, unnegotiated path. These source audits
do not establish live behavior near the boundary on an installed host.

## Stop and ownership

Under the fence lock, exhaustion takes precedence over capacity and increment.
It latches the first EOVERFLOW stop cause without modifying the rejected wire
header, private sequence-zero fence, pending list, count or references. The
space predicate permits immediate rejection even behind a full retained list.
The last valid allocation may still retry descriptor pressure with the same ID.

The common queue seals DMA and clears readiness under the submit mutex before
unlocking. Already admitted fenced, unfenced and cursor producers then reject
at their existing readiness check. Normal reset follows only after submit and
reservation locks are released, before canceling the rejected cookie. The
producer stays registered through cancellation and its final shared access.
Only emitter exhaustion takes this path; unrelated DMA preparation errors do
not become fence exhaustion.

The [EXEC ledger](exec-ownership.md) and [backing lease](backing-lifetime.md)
keep their independent lifetimes. Rejection before emission creates no new
PRE or ledger membership. Existing operations retire through the normal joined
cookie drain, lease POST and terminal publication order. Zero-reference UNREF
uses its existing raw retirement pin, never a resurrected GEM reference or a
new cleanup ID. Open handles retain their objects until ordinary final release.

The rejecting allocation reports EOVERFLOW; later stopped submissions report
ENODEV. Known cookie results remain exact. Unknown reset-canceled cookies retain
the existing ENODEV result; the driver's first stop cause remains EOVERFLOW.
Private output descriptors, including fd 0, close and abort without publication.

## Reproduction and limits

```sh
sh ember/tools/virtgpu-fence-exhaustion-contract.sh
sh ember/tools/virtgpu-fence-backing-contract.sh
FENCE_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-fence-exhaustion-contract.sh
FENCE_TEST_CFLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
    sh ember/tools/virtgpu-fence-backing-contract.sh
```

The first harness has 18 groups and extracts the production native comparator,
emitter, common queue, EXEC, completion and reset paths. The second has seven
groups and combines the actual emitter with production backing retirement.
Allocators, low-level locks, scheduler interleavings, hardware reset and transport
are controlled seams. The backing harness models terminal fence publication;
the completion harness tests the actual publication algorithm. Neither fixture
claims native interrupt concurrency or live DMA behavior.

Both pass host checks with ASan/UBSan. Against unchanged source revision
`725ba7dcca1d83da0b25f072a185115e3610ed45`, the same harnesses compile and fail
14 of 18 and six of seven groups respectively; the other cases are controls.
All 179 affected existing contract groups also pass on the host. Native object
compilation and contract execution for this change remain pending, as do a
matched kernel, live exhaustion/reset and graphics qualification. Transfers,
WAIT status and CPU-copy ownership remain separate work.
