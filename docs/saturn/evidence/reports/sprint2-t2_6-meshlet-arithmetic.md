# Sprint 2 Task T2.6 — the meshlet depth walk, fixed

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `bbe08c59`.
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.6 — executing items 1 and 2 of
  `sprint2-t2_5-prepare-mario-audit.md` §9.
- T2.5 measured `actor_meshlet_live_depth_bounds` at **20.7% of the whole
  frame** and named two independent causes. Both are fixed here.

## Headline

**Sustained cadence is 1.4634 FPS against T2.3's 1.0866 — +34.68%, and
14.22 VBlanks off a 55.22-VBlank frame. The depth walk went from 3,725.8
cycles per position visit to 102.9, and the equivalence is bit-identical.**

- The pre-notification window fell from **60,475.6 to 20,756.6 FRT ticks
  (−65.68%)**; `demo_prepare_mario` fell from 41,895.4 to **2,067.1 ticks
  (−95.07%)**, i.e. from 69.28% of the window to 9.96%.
- `meshlet_depth_emit` reads **0.0 ticks and 0 calls** — step 1, measured.
  `meshlet_depth_admit` fell 20,491.7 → **1,132.3 ticks (−94.47%)** — step 2,
  measured. The two fixes are attributable separately from one build.
- **102.9 cycles per tier-0 position visit** against T2.5's 3,725.8 — a
  **36.2× reduction**, landing inside T2.5's predicted "defensible ~100–150".
- **Equivalence: bit-identical.** 685,456 swept cases, **zero** divergences,
  therefore zero LOD-tier and zero painter-bin changes. Six mutation kills
  against a surviving control. There is no visual-risk item for the owner.
- Every gate passes. `verify-memory-map` **OK** on both builds. Zero SH-2
  exceptions across 26,181 emulated frames; all twelve profiler acceptance
  checks pass and the harness exits 0.

## 1. The four commits

| SHA | Subject |
| --- | --- |
| `b5733174` | `test(actor)`: pin depth-arithmetic equivalence before the T2.6 swap |
| `41e4e223` | `perf(actor)`: carry pass 1 depth bounds into the meshlet emission pass |
| `f4f8ad6b` | `perf(actor)`: replace the depth walk divide-based saturating arithmetic |
| `dfe41b57` | `fix(actor)`: keep T2.6 comments out of the source-text body extractor |

Two source files change: `src/port/saturn/gfx/saturn_actor_meshlets.c` and
`tools/saturn/actor_meshlet_test.c`, plus one `-D` on each of the two
`verify-actor-meshlets` compile lines in `Makefile.saturn.mk`. Nothing else
in the port is touched. `dfe41b57` is comment text only and is proven
byte-identical at object level (§7), so the measurements below describe the
final tree.

## 2. Were the two passes really input-identical?

**Yes.** This was step 1's premise, so it was confirmed against the code
rather than inherited from T2.5's measurement.

`actor_meshlet_core` receives `source`, `transform` and `view` as `const`
pointers and never writes through them. Between the admission loop and the
emission loop the only state that changes is the `opaque_cursor` /
`translucent_cursor` prefix sums and their offsets, `position_cursor`, and
`memset(position_seen, 0, source->vertex_count)` — all function locals or the
de-duplication bitmap.

None of that aliases what the depth walk reads. The walk reads exactly four
things: `transform->vertices` (the selected pose table),
`transform->{position_q16, scale_q16, yaw}`, `view->{camera_position_q16,
view_forward_q16}`, and the meshlet/position-reference tables reached through
`source`. On the Mario path the pose table is `const` data in `.cart_rodata`
and `position_seen` is a stack array in the prepare entry point; on the bank
path `actor_bind_workspace_with_stride` lays `pose_work.vertices` and
`output.position_seen` at distinct, non-overlapping cursors in the same arena.
No aliasing exists in either.

Nor can another CPU intervene: the Mario entry point runs on the master
inside the pre-notification window, and the bank entry point works entirely
inside its own claimed workspace lane.

T2.5's 20,491.7 vs 20,491.9 ticks is therefore corroboration of a structural
fact, not the fact itself. **The premise holds; nothing invalidated it.**

## 3. Step 1 — carry pass 1's bounds into pass 2

### The change

Pass 2 called `actor_meshlet_live_depth_bounds` again for every meshlet,
discarded the return value with a `(void)` cast, and re-derived the same
bounds pass 1 had computed and thrown away. Pass 1 now stores its result and
pass 2 reads it.

### Carry design, placement and margin

```c
#define ACTOR_MESHLET_DEPTH_CARRY_CAPACITY 64U

typedef struct actor_meshlet_depth_carry {
    const int16_t (*vertices)[3];   /* pose table identity */
    uint32_t generation;            /* view->generation    */
    uint16_t meshlet_count;
    uint16_t valid;
    actor_meshlet_depth_bounds_t bounds[ACTOR_MESHLET_DEPTH_CARRY_CAPACITY];
} actor_meshlet_depth_carry_t;

static actor_meshlet_depth_carry_t s_mario_depth_carry;
```

- **Size: 524 bytes** (`0x20C`, confirmed by `sh-elf-nm -S`) — 64 × 8 B of
  `{nearest_q16, furthest_q16}` plus a 12-byte stamp. T2.5 sized the naive
  carry at 868 B because it carried the span too; the span is not carried,
  because `actor_meshlet_span` is four table reads inside pass 2's 258.6-tick
  self time and was never the cost.
