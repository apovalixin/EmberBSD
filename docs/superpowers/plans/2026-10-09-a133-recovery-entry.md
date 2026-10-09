# A133 Recovery Entry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Guard the Android one-shot recovery env write and exact factory restoration from known recovery states.

**Architecture:** Expose the full-backup GPT digest, add read-only target-env usage inspection, and implement a separate two-phase env transition library. Reuse the existing env codec, one-shot preparer, Source reads and binary channel.

**Tech Stack:** Ruby, ADB, actual-dd sparse GPT/partition fixtures.

**Spec:** ../specs/2026-10-09-a133-recovery-entry-design.md

## Global Constraints

- English code/public docs; no Python, secrets or private images in Git.
- Production31037849600-byte17-partition geometry; env16777216/prefix131072 bytes.
- Fresh in-process capture receipts with serial/CID, GPT digest and critical/hardware evidence; caller retains evidence/exclusive operation.
- Only locked1/green, explicit device/recovery state and adbd/vendor_su root wrapper.
- Android permits other partition mounts but rejects whole-eMMC/env mounts/holders and all active swap; recovery retains stricter mutable guard.
- No automatic reboot/unlock/root restart/retry/rollback. Only env prefix dd+sync; installation_ready=false.
- Consumed env matches every expected variable and original tail, while valid export ordering/padding may differ.
- Preserve old protection/session/capture schema contracts and working reference; no kernel/userland changes.

## Review Focus

- Android uses a dm alias of env while userdata is also mounted; refuse the target use and permit ordinary unrelated mounts.
- Vendor saveenv reorders variables or changes another variable; accept only exact known consumed values, never an extra or modified value.
- A caller replays an old backup on matching CID with different valid GPT; refuse before writing.
- After env submission, launcher/transport fails or another namespace uses env; preserve possible-write status and privacy.
- Manual repeat after partial/sync failure; fresh exact-state checks must not adopt an unknown env or claim durability.

### Task 1: Backup GPT binding and target-env usage inspection

**Files:**
- Modify: ember/tools/a133-backup-check.rb (receipt field only)
- Modify: ember/tools/a133-usb-backup-source.rb (read-only environment_inspect!)
- Create: ember/tools/a133-backup-gpt-test.rb
- Create: ember/tools/a133-env-usage-test.rb
- Modify: ember/tools/a133-recovery-protection-fixture.rb (test-only queries)

**Interfaces:**
- Consumes: verify_stream retained GPT bytes, Source.inspect!/mutable_inspect! and channel.
- Produces: gpt_sha256 in stream/file/capture receipts; Source.environment_inspect! returning pinned profile or Invalid.

- [ ] **Step 1: Write tests** for fresh raw/capture GPT digest, changed valid GPT identity, other Android mounts permitted, whole/env/task alias mount, holders, swap, unreadable/malformed inventories and unchanged source.
- [ ] **Step 2: Run new GPT/usage tests.** Expected: FAIL with missing receipt field or inspection method.
- [ ] **Step 3: Implement receipt propagation and target guard** while keeping Source write-free and old schemas unchanged.
- [ ] **Step 4: Run both new suites and old backup-library/capture-check/mutable-thread tests on both Rubies.** Expected: all pass. Whole relevant suite is completed with Task2 because its fixture exercises this interface during actual writes.
- [ ] **Step 5: Commit** `feat(a133): bind env transitions to backup GPT and usage`.

### Task 2: Guarded arm/restore library and instructions

**Files:**
- Create: ember/tools/a133-recovery-entry.rb
- Create: ember/tools/a133-recovery-entry-test.rb
- Modify: ember/tools/a133-recovery-protection-fixture.rb (actual entry fault modes)
- Create: ember/boards/ys-m33-a133-recovery-entry.md
- Modify: README.md, ember/boards/ys-m33-a133-cable-install.md

**Interfaces:**
- Consumes: capture receipt gpt_sha256 and Source.environment_inspect! from Task1; prepare/decode/patch, Channel.run.
- Produces: Entry.new(adb:,serial:,cid:,state:,root_method:'vendor_su',timeout:600).arm/restore(original_env:,backup:), symbol-keyed env_entry_armed/restored receipt with accurate writes_performed, env_sha256 and installation_ready:false; errors Invalid with write_attempted.

- [ ] **Step 1: Write actual-dd tests** for device arm/repeat, recovery restore of armed/consumed/reordered states, original repeat, full tail and critical images unchanged; wrong state/backup/GPT/profile/usage, altered consumed values, caller mutation, read/partial/sync/readback failures and late transport/source drift.
- [ ] **Step 2: Run `ruby ember/tools/a133-recovery-entry-test.rb`.** Expected: FAIL with missing entry library.
- [ ] **Step 3: Implement policy pinning and strict state selection**, with only prefix writes and normalized errors. Document vendor/hardware/no-swap limits and remaining unlock/trial boot stages.
- [ ] **Step 4: Run all new tests and old recovery-env/protect/protection/regression/channel/transfer/session/mutable/capture/backup/acquisition contracts on both Rubies.** Expected: all pass; no single whole-OS host command exists. Run syntax/diff/local-link checks; expected success.
- [ ] **Step 5: Commit** `feat(a133): guard one-shot recovery entry and restoration`.

## Completion

One fresh independent review of this plan's whole range; one regression-first fix pass. Publish to the existing draft PR and update related project evidence without changing product acceptance. Carry all rulings/minors into the plan and final answer, then remove only this plan's scratch workspace. Factory unlock, real round trips and accepted trial boot remain required stages.
