# A133 resumable write-stage sessions implementation plan

> For agentic workers: use superpowers:executing-plans inline.

**Goal:** coordinate guarded write/readback with durable private progress and safe re-entry.
**Architecture:** callable bundle verifier, readback-only USB gate, locked JSON journal/coordinator.
**Tech stack:** Ruby standard library and existing guarded ADB/backup/environment tools.
**Spec:** [session design](../specs/2026-10-08-a133-install-session-design.md).

## Global constraints

- Private directory0700/files0600; max65536-byte snapshots; exact schema/checksum.
- Pin serial/CID, source_commit, backup/recovery/protected-env and three images.
- Root → boot → resources; always fresh guarded readback, including saved verified roles.
- Persist writing and fsync before any write; no automatic retry within a run.
- No physical write CLI, recovery unlock, env release, reboot or installation_ready=true.
- Caller owns fresh backup provenance/release validation; tests are file-backed.
- Existing linked worktree/reference retained; no Python, audio or camera tests.

## Review focus

- Importing the bundle checker must not run a CLI or weaken existing file guards.
- Lock contention, symlink/hardlink or unsafe modes must stop before ADB/write.
- Interrupted snapshot publication/corruption must not turn writing into accepted verified.
- Saved verified status must not skip mutated device bytes or changed recovery/identity.
- Errors/persistence failures must stop later roles and not leak private inputs.

### Task 1: importable integrity and live range inspection

**Files:** modify `ember/tools/a133-install-bundle.rb`, `a133-usb-transfer.rb`;
add `a133-bundle-library-test.rb`; extend transfer test and extract only its
fixture helpers into `a133-usb-test-support.rb` for reuse.
**Produces:** `A133Bundle.verify(path)` => `{receipt:, paths:}`; and
`Client.verify_installed(role:,bytes:,sha256:,backup:,protected_env:)` =>
range_readback_verified/writes_performed0 or Invalid/write_attempted=false.

- [ ] Write import/real-file and no-write readback tests; run RED against current tools.
- [ ] Export verifier with identical CLI receipt/guards; implement fully guarded readback.
- [ ] Run library, bundle35 and transfer extended contracts on both Ruby versions; commit.

### Task 2: private atomic session state and coordinator

**Files:** add `ember/tools/a133-install-session.rb`, `a133-install-session-test.rb`,
`ember/boards/ys-m33-a133-install-session.md`; update affected overview/cable documents.
**Consumes:** Task1 verify/path mapping/live readback, existing Client.write_verified.
**Produces:** `A133Install::Store.open(directory,context){|store|...}` with
`store.record(role,state,reason=nil)`/`store.report`; and `A133Install.run` signature
from spec, plus inspection-only CLI status for an existing session directory.

- [ ] Write real filesystem/lock/crash and file-backed full-run/resume tests; run RED.
- [ ] Implement exact private state schema, checksum, flock and atomic fsync publication.
- [ ] Implement pinned context and sequential check/write/verified flow; normalize errors.
- [ ] Run new and relevant existing host contracts; inspect final output and actual bytes.
- [ ] Independent review, reproduced fixes, limits in PR/wiki, normal push; no main merge.