- **Placement: HWRAM `.bss`.** `_s_mario_depth_carry` links at `0x060DE0F8`,
  inside `.bss` at `0x06092DA0 + 0x679F0`. Per the memory doctrine this is
  per-frame-hot state: HWRAM, not `.lwram_bss`, not the cart. It costs no ISO
  bytes.
- **Margin:** `verify-memory-map` **RESULT OK** on both builds. Product
  `hwram_remaining = 0x5078` (20,600 B) against the `0x1F00` floor — **12,664 B
  of true slack**. Diagnostic `hwram_remaining = 0x484C` (18,508 B) —
  **10,572 B of slack** against T2.5's 11,756 B, so the whole changeset costs
  **1,184 B of HWRAM** in the diagnostic build (524 B carry + ~660 B of code)
  and LWRAM is untouched at `0x17440`.
- **`_Static_assert(SM64_MARIO_MESHLET_COUNT <= 64)`** so a future mesh with
  more meshlets is a build error, not a silent fall back to the slow path.

### Guard

The carry is stamped at the top of pass 1 with `view->generation`,
`source->meshlet_count` and the pose vertex-table pointer; pass 2 checks all
three plus `meshlet < carry->valid` before serving, and recomputes on any
mismatch.

This is **belt-and-braces, not load-bearing.** Nothing inside a single core
call can change any of the three, so the guard can only fire if an invariant
this code does not control has broken — and then the safe answer is a fresh
computation, never a stale bound. A source with more meshlets than the carry
holds simply keeps the old two-walk behaviour.

### Scoped to the Mario path on purpose

The bank entry point passes `NULL` and keeps recomputing.
`demo_generic_actor_process` (`saturn_demo_render.c:3129`) picks a workspace
lane from the actor claim — `lane == SM64_SATURN_ACTOR_CLAIMED_MASTER ? 0U :
1U` — so the bank path can run on either SH-2 and a single shared static
would be a cross-CPU race. It is also not on the measured hot path
(`SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0`; T2.5 measured `actor_closure` at
exactly zero). A per-lane carry is possible later if the generic path is
enabled and profiled; it was not worth the memory or the risk today.

### Pinned as a measurement, not an argument

`sm64_saturn_actor_meshlets_prepare_recompute` — test-only, behind
`SM64_SATURN_ACTOR_MESHLET_DEPTH_REFERENCE` — is the shipped Mario entry
point with the carry withheld, i.e. the pre-T2.6 two-walk behaviour. The host
test requires the two to produce **byte-identical** output (accept flag, all
three counts, the full opaque and translucent record arrays, the position
stream, the whole `sm64_saturn_fast3d_profile_t`) across 4,000 randomised
cases spanning all three pose banks, four output capacities, and a `z`
distribution biased across the LOD-tier thresholds and the behind-camera cull.

## 4. Step 2 — de-divide the algebra

### What was wrong

`actor_saturating_mul_i64` checks overflow by **dividing**. The SH-2 has no
64-bit divide, so each call emitted a libgcc `___divdi3`. T2.5 counted ten per
position visit and 1,408 visits per frame — **~14,080 software divisions per
frame** — and established that this helper is the only 64-bit-division caller
on any hot path in the entire linked image. Three of the ten were a multiply
by 2^16. The loop also recomputed per vertex what is per-actor algebra, and
recomputed the yaw sine/cosine once per meshlet from a yaw that cannot change
within the frame.

### The identity — and a correction to T2.5's Finding B

T2.5 wrote the hoist as
`depth(v) = dot(actor_pos − camera_pos, forward) + dot(S ⊙ v, R_yawᵀ · forward)`
and called it "an identity, not an approximation". **That form is not exact.**
The reference floors twice — once quantising the rotated vertex to integer
world units, once per axis after the Q16 product — and a floor does not
distribute over a sum. Implementing T2.5's form literally would have been a
silent numeric change.

The correct identity is narrower and does hold:

```
term_a = ((B_a + (q_a << 16)) * F_a) >> 16          [what the reference computes]
       = floor( B_a*F_a / 2^16  +  q_a*F_a )
       = ((B_a * F_a) >> 16) + q_a * F_a            [because q_a*F_a is an integer]
```

`B_a = P_a − C_a` is per-actor, so `SUM_a ((B_a * F_a) >> 16)` is a per-actor
constant and only the `q_a * F_a` terms are per-vertex. Separately, under unit
scale the reference's two successive `>>16` narrowings compose into one `>>32`,
and since `scaled = v << 16` exactly, the 2^16 factors out of the numerator and
leaves a single `>>16` over 32×32 products.

The oracle evaluates **both sides** of that pull-out and requires them to
agree, so it constrains the identity rather than restating it. Breaking the
pull-out in the model kills 264,252 of 268,816 in-domain cases (§6).

### The shape that ships

A per-actor `actor_depth_kernel_t` is prepared once per core call — not once
per meshlet, not once per vertex — holding the trig, the forward vector and
`base_depth`. The per-vertex body becomes:

