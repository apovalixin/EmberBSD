#!/bin/sh
# Build this fork's already-patched NetBSD encoder.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cd "$base/a2dp-source"
cc -O2 cosdata.c -lm -o cosdata-gen
./cosdata-gen > sbc_coeffs.h
cc -O2 sbc_crc.c -o sbc_crc-gen
./sbc_crc-gen > sbc_crc.h
cc -O2 -I. bta2dpd.c avdtp.c sbc_encode.c -lbluetooth -levent -lutil \
    -o "$base/bluetooth-a2dp"
/usr/pkg/bin/python3.13 "$base/bta2dpd-rtp-contract.py" "$base/bluetooth-a2dp"
