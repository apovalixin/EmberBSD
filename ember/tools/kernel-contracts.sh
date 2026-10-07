#!/bin/sh
# Existing source contracts: portable subset on the build host, all on NetBSD.
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || {
    echo 'usage: kernel-contracts.sh SOURCE_ROOT [all|portable]' >&2; exit 1;
}
src=$(CDPATH= cd -- "$1" && pwd)
mode=${2:-all}
case "$mode" in all|portable) ;; *) echo 'invalid contract mode' >&2; exit 1;; esac
python=${NETBSD2_PYTHON:-$(command -v python3 || true)}
[ -n "$python" ] && [ -x "$python" ] || {
    echo 'set NETBSD2_PYTHON to the host Python 3 executable' >&2; exit 1;
}
"$python" "$src/ember/tools/bluetooth-uart-contract.py" "$src/sys/dev/ic/bcm2712btcom.c"
"$python" "$src/ember/tools/bluetooth-rc-contract.py"
if [ "$mode" = all ]; then
    "$python" "$src/ember/tools/btuart-contract.py" "$src/sys/dev/bluetooth/btuart.c"
    "$python" "$src/ember/tools/bluetooth-control-contract.py" "$src/ember/tools/bluetooth-control.c"
    "$python" "$src/ember/tools/bluetooth-pair-contract.py" "$src/ember/tools/bluetooth-pair.c"
    "$python" "$src/ember/tools/codec-contract.py" "$src"
else
    echo 'Native contracts pending: run kernel-contracts.sh SOURCE_ROOT all on NetBSD.'
fi