```c
    const int32_t rotated_x = (int32_t)(
        (actor_widen_mul(vertex[0], kernel->cosine) +
         actor_widen_mul(vertex[2], kernel->sine)) >> 16);
    const int32_t rotated_z = (int32_t)(
        (actor_widen_mul(vertex[2], kernel->cosine) -
         actor_widen_mul(vertex[0], kernel->sine)) >> 16);
    return kernel->base_depth +
        actor_widen_mul(rotated_x, kernel->forward_q16[0]) +
        actor_widen_mul(vertex[1], kernel->forward_q16[1]) +
        actor_widen_mul(rotated_z, kernel->forward_q16[2]);
```

`actor_widen_mul` is written as a widening 32×32 → 64 product so GCC emits a
single `dmuls.l` — the primitive `saturn_q16_sh2.h`'s
`sm64_saturn_q16_mul_sh2` is built on — rather than a `__muldi3` call. This
is the in-repo pattern `matrix_apply` (`saturn_actor_pose.c:31-56`) already
uses: an `int64` accumulator over `int16 × Q16` products narrowed once by a
shift, with no division and no saturating helper.

**Confirmed in the linked image, not inferred.** Disassembling
`_actor_meshlet_live_depth_bounds` (`0x0607017C`, 912 B) and splitting at the
`kernel->fast` branch:

| Region | Instructions | `jsr` | `dmuls.l` |
| --- | ---: | ---: | ---: |
| Fast path `0x6070262`–`0x60703B0` | 167 | **0** | **9** |
| Saturating fallback `0x60703B0`+ | — | 15 | 0 |

Zero `___divdi3`, `___udivdi3` and `__muldi3` references appear anywhere in
the function. The 15 `jsr`s to `_actor_saturating_mul_i64` /
`_actor_saturating_add_i64` all sit past the branch the Mario path never
takes. The only per-vertex call left on the fast path is
`_actor_position_ref`, which is T2.5's Finding E and was never material.

### The preconditions, and why they exist

The identity is exact only while no saturating helper in the reference would
actually have saturated — otherwise the reference clamps where the fast form
wraps. Rather than re-checking overflow per operation (the thing that was
expensive), the safety argument is hoisted to **one per-actor test**:

| Precondition | Bound it buys |
| --- | --- |
| `scale_q16[a] == 65536` (unit) | `scaled = v << 16`, so `\|scaled\| <= 2^31` |
| `\|sin\|, \|cos\| <= 2^16` | `\|v·trig\| <= 2^31`, rotation sum `<= 2^32`, `\|q\| <= 2^16` |
| `\|position_q16[a]\| <= 2^40` | with `\|camera\| <= 2^31` by type, `\|B\| < 2^41` |
| `\|view_forward_q16[a]\| <= 2^20` | `\|B·F\| <= 2^61`, `\|relative·F\| <= 2^62` |

Every product therefore stays inside `int64`, and under no-overflow the
saturating helpers are exactly addition and multiplication — their `0`, `-1`
and `INT64_MIN` special cases are exact too. Outside the preconditions
`actor_depth_reference` — the verbatim pre-T2.6 body — still runs. **That is
why it was kept rather than deleted:** the generic bank path admits arbitrary
per-axis scales and does not qualify, so `actor_saturating_mul_i64` and its
`___divdi3` remain in the image, off the hot path.

The bounds are generous relative to the data: SM64 world coordinates are about
±2^15, so `position_q16` reaches about 2^31 against a 2^40 limit, and
`view_forward_q16` is a unit Q16 direction bounded by 2^16 against a 2^20
limit.

## 5. Equivalence — **bit-identical**

The oracle runs three independent statements per case: the pinned reference,
whatever the file actually ships, and a model written from the algebraic
identity. `verify-actor-meshlets` reports, verbatim:

```
depth equivalence: 685456 cases, 268816 hoist-domain, 416640 saturating fallback, 0 divergences
depth carry: 4000 cases, carried emission == fresh recompute
actor meshlet fixture: PASS
actor meshlet invalid-span mutation caught by fixture
```

- **685,456 cases, zero divergences.** Not "small differences" — zero.
  Therefore **zero LOD-tier changes and zero painter-bin changes**, which is
  the consequence the owner would otherwise have had to adjudicate. Divergence
  reporting is built to quantify: the harness counts differing cases, the worst
  `|delta|` in Q16, and how many cross an `actor_lod_tier` or
  `actor_depth_bin` boundary. All four are zero. **No visual-risk item.**
- **268,816 cases go through the new divide-free kernel**, 416,640 through the
  saturating fallback. The test asserts `fast_cases == model_cases`, so a sweep
  that silently fell back everywhere — and thus compared the reference against
  itself — fails rather than passes. It also asserts the shipped path's
  precondition decision matches the test's mirror in **both** directions, so
  drift between the code and its documented domain is a failure.
- The fallback cases are deliberately the awkward ones: `INT64_MAX`,
  `INT64_MIN`, `INT64_MAX/2`, `2^40` and `2^40 + 1` positions; `INT32_MAX` and
  `INT32_MIN` cameras and forwards; zero, `1`, `65535`, `INT32_MAX`,
  `INT32_MIN` and negative scales; `INT16_MIN`/`INT16_MAX` vertices on every
  axis and sign. These are exactly what the divide-based overflow check existed
  to handle, and the shipped path still agrees with the reference there — by
  falling back to it.
- The in-domain cases are the domain that ships: real Mario pose vertices drawn
  from both animation banks, BOB-scale world and camera coordinates, and Q16
  forwards from the same sine table the runtime uses.
