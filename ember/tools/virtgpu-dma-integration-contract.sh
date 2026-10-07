#!/bin/sh
# Origin: EmberBSD; AI-assisted production DMA admission/unwind integration.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
RESOURCE_TEST_CFLAGS="-DDMA_ELIGIBILITY_CONTRACT ${DMA_TEST_CFLAGS:-}" \
    sh "$tools/virtgpu-resource-contract.sh"
