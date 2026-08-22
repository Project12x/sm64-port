# W6a — replace the soft-float implementation (bit-exact)

- Date: 2026-08-21. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `a583e9a9`.
- Task: replace GCC `soft-fp` with a faster implementation that is
  **bit-identical**, size the change with the exact master census before and
  after, and prove correctness with the existing gate rather than a new one.
- Baseline (owner-accepted): `id-e8720d58595d9a62`, ELF SHA-256
  `021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`,
  **6.6923 FPS mean / 6.0 1% low**, caches-off basis.
- Candidate: `id-586944bb637a920f`, ELF SHA-256
  `03e6f55e2667e7559c8c68e0862b099448a6bd41e9f46fe4f4c9308c93eca8d7`,
  **7.0445 FPS mean / 6.6667 1% low**, caches-off basis.
- **No emulator GUI.** Three headless Ymir boots (two contiguous instruction
  traces, one cadence capture), all with on-target identity **MATCH**.

---

## 0. The one-paragraph version

**+5.26% FPS, measured, with 43,333,704,485 bit-exact checks and 0 failures.**
The four single-precision routines that carry essentially all of this port's
floating point — `__addsf3`, `__subsf3`, `__mulsf3`, `__divsf3` — now come from
**LLVM `compiler-rt`** instead of GCC `soft-fp`, patched at six sites so their
NaN results are the same canonical `0x7FC00000` that `soft-fp` produced. Every
other soft-float entry point is untouched. Soft-float fell from **33.55% to
24.87%** of master cycles, from **1,345,842 to 947,679 cycles per frame
(−29.6%)**; the master frame fell from 4,011,600 to 3,811,010 cycles (−5.00%),
and mean VBlanks per frame from 8.9655 to 8.5172. Two premises in the task
brief did **not** survive contact with the code and are corrected in section 2:
`soft-fp` was already using `dmulu.l` for the mantissa multiply, and the
decisive constraint on candidate selection was not licensing but the fact that
`verify-softfp-bitexact` is a *host* test, which silently rules out assembly.
The gate's MSYS path defect was **fixed**, not worked around (section 6); the
stray `D:\d\Code\...` mirror tree it had been creating was found and is
**left in place for the owner to delete** — deleting files outside the repo is
not something this task did on its own authority.

---

## 1. The census, before — which operations actually dominate

Instrument: `tools/saturn/capture_idle_attribution.py`, one contiguous
6,000,000-master-instruction window, `--warmup-ticks 150` (T2.20's standard:
steady-state locomotion, clear of both the level-load transient and the
120-tick stationary block), `--slave-stride 16 --io-stride 20000`,
`--vblanks-per-frame 8.9655`, caches **off**. This is the exact-residency
instrument, not the burst sampler: T2.20 measured
`capture_softfloat_profile.py` at ±2.6 pp per symbol and caught it reporting
the soft-float trend with the **wrong sign**, so it is not used here.

Target `id-e8720d58595d9a62`, identity probe MATCH, master idle **0.000000**
(the master is saturated; it is the critical path). 7,281,857 master cycles
traced, 1.8152 frames equivalent.

Every soft-float symbol with non-zero residency, plus the four libgcc integer
helpers `soft-fp` imports:

| routine | cycles | cycles/frame | % of master |
| :--- | ---: | ---: | ---: |
| `__mulsf3` | 664,644 | 366,155 | 9.13% |
| `__addsf3` | 489,768 | 269,815 | 6.73% |
| `__divsf3` | 289,321 | 159,388 | 3.97% |
| `__subsf3` | 265,568 | 146,302 | 3.65% |
| `__udiv_qrnnd_16` | 141,853 | 78,147 | 1.95% |
| `__ashlsi3_r0` | 116,977 | 64,443 | 1.61% |
| `__gesf2` | 81,273 | 44,774 | 1.12% |
| `__lesf2` | 80,068 | 44,110 | 1.10% |
| `__lshrsi3_r0` | 74,015 | 40,775 | 1.02% |
| `__floatsisf` | 70,957 | 39,090 | 0.97% |
| `__clzsi2` | 53,131 | 29,270 | 0.73% |
| `__adddf3` | 30,300 | 16,692 | 0.42% |
| `__fixsfsi` | 28,452 | 15,674 | 0.39% |
| `__subdf3` | 17,402 | 9,587 | 0.24% |
| `__extendsfdf2` | 15,570 | 8,578 | 0.21% |
| `__eqsf2` | 9,529 | 5,250 | 0.13% |
| `__muldf3` | 7,568 | 4,169 | 0.10% |
| `__truncdfsf2` | 4,688 | 2,583 | 0.06% |
| `__fixunssfsi` | 928 | 511 | 0.01% |
| `__floatunsisf` | 487 | 268 | 0.01% |
| `__divdf3` | 474 | 261 | 0.01% |
| **total** | **2,442,973** | **1,345,842** | **33.55%** |

