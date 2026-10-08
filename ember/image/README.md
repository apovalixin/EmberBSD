# AArch64 development image

This OS-owned builder combines verified NetBSD base sets, an accepted EmberBSD
kernel, an accepted static `fsck_ffs`, matching CTF tools and libdwarf,
and the offline GDB/LLVM package closure from
[EmberBSD Ports](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain).
It creates a new 12 GiB qcow2 image. First boot installs the packages through
native `pkg_add`; boot and installed-debugger acceptance remain separate steps.

## Previous image acceptance

On 2026-10-08, a fresh image passed first boot and a normal reboot under
Apple Silicon QEMU/HVF, AArch64 `virt`, four CPUs and 4 GiB RAM. All eleven
offline packages installed and passed `pkg_admin check`. Both boots passed
live GCC16 DWARF5 split-DWARF32/64 checks: breakpoint, argument and local
values, two stack frames and normal process exit. The second boot retained
the installation receipt instead of reinstalling the packages. Shutdown
unmounted the filesystem cleanly; no FFS warning or kernel panic occurred.

The tested inputs were kernel revision
`21cd2464c720159bec0a3ba352e4dee940a58b02`, fsck revision
`56ed7398e95a9b6a73f2d99d2cac0705abb69fa3`, and Ports package revision
`a40f855ee9c4f9ae70f0807f71421c41603ec87d`. The pristine qcow2 SHA256 is
`ca0cbb9a4cc8d3324fa99c91b3d6eaa23411310ee5411869139995090376e7b4`.
Acceptance ran on a disposable overlay; the pristine image still performs
offline package installation on its own first boot. Binary images are not
stored in Git. The builder's `inputs.txt` records input and script hashes.

