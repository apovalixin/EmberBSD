#!/bin/sh
# Origin: EmberBSD; AI-assisted explicit 3D transfer and WAIT regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TRANSFER_CONTRACT=1 SUBMIT_TEST_CFLAGS="-DEXEC_CONTRACT -DTRANSFER_CONTRACT ${TRANSFER_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-submit-contract.sh"