- The file's **pre-existing pinned Mario output hashes are unchanged** across
  all four commits — an end-to-end statement of the same claim that predates
  this task and could not have been tuned to it.

## 6. Mutation proofs

Applied to the working tree, run, reverted. **None is committed.**

### The oracle itself (`b5733174`)

| # | Mutation | Result | Signal |
| --- | --- | --- | --- |
| MV1 | reference `rotated_x` shift 16 → 15 | **KILLED** | 253,619 / 268,816 in-domain cases break the identity |
| MV2 | model quantisation shift 32 → 31 | **KILLED** | 253,619 / 268,816 |
| MV3 | model floor pull-out broken | **KILLED** | 264,252 / 268,816 |

MV1 is the important one: it perturbs the *reference*, which the candidate
also called at that commit, so reference-vs-candidate could not have caught
it. The model did. That is what makes the third statement non-vacuous.

### The carry (`41e4e223`)

| # | Mutation | Result | Caught by |
| --- | --- | --- | --- |
| MC1 | carry index pinned to meshlet 0 | **KILLED** | pinned output hashes |
| MC2 | carry off-by-one (`meshlet − 1`) | **KILLED** | pinned output hashes |
| MC3 | stored bounds swapped | **KILLED** | fixture crash |

MC3 is recorded honestly: it is caught by a **segfault**, not an assertion.
Swapping the bounds makes pass 2 select a different LOD span than pass 1
counted, so `opaque_cursor[bin]++` overruns the record array. That is a
pre-existing structural assumption — pass 2's spans must match pass 1's —
which the carry *strengthens*, since a carried bound cannot diverge from the
one that was counted.

### The fast kernel (`f4f8ad6b`)

| # | Mutation | Result | Signal |
| --- | --- | --- | --- |
| M1 | position precondition dropped | **KILLED** | 4,661 differ, worst \|delta\| 8.44e14 Q16, **2,447 cross an LOD tier, 2,494 cross a painter bin** |
| M2 | rotation shift 16 → 15 | **KILLED** | 253,619 differ |
| M3a | dot-product forward axes swapped | **KILLED** | pinned output hashes |
| M3b | rotation sine/cosine swapped | **KILLED** | 247,445 differ, 10,533 cross a bin |
| M3c | `rotated_z` sign flipped | **KILLED** | pinned output hashes |
| M4 | per-actor `base_depth` shift 16 → 17 | **KILLED** | pinned output hashes |
| — | **control**: multiply the `vertex[1]` term by 1 | **SURVIVED** | as required |

M1 answers "does the precondition actually do anything": removing one bound
produces 2,447 LOD-tier changes and 2,494 painter-bin changes. The saturation
cases are real, the fallback is load-bearing, and the harness sees them. The
surviving control shows the harness is not simply failing on any edit.

## 7. Builds and identity

Both builds used the same 27-variable invocation as
`sprint1-stage1-link-smoke.md` / T2.2 / T2.3 / T2.4 / T2.5 (pool 208), via
`tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`,
sourcing `../../.yaul.env` then `unset COMPILER_PATH`. **The g15
package-staleness cascade did not fire; no repair was needed.** One attempt
each, exit 0.

### Diagnostic build — `SATURN_DIAGNOSTIC_MODE=2`

`tools/saturn/profiles/sourceboot-bob-demo-v1.json` had `diagnostic_mode`
flipped 0 → 2 as an **uncommitted, byte-canonical (LF) edit**, reverted
immediately after the build — T2.1's established precedent. Verified reverted:
`git diff` on that file is empty and it reads `"diagnostic_mode":0`.

Sealed identity **`id-d2a4896c541ba56b`**.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,992,100 | `27803d6106549529ca532e2d1034bd67abe6be773fc7044ed6015f57985204ce` |
| `sm64-saturn-sourceboot-e2.iso` | 5,171,200 | `17c4c1448b774b2235eb9d857e02054c4ba138942132b6c6d85b486e4dd04c45` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `2dc48833d5141a52acffa18057b4897706a8e870b7bcf07d31f5be8fbe7e568d` |

```
verify-memory-map: .../e2-bob-identity-id-d2a4896c541ba56b/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB7B4
  hwram_remaining = 0x484C bytes (required >= 0x1F00)
  lwram_end       = 0x002E8BC0
  lwram_remaining = 0x17440 bytes (floor >= 0x4000)
  RESULT          = OK
```

### Product build — `SATURN_DIAGNOSTIC_MODE=0`

Sealed identity **`id-6eca5970628d581d`**, label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg6eca5970628d`.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,975,488 | `1b4ff08d763519e71c075d41b04ad9308f1fd56c3761639bb82d8a2ddc260ad4` |
| `sm64-saturn-sourceboot-e2.iso` | 5,169,152 | `69ca844f5635dac90ff68871b00cd614b7fe9df1dd4f43dcff5b94d1a3e7fff1` |
| `sm64-saturn-sourceboot-e2.cue` (88 B) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,270 | `8622d87a5124e61f4f9d6691e43c2eb686bd2f58095876ad99befa99fa5dff4a` |

```
verify-memory-map: .../e2-bob-identity-id-6eca5970628d581d/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FAF88
  hwram_remaining = 0x5078 bytes (required >= 0x1F00)
  lwram_end       = 0x002E89E0
  lwram_remaining = 0x17620 bytes (floor >= 0x4000)
  RESULT          = OK
