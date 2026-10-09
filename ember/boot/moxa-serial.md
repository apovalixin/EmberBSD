<!-- Origin: EmberBSD MOXA serial-core development guide, 2026-10-09. -->
# MOXA G2 framing and transport-state helpers

EmberBSD provides preparatory C helpers for a shared four-port MOXA G2 USB
transport. They let driver developers exercise framing and completion
handling before attaching a device. The helpers are not yet connected to
`ucom` or a USB parent; device attachment, firmware, termios, serial control
and physical RS-485 validation remain pending. UPort 1150 and Ethernet
NPort devices require separate implementations.

## Source and validation

- `sys/dev/usb/umoxa_frame.{c,h}` validates G2 data/event transfers and
  builds TX headers. Its host contract has eight named groups.
- `sys/dev/usb/ucom_transport_core.{c,h}` models one port's open, close,
  detach, fault and TX completion state. Its contract has eight named groups.
- Host C99 builds use `-Wall -Wextra -Werror`; available ASan/UBSan builds
  run the same contracts. Host execution validates a sequential model.
  Kernel locking, asynchronous callbacks, TTY queues and USB behaviour need
  integration checks before a driver support claim.

Run from a clean source checkout, with an absolute, unused output directory
outside the source tree for each command:

```sh
sh ember/tools/umoxa-frame-test.sh /absolute/new-frame-output
sh ember/tools/ucom-transport-core-test.sh /absolute/new-state-output
```

Each runner saves its compiler version, binaries, build/run logs and source
SHA256 hashes. It refuses to overwrite an existing output. `CC` selects a
single host compiler executable path; it is not a string of compiler flags.
Sanitizer compiler/linker unavailability is recorded as a skip; sanitizer
runtime/test failure fails the command. These runners execute host binaries,
so an AArch64 cross compiler is not a replacement for their host `CC`.

After preparing an EMBER64 cross build with the fork's
[wrapper](cross-build.md), compile both conditional kernel variants:

```sh
sh ember/tools/moxa-core-cross-check.sh /absolute/clean-source \
    /absolute/kernel-build /absolute/new-kernel-object-output
```

The output's parent must already exist. This checks two AArch64 kernel
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
