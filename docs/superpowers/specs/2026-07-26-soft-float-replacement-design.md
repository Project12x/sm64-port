# Soft-float replacement: design for the sourceboot arithmetic path

**Status:** Design recommendation, pending owner acceptance.
**Date:** 2026-07-26
**Branch at time of writing:** `saturn/bootstrap` @ `02dc28d`
**Scope:** How the sourceboot target stops spending ~82% of its SH-2 time inside
libgcc's generic C soft-float (`fp-bit.c`).
**Depends on and does not relitigate:**
[`docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md`](../../saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md)
(commit `02dc28d`).

**Investigation and design only.** No source file was modified in producing this
note. No build was run and no capture was taken. Every figure below is either
(a) **measured** by static analysis of the committed ELF
`build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf`
(md5 `8305522447a477d1717772e9fdedf57c`) and committed capture JSON,
(b) **derived** by arithmetic from measured quantities, or (c) an explicitly
labelled **guess**.

---

## 0. Summary

**Recommendation: replace libgcc's `fp-bit.c` soft-float with GCC's `soft-fp`
soft-float — which is already built, for this exact target and ISA, in the
GCC 15.2.0 sh-elf toolchain sitting on this machine — and separately fix the
port's own `sqrtf`. Do not attempt a general fixed-point conversion of SM64's
simulation.**

The single strongest reason: **the fix requires no engine changes, no semantic
change, no differential-test risk, and no toolchain build.** GCC 15 switched the
SH port off `fp-bit.c` (commit `e95512e2d5a3`, 2024-10-10, benchmarked upstream
at **~3× on Whetstone**). The replacement objects exist at
`/d/tmp/gcc15sh/sh-gcc-toolchain/work/lib/gcc/sh-elf/15.2.0/libgcc.a`, are
flagged `sh2` big-endian (§5.1), and every helper symbol they import
(`__clzsi2`, `__ashlsi3_r0`, `__lshrsi3_r0`, `__udiv_qrnnd_16`) is *already
defined* in the 14.3.0 libgcc the project links today. This is a link-order
change, not a rewrite. **GCC 14.3.0 has no faster path that is merely
unselected** — `libgcc/config/sh/lib1funcs.S` contains zero float routines and
there is no SH `sfp-machine.h` on that branch (§5.1), which answers the
diagnosis's open question §7.3.

**And it must be said before anything else: this fix does not reach playable,
and neither does any possible soft-float fix.** The frame is 75.9 M SH-2 cycles.
Soft-float is 82.4% of it. Removing *all* of it — cost zero, not merely faster —
leaves 13.4 M cycles, which is **2.14 FPS** (§6.1). Playable at 15 FPS needs
1.91 M cycles. So the ceiling of this entire workstream is roughly **one seventh
of the way to the target**, and the remaining 17.6% of today's frame is itself
14× over a 33 ms budget. This is one step of several, and the second step is
already larger than this one.

**The finding that shapes the whole design: 99.4% of the port's soft-float call
sites are in the engine tree** (`src/game/` 91.8%, `src/engine/` 4.5%,
`lib/src/` 1.4%), against **19 call sites — 0.6% — in port-owned code** (§3.2).
There is no interception boundary that captures `camera.c`'s or
`mario_actions_moving.c`'s float arithmetic, because that arithmetic is inline
`f32` expressions, not calls through a replaceable API. Extending the Q16.16
precedent to the geometry-and-simulation path therefore means **editing the
engine tree, function by function**, or it means nothing. That is stated plainly
here so the humans decide it, rather than discovering it in implementation.

**And that is not a prediction — it has already been tested by someone else.**
`malucard/sm64-psx` took the same SM64 engine to the FPU-less PlayStation.
They found no interception boundary, did not swap the `f32` typedef, and
converted **~112 engine-tree files** by hand to a Q20.12 parallel type system
(§10.4) — while *also* keeping a hand-written soft float permanently, because
float never fully goes away. That is this document's option (c), converged on
independently. Their one recorded physics artefact — Mario walking slowly
downward on a neutral analogue stick, "due to imprecision of the reimplemented
math", papered over with a deadzone clamp — is exactly the failure mode §8.3's
behavioural gate exists to catch.

---

## 1. Problem statement with the measured evidence

### 1.1 What is measured

| Quantity | Value | Method |
| --- | --- | --- |
| Rendered frame rate | **0.378 FPS** (2.65 s/frame) | Differential `frame_serial` between two captures of one build differing only in `--post-poke-frames` (diagnosis §1.2) |
| SH-2 cycles per rendered frame | **75.9 M** | 2.65 s × 28.6364 MHz (derived) |
| A 30 FPS budget | 954,547 cycles | derived |
| Overrun | **~80×** | derived |
| Share of PC samples inside libgcc soft-float | **28/34 = 82.4%**, exact binomial 95% CI 65–93% | PC histogram over every committed sourceboot capture, resolved against the ELF symbol table (diagnosis §2; independently reproduced here, §1.2) |
| `_vdp1_sync_wait` share | 1/34 = 2.9% | ibid. |
| Fast3D frontend share | 1/34 = 2.9% | ibid. |
| Soft-float implementation in use | `fp-bit.c` | Object names in `<repo-root>/work/yaul-install/lib/gcc/sh-elf/14.3.0/libgcc.a` (per `.yaul.env:3`; note this is the repo *parent* directory, not `sm64-port/work/`): `_addsub_sf.o`, `_pack_sf.o`, `_unpack_sf.o`, `_compare_sf.o`, `_div_sf.o` — reconfirmed here (§5.1) |
| Static instruction budget, one `float + float` | **542 instructions** across 4 calls | `sh-elf-nm --print-size` + `sh-elf-objdump -d` (diagnosis §4.1) |
| Executed instructions, one `float + float`, normal operands | **~200–260** | traced by hand from the disassembly (§1.3); **derived**, not measured |
| Cycles per soft-float operation | **unmeasured** | — |

### 1.2 A refinement to the sampling evidence, stated because it matters

The PC histogram was reproduced exactly (28/34 soft-float). Two things about it
were not stated in the diagnosis and change how much weight it can carry:

**(a) The sample is dominated by superseded builds.** Of 53 raw samples, **42
are from 2026-07-22** — before the Q16.16 render-matrix pipeline (2026-07-23),
before Gouraud (07-24), before Mario (07-25) and before quad merging (07-26).
Restricting to captures taken at or after the Q16 change leaves **10 raw / 7
deduplicated** samples, of which **6/7 (85.7%) are in soft-float** and 1 is
`_vdp1_sync_wait`. Exact binomial 95% CI for 6/7 is **42%–100%**.

The direction is unchanged and the conclusion survives — but the *current
build* is characterised by seven samples, not thirty-four, and one of those
seven is a VDP1 wait. Any plan that leans on "82.4%" as a precise figure is
leaning on a number measured mostly on a different program.

**(b) Cross-build symbol resolution is unsound for game-code addresses.** All 53
PCs were resolved against one ELF. Two samples resolve to
`_draw_skybox_tile_grid` and `_set_background_music` as the caller
(`registers_at_stop.pr`). Both functions were disassembled here at their
addresses in the current ELF: **neither contains any soft-float literal-pool
reference or any call to a soft-float routine.** Those `pr` attributions
therefore cannot be genuine in this build's symbol table — the addresses belong
to a different build's layout. Low `.text` (crt0 + libgcc, `0x06004000`–
`0x06006400`) is link-order-stable and its hits are trustworthy; game-code
addresses at `0x0603xxxx`–`0x0607xxxx` are not portable between builds.

