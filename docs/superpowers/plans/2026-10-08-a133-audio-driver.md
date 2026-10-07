# A133 Audio Driver Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Build the A133 audio(4) driver with silent default routes and stage its kernel without disturbing kernel#7.

**Architecture:** Separate portable hardware/FIFO operations from the NetBSD adapter. Keep existing non-A133 codecs unchanged. Explicit mixer routes prepare the clock/analog lease; attachment and read-only queries have no register effects.

**Tech Stack:** KNF C99, bus_space, audio_hw_if, FDT, native NetBSD11/aarch64 GCC12.5, shell.

**Spec:** ../specs/2026-10-08-a133-audio-driver-design.md

## Global Constraints

- No physical playback or microphone recording at night.
- No PMIC supply writes; preserve AVCC/CPVIN1.8V and calibration.
- Routes default disabled; invalid trigger has no register writes.
- FIFO service<=128samples; blocks>=1024bytes; allocated buffers<=1MiB.
- Retain kernel#7 until unattended UART recovery is verified.
- No existing codec driver, Wi-Fi or U-Boot replacement changes.

## Review Focus

- Partial initialization/PLL timeout must restore owned state, except that a
  released codec reset must remain released to preserve unowned calibration.
- Wrong/different FDT resources must not enable this board-specific path.
- Stereo FIFO budgets must not split a frame or overrun a ring/allocation.
- Disable/detach races must not call stale callbacks or free active memory.
- Boot/read-only queries/disabled triggers must leave hardware untouched.

### Task 1: Hardware lease and FIFO service

**Files:** Create `sys/arch/arm/sunxi/sun50i_a133_codec_io.{c,h}` and `ember/tools/a133-audio-io-test.{c,sh}`.

**Interfaces:**
- Consumes PCM core `a133_pcm_ring_chunk/advance`.
- Produces `struct a133_codec_io` (cookie, read/write/delay_us callbacks with space+offset), `struct a133_codec_lease` (saved registers and prepared flag); `int a133_codec_prepare(io,lease)`, `void a133_codec_restore(io,lease)`, `int a133_codec_route(io,lease,mode,bool)`, `int a133_codec_transfer(io,lease,mode,buffer,ring,budget,size_t*,bool*)`.
- Spaces CODEC, CCU, PIO; mode PLAY/RECORD from PCM core. The adapter serializes helpers and bounds allocation lifetime.

- [x] Write failing production-helper tests: no lease source, then inactive codec preparation, PLL timeout rollback, active codec rejection/no writes, non-codec PLL consumer rejection, preserved unrelated CCU/PIO/calibration, exact TX LE stereo/RX sign-extended sample fixtures, budget128 cap, full/empty FIFO and block wrap.
- [x] Run `sh ember/tools/a133-audio-io-test.sh`. Expected: FAIL missing production helper.
- [x] Implement the six-register clock lease, saved analog/FIFO state, route operations and bounded transfer helper against manual and inspected native probe values.
- [x] Run the helper contract and ASan/UBSan. Expected: hardware lease/FIFO tests pass.
- [x] Commit `feat(audio): add A133 codec lease and FIFO service`.

### Task 2: Verified FDT audio resource marker

**Files:** Modify `sys/arch/arm/sunxi/sun50i_a133_fdt.c`; create `ember/tools/a133-audio-fdt-test.{c,sh}`.

**Interfaces:**
- Produces `ember,ys-m33-audio` marker plus copied canonical interrupts on verified vendor codec; root remains A133.
- Consumes exact codec/machine/GPIO/interrupt resources in spec; no MMIO.

- [x] Add fixtures: matching resources attach marker/IRQ25, repeated fixup remains valid, disabled nodes/foreign phandle/wrong PA/PF6/IRQ/codec reg reject marker, no-space propagation, non-A133 tree unchanged.
- [x] Run the new contract. Expected: FAIL missing marker.
- [x] Implement validation before property mutation; copy source cells before libfdt relocations and clear stale marker before revalidation.
- [x] Run new and existing FDT contracts with matching libfdt. Expected: all pass.
- [x] Commit `feat(audio): mark verified YS-M33 codec resources`.

### Task 3: NetBSD adapter and native kernel

**Files:** Create `sys/arch/arm/sunxi/sun50i_a133_codec.c`, kernel config `sys/arch/evbarm/conf/EMBER64_A133_AUDIO`; modify `files.sunxi`; update board/root audio claims.

**Interfaces:**
- Consumes Task1 helper and Task2 marker, existing PCM core.
- Produces audio_hw_if query/set_format, allocation, trigger/halt, mixer route controls, properties, device/locks and detach; attaches audio(4) on a133codec.
- Default mixer `outputs.route=disabled`, `record.route=disabled`. Enable requires idle streams and prepared lease; mixer query/get never reads hardware.

- [x] Add a native compilation gate against the real audio API and a production-adapter guard/format contract. Initial native compilation failed on the mono constant and missing kpause declaration before source corrections.
- [x] Implement adapter with thread/interrupt locks, single-owner register mappings, staged prepare/IRQ unwind, allocation bounds, default denied streams, IRQ budgets, XRUN counters and child-first detach.
- [x] Build a clean pinned commit natively as `EMBER64_A133_AUDIO`; run the seven portable A133/Goodix contracts and adapter gate. Expected: kernel ELF/image and contracts succeed; runtime hardware not claimed.
- [x] Package kernel image in private Android boot container using the original DTB and empty ramdisk; inspect header, size and hash. Expected: bounded valid container, no eMMC writes.
- [x] Update docs with exact source/build level and pending physical acceptance; commit `feat(audio): integrate guarded A133 audio interface`.

## Finish

Independent read-only review; fix important issues with regressions and repeat affected gates. Keep the source branch/worktree and staged private artifact. Do not ask nighttime acoustic questions or deploy an unverified kernel with unavailable automatic recovery.
