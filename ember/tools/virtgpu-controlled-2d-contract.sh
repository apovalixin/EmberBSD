#!/bin/sh
# Origin: EmberBSD; AI-assisted production controlled console/legacy 2D regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CONTROLLED_2D_CONTRACT=1 TRANSFER_CONTRACT=1 \
 SUBMIT_TEST_CFLAGS="-DEXEC_CONTRACT -DTRANSFER_CONTRACT -DCONTROLLED_2D_CONTRACT ${CONTROLLED_2D_TEST_CFLAGS:-}" \
 sh "$tools/virtgpu-submit-contract.sh"
BACKING_CONTRACT=1 LEGACY_BACKING_CONTRACT=1 \
 RESOURCE_TEST_CFLAGS="-DBACKING_CONTRACT -DLEGACY_BACKING_CONTRACT ${CONTROLLED_2D_TEST_CFLAGS:-}" \
 sh "$tools/virtgpu-resource-contract.sh"
