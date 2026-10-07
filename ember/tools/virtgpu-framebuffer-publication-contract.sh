#!/bin/sh
# Origin: EmberBSD; AI-assisted production GETFB/core publication regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
FRAMEBUFFER_PUBLICATION_CONTRACT=1 \
 RESOURCE_TEST_CFLAGS="-DFRAMEBUFFER_PUBLICATION_CONTRACT ${PUBLICATION_TEST_CFLAGS:-}" \
 sh "$tools/virtgpu-resource-contract.sh"
