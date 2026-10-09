# A133 Journaled Unlock Stage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans inline. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Guard factory unlock, persist possible effects and verify unlocked USB recovery before returning an env-original receipt.
**Architecture:** Independent private journal plus fixed-profile transport composed by one resumable stage; existing factory APIs stay unchanged.
**Tech Stack:** Ruby, bounded ADB, actual-dd sparse fixtures.
**Spec:** ../specs/2026-10-09-a133-unlock-stage-design.md

## Global Constraints

- Fixed31037849600-byte17-partition profile, env16777216/prefix131072; fresh caller-trusted retained Capture/Mutable and explicit physical round-trip gate.
- Immutable policy; full critical/env/GPT checks, same serial/CID and hardware sizes; exact original/armed/known consumed only.
- Private0700 directory/0600 single-link files, lock held through I/O;65536-byte checksummed exact schema and monotonic flags.
- Marker before every possible write/reboot; never replay a recorded reboot; no rollback/relock/image write/root restart/trial acceptance.
- Native subprocess tests on Ruby4.0.5/system2.6.10; no new Python/secrets/hardware or kernel rebuild; existing draft PR retained.

## Review Focus

- A crash after durable reboot intent but before the command must not cause replay on resume.
- Journal fsync/rename/cleanup errors must stop later device effects and hide private paths.
- Already-unlocked/unknown env on a fresh journal must not be adopted as a completed unlock.
- Caller mutation, identity/GPT/critical/tail/usage drift must not change trusted policy.
- A detached Store or different directory/owner must not claim the protected lock scope.

### Task 1: Private journal

**Files:** create ember/tools/a133-unlock-journal.rb and a133-unlock-journal-test.rb.
**Interfaces:** `Journal.open(directory,context){|store|...}`; Store snapshot, record(phase,write:,reboot:,hardware_bytes:,reason:), report; nil context is inspection-only.

- [ ] Write real-file tests for durable monotonic intent, context/duplicate/checksum/mode/symlink/hardlink refusals, locking, escaped Store and injected publication failure.
- [ ] Run new journal test on both Rubies. Expected: missing implementation FAIL.
- [ ] Implement the schema/lock/publication contract in the spec.
- [ ] Run journal tests on both Rubies. Expected: all pass.
- [ ] Commit `feat(a133): persist private unlock stage intent`.

### Task 2: Guarded transition and instructions

**Files:** create a133-unlock-stage.rb, a133-unlock-stage-test.rb, a133-unlock-stage-fixture.rb and ember/boards/ys-m33-a133-unlock-stage.md; update README/cable/unlock-env instructions.
**Interfaces:** Stage.run signature from spec consumes Journal plus offline unlock encoder, Recovery Policy and Source/Channel; produces unlocked_recovery_verified or redacted Stage::Invalid report, installation_ready=false.

- [ ] Write actual-I/O tests for happy/restored repeat, prefix/write/reboot order and marker inspection, command failure/resume without duplicate reboot, unknown fresh states, evidence/identity/critical/tail/usage drift and journal refusal before USB.
- [ ] Run new stage test on both Rubies. Expected: missing stage FAIL.
- [ ] Implement fixed transitions, live rechecks and inspection-only CLI.
- [ ] Run all new suites and adjacent unlock44/cleanup3/codec/Recovery19/Protect14/Channel contracts on both; syntax/diff/local links. Expected: all pass.
- [ ] Commit `feat(a133): guard journaled vendor unlock recovery`.

## Completion

One fresh independent GPT-6 Astra review; one regression-first fix pass, no re-review. Carry all rulings/minors into plan/final, update existing draft PR and both wikis, then remove only this plan scratch. No physical acceptance claim.
