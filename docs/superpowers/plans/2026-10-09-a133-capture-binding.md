# A133 capture binding implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline task-by-task.

**Goal:** freshly verify full backup and critical copies against a trusted capture identity.
**Architecture:** expose the current file checker; consume it in a private manifest verifier.
**Tech Stack:** Ruby standard library, existing GPT checker and decoder process group.
**Spec:** [capture binding design](../specs/2026-10-09-a133-capture-binding-design.md).

## Global Constraints

- Production size31037849600/17-partition inventory stays fixed; no test mode.
- Private directory0700/files0600; bounded65536-byte manifest and exact schema.
- Recorded serial/CID/SHA are trusted inputs; no inference for a missing CID.
- Fresh full-image read and four full critical-copy hashes per invocation.
- No hardware writes, reboot, audio/camera tests or installation_ready=true.
- Preserve existing linked worktree, private images and caller firstboot obligations.

## Review Focus

- Callable checker and CLI must share guards and decoder cleanup, including outer rescue.
- Success cannot come from a cached JSON receipt or a wrong-device capture record.
- Links, unsafe modes, duplicate keys or paths cannot bypass same-directory policy.
- Mutation/deadline after initial full-image verification must not return success.
- Private receipt never leaks through CLI/errors; no implied consistency/provenance claim.

### Task 1: callable full-backup file verification

**Files:** modify `ember/tools/a133-backup-check.rb`, extract fixture-only support
from `a133-backup-check-test.rb`; add `a133-backup-library-test.rb`.
**Interfaces:** produces `A133Backup.verify_file(path, sha256:, format: 'zstd',
zstd: 'zstd', timeout: 3600)` with the current symbol-keyed CLI receipt.

- [ ] Write tests for silent import, real raw/decoded file verification, hash/size,
  source mutation, decoder failure/timeout and invocation from outer rescue.
- [ ] Run `ruby ember/tools/a133-backup-library-test.rb`; Expected: missing API RED.
- [ ] Extract current fixed-profile file check and normalized reasons; make CLI consume it.
- [ ] Run library and backup29 on Ruby4/system2.6; Expected: all cases pass; commit.

### Task 2: trusted capture binding and critical copies

**Files:** add `ember/tools/a133-capture-check.rb`, `a133-capture-check-test.rb`;
update backup/cable instructions and affected overview claims.
**Interfaces:** consumes Task1 verifier. Produces `A133Capture.verify(path,
serial:, cid:, zstd: 'zstd', timeout: 3600)` returning string-keyed USB backup
receipt with recorded serial/CID; CLI redacts those identifiers.

- [ ] Write real-file tests for a four-partition independent GPT/copy capture,
  identity mismatch, critical copy hash/size, changed backup after prior success,
  private modes/links, JSON schema/duplicates/path/size, and redacted CLI.
- [ ] Run `ruby ember/tools/a133-capture-check-test.rb`; Expected: missing tool RED.
- [ ] Implement exact record/file policy, fresh backup verification and four-copy hashes.
- [ ] Run capture/library/backup29 on both Ruby versions and USB/session/environment
  contracts on Ruby4; Expected: all pass; commit.
- [ ] Independent review, required RED/GREEN fixes, documentation links and normal push.