```

### The measured builds correspond to the final tree

Both builds were produced from `f4f8ad6b`; `dfe41b57` landed afterwards. It
changes only comment text, and that is **proven**, not asserted: the
translation unit was compiled at `SATURN_DIAGNOSTIC_MODE=0` with `-g0` using
the product build's **own** command line lifted verbatim from that build's
log, once before and once after `dfe41b57`, and the two objects are
**byte-identical** — 16,500 B,
`f423caa89b4dbd14cd84eaa99e961e67c0870dea36df0bdff7bcfe38ada50cdd`.

### Preservation

The accepted and current candidates (`id-aa57d83c898e3af1`,
`id-6b7c7e5d5f71e809`, `id-4d501e08f75df139` — ELF/ISO/CUE/manifest) were
copied to `releases/2026-08-15_1817_t2_6-pre-build/` **before** any build ran.
This task's artifacts are at `releases/2026-08-15_1843_t2_6-diag/` and
`releases/2026-08-15_1904_t2_6-product/`.

### Gates

| Suite | Result |
| --- | --- |
| `verify-memory-map` (both builds) | **RESULT OK** |
| `verify-actor-meshlets` | **PASS** — equivalence sweep, carry test, and the invalid-span mutation fixture |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-audio-loop-contracts` | **OK** |
| `verify-pcm68k-model` | **OK** |
| `verify-terrain-depth-bins` | **OK** |
| `verify-vdp1-frame-bank` | **OK** |
| `verify-demo-render-overlap` | **PASS**, mutation fixture caught |
| `verify-render-overlap-integration` | **PASS**, mutation fixture caught |
| `verify-render-job-runtime` | **PASS** |
| `test_dual_sh2_work_storage_contract.py` | **OK** |
| `test_vdp1_staging_relocation.py` | **OK** |
| `test_render_job_runtime_source.py` | **OK** |

**Pre-existing failure, unchanged.** `tools/saturn/test_render_snapshot_source.py`
aborts at `test_vdp1_painter_chain_uses_all_existing_master_depth_tags`
(`assert "records[local].sort_key >> 16" in text`, line 148) — the identical
assertion T2.1–T2.5 all recorded. §10 records the defect this task briefly
introduced there and how it was found.

## 8. The diagnostic re-measure

Same rig as T2.5 (`tools/saturn/capture_prenotification_profile.py`, headless
Ymir **build-agent2**, USA BIOS, absolute paths, release-manifest bound,
`ROUTE_REPLAY=1`), 24,000 post-BIOS frames in 300-frame chunks. **26,181
emulated frames, 303.5 s wall, 89 samples (77 valid), 1,350 completed
pre-notification windows.** On-target identity **MATCH** against ELF
`27803d61…04ce`. **Zero SH-2 exceptions** in every sample. Movement witnessed
at 30 distinct sampled Mario positions. **No desktop launch.**

All twelve acceptance checks pass, harness exits 0:

```
capture_completed        true    profile_seen             true
exception_record_clear   true    profile_stable_sample    true
frt_wrap_headroom_ok     true    profile_version_ok       true
no_profiler_faults       true    profiler_stack_balanced  true
route_movement_observed  true    vdp_generations_climbing true
windows_accumulated      true    pass                     TRUE
```

`faults = 0`, `end_depth_max = 1`, `max_raw_interval = 18,590` (headroom
71.6%, still set by `spatial_admit`) — the rig is as clean as T2.5 left it.

### The ranked table, against T2.5's

Mean per pre-notification window, T2.5 over 798 windows, T2.6 over 1,350.
`↳` marks sub-nodes nesting under `prepare_mario`.

| Sub-stage | T2.5 ticks | **T2.6 ticks** | Δ | T2.6 % of window | Calls |
| --- | ---: | ---: | ---: | ---: | ---: |
| `spatial_admit` | 15,942.8 | **16,020.6** | +77.8 (+0.49%) | 77.18% | 1 |
| `work_order` | 1,920.2 | **1,943.6** | +23.4 | 9.36% | 1 |
| ↳ `meshlet_depth_admit` | 20,491.7 | **1,132.3** | **−19,359.4 (−94.47%)** | 5.46% | 31 |
| ↳ `meshlet_admit` | 509.9 | 526.1 | +16.2 | 2.54% | 1 |
| `position_set` | 450.8 | 458.2 | +7.4 | 2.21% | 1 |
| ↳ `meshlet_emit` | 258.6 | 262.2 | +3.6 | 1.26% | 1 |
| `mario_ctx` | 161.7 | 162.3 | +0.6 | 0.78% | 1 |
| ↳ `mario_draw_order` | 108.5 | 108.5 | 0.0 | 0.52% | 1 |
| `window_residue` | 30.0 | 30.2 | +0.1 | 0.15% | 1 |
| `queue_contexts` | 20.6 | 20.7 | +0.1 | 0.10% | 1 |
| `frame_reset` | 16.4 | 16.3 | −0.1 | 0.08% | 1 |
| ↳ `meshlet_prefix` | 14.0 | 14.5 | +0.5 | 0.07% | 1 |
| `graph_publish` | 13.0 | 13.0 | 0.0 | 0.06% | 1 |
| ↳ `meshlet_prepare` (self) | 10.5 | 13.0 | +2.5 | 0.06% | 1 |
| `queue_reset` | 12.3 | 12.4 | +0.1 | 0.06% | 1 |
| **`prepare_mario`** (self) | 5.6 | 5.7 | 0.0 | 0.03% | 1 |
| `bank_open` | 5.2 | 5.3 | 0.0 | 0.03% | 1 |
| ↳ `mario_setup` | 4.8 | 4.8 | 0.0 | 0.02% | 1 |
| `actor_pose` | 2.9 | 2.9 | 0.0 | 0.01% | 1 |
| `snapshot_acquire` | 2.2 | 2.1 | 0.0 | 0.01% | 1 |
| `notify` | 2.0 | 2.0 | 0.0 | 0.01% | 1 |
| ↳ **`meshlet_depth_emit`** | 20,491.9 | **0.0** | **−20,491.9 (−100%)** | **0.00%** | **0** |
| `actor_closure` | 0 | 0 | 0 | 0 | 0 |
| **window total** | **60,475.6** | **20,756.6** | **−39,719.0 (−65.68%)** | 100% | — |

