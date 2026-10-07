#!/bin/sh
# Origin: EmberBSD; AI-assisted actual backing lease/retirement regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BACKING_CONTRACT=1 RESOURCE_TEST_CFLAGS="-DBACKING_CONTRACT ${BACKING_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-resource-contract.sh"
