#!/bin/sh
# Origin: EmberBSD; verify CAN FD tests through the real release-set selector.
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
    label=$1 machine=$2 arch=$3 rump=$4 atf=$5 debug=$6
    want_test=$7 want_debug=$8
    env NETBSDSRCDIR="$src" MAKE="$out/make" MACHINE="$machine" \
        MACHINE_ARCH="$arch" MKCOMPAT=no MKRUMP="$rump" \
        MKATF="$atf" MKDEBUG="$debug" \
        sh "$src/distrib/sets/makeflist" tests debug \
        > "$out/$label.list" 2> "$out/$label.err"
    [ ! -s "$out/$label.err" ] || { cat "$out/$label.err" >&2; exit 1; }
    got_test=$(awk '$0 == "./usr/tests/net/can/t_canfd" {n++} END {print n+0}' "$out/$label.list")
    got_debug=$(awk '$0 == "./usr/libdata/debug/usr/tests/net/can/t_canfd.debug" {n++} END {print n+0}' "$out/$label.list")
    printf '%s test=%s/%s debug=%s/%s\n' "$label" "$got_test" "$want_test" "$got_debug" "$want_debug"
    [ "$got_test" -eq "$want_test" ] && [ "$got_debug" -eq "$want_debug" ]
}
check native evbarm aarch64 yes yes yes 1 1
check no-debug evbarm aarch64 yes yes no 1 0
check no-atf evbarm aarch64 yes no yes 0 0
check no-rump evbarm aarch64 no yes yes 0 0
check amd64 amd64 x86_64 yes yes yes 1 1
