#!/bin/sh
# Origin: EmberBSD; AI-assisted production outlined CAS regression.
# SPDX-License-Identifier: BSD-2-Clause
set -eu
provider=
if [ "$#" -eq 4 ] && [ "$1" = --dso ]; then
    provider=$2
    shift 2
    case "$provider" in
        /*) ;;
        *) echo "provider must be an absolute regular file" >&2; exit 2 ;;
    esac
    if [ ! -f "$provider" ] || [ -L "$provider" ]; then
        echo "provider must be an absolute regular file" >&2
        exit 2
    fi
fi
if [ "$#" -ne 2 ]; then
    echo "usage: $0 [--dso /absolute/regular/provider.so] source-tree new-output-directory" >&2
    exit 2
fi
src=$(CDPATH= cd -- "$1" && pwd)
tools=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
mkdir "$2"
out=$(CDPATH= cd -- "$2" && pwd)
cc=${CC:-cc}
objcopy=${OBJCOPY:-objcopy}
objdump=${OBJDUMP:-objdump}
at="$src/common/lib/libc/arch/aarch64/atomic"
test "$(uname -p)" = aarch64
: > "$out/cas-variants.h"
for sz in 1 2 4 8; do
    for ar in _relax _acq _rel _acq_rel _sync; do
        name=__aarch64_cas${sz}${ar}
        printf 'CAS(%s, %s)\n' "$sz" "$ar" >> "$out/cas-variants.h"
        if [ -n "$provider" ]; then
            continue
        fi
        printf '#define OP cas\n#define OP_cas\n#define SZ %s\n#define AR %s\n#define AR%s\n#include "__aarch64_lse.S"\n' \
            "$sz" "$ar" "$ar" > "$out/$name.S"
        "$cc" -c -I"$at" "$out/$name.S" -o "$out/$name.o"
        "$objcopy" --redefine-sym "$name=ember_cas${sz}${ar}" \
            "$out/$name.o" "$out/ember_cas${sz}${ar}.o"
    done
done
"$cc" -c "$tools/aarch64-cas-call.S" -o "$out/call.o"
if [ -n "$provider" ]; then
    printf '%s\n' "$provider" > "$out/provider.txt"
    sha256 "$provider" > "$out/provider.sha256"
    readelf -h -d -Ws -r -V "$provider" > "$out/provider-elf.txt"
    "$objdump" -d "$provider" > "$out/provider-disassembly.txt"
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -DCAS_CONTRACT_DSO -I"$out" \
        "$tools/aarch64-cas-contract.c" "$out/call.o" \
        -Wl,-Map,"$out/contract.map" -o "$out/contract"
else
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -I"$out" \
        "$tools/aarch64-cas-contract.c" "$out/call.o" "$out"/ember_cas*.o \
        -Wl,-Map,"$out/contract.map" -o "$out/contract"
fi
"$objdump" -d "$out/contract" > "$out/disassembly.txt"
readelf -Ws "$out/contract" > "$out/symbols.txt"
readelf -h -d -r "$out/contract" > "$out/executable-elf.txt"
set +e
if [ -n "$provider" ]; then
    "$out/contract" "$provider" > "$out/execution.txt" 2>&1
else
    "$out/contract" > "$out/execution.txt" 2>&1
fi
status=$?
set -e
printf '%s\n' "$status" > "$out/execution.status"
cat "$out/execution.txt"
exit "$status"
