#!/bin/sh
# Origin: EmberBSD; AI-assisted native loaded-map eligibility regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
src=$(CDPATH= cd -- "$tools/../.." && pwd)
work=$(mktemp -d "${TMPDIR:-/tmp}/virtgpu-dma.XXXXXXXX")
trap 'rm -rf "$work"' EXIT HUP INT TERM
md="$src/sys/external/bsd/drm2/virtio/virtgpu_dma_arm64.c"
extract() {
    awk -v name="$1" -v type="$2" '
        $0 ~ "^" name "[(]" { print type; copying=1 }
        copying { print }
        copying && /^}/ { found=1; exit }
        END { if (!found) exit 1 }
    ' "$md"
}
extract virtgpu_dma_tag_eligible 'static bool' > "$work/dma-production.h"
extract virtio_gpu_dma_eligible 'static int' >> "$work/dma-production.h"
# Keep the conservative other-architecture body under test too.
sed -n '/^#else/,/^#endif/p' "$md" | sed '1d;$d' |
    sed 's/^virtio_gpu_dma_eligible(/virtio_gpu_dma_unavailable(/' > "$work/dma-stub.h"
for name in arm32_bus_dma_segment arm32_dma_range arm32_bus_dma_tag arm32_bus_dmamap; do
    sed -n "/^struct $name {/,/^};/p" "$src/sys/arch/arm/include/bus_defs.h" >> "$work/dma-native-layout.h"
done
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror -Wno-unused-parameter \
    ${DMA_TEST_CFLAGS:-} -I"$work" "$tools/virtgpu-dma-fixture.c" -o "$work/test"
"$work/test"
