# A133 audio driver: source and native build validation

Validation date: 2026-10-08. This receipt covers the experimental YS-M33
audio(4) driver, not physical playback/capture acceptance.

## Implemented boundary

`EMBER64_A133_AUDIO` includes the preceding eMMC configuration and adds the
guarded A133 codec adapter, its register/FIFO helper and PCM core. The FDT
marker requires the inspected vendor codec/machine/GPIO/interrupt resources.
Existing non-A133 codec drivers remain unchanged.

Attachment and read-only queries do not prepare hardware. Speaker and MIC1
routes default to disabled. Explicit enable prepares clocks and analog paths
under the thread lock. Streams validate formats and allocation bounds, use
bounded interrupt service, and clear callbacks under the interrupt lock on
halt. No sound or microphone recording runs automatically.

The helper never asserts codec bus reset. After releasing an initially held
reset, it deliberately leaves reset released when restoring original gates;
otherwise unowned BIAS, DAP and calibration state could be erased. A timeout
before release preserves the original reset state. This exception is part
of the resource contract, not a claim of complete CCU-state restoration.

## Checks actually run

- Eight production-source contracts passed on macOS and native NetBSD:
  `a133-fdt-test`, `a133-pin-test`, `gt9xx-frame-test`, `gt9xx-reset-test`,
  `a133-pcm-test`, `a133-audio-io-test`, `a133-audio-fdt-test` and
  `a133-audio-interface-test` in [ember/tools](../../tools).
- The I/O and adapter contracts also passed AddressSanitizer and
  UndefinedBehaviorSanitizer on macOS.
- Native NetBSD 11/aarch64 GCC 12.5 built the full `EMBER64_A133_AUDIO` kernel
  ELF and ARM64 Image from a clean source pin. Initial compilation caught
  an incorrect mono format constant and missing `kpause` declaration; both
  were corrected in the source before exporting another clean pin.
- The native PCM kernel-context gate passed with the actual kernel flags.
- A reset-aware regression reproduced the destructive-reset defect before
  its fix. Cold/warm leases now preserve seeded BIAS, DAP and calibration.
- The adapter fixture executes real trigger, halt, service and ISR bodies:
  finite blocks and wrap, signed capture words, flush timeout, impossible
  FIFO count, and no I/O/callback after halt. It is a software model.
- Independent read-only review found no remaining critical/important issues.
  `git diff --check` passed.

## Staged artifact

The ARM64 Image is 16,989,540 bytes. The private Android v2 boot container
is 17,002,496 bytes, with an empty ramdisk and the original 8,351-byte vendor
DTB extracted from the preceding working container. Header/version, load
addresses, page alignment, exact kernel/DTB payloads and the 32 MiB partition
size limit were checked. The image was not written to the tablet.

The physical sample retains its preceding kernel. No physical audio test,
recording or reboot was performed during this milestone. UART recovery
interception was unavailable, so replacing that working kernel would require
operator assistance after a failed boot.

## Remaining acceptance

Physical driver attachment, IRQ latency/CPU use, underrun/overrun behavior,
amplifier polarity, acoustic level, microphone speech quality, duplex and
the voice application remain unverified. Suspend/resume and DMA are outside
this implementation. The portable fixture does not execute the complete
allocation/free or mixer/IRQ establishment lifecycle; those paths were
compiled and reviewed. Longer uptime is not sustained stability acceptance.