The base remains the verified NetBSD 11 sets. This is not a complete GCC16
userland rebuild, physical-board image or proof of every DWARF form. The
[Ports matrix](https://github.com/oxtech-ember/EmberBSD-Ports/blob/main/profiles/development-toolchain/gdb/dwarf-variants.md)
records the debugger's accepted cases; revision nb1 closes the earlier entry-value gaps.
The console accepts the base set's root login without a password; SSH is
disabled. Configure credentials and access for the deployment before use.

## Required inputs

- NetBSD 11/AArch64 `base.tar.xz`, `etc.tar.xz`, `comp.tar.xz` and their original
  `SHA512` manifest. Archive hashes are checked before extraction.
- An accepted AArch64 kernel and its full source commit ID.
- The **static** `fsck_ffs` built from the corrected OS sources below, its full
  source commit ID, and passing native acceptance. The unmodified base-set
  checker is not an acceptable replacement.
- An accepted CTF bundle and its full OS source commit ID. The directory
  contains regular files `ctfconvert`, `libdwarf.so.2.2`, `libdwarf.a`,
  `libdwarf_p.a`, `libdwarf_pic.a`, `libdwarf.h`, and `dwarf.h` from the same build.
  Follow the [CTF build and target checks](../boot/dtrace-dwarf.md).
- GDB 18.1, optional common LLVM 23.1.2 and their complete package dependency closure,
  plus the full Ports source commit ID. Exactly one debugger archive is allowed,
  including its pkgsrc `nb` revision; obsolete or parallel debugger versions fail.
- NetBSD host tools `nbmakefs`, `nbgpt`, and host `qemu-img`, `tar`, `shasum`.
  The builder uses absolute paths without whitespace or shell metacharacters.

```sh
sh ember/image/development-image.sh \
    /absolute/sets /absolute/netbsd KERNEL_COMMIT \
    /absolute/static-fsck_ffs FSCK_COMMIT \
    /absolute/ctf-tools CTF_COMMIT \
    /absolute/packages PORTS_COMMIT /absolute/tools /absolute/new-image
```

The builder records hashes with stable artifact names, avoiding private host
paths in the image. Distribution mtree files preserve target users, groups,
modes and special nodes. Explicit entries describe changed and new files.
Base manuals are omitted because some names collide on case-insensitive build
volumes. The old base GDB and `gdbtui` are removed; `/usr/bin/gdb` points to the
packaged debugger. Static `fsck_ffs` replaces both `/sbin/fsck_ffs` and its
`/rescue` entry, without modifying the other rescue crunch hardlinks.
The accepted converter, shared/static libdwarf and both development headers replace
the base-set copies together. Only libdwarf 2.2 remains in the runtime library
directory; its two linker/loader symlinks point to that same file.

The first-boot installer rejects missing, duplicate, unlisted and symlink
archives before invoking `pkg_add`. It rejects obsolete or parallel LLVM providers when LLVM is included, then installs\nall listed packages from the local bundle. It verifies package checksums, installed
package integrity and debugger selection before recording completion. Its
read-only preflight also runs on the build host:

```sh
sh ember/image/install-development.sh --verify /absolute/package-bundle
```

## FFS superblock selection repair

NetBSD `makefs` places its UFS2 primary superblock at 8 KiB. With 64 KiB blocks,
the first alternate lies at 64 KiB, the first automatic search address. The
unmodified `fsck_ffs` accepted that alternate without checking `fs_sblockloc`.
The kernel checks the recorded location and uses the actual 8 KiB primary.
After writes or a crash, the alternate can still say clean while the primary
is dirty. An ordinary check then skips the filesystem or updates the wrong
superblock.

The local change in `sbin/fsck_ffs/setup.c` requires an automatically selected
superblock to match its recorded primary location. It preserves old UFS1
layouts without that field and explicit alternate selection with `-b`. This
is an AI-assisted EmberBSD change; it has not been accepted upstream.

Use the prepared GCC16 cross-build wrapper, a complete AArch64 sysroot and
an object directory belonging to this build. Cleaning this target before
changing link mode ensures a previous dynamic binary is not reused:

```sh
/absolute/tools/bin/nbmake-evbarm -C /absolute/source/sbin/fsck_ffs \
    DESTDIR=/absolute/sysroot MKMAN=no MKHTML=no clean
/absolute/tools/bin/nbmake-evbarm -C /absolute/source/sbin/fsck_ffs \
    DESTDIR=/absolute/sysroot MKDEBUG=no MKDEBUGLIB=no MKMAN=no MKHTML=no \
    LDSTATIC=-static MKDYNAMICROOT=no dependall
```

Inspect the resulting ELF program headers and dynamic section with the cross
`readelf`: there must be no `INTERP` segment, dynamic section or `NEEDED`
libraries. Record its SHA256 and source revision with its acceptance results.

## Regression and recovery checks

```sh
sh ember/image/test-development.sh /absolute/tools
sh ember/tools/ffs-superblock-contract.sh /absolute/tools /absolute/new-fixtures
```

The first test covers bundle rejection, distribution ownership, traversal of
nested directories with mode `000`, special nodes, debugger selection, the
rescue hardlink, recovery menu and sanitized provenance.
Its fake `makefs` stops before any image is built. The second compiles the actual
production selector, checks UFS1 and both byte orders, and creates four small
UFS2/UFS2EA files using real `makefs`. Original code fails the alternate-location
check; corrected code passes. Optional native fixtures make only the primary
dirty, leaving the alternate clean.

Transfer those four fixture files, the old checker and the fixed static checker
to a private test directory on NetBSD/AArch64. Preserve no macOS archive extended
attributes when packaging the fixtures. Run:

```sh
sh ember/tools/ffs-superblock-target.sh \
    /absolute/old-fsck /absolute/fixed-fsck \
    /absolute/fixtures /absolute/new-results
```

This checks the old false-clean skip and corrected primary selection, then runs
forced checks with both binaries. All invocations use `-F -n` on ordinary files;
before/after hashes prove the fixtures remain unchanged. The four native
before/after cases passed in an AArch64 VM. That does not establish a complete
release image or physical-board acceptance.

The boot menu provides **Recovery (single user)** with a ten-second selection
window. For an existing filesystem made with the affected layout, work from a
separate recovery system with the target unmounted. Prefer the corrected
checker. If only the old checker is available, explicitly select the verified
primary: `-b 16` means offset 8192 only on a device with 512-byte sectors.
First inspect with `-f -n`; perform repair on the recovery copy, then repeat the
read-only check. Do not equate the old checker's automatic clean result with a
clean primary superblock.
