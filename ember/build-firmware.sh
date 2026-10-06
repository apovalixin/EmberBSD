#!/usr/bin/env bash
# Build the pinned eotics UEFI with this fork's ordered hardware diffs.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
[[ $# -ge 1 && $# -le 2 ]] || { echo 'usage: build-firmware.sh CACHE [--rp1-console|--rp1-trace]' >&2; exit 1; }
CACHE="$1"
out="$CACHE/firmware"
flags='--model 5'
case "${2:-}" in
    '') ;;
    --rp1-console)
        out="$CACHE/firmware-rp1-console"
        flags="--model 5 --tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE'" ;;
    --rp1-trace)
        out="$CACHE/firmware-rp1-trace"
        flags="--model 5 --tfa-flags LOG_LEVEL=0 --edk2-flags '-D RP1_UART_CONSOLE=TRUE -D CM5_BOOT_TRACE=TRUE'" ;;
    *) echo 'unknown firmware option' >&2; exit 1 ;;
esac
mkdir -p "$out"
out="$(cd "$out" && pwd)"
# A failed rebuild must make the previous binary unusable as an image input.
rm -f "$out/RPI_EFI.fd" "$out/RPI_EFI.fd.incoming"
series="$ROOT/ember/firmware/series"
[[ "$(sort "$series")" == "$(cd "$ROOT/ember/firmware" && ls ./*.diff | sed 's|^\./||' | sort)" ]] \
    || { echo 'firmware series must name every diff once' >&2; exit 1; }
docker info >/dev/null 2>&1
docker run --rm -v "$ROOT/ember/firmware:/diffs:ro" -v "$out:/out" \
    ubuntu:24.04 bash -c "set -e
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null
    apt-get install -y -qq build-essential git python3 python-is-python3 uuid-dev \
        acpica-tools device-tree-compiler python3-pyelftools ca-certificates >/dev/null 2>&1
    git clone -q https://github.com/eotics-com/rpi5-uefi.git /w
    cd /w && git checkout -q 246e70acd939d62b5a79f895984afd0a93bd83e9
    git submodule update -q --init --depth 1
    git -C edk2 submodule update -q --init --depth 1 --recursive
    for d in \$(cat /diffs/series); do patch -s -p1 -F0 -d edk2-platforms < \"/diffs/\$d\"; done
    ./build.sh $flags > /out/build.log 2>&1
    cp RPI_EFI.fd /out/RPI_EFI.fd.incoming"
mv "$out/RPI_EFI.fd.incoming" "$out/RPI_EFI.fd"
echo "firmware: $out/RPI_EFI.fd"
