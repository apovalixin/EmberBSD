#!/bin/sh
# Build an exported EmberBSD tree with NetBSD 11's native toolchain.
set -eu
src=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
[ "$(uname -s)" = NetBSD ] || { echo 'native NetBSD build required' >&2; exit 1; }
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || { echo 'usage: build-kernel.sh ABSOLUTE_OUTPUT [CONFIG]' >&2; exit 1; }
out=$1
config=${2:-EMBER64}
case "$out" in /*) ;; *) echo 'output must be an absolute path' >&2; exit 1 ;; esac
case "$config" in ''|*[!A-Za-z0-9_]*) echo 'invalid kernel configuration' >&2; exit 1 ;; esac
[ -f "$src/sys/arch/evbarm/conf/$config" ] || { echo 'kernel configuration missing' >&2; exit 1; }
python=${NETBSD2_PYTHON:-/usr/pkg/bin/python3.13}
[ -x "$python" ] || { echo 'Python 3.13 from pkgsrc is required for native contracts' >&2; exit 1; }
mkdir -p "$out"
rm -rf "$src/sys/arch/evbarm/compile/$config"
cd "$src/sys/arch/evbarm/conf"
config "$config" > "$out/build.log" 2>&1
cd "../compile/$config"
jobs=${NETBSD2_JOBS:-$(sysctl -n hw.ncpu)}
make -j"$jobs" depend >> "$out/build.log" 2>&1
make -j"$jobs" >> "$out/build.log" 2>&1
cp netbsd "$out/netbsd-$config"
cp netbsd.img "$out/netbsd-$config.img"
cd "$src/sys/external/gpl2/dts/dist"
cpp -P -xassembler-with-cpp -I include -I arch/arm/boot/dts \
    arch/arm/boot/dts/bcm2837-rpi-zero-2-w.dts > "$out/zero-2.dts"
dtc -I dts -O dtb -p 1024 -b 0 -@ -o "$out/bcm2837-rpi-zero-2-w.dtb" \
    "$out/zero-2.dts" 2> "$out/zero-2-dtb.log"
for board in orangepi-zero4 orangepi-zero3w; do
    cpp -P -xassembler-with-cpp -I include -I arch/arm64/boot/dts/allwinner \
        "arch/arm64/boot/dts/allwinner/sun60i-a733-$board.dts" > "$out/$board.dts"
    dtc -I dts -O dtb -o "$out/sun60i-a733-$board.dtb" \
        "$out/$board.dts" 2> "$out/$board-dtb.log"
done
for module in if_cemac_acpi bcm2712btcom rp1wmcodec; do
    cd "$src/sys/modules/$module"
    make EMBER_KERNEL_CONFIG="$config" >> "$out/build.log" 2>&1
    cp "$module.kmod" "$out/"
done
{
    "$python" "$src/ember/tools/btuart-contract.py" "$src/sys/dev/bluetooth/btuart.c"
    "$python" "$src/ember/tools/bluetooth-uart-contract.py" "$src/sys/dev/ic/bcm2712btcom.c"
    "$python" "$src/ember/tools/bluetooth-control-contract.py" "$src/ember/tools/bluetooth-control.c"
    "$python" "$src/ember/tools/bluetooth-pair-contract.py" "$src/ember/tools/bluetooth-pair.c"
    "$python" "$src/ember/tools/bluetooth-rc-contract.py"
    "$python" "$src/ember/tools/codec-contract.py" "$src"
} > "$out/contracts.log" 2>&1
echo "kernel and three modules built: $out"
cat "$out/contracts.log"
