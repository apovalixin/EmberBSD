#!/bin/sh
# Origin: EmberBSD CAN FD native rump validation, developed with AI assistance.
# SPDX-License-Identifier: BSD-2-Clause
#
# Build the checkout's CAN component and run its ATF socket tests without
# installing libraries, changing host headers, or booting a new kernel.
# Usage: sh ember/tools/canfd-rump-check.sh /absolute/src /new/absolute/work
# Requires matching NetBSD 11 userland, compiler tools, rump libraries and ATF.

set -eu
umask 022
LC_ALL=C
export LC_ALL

fail()
{
	printf '%s\n' "$*" >&2
	exit 1
}

[ "$#" -eq 2 ] || fail "usage: $0 /absolute/src /new/absolute/work"
src=$1
work=$2
case "$src:$work" in
*[[:space:]]*) fail "source and work paths must not contain whitespace" ;;
esac
case "$src" in /*) ;; *) fail "source path must be absolute" ;; esac
case "$work" in /*) ;; *) fail "work path must be absolute" ;; esac
src=$(cd "$src" && pwd -P)
[ ! -e "$work" ] && [ ! -L "$work" ] || fail "work path already exists: $work"
[ "$(uname -s)" = NetBSD ] || fail "run this check natively on NetBSD 11"
case "$(uname -r)" in 11.*) ;; *) fail "matching NetBSD 11 userland is required" ;; esac

for tool in make gcc config nm objcopy atf-run atf-report ldd sha256 realpath; do
	command -v "$tool" >/dev/null || fail "missing tool: $tool"
done
for file in sys/netcan/can.h sys/rump/Makefile.rump \
    sys/rump/net/lib/libnetcan/Makefile tests/net/can/Makefile \
    sbin/canconfig/canconfig.c sbin/canconfig/tests/fixture.c \
    sbin/canconfig/tests/check.sh; do
	[ -f "$src/$file" ] || fail "missing source file: $file"
done

mkdir "$work"
work=$(cd "$work" && pwd -P)
mkdir "$work/lib" "$work/tests" "$work/include" "$work/include/netcan" \
    "$work/tmp" "$work/cli"
cp "$src"/sys/netcan/*.h "$work/include/netcan/"
TMPDIR=$work/tmp
export TMPDIR
unset MAKEOBJDIRPREFIX LD_PRELOAD LD_LIBRARY_PATH

{
	date -u '+UTC %Y-%m-%dT%H:%M:%SZ'
	uname -srvm
	gcc --version
	printf 'source=%s\nwork=%s\n' "$src" "$work"
	if command -v git >/dev/null && git -C "$src" rev-parse HEAD 2>/dev/null; then
		git -C "$src" status --short
	fi
} > "$work/environment.log"

# Preserve exactly the public CAN headers used by the test programs. Other
# userland headers and the remaining rump components come from this host.
find "$src/sys/netcan" "$src/sys/rump/net/lib/libnetcan" \
    "$src/tests/net/can" -type f -print | sort |
    while IFS= read -r file; do sha256 "$file"; done > "$work/source.sha256"
sha256 "$src/sys/rump/Makefile.rump" "$src/sys/rump/ldscript.rump" \
    "$src/sys/rump/net/lib/Makefile.inc" "$src/tests/Makefile.inc" \
    "$src/tests/net/Makefile.inc" "$src/tests/h_macros.h" \
    "$src/sbin/canconfig/canconfig.c" \
    "$src/sbin/canconfig/tests/fixture.c" "$src/sbin/canconfig/tests/check.sh" \
    "$src/ember/tools/canfd-rump-check.sh" \
    >> "$work/source.sha256"

build()
{
	buildsrc=$1
	buildobj=$2
	shift 2
	(
		cd "$buildsrc"
		export MAKEOBJDIR=$buildobj
		actual=$(make USETOOLS=no NETBSDSRCDIR="$src" -V .OBJDIR)
		[ "$actual" = "$buildobj" ] || fail "make selected unexpected object directory: $actual"
		nice -n 10 make -j1 USETOOLS=no NETBSDSRCDIR="$src" \
		    MKDEBUGLIB=no MKPROFILE=no MKLINT=no MKMAN=no "$@" dependall
	)
}

printf '%s\n' 'Building private rump CAN component...'
if ! build "$src/sys/rump/net/lib/libnetcan" "$work/lib" \
    > "$work/library-build.log" 2>&1; then
	tail -60 "$work/library-build.log" >&2
	fail "CAN component build failed; see $work/library-build.log"
fi

printf '%s\n' 'Building native ATF CAN tests...'
if ! build "$src/tests/net/can" "$work/tests" \
    CPPFLAGS="-I$work/include -I$src/tests -D_KERNTYPES" \
    LDFLAGS="-L$work/lib -Wl,-rpath,$work/lib" \
    > "$work/test-build.log" 2>&1; then
	tail -60 "$work/test-build.log" >&2
	fail "ATF build failed; see $work/test-build.log"
fi

printf '%s\n' 'Building and checking canconfig command handling...'
if ! (
	nice -n 10 gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	    -Werror -I"$work/include" "$src/sbin/canconfig/canconfig.c" \
	    -o "$work/cli/canconfig" || exit 1
	nice -n 10 gcc -std=gnu11 -O2 -Wall -Wextra -Wno-unused-parameter \
	    -Werror -I"$work/include" "$src/sbin/canconfig/tests/fixture.c" \
	    -o "$work/cli/canconfig-fixture" || exit 1
	sh "$src/sbin/canconfig/tests/check.sh" "$work/cli/canconfig-fixture" \
	    "$work/cli/results"
) > "$work/cli.log" 2>&1; then
	cat "$work/cli.log" >&2
	fail "canconfig check failed; see $work/cli.log"
fi
cat "$work/cli.log"

# Hash the compiler's actual kernel-header dependencies, including machine
# symlinks, rather than only the directly edited files.
(
	cd "$work/lib"
	awk '{ for (i = 1; i <= NF; i++)
	    if ($i != "\\" && $i !~ /:$/) print $i }' ./*.d |
	    while IFS= read -r file; do
		[ ! -f "$file" ] || realpath "$file"
	    done | sort -u |
	    while IFS= read -r file; do sha256 "$file"; done
) > "$work/kernel-dependencies.sha256"

# LD_LIBRARY_PATH also reaches children launched by ATF. Check each binary
# before execution so a successful run cannot silently test the installed CAN.
LD_LIBRARY_PATH=$work/lib
export LD_LIBRARY_PATH
cd "$work/tests"
for test in t_can t_canfilter t_canfd; do
	[ -x "$test" ] || fail "missing required test binary: $test"
	ldd "./$test" > "$work/$test.ldd"
	grep -F "$work/lib/librumpnet_netcan.so" "$work/$test.ldd" \
	    >/dev/null || fail "$test did not select the private CAN component"
	sha256 "$test" >> "$work/output.sha256"
done
sha256 "$work/lib/librumpnet_netcan.so.0.0" >> "$work/output.sha256"
sha256 "$work/cli/canconfig" "$work/cli/canconfig-fixture" \
    >> "$work/output.sha256"
awk '$2 == "=>" { print $3 }' "$work"/*.ldd | sort -u |
    while IFS= read -r file; do sha256 "$file"; done > "$work/runtime.sha256"

printf '%s\n' 'Running CAN, filters and CAN FD tests...'
result=0
nice -n 10 atf-run > "$work/atf.log" 2>&1 || result=$?
atf-report -o ticker:- < "$work/atf.log" > "$work/report.log"
cat "$work/report.log"
[ "$result" -eq 0 ] || fail "ATF failed; see $work/atf.log"
printf 'PASS: native rump CAN tests; evidence: %s\n' "$work"
printf '%s\n' 'This checks the software stack, not a booted kernel or physical CAN bus.'
