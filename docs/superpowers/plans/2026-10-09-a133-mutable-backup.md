# A133 Mutable Backup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Retain verified per-device UDISK/metadata files through a read-only recovery acquisition.

**Architecture:** A separate strict manifest checker feeds a second acquisition API in the existing backup module. Extend Source only for guarded mutable reads; preserve full-capture behavior and sampled-observation limits.

**Tech Stack:** Ruby, existing bounded ADB channel, actual dd fixtures, private files.

**Spec:** ../specs/2026-10-09-a133-mutable-backup-design.md

## Global Constraints

- English public code/docs; no new Python, secrets or images in Git.
- Fixed production17-partition31037849600-byte profile; UDISK27676098048/metadata16777216 bytes.
- Exact schema1 mutable manifest, <=65536 bytes; files0600/private directory0700; distinct inodes.
- Only root-readable USB recovery; explicit trusted serial/CID; no device writes/reboot/unlock/root restart.
- observation=recovery_unmounted_two_matching_reads; filesystem_consistency=not_established_by_integrity_check; installation_ready=false.
- New outputs only, no retries/adoption; preserve failed evidence; deadline1..7200 seconds.
- Preserve existing schema1/schema2 full-capture contracts and working reference.

## Review Focus

- A second namespace mounts the block device through an alias; reject the acquisition.
- UDISK changes while the other partition is read; no final success manifest.
- Host evidence changes during the last source check or publication; fail despite matching earlier hashes.
- The source has no mounts but active swap/holders or unreadable process inventory; reject.
- An old valid manifest is replayed for another CID or an aliased host file; refuse fresh verification.

### Task 1: Fresh private mutable-manifest verification

**Files:**
- Create: ember/tools/a133-mutable-check.rb
- Create: ember/tools/a133-mutable-test-support.rb
- Test: ember/tools/a133-mutable-check-test.rb

**Interfaces:**
- Consumes: A133Backup::INVENTORY physical partition sizes.
- Produces: A133Mutable.verify(path, serial:, cid:, timeout:3600), returning a private string-keyed receipt with mutable_copies_verified, serial, cid, partition_sha256 and observation; filesystem_consistency remains unestablished.

- [x] **Step 1: Write real-file tests** for fresh whole-byte hash verification, trusted identity mismatch, wrong schema/fields/roles/bytes/hash, duplicate keys, unsafe mode/path, inode aliases, late mutation and redacted CLI/no write flag.
- [x] **Step 2: Run `ruby ember/tools/a133-mutable-check-test.rb`.** Expected: FAIL with missing mutable checker.
- [x] **Step 3: Implement the strict verifier** with retained descriptors, privacy/identity/lifetime guards and bounded errors.
- [x] **Step 4: Run `ruby ember/tools/a133-mutable-check-test.rb` and `/usr/bin/ruby ember/tools/a133-mutable-check-test.rb`.** Expected: all cases pass, no private CLI output.
- [x] **Step 5: Commit** `feat(a133): verify retained mutable partition copies`.

### Task 2: Read-only recovery acquisition and public instructions

**Files:**
- Create: ember/tools/a133-usb-mutable-backup.rb
- Modify: ember/tools/a133-usb-backup.rb (parameterize retained manifest name only)
- Modify: ember/tools/a133-usb-backup-source.rb (guard mutable reads/checkpoints)
- Create: ember/tools/a133-usb-mutable-fixture.rb
- Test: ember/tools/a133-usb-mutable-backup-test.rb
- Create: ember/boards/ys-m33-a133-mutable-backup.md
- Modify: ember/boards/ys-m33-a133-backup-recovery.md, ys-m33-a133-usb-backup.md and README.md

**Interfaces:**
- Consumes: A133Mutable.verify from Task1; Source.inspect!/read; existing exclusive private host output helpers.
- Produces: A133UsbBackup.collect_mutable(directory:, serial:, cid:, adb:'adb', root_method:'adbd', timeout:3600), redacted usb_mutable_captured receipt, exactly3 private outputs; Source.mutable_inspect! and recovery-only UDISK/metadata read roles.

- [ ] **Step 1: Write actual-dd tests** for exact two-pass copies, namespace mount aliases, swap/holders/unreadable inventory, source change across copies, changed second-read bytes, late host mutation/publication, stream failure, destination collision, fsync failure, timeout and redacted CLI.
- [ ] **Step 2: Run `ruby ember/tools/a133-usb-mutable-backup-test.rb`.** Expected: FAIL with missing mutable acquisition.
- [ ] **Step 3: Implement source guards and collector** without device-write or automatic recovery commands; retain evidence through publication; document observation limits and storage/time cost.
- [ ] **Step 4: Run on both Rubies** mutable checker/acquisition and existing USB backup26/lifetime5/capture37/hardware18/review-regression6 suites. Expected: all pass; old full-capture behavior unchanged. Run existing channel13/transfer43/session25 on defaultRuby. Expected: all pass. Check Ruby syntax and `git diff --check`; expected: success.
- [ ] **Step 5: Commit** `feat(a133): retain recovery mutable backups over USB`.

## Completion

Fresh independent review of this plan's change range; findings get regression-first fixes, one fix pass. Publish the existing draft PR and separately update Ember/ROOMY evidence, preserving review status. Physical USB/restoration/power-cycle acceptance remains open. Record tested revisions, source observations, test counts and every ruling before removing this plan's scratch workspace.
