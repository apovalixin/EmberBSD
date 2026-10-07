#!/bin/sh
# Origin: EmberBSD; exercise the real release-set selector for binary128 tests.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
[ "$#" -eq 2 ] || { echo "usage: $0 source-tree new-output-directory" >&2; exit 2; }
src=$(CDPATH= cd -- "$1" && pwd)
mkdir "$2"
out=$(CDPATH= cd -- "$2" && pwd)
export EMBER_SETLIST_SOURCE="$src"
export EMBER_SETLIST_MAKE="${SETLIST_MAKE:-/usr/bin/make}"
cat > "$out/make" <<'EOF'
#!/bin/sh
exec "$EMBER_SETLIST_MAKE" -m "$EMBER_SETLIST_SOURCE/share/mk" "$@"
EOF
chmod 700 "$out/make"
check()
{
    label=$1 machine=$2 arch=$3 softfloat=$4 atf=$5 debug=$6
    want_test=$7 want_debug=$8
    env NETBSDSRCDIR="$src" MAKE="$out/make" MACHINE="$machine" \
        MACHINE_ARCH="$arch" MKCOMPAT=no MKSOFTFLOAT="$softfloat" \
        MKATF="$atf" MKDEBUG="$debug" \
        sh "$src/distrib/sets/makeflist" tests debug \
        > "$out/$label.list" 2> "$out/$label.err"
    [ ! -s "$out/$label.err" ] || { cat "$out/$label.err" >&2; exit 1; }
    got_test=$(awk '$0 == "./usr/tests/lib/libc/gen/t_comparetf2" {n++} END {print n+0}' "$out/$label.list")
    got_debug=$(awk '$0 == "./usr/libdata/debug/usr/tests/lib/libc/gen/t_comparetf2.debug" {n++} END {print n+0}' "$out/$label.list")
    printf '%s test=%s/%s debug=%s/%s\n' "$label" "$got_test" "$want_test" "$got_debug" "$want_debug"
    [ "$got_test" -eq "$want_test" ] && [ "$got_debug" -eq "$want_debug" ]
}
check aarch64 evbarm aarch64 no yes yes 1 1
check aarch64-no-debug evbarm aarch64 no yes no 1 0
check aarch64-no-atf evbarm aarch64 no no yes 0 0
check aarch64-softfloat evbarm aarch64 yes yes yes 0 0
check aarch64eb evbarm aarch64eb no yes yes 1 1
check arm evbarm earmv7hf no yes yes 0 0
check amd64 amd64 x86_64 no yes yes 0 0
