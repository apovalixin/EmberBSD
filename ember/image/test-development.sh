#!/bin/sh
# Origin: EmberBSD (AI-assisted), development image staging regressions.
set -eu
[ "$#" = 1 ] || { echo "Usage: $0 TOOLDIR" >&2; exit 2; }
tools=$1
[ -x "$tools/bin/nbmtree" ]
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/ember-development-test.XXXXXX")
cleanup()
{
	find "$work" -type d -exec chmod u+rwx {} \;
	rm -rf "$work"
}
trap cleanup EXIT HUP INT TERM
work=$(CDPATH= cd -- "$work" && pwd)
mkdir "$work/bundle"
bundle=$work/bundle
printf 'debugger fixture\n' > "$bundle/gdb-18.1.tgz"
printf 'dependency fixture\n' > "$bundle/gmp-6.3.0.tgz"
printf '%040d\n' 1 > "$bundle/ports-revision"
(cd "$bundle"; shasum -a 256 ./*.tgz | sed 's,  ./,  ,') > "$bundle/packages.sha256"
cp "$bundle/packages.sha256" "$work/good-manifest"
passed=0
expect()
{
	want=$1 label=$2
	actual=fail
	if sh "$here/install-development.sh" --verify "$bundle" > "$work/verify.log" 2>&1; then
		actual=pass
	fi
	if [ "$actual" != "$want" ]; then
		cat "$work/verify.log" >&2
		echo "FAIL: $label ($actual, expected $want)" >&2
		exit 1
	fi
	passed=$((passed + 1))
	printf 'PASS: %s\n' "$label"
}
expect pass 'complete bundle'
sed '/gdb-18[.]1[.]tgz$/d' "$work/good-manifest" > "$bundle/packages.sha256"
expect fail 'unlisted root debugger'
sed '/gmp-6[.]3[.]0[.]tgz$/d' "$work/good-manifest" > "$bundle/packages.sha256"
expect fail 'unlisted dependency'
cat "$work/good-manifest" "$work/good-manifest" > "$bundle/packages.sha256"
expect fail 'duplicate manifest records'
sed 's,  gmp-,  ../gmp-,' "$work/good-manifest" > "$bundle/packages.sha256"
expect fail 'path in manifest'
cp "$work/good-manifest" "$bundle/packages.sha256"
printf 'corrupted\n' >> "$bundle/gmp-6.3.0.tgz"
expect fail 'checksum mismatch'
printf 'dependency fixture\n' > "$bundle/gmp-6.3.0.tgz"
mv "$bundle/gmp-6.3.0.tgz" "$work/dependency"
expect fail 'missing archive'
ln -s "$work/dependency" "$bundle/gmp-6.3.0.tgz"
expect fail 'symlink archive'
rm "$bundle/gmp-6.3.0.tgz"
mv "$work/dependency" "$bundle/gmp-6.3.0.tgz"
touch "$bundle/.hidden.tgz"
expect fail 'hidden extra archive'
rm "$bundle/.hidden.tgz"
touch "$bundle/other.txz"
expect fail 'alternative archive suffix'
rm "$bundle/other.txz"
mkdir "$bundle/other.tgz"
expect fail 'directory in archive inventory'
rmdir "$bundle/other.tgz"
printf 'not-a-revision\n' > "$bundle/ports-revision"
expect fail 'invalid source revision'
printf '%040d\n' 1 > "$bundle/ports-revision"
# A missing final newline must not skip the last checksum verification.
printf '%s' "$(cat "$work/good-manifest")" > "$bundle/packages.sha256"
expect pass 'manifest without final newline'
printf 'corrupted\n' >> "$bundle/gmp-6.3.0.tgz"
expect fail 'last checksum without final newline'
printf 'dependency fixture\n' > "$bundle/gmp-6.3.0.tgz"
cp "$work/good-manifest" "$bundle/packages.sha256"
for receipt in installed-packages.sha256 installed-packages.sha256.new gdb-version.txt gdb.sha256; do
	touch "$bundle/$receipt"
done
expect pass 'retry with prior receipts'

# Tiny distribution fixtures preserve the metadata shapes used by NetBSD
# release sets. The fake makefs stops at the image boundary, without a disk.
for set in base etc comp; do mkdir -p "$work/$set/etc/mtree"; done
mkdir -p "$work/base/usr/bin" "$work/base/usr/mdec" "$work/base/sbin" "$work/base/rescue" \
    "$work/base/var/chroot/nsd/var/db" "$work/base/usr/share/man/man1" \
    "$work/base/var/spool/ftp/hidden/nested" \
    "$work/etc/etc/rc.d" "$work/etc/dev" "$work/comp/usr/bin" \
    "$work/sets" "$work/packages" "$work/tools/bin"
printf 'wall\n' > "$work/base/usr/bin/wall"
printf 'write\n' > "$work/base/usr/bin/write"
printf 'firmware\n' > "$work/base/usr/mdec/bootaa64.efi"
printf 'old fsck\n' > "$work/base/sbin/fsck_ffs"
printf 'old crunch\n' > "$work/base/rescue/cat"
ln "$work/base/rescue/cat" "$work/base/rescue/fsck_ffs"
printf 'omitted manual\n' > "$work/base/usr/share/man/man1/example.1"
printf 'old debugger\n' > "$work/comp/usr/bin/gdb"
printf 'old TUI debugger\n' > "$work/comp/usr/bin/gdbtui"
printf 'restricted directory payload\n' > "$work/base/var/spool/ftp/hidden/nested/payload"
printf 'rc_configured=NO\n' > "$work/etc/etc/rc.conf"
printf 'root:*:0:0::0:0:root:/root:/bin/sh\n_nsd:*:32:32::0:0:nsd:/var/empty:/sbin/nologin\n' \
    > "$work/etc/etc/master.passwd"
printf 'wheel:*:0:root\ntty:*:4:\n_nsd:*:32:\n' > "$work/etc/etc/group"
cat > "$work/base/etc/mtree/NetBSD.dist" <<'EOF'
/set type=dir uname=root gname=wheel mode=0755
.
./usr
./sbin
./rescue
./usr/bin
./usr/libexec
./usr/mdec
./usr/share
./usr/share/man
./usr/share/man/man1
./var
./var/chroot
./var/chroot/nsd uname=_nsd gname=_nsd
./var/chroot/nsd/var uname=_nsd gname=_nsd
./var/chroot/nsd/var/db uname=_nsd gname=_nsd
./var/spool
./var/spool/ftp
./var/spool/ftp/hidden mode=0000
./var/spool/ftp/hidden/nested mode=0000
./etc
./etc/rc.d
./etc/mtree
./dev
EOF
cat > "$work/base/etc/mtree/set.base" <<'EOF'
./usr/bin/wall type=file uname=root gname=tty mode=02555
./usr/bin/write type=file uname=root gname=tty mode=02555
./usr/mdec/bootaa64.efi type=file uname=root gname=wheel mode=0444
./sbin/fsck_ffs type=file uname=root gname=wheel mode=0555 size=9
./rescue/fsck_ffs type=file uname=root gname=wheel mode=0555 size=11
./rescue/cat type=file uname=root gname=wheel mode=0555
./var/chroot/nsd/var/db type=dir uname=_nsd gname=_nsd mode=0755
./var/spool/ftp/hidden/nested/payload type=file uname=root gname=wheel mode=0444
./usr/share/man/man1/example.1 type=file uname=root gname=wheel mode=0444
EOF
cat > "$work/etc/etc/mtree/set.etc" <<'EOF'
./etc/rc.conf type=file uname=root gname=wheel mode=0644 size=17
./etc/group type=file uname=root gname=wheel mode=0644
./etc/master.passwd type=file uname=root gname=wheel mode=0600
./dev/MAKEDEV type=file uname=root gname=wheel mode=0555
EOF
cat > "$work/comp/etc/mtree/set.comp" <<'EOF'
./usr/bin/gdb type=file uname=root gname=wheel mode=0555 size=13
./usr/bin/gdbtui type=file uname=root gname=wheel mode=0555 size=17
EOF
cat > "$work/etc/dev/MAKEDEV" <<'EOF'
#!/bin/sh
echo './console type=char uname=root gname=wheel mode=0600 device=netbsd,0,0'
echo './null type=char uname=root gname=wheel mode=0666 device=netbsd,2,2'
EOF
cat > "$work/tools/bin/nbmakefs" <<'EOF'
#!/bin/sh
exit 99
EOF
printf '#!/bin/sh\nexit 98\n' > "$work/tools/bin/nbgpt"
chmod +x "$work/tools/bin/nbmakefs" "$work/tools/bin/nbgpt"
for set in base etc comp; do
	(cd "$work/$set"; tar -cf "$work/$set.tar" .)
	if [ "$set" = base ]; then
		# Append restrictive directory headers after archiving their data.
		# Extraction restores both 000 modes without requiring root to build
		# the fixture; the image builder must open each parent before descent.
		for directory in var/spool/ftp/hidden/nested var/spool/ftp/hidden; do
			chmod 000 "$work/base/$directory"
			COPYFILE_DISABLE=1 tar --no-xattrs --no-recursion \
			    -rf "$work/base.tar" -C "$work/base" "./$directory"
			chmod 700 "$work/base/$directory"
		done
	fi
	tar -cJf "$work/sets/$set.tar.xz" @"$work/$set.tar"
	printf 'SHA512 (%s.tar.xz) = %s\n' "$set" \
	    "$(shasum -a 512 "$work/sets/$set.tar.xz" | awk '{print $1}')" >> "$work/sets/SHA512"
done
cp "$bundle"/*.tgz "$work/packages/"
printf 'kernel\n' > "$work/netbsd"
printf 'accepted fsck\n' > "$work/fsck_ffs"
revision=$(printf '%040d' 1)
status=0
sh "$here/development-image.sh" "$work/sets" "$work/netbsd" "$revision" \
    "$work/fsck_ffs" "$revision" "$work/packages" "$revision" "$work/tools" \
    "$work/stage" > "$work/stage.log" 2>&1 || status=$?
if [ "$status" != 99 ]; then cat "$work/stage.log" >&2; exit 1; fi
root=$work/stage/root
"$tools/bin/nbmtree" -C -k type,uname,gname,mode,link,device,size \
    -N "$root/etc" -f "$work/stage/spec" > "$work/normalized.spec"
grep '^./usr/bin/wall ' "$work/normalized.spec" | grep -q 'gname=tty'
grep '^./usr/bin/write ' "$work/normalized.spec" | grep -q 'mode=02555'
grep '^./var/chroot/nsd/var/db ' "$work/normalized.spec" | grep -q 'uname=_nsd'
grep '^./var/spool/ftp/hidden ' "$work/normalized.spec" | grep -Eq 'mode=0+([[:space:]]|$)'
grep '^./var/spool/ftp/hidden/nested ' "$work/normalized.spec" | grep -Eq 'mode=0+([[:space:]]|$)'
cmp "$work/base/var/spool/ftp/hidden/nested/payload" \
    "$root/var/spool/ftp/hidden/nested/payload"
grep '^./dev/console ' "$work/normalized.spec" | grep -q 'type=char'
grep '^./usr/bin/gdb ' "$work/normalized.spec" | grep -q 'type=link'
! grep -q '^./usr/bin/gdbtui ' "$work/normalized.spec"
! grep -q '^./usr/share/man' "$work/normalized.spec"
! grep -q 'size=' "$work/normalized.spec"
[ "$(readlink "$root/usr/bin/gdb")" = /usr/pkg/bin/gdb ]
[ ! -e "$root/usr/bin/gdbtui" ]
[ ! -e "$root/usr/share/man" ]
[ ! -e "$work/stage/disk.raw" ]
cmp "$work/fsck_ffs" "$root/sbin/fsck_ffs"
cmp "$work/fsck_ffs" "$root/rescue/fsck_ffs"
cmp "$work/base/rescue/cat" "$root/rescue/cat"
grep -q '^menu=Recovery.*:boot netbsd -s root=NAME=netbsd-root console=com$' "$work/stage/boot/boot.cfg"
cmp "$work/stage/boot/boot.cfg" "$work/stage/boot/EFI/BOOT/boot.cfg"
! grep -q "$work" "$root/var/ember/image-inputs.txt"
grep -q '^SHA256 (packages.sha256) = ' "$root/var/ember/image-inputs.txt"
grep -q '^SHA256 (fsck_ffs) = ' "$root/var/ember/image-inputs.txt"
grep -q "^fsck-source=$revision\$" "$root/var/ember/image-inputs.txt"
printf 'PASS: staging ownership, restricted traversal, devices, debugger selection, recovery and provenance\n'
printf 'Passed %s bundle cases and the image staging regression. No image built.\n' "$passed"
