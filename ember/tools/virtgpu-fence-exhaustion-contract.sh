#!/bin/sh
# Origin: EmberBSD; AI-assisted classic fence range/retirement regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
COMPLETION_CONTRACT=1 FENCE_CONTRACT=1 \
    SUBMIT_TEST_CFLAGS="-DCOMPLETION_CONTRACT -DEXEC_CONTRACT -DFENCE_CONTRACT ${FENCE_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-submit-contract.sh"