Three things this settles, and they decided the scope:

1. **Single precision is the whole story.** Every double-precision routine put
   together is 0.77% of master cycles, and `__divdf3` executes 474 cycles in
   two frames. Optimising double would have been the T2.14 mistake — a static
   count that does not run.
2. **Four routines are 23.5% of the master on their own**, and with the
   `__udiv_qrnnd_16` that only `__divsf3`/`__divdf3` call, **25.4%**. That is
   the whole addressable target; everything else in the table is under 2%.
3. `__udiv_qrnnd_16` at 1.95% is not an engine cost at all. It is the SH
   assembly long-division helper that `soft-fp`'s
   `_FP_DIV_MEAT_1_udiv_norm` calls **twice per single-precision divide**, and
   it belongs to `__divsf3`'s bill.

`__ashlsi3_r0`, `__lshrsi3_r0` and `__clzsi2` are shared between `soft-fp` and
the engine, so they are listed but not attributed.

---

## 2. Two premises that did not survive the code

Recorded because both were load-bearing in the task brief and both are wrong.

**"It leaves `dmuls.l` on the table for mantissa work."** It does not.
`third_party/gcc-soft-fp/include/longlong.h:1117` defines `umul_ppmm` for
`__sh__` as inline `dmulu.l` (unsigned widening, which is the correct
instruction for mantissas — `dmuls.l` would be wrong), and defines
`udiv_qrnnd` and `sub_ddmmss` as SH assembly too. Disassembling the shipped
`id-e8720d58595d9a62` confirms it on the object: `___mulsf3+0x102` is
`dmulu.l r8,r11`, on the fast path. The vendored `soft-fp` was already
SH-specific at the primitive level. **The available win was structural, not
instruction selection.**

**"GPL is not a barrier, so GCC's SH `lib1funcs.S` is an obvious candidate."**
The licence was never the obstacle; the file simply has no floating point in
it (section 3). The constraint that actually bound is different and was not in
the brief: **`verify-softfp-bitexact` is a *host* differential test.** It
compiles the shipped sources for the host and diffs them against the host FPU.
An SH-2 assembly implementation cannot be run by it at all. Shipping assembly
would have forfeited the one decidable gate this workstream is built around
and required designing a new oracle — precisely what the brief says not to do.
**The replacement had to stay in C for the gate to keep meaning anything.**
That single fact eliminated every hand-written-assembly candidate before
performance was even considered.

---

## 3. Candidates, verified live

`find-library`'s vetted register had no rows, as expected. All verification is
`gh api` / `gh search` on 2026-08-21.

