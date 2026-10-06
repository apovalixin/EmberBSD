# AArch64 outlined CAS argument regression

EmberBSD normalizes byte and halfword expected arguments in the NetBSD
LL/SC outlined CAS helpers. A signed narrow expected value can arrive
with nonzero upper register bits. Comparing those bits against a
zero-extended exclusive load incorrectly skips a matching store.

The owner is the OS source tree:
[`__aarch64_lse.S`](../../common/lib/libc/arch/aarch64/atomic/__aarch64_lse.S).
The change preserves NetBSD's implementation, licence and attribution;
it does not import GCC's assembly. AAPCS64 leaves unused argument bits
unspecified, and GCC's AArch64 outlined helper also normalizes narrow
expected arguments before its LL/SC comparison.

## Run the production contract

On little-endian NetBSD/EmberBSD AArch64, from a pinned source export:

```sh
CC=/usr/pkg/gcc16/bin/gcc sh ember/tools/aarch64-cas-contract.sh \
    "$PWD" /absolute/new-output-directory
```

Use an external timeout and appropriate resource guard on shared machines.
The script creates its output directory and retains generated wrappers,
objects, executable, link map, symbols, disassembly and execution status.
It exits nonzero on a failed check; do not interpret a completed diagnostic
controller as a passing regression.

The wrappers use the production Makefile's OP/SZ/AR definitions and compile
the actual source with native NetBSD headers. Only the test objects' symbol
names are changed. The one-instruction assembly call seam passes full
64-bit expected/desired values without C narrow-argument conversion.
The original named objects remain available for an isolated upstream test.

The 850 functional checks cover 1/2/4/8 bytes across relax/acq/rel/acq_rel/sync,
positive, negative and boundary bit patterns, independent upper-bit poison,
matching and mismatching expected values, truncated desired values, exact
old-value returns and adjacent-byte canaries. CAS4 must ignore high32;
CAS8 must compare them. The unchanged baseline must fail the ten narrow
variants; the repaired source must pass all twenty variants.

## Evidence and limits

The original production source at `7f47b7c3f935ccba2207fe73b57742ae4b6b9f92`
failed 140 of 850 checks in an AArch64 UTM guest with GCC 16.2.0.
The source repair requires the same native GREEN gate before acceptance.
This is an isolated software contract, not an installed libc update,
physical-board check or acceptance of the ongoing GCC test suite.

All existing acquire/release instructions, retry branches and barriers
remain unchanged. Functional coverage of the five suffixes does not prove
the complete memory model. In particular, the existing `_sync` mismatch
exit skips its trailing barrier; that separate ordering issue is not fixed.
`_HAVE_LSE` is not enabled: its dispatch predicate requires a separate audit,
and this regression does not claim LSE execution or support.
