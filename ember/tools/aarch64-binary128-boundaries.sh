#!/bin/sh
# Origin: EmberBSD; AI-assisted fail-closed binary128 opt-in/extraction checks.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
contract=$src/ember/tools/aarch64-binary128-contract.sh
production=$src/sys/external/bsd/compiler_rt/dist/lib/builtins/comparetf2.c
work=$(mktemp -d "${TMPDIR:-/tmp}/binary128-boundaries.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM

# The ungated source retains its old quiet policy, including signaling NaNs.
BINARY128_POLICY_FLAGS=-DTEST_LEGACY_POLICY sh "$contract"
for forbidden in _KERNEL _STANDALONE __SOFT_FP__ __SOFTFP__ \
    TEST_NON_NETBSD TEST_NON_AARCH64 TEST_NO_DOUBLE; do
    if BINARY128_POLICY_FLAGS="-DCOMPILER_RT_NETBSD_AARCH64_FENV -D$forbidden" \
        sh "$contract" > "$work/$forbidden.log" 2>&1; then
        echo "FAIL: accepted forbidden opt-in $forbidden" >&2
        exit 1
    fi
    if ! grep -q 'binary128 FENV requires' "$work/$forbidden.log"; then
        cat "$work/$forbidden.log" >&2
        exit 1
    fi
done

# Malformed extraction must fail before a partial comparator can report PASS.
sed '/^#include "fp_lib.h"$/d' "$production" > "$work/no-header.c"
sed '/^\/\* EMBER_BINARY128_INVALID_END \*\/$/d' "$production" > "$work/no-end.c"
cat "$production" "$production" > "$work/duplicate.c"
for malformed in no-header no-end duplicate; do
    status=0
    BINARY128_SOURCE="$work/$malformed.c" sh "$contract" \
        > "$work/$malformed.log" 2>&1 || status=$?
    if [ "$status" -ne 2 ]; then
        echo "FAIL: malformed $malformed extraction status=$status, expected 2" >&2
        cat "$work/$malformed.log" >&2
        exit 1
    fi
done
echo 'PASS: ungated policy, seven forbidden opt-ins, three extraction failures'
