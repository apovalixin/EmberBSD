# A133 Recovery Protection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare and verify the persistent recovery env required by the guarded A133 image writer.

**Architecture:** Extend the copied-byte env preparer; add a separate write primitive consuming trusted fresh backup receipts. Reuse read-only Source for pinned recovery inspection and full partition reads, with the bounded channel for exactly one env prefix write.

**Tech Stack:** Ruby, existing ADB channel, real-dd sparse device fixtures.

**Spec:** ../specs/2026-10-09-a133-recovery-protection-design.md

## Global Constraints

- English public code/docs, no Python or private identifiers/images in Git.
- Fixed31037849600-byte17-partition geometry; env16777216, prefix131072, recovery33554432 bytes.
- Explicit serial/CID; recovery locked1/green or unlocked0/orange; root_method adbd/vendor_su.
- Trusted fresh schema2 capture and mutable receipts supplied in-process by the caller; no saved-receipt CLI or global device lease.
- Only prefix of mmcblk0p2 may be written; no unlock/reboot/root restart/GPT/recovery/boot-area writes.
- Unknown state fails closed; no automatic retry/rollback. Return installation_ready=false.
- Capture caller policy bytes before USB; check all task mount inventories/swap/holders, full env and recovery hashes and pinned GPT/profile.
- Keep working reference untouched; physical cold boot/cable return/restore/first boot still pending.

## Review Focus

- Caller policy strings change during a blocking USB operation; pinned values still govern.
- Env tail changes even when prefix readback matches; reject completion.
- A nonleader recovery thread mounts an alias of eMMC between preflight and write; reject or report possible write after submission.
- Sync fails after successful dd; no success or unsafe automatic retry.
- Already-protected env belongs to a different original backup or GPT; no idempotent adoption.

### Task 1: Deterministic persistent protection bytes

**Files:**
- Modify: ember/tools/a133-recovery-env.rb
- Create: ember/tools/a133-recovery-protect-test.rb

**Interfaces:**
- Consumes: A133Env.decode/patch and existing PROFILE/UPDATES.
- Produces: A133Recovery.protect(data), returning131072 bytes or A133Recovery::Invalid.

- [ ] **Step 1: Write independent-byte tests** for intended two-variable changes, exact reversible padding/opaque preservation, existing reserved keys, unsupported profiles, bad CRC/size and compatibility with the image writer's protection validation.
- [ ] **Step 2: Run `ruby ember/tools/a133-recovery-protect-test.rb`.** Expected: FAIL because protect is missing.
- [ ] **Step 3: Implement protect** with profile/collision checks and byte restoration.
- [ ] **Step 4: Run new protect and existing recovery-env tests on `ruby` and `/usr/bin/ruby`.** Expected: all pass.
- [ ] **Step 5: Commit** `feat(a133): prepare persistent recovery protection`.

### Task 2: Guarded env transition and instructions

**Files:**
- Create: ember/tools/a133-recovery-protection.rb
- Create: ember/tools/a133-recovery-protection-fixture.rb
- Create: ember/tools/a133-recovery-protection-test.rb
- Create: ember/boards/ys-m33-a133-recovery-protection.md
- Modify: README.md, ember/boards/ys-m33-a133-backup-recovery.md

**Interfaces:**
- Consumes: protect from Task1, Source.mutable_inspect!/read, Channel.run, trusted receipt shapes from Capture/Mutable.
- Produces: Protection.new(adb:,serial:,cid:,root_method:'adbd',timeout:600).install(original_env:,backup:,mutable:), returning symbol-keyed recovery_protection_verified receipt with protected_env, env_sha256, writes_performed and installation_ready:false. Failures are A133Usb::Invalid with accurate write_attempted boolean.

- [ ] **Step 1: Write actual-dd tests** for exact prefix and tail preservation, idempotent fresh check, locked/unlocked/wrappers, receipt/identity/state/layout/GPT mismatch, foreign env/recovery, threaded mounts/swap/holders, policy mutation during USB, bounded read errors, partial write/sync/readback failure and post-write drift. Before-write failures preserve all device files; after-write failures retain attempted status.
- [ ] **Step 2: Run `ruby ember/tools/a133-recovery-protection-test.rb`.** Expected: FAIL with missing protection primitive.
- [ ] **Step 3: Implement pinned policy and separate guarded writer**, then document caller trust/exclusivity and physical limitations.
- [ ] **Step 4: Run both new suites and existing recovery-env/channel/transfer/session/mutable/check/acquisition/capture contracts on both Rubies; syntax and `git diff --check`.** Expected: all pass. There is no single native test command covering all OS components; relevant host-tool contracts define this stage's suite.
- [ ] **Step 5: Commit** `feat(a133): guard persistent recovery env writes`.

## Completion

One fresh independent review of this plan's entire change range; one regression-first fix pass. Update the existing draft PR and Ember/ROOMY wiki with source revisions and verification limits; preserve ROOMY review status. Publish under existing autonomous authorization. Copy all rulings/review evidence into this plan and final response before removing only this plan's scratch workspace.
