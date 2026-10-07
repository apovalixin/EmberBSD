#!/bin/sh
# Origin: EmberBSD; AI-assisted whole-context EXEC production regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
SUBMIT_TEST_CFLAGS="-DEXEC_CONTRACT ${EXEC_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-submit-contract.sh"