| Candidate | Pin | Licence | Verdict |
| :--- | :--- | :--- | :--- |
| **LLVM `compiler-rt` builtins** | `llvm/llvm-project@05486e3049ba8666287794fc7d4f9780edf702e9` | **Apache-2.0 WITH LLVM-exception** | **Adopted.** Direct-copy of 15 files with 3 modified. Section 4. |
| GCC `libgcc/config/sh/lib1funcs.S` | `gcc-mirror/gcc@9e54d865af9361413b9c84e11dddfe652cb4ca81` | GPL-3.0-or-later + Runtime Library Exception | **Rejected — contains no floating point.** Fetched and enumerated: 19 entry points, all integer (`__ashiftrt_r4_*`, `__ashlsi3{,_r0}`, `__ashrsi3`, `__lshrsi3{,_r0}`, `__mulsi3`, `__[su]divsi3{,_i4,_i4i}`, `__udiv_qrnnd_16`, `__movmem*`, `__set_fpscr`, `ic_invalidate*`). Zero matches for `sf3`/`df3`/`sf2`/`df2`/`ieee` in 2,294 lines. |
| GCC `config/arm/ieee754-sf.S`, `config/xtensa/ieee754-sf.S` | same pin | GPL-3.0-or-later + RLE | **Rejected as a code source.** GCC *does* ship hand-written IEEE-754 assembly — for arm, m68k and xtensa, **not** for SH. Different ISA (barrel shifter, predication, `clz`), and assembly is untestable by the host gate (section 2). Pattern-only value. |
| Berkeley SoftFloat (`ucb-bar/berkeley-softfloat-3`) | checked 2026-08-21 | BSD-3-Clause | **Rejected.** Portable C like `soft-fp`, but carries runtime rounding-mode and exception-flag state this target never varies — strictly more work per call. |
| Any published SH-2/SH-4 soft-float library | `gh search repos` × 4 queries: "sh2 soft float", "SH-2 softfloat", "sh-elf softfloat", "sega saturn soft float" | — | **Zero results on every query.** None exists. Recorded so this search is not run again. |

**Why `compiler-rt` wins on SH-2.** `soft-fp` is GCC's portable *reference*:
correct everywhere, structured around a class-combination dispatch
(`_FP_CLS_COMBINE`) over macro-expanded fraction arrays. On a 16-register CPU
with no barrel shifter that costs register pressure, a seven-register
prologue, and a long path to the first useful arithmetic. `compiler-rt` is
written fast-path-first: **one** combined test
(`aExponent - 1U >= maxExponent - 1U || bExponent - 1U >= ...`) rejects every
zero/subnormal/infinity/NaN case, and the normal case is straight-line integer
code. Compiled with the shipped `sh-elf-gcc 14.3.0 -O2 -m2 -mb`:

| routine | `soft-fp` `.text` | `compiler-rt` `.text` | change |
| :--- | ---: | ---: | ---: |
| `__addsf3` | 1512 | 652 | −57% |
| `__subsf3` | 1544 | 16 | −99% (tail-calls `__addsf3`) |
| `__mulsf3` | 792 | 508 | −36% |
| `__divsf3` | 776 | 604 | −22% |
| total | 4624 | 1780 | −61% |

`soft-fp` compiles `subsf3.c` as a full second copy of the addition path.
`compiler-rt`'s is a 16-byte thunk that flips the sign bit and tail-calls
`__addsf3` — which is exactly IEEE-754's own definition of subtraction.
`compiler-rt`'s `__divsf3` reaches its quotient through a Newton–Raphson
reciprocal built from `dmulu.l` widening multiplies and never calls
`__udiv_qrnnd_16`.

---

## 4. What was replaced, and how it is wired

Vendored at `third_party/llvm-compiler-rt/` (README carries the full
provenance): `addsf3.c`, `subsf3.c`, `mulsf3.c`, `divsf3.c`,
`fp_{add,mul,div}_impl.inc`, `fp_lib.h`, `fp_mode.h`,
`int_{lib,types,endianness,util,math}.h`, `assembly.h`, plus `LICENSE.TXT`
from the same commit. Nothing else from `compiler-rt` is vendored, compiled or
linked.

**Three files carry local changes**, each marked in place with a
`LOCAL CHANGE (sm64-saturn-port, 2026-08-21)` comment quoting the upstream
line it replaced (Apache-2.0 §4(b)):

1. **NaN canonicalisation, 6 sites (2 each in `fp_add_impl.inc`,
   `fp_mul_impl.inc`, `fp_div_impl.inc`).** Upstream propagates the offending
   operand's payload (`fromRep(toRep(a) | quietBit)`). GCC `soft-fp`, as
   configured by `config/sh/sfp-machine.h` (`_FP_CHOOSENAN`, `_FP_NANSIGN_S 0`,
   `_FP_NANFRAC_S _FP_QNANBIT_S`, `_FP_KEEPNANFRACP 0`), returns the canonical
   positive quiet NaN `0x7FC00000`. The six returns become
   `fromRep(qnanRep)`, which is that exact pattern.
   **This is the change that makes the substitution bit-identical to the
   implementation it replaces on every input, rather than merely
   IEEE-754-equivalent**, and it is why the gate needed no new NaN contract:
   its exemption list, written against `soft-fp`'s canonicalisation, still
   describes what ships.
