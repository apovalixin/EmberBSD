#!/bin/sh
# Compile with the release's native headers and libbluetooth in NetBSD.
set -eu
base=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
cc -O2 -Wall -Wextra -Werror -o "$base/bluetooth-control" \
    "$base/bluetooth-control.c" -lbluetooth
cc -O2 -Wall -Wextra -Werror -o "$base/bluetooth-pair" \
    "$base/bluetooth-pair.c" -lbluetooth
