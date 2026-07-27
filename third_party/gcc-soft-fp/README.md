# GCC `soft-fp` — vendored soft-float for the FPU-less SH-2

This directory is a verbatim, unmodified subset of GCC's runtime library. It
exists so `sourceboot` can stop using libgcc's `fp-bit.c` soft-float, which
absorbs roughly 82% of SH-2 time in the current build
(`docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md`).

**Nothing here is modified.** Every file is byte-identical to its upstream
original. `src/port/saturn/sourceboot/Makefile` compiles them into
`libsm64softfp.a` with the project's own pinned `sh-elf-gcc 14.3.0` and links
that archive ahead of `-lgcc`.

## What it is and why it is here

GCC 15 switched bare-metal `sh-elf` off `fp-bit.c` and onto `soft-fp`
(commit `e95512e2d5a317e8c043f232158df4b38186e51c`, "SH: Use softfp for
sh-elf", 2024-10-10, benchmarked upstream at ~3x on Whetstone). The whole of
that change, for this target, is two things:

1. a new `libgcc/config/sh/sfp-machine.h`, and
2. one line of `libgcc/config.host` flipping `t-fdpbit` to
   `t-softfp-sfdf t-softfp`.

The `soft-fp` sources themselves were **not** touched by it, and were verified
here to be byte-identical between GCC 14.3.0 and GCC 15.2.0, file by file, for
every file vendored below. So this directory pairs GCC 14.3.0's own `soft-fp`
sources with GCC 15.2.0's `sfp-machine.h`, and the project's existing 14.3.0
compiler builds the result. No second toolchain is involved in the shipped
binary and no prebuilt object is committed.

## Provenance

| Path | Upstream file | Version | Licence |
|---|---|---|---|
| `soft-fp/*.c`, `soft-fp/*.h` | `libgcc/soft-fp/` | GCC **14.3.0** | LGPL-2.1-or-later **with the unlimited linking exception** |
| `include/longlong.h` | `include/longlong.h` | GCC **14.3.0** | LGPL-2.1-or-later with the unlimited linking exception |
| `config/sh/sfp-machine.h` | `libgcc/config/sh/sfp-machine.h` | GCC **15.2.0** | GPL-3.0-or-later **with the GCC Runtime Library Exception** |

Local source trees the files were taken from:

- GCC 14.3.0: `<repo-parent>/work/upstream/marsdev/sh-gcc-toolchain/gcc-14.3.0/`
  — the exact tree the pinned `sh-elf-gcc 14.3.0` in `work/yaul-install` was
  built from.
- GCC 15.2.0: `/d/tmp/gcc15sh/sh-gcc-toolchain/gcc-15.2.0/` — a local build of
  the same `marsdev` toolchain-builder at its current default version.

`sfp-machine.h` is taken from 15.2.0 rather than reconstructed because that
version already carries the two correctness follow-ups that landed after the
original patch: `2a643f55f5ac` ("sh: Correct NaN signalling bit and propagation
rules [PR111814]") and `05c4e3ecb54d` ("sh: libgcc: Implement fenv rounding and
exceptions for soft-fp [PR118257]").

## Licence position

Both licences carry an exception that exists precisely for this case:

- The `soft-fp` files are glibc-derived and state, in every file header, that
  "the Free Software Foundation gives you unlimited permission to link the
  compiled version of this file into combinations with other programs, and to
  distribute those combinations without any restriction coming from the use of
  this file."
- `sfp-machine.h` is under the **GCC Runtime Library Exception**, the same
  permission the project already relies on for linking libgcc itself.

Linking these therefore imposes no new obligation on the ROM beyond what
libgcc already imposed. The obligations that *do* apply — preserving the
notices and stating that the files are unmodified — are discharged by this
file and by the untouched headers. Recorded in `docs/saturn/PROVENANCE.md` and
`THIRD_PARTY_LICENSES.md`.

## Files

`soft-fp/` — 8 headers (`soft-fp.h`, `op-1.h`, `op-2.h`, `op-4.h`, `op-8.h`,
`op-common.h`, `single.h`, `double.h`) and the 36 `.c` files that
`libgcc/config/t-softfp-sfdf` plus `t-softfp` generate for
`softfp_float_modes = sf df`, `softfp_int_modes = si di`. The `_BitInt` helpers
are deliberately absent: nothing in this tree uses `_BitInt`, and libgcc's
fp-bit configuration did not provide them either.

## Verification

`make -f Makefile.saturn.mk verify-softfp-bitexact` compiles these same sources
for the host, with this same `sfp-machine.h`, and diffs every routine against
the host CPU's IEEE-754 hardware bit for bit — all 2^32 inputs for each
single-argument routine, the full special-value cross product, and >=1e7 seeded
random pairs per binary operation. See
`tools/saturn/softfp_bitexact_diff_test.c` for the two places IEEE-754 stops
specifying an answer and how each is pinned exactly rather than skipped.

`src/port/saturn/sourceboot/Makefile`'s `verify` target additionally asserts,
on the linked ELF, that no `fp-bit.c` internal symbol survives and that the
soft-float entry points are present — the substitution is a link-order effect
and would otherwise be able to regress silently.