2. **`fp_mode.h` inlined.** Upstream declares `__fe_getround` /
   `__fe_raise_inexact` and links `fp_mode.c`, costing an out-of-line call per
   arithmetic operation. The SH-2 has no FPU and no dynamic rounding mode.
   Both become `static inline` returning exactly what `fp_mode.c` returns
   (`CRT_FE_TONEAREST`, `0`) and constant-fold away; `fp_mode.c` is not
   vendored.

**Build wiring.** `src/port/saturn/sourceboot/Makefile` gains
`SOFTFP_FAST_DIR` / `SOFTFP_FAST_FUNCS` / `SOFTFP_FAST_OBJS` and a **static
pattern rule** — which names its targets explicitly and therefore wins over
the existing implicit `soft-fp` rule for exactly those four objects. Both arms
land in the same `libsm64softfp.a`, so the delicate link-order arrangement
documented above `SH_LDFLAGS += -L$(SOFTFP_BUILD_DIR) -lsm64softfp` is
completely unchanged. `Makefile.saturn.mk` gains the identical
`SOFTFP_FAST_DIR`/`SOFTFP_FAST_FUNCS` pair, and the gate's host-compile loop
now selects source and include flags per routine, so **the gate compiles
exactly the source the target links**. The two lists must be kept in step;
both files say so.

**Verified on the linked ELF, not in source** — `--gc-sections` is live in this
build, so the symbol table is the authority:

| symbol | baseline ELF | candidate ELF |
| :--- | ---: | ---: |
| `___addsf3` | 0x5E8 (1512) | 0x280 (640) |
| `___subsf3` | 0x608 (1544) | 0x010 (16) |
| `___mulsf3` | 0x318 (792) | 0x1E0 (480) |
| `___divsf3` | 0x308 (776) | 0x248 (584) |

---

## 5. The gate result actually obtained

```
make -f Makefile.saturn.mk verify-softfp-bitexact
```

at the shipped defaults (`SOFTFP_TEST_PAIRS=12000000`, `SOFTFP_TEST_STRIDE=1`),
run to completion on the candidate source tree. Full log:
`w6a-softfp-bitexact-full.txt`.

```
self-check: reference arm (7fc00001) and soft-fp arm (7fc00000) are distinct implementations
special values: 40 x 40 single, 35 x 35 double cross products done (31525 checks so far)
exhaustive 32-bit sweeps: 4294967296 inputs x 4 float->int + 4 int->float + negsf2 + extendsfdf2 (stride 1, 42949704485 checks total)
single-precision random pairs: 12000000 (11 routines each)
double-precision random pairs: 12000000 (11 routines each)
64-bit random sweeps: 12000000 inputs (truncdfsf2, negdf2, 4 double->int, 4 int64->float)

total bit-exact checks:            43333704485
of which NaN-result cases:         97742
of which out-of-range int casts:   6287831620
failures:                          0

RESULT: soft-fp matches the host IEEE-754 reference bit for bit across every input class.
```

**43,333,704,485 checks, 0 failures** — the same volume T2.13 obtained, now
over the mixed `compiler-rt`/`soft-fp` library. 97,742 of those are
NaN-producing cases, which is exactly the class the canonicalisation patch
exists to hold. A smoke run at `SOFTFP_TEST_PAIRS=200000
SOFTFP_TEST_STRIDE=4096` (16,917,285 checks, 0 failures) was taken first,
before the product build, as the cheap red-flag check.

The gate's banner still says "soft-fp"; the wording predates this change and
was not edited, so it should be read as "the linked soft-float library".

---

## 6. The gate's path defect — **fixed**, not worked around

