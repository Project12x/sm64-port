# LLVM `compiler-rt` builtins — the four hot single-precision soft-float routines

This directory is a near-verbatim subset of LLVM's `compiler-rt/lib/builtins`.
It supplies `__addsf3`, `__subsf3`, `__mulsf3` and `__divsf3` for the FPU-less
SH-2. Every other soft-float entry point still comes from
`third_party/gcc-soft-fp` (GCC's `soft-fp`); both arms are compiled into the
same `libsm64softfp.a` and linked ahead of `-lgcc` by
`src/port/saturn/sourceboot/Makefile`.

**Three files carry local changes**, each marked in place with a
`LOCAL CHANGE (sm64-saturn-port, 2026-08-21)` comment that quotes the upstream
line it replaced. Everything else is byte-identical to upstream. See
"Local changes" below.

## Why it is here

W6a. `soft-fp` is GCC's *portable reference* soft-float: correct on every
target GCC supports, and structured around a class-combination dispatch
(`_FP_CLS_COMBINE`) plus macro-expanded fraction arrays. On the SH-2 — 16
registers, no barrel shifter, no conditional execution — that structure costs
register pressure, a seven-register prologue, and a long path to the first
useful arithmetic. `compiler-rt`'s float routines are written fast-path-first:
one combined test rejects every zero/subnormal/infinity/NaN case, and the
normal case is straight-line integer code.

Measured on the exact product build, `sh-elf-gcc 14.3.0 -O2 -m2 -mb`, `.text`
bytes per routine:

| routine | `soft-fp` | `compiler-rt` | change |
| :--- | ---: | ---: | ---: |
| `__addsf3` | 1512 | 652 | −57% |
| `__subsf3` | 1544 | 16 | −99% (tail-calls `__addsf3`) |
| `__mulsf3` | 792 | 508 | −36% |
| `__divsf3` | 776 | 604 | −22% |

`compiler-rt`'s `__divsf3` reaches its quotient through a Newton–Raphson
reciprocal built out of `dmulu.l` widening multiplies and does **not** call
libgcc's `__udiv_qrnnd_16`, which `soft-fp`'s `_FP_DIV_MEAT_1_udiv_norm` does
twice per divide.

A note on a premise that did **not** survive contact with the sources: the
vendored `soft-fp` does *not* leave SH-2 widening multiply unused.
`third_party/gcc-soft-fp/include/longlong.h:1117` defines `umul_ppmm` for
`__sh__` as inline `dmulu.l`, and `udiv_qrnnd`/`sub_ddmmss` likewise as SH
assembly. The mantissa multiply was already a single `dmulu.l` before this
change (verified in the shipped `id-e8720d58595d9a62` disassembly). The win
here is structural, not instruction-selection.

## Provenance

| Field | Value |
| :--- | :--- |
| Upstream repo | `https://github.com/llvm/llvm-project` |
| Pinned commit | `05486e3049ba8666287794fc7d4f9780edf702e9` |
| Verified on | 2026-08-21 (`gh api repos/llvm/llvm-project/commits/HEAD`) |
| Licence | `Apache-2.0 WITH LLVM-exception` (SPDX, stated in every file header) |
| Licence text | `LICENSE.TXT`, taken from the same commit |
| Reuse mode | direct-copy, with the three local changes below |
| Upstream path | `compiler-rt/lib/builtins/` |

Files taken, all from `compiler-rt/lib/builtins/` at that commit:

`addsf3.c`, `subsf3.c`, `mulsf3.c`, `divsf3.c`, `fp_add_impl.inc`,
`fp_mul_impl.inc`, `fp_div_impl.inc`, `fp_lib.h`, `fp_mode.h`, `int_lib.h`,
`int_types.h`, `int_endianness.h`, `int_util.h`, `int_math.h`, `assembly.h`.

Nothing else from `compiler-rt` is vendored, compiled, or linked.

## Local changes

Apache-2.0 §4(b) requires modified files to carry prominent notice. Each site
below is commented in place and quotes the upstream line.

1. **`fp_add_impl.inc`, `fp_mul_impl.inc`, `fp_div_impl.inc` — NaN result is
   canonicalised (6 sites, 2 per file).** Upstream propagates the offending
   operand's payload (`fromRep(toRep(a) | quietBit)`). GCC `soft-fp`, as
   configured by `third_party/gcc-soft-fp/config/sh/sfp-machine.h`
   (`_FP_CHOOSENAN` with `_FP_NANSIGN_S 0`, `_FP_NANFRAC_S _FP_QNANBIT_S`,
   `_FP_KEEPNANFRACP 0`), returns the canonical positive quiet NaN
   `0x7FC00000` instead. These six returns are changed to `fromRep(qnanRep)`,
   which is that exact bit pattern. **This is what makes the substitution
   bit-identical to the implementation it replaces on every input, not merely
   IEEE-754-equivalent**, and it is why `verify-softfp-bitexact` — whose
   NaN-group contract is written against `soft-fp`'s canonicalisation — runs
   unchanged over both arms. Every non-NaN result was already identical; the
   gate proves it.

2. **`fp_mode.h` — `__fe_getround` / `__fe_raise_inexact` are `static inline`
   constants.** Upstream declares them and links `fp_mode.c`, costing a real
   out-of-line call per arithmetic operation. The SH-2 has no FPU and no
   dynamic rounding mode; `soft-fp` is compiled here with a fixed
   round-to-nearest `FP_ROUNDMODE` and no exception handling. The inline
   definitions return exactly the values `fp_mode.c` returns
   (`CRT_FE_TONEAREST`, `0`) and constant-fold away. `fp_mode.c` is therefore
   not vendored.

## How the substitution is verified

`make -f Makefile.saturn.mk verify-softfp-bitexact` compiles **the same
sources the target links**, per routine, for the host and diffs every result
against the host's own IEEE-754 hardware bit for bit
(`tools/saturn/softfp_bitexact_diff_test.c`). `SOFTFP_FAST_FUNCS` in
`Makefile.saturn.mk` and in `src/port/saturn/sourceboot/Makefile` must stay
identical, or the gate stops testing what ships.

## Candidates rejected

| Candidate | Verified | Reason rejected |
| :--- | :--- | :--- |
| GCC `libgcc/config/sh/lib1funcs.S` | `gcc-mirror/gcc@9e54d865af9361413b9c84e11dddfe652cb4ca81`, 2026-08-21 | **Contains no floating-point routines at all.** Its 19 entry points are integer shifts, `__mulsi3`, the `__[su]divsi3` family, `__udiv_qrnnd_16`, `__movmem*`, `__set_fpscr` and `ic_invalidate`. GCC ships hand-written IEEE-754 assembly for arm, m68k and xtensa; **not** for SH. |
| GCC `libgcc/config/arm/ieee754-sf.S`, `config/xtensa/ieee754-sf.S` | same tree | Hand-written assembly for a different ISA (barrel shifter, conditional execution, `clz`); nothing is portable to SH-2. Worse, assembly cannot be run by `verify-softfp-bitexact`, which is a **host** differential test — replacing C with asm would forfeit the only decidable correctness gate this workstream has. Pattern-only value at most. |
| Berkeley SoftFloat (`ucb-bar/berkeley-softfloat-3`) | 2026-08-21 | Portable C like `soft-fp`, but carries runtime rounding-mode and exception-flag state that this target never varies — strictly more work per call, not less. |
| GitHub search for an SH-2/SH-4 soft-float library | 2026-08-21, `gh search repos` over "sh2 soft float", "SH-2 softfloat", "sh-elf softfloat", "sega saturn soft float" | **Zero results on every query.** No such library is published. |
