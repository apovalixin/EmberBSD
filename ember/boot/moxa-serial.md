<!-- Origin: EmberBSD MOXA serial-core development guide, 2026-10-09. -->
# MOXA G2 preparation and external ucom transports

EmberBSD provides preparatory C helpers for a shared four-port MOXA G2 USB
transport, plus an opt-in `ucom` backend that connects external transports to
the existing TTY lifecycle. They let developers exercise framing, completion,
queueing and TTY faults before attaching a device. A real MOXA USB parent,
firmware, hardware termios/control and physical RS-485 validation remain
pending. UPort 1150 and Ethernet NPort require separate implementations.

## Source and validation

- `sys/dev/usb/umoxa_frame.{c,h}` validates G2 data/event transfers and
  builds TX headers. Its host contract has eight named groups.
- `sys/dev/usb/ucom_transport_core.{c,h}` models one port's open, close,
  detach, fault and TX completion state. Its contract has eight named groups.
- `sys/dev/usb/ucom_transport_rx.{c,h}` provides a bounded per-port RX ring;
  six groups pin FIFO/wrap, capacity, atomic overflow, hysteresis and isolation.
- Host C99 builds use `-Wall -Wextra -Werror`; available ASan/UBSan builds
  run the same contracts. Host execution validates a sequential model.
  The real `ucom`/TTY path has a separate ten-group native rump fixture.
  Its USB stubs exercise no hardware. Physical USB behaviour and sustained
  operation still need validation before a device support claim.

Run from a clean source checkout, with an absolute, unused output directory
outside the source tree for each command:

```sh
sh ember/tools/umoxa-frame-test.sh /absolute/new-frame-output
sh ember/tools/ucom-transport-core-test.sh /absolute/new-state-output
sh ember/tools/ucom-transport-rx-test.sh /absolute/new-rx-output
```

Each runner saves its compiler version, binaries, build/run logs and source
SHA256 hashes. It refuses to overwrite an existing output. `CC` selects a
single host compiler executable path; it is not a string of compiler flags.
Sanitizer compiler/linker unavailability is recorded as a skip; sanitizer
runtime/test failure fails the command. These runners execute host binaries,
so an AArch64 cross compiler is not a replacement for their host `CC`.

After preparing an EMBER64 cross build with the fork's
[wrapper](cross-build.md), compile the three conditional kernel helpers:

```sh
sh ember/tools/moxa-core-cross-check.sh /absolute/clean-source \
    /absolute/kernel-build /absolute/new-kernel-object-output
```

The output's parent must already exist. This checks three AArch64 kernel
objects using the generated `nbmake-evbarm` compiler and kernel headers;
it does not link the helpers into a kernel or execute target code. Keep
source revisions and header/toolchain provenance with the resulting receipt.

## Framing contract

RX data records use a four-byte header: big-endian 16-bit port followed by
big-endian 16-bit payload length. Ports are 0 through 3. Zero-length records
are consumed without callback delivery. Events are eight bytes: port, event
code and four opaque detail bytes. Unknown event codes remain opaque.
The decoder validates the complete transfer before invoking any callback;
a malformed suffix rejects a valid prefix too. Input must remain immutable
through callbacks, whose payload/detail pointers are borrowed for that call.

`umoxa_g2_rx_data` takes a payload limit from 1 through 65535. Empty input
may be NULL, but a callback is always required. Bad arguments return EINVAL;
invalid ports, truncated records, event tails and excessive record lengths
return EPROTO. Callers must provide spans covering their declared lengths.

`umoxa_g2_tx` accepts payload lengths 1 through 65535 and a SEND_NEXT value
of 0 or 1. SEND_NEXT sets bit 0x8000 in the TX port word; this bit is invalid
as an RX port. Insufficient capacity returns EMSGSIZE without changing the
output buffer. Payload and output may overlap; the written counter must not
overlap either. With a valid counter pointer, errors set it to zero.