`Makefile.saturn.mk:1` derives `SATURN_REPO_ROOT` from `$(realpath ...)`, which
under MSYS2 Make is the MSYS spelling `/d/Code/...`. The recipe's first step
handed that to the repo's **native-Windows** venv Python:

```make
@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SOFTFP_HOST_BUILD)').mkdir(parents=True, exist_ok=True)"
```

A native Python resolves `/d/Code/...` against the current drive, so instead of
creating its build directory it created a mirror tree at `D:\d\Code\...`. The
target then died, because the directory it needed still did not exist.

**Fix:** the step is now `@mkdir -p "$(SOFTFP_HOST_BUILD)"`. The recipe shell is
the MSYS shell, which understands both spellings. Nothing else in the recipe
needed changing, and the comment block above it says why: every *other* path in
the recipe reaches a native mingw64 `gcc` through MSYS2's argv path conversion,
which is why only the Python step was ever wrong. The target now runs
end-to-end from a single `make` invocation — both the smoke and the full run
above were obtained that way, not by hand.

**The litter it left is still on disk and was not deleted.** `D:\d\Code\...`
holds 27 directories mirroring four worktrees and 6 stray `.bin` files from
`sh2-native-math-purge`'s host tests. It is outside every repository, so this
task reports it rather than removing it; nothing recreates it now.

**A sibling instance of the same defect exists and was not fixed here.**
`verify-bob-bsp-header` (`Makefile.saturn.mk:727`) runs its built `.exe`
through `python -c "subprocess.run([r'$(SATURN_REPO_ROOT)/...exe'])"` and fails
with `FileNotFoundError: [WinError 2]` for exactly the same reason. It is in a
target W6a does not touch and is independent of the soft-float change; it is
filed as a follow-up rather than folded in.

---

## 7. Gates

| Gate | Result |
| :--- | :--- |
| `verify-softfp-bitexact` (full, 43.3 G checks) | **PASS, 0 failures** |
| `verify-render-native-math` | **PASS** |
| `verify-render-native-math-mutation` | **PASS** (mutation caught) |
| `verify-memory-map` | **RESULT OK** — `___end = 0x060FAA68`, `hwram_remaining = 0x5598` (required ≥ `0x1F00`), `lwram_remaining = 0x16A20` (floor ≥ `0x4000`) |
| `verify-shadow-trig`, `-mutation` | PASS |
| `verify-runtime-contracts` | PASS |
| `verify-terrain-clip` | PASS |
| `verify-ztreme-frustum` | PASS |
| `verify-frame-pipeline` | PASS |
| `verify-vdp1-frame-bank` | PASS |
| `verify-vdp1-user-clip`, `-mutation` | PASS |
| `verify-gouraud-transfer` | PASS |
| `verify-ir-transform` | PASS |
| `verify-hot-promotion` | PASS |
| `verify-saturn-object-pool-probe-contract` | PASS |
| `verify-bob-bsp-header` | **FAIL** — MSYS path defect, section 6. Not a soft-float regression; the target does not reach any soft-float code. |

