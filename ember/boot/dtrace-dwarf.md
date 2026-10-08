# Live DTrace and external DWARF

EmberBSD keeps GCC's DWARF5 default. Its CTF tools read ordinary C debug
units, standalone split objects and standard supplementary objects. The
FBT provider reads both CTF2 and CTF3, including the running GCC16 kernel's
argument types. This allows a D script to inspect kernel structures rather
than receive untyped probe arguments.

These adaptations belong to the OS repository. They preserve the original
NetBSD, elftoolchain and CDDL authorship. The new adaptations and contracts
are AI-assisted EmberBSD work; they have not been accepted upstream.

## Accepted scope

On 2026-10-08, Apple Silicon macOS cross-built the tools and modules with
GCC 16.2.0. Native execution used NetBSD 11.0/AArch64 in UTM, four CPUs and
4 GiB RAM. The VM runs the `EMBER64` kernel built from
`21cd2464c720159bec0a3ba352e4dee940a58b02`, SHA256
`d79c53a823855211c613245808c6f588d418c9e5bd83990ff3dc60cee643b54c`.
The modules use that source export and compiler with this change applied.
The FBT module includes the repaired `kern_ctf.c` reader.

| Consumer | Executed check |
| --- | --- |
| Host CTF tools | Six ordinary DWARF groups, including GCC/Clang DWARF4/5, DWARF32/64, string bounds and merging |
| Host external-DWARF reader | GCC/Clang standalone DWO32/64; supplementary strings, `ref_sup4`/`ref_sup8`, imported units, multiple supplementary CUs and inherited types |
| Native CTF tools | All eight external-object results match host output; six malformed inputs fail without changing the ELF |
| FBT decoder | ASan/UBSan, CTF2/3 widths, large type IDs, truncated records and all 20,771 types in the actual kernel CTF |
| Live DTrace | Syscall and FBT entry/return probes, profile ticks and `args[0]->l_proc->p_pid` dereferences |

The final live run reported `calls=101 returns=101 fbt=101 fbt_returns=101
typed=101 ticks=24` and exited successfully. This is a bounded VM run, not
physical-board acceptance, a soak test or coverage of every DTrace provider.
Loading a module or listing probes alone is insufficient evidence.

CTF conversion still accepts one primary compilation unit per object. It
explicitly rejects multiple skeleton CUs instead of silently losing types.
The supported external relationships are a standalone `.dwo` or a
`.debug_sup` supplement. A combined split-plus-supplement relationship,
indexed `.dwp` packages, signature-referenced type units and general DWARF
location/expression evaluation are outside this CTF acceptance.
CTF is a type consumer; it does not establish every debugger's DWARF support.

Separately, [Ports GDB 18.1](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/gdb)
passes all eight external-object cases and live DWARF32/64 debugging in
the same VM. Its native backend repairs FP register ordering and signal
unwinding; Unicode conversion and malformed supplementary metadata are
also checked. It is the installed active debugger there. The base source
import remains GDB 15.1, and release-image/pkgsrc integration of the current
Ports debugger is separate work. Neither check establishes every DWARF form.

## Build and check the host readers

Use the complete source export and external toolchain described in
[cross-build.md](cross-build.md). Preserve its source revision, compiler,
kernel configuration and output hashes. With the generated make wrapper:

```sh
mk=/absolute/output/tools/bin/nbmake-evbarm
src=/absolute/source
"$mk" -C "$src/tools/elftoolchain/libdwarf" dependall install
"$mk" -C "$src/tools/ctfconvert" dependall install
"$mk" -C "$src/tools/ctfdump" dependall install
"$mk" -C "$src/tools/ctfmerge" dependall install
sh "$src/ember/tests/ctf-dwarf.sh" /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/output/tools /absolute/new-ordinary-tests
sh "$src/ember/tests/ctf-external.sh" /absolute/cross/bin/aarch64--netbsd-gcc \
    /absolute/clang /absolute/output/tools /absolute/new-external-tests
```

