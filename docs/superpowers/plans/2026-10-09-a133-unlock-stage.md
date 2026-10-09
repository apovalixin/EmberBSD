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

- [x] Write real-file tests for durable monotonic intent, context/duplicate/checksum/mode/symlink/hardlink refusals, locking, escaped Store and injected publication failure.
- [x] Run new journal test on both Rubies. Expected: missing implementation FAIL.
- [x] Implement the schema/lock/publication contract in the spec.
- [x] Run journal tests on both Rubies. Expected: all pass.
- [x] Commit `feat(a133): persist private unlock stage intent`.

### Task 2: Guarded transition and instructions

**Files:** create a133-unlock-stage.rb, a133-unlock-stage-test.rb, a133-unlock-stage-cli-test.rb, a133-unlock-stage-admission-test.rb, a133-unlock-stage-fixture.rb and ember/boards/ys-m33-a133-unlock-stage.md; update README/cable/unlock-env instructions.
**Interfaces:** Stage.run signature from spec consumes Journal plus offline unlock encoder, Recovery Policy and Source/Channel; produces unlocked_recovery_verified or redacted Stage::Invalid report, installation_ready=false.

- [x] Write actual-I/O tests for happy/restored repeat, prefix/write/reboot order and marker inspection, command failure/resume without duplicate reboot, unknown fresh states, evidence/identity/critical/tail/usage drift and journal refusal before USB.
- [x] Run new stage test on both Rubies. Expected: missing stage FAIL.
- [x] Implement fixed transitions, live rechecks and inspection-only CLI.
- [x] Run all new suites and adjacent unlock44/cleanup3/codec/Recovery19/Protect14/Channel contracts on both; syntax/diff/local links. Expected: all pass.
- [x] Commit `feat(a133): guard journaled vendor unlock recovery`.

## Completion

One fresh independent GPT-6 Astra review; one regression-first fix pass, no re-review. Carry all rulings/minors into plan/final, update existing draft PR and both wikis, then remove only this plan scratch. No physical acceptance claim.

## Execution rulings

1. Keep the CLI late-close regression separate from transport tests. Inspection
   emits one final JSON only after lock release. Cost: host regressions do not
   establish filesystem power-loss durability.
2. Admission before journal load reports unknown/possible prior effects, rather
   than claiming a previous session had none. Cost: even a fresh invalid call
   can require journal inspection; this does not assert command submission.
3. Review the entire current plan range from `a86ef4d8555e`, including Journal
   and Stage. Cost: the whole historical BSP/main reconciliation and fleet
   release remain outside this review; the existing PR stays draft.
4. The task-done checkpoint uses syntax checks after the already completed
   ten-suite matrices on both runtimes. Cost: that checkpoint alone is weaker;
   completion depends on the separately read full outputs and exit codes. Do
   not repeat successful costly suites without a code change or new concern.

## Verification before final review

On macOS, Ruby 4.0.5 and system Ruby 2.6.10 each passed the ten required suites:
Journal18, Stage23, CLI2, Admission2, Unlock44, Cleanup3, env codec, Recovery19,
Protect14 and Channel13. Both full commands exited0. Syntax14, diff whitespace
and63 changed-page local links passed. Initial Journal/Stage tests were RED
on both runtimes before implementation; CLI late-close and admission prior
effects were separately RED then GREEN. No tablet operation, OS rebuild or
private full-archive reread belongs to this stage.