Not run, known pre-existing failures per the standing list:
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`.

---

## 8. The census, after — and the measured FPS

Same instrument, same warm-up tick, same window size, caches off,
`--vblanks-per-frame 8.5172` (the candidate's own measured cadence — mixing
that constant across bases is the one easy way to produce a wrong number
here). Target `id-586944bb637a920f`, identity probe MATCH.

| routine | before /frame | after /frame | change |
| :--- | ---: | ---: | ---: |
| `__mulsf3` | 366,155 | 209,343 | **−42.8%** |
| `__addsf3` | 269,815 | 316,906 | +17.5% |
| `__subsf3` | 146,302 | 5,683 | −96.1% |
| — add + sub together | 416,117 | 322,589 | **−22.5%** |
| `__divsf3` | 159,388 | 133,048 | −16.5% |
| `__udiv_qrnnd_16` | 78,147 | 144 | −99.8% |
| — divide together | 237,535 | 133,192 | **−43.9%** |
| **four replaced routines + `__udiv_qrnnd_16`** | **1,019,807** | **665,124** | **−34.8%** |
| every other soft-float routine | 326,035 | 282,555 | −13.3% |
| **soft-float total** | **1,345,842** | **947,679** | **−29.6%** |
| soft-float share of master | 33.55% | 24.87% | −8.68 pp |

The "every other routine" line is **not** claimed as a win: those routines were
not touched, and most of that −13.3% is the per-frame normalisation (the
candidate window covers 2.0059 frames against the baseline's 1.8152). The
shared integer helpers `__ashlsi3_r0`, `__lshrsi3_r0` and `__clzsi2` do fall a
little in absolute terms because the replaced routines call them less, but the
line is reported for completeness, not credited.

`__addsf3` rising is not a regression: `compiler-rt`'s `__subsf3` is a thunk
that tail-calls it, so every subtraction's work is now billed to `__addsf3`.
The honest figure is the combined line, −22.5%.

`__udiv_qrnnd_16` falling to 144 cycles per frame — from 78,147 — is the
Newton–Raphson divide taking software long division out of the frame
completely. The 144 that remain belong to `__divdf3`.

**Sanity check that the two windows observe the same work:** every soft-float
routine that was *not* replaced reports an unchanged absolute cycle count
across the two traces — `__subdf3` 17,402 both times, `__extendsfdf2` 15,570,
`__truncdfsf2` 4,688, `__fixunssfsi` 928, `__floatunsisf` 487, `__divdf3` 474.
The scene is identical; only the replaced routines moved.

### Frame cost and FPS

| | baseline `id-e8720d58595d9a62` | candidate `id-586944bb637a920f` |
| :--- | ---: | ---: |
| **FPS mean** (`summarize_cadence`, 30 events / 29 intervals) | **6.6923** | **7.0445** |
| **FPS 1% low** | **6.0** | **6.6667** |
| FPS median | 6.6667 | 6.6667 |
| mean VBlanks/frame | 8.9655 | 8.5172 |
| VBlank delta range | 8–9 | 8–9 |
| cadence-unstable retries | 1 | 0 |
| master cycles/frame | 4,011,600 | 3,811,010 |
| master idle share | 0.000000 | 0.052248 |
| slave idle share | 0.638758 | 0.674754 |

**+5.26% mean FPS. +11.1% on the 1% low.** Both from `summarize_cadence`, both
caches-off; no caches-on figure is reported, and the two bases are never mixed.

Two things in that table are worth more than the headline:

- **The master stopped being saturated.** Master idle was
  *exactly* 0.000000 on the baseline — T2.17's collapse — and is now 5.22%,
  with five VBlank-spin excursions attributed to `_main+0xe14`. The arithmetic
  closes exactly: soft-float shed **398,163** cycles/frame, the frame itself
  shortened by only **200,589**, and the **197,574**-cycle residual is
  independently accounted for by the master's new idle — 5.2248% of a
  3,810,963-cycle frame is **199,115** cycles, agreeing to within 0.8%. **The
  master converted almost exactly half its saving into waiting**, because the
  frame is quantised to VBlank boundaries and the master now reaches that
  boundary with slack. **Roughly half of any further soft-float win would be
  absorbed the same way unless whatever the master is now waiting for moves
  too.** That is the single most useful thing this task learned about what to
  do next.
- The cadence became *more* stable: 1 unstable retry on the baseline, 0 here,
  and the 1% low moved from 6.0 to 6.6667 — the slow frames got faster by more
  than the average did.

---

## 9. Identity and hashes

| | baseline | candidate |
| :--- | :--- | :--- |
| identity | `id-e8720d58595d9a62` | `id-586944bb637a920f` |
| `effective_config_sha256` | `e8720d58595d9a62…eb717` | `586944bb637a920f…9f557` |
| ELF SHA-256 | `021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50` | `03e6f55e2667e7559c8c68e0862b099448a6bd41e9f46fe4f4c9308c93eca8d7` |
| ELF size | 9,992,968 | 9,983,384 (−9,584) |
| ISO SHA-256 | `186f8734944941a6fe2f9f2455483871007e8f11a13a7e86966235be13729846` | `d956f7278e1fc84162d2fc70388694bb9ffa9c2f7ed978d650e7a941c9f3dce0` |
| ISO size | 5,179,392 | 5,177,344 (−2,048) |
| CUE SHA-256 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | *same 88-byte CUE* — never an identity on its own |
| manifest SHA-256 | `fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990` | `e94d56837101ad616ca4b278727f36f7fdc0455dcf1b1ee9fc9119d2cf405ac0` |
| `source_closure_sha256` | `f32ac50274…535e` | `d2f8abcb29…fde5f` |
| executable-window probe | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` | **identical**, MATCH on all three captures |
| `provenance.closure_clean` | `true` | **`false`** |

