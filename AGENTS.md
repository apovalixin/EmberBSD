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
