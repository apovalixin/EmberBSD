#!/bin/sh
# Origin: EmberBSD; AI-assisted classic exhaustion with real backing retirement.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BACKING_CONTRACT=1 FENCE_BACKING_CONTRACT=1 \
    RESOURCE_TEST_CFLAGS="-DBACKING_CONTRACT -DFENCE_BACKING_CONTRACT ${FENCE_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-resource-contract.sh"