### Per-fix attribution — from one build, structurally

The instrument attributes the two fixes separately without needing an
intermediate build, because each fix moves a different node and one of them
moves a **call count**:

| Fix | Evidence | Saving |
| --- | --- | ---: |
| **Step 1** (carry) | `meshlet_depth_emit` reads **0 ticks and `node_calls_last == 0`**. That node is pushed only on pass 2's recompute branch. A call count of zero can only mean the branch was never taken — step 2 cannot zero a call count. | 20,491.9 ticks at T2.5's rate |
| **Step 2** (de-divide) | `meshlet_depth_admit` 20,491.7 → 1,132.3. Step 1 does not touch pass 1 at all, so every tick of this is step 2. | 19,359.4 ticks |

**Honest note on non-additivity, exactly as T2.5 predicted.** The two are not
additive. Step 2 alone would have left both passes at ~1,132 ticks (2,264.6
total, saving 38,719); step 1 alone would have left one pass at 20,491.7
(saving 20,492). Together they save 39,851.3. **Step 2 accounts for 97.2% of
the realised saving; step 1's marginal contribution given step 2 is 1,132.3
ticks (2.8%).** T2.5 called this correctly: item 1's value is that it is free,
safe, and halves the surface item 2 has to be validated against — which is
exactly how it was used.

### `demo_prepare_mario` rolled up

| Quantity | T2.5 | **T2.6** | Δ |
| --- | ---: | ---: | ---: |
| Stage total (self + 8 children) | 41,895.4 ticks | **2,067.1** | **−95.07%** |
| Stage share of window | 69.28% | **9.96%** | |
| ↳ depth bounds (both passes) | 40,983.6 | **1,132.3** | **−97.24%** |
| ↳ depth share of stage | 97.82% | **54.78%** | |
| Cycles per tier-0 position visit | 3,725.8 | **102.9** | **36.2× faster** |
| Cycles per mesh vertex | 12,647.7 | **624.0** | **20.3× faster** |
| Stage cycles | 5,362,617 | **264,589** | |

102.9 cycles per visit lands **inside T2.5's predicted "defensible ~100–150"**,
so Finding D (uncached cart reads) is now the dominant residual as T2.5 said
it would be, and there is little room left in this loop.

### Cross-checks

- **`spatial_admit` moved +0.49%** (15,942.8 → 16,020.6) across two
  independent builds and two independent 26,000-frame runs. It received no
  change, so it is the control: the saving is localised to the nodes that were
  edited, not a global shift. Every other unedited node moves under 1.6%.
- **The slave.** Busy 11,042.9 → 11,186.9 ticks (+1.3%); its overlap window
  grew 3.085 → 3.391 VBlanks. Against the nominal 3,509.1 ticks/VBlank that is
  **busy 0.94× its own window, down from T2.5's 1.02×.** The slave is no longer
  quite saturated — the master finishing sooner has opened a little slack — but
  1.02 → 0.94 is not enough headroom to justify a rebalance, and §10 records
  why this figure is now less precise than T2.5's.
- **Wrap safety.** `max_raw_interval` 18,590 of 65,535, **71.6% headroom**,
  still equal to `spatial_admit`'s maximum. No interval aliased.

## 9. Product FPS

Instrument: `tools/saturn/capture_sourceboot_throughput.py`, headless Ymir
**build-agent2** (SHA-256 `fcc88d82…3943`), BIOS
`sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`, absolute paths,
`--startup-vblanks 4096`, release manifest bound. On-target identity **MATCH**
at startup attempt **681**/4096 — the same attempt count as T2.3, T2.2 and R1.
**No desktop launch; the owner owns the look-and-listen.**

### Run 1 — tool-complete, `--max-vblanks 3600`, default 2 presentation events

`docs/saturn/evidence/reports/sprint2-t2_6-throughput.json`, status `complete`:

| Metric | T2.2 | T2.3 | **T2.6** |
| --- | ---: | ---: | ---: |
| Guest FPS (mean = median = 1%-low, n=1) | 2.0 | 2.069 | **4.286** |
| `vblank_delta` | 30 | 29 | **14** |
| Construction | 25 | 24 | **9** |
| Master finalization | 5 | 4 | **4** |
| Simulation | 5 | 5 | **5** |
| Slave work overlap | 2 | 2 | **2** |
| Dropped VBlank credits | 14 | 13 | **6** |
| Unattributed | 0 | 0 | **0** |

