# A133 composed cable write stage implementation plan

> For agentic workers: use superpowers:executing-plans inline and TDD.

**Goal:** Resume unlock → protected recovery → image writes in a single pinned
host context without returning to unlock after protection starts.
**Spec:** ../specs/2026-10-10-a133-cable-write-stage-design.md
**Base:** 7c394ab2f00d009ab46d6b8b033b60701b585d50

## Global constraints

Existing worktree, fixed factory profile and trusted caller evidence; private
host state; no physical writes or feedback, secrets, new Python, OS rebuild,
trial boot or main merge. Preserve other checkouts and wiki work. Fresh scoped
Git before each stage. Keep draft PR and both wikis current.

## Review Focus

- A resume after protection intent must never re-enter unlock or submit reboot.
- Release/evidence context must be pinned before unlock and rechecked before image I/O.
- Fsync/rename/scope failures must preserve visible intent and stop later effects.
- Unknown partial env and live identity/recovery/hardware/usage drift must stop.
- Inspection and error paths must redact private inputs and conservatively report prior effects.

### Task 1: Image Store scope and bundle admission

**Produces:** unchanged Store schema/API with descriptor-bound lock, visible
post-rename state; optional `expected_bundle_sha256` writer admission and
`A133Install.bundle_digest(receipt)` semantic fingerprint.
**Files:** a133-install-session.rb, a133-install-session-regression-test.rb.

- [ ] Add real-file fsync/scope regressions and changed expected-bundle rejection.
- [ ] Run both Rubies; Expected: regressions FAIL for missing guards.
- [ ] Implement minimal guards and canonical fingerprint admission before USB.
- [ ] Run regressions and original Session25 both; Expected: all pass.
- [ ] Commit fix and record completion.

### Task 2: Outer journal and composed API

**Consumes:** Task1 fingerprint/admission, unlock Stage, Protection and image writer.
**Produces:** A133Cable.run and Journal, inspection-only command, public instructions.
**Files:** a133-cable-journal.rb, a133-cable-install.rb, corresponding tests and
native combined fixture; README.md and ember/boards/ys-m33-a133-cable-stage.md.

- [ ] Write journal and native composition/resume/refusal tests first.
- [ ] Run both; Expected: missing implementation FAIL.
- [ ] Implement explicit monotonic preparation journal and composed guards.
- [ ] Run new suites and affected Journal/Stage/Protection/Session/Transfer/Bundle
  suites on both Rubies; Expected: all pass, actual bytes/reboot evidence checked.
- [ ] Check syntax, diff and public links; commit implementation/docs.
- [ ] One fresh GPT-6 Astra high review of full current plan range; regression-first
  Important/Critical fixes once, record every ruling/minor and final test evidence.
- [ ] Update existing draft PR and both wikis; cleanup only this plan scratch.

## Current verification

The initial native composition14 passed on both runtimes, as did Journal15,
CLI2, unlocked-only admission2 and image regressions5/Session25. A23-suite
post-change matrix is running on both; final publication waits for its outputs.

## Execution rulings

1. Continue inline under the user's repeated autonomous-work authorization;
   no additional design/publication question. Cost: design decisions receive
   independent final review but no new manual approval before implementation.
2. Retain an independent outer journal rather than generalizing existing
   storage classes. Cost: duplicated mechanics require keeping regression
   coverage aligned; a later shared primitive remains separate work.
3. Hardware feedback and accepted first boot remain future gates. Cost: a
   green host composition does not prove physical install or fleet readiness.

4. The task-done checkpoint repeats inexpensive regressions after the read
   Session25 outputs. Cost: that checkpoint alone does not replace the original
   suite evidence.
5. Add require_unlocked:true to Protection's repeated guards in this flow;
   preserve its default API. Cost: one additional opt-in constructor contract.
6. Start the single fresh review while the final regression matrices run, after
   initial native/new tests pass. Cost: its verdict alone cannot establish final
   test success; publication waits for both matrices and any required fix pass.
