# A133 guarded USB range transfer implementation plan

> For agentic workers: use superpowers:executing-plans for inline execution.

**Goal:** reusable guarded raw transfers with complete host readback hashes.
**Architecture:** bounded binary ADB channel, board/write policy, inspection-only CLI.
**Tech stack:** Ruby standard library, installed ADB, existing A133Backup/A133Env.
**Spec:** [transfer design](../specs/2026-10-08-a133-usb-transfer-design.md).

## Global constraints

- No device write CLI, root restart, unlock, reboot or normal-env release.
- Exact YS-M33 geometry/CID, USB/recovery/root/unlocked/orange and shell_v2.
- Only raw boot/resources/root; full prehash, transfer hash and readback hash.
- Protected env and saved recovery must match before/after writing.
- Backup receipt is a trusted caller contract, not authenticated provenance.
- Timeout 1–7200 seconds; bounded streams/diagnostics, installation_ready=false.
- Preserve current reference hardware/worktree; tests use files, no new Python.

## Review focus

- Concurrent stderr/stdout, failed remote sync and children holding pipes must fail boundedly.
- Wrong device, alternate partition mapping or mounted aliases must not receive bytes.
- CRC-valid GPT discrepancies must fail through the actual live GPT validator.
- Source mutation/short stream/readback corruption must never be called accepted.
- Stale/foreign backup or unprotected env must not silently enable a write.

### Task 1: bounded binary channel

**Files:** create `ember/tools/a133-usb-channel.rb`, `a133-usb-channel-test.rb`.
**Interface:** `A133Usb::Channel.new(adb, timeout).run(arguments, source: nil,
input_bytes: 0, limit: 16384) { |chunk| ... }` returns bounded captured bytes;
with a block streams output, with source sends exactly input_bytes and hashes
those bytes. Result includes input_sha256. Raises A133Usb::Invalid.

- [x] Write real child tests for binary preservation, simultaneous large output,
  nonzero/stderr failure, output bound, short input, timeout and orphan cleanup.
- [x] Run; expect missing channel. Implement channel with concurrent pipes/cleanup.
- [x] Run tests on Ruby 4.0.5 and system 2.6.10; commit.

### Task 2: guarded writer and read-only CLI

**Files:** create `ember/tools/a133-usb-transfer.rb`, `a133-usb-transfer-test.rb`,
`ember/boards/ys-m33-a133-usb-transfer.md`; update cable doc/public overview if affected.
**Consumes:** Channel.run and A133Backup.validate_gpt/A133Env.decode.
**Produces:** Client.inspect! and Client.write_verified(role:, path:, bytes:,
sha256:, backup:, protected_env:); CLI only --adb/--serial/--cid/--timeout inspection.

- [x] Write file-backed strict ADB fixture with sparse full-size GPT. Test actual
  raw writes/readback/preserved tail and zero-write rejection for policy failures.
- [x] Run; expect missing Client. Implement exact gates and full-range verification.
- [x] Add partial-write/readback corruption/mutation/symlink and inspection CLI cases.
- [x] Run new contracts plus existing backup/recovery/env/bundle/preflight/firstboot guards.
- [ ] Review independently; fix reproduced Important/Critical issues. Document
  file-backed versus physical limits, update draft PR/wiki and push; do not merge main.
