#!/bin/sh
# Origin: EmberBSD; AI-assisted exact-cookie completion/reset regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
COMPLETION_CONTRACT=1 SUBMIT_TEST_CFLAGS="-DCOMPLETION_CONTRACT ${COMPLETION_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-submit-contract.sh"