Queue terminal record: `master_failures 0, slave_failures 0, qf 0, qw 0`.

### Run 2 — sustained, `--max-vblanks 4096 --presentation-events 60`

The tool aborted in `summarize_cadence`, the **same known tool invariant** R1,
T2.2 and T2.3 all hit at 60 events; as before the figure comes from the run's
preserved cadence-trace diagnostics
(`docs/saturn/evidence/reports/sprint2-t2_6-throughput-sustained.json`,
`observation_diagnostics`). 60 presentation events observed over
`vblanks_advanced = 2,460`; terminal runtime record `master_failures 0,
slave_failures 0, qf 0, qw 0`.

| Metric | R1 baseline | T2.2 | T2.3 | **T2.6** | T2.6 vs T2.3 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Presentations / VBlanks | 60 / 3,362 | 60 / 3,370 | 60 / 3,313 | **60 / 2,460** | **−853** |
| **Sustained FPS** | 1.071 | 1.0682 | 1.0866 | **1.4634** | **+34.68%** |
| VBlanks per frame | 56.0 | 56.17 | 55.22 | **41.00** | **−14.22** |
| Construction | ~24.5 | 24.72 | 23.75 | **9.450** | **−14.30** |
| — of which pre-notification | — | 18.92 | 18.92 | **5.017** | **−13.90** |
| — of which master finalization | 5.8 | 5.80 | 4.83 | **4.433** | **−0.40** |
| Simulation | — | 6.07 | 6.07 | **6.213** | +0.14 |
| Slave work overlap | — | 2.73 | 2.73 | **2.950** | +0.22 |
| Dropped VBlank credits | ~14 | 14.42 | 13.95 | **6.917** | **−7.03** |

**The attribution is clean.** Construction fell 14.30 VBlanks/frame against a
total frame reduction of 14.22, and 13.90 of that 14.30 is pre-notification.
Every other phase is flat to within 0.25 VBlanks. Nothing outside the edited
code moved.

**Two instruments agree across two different builds.** The diagnostic rig
measured pre-notification at 5.323 VBlanks/frame; the product build's cadence
rig measures 5.017. That is 6% apart on entirely separate builds, tuples and
harnesses.

**Against the 4 FPS floor:** run 1 reads 4.286, up from T2.3's 2.069. The
floor is not threatened by this change in either direction.

## 10. Honesty — what is wrong with these numbers, and what did not pay off

1. **This task introduced one defect and it was caught by the full gate set,
   not by design.** T2.6's doc comments referred to `actor_meshlet_core()` and
   `sm64_saturn_actor_meshlets_prepare()` with call parentheses above their
   definitions. `test_render_snapshot_source.py`'s `function_body` helper finds
   a function with `text.index("<name>(")` and takes the first `{` after it, so
   it matched the **comment** and extracted the depth-carry typedef instead of
   the function body — failing `test_opaque_actor_meshlets_use_depth_bins`
   against code that was correct. Fixed in `dfe41b57` by rewording, with the
   constraint recorded in the file and the extractor's fragility filed
   separately. **Rewording comments is a workaround, not a fix.** Had I run
   only the task-named gates and not this suite, it would have shipped.
2. **T2.5's Finding B was wrong as written**, and implementing it literally
   would have been a silent numeric change. The floors do not distribute over
   the sum. The correct, narrower identity is in §4; it is what the oracle
   pins. This is the clearest argument for T2.3's oracle-first sequencing:
   the plan's own algebra did not survive contact with a test.
3. **The two fixes are not additive** and the report does not pretend they are.
   Step 2 carries 97.2% of the realised saving. Step 1 is still worth having —
   it is free, it cannot change a number, and it halved what step 2 had to be
   validated against — but anyone reading "5.72 VBlanks" and "5.5 VBlanks" from
   T2.5 §9 and adding them would be wrong, exactly as T2.5 warned.
4. **The VBlank-equivalent conversion is now much coarser than in T2.5.**
   `ticks_per_vblank_agreement` moved from +2.30% to **+11.12%**. The rig
   counts *whole* VBlank crossings per frame, so its quantisation error is
   roughly ±0.5 crossings — about 3% at T2.5's 16.87 crossings, but about 9%
   at T2.6's 5.32. The FRT tick figures are the precise ones; treat any
   VBlank-equivalent derived from the diagnostic rig as ±10%, and prefer the
   product cadence rig (§9) for whole-frame statements. The slave's 0.94×
   busy ratio inherits this uncertainty and should not be read as a precise
   claim that idle slack now exists.
5. **`unattributed_share` rose from 0.0497% to 0.1453%** — but in absolute
   ticks the residue is unchanged (30.04 → 30.16). It is a fixed cost that is
   now a larger fraction of a much smaller window. Nothing new is hiding.
6. **Fewer distinct route positions were sampled** — 30 against T2.5's 42 — on
   the same route and the same 24,000 post-BIOS frames. The game now advances
   ~1.7× more frames in the same emulated time, so the 600-tick scripted
   movement completes earlier in the sample sequence and more samples land in
   the idle tail. Movement is still positively witnessed; the route coverage
   is the same route, sampled differently.
