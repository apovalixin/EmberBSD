#!/bin/sh
# Origin: EmberBSD; AI-assisted production canconfig CLI checks.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
LC_ALL=C
export LC_ALL
[ "$#" -eq 2 ] || { echo 'Usage: sh check.sh ABS_FIXTURE ABS_NEW_RESULTS' >&2; exit 2; }
fixture=$1
results=$2
case "$fixture:$results" in /*:/*) ;; *) exit 2 ;; esac
[ -x "$fixture" ] || exit 2
mkdir "$results"
# Keep caller fixture variables from changing test preconditions.
unset FIXTURE_MODE FIXTURE_FLAGS FIXTURE_KIND FIXTURE_FAIL_SET
unset EXPECT_MODE EXPECT_FLAGS EXPECT_BRP EXPECT_PROP EXPECT_PS1 EXPECT_PS2 EXPECT_SJW
pass()
{
	name=$1; shift
	if "$@" >"$results/$name.out" 2>"$results/$name.err"; then
		return
	else
		status=$?
	fi
	cat "$results/$name.err" >&2
	echo "Failed: $name" >&2
	exit "$status"
}
fail()
{
	name=$1; pattern=$2; shift 2
	if "$@" >"$results/$name.out" 2>"$results/$name.err"; then
		echo "Unexpected success: $name" >&2; exit 1
	fi
	grep -q "$pattern" "$results/$name.err"
}
pass virtual "$fixture" canlo0
grep -q 'mtu 16' "$results/virtual.out"
grep -q 'no physical bit timing' "$results/virtual.out"
grep -q 'capabilities=10<FD>' "$results/virtual.out"
if grep -Eq 'clock |operational timings|time quanta' "$results/virtual.out"; then exit 1; fi
pass fd-status env FIXTURE_MODE=16 "$fixture" canlo0
grep -q 'mtu 72' "$results/fd-status.out"
grep -q 'mode=10<FD>' "$results/fd-status.out"
pass physical env FIXTURE_KIND=physical "$fixture" canlo0
grep -q '8 time quanta of 125ns' "$results/physical.out"
pass zero-brp env FIXTURE_KIND=zero-brp "$fixture" canlo0
if grep -q 'time quanta' "$results/zero-brp.out"; then exit 1; fi
pass fd-on env EXPECT_MODE=16 EXPECT_FLAGS=0 "$fixture" canlo0 fd
pass fd-off env FIXTURE_MODE=16 EXPECT_MODE=0 "$fixture" canlo0 -fd
pass up env EXPECT_FLAGS=1 "$fixture" canlo0 up
pass down env FIXTURE_FLAGS=1 EXPECT_FLAGS=0 "$fixture" canlo0 down
pass classic-on env FIXTURE_KIND=physical EXPECT_MODE=7 "$fixture" canlo0 loopback listenonly 3samples
pass classic-off env FIXTURE_KIND=physical FIXTURE_MODE=7 EXPECT_MODE=0 "$fixture" canlo0 -loopback -listenonly -3samples
pass timing env FIXTURE_KIND=physical EXPECT_FLAGS=1 EXPECT_BRP=4 EXPECT_PROP=2 EXPECT_PS1=3 EXPECT_PS2=2 EXPECT_SJW=1 "$fixture" canlo0 brp 4 prop_seg 2 phase_seg1 3 phase_seg2 2 sjw 1 up
fail active-fd 'down.*before changing CAN FD' env FIXTURE_FLAGS=1 "$fixture" canlo0 fd
fail deferred-down 'down.*before changing CAN FD' env FIXTURE_FLAGS=1 "$fixture" canlo0 down fd
fail unsupported 'does not support CAN FD' env FIXTURE_KIND=physical "$fixture" canlo0 fd
fail ioctl-failure 'fd: Input/output error' env FIXTURE_FAIL_SET=1 "$fixture" canlo0 fd
fail virtual-timing 'no physical bit timing' "$fixture" canlo0 brp 0
fail invalid-timing 'out of range value' env FIXTURE_KIND=physical "$fixture" canlo0 brp 0
fail missing-argument 'requires 1 argument' env FIXTURE_KIND=physical "$fixture" canlo0 brp
fail unknown-command 'unknown command' "$fixture" canlo0 invalid
pass all "$fixture" -a
grep -q 'canlo0:.*mtu 16' "$results/all.out"
echo 'PASS canconfig: 20 production CLI fixtures (mode/status/timing/classic controls/errors)'
