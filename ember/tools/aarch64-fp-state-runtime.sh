#!/bin/sh
# Origin: EmberBSD; AI-assisted native AArch64 FP regression runner.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/aarch64-fp-runtime.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
${CC:-cc} -std=c11 -O0 -fno-fast-math -Wall -Wextra -Werror -pthread \
    "$src/ember/tools/aarch64-fp-state-runtime.c" -o "$work/test"
failed=0
for mode in defaults preserve exec-defaults; do
    status=0
    "$work/test" "--$mode" || status=$?
    echo "$mode status: $status"
    if [ "$status" -ne 0 ]; then
        failed=1
    fi
done
exit "$failed"
