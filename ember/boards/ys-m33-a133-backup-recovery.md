# YS-M33 backup integrity and recovery preparation

[Cable installation](ys-m33-a133-cable-install.md) · [Board](ys-m33-a133.md)

These host tools implement two preparation stages of cable installation.
They do not discover a connected device, write storage, unlock or reboot.
Both always report `installation_ready=false`. A full fleet installer and
physical Android restoration remain separate acceptance work.

## Verify a saved full eMMC snapshot

Keep the original image and its separately recorded uncompressed SHA256 in
private storage. Use the recorded hash, not a fresh hash of a possibly damaged
file. The checker streams through the image without creating a raw copy:

```sh
ruby ember/tools/a133-backup-check.rb --sha256 "$a133_backup_sha256" /private/backup/emmc.img.zst
# A raw snapshot uses the same physical size and partition guards:
ruby ember/tools/a133-backup-check.rb --format raw --sha256 "$a133_backup_sha256" /private/backup/emmc.img
```

The default decoder is `zstd` from PATH; `--zstd /path/to/zstd` selects a trusted
host executable. The deadline defaults to 3600 seconds; `--timeout` accepts
1–7200. No decoder diagnostics, GUIDs, file contents or host paths enter JSON.
Zstd must finish without diagnostics or errors. Its process group is killed
and reaped on a read failure/deadline; stderr is drained with bounded memory.

Exit zero means all 31037849600 uncompressed bytes matched the expected hash,
both GPT header and table CRCs passed, reciprocal headers/geometry/GUIDs and
table copies agree, and all 17 partition names/bounds match this YS-M33 profile.
The protective MBR is checked; the vendor's 0xffffffff protective length is
accepted. It does not establish compatibility of another A133 board.
The same pass reports bootloader, env, boot and recovery range hashes so the
operator can compare independently saved per-device partition files.

`backup_integrity_verified` is an integrity receipt, not an authenticity or
restore receipt. A live Android snapshot can contain inconsistent mutable
filesystems. Before overwriting UDISK/metadata, retain their separately verified
quiescent recovery copies. eMMC hardware boot0/boot1 are outside the main
snapshot and must be saved separately. Device binding, retained secure state,
backup revalidation at use and physical restoration remain installer stages.

## Prepare a one-shot recovery environment copy

Read the exact original 128 KiB environment prefix from this particular tablet
into a protected host file. The preparer creates a new file; it cannot write
the tablet:

```sh
ruby ember/tools/a133-recovery-env.rb /private/backup/env-prefix.bin /private/staging/recovery-env.bin
```

Only the inspected factory profile is accepted: bootcmd runs setargs_mmc and
boot_normal, boot_normal runs the hook followed by boot_android, and the boot
and recovery scripts read their respective partitions at 0x45000000.
Existing hook or either helper variable is refused, including empty values.
Customized/reference-tablet scripts must not be coerced into this profile.

The candidate adds exactly hook, ember_restore_normal and ember_recovery_once.
The first helper clears the hook and saves normal boot; the second selects
USB device role in the vendor FDT and enters the retained recovery script.
Bootcmd, boot_normal, all other entries, padding and the original input remain
unchanged. Removing the three additions must reproduce every original byte.
The new 0600 file is published without replacing an existing output or symlink.
No environment contents are printed.

This is a prepared file, not an installed recovery path. Deployment must verify
the selected tablet, original env readback, full backup, locked recovery round
trip, complete write readback, rollback on failure and USB access. It must not
mistake a successful file-generation command for hardware acceptance.

## Evidence and reproducible checks

The host contracts use small independently constructed GPT images and real
decoder subprocesses, not a 31 GB allocation:

```sh
ruby ember/tools/a133-backup-check-test.rb
ruby ember/tools/a133-recovery-env-test.rb
ruby ember/tools/a133-env-edit-test.rb
ruby ember/tools/a133-install-bundle-test.rb
ruby ember/tools/a133-install-preflight-test.rb
sh ember/tools/a133-firstboot-guard-test.sh
```

On 2026-10-08 the new backup checker read the complete previously saved factory
image. Its raw SHA256, both GPT copies and four independently saved partition
hashes matched. That image was captured from live Android: filesystem
consistency and a physical full restore were not claimed by this check.
The generated recovery candidate was byte-identical to the prior candidate
used in the closed-enclosure installation trial. That file comparison does
not constitute another device write or recovery boot.
Images, per-device environments, keys and private configuration are not in Git.

Both Ruby 4.0.5 and the host's compatibility Ruby 2.6.10 passed 29 backup cases
and 19 recovery-copy cases. The existing environment, bundle, ADB preflight
and first-boot guard contracts also passed. Independent Codex GPT-6 Astra
review found no blocking defect. Its regression-coverage finding was addressed
with successful-exit/noisy-stderr and exited-parent/stalled-descendant cases;
the latter checks that the descendant is actually terminated.
The reviewer did not independently inspect private images or accept hardware
restoration. Production code was unchanged after the full-image check.