7. **Route coverage, as always.** BOB entry, ~600 ticks of scripted movement,
   an idle tail. Not owner free-roam, not object interactions
   (`dynamic_actor_closure=0`), not other levels or camera angles. Ymir, not
   hardware.
8. **The bank path was not improved.** It keeps the two-walk behaviour and, for
   non-unit scales, the divide-based arithmetic. That is deliberate (cross-CPU
   dispatch, §3; arbitrary scales, §4) and it is unmeasured because it is
   compiled out of this tuple. If `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE` is
   ever enabled, the same two fixes will need a per-lane carry and a scale
   generalisation, and neither is free.
9. **`___divdi3` is still in the image.** It is no longer on any hot path — the
   only caller left is `actor_saturating_mul_i64` on the fallback branch and
   the on-screen diagnostic profiler's bar drawing — but T2.5's "only 64-bit
   division caller on a hot path" is now "zero", not "removed from the link".
10. **What did not pay off:** nothing was attempted and abandoned. T2.5's
    Findings D (staging cart arrays into HWRAM), E (`always_inline`) and G
    (cross-frame memo) were deliberately **not** attempted, per T2.5 §9's
    ranking, and remain unattempted. Finding D is now the dominant residual of
    a loop that is only 5.5% of the window, so its ~25k–45k cycles are worth
    about 0.08 VBlanks/frame — proportionally larger than before, absolutely
    negligible.
11. **The next target is `spatial_admit`, and the ranking has changed exactly
    as T2.5 predicted.** It is now **77.18% of the pre-notification window**
    (was 26.36%) at 16,020.6 ticks, and it still sets the FRT wrap ceiling.
    T2.5's Finding H recorded that it uses `int64` dot products with **no**
    saturating helpers and **no** 64-bit division, so Finding A does not apply
    to it — whatever is slow there is a different shape, and it has never been
    decomposed.

## 11. Reproduction

```
# Host gates (no build required)
make -f Makefile.saturn.mk verify-actor-meshlets
make -f Makefile.saturn.mk verify-vdp1-painter-chain verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model verify-terrain-depth-bins
make -f Makefile.saturn.mk verify-vdp1-frame-bank verify-demo-render-overlap
make -f Makefile.saturn.mk verify-render-overlap-integration verify-render-job-runtime
python tools/saturn/test_dual_sh2_work_storage_contract.py
python tools/saturn/test_vdp1_staging_relocation.py
python tools/saturn/test_render_job_runtime_source.py

# Diagnostic build: the 27-variable product invocation from
# sprint1-stage1-link-smoke.md with SATURN_DIAGNOSTIC_MODE=2, and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's
# release_config.diagnostic_mode temporarily 0 -> 2 (canonical LF JSON,
# uncommitted, reverted after the build), via
# tools/saturn/with-msys-toolchain.ps1 sh --noprofile --norc -l -c
#   'source ../../.yaul.env; unset COMPILER_PATH; make -f Makefile.saturn.mk -j1 sourceboot <27 vars>'
make -f Makefile.saturn.mk verify-memory-map

python tools/saturn/capture_prenotification_profile.py \
  --ymir <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl  "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game <abs>/build/saturn/sourceboot/e2-bob-identity-id-d2a4896c541ba56b/sm64-saturn-sourceboot-e2.cue \
  --elf  <abs>/build/saturn/sourceboot/e2-bob-identity-id-d2a4896c541ba56b/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_6-meshlet-arithmetic.json
# exits 0; all twelve acceptance checks pass.

# Product build: the same 27 vars with SATURN_DIAGNOSTIC_MODE=0 and no
# profile edit, then:
make -f Makefile.saturn.mk verify-memory-map
python tools/saturn/capture_sourceboot_throughput.py \
  --ymir <abs>/.../ymir-headless.exe \
  --ipl  "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game <abs>/build/saturn/sourceboot/e2-bob-identity-id-6eca5970628d581d/sm64-saturn-sourceboot-e2.cue \
  --elf  <abs>/build/saturn/sourceboot/e2-bob-identity-id-6eca5970628d581d/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_6-throughput.json \
  --startup-vblanks 4096 --max-vblanks 3600
# sustained: --max-vblanks 4096 --presentation-events 60 (aborts in
# summarize_cadence by the known tool invariant; read
# observation_diagnostics.vblanks_advanced and presentation_events_observed)
```

Instruction-level evidence (linked image `id-d2a4896c541ba56b`):

```
sh-elf-nm -S <elf> | grep -E 'actor_saturating|actor_meshlet_live|s_mario_depth_carry'
sh-elf-objdump -d <elf> --start-address=0x0607017c --stop-address=0x0607050c
# fast path 0x6070262-0x60703b0: 167 instructions, 0 jsr, 9 dmuls.l
# fallback  0x60703b0+        : 15 jsr to the saturating helpers
```

Mutation reproduction (do **not** commit): apply one row of §6 to
`src/port/saturn/gfx/saturn_actor_meshlets.c` or
`tools/saturn/actor_meshlet_test.c` and run `verify-actor-meshlets`.

Comment-only codegen proof: compile
`src/port/saturn/gfx/saturn_actor_meshlets.c` at `SATURN_DIAGNOSTIC_MODE=0`
with `-g0` on the product build's own command line, once at `f4f8ad6b` and
once at `dfe41b57`, and compare the objects — 16,500 B, `f423caa8…50cdd`, byte-identical.