**Consequence for this design: the `pr`-based caller attribution in the
diagnosis (§2, "returns into game code — `_set_background_music`,
`_draw_skybox_tile_grid`, `_create_skybox_facing_camera`") should not be used to
rank work.** The PC bucket "inside soft-float" stands. The claim "the skybox is
3–9% of the frame" rests entirely on 2026-07-22-era captures and is not
established for the current build.

None of this is a reason to doubt that soft-float dominates. It is a reason to
put a real sampling profiler in front of the work (Stage 0, §7.1).

### 1.3 Why `fp-bit.c` is this slow — the executed path

`__addsf3` at `0x06004be8` was traced instruction by instruction. It allocates a
**56-byte stack frame** (three 16-byte `fp_number_type` structs plus spill),
then makes four out-of-line calls:

```
6004be8: mov.l r8,@-r15        ; prologue
6004bee: add   #-56,r15        ; 56-byte frame for three unpacked structs
6004bf8: jsr   @r8             ; __unpack_f  (arg a)
6004c02: jsr   @r8             ; __unpack_f  (arg b)
6004c12: jsr   @r0             ; __fpadd_parts
6004c1a: jsr   @r0             ; __pack_f
6004c22: rts
```

`__unpack_f`'s normal-operand path is ~27 executed instructions; `__pack_f`'s and
`__fpadd_parts`' normal paths are larger and `__fpadd_parts` additionally calls
`__ashlsi3_r0` for its variable alignment shift (one PC sample landed there).
Executed total for a normal add: **~200–260 instructions** (derived), across
four `jsr`/`rts` pairs and roughly 30 memory accesses to that 56-byte frame.

There is no cheap path. fp-bit unpacks to a struct in memory, operates on it,
and repacks — on every operation, for every operand, including `0.0f + x`.

A second, unquantified aggravator worth naming: the SH7604's cache is **4 KB,
4-way, unified**. The float subset of fp-bit spans `0x06004a54`–`0x06005ec4`
(5,232 bytes of address range; 920 bytes of actual code for the add path alone),
and the double subset extends to `0x06006340`. Every float operation therefore
walks a code working set comparable to the whole cache, evicting the caller.
This is a plausible reason the true cycles-per-op is at the high end of any
instruction-count estimate — **it is not measured and should not be asserted**;
Stage 0 settles it.

---

## 2. What the existing Q16.16 precedent actually covers

This project has already done fixed-point conversion once, successfully, under
duress: `docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md`
established by host-vs-target differential test that the toolchain's soft-float
corrupts `mtxf_lookat`/`mtxf_mul` on this target, and the 2026-07-23 sprint
removed the dependency for the render matrix.

**What it built** (all in `src/port/saturn/gfx/`):

- `saturn_matrix.h` — Q16.16 `sm64_saturn_mtx_t`, 11-deep stack, overflow-checked
  `sm64_saturn_matrix_mul` using SH-2's native 32×32→64 `dmuls.l`.
- `saturn_matrix_kernels.h` — `sm64_saturn_q16_mul`, `sm64_saturn_isqrt64`
  (bit-by-bit integer sqrt), and the **exact float↔Q16 bridge**
  (`sm64_saturn_float_to_q16` / `sm64_saturn_q16_to_float`) that scales by
  adjusting the IEEE exponent field directly so the conversion never invokes
  float arithmetic.
- `saturn_matrix_ctors.h` — Q16 mirrors of `mtxf_lookat`,
  `mtxf_rotate_zxy_and_translate`, `mtxf_rotate_xyz_and_translate`,
  `mtxf_billboard`, `mtxf_scale_vec3f`, `mtxf_translate`, `mtxf_rotate_xy`.
- `saturn_trig_q16.inc.c` (651 lines), generated by `tools/saturn/gen_trig_q16.py`.
- Host differential tests: `tools/saturn/mtxq_ctor_diff_test.c`,
  `tools/saturn/mtxf_lookat_host_diff_test.c`, run by the Makefile targets
  `verify-mtxq-ctors` and `verify-mtxf-lookat-host-diff`.

**What it cost at the boundary, and this is the part to internalise.** The Q16
stack is *not* a replacement for `gMatStack` — it is a shadow of it. Because the
rest of the engine reads `gMatStack` as `Mat4` (shadow positioning, culling,
held-object math, positional audio), `rendering_graph_node.c` maintains a float
mirror:

```c
static void saturn_mtxq_refresh_float_mirror(s16 index) {
    for (s32 i = 0; i < 4; i++)
        for (s32 j = 0; j < 4; j++)
            gMatStack[index][i][j] =
                sm64_saturn_q16_to_float(gMatStackQ[index].m[i][j]);
}
```
— `src/game/rendering_graph_node.c:81-88`

That is **16 `__floatsisf` calls per composition site per frame**, called from
nine sites in that file. The conversion bought back the matrix *arithmetic* and
paid back part of it in *conversions at the boundary*. That is the generic tax
on any partial fixed-point island inside a float engine, and it scales with the
perimeter of the island, not its area.

**And the discipline was already bent to do it.** The standing rule that
`src/engine/`, `src/game/`, `levels/`, `actors/`, `include/` stay unmodified
(e.g. `docs/superpowers/specs/2026-07-25-general-quad-merging-design.md:182`,
`docs/saturn/SEGMENT_ADDRESSING_DECISION.md:119`) is today observed with four
exceptions, all under `#ifdef TARGET_SATURN`:

| file | `TARGET_SATURN` lines |
| --- | ---: |
| `src/game/rendering_graph_node.c` | 28 |
| `src/game/game_init.c` | 12 |
| `lib/src/guMtxF2L.c` | 7 |
| `src/game/main.c` | 1 |

So the precedent is not "the engine tree is untouchable"; it is "the engine tree
is touched narrowly, under a target guard, with a differential test per change."
That is a workable precedent — but §3 shows how far it would have to stretch.

---

## 3. The call-site map

### 3.1 Method

`sh-elf-objcopy -O binary --only-section=.text` on the committed ELF, then scan
the 480,552-byte image for 4-aligned big-endian words equal to the address of
each soft-float symbol (SH-2 calls load the target from a PC-relative literal
pool). Each hit's address is mapped to its containing function via
`sh-elf-nm -n --print-size`, and each function to its source file via
`sh-elf-addr2line` against the ELF's DWARF (`-g` is on).

This counts **literal-pool entries** — roughly one per calling function per pool
— not dynamic calls. It maps *where* float code is, not *how hot* it is. That
distinction is load-bearing and §3.4 addresses it.

Total found: **3,144 references**, of which 45 are internal to `fp-bit.c` itself
(`__pack_f`→`__unpack_f` etc.), leaving **3,099 in project code**. The
diagnosis's 3,041 is the same measurement with `___eqsf2`, `___floatunsisf`,
`___fixunssfsi`, `___fixunsdfsi` and the fp-bit internals elided under "others".

### 3.2 The decisive partition: engine tree versus port-owned

| bucket | refs | share of project code | distinct functions |
| --- | ---: | ---: | ---: |
| `src/game/` | 2,886 | 93.1% | 902 |
| `src/engine/` | 141 | 4.6% | 49 |
| `lib/src/` (N64 `gu*`, `sinf`/`cosf`) | 45 | 1.5% | 8 |
| **engine tree subtotal** | **3,072** | **99.4%** | **959** |
| **`src/port/saturn/` (port-owned)** | **19** | **0.6%** | **3** |

The entire port-owned surface is three functions:

| refs | function | file |
| ---: | --- | --- |
| 14 | `sm64_saturn_fast3d_resolve_triangle` | `src/port/saturn/gfx/saturn_fast3d_frontend.c:288` |
| 4 | `sqrtf` | `src/port/saturn/compat/sqrtf.c:11` |
| 1 | `sm64_saturn_float_to_q16` | `src/port/saturn/gfx/saturn_matrix_kernels.h:128` |

**This is the crux of feasibility.** Any plan of the form "the port intercepts at
a boundary and does the math in fixed point" has 0.6% of the call sites available
to it before it must edit engine files.

### 3.3 Where the engine-tree float lives

Top source files by soft-float literal-pool references (`dbl` = the
double-precision subset, the parallel workstream's target):

| refs | dbl | file | per-frame role |
| ---: | ---: | --- | --- |
| 382 | 0 | `src/game/camera.c` | yes, every tick |
| 166 | 10 | `src/game/object_helpers.c` | yes, per object |
| 133 | 7 | `src/game/mario_actions_moving.c` | yes, action-dependent |
| 100 | 3 | `src/game/mario_actions_airborne.c` | yes, action-dependent |
| 85 | 0 | `src/game/mario_actions_submerged.c` | BOB: no |
| 82 | 28 | `src/game/shadow.c` | yes |
| 79 | 15 | `src/game/obj_behaviors.c` | yes, per object |
| 75 | 0 | `src/game/mario.c` | yes |
| 63 | 0 | `src/game/mario_actions_cutscene.c` | rarely |
| 57 | 0 | `src/game/mario_actions_automatic.c` | rarely |
| 52 | 0 | `src/engine/math_util.c` | yes, heavily |
| 52 | 0 | `src/game/mario_step.c` | yes |
| 46 | 0 | `src/game/interaction.c` | yes |
| 43 | 8 | `src/game/ingame_menu.c` | yes (HUD) |
| 41 | 0 | `src/engine/surface_collision.c` | yes, heavily |
| 40 | 9 | `src/game/envfx_snow.c` | BOB: no |
| 39 | 15 | `src/game/screen_transition.c` | rarely |
| 31 | 0 | `src/game/rendering_graph_node.c` | yes, per node |
| 24 | 7 | `src/game/skybox.c` | yes |
| — | — | `src/game/behaviors/*` (130 files) | **1,218 refs (38.7%)**, only for spawned objects |

**Structural reading:** 38.7% of all references are in per-object behaviour code
that only executes for objects actually spawned. The remainder splits into a
*simulation* core (camera 382, Mario ~500, collision 41, math_util 52,
object_helpers 166) and a *render* path (rendering_graph_node 31, shadow 82,
skybox 24, `lib/src/gu*` 45).

**The render path has already been largely converted.** What remains in
`_geo_process_node_and_siblings` (26 refs) is `__floatsisf` ×8 / `__mulsf3` ×6 /
`__addsf3` ×4 — the animation-translation and culling scalars, not matrix
composition. The *simulation* core has not been touched at all, and it is where
the float now is.

### 3.4 The functions that concentrate calls, and the divide problem

Static call-site counts for the hot math helpers (same method, counting
literal-pool references to each *helper's* address):

| callers | helper | note |
| ---: | --- | --- |
| 74 | `atan2s` (`math_util.c:713`) | → `atan2_lookup`, which contains a `__divsf3` |
| **60** | **`sqrtf`** (`src/port/saturn/compat/sqrtf.c`) | **port-owned; 4 `__divsf3` + 4 `__mulsf3` + 4 `__addsf3` per call** |
| 58 | `find_floor` (`surface_collision.c:511`) | |
| 17 | `approach_f32` (`math_util.c:679`) | |
| 7 | `find_wall_collisions` | |
| 7 | `find_ceil` | |
| 3 | `sinf`, 3 `cosf` | each runs the full fp-bit **double** path |

`___divsf3` has only 57 literal-pool references — near the bottom of the static
table — but **4 of 34 PC samples (11.8%) land inside it**, third-highest of any
symbol. Divide is rare and expensive, and the port's own `sqrtf` issues four of
them per call:

```c
float sqrtf(float value) {
    if (value <= 0.0f) return 0.0f;
    union { float f; uint32_t u; } estimate = { .f = value };
    estimate.u = (estimate.u >> 1) + 0x1FC00000U;
    for (uint8_t iteration = 0; iteration < 4; iteration++)
        estimate.f = 0.5f * (estimate.f + value / estimate.f);
    return estimate.f;
}
```
— `src/port/saturn/compat/sqrtf.c:11-18`

Every `vec3f_normalize`, `vec3f_get_dist_and_angle`, `dist_between_objects`,
`calc_hor_dist`, `guNormalize`, `guLookAtReflectF`, `init_shadow`,
`create_skybox_facing_camera` and ~50 more call this. **The port already owns an
integer square root** (`sm64_saturn_isqrt64`) that needs zero float operations.
This is the single highest-value change in the whole document per line of code,
and it touches no engine file.

### 3.5 Operation mix

| class | refs | share | replaceability in assembly |
| --- | ---: | ---: | --- |
| arithmetic `sf` (`add` 492, `mul` 487, `sub` 344, `div` 57) | 1,380 | 44.5% | moderate — needs normalisation |
| comparisons `sf` (`lt` 348, `gt` 285, `ge` 108, `le` 56, `ne` 41, `eq` 29) | 867 | 28.0% | **trivial** — IEEE-754 floats order as sign-magnitude integers; ~10 instructions |
| conversions (`floatsi` 282, `fixsf` 209, `floatunsi` 16, `fixunssf` 8) | 515 | 16.6% | easy — ~20–25 instructions |
| double-precision (all forms) | 337 | 10.9% | **the parallel workstream's target** |

Statically, comparisons and conversions are 44.6% of call sites and are the
cheapest to hand-write. **Dynamically they are much less significant**: only
2 of 28 soft-float PC samples are comparisons (`___lesf2`, `___gesf2`), because
they are already the cheapest fp-bit routines. Do not over-invest there on the
strength of the static count alone.

---

## 4. Precision analysis — what fixed point can and cannot represent

This is needed to set an acceptance bar (§8) that is justified rather than
picked, and to say honestly where Q16.16 would break SM64.

**f32 versus Q16.16 resolution.** `float` has a 24-bit significand, so at
magnitude *m* its ulp is 2^(⌊log₂m⌋−23). Q16.16's ulp is a constant 2^−16.
They are equal at **m = 128**. Therefore:

- **Above 128 world units, Q16.16 is strictly *finer* than `f32`.** BOB spans
  roughly ±8,000 units; at that magnitude `f32`'s ulp is ~0.00098 while
  Q16.16's is 0.0000153 — Q16.16 is 64× more precise for world positions.
- **Below 128, `f32` is finer**, but Q16.16 still resolves 1.5×10⁻⁵ absolutely,
  which is far below anything SM64 gameplay observes in a position, velocity or
  angle.

**Where Q16.16 genuinely fails, and these are the real risks:**

1. **Magnitude ceiling ±32,767.** Squared distances overflow immediately —
   `dx² + dy² + dz²` at `dx = 1000` is 10⁶. Every sum-of-squares must widen to
   `int64` (Q32.32) before `isqrt64`. This is exactly the failure Sonic Z-Treme
   ships unguarded (`ZT_VECTOR.c:3-20` accumulates three `slMulFX` results into
   one `FIXED`, overflowing above ~±181 units, with no clamp and no comment).
   The port's `sm64_saturn_matrix_mul` already models the right discipline:
   accumulate in `int64_t`, check before adding, return an overflow flag.
2. **Ratios and reciprocals.** `f32`'s floating exponent handles `a/b` across
   many orders of magnitude; Q16.16 loses the small end. Normalisation,
   perspective divide and `approach_f32`-style easing coefficients are the
   sensitive sites.
3. **Iterated accumulation.** Constant-ulp truncation biases in one direction
   under repeated `+=`; float's round-to-nearest does not. Camera smoothing and
   velocity integration accumulate over hundreds of frames.

**Conclusion for the design:** fixed point is not a precision downgrade for SM64
world-space *positions* — it is an upgrade. It is a downgrade for *ratios* and a
hard hazard for *products*. That asymmetry is why §7 converts distance/collision
paths (positions and comparisons, wide intermediates) before it converts camera
smoothing (ratios and accumulation), and why it does not propose converting
behaviour code at all.

---

## 5. The options

### 5.1 Option (a) — Replace `fp-bit.c` with a faster soft-float. **Recommended as Stage 1.**

Exact IEEE-754 semantics preserved, so no engine change, no differential-test
risk, no gameplay risk. Bounded by what IEEE-754 costs on a 28 MHz SH-2 even
when written well.

**The diagnosis flagged as an open question (§7.3) whether GCC 14.3.0 ships a
usable assembly soft-float for `-m2`. The answer found here is better than
that question anticipated, and it is verified at the object level:**

`sh-elf-ar t` on the two libgcc archives on this machine:

| toolchain | float objects | library |
| --- | --- | --- |
| GCC **14.3.0** (`<repo-root>/work/yaul-install/lib/gcc/sh-elf/14.3.0/libgcc.a` (per `.yaul.env:3`; note this is the repo *parent* directory, not `sm64-port/work/`), the one linked today) | `_addsub_sf.o`, `_mul_sf.o`, `_div_sf.o`, `_compare_sf.o`, `_pack_sf.o`, `_unpack_sf.o`, `_fpcmp_parts_sf.o` | **`fp-bit.c`** |
| GCC **15.2.0** (`/d/tmp/gcc15sh/sh-gcc-toolchain/work/lib/gcc/sh-elf/15.2.0/libgcc.a`, already built) | `addsf3.o`, `subsf3.o`, `mulsf3.o`, `divsf3.o`, `eqsf2.o`, `lesf2.o`, `gesf2.o`, `floatsisf.o`, `fixsfsi.o`, `unordsf2.o`, `negsf2.o`, `truncdfsf2.o`, `extendsfdf2.o` | **`soft-fp`** |

`soft-fp` is GCC's modern soft-float — the same implementation glibc uses on
FPU-less targets. Structurally it is a different animal from `fp-bit.c`: unpack,
operate and pack are **macro-expanded inline into one function**, so a float add
is one call with register-resident intermediates instead of four calls through a
56-byte stack frame. The GCC maintainers' own reason for the switch, as recorded
in `docs/saturn/evidence/e2-sourceboot-gmatstack-corruption-2026-07-22.md`, was
performance.

**The upstream change, traced.** GCC commit
`e95512e2d5a317e8c043f232158df4b38186e51c`, "SH: Use softfp for sh-elf",
2024-10-10 — after the GCC 14 branch point (~April 2024) and never backported.
It consists of exactly two things:

- a new `libgcc/config/sh/sfp-machine.h` (**83 lines**, pure boilerplate:
  `_FP_W_TYPE_SIZE 32`, `_FP_MUL_MEAT_S` via `umul_ppmm`, `_FP_DIV_MEAT_S` via
  `_FP_DIV_MEAT_1_udiv_norm`, NaN conventions, endian handling — **no SH-specific
  assembly at all**); confirmed absent on `releases/gcc-14`, present on
  `releases/gcc-15` and in the local 15.2.0 tree at
  `/d/tmp/gcc15sh/sh-gcc-toolchain/gcc-15.2.0/libgcc/config/sh/sfp-machine.h`;
- one line in `libgcc/config.host`: `t-fdpbit` → `t-softfp-sfdf t-softfp` for
  the `sh-*-elf*` entry (gcc-14 branch line 1410, gcc-15 line 1405).

**The upstream patch submission benchmarked it at ~3× on Whetstone.** That is a
*measured* figure for this exact change on this exact target family — the best
number available short of Stage 0's own microbenchmark, and better grounded than
the 2–4× structural estimate.

`libgcc/soft-fp/` and `libgcc/config/t-softfp-sfdf` **already exist in the
14.3.0 source tarball.** So a third route exists: copy the 83-line header and
change one line in `config.host`, rebuild libgcc only. Take the *post-fix*
version of the header — two correctness follow-ups landed 2025-04-19
(`2a643f55f5ac` "sh: Correct NaN signalling bit and propagation rules
[PR111814]"; `05c4e3ecb54d` "sh: libgcc: Implement fenv rounding and exceptions
for soft-fp [PR118257]") and GCC 15.2.0 includes both.

**Also confirmed:** `libgcc/config/sh/lib1funcs.S` contains **zero** float
routines — `LIB1ASMFUNCS` in `libgcc/config/sh/t-sh:19-24` is integer-only
(`_ashiftrt _movmem _mulsi3 _sdivsi3 _udivsi3 _set_fpscr _div_table
_udiv_qrnnd_16`), and the only FP-adjacent entry, `_set_fpscr`, is FPU control.
And with `-m2`, `gcc/config/sh/sh.h:47,66` make `TARGET_SH2E` false and therefore
`TARGET_FPU_ANY` false, so every SF/DF operation is a libgcc call by
construction. **There is no hidden faster path in 14.3.0 that is merely
unselected — the diagnosis's open question §7.3 is answered "no, not in 14.3.0;
yes, in 15."**

Both toolchains were configured identically
(`--target=sh-elf --with-endian=big --with-cpu=m2 --disable-multilib`), so
they are a clean A/B on GCC version alone, and neither install contains an
alternative libgcc variant to switch to.

Measured properties of the GCC 15.2.0 objects (`sh-elf-size`, `sh-elf-nm -u`,
`sh-elf-readelf -h`):

| object | `.text` bytes | undefined symbols |
| --- | ---: | --- |
| `addsf3.o` | 1,436 | `__clzsi2`, `__ashlsi3_r0`, `__lshrsi3_r0` |
| `subsf3.o` | 1,564 | same |
| `mulsf3.o` | 856 | same |
| `divsf3.o` | 828 | same + `__udiv_qrnnd_16` |
| `floatsisf.o` | 296 | same |
| `fixsfsi.o` | 124 | `__ashlsi3_r0`, `__lshrsi3_r0` |
| `lesf2.o` | 152 | **none** |

Three facts make this a link-order change rather than a port:

1. **ISA matches.** `sh-elf-readelf -h addsf3.o` reports `Flags: 0x2, sh2` —
   identical to the linked sourceboot ELF. Both toolchains report
   `sh-elf-gcc -print-multi-lib` = `.;` (single multilib), so there is no
   variant-selection trap. `mulsf3.o` uses SH-2's hardware multiply.
2. **Every imported helper already exists in the 14.3.0 archive.**
   `sh-elf-nm --defined-only` on `<repo-root>/work/yaul-install/.../14.3.0/libgcc.a` finds
   `___clzsi2`, `___udiv_qrnnd_16`, `___ashlsi3_r0`, `___lshrsi3_r0`. (`___ashlsi3_r0`
   is already linked into the current build — it appears in the PC histogram.)
3. **No C++, no LTO, stable SH ABI**, so cross-version object mixing is a normal
   link.

**Two ways to take it, and they should be tried in this order:**

- **(a1) Extract the soft-fp objects and link them ahead of libgcc.**
  Smallest possible change: no toolchain switch, no rebuild of anything, the
  14.3.0 compiler still compiles the engine. If the symbols resolve from the
  earlier archive, fp-bit's are never pulled in.
- **(a2) Backport to 14.3.0**: copy the 83-line `sfp-machine.h` (post-2025-04-19
  version) into the 14.3.0 source, change the one `config.host` line, rebuild
  libgcc. Keeps the compiler the project is validated against; gets soft-fp for
  both single and double. Slightly more work than (a1), materially less risk
  than (a3).
- **(a3) Move the whole build to the GCC 15.2.0 toolchain.**
  Cleanest, already built. **Blocked by a recorded objection**:
  `src/port/saturn/gfx/saturn_matrix_kernels.h:12-15` states
  "GCC 15.2.0 has a different miscompilation of the same function at `-Os`".
  Note that the parallel workstream is removing `-Os`. Whether that objection
  survives `-O2` is an open question (§11.3) with a cheap, decisive test:
  run `verify-mtxf-lookat-host-diff`'s inputs through a GCC-15 `-O2` target build.

- **(a4) Hand-written SH-2 assembly**, as a later refinement on top of soft-fp,
  prioritised by Stage 0's measured mix. Comparisons first (IEEE-754
  sign-magnitude ordering), then conversions, then `__mulsf3` (`dmuls.l`), then
  `__divsf3` (SH-2 DIVU, 39 clocks).

  **No permissively- or GPL-licensed SH-2 assembly IEEE-754 soft-float exists**
  (§10.2) — searched: newlib (`newlib/libc/machine/sh/` has string/setjmp only,
  zero float), musl (defers to libgcc), libyaul (defers to libgcc), Berkeley
  SoftFloat (portable C, no SH tuning), and Joern Rennecke's 2006/2010 SH
  IEEE-754 assembly (`gcc/config/sh/IEEE-754/`, GPLv3+RLE) which **was never
  merged**, is absent from GCC master and from the local 15.2.0 tree, targeted
  SH3/SH4 rather than SH-2, and has been unmaintained for 16 years. Other GCC
  ports (`arm/ieee754-sf.S`, `arc/ieee-754/`, `xtensa/ieee754-sf.S`) do ship
  hand-written float assembly; **SH is one of the ports that never got it.**
  So (a4) is new code, and should not be started until (a1) has been measured.

  **§10.4's PS1 reference calibrates what (a4) is worth**: a hand-written
  relaxed soft float on a comparable no-FPU 33 MHz scalar RISC costs 33–35
  cycles for an add, 30 for a multiply, 57 for a divide and 4–8 for a
  comparison, with cycle counts documented per operation in the source. Against
  fp-bit's ~200–260 executed instructions for an add (§1.3), that is roughly
  **5–8× if SH-2 matches** — see §6.2 for the caveats on that analogy.

- **(a5) Relaxed-precision soft float that keeps the IEEE-754 *bit layout*.**
  Not in the original option set; **added after studying the PS1 port**
  (§10.4), which took exactly this route. The insight is that §5.4's objection —
  compiler-emitted float constants are IEEE bit patterns — only bites if you
  change the *format*. Keep the format (sign / 8-bit exponent / 23-bit
  significand) and constants stay valid; then drop what SM64 does not need:
  denormals, NaN and infinity handling, correct rounding (truncate instead),
  and exception flags. That is where most of an IEEE implementation's cost is.
  Cheaper than (a4), same integration story, **but it forfeits §8.2's
  bit-exact acceptance bar** and inherits §8.3's tolerance-plus-behavioural-gate
  bar instead. Worth doing only if Stage 0 shows (a1) leaves float still
  dominant, and only after (a1) has established a correct baseline to diff
  against.

**Expected win for (a1): ~3×** (**measured upstream**, Whetstone, for this exact
change on this target family), consistent with a 2–4× structural estimate
derived here independently (one call versus four; register intermediates versus
a 56-byte stack frame; `lesf2.o` with zero calls versus `___lesf2` +
2×`__unpack_f` + `__fpcmp_parts_f`). **Not measured for this workload** —
Stage 0.3 measures it, and can do so *before* any link change by running the
microbenchmark against both archives.

### 5.2 Option (b) — Extend Q16.16 fixed point into the geometry and simulation path

Highest ceiling by far: a Q16.16 multiply is `dmuls.l` + `sts` ×2 + `xtrct`
(~4 instructions, ~8 cycles); an add is one instruction; a comparison is one.
Against ~200–260 executed instructions for a soft-float add, that is a
**30–100× per-operation** improvement, not 2–4×.

Both in-tree shipping Saturn engines validate the destination unambiguously
(§10): **SlaveDriver and Sonic Z-Treme contain zero runtime floating point.**
Sega's own SGL uses `FIXED` — the identical Q16.16 — for exactly this job.

**And here is the honest cost.** §3.2 measured that 99.4% of soft-float call
sites are in the engine tree. Those are not calls through a replaceable API;
they are inline `f32` expressions inside `camera.c`, `mario_actions_moving.c`,
`object_helpers.c` and 130 behaviour files. To convert them you must edit those
files. There is no third alternative:

- **A link-time interception does not exist**, because the arithmetic is
  `a * b + c` in C, lowered to `__mulsf3`/`__addsf3` by the compiler — you can
  replace those symbols (that is option (a)) but you cannot make them
  fixed-point, because §5.4 shows the float ABI cannot be reinterpreted.
- **A boundary interception captures only what is behind a function boundary**:
  `math_util.c`'s `vec3f_*`/`atan2s`/`approach_f32` (52 refs),
  `surface_collision.c`'s `find_floor`/`find_ceil`/`find_wall_collisions`
  (41 refs), `lib/src/gu*.c` (45 refs), `sinf`/`cosf`, and the port's own
  `sqrtf`. **That is ~150 of 3,099 references** — under 5% statically, though
  plausibly much more than 5% dynamically, since `find_floor` has 58 callers,
  `atan2s` 74 and `sqrtf` 60.
- **Everything else requires editing `camera.c` and the Mario action files.**
  That is not "extending the precedent"; it is porting SM64's simulation to
  fixed point. It is a multi-month project, it diverges permanently from
  upstream, and every behavioural regression it introduces (camera drift,
  collision jitter, velocity accumulation bias) surfaces as a gameplay bug that
  a host differential test on one function will not catch.

**So option (b) splits cleanly into (b1), which is tractable, and (b2), which is
a different project:**

- **(b1) — port-owned + function-boundary conversion.** `sqrtf` (port-owned),
  `sinf`/`cosf`, `math_util.c` helpers, `surface_collision.c` entry points,
  `lib/src/gu*.c`. Bounded, each with a clean differential-test story matching
  `mtxq_ctor_diff_test.c`. Two to four engine files touched, under
  `#ifdef TARGET_SATURN`, on the pattern already set by
  `rendering_graph_node.c`.
- **(b2) — simulation conversion** (`camera.c`, `mario_*.c`, `behaviors/*`,
  ~2,700 refs). **Not recommended, and not costed here beyond "months".** If the
  owner wants it, it needs its own design document and its own risk budget.

**§10.4's PS1 reference is direct empirical confirmation of this split, and it
is the strongest single piece of evidence in this document.** `malucard/sm64-psx`
took the same float-heavy SM64 engine to an FPU-less CPU and **did (b2) — and it
cost them ~112 engine-tree files**: 1,040 fixed-point occurrences across 98 files
in `src/game/`, 192 across 11 files in `src/engine/`, 12 across 3 in `src/menu/`.
They found no shortcut. There was no typedef swap, no boundary interception, no
clever ABI trick. `f32` is still `typedef float f32` in their
`include/PR/ultratypes.h:18`. They introduced a *parallel* `q32`/`Vec3q` type
system and converted call sites by hand, file by file. **And they kept a
hand-written soft float permanently anyway**, because float never fully goes
away. That is option (c), arrived at independently, by the only other team that
has done this.

### 5.3 Option (c) — Hybrid

(a) as the floor for everything, (b1) for the measured hot boundaries. **This is
the recommendation**, and §7 is its staging. It is not a compromise between two
half-measures: (a) is the only thing that helps the 2,700 references nobody is
going to convert, and (b1) is the only thing that helps the paths where a 2–4×
soft-float speedup is still 30× short.

### 5.4 Rejected: reinterpret the float ABI as Q16.16 — but note what *is* salvageable

Tempting, and worth killing explicitly because it will occur to the next reader.
Since *all* float arithmetic routes through libgcc symbols, one could define
`__addsf3(a,b) = a+b`, `__mulsf3 = q16_mul`, `__floatsisf = x<<16`,
`__ltsf2 = a<b` … and get fixed-point speed with zero source changes.

**It cannot work.** Float *constants* are materialised by the compiler as
IEEE-754 bit patterns in PC-relative literal pools at compile time; no runtime
library sees them. `1.0f` becomes `0x3F800000`, which reads as 16256.0 in
Q16.16. Nothing short of a linker pass that could identify which pool words are
floats — undecidable — recovers this. The same objection kills any *format*
change.

**But it does not kill a *semantics* change at the same format**, and that
distinction is worth a lot — it is option **(a5)** in §5.1, added after studying
the PS1 port, which shipped exactly this. Keep the IEEE-754 bit layout, so
compiler-emitted constants remain valid; discard denormals, NaN, infinity,
correct rounding and exception flags, which is where most of the cost lives.
The PS1 implementation's `__addsf3` reference version, for instance, has no
NaN/Inf path at all and returns exact zero on equal-magnitude subtraction.

### 5.5 Rejected: compile the engine as C++ with an operator-overloaded fixed type

Would give a genuine `typedef f32 fixed` substitution with no expression
rewrites. Rejected: the SM64 decompilation is C with C-specific constructs
throughout; the conversion cost and its regression surface exceed (b2)'s, and
`-Os`/`-O2` codegen for a wrapper class on SH-2 is unproven. Named for
completeness.

### 5.6 Options table

| # | Option | Expected win on float ops | Effect on frame rate | Cost | Risk | Engine edits |
| --- | --- | --- | --- | --- | --- | --- |
| **a1** | **Link GCC 15.2.0 soft-fp objects ahead of libgcc** | **~3×** *(measured upstream, Whetstone)* | 0.378 → **0.84 FPS** *(derived)* | **hours** | low — bit-exact IEEE, testable | **none** |
| a2 | Backport `sfp-machine.h` (83 lines) + 1 line of `config.host` to 14.3.0, rebuild libgcc | same, plus double | same | 1–2 days | low | none |
| a3 | Move build to GCC 15.2.0 | same, plus double | same | days | medium — recorded `-Os` miscompilation, unretested at `-O2` | none |
| a4 | Hand-written SH-2 assembly, bit-exact IEEE | 5–8× *(derived by analogy, §6.2)* | 0.378 → 1.1–1.3 FPS *(derived)* | weeks | medium — new code, but bit-exact-testable | none |
| a5 | Hand-written SH-2 assembly, **relaxed** semantics at IEEE layout (the PS1 route) | 6–10× *(guess)* | 0.378 → 1.2–1.4 FPS *(guess)* | weeks | medium-high — **forfeits the bit-exact bar** | none |
| b1 | Q16 at port-owned + function boundaries | 30–100× on ~5% of static sites, unknown % of dynamic | **unknown until Stage 0** | 1–2 weeks | medium — needs a differential test per conversion | 2–4 files, guarded |
| b2 | Fixed point across `camera.c` / `mario_*` / behaviours | 30–100× on ~87% of sites | approaches the 2.14 FPS ceiling | **months** — the PS1 port needed **~112 engine files** (§10.4) | **high** — gameplay regressions (the PS1 port shipped one it papered over), permanent upstream divergence | ~112 files |
| **c** | **a1 + b1 (+ a4/a5 if Stage 0 justifies)** | — | **0.9–1.6 FPS** *(guess)* | 2–3 weeks | low–medium | 2–4 files, guarded |
| — | *Perfect* soft-float elimination (theoretical bound) | ∞ | **2.14 FPS** *(derived)* | — | — | — |

---

## 6. Expected win, quantified honestly

### 6.1 The Amdahl ceiling — the most important number here

Frame = 75.9 M cycles (derived from a measured 2.65 s). Soft-float share =
82.4% (measured, CI 65–93%). Non-soft-float remainder = 17.6% = **13.4 M
cycles**.

| soft-float cost after the fix | frame (M cycles) | FPS | speedup |
| --- | ---: | ---: | ---: |
| unchanged (today) | 75.9 | 0.378 | 1.0× |
| ÷2 | 44.6 | 0.64 | 1.7× |
| ÷3 (central soft-fp estimate) | 34.2 | 0.84 | 2.2× |
| ÷5 | 25.9 | 1.11 | 2.9× |
| ÷10 | 19.7 | 1.45 | 3.9× |
| ÷50 (fixed-point-class) | 14.7 | 1.95 | 5.2× |
| **→ 0 (impossible bound)** | **13.4** | **2.14** | **5.7×** |
| **target: playable** | **1.91** | **15.0** | **40×** |

Sensitivity to the measured share's confidence interval, at the impossible
bound: at 93% soft-float → 5.3 M cycles → **5.4 FPS**; at 65% → 26.6 M cycles →
**1.08 FPS**. So the honest range for *perfect* soft-float elimination is
**1.1 – 5.4 FPS, central 2.1 FPS**.

**Playable is not reachable through this workstream at any level of effort.**
Even at the optimistic end of the confidence interval, a perfect fix lands at
one third of 15 FPS. This should be the headline in any status report, and it
should reset expectations before anyone spends a month on option (b2) expecting
it to make the game playable.

Two caveats on the ceiling, in opposite directions:

- **It may be optimistic.** 1/34 samples are in `_vdp1_sync_wait` — 2.2 M cycles
  ≈ 78 ms of VDP1 *waiting* per frame, which does not shrink when the CPU gets
  faster. The single sample from the *current* build (2026-07-26) is in
  `_vdp1_sync_wait`. If VDP1 wait is a larger share of the current build than of
  the 2026-07-22 builds that dominate the histogram, the ceiling is lower.
- **It may be pessimistic.** The 13.4 M-cycle remainder is 14,700 cycles per
  emitted triangle (913 triangles/frame, measured). That is itself absurd, so
  the remainder is compressible — by the diagnosis's Ranks 3–6 (`-O2`, cart
  staging, skybox, slave SH-2) and by reducing work volume (draw distance,
  object budget, decoupling tick rate from render rate). Those are separate
  workstreams, and **they are where the other 7× has to come from.**

### 6.2 Per stage

| stage | what | expected soft-float speedup | expected frame rate | confidence |
| --- | --- | --- | --- | --- |
| 0 | Measurement only | 1.0× | 0.378 FPS | — |
| **1a** | soft-fp objects linked ahead of libgcc | **~3×** | **0.84 FPS** | **measured upstream** (Whetstone, this target family); not measured for this workload |
| 1b | port-owned `sqrtf` → integer sqrt; `sinf`/`cosf` → Q16 table | removes 12 float ops per `sqrtf` call (60 static callers) and the entire double path from trig | **+10–30% on top of 1a** | **guess** — call frequency unmeasured |
| 2 | (b1) Q16 at `math_util` / `surface_collision` / `gu*` boundaries | 30–100× on those paths | **0.9–1.6 FPS** | **guess** — depends entirely on Stage 0's profile |
| (opt) | a4/a5 hand-written SH-2 assembly | 5–10× vs fp-bit, i.e. ~2–3× on top of 1a | 1.1–1.4 FPS | **derived by analogy** (a4) / **guess** (a5) |
| — | parallel: double-promotion removal | shrinks the 10.9% double subset | folded into the above | other workstream |
| — | parallel: `-Os` → `-O2` | attacks the 17.6% remainder and call overhead | multiplicative, unquantified | other workstream |
| — | **all of the above, perfectly** | — | **~2.1 FPS** | **derived bound** |

Every figure past Stage 1a is a guess, and is labelled one. Stage 0 exists to
turn the Stage 2 row into a derived number before anyone commits to it.

**The one external calibration point available for hand-written assembly.**
`malucard/sm64-psx` documents per-operation cycle counts in its MIPS soft-float
source (§10.4), for a 33.87 MHz R3000A — a scalar, in-order, FPU-less RISC of the
same generation as the SH-2:

| operation | cycles (their measurement, excluding call overhead) |
| --- | --- |
| `__addsf3` | 6 (a=0) … 33–35 (general) |
| `__subsf3` | add 2 |
| `__mulsf3` | 13 (a=0) … 30 (general) |
| `__divsf3` | 13 … 57 (general; the `divu` alone is 36) |
| all six comparisons (one shared entry) | 4–8 |
| float ↔ Q20.12 conversion | 3–16 |

Against fp-bit's **~200–260 executed instructions** for an add on SH-2 (§1.3),
that implies roughly **5–8×**. Three caveats keep this "derived by analogy", not
measured: (i) the R3000A has 32 general registers to the SH-2's 16, so an SH-2
port spills more; (ii) SH-2's DIVU is 39 clocks against MIPS `divu`'s 36;
(iii) their implementation is *relaxed*, not bit-exact IEEE, so the a4 (exact)
figure should be discounted below the a5 (relaxed) one. Stage 0.3's
microbenchmark replaces this analogy with a number.

---

## 7. Staged plan

The ordering principle: **each stage must be independently verifiable and
shippable, and must produce a measurable number.** Nothing here is a grand
rewrite.

### Stage 0 — Instrument (prerequisite; ~1–2 days; no semantic risk)

Without this, every estimate past §6.1 is a guess, and the current build is
characterised by seven PC samples.

1. **Fix `tools/saturn/telemetry_decode.py`.** Recent captures report
   `decode_error: unexpected telemetry magic` (`0x2118DE5A`, `0xD2552E11`); the
   profile is currently readable only by hand-decoding `probe_window` against
   the ELF. Cheap, and it unblocks everything else.
2. **FRT phase timers in `sm64_saturn_fast3d_profile_t`.** The struct has 60+
   counters and zero timing fields. Add deltas around: game tick,
   fast3d submit, VDP1 emit, sync wait. Add `frt_ticks_elapsed`, `frames_rendered`
   and `gGlobalTimer` so frame rate and the tick:render ratio are single-capture
   reads. (`src/port/saturn/sourceboot/main.c:294` runs exactly one
   `game_loop_one_iteration()` per submit, so the ratio should read 1:1 —
   confirm it rather than assume it.)
3. **Soft-float microbenchmark in `src/port/saturn/hwtest/`.** The harness
   already FRT-times loops (`hwtest/main.c:163-190`). Add loops of
   `__addsf3`/`__mulsf3`/`__divsf3`/`__ltsf2`/`__floatsisf` over a static
   operand array (so nothing constant-folds), reporting cycles per operation.
   **This is the number the entire plan is missing** (diagnosis open question 1).
   Run it against both fp-bit and soft-fp to measure Stage 1a's win directly.
4. **Multi-stop PC sampling in `tools/saturn/capture_hwtest.py`.** Stop N times
   at pseudo-random intervals, record only `pc`. A few hundred samples in one
   run replaces a 34-sample opportunistic histogram, settles the 65–93% CI, and
   — critically — gives a *per-caller* profile that ranks Stage 2's candidates.
5. **Record the build's ELF hash in every capture.** §1.2 showed cross-build PC
   resolution is unsound; make it detectable.

**Exit criterion:** ≥200 PC samples on the current build, plus a measured
cycles-per-float-op figure for each operation class.

### Stage 1 — Faster float, no semantic change (the first shippable win)

**1a. Link GCC 15.2.0's soft-fp objects ahead of libgcc.** Extract
`addsf3.o subsf3.o mulsf3.o divsf3.o negsf2.o eqsf2.o nesf2.o lesf2.o gesf2.o
ltsf2.o gtsf2.o unordsf2.o floatsisf.o floatunsisf.o fixsfsi.o fixunssfsi.o
extendsfdf2.o truncdfsf2.o` from
`/d/tmp/gcc15sh/sh-gcc-toolchain/work/lib/gcc/sh-elf/15.2.0/libgcc.a`, vendor
them under a clearly-provenanced path, and place them before `-lgcc` in the
sourceboot link. **Verify** with `sh-elf-nm` on the linked ELF that
`___pack_f`/`___unpack_f`/`__fpadd_parts` are *absent* — that is the pass/fail
signal that fp-bit is gone.

Note the size trade: soft-fp's inlined routines are larger per function
(`addsf3.o` 1,436 B vs fp-bit's `___addsf3` 76 B) but eliminate the shared
`__pack_f`/`__unpack_f`/`__fpadd_parts` bodies (844 B). Net `.text` growth
should be small, but **HWRAM free space is only 135,964 bytes** and `-O2` (the
parallel workstream) is also spending it. Measure `___end` before and after;
`sourceboot-cart.x:130` will ASSERT on overflow, but only after the fact.

**If the size trade turns out badly, 1a has a cheaper sibling: 1a′.** Copy
GCC 15's 83-line `libgcc/config/sh/sfp-machine.h` (post-2025-04-19 version) into
the 14.3.0 source and flip `libgcc/config.host`'s `sh-*-elf*` line from
`t-fdpbit` to `t-softfp-sfdf t-softfp`, then rebuild libgcc only. Same
implementation, but built by the 14.3.0 compiler the project is validated
against, and it covers double as well. Option (a2) in §5.1.

**Whichever route, the symbol-override mechanism is proven.** `malucard/sm64-psx`
declares its own float routines `.weak` for exactly this purpose
(`src/port/psx/float_asm_psx.S:95,195,253,314`) and has shipped that way.

**1b. Fix `src/port/saturn/compat/sqrtf.c`.** Replace the four-iteration Newton
loop (4 `__divsf3` + 4 `__mulsf3` + 4 `__addsf3` per call, 60 static callers)
with `sm64_saturn_isqrt64` plus the existing exact exponent-bridge conversions.
**Port-owned file; zero engine risk.**

**1c. Route `sinf`/`cosf` to the Q16 trig table.** `lib/src/math/sinf.c` and
`cosf.c` compute in `double` by construction (a `du`-union `P[5]` polynomial),
so each call runs ~8 `__muldf3` + ~4 `__adddf3` + 2 conversions through fp-bit's
double path. `saturn_trig_q16.inc.c` already exists. **Coordinate with the
parallel double-promotion workstream — this is the same target seen from a
different angle, and both should not land independently.**

**Ship gate:** a free-roam capture with a measured frame rate, and the existing
`verify-all` suite green.

### Stage 2 — Targeted Q16.16 at function boundaries (ranked by Stage 0, not by this document)

Candidates, all behind a call boundary, all differential-testable, in the order
§4's precision analysis says is safest:

1. **`src/engine/surface_collision.c`** — `find_floor` (58 callers),
   `find_ceil`, `find_wall_collisions`, `find_water_level`. 41 refs. Surface
   data is already `s16`; the float work is plane evaluation and comparison —
   positions and comparisons, the case where Q16.16 is *more* precise than
   `f32` (§4). Widen the plane dot product to `int64`.
2. **`src/engine/math_util.c`** — `vec3f_normalize`, `vec3f_get_dist_and_angle`,
   `vec3f_cross`, `find_vector_perpendicular_to_plane`, `atan2s`/`atan2_lookup`
   (74 callers). 52 refs. Sum-of-squares must widen to `int64`/Q32.32.
3. **`lib/src/gu*.c`** — `guPerspectiveF`, `guLookAtReflectF`, `guNormalize`,
   `guOrthoF`, `guRotateF`. 45 refs, called once or twice per frame each — likely
   *low* dynamic weight despite `guPerspectiveF`'s 10 refs. **Only do these if
   Stage 0 says they matter.**

**§10.4 confirms this list from the outside.** The PS1 port's `src/engine/`
conversions were `math_util.c/.h`, `surface_collision.c/.h`, `surface_load.c`,
`graph_node.c/.h`, `graph_node_manager.c`, `geo_layout.c`,
`behavior_script.c/.h` — the same set, plus the render graph (which this port has
already done) and `surface_load.c` (which this port should add to the list:
`read_surface_data` and `transform_object_vertices` carry 16 refs including a
`__divsf3` and a double-precision comparison).

**Stage 2 must open by settling the format question (§11.9), not by assuming
Q16.16.** The render-matrix pipeline is committed to Q16.16 and stays; but the
one reference that converted SM64's simulation chose Q20.12, for range. Take a
capture, histogram the real operand magnitudes on each candidate path, and
decide from that.

Each conversion: one engine file, `#ifdef TARGET_SATURN`, a fixed-point
implementation in `src/port/saturn/gfx/` or a new `src/port/saturn/math/`, and a
host differential test on the `mtxq_ctor_diff_test.c` pattern. **One file per
commit, each with its own capture and its own behavioural gate (§8.3).**

Explicitly **out of scope**: `camera.c`, `mario_*.c`, `object_helpers.c`,
`behaviors/*`, `shadow.c`, `skybox.c`, `envfx_*`. That is option (b2), it is
~112 engine files by the PS1 port's measurement, and it needs its own document.

### Stage 3 — Not this workstream

Reaching 15 FPS requires ~7× beyond this workstream's ceiling. The candidates
are already ranked in the diagnosis (§5, Ranks 3–6) and by §6.1's observation
that the non-float remainder is 14,700 cycles per emitted triangle:
`-O2`, cart→work-RAM staging, skybox cost, the slave SH-2, and — not yet on
anyone's list — **reducing work volume**: draw distance, object activation
budget, and decoupling the simulation tick rate from the render rate so one
render does not cost one full SM64 tick.

---

## 8. Correctness methodology and acceptance bar

### 8.1 The existing pattern, which transfers directly

`tools/saturn/mtxq_ctor_diff_test.c` and `tools/saturn/mtxf_lookat_host_diff_test.c`
compile the **real, unmodified** `src/engine/math_util.c` on the host next to
the fixed-point replacement, feed both real captured data, and **assert**
per-entry tolerances (not print them):

```c
#define Q16_TOL_TRIG   4    /* ulps, table-identical trig      */
#define Q16_TOL_NORM   64   /* ulps (~0.001), sqrt/divide chains */
#define Q16_TOL_TRANS  1    /* ulps, exact conversions          */
```

Run by `make -f Makefile.saturn.mk verify-mtxq-ctors` /
`verify-mtxf-lookat-host-diff`. This is the pattern; it does not need
reinventing.

### 8.2 Stage 1 (options a1/a2/a3/a4) — the bar is **bit-exact**, and that is a feature

Because soft-fp preserves IEEE-754 semantics exactly, the acceptance bar is not
a tolerance at all:

- A host harness compiles the SH-2 soft-fp routines' C sources (or runs them
  under an SH-2 interpreter) against the host's native `float`, over: every
  IEEE special value (±0, ±∞, sNaN, qNaN, ±FLT_MIN, ±FLT_MAX, all subnormal
  boundaries), all 2^32 single-argument conversion inputs where feasible, and
  ≥10⁷ seeded-PRNG operand pairs per binary operation.
- **Pass = bit-identical result, including NaN payload and sign of zero.** Not
  "within tolerance". Any deviation is a bug in the replacement, full stop.
- Plus: the existing `verify-all` suite must stay green, and a free-roam capture
  must produce **identical** `triangles_emitted` / `reject_*` counters to the
  pre-change build. Identical, not similar — IEEE semantics did not change, so
  the render must be bit-identical. **Any divergence is the signal that the
  swap was not semantics-preserving**, and is the cheapest possible regression
  detector available anywhere in this plan.

This bar is *stronger* than anything currently in the tree and *cheaper* to meet,
because the specification is exact. It is the main reason option (a) goes first.

### 8.3 Stage 2 (option b1) — the bar is a justified fixed-point ulp tolerance, plus a behavioural gate

Bit-exactness is impossible for fixed point; the bar has to be derived. Derive it
this way, per conversion, and record the derivation in the test file:

1. **Representational floor.** Q16.16 ulp = 2^−16 = 1.526×10⁻⁵ world units.
   Any tolerance below that is meaningless.
2. **Comparison against `f32`'s own error at the operand magnitude.** §4: `f32`
   ulp exceeds Q16.16 ulp above magnitude 128. For a function operating on world
   positions (`find_floor`, `vec3f_get_dist_and_angle`), the tolerance should be
   stated as **"within 1 `f32` ulp at the operand's magnitude"** — i.e. the
   fixed-point result must be at least as close to the mathematically exact
   answer as the float original is. That is a *justified* bar, not a picked one,
   and for world-space work it is achievable because Q16.16 is finer there.
3. **Chain-length allowance for normalisation.** Where the computation includes
   an integer sqrt and a 64-bit divide (as `mtxq_ctor_diff_test.c`'s
   `Q16_TOL_NORM = 64` already allows), widen explicitly and document the chain
   that justifies the widening — as that file already does for its dot-product
   bound.
4. **Behavioural gate, which the unit tolerance cannot replace.** Fixed-point
   truncation biases in one direction; float rounds to nearest. Over hundreds of
   frames that is a drift, not a rounding error, and no per-call tolerance
   detects it. So each Stage 2 conversion additionally requires:
   - a deterministic free-roam capture of **N ≥ 2,000 emulated frames** before
     and after, with `gMarioState->pos`, `gMarioState->faceAngle`,
     `gCamera->pos` and `gGlobalTimer` recorded at a fixed frame;
   - the acceptance bar being **positional divergence < 1.0 world unit at frame
     N** (justified: SM64's own collision quantises to `s16` world units, so
     sub-unit divergence cannot change a collision outcome), and **identical
     `gGlobalTimer`** (i.e. no divergence in action-state transitions);
   - `triangles_emitted` and every `reject_*` counter within **±1%**.
   If divergence exceeds the bar, the conversion is rejected — not re-tuned
   until it passes.
5. **Mutation check** on every new differential test, per the project's standing
   rule: flip 3–5 comparisons/operators in the fixed-point implementation and
   confirm the test *fails* for each. A differential test that passes a mutated
   implementation is not a test.

**The PS1 port is the cautionary case for point 4, and it is not hypothetical.**
`src/game/game_init.c:151` in `malucard/sm64-psx` reads:

> `// on PS1, Mario would keep slowly walking down on neutral stick (due to imprecision of the reimplemented math?)`

— followed by an added deadzone clamp. That is precisely the accumulated-bias
failure this bar exists to catch: a neutral analogue stick producing a non-zero
magnitude through the reimplemented `sqrtf`/vector chain, expressed as
continuous unintended movement. **They did not detect it with a unit tolerance;
they detected it by playing, and they papered over it rather than fixing it.**
Their only validation mechanism was a `USE_FLOATS` build switch that reverts the
fixed-point replacements (`include/types.h:60`, commented "only for debugging my
fixed point math replacements"; `Makefile.psx.mk:77` adds "hasn't been needed for
a while!"). That is an A/B build toggle, not a differential test — **this project
already has a stronger methodology than the one reference that has done this
work**, and point 4 is what keeps that advantage real.

### 8.4 If option (a5) is taken, the bar changes

§8.2's bit-exact bar applies to (a1)/(a2)/(a3)/(a4) — anything preserving IEEE
semantics. **It does not apply to (a5)**, the relaxed-semantics route, which by
construction returns different bits for denormals, NaN, infinity and any
result requiring rounding. If (a5) is taken, its bar is §8.3's shape instead:
an explicitly derived per-operation tolerance (stated in ulps of the IEEE result,
with the discarded cases enumerated) *plus* the behavioural gate. **That is a
strictly worse testing position than (a1) and is the main reason (a1) goes
first**: it establishes a known-correct, bit-exact baseline that a later (a5)
can be diffed against.

### 8.5 What no test here can establish

Emulator evidence, not retail proof — the standing rule. And specifically:
Ymir's fidelity for SH-2 memory stalls and A-bus latency is unverified, so a
frame-rate improvement measured under Ymir is a claim about Ymir. If Ymir
under-models stalls, real hardware is slower than 2.65 s/frame today and stays
proportionally slower afterwards.

---

## 9. Interactions

### 9.1 With the two parallel cheap fixes — coordinate, do not merge blind

- **Double-promotion removal** (`sinf`/`cosf`, `-fsingle-precision-constant`)
  and **this document's Stage 1c are the same target.** `lib/src/math/sinf.c`
  must not be rewritten twice. Whoever gets there first owns it; the other
  drops the item.
- **`-Os` → `-O2`** is complementary and should land *first*, because:
  (i) it mostly attacks the 17.6% remainder and call overhead, which is where
  the residual bottleneck moves to; (ii) it changes the baseline every figure in
  §6 is measured against, so measuring Stage 1a against a `-Os` baseline and
  shipping against an `-O2` one would confuse both results;
  (iii) it may dissolve the recorded GCC 15.2.0 `-Os` miscompilation objection
  that currently blocks option (a3).

  **The PS1 port independently reached the same conclusion and wrote it down**
  (`Makefile.psx.mk:121-122`): *"despite `-Os`/`-Oz` being commonly recommended
  due to code size savings, it runs much better with `-O2`."* Their full
  soft-float-relevant flag set is worth handing to that workstream verbatim as a
  checklist — `-O2 -msoft-float -msingle-float -fsingle-precision-constant
  -mno-fp-exceptions -ffast-math -ffp-contract=fast -fno-builtin
  -fomit-frame-pointer` (`Makefile.psx.mk:121-139`). Several of those are
  independent of the `-Os` question and are not currently set in
  `src/port/saturn/sourceboot/Makefile`. **`-ffast-math` in particular needs its
  own decision** — it is not obviously safe under a Q16 bridge that depends on
  exact `__fixsfsi`/`__floatsisf` behaviour, and it is not this document's call.
- **Contention for HWRAM.** `-O2` grows `.text`; soft-fp's inlined routines grow
  `.text`; **HWRAM free is 135,964 bytes** and Rank 4's future work-RAM staging
  buffer wants the same margin. Measure `___end` after each change. Do not let
  two workstreams discover the ASSERT independently.

### 9.2 With `docs/saturn/SEGMENT_ADDRESSING_DECISION.md` (`44b4f6e`)

**No interaction.** That decision governs where `.rodata` lives and how level
data reaches the cart; this workstream is `.text`-only and changes no pointer,
no VMA and no overlay slot. The diagnosis reached the same conclusion (Rank 1,
"Interaction: none. This is `.text`-only.").

The one indirect coupling worth stating: the diagnosis measured cart traffic at
**0.8–1.9% of today's frame but 60–150% of a future 33 ms frame**. Every speedup
this workstream lands makes the cart-residency problem proportionally larger.
Stage 1 of this document moves cart staging up the priority list; it does not
change its design.

### 9.3 With `docs/superpowers/specs/2026-07-26-vdp1-textures-design.md` (`ff627ef`)

**Weak interaction, one direction, and one caution.**

- That design's milestone 1 adds a tile-index lookup through
  `saturn_fast3d_quad_map_bind` and **no new GBI decoding** — so it adds
  approximately no float work. It is not blocked by this document and does not
  block it.
- **Caution:** it budgets 333,696 bytes of VDP1 VRAM, not HWRAM, so it does not
  contend for the 135,964-byte HWRAM margin §9.1 flags — but its
  `saturn_texture_residency` machinery does live in HWRAM. Confirm before both
  land.
- **Ordering opinion:** textures make the frame *look* finished; this workstream
  makes it *arrive*. At 0.378 FPS a textured frame is still one frame every 2.6
  seconds. If the two compete for one person's time, the arithmetic goes first —
  but they do not conflict technically, and the texture work does not get harder
  by waiting.

### 9.4 With the Q16 render-matrix pipeline

Stage 1a is transparent to it: soft-fp preserves IEEE semantics, and the Q16
path's only float contact is the exact exponent-bridge conversions
(`sm64_saturn_float_to_q16` / `_q16_to_float`), which use `__fixsfsi`/`__floatsisf`
only. Those routines get faster and stay exact.

Stage 2, if it converts `math_util.c`, will encounter
`saturn_mtxq_refresh_float_mirror` (§2): the more of the engine that reads Q16
directly, the less of the float mirror is needed, and eventually 16
`__floatsisf` calls per composition site per frame can be removed. **That is a
real second-order win from Stage 2 that the static call-site map does not
show.**

---

## 10. Prior art

Standing rule: prefer adapting battle-tested code over writing new. GPL is
permitted here, with the obligations recorded in `THIRD_PARTY_LICENSES.md` and
provenance in `docs/saturn/PROVENANCE.md` (precedent:
`Lobotomy-Software/SlaveDriver-Engine`, GPL-3.0-or-later, in
`src/port/saturn/gpl/`).

### 10.1 Findings

| Source | Pinned commit | Licence | What it establishes | Reuse mode recommended |
| --- | --- | --- | --- | --- |
| **GCC 15.2.0 `soft-fp` for sh-elf** — built locally at `/d/tmp/gcc15sh/sh-gcc-toolchain/work/lib/gcc/sh-elf/15.2.0/libgcc.a` | GCC 15.2.0 release | GPL-3.0-or-later **with the GCC Runtime Library Exception** | GCC switched the SH port off `fp-bit.c`. Objects verified: `sh2` big-endian ELF flags, single multilib, all imported helpers already present in the 14.3.0 archive | **Dependency** (link the vendored objects) or **toolchain upgrade**. The GCC Runtime Library Exception explicitly permits linking libgcc into non-GPL programs — **this is the same permission under which the project already links 14.3.0's libgcc**, so it introduces no new obligation |
| **Lobotomy `SlaveDriver-Engine`** — `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | **GPL-3.0-or-later** (`LICENSE.txt`; `README.md:19` SPDX; © 1996, 2006, 2025 Ezra Dreisbach) | **Zero runtime floating point.** Q16.16 `Fixed32` throughout; vertices `short x,y,z` widened by `F(a) ((a)<<16)` (`SLEVEL.H:115-118`, `UTIL.H:83-84`). Inline `dmuls.l`+`xtrct` fixed multiply (`UTIL.H:86-97`). **Direct SH-2 DIVU access at `0xFFFFFF00`** with 64/32 fixed divide (`UTIL.H:168-213`) and documented 37-clock latency. `WALLASM.S:105-145` is a **working GPL implementation of DIVU-latency-hiding** — it issues the divide, computes depth-cue lighting during the wait, then reads the quotient. 1024-entry integer sqrt LUT (`SQRTTAB.H`, `UTIL.C:292-305`) | **Pattern-only for Stage 1/2 as specified.** If Stage 2 needs a scheduled fixed divide or a faster integer sqrt, `UTIL.H:168-213` and `SQRTTAB.H` are **direct-copy candidates under GPL-3.0-or-later**, with the existing `src/port/saturn/gpl/` precedent |
| **Sonic Z-Treme** — `work/upstream/sonic-z-treme` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 (`LICENSE`), but see note | **Zero runtime floating point.** All 347 `toFIXED(...)` sites take literal constants and fold at compile time (`SL_DEF.H:155`). Physics tuning authored in decimal, folded to Q16.16 (`SRC/physics.h:5-28`). All transform/projection delegated to the proprietary `LIBSGL.A`; **no in-tree SH-2 assembly, no divider access** | **Behaviour study only.** Confirms the destination and the conventions; contains no transform, sort, clip or divider code to learn from. **Its `VecNormalize` (`ZT_VECTOR.c:3-20`) is a worked example of the overflow trap §4 warns about** — three `slMulFX` products summed into one `FIXED`, overflowing above ~±181 units, unguarded. Do not copy that shape |
| **Jo Engine** — `work/upstream/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` | **MIT** (`LICENSE`, © 2024 Johannes Fetz) | A **permissively-licensed** Saturn Q16.16 library: `jo_fixed_mult` is inline `dmuls.l`/`sts`/`xtrct` (`jo_engine/math.c:57-70`); `jo_fixed_dot` is a three-term `mac.l` dot product (`math.c:72-88`); `jo_fixed_div` drives the SH-2 hardware divider directly via `DVSR`/`DVDNTH`/`DVDNTL` (`math.c:109+`); `jo_fixed_sqrt` is a bit-by-bit integer sqrt (`math.c:363+`) | **Direct-copy candidate for Stage 2 primitives, MIT — the cheapest licence in the set.** Notably, the port's `sm64_saturn_q16_mul` currently expresses the multiply as `((int64_t)a*(int64_t)b)>>16` in C and relies on GCC to lower it; Jo Engine's explicit `dmuls.l`+`xtrct` is one instruction shorter and is proven on hardware. Worth a codegen comparison |
| **Sega SGL** (documentation study only) | `SGL302J.ZIP`, SHA-256 `429d7299…4ff811`, kept outside the repo | Proprietary — **doc-study only, no code** | `FIXED` is the identical Q16.16 format (`MATH.TXT`; confirmed against the header vendored in Sonic Z-Treme, `SL_DEF.H:105`). `MATRIX` is `FIXED[4][3]`, affine. DIVU latency quoted at 39 clocks; DIVU state is not saved across ISRs. See `docs/saturn/SGL_REFERENCE_NOTES.md` | **Behaviour study.** Already recorded |
| **`malucard/sm64-psx`** — cloned for this study to `<repo-root>/work/upstream/sm64-psx` (outside the git repo; `.gitignore:56` covers `/work/`) | **`27d80c0b6fc0be8d3b71dfb46486c14333d28a8d`** (2026-07-26; supersedes the `3073845688ea…` pin recorded at `THIRD_PARTY_LICENSES.md:94`) | **None at this commit** — no `LICENSE`, `LICENCE` or `COPYING` at the root, verified. **No licence means all rights reserved, which is *more* restrictive than GPL, not less** | The other no-FPU SM64 port. See §10.4 — it is the single most informative reference in this document | **Behaviour lessons only. No copying, porting or close adaptation, and none is recommended anywhere in this document.** If an implementation detail ever matters, write a clean-room behaviour spec first. Note: `src/port/psx/float_asm_psx.S:1` carries the author's comment *"feel free to use or improve it"* — that is **not** a licence grant and does not override the absent LICENSE file; using it would require asking the author for explicit terms. It is MIPS assembly regardless, so it is not portable to SH-2 in any case |

### 10.2 What was searched for and not found

**No permissively- or GPL-licensed hand-written SH-2 assembly IEEE-754
soft-float exists** — searched in-tree and externally (newlib 4.2.0's
`newlib/libc/machine/sh/` is string/setjmp only, zero float symbols; musl and
libyaul both defer to libgcc; Berkeley SoftFloat and `LiraNuna/soft-ieee754` are
portable C with no SH tuning; SMX GoFast and SEGGER emFloat are commercial).
The nearest miss is **Joern Rennecke's SH IEEE-754 assembly** (2006, resubmitted
July 2010, GPLv3 + Runtime Library Exception, `gcc/config/sh/IEEE-754/`) which
**was never merged**, is absent from GCC master and from the local 15.2.0 tree,
targeted SH3/SH4, and is recoverable only from 2010 mailing-list attachments.
Other GCC ports (`arm/ieee754-sf.S`, `arc/ieee-754/`, `xtensa/ieee754-sf.S`) do
have hand-written float assembly; **SH is one of the ports that never got it.**

SlaveDriver and Jo Engine both solve the problem by *not using floats*, not by
making floats fast. Neither Sonic Z-Treme nor SGL contains a soft-float at all.
That is itself a finding: **the shipping Saturn ecosystem's answer to "no FPU"
was universally fixed point, never a faster soft-float.** Option (a) is
recommended here despite that, because it is the only option that helps the
2,700 engine-tree call sites nobody is going to convert — and because §10.4
shows the one team that *did* do the fixed-point conversion kept a soft float
permanently anyway.

### 10.3 Provenance obligations if the recommendation is taken

Record in `docs/saturn/PROVENANCE.md` before merging Stage 1a: GCC 15.2.0,
its licence and **the GCC Runtime Library Exception under which the objects are
linkable**, the exact object list vendored, the source archive path, and reuse
mode "dependency". If Stage 2 copies from Jo Engine (MIT) or SlaveDriver
(GPL-3.0-or-later), the same ledger entry plus retained copyright headers,
licence text and change notices — following the `src/port/saturn/gpl/` precedent.

**Also update `THIRD_PARTY_LICENSES.md:94` and
`SEGMENT_ADDRESSING_DECISION.md:690`**, both of which pin `malucard/sm64-psx` at
`3073845688ea273da78d539b20c45110d8a868c3`. The repository is now cloned at
`<repo-root>/work/upstream/sm64-psx` at
`27d80c0b6fc0be8d3b71dfb46486c14333d28a8d`, and the licence status is unchanged
(none). Its ledger entry should read **behaviour-lessons-only, no copying**, for
the reason in §10.1.

### 10.4 `malucard/sm64-psx` — the reference that answers the crux question

Cloned for this study at commit `27d80c0b6fc0be8d3b71dfb46486c14333d28a8d`
(2026-07-26), to `<repo-root>/work/upstream/sm64-psx`, outside the git repo.
**No licence file exists at this commit — all rights reserved. Everything below
is a behaviour lesson read from the source. Nothing in this document recommends
copying, porting or closely adapting any of it, and the reader should not.**

Why it matters: the PlayStation's R3000A has **no FPU either**. The same
float-heavy SM64 engine, the same problem, on a comparable no-FPU 33 MHz scalar
RISC. Whatever they chose is evidence about what survives contact with reality.

**1. Did they modify the engine tree, or intercept at a boundary?**

**They modified the engine tree, extensively. There was no boundary
interception, and no shortcut was found.** Counting occurrences of their
fixed-point vocabulary (`q32`, `Vec3q`, `ftoq`, `qtof`, `qmul`, `qdiv`, `QONE`,
`FRACT_BITS`):

| directory | occurrences | files |
| --- | ---: | ---: |
| `src/game/` | 1,040 | **98** |
| `src/engine/` | 192 | **11** |
| `src/menu/` | 12 | 3 |
| `include/` | 31 | 3 |
| `src/port/` | 46 | 6 |
| `src/audio/`, `lib/`, `levels/`, `actors/` | **0** | 0 |

**~112 engine-tree files.** The 11 `src/engine/` files are
`math_util.c/.h`, `surface_collision.c/.h`, `surface_load.c`, `graph_node.c/.h`,
`graph_node_manager.c`, `geo_layout.c`, `behavior_script.c/.h` — **which is
exactly this document's Stage 2 candidate list, plus the render graph.** That is
independent confirmation of the ordering in §7, and independent confirmation
that going further means `src/game/`.

Note also what they left alone: **`src/audio/`, `lib/`, `levels/` and `actors/`
have zero fixed-point content.** Cold and data-only trees stayed float.

**2. Representation, and where they drew the line.**

`include/types.h:16-23`:

```c
#define FRACT_BITS 12 // this can be adjusted to adjust precision (has to be an even number)
#define QONE ((q32) (1 << FRACT_BITS))
typedef s16 q16;  typedef s32 q32;  typedef s64 q64;
```

**Q20.12, not Q16.16** — and adjustable, constrained to an even number so the
square root halves cleanly. Matrices are a *different* format again: a
`ShortMatrix` of `s16 m[3][3]` (`types.h:66+`), i.e. 16-bit rotation with
translation carried separately, matching the PS1 GTE's native shape. The README
calls this "the 16-bit integer vectors and matrices that are standard on PSX."

The line was drawn at **hot simulation and geometry in fixed point; audio, asset
data and cold paths left in float**; and float never removed, because a
hand-written soft float remained (item 3).

**Direct relevance to this port's §11.9.** Q20.12 has an ulp of 2.4×10⁻⁴ —
16× coarser than Q16.16 — but a range of ±524,288 against Q16.16's ±32,768.
**They chose range over precision.** §4 identified the magnitude ceiling as
Q16.16's real hazard for SM64 (squared distances overflow immediately at
±32,768), so this looks like the same hazard being answered differently. It does
not overturn this project's Q16.16 choice — the render-matrix pipeline is
committed, SGL and both Saturn engines use Q16.16, and §4 shows Q16.16 beats
`f32` above magnitude 128 — but it is a real data point that **Stage 2 should
settle the format question against captured value ranges rather than inherit
Q16.16 by default.**

**3. `f32`/`Vec3f` typedef strategy — the question with the most valuable
negative answer.**

**They did not swap the typedefs.** `include/PR/ultratypes.h:18` is still
`typedef float f32;` and `include/types.h:115` is still `typedef f32 Vec3f[3];`.
Instead they added a *parallel* type system (`q32`, `Vec3q`, `ShortMatrix`) with
explicit bridges (`vec3f_to_vec3q`, `vec3q_to_vec3f`, `vec3q_to_vec3s` in
`src/engine/math_util.c:124-165`) and converted call sites by hand. Their
`math_util.c` grew a `vec3q_*` mirror alongside every `vec3f_*` — the same
shadow-mirror shape this port already uses for `gMatStackQ`, but at far greater
extent.

**The cheap interception point does not exist.** That is the single most useful
thing this reference establishes, and it is established negatively: the team
that had every incentive to find it did not find it.

They did make the conversion *macros* type-safe rather than raw, which is a
transferable idea — `q(x)`, `qmul(x,y)`, `qdiv(x,y)` and `qtrunc(x)`
(`types.h:42-56`) use `__builtin_types_compatible_p` to fail the build on a
wrong-typed operand, and `q(x)` uses `__builtin_constant_p` to fold constants at
compile time instead of calling the runtime converter. That pattern is worth
adopting in this port's own kernels; it is a *technique*, and this document
describes it rather than reproducing their code.

**4. Did they keep a soft float? Yes, permanently.**

`src/port/psx/float_asm_psx.S` — hand-written MIPS assembly implementing
`__addsf3`, `__subsf3`, `__mulsf3`, `__divsf3` and a **single shared entry point
for all six comparisons** (`__eqsf2: __nesf2: __ltsf2: __lesf2: __gtsf2:
__gesf2:` on one line), plus `qtof`/`ftoq`. All declared `.weak` so libgcc's are
overridden — **the same symbol-override mechanism Stage 1a assumes, confirming
it works in practice.** Semantics are relaxed, not IEEE: the C reference version
in `src/port/float_math.c` has no NaN or infinity path.

Every routine documents its cycle count per input class (reproduced in §6.2).
That is the calibration this document's option (a4)/(a5) estimates rest on.

**5. What frame rate did they achieve?**

**Not established.** The repository has a runtime profiler
(`gShowProfiler`, toggled from the controller at
`src/port/psx/controller_psx.c:312`) and a `make BENCH=1` benchmark mode that
boots directly into a level, but **no frame-rate figure is recorded in the
README, in `CHANGES`, or in the source.** Anyone wanting the number would have
to build and run it. The README's stutter complaint is attributed to something
else entirely: *"Textures are loaded individually, causing long stutters and
loading times"* — an I/O problem, not an arithmetic one.

**So the reference does not tell us what this workstream is worth in FPS.** It
tells us what the work costs and where the boundaries are.

**6. What they gave up.**

- **A physics bug they papered over rather than fixed** — `src/game/game_init.c:151`,
  quoted in §8.3: Mario walking slowly downward on a neutral stick, attributed
  in their own comment to "imprecision of the reimplemented math", fixed by
  adding a deadzone clamp.
- **Validation was a build switch, not a differential test.** `USE_FLOATS`
  (`include/types.h:60`, `Makefile.psx.mk:77`) reverts the fixed-point
  replacements; the Makefile comment says it "hasn't been needed for a while!".
  This project's `verify-mtxq-ctors` harness is a stronger instrument.
- A long list of accepted breakage in the README's own "Known issues":
  crashes entering certain levels, ending sequence crashes, some animations
  crash the game, camera uncontrollable in many levels, pause menu non-functional,
  stretched textures, tessellation insufficient for large polygons. **Not all of
  that is arithmetic** — but it is the honest shape of what a port at this stage
  of a full fixed-point conversion looks like.

**7. What this changed in this document — stated explicitly rather than quietly
revised.**

| # | Change | Why |
| --- | --- | --- |
| 1 | **Added option (a5)** — relaxed-semantics soft float at the IEEE bit layout (§5.1, §5.4) | §5.4 previously rejected "reinterpret the float ABI" as a single idea. The PS1 port shows the *format* objection and the *semantics* objection are separable, and shipped the separable half |
| 2 | **Added §8.4** — the acceptance bar changes under (a5) | (a5) forfeits bit-exactness by construction; that needed saying, and it is now an argument for doing (a1) first |
| 3 | **Recalibrated (a4)/(a5) from "guess 4–8×" to "derived 5–8×"** (§6.2) | Their documented per-operation cycle counts on a comparable CPU are a real external reference point |
| 4 | **Hardened the §5.2 (b2) cost estimate from "months, ~40 files" to "~112 engine files"** | Measured from their tree, not estimated from ours |
| 5 | **§9.1 now carries their `-O2` finding and flag list**, including a flag `-ffast-math` this port does not set and should decide on deliberately | Independent confirmation of the parallel workstream's direction, plus a checklist it may not have |
| 6 | **§11.9 (format) upgraded from a footnote to a decision Stage 2 must make** | Q20.12 vs Q16.16 is a live disagreement between references, not a settled convention |
| 7 | **§8.3's behavioural gate is no longer hypothetical** | Their neutral-stick drift bug is the exact failure mode it is designed to catch, observed in the wild |

**What it did not change: the recommendation, the staging, or the ceiling.**
Option (c) — faster soft float first, targeted fixed point second, full
simulation conversion not recommended — is what the PS1 port converged on
independently. The 2.14 FPS Amdahl ceiling (§6.1) is a property of this port's
own measurements and is untouched by anything in their tree.

---

## 11. Open questions

1. **What does one soft-float operation actually cost, in cycles, on this
   target?** Unmeasured. Static instruction counts are measured; the executed
   path is derived; cycles are neither. **Stage 0.3 settles it, and until it
   does, the whole of §6.2 past Stage 1a is guesswork.** This is the single
   most valuable hour of work available.
2. **How much faster is soft-fp than fp-bit on SH-2, for this operand mix?**
   ~3× is upstream's Whetstone figure, not a measurement on this workload.
   Stage 0.3's microbenchmark run against both archives answers it in one
   capture, *before* any link change.
3. **Does the recorded GCC 15.2.0 `-Os` miscompilation
   (`saturn_matrix_kernels.h:12-15`) survive `-O2`?** Gates option (a3). Cheap
   decisive test: build the `verify-mtxf-lookat-host-diff` inputs through a
   GCC-15 `-O2` target build and compare. Note that (a1) and (a2) sidestep the
   question entirely — the 14.3.0 compiler still compiles the engine.
4. **What is the current build's real profile?** Seven deduplicated samples,
   one of them a VDP1 wait (§1.2). Stage 0.4 settles it, and it is what should
   rank Stage 2's candidates. **Do not start Stage 2 on the strength of the
   static map in §3.**
5. **How much of the frame is VDP1 wait on the current build?** The one sample
   from 2026-07-26 landed in `_vdp1_sync_wait`. If that is representative rather
   than luck, §6.1's ceiling is lower than 2.14 FPS, and the fill-rate
   hypothesis the diagnosis refuted (on 2026-07-22 data) needs re-testing on
   current data.
6. ~~What did `malucard/sm64-psx` do about the R3000A's lack of an FPU?~~
   **Answered — §10.4.** Both: a hand-written relaxed soft float *and* a Q20.12
   fixed-point conversion spanning ~112 engine-tree files, with no typedef swap
   and no boundary interception. **The residual question is their frame rate**,
   which is not recorded anywhere in their repository and would require building
   and running their benchmark mode (`make BENCH=1`) to obtain. Without it, the
   reference tells us what the work *costs*, not what it *buys*.
7. **Is one SM64 tick really one rendered frame?** `main.c:294` runs exactly one
   `game_loop_one_iteration()` per iteration, so it should be 1:1 — but
   `gGlobalTimer` has never been captured. Stage 0.2 confirms it. If it is not
   1:1, every per-tick cost in §3 and §6 is proportionally wrong.
8. **Does linking soft-fp ahead of libgcc actually displace fp-bit, or does the
   linker pull both?** Should work (archive member selection is
   first-definition-wins), but it is an assumption. The `sh-elf-nm` check in
   Stage 1a is the verification, and it must be a build-time assertion, not a
   one-off manual check — the same discipline `sourceboot/Makefile:196` already
   applies to the cart VMA.
9. **Is Q16.16 the right target format for Stage 2 — a decision, not a
   footnote.** The Saturn precedent is unanimous (SGL, SlaveDriver, Jo Engine
   and this port's own render-matrix pipeline all use Q16.16) — but **the only
   reference that actually converted SM64 chose Q20.12** (§10.4), 16× coarser
   with 16× the range, and made the fraction width a single tunable constant.
   SM64's world spans ±8,000 units while its velocity deltas are ~10⁻², and §4
   identifies the magnitude ceiling — not the ulp — as Q16.16's real hazard.
   **Stage 2 must settle this against captured value ranges from a real
   free-roam run, not inherit Q16.16 by default.** The render-matrix pipeline is
   already committed to Q16.16 and should stay there; the question is whether
   the *simulation-adjacent* conversions (`surface_collision`, `math_util`
   distance work) want a different width, and if so, what the bridge between the
   two costs.
10. **Should the port adopt a type-checked conversion macro layer?** §10.4's
   `q(x)`/`qmul`/`qdiv`/`qtrunc` pattern uses `__builtin_types_compatible_p` to
   make a wrong-typed operand a build error and `__builtin_constant_p` to fold
   constant conversions at compile time. This port's kernels currently take raw
   `int32_t` with the Q-ness tracked only by naming convention and comments —
   which is exactly how a Q16.16 value gets multiplied by a raw integer without
   anyone noticing. Cheap to add; worth deciding before Stage 2 grows the
   surface.
11. **Is `-ffast-math` safe for this port?** §9.1 notes the PS1 port uses it.
   This port's exact float↔Q16 bridge depends on precise `__fixsfsi`/
   `__floatsisf` behaviour and on bit-level exponent manipulation; `-ffast-math`
   licenses transformations that could interact with that. Not this document's
   call, but it should be an explicit decision rather than a flag copied across.