Wire facts were inspected in MOXA's official
[UPort family Linux driver v6.2 archive](https://cdn-cms-frontdoor-dfc8ebanh6bkb3hs.a02.azurefd.net/getmedia/334908bb-fa9d-4fd8-8e18-db2512348451/moxa-uport-1200-1400-1600-1200-g2-1400-g2-1600-8-g2-series-linux-kernel-6.x-driver-v6.2.tar),
`mxuport/driver/mxuport/mx-uport.c` and `mx-uport.h`.
The archive's SHA512 matched the
[official catalog](https://www.moxa.com/en/support/product-support/software-and-documentation?psid=131128):

```text
B7A1FD7C2E1A23259E5696611FBE0CDEF50912E85200C70EBA6CF80DF86B90985ED774E69B8E7A24C1654985143F1C0F366C54CED164725374A1D0AD33F5A1BE
```

These are independent BSD-2-Clause helpers based on wire facts. Vendor GPL
code and firmware are not included. Device revision and actual descriptors
still need validation. SEND_NEXT event processing, its timeout/scheduling
and UART drain semantics belong to the future USB parent.

## State contract

One initialized `ucom_transport_core` belongs to one port; the caller supplies
locking. No allocation, I/O or callbacks occur inside the state core. Output
counters must not alias the state; void functions require non-NULL state.
Other NULL arguments produce EINVAL or a rejected boolean query/completion.

- Open increments a lifetime epoch and resets its TX cookie. A pending TX
  gets a unique cookie. Open/begin return zero or a positive errno.
- Accept and done return 0 or 1. A stale or duplicate done returns 0 without
  changing state. A matching failed done returns 1, consumes zero bytes and
  faults the port. Full success consumes exactly the pending length once.
- Short, overlong, failed or negative-error completion faults without replay.
  A supplied positive errno is retained; otherwise the fault is EIO.
- Close permits a new lifetime after a fault. Detach is final. Epoch overflow
  leaves the port detached; cookie overflow faults it. Neither counter wraps.
- Each instance is independent. This does not prove isolation in a shared USB
  scheduler; that scheduler and quiescent stop/detach are still to be built.

An epoch identifies host callbacks, not bytes still in a UART or device queue.
The parent must establish purge/enable and USB completion barriers on reopen.
The core leaves TTY output accounting to the bridge; it cannot infer whether
an errored USB transfer already reached the physical line.

## External ucom backend

`ucom_methods.ucom_transport` is a trailing nullable pointer. NULL preserves
the existing per-child USB pipe path. Stack `ucom_attach_args` is unchanged.
Rebuild the kernel, all affected parent drivers and rump components from one
pinned source tree; old module binaries are not compatible by assumption.
No real MOXA parent or VID/PID binding is added by this backend.

An external parent supplies `ucom_param` and all `uct_*` methods in
`struct ucom_transport_methods` (`sys/dev/usb/ucomvar.h`). Legacy
open/close/read/write/set methods must be NULL. get_status/ioctl remain
available. Attach rejects an incomplete table, a payload buffer outside
1..65535 or a nonzero packet header length. Framing belongs to the parent.

| Parent method | Contract |
| --- | --- |
| uct_attach(arg, port, child) | Process context; publish child only on success; clean resources on error |
| uct_start(arg, port, epoch) | Process context; stop will run even if start fails partway |
| uct_stop(arg, port, epoch) | Quiescence barrier; no dispatch/callback/buffer use remains on return |
| uct_submit(arg, port, epoch, cookie, bytes, length) | Nonblocking softint; borrow payload until done/stop; error accepts nothing |
| uct_rx_flow(arg, port, epoch, paused) | Nonblocking softint; no synchronous child callback |
| uct_set(arg, port, which, on) | Process context; return errno for DTR/RTS/break rejection |
| uct_detach(arg, port) | Process context; release the child binding permanently |

No `sc_lock` or TTY lock is held across parent methods. A process mutex
serializes termios/modem/break setting ioctls and param/set, covering the
TTY commit after parent acceptance. Drain may wait while holding this mutex,
but progress commands (start/stop/flush) and queries remain available. Never
reenter these setting controls on the same port. Lock order is process
control, `sc_lock`, then TTY. t_oproc and
hwiflow only schedule work while holding the TTY lock. Submit, flow and fault
notification carry references that close waits for before calling stop.
Parents release their lock before `ucom_transport_input/done/fault`.

Input copies the entire supplied block or returns errno. Capacity is 8192
bytes per port, with pause/resume watermarks 6144/2048. These are software
limits, not measured throughput. Overflow or failed TTY admission faults
that port instead of silently dropping the remainder. A fault wakes blocked
TTY I/O, subsequent operations return its errno, and poll reports error/hangup.
Selector registration and fault admission share `sc_lock`; fault notification
wakes both read and write selectors. External read/write kqueue filters retain
native TTY readiness rules while healthy and report `EV_EOF` with the transport
errno in `fflags` on fault. They use an atomic error snapshot under the TTY
lock, avoiding the reverse TTY-to-`sc_lock` order. Closing invalidates the
epoch and waits for notification dispatch before clearing this snapshot.
On a revoked TTY vnode, read may report EOF; consumers must treat EOF as loss.

Done uses payload bytes, excluding the parent's framing. Full success removes
the pending payload exactly once; stale/duplicate done changes nothing.
Partial/errored TX faults without replay. A TTY flush during pending TX keeps
newly queued bytes intact when the old completion arrives. The parent
relinquishes the borrowed buffer before invoking done. USB acceptance does
not prove UART drain; real parent scheduling/drain semantics remain pending.

Initial termios/set/start failure aborts open. Rejected parameters leave the
TTY settings unchanged, and failed initial open restores its saved settings.
Errors from two-part modem control are returned; successfully changed bits
remain reflected in cached state. Hardware partial-control rollback belongs
to the parent, which must expose uncertainty/fault instead of claiming success.

## Cross build and native execution

Prefer building the fixture on the development host with the fork's prepared
AArch64 cross tools. Supply a separate, pinned target-runtime sysroot containing
`usr/include`, `usr/lib` and `lib` from the matching NetBSD 11 software lab.
It supplies TTY/VFS/rump and C runtime dependencies, not a claim of complete
EmberBSD userland coherence. The private component compiles kernel sources from
the fork; the runner records source/header/link-input and output hashes.
No target binary is executed on the development host.

```sh
sh ember/tools/ucom-transport-rump-cross-build.sh /absolute/clean-source \
    /absolute/prepared-kernel-build /absolute/pinned-target-runtime \
    /absolute/new-cross-output
```

Copy the resulting `test` to the matching disposable VM and run
`RUMP_NCPU=4 ./test`. Record the target runtime/library hashes and output.
The wrapper passes `MAKEOBJDIR` on the make command line and checks `.OBJDIR`
before compilation: generated files remain in the new private output.
The source/build/sysroot paths must have no whitespace; host `shasum` is
required. No libraries or kernel are installed by this check.

### Explicit native build fallback

On a matching NetBSD 11 development runtime with compiler, rump libraries and
the fork's clean source export (native make paths must have no whitespace):

```sh
sh ember/tools/ucom-transport-rump-test.sh /absolute/clean-source \
    /absolute/new-native-output
```

The runner builds a private component containing real ucom/core/RX and a
test-only parent, links to the runtime's TTY/VFS/rump libraries, and records
those library hashes. Thirteen groups exercise actual device nodes, data exchange,
stale completion/reopen, partial TX fault, RX pressure/overflow, flush,
rejected/serialized controls, drain/resume/flush progress, dispatch-close
ordering, blocked read/poll, poll-registration races, healthy/fault kqueue
faults and detach/failed attach. Three attach warnings are intentional
invalid children. Raw rump syscall clients retry RUMP_ERESTART where ordinary
kernel/libc syscall handling would restart. This is bounded native software
acceptance, not a complete rebuilt OS, USB stack or physical-device test.

The focused compatibility matrix uses the prepared kernel's actual compiler
flags and generated headers. It cross-compiles ucom/core/RX plus all 17 legacy
method-table owners and retains dependency/object hashes:

```sh
sh ember/tools/ucom-transport-cross-check.sh /absolute/clean-source \
    /absolute/kernel-build /absolute/new-matrix-output
```

This matrix does not link a kernel; run the full kernel wrapper separately
from that same pinned source for the configured link and matched modules.
GitHub CI runs the 22 host groups on Linux/macOS; it does not run the native
fixture or kernel build. Keep those separate receipts with a published change.