Each work directory must be new. The external test uses real compiler DWO
output and the assembly fixture in `ember/tests/ctf-dwarf/supplementary.S`.
It verifies DWO identities and matching supplementary checksums. Negative
cases cover missing files, wrong identities, truncated headers/references,
cross-object offset collisions and invalid CU-relative inherited references.
Failed conversion must leave the input ELF byte-for-byte unchanged.

The [Ports cross compiler](https://github.com/oxtech-ember/EmberBSD-Ports/tree/main/profiles/development-toolchain/cross)
installs binutils in GCC's target-tool search directory. Its split-DWARF
contract runs with a clean PATH and no `-B` workaround for `objcopy` lookup.

The FBT decoder contract is independently runnable on the host:

```sh
/absolute/cross/bin/aarch64--netbsd-objcopy \
    --dump-section .SUNW_ctf=/absolute/kernel.ctf /absolute/matching/netbsd
cc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    "$src/ember/tests/fbt-ctf.c" -lz -o /absolute/fbt-ctf
/absolute/fbt-ctf /absolute/kernel.ctf
```

## Native CTF execution

Cross-build the target libraries `external/bsd/elftoolchain/lib/libelf`,
`external/bsd/elftoolchain/lib/libdwarf`, `lib/libz` and
`external/cddl/osnet/lib/libctf`, then
`external/cddl/osnet/usr.bin/ctfconvert`, with the same make wrapper.
Run `obj` before `dependall` for a newly selected directory.
Use a complete target `DESTDIR`, including base headers and startup objects.
A partial tool-only destination is insufficient.

For an isolated build against an older installed header, pass
`CPPFLAGS.dwarf.c=-I/absolute/source/external/bsd/elftoolchain/dist/libdwarf`
to the converter's make invocation. A full build installs the new header
normally. Do not replace the complete CPPFLAGS include configuration.

The new exported `dwarf_set_tied_dbg` API advances libdwarf from 2.0 to 2.1;
the ABI major remains 2. Install the matching library and converter together.
Copy the host external-test directory and run on the target:

```sh
sh ember/tests/ctf-external-target.sh /usr/bin/ctfconvert /usr/bin/ctfdump \
    /absolute/host-external-tests /absolute/new-native-tests
```

The API borrows a caller-owned supplementary context. The caller validates
file identity, keeps it alive and resolves supplementary reference offsets
in that object's namespace. See `dwarf_set_tied_dbg(3)` for its lifetime and
error contract. It does not search arbitrary files on behalf of callers.

## Live tracing in the matching VM

Build `sys/modules/solaris`, `sys/modules/cyclic` and `sys/modules/dtrace`
with `obj` followed by `dependall` through the same make wrapper. The last
directory recursively builds its providers. Keep these modules with their
matching kernel; do not force-load modules from a different kernel build.
The checked set is `solaris`, `cyclic`, `dtrace`, `dtrace_sdt`, `dtrace_fbt`,
`dtrace_lockstat`, `dtrace_profile` and `dtrace_syscall`.

Copy the eight `.kmod` files into a private test directory on the VM.
Use root in a test VM permitting module loading and DTrace. Check
`uname -a`, the kernel hash and `modstat` before changing loaded modules.
Load missing modules in the order above with `modload /absolute/name.kmod`.
Do not unload modules in use by another tracing session.

Cross-compile and copy the bounded workload to that VM:

```sh
/absolute/cross/bin/aarch64--netbsd-gcc -O2 -Wall -Wextra -Werror \
    ember/tests/dtrace-live.c -o /absolute/dtrace-live
```

Run the acceptance script using the target DTrace executable:

```sh
/usr/sbin/dtrace -q -s ember/tests/dtrace-live.d -c /absolute/dtrace-live
```

The D script checks entry/return counts and typed reads, handles DTrace
errors, and has a ten-second watchdog. The workload validates its own
syscall results. The decoded CTF extent is bounded before FBT walks its
tables; CTF3 uses 32-bit type IDs and different function-record alignment
from CTF2. Both formats remain supported.
