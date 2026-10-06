# EmberBSD

This is a full fork of NetBSD/src with adaptations for single-board
computers. Talk to people in Russian. Everything that stays in the
repository is written in English: sources, comments, documentation,
diff descriptions, commit messages and this file. Commits follow
Conventional Commits.

Read README.ember.md first. Board support and its provenance live in
ember/, kernel changes go directly into sys/. C follows NetBSD KNF.
Keep upstream licences and identifiers. Our own source diffs start with
exactly one Origin: line; imported changes name the upstream revision.

A build exports a clean commit; do not fix sources only inside the build
guest. After a change, check the native build and the relevant contracts
in ember/tools. Do not present a hardware check under QEMU as a check of
the physical board. Read the current NetBSD rules before sending anything
upstream.

Applications are deployed separately from the OS sources. Their roles,
tokens, models and configurations are not brought here. Standalone
examples of using the OS live in the public EmberBSD-Examples repository.
Public documentation must be understandable without access to internal
repositories and must not contain their names or addresses.
Networks, SSH keys, Bluetooth bonds and personal images never enter Git.

## Keep the public project overview current

The root README is the public explanation of EmberBSD's value: what a
developer can build with it, what this fork and its companion projects add,
and how to start. Keep "What this fork adds to NetBSD 11", the introduction,
ecosystem links and support tables consistent with the actual project.
The overview covers OS work and optional Ports/Examples capabilities,
including AI, robotics, GPU, NPU and GUI; identify which repository owns each.

As part of completing a substantial feature, port, runnable example,
validation milestone, regression or support withdrawal, review this page
and update every affected claim. Companion-repository changes can require
a README update here even when no OS code changes. Inspect the latest
published sources and checks before editing; do not rely on chat summaries.

Describe the user benefit, link to the public implementation/instructions,
and state the validation level and relevant platform. Distinguish source,
build, contract tests, VM execution, physical hardware and sustained use.
Keep research and planned acceleration visibly separate from available
workflows. Preserve upstream credit; availability in NetBSD/pkgsrc alone
is not an EmberBSD contribution. Explain our adaptation or verified scenario.

Use a concise current overview, not a chronological change log. Keep exact
recipes, logs and detailed evidence in their owning repository. Correct
stale claims and broken links in the same task, and check documentation
links and consistency before publishing. If a companion overview update
cannot be published, identify the exact remaining change in the handoff.
