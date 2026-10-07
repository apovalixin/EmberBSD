#!/bin/sh
# Origin: EmberBSD (AI-assisted), verify modern compiler DWARF/CTF compatibility.
set -eu
[ "$#" = 4 ] || { echo "Usage: $0 NBMAKE KERNEL_OBJ TARGET_CC NEW_WORK" >&2; exit 2; }
make=$1 obj=$2 cc=$3 work=$4
mkdir "$work"
cat > "$work/types.c" <<'C'
struct ctf_record { int count; long value; };
struct ctf_record ctf_data;
C
ctf=$("$make" -C "$obj" -V '${CTFCONVERT}')
readelf=$("$make" -C "$obj" -V '${OBJDUMP:S/objdump/readelf/}')
# The baseline proves the actual converter cannot consume GCC16's default.
"$cc" -g -c "$work/types.c" -o "$work/default.o"
if "$ctf" -g -l EmberBSD "$work/default.o" > "$work/before.log" 2>&1; then
    echo 'Expected default DWARF incompatibility was not reproduced' >&2; exit 1
fi
grep -q 'incompatible version 5 DWARF' "$work/before.log"
flags=$("$make" -C "$obj" -V '${CFLAGS}')
case " $flags " in *' -gdwarf-4 '*) ;; *) echo 'Kernel is missing DWARF4 selection' >&2; exit 1 ;; esac
"$cc" -gdwarf-4 -c "$work/types.c" -o "$work/compatible.o"
"$ctf" -g -l EmberBSD "$work/compatible.o"
"$readelf" -S "$work/compatible.o" > "$work/sections.txt"
grep -q '\.SUNW_ctf' "$work/sections.txt"
echo 'PASS: GCC16 default reproduces CTF failure; selected DWARF4 produces a CTF section'