**`closure_clean: false` is stated rather than hidden.** The candidate was
built from the worktree while the W6a changes were uncommitted, which is what
that flag records. It does not affect the measurement — the ELF is sealed,
hashed and identity-probed, and all three captures bound to it — but a
reproduction build after the commit will seal `true`, and any promotion of
this artifact to accepted baseline should use that rebuild.

Staged at `releases/2026-08-21_w6a-product/id-586944bb637a920f/`. The accepted
baseline was preserved to `releases/2026-08-21_2054_w6a-pre-build/` before the
build, per the preservation rule.

Build: 27-variable product tuple, `SATURN_DIAGNOSTIC_MODE=0`, `-j10` through
`tools/saturn/with-msys-toolchain.ps1`. `-j1` was not used and is not required
(AGENTS.md, T2.18).

---

## 10. Evidence

| File | What |
| :--- | :--- |
| `w6a-idle-before-tick150-e8720d58.json` | exact master census, baseline, tick 150, caches off |
| `w6a-idle-after-tick150-586944bb.json` | exact master census, candidate, tick 150, caches off |
| `w6a-throughput-after-30events-586944bb.json` | candidate cadence, 30 presentation events |
| `w6a-softfp-bitexact-full.txt` | the 43.3 G-check gate run |
| `w0-vdp1-overwrite-fence-throughput.json` | baseline cadence (pre-existing, unchanged) |

Reproduce (PowerShell for Make; `$env:YAUL_INSTALL_ROOT` must be the Windows
spelling so the wrapper builds `PATH`, while Make gets the MSYS spelling):

```powershell
$env:YAUL_INSTALL_ROOT='D:\Code\RetroDev\sm64-saturn-port\work\yaul-install'
.\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-softfp-bitexact
```

```sh
# census, both arms; --vblanks-per-frame MUST match the arm's own cadence
python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$G/sm64-saturn-sourceboot-e2.cue" --elf "$G/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 150 --windows 1 --window-steps 6000000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame <8.9655|8.5172> \
  --timeout 9000 --output <out>.json
```

`$G` must be **absolute**: `YmirClient` sets `cwd=game.parent` and then passes
the same string to Ymir, so a relative CUE path is resolved twice and the boot
fails with `Ymir closed its protocol stream`. That cost one run here.

---

## 11. What remains

- **Promote or reject.** This is an evidence artifact, not an accepted
  baseline. `id-586944bb637a920f` needs an owner-observed CUE before it
  replaces `id-e8720d58595d9a62`, per AGENTS.md's product gate. A clean-closure
  rebuild after the commit should be what is promoted.
- **The master is no longer saturated.** 5.22% master idle at
  `_main+0xe14` is new. Before spending more on soft-float, find out what the
  master is now waiting for — roughly half of any further arithmetic saving is
  currently being absorbed by that wait.
- **Two-thirds of soft-float is still there.** 947,679 cycles/frame, 24.87% of
  the master. `__addsf3` alone is 8.32%. A further pass on the same four
  routines is possible but is now the *harder* half; W6b (Q16 the simulation)
  attacks the call sites instead and is the better next lever on this code.
- **Double precision remains `soft-fp` and should stay there.** 0.77% of the
  master; replacing it would be measurement-free work.
- **`verify-bob-bsp-header`** carries the same MSYS path defect this task fixed
  in `verify-softfp-bitexact`; filed as a follow-up.
- **Caches-on was not measured.** Soft-float is master-local arithmetic so it
  is not required, and reporting one basis avoids the mixing error. If a
  caches-on figure is wanted later, both arms must be re-measured on that
  basis — the baseline caches-on number in the sprint record belongs to a
  different build.
