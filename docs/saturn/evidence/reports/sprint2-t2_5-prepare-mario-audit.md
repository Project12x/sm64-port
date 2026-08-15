# Sprint 2 Task T2.5 — inside `demo_prepare_mario()`

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `6b8cbe77`.
- Plan: `docs/superpowers/plans/2026-08-15-sprint2-cadence-recovery.md`,
  Task T2.5. Instrument extends T2.4's
  (`sprint2-t2_4-prenotification-profile.md`), which pinned L14 from T2.0's
  reference sweep.
- Why now: T2.4 measured `demo_prepare_mario()` at **11.665 VBlanks/frame,
  5,348,377 SH-2 cycles, 69.24% of the pre-notification window and ~21% of
  the whole frame** — the largest single measured block in this port — and
  reported it as one opaque stage. It also recorded four defects against its
  own rig; three are fixed here and the fourth is resolved by construction.
- **This is a measurement + audit task. Nothing was optimised.**

## Headline

**97.8% of `demo_prepare_mario()` is one function, called 62 times per
frame, and it is slow because it performs a 64-bit software division per
multiply.**

- `demo_prepare_mario()` measures **41,895 FRT ticks = 5,362,617 cycles =
  69.28% of the window**, reproducing T2.4's 41,784 / 5,348,377 / 69.24% to
  within 0.27%.
- Of that, **`actor_meshlet_live_depth_bounds()` is 40,984 ticks =
  5,245,905 cycles = 97.82% of the stage** — 11.43 of its 11.69
  VBlanks/frame, **20.7% of the whole frame**.
- It is called **twice per meshlet per frame**, and the two calls measure
  **20,491.7 and 20,491.9 ticks** — 0.001% apart. The second walk is an
  exact, unconditional recomputation of the first.
- Cost per tier-0 position visit: **3,725.8 cycles**. Per mesh vertex:
  **12,647.7 cycles** — the brief's ~12,600, confirmed.
- Mechanism, confirmed at instruction level: each visit makes **10 calls to
  `actor_saturating_mul_i64()`**, and that helper checks overflow by
  *dividing*, so each call emits a `___divdi3` — libgcc's 620-byte software
  64-bit divide. That is **~14,080 software divisions per frame**, and
  `actor_saturating_mul_i64` is **the only 64-bit-division caller on any hot
  path in the entire linked image**.
- Everything else in the stage is noise: `demo_prepare_mario`'s own self
  time is **5.6 ticks — 0.013% of the stage**.

**Verdict on the mesh (§10): NO.** Polygon count is not a material
contributor. Halving the mesh would leave 5.72 VBlanks/frame; fixing the
arithmetic at *full* mesh detail leaves ~0.19. The fix is ~30x better than
the reduction and costs no fidelity.

## 1. The sub-stage enumeration, established before instrumenting

Reading `demo_prepare_mario()`
(`src/port/saturn/gfx/saturn_demo_render.c:3980`) and everything it reaches
produced this list **before** any probe was written. The function is a thin
wrapper; almost all of its body is one call.

| # | Sub-stage | What runs | Where |
| --- | --- | --- | --- |
| — | (validation) | null/`valid`/`vertex_count` guards, output counters reset | `saturn_demo_render.c:3980-3997` |
| 1 | `mario_setup` | meshlet generation id, `sm64_saturn_render_snapshot_t` copy of the Mario snapshot, `sm64_saturn_render_view_t` assembly (camera position → Q16, view forward), output binding | `saturn_demo_render.c:4003-4028` |
| 2 | `meshlet_prepare` | `sm64_saturn_actor_meshlets_prepare()` prologue/epilogue: source descriptor, transform (position → Q16, unit scale, yaw) | `saturn_actor_meshlets.c:692-729` |
| 3 | `meshlet_admit` | `actor_meshlet_core()` **pass 1** — per-meshlet cull test, LOD tier choice, position de-duplication into the output set, per-depth-bin primitive counting — **excluding** the depth-bounds calls below | `saturn_actor_meshlets.c:511-593` |
| 4 | `meshlet_depth_admit` | the 31 `actor_meshlet_live_depth_bounds()` calls pass 1 makes | `saturn_actor_meshlets.c:522` → `:411-477` |
| 5 | `meshlet_prefix` | descending-bin cursor prefix sum over the 64 opaque and 64 translucent bins, plus the `position_seen` re-clear | `saturn_actor_meshlets.c:613-625` |
| 6 | `meshlet_emit` | `actor_meshlet_core()` **pass 2** — draw-ref emission into the bin cursors, sort-key derivation — **excluding** its depth-bounds calls | `saturn_actor_meshlets.c:627-680` |
| 7 | `meshlet_depth_emit` | the 31 `actor_meshlet_live_depth_bounds()` calls **pass 2 makes again** | `saturn_actor_meshlets.c:634` → `:411-477` |
| 8 | `mario_draw_order` | the opaque-then-translucent walk that fills `s_actor_draw_order` | `saturn_demo_render.c:4035-4053` |
| 0 | `prepare_mario` (self) | everything in the stage not named above — validation, control flow between probes, the epilogue | — |

Node 0 is deliberately the residue, so `demo_prepare_mario`'s own
unattributed remainder is measured rather than inferred, exactly as T2.4 did
for the window.

Several sub-stages the brief listed as candidates **do not exist on this
path**, and are recorded as absent rather than reported as zero:

- **Vertex transform, perspective projection, backface classify, Gouraud
  table reservation, command record fill.** None are inside
  `demo_prepare_mario()`. They live in `demo_finalize_mario_draws()`,
  `demo_reserve_mario_gouraud()` and `demo_emit_mario()`
  (`saturn_demo_render.c:4059-4194`), which run *after* notification and are
  outside T2.4's window entirely. `demo_prepare_mario()` produces no
  screen-space coordinate and no VDP1 command, so **there is no perspective
  divide in this stage at all** — the brief's specific suspicion does not
  apply here (it may still apply to the emit stage, which no task has
  profiled).
- **Pose / skeleton evaluation, per-joint matrix compose.** At this tuple's
  `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=0`,
  `sm64_saturn_mario_actor_pose()` is a table *select*, not an evaluation
  (`saturn_actor_bridge.c:169-189`). `sm64_saturn_actor_pose_evaluate()`
  (`saturn_actor_pose.c:58`), which does compose joint matrices, belongs to
  the *bank* path and is not called. T2.4 measured the whole `actor_pose`
  stage at 3 ticks; this run measures 2.9.

So the stage is structurally **meshlet admission and nothing else** — and
inside it, one function called 62 times per frame.

## 2. The mesh, confirmed

Counted from the shipped header, not from the donor-era audit:

| Quantity | Value | Source |
| --- | ---: | --- |
| Vertices | **424** | `saturn_mario_actor_mesh.h:7` `SM64_MARIO_VERTEX_COUNT` |
| Primitives (quads) | **644** | `saturn_mario_actor_mesh.h:8` `SM64_MARIO_PRIMITIVE_COUNT` |
| Meshlets | **31** | `saturn_mario_actor_mesh.h:49057` `SM64_MARIO_MESHLET_COUNT` |
| LOD primitive-ref list | 1,369 | `:49060` |
| LOD position-ref list | 1,659 | `:49061` |
| Translucent meshlets | 5 of 31 | `sm64_mario_meshlet_opacity` |
| Animation frames (neutral / walking) | 30 / 77 | `:439`, `:13222` |

The brief's ~424 / 644 / 31 is confirmed exactly.

**The number that actually governs this stage is a different one.**
`actor_meshlet_live_depth_bounds()` iterates the **tier-0** position span of
whichever meshlet it is given (`saturn_actor_meshlets.c:419-426`), and it is
called for *every* meshlet *before* any cull test or LOD choice can shorten
it. Summing tier-0 spans over all 31 meshlets from
`sm64_mario_meshlet_lod_position_offsets`:

| Tier | Position refs | Primitive refs |
| --- | ---: | ---: |
| **0** | **704** | 644 |
| 1 | 704 | 644 |
| 2 | 251 | 81 |

So the stage performs **704 position visits per pass** and — see Finding C —
**two passes**, i.e. **1,408 per frame**, deterministically, independent of
camera, pose and LOD. That is the divisor every per-vertex figure below
uses. It exceeds 424 because meshlets share vertices and the depth walk does
not de-duplicate.

The target confirms the shape rather than the profiler assuming it:
`node_calls_last` reads **31** for `meshlet_depth_admit` and **31** for
`meshlet_depth_emit` in every sampled window.

## 3. What was added

### The instrument

`src/port/saturn/runtime/saturn_prenotify_profile.h`: node table 16 → 24,
record ABI 288 → 452 bytes, version 1 → 2. Eight new nodes, all nesting
under `PREPARE_MARIO`; ids 0–14 are unchanged so T2.4's ranked table stays
directly comparable. Probes added in `saturn_demo_render.c` and — newly
instrumented — `saturn_actor_meshlets.c`. `node_calls_last[]` was added so
per-call cost is measured rather than assumed.

Same discipline as T2.4: fixed table, zero allocation, nestable push/pop
charging at every transition, raw FRT reads, diagnostic-gated on
`SATURN_DIAGNOSTIC_MODE != 0 && defined(__sh__)`. Maximum nesting is 4
(`WINDOW → PREPARE_MARIO → MESHLET_PREPARE → MESHLET_ADMIT →
MESHLET_DEPTH_ADMIT`) against a stack depth of 8.

### Where the probes deliberately are NOT

**No probe is per-vertex.** The two depth nodes are pushed once per
*meshlet* — 31 per pass, 62 per frame. A probe is tens of cycles and there
are 1,408 position visits per frame; a per-vertex probe would have added
~140k–200k cycles *and* would have been measuring mostly itself at exactly
the granularity where the answer lives.

Per-vertex cost is therefore **derived by division**, and the divisor is
sound because it is a compile-time property of the mesh tables (§2), not a
runtime quantity: the loop bound is `tier_zero.position_count` for every
meshlet unconditionally.

### Perturbation — predicted, then checked against an unprobed control

Probe events per window: T2.4's 28, plus 12 for the six new stage brackets,
plus 62 + 62 for the two depth nodes = **164**, versus T2.4's 28. The
per-probe cost also rose, because the working state is now `__uncached`
(below), so `charge()` performs ~10 uncached HWRAM accesses instead of
cached ones — call it ~100 cycles per probe rather than ~40.

Predicted: 164 × ~100 ≈ **16,400 cycles**, ~0.21% of the window.

Measured, and this is the useful part:

| | T2.4 | T2.5 | Δ |
| --- | ---: | ---: | ---: |
| Window mean, ticks | 60,350 | 60,475.6 | **+125.6** (+16,077 cycles, +0.208%) |
| `spatial_admit` mean, ticks — **unprobed control** | 15,943 | **15,942.8** | **−0.2** |
| `mario_ctx` mean, ticks — unprobed control | 162 | 161.7 | −0.3 |
| `prepare_mario` stage total, ticks | 41,784 | 41,895.4 | +111.4 |

The measured window growth (+16,077 cycles) lands within 2% of the
prediction (+16,400), and **89% of it lands inside the stage that received
the probes.** `spatial_admit` — 26% of the window, untouched — reproduces to
five significant figures across two independent builds and two independent
26,000-frame runs. That is a stronger control than T2.4 had, and it says the
instrument is not distorting what it measures.

### Wrap headroom — resolved, not merely re-checked

T2.4 closed with 17% headroom (`max_raw_interval` 54,192 of 65,535) and
noted that the maximum *was* `prepare_mario`'s own maximum — the stage being
decomposed here was the binding constraint, and φ/128 is the coarsest
internal FRT clock the SH-2 offers.

Subdividing that stage can only **shorten** the longest inter-probe interval
(the 16-bit extension is exact per *interval*, not per node), so the new
binding constraint should be `spatial_admit`. It is, exactly:

| | T2.4 | T2.5 |
| --- | ---: | ---: |
| `max_raw_interval` | 54,192 | **18,591** |
| `spatial_admit` max, ticks | 18,591 | **18,591** |
| Headroom | 17.3% | **71.6%** |

`max_raw_interval` equals `spatial_admit`'s maximum to the tick, which both
proves no interval aliased and identifies which stage now sets the ceiling.
T2.4's defect 4 is closed: **finer sub-stages carry less wrap risk here, not
more.**

### Rig defects fixed (T2.4 §7 items 1–3)

1. **`notify_to_retire` and `finalize_ticks` removed, not re-derived.** T2.4
   recorded them as invalid: `runtime_publish_retirement_marker()` is called
   from `render_job_slave_entry`, so the RETIRED marker observer executes on
   the **slave** SH-2, and the FRT is a per-CPU on-chip block — the two
   fields subtracted one CPU's free-running counter from the other's. Both
   fields, both `mark_*()` entry points, both macro call sites and the
   harness's two derived blocks are gone. The cadence rig reports both
   intervals correctly in VBlank crossings, so nothing is lost. `slave_busy`
   is untouched: begun and ended on the slave, published straight to LWRAM
   through P2, and it still reads 11,043 ticks against T2.4's 11,043.
2. **The state is `__uncached`, and the slave no longer touches it.**
   Removing the marks removes the only slave writes to
   `g_sm64_saturn_prenotify_profile_state` — T2.4's three bytes (`retire16`,
   `notified`, `retired`); all three members are deleted. The object
   additionally now carries `__uncached` (`sourceboot/main.c:180`), matching
   what the shipped cadence rig already does for
   `sourceboot_render_overlap_phase`. Confirmed in the linked image:
   `_g_sm64_saturn_prenotify_profile_state` is at **`0x260FA8E4`**, i.e. the
   `.uncached` section reached through the SH-2 P2 alias, not cached `.bss`.
   Both fixes together, so the correctness of the window measurement no
   longer rests on an argument from consistency.
3. **The fault accounting is fixed and the exit code means something.**
   T2.4's `end()` counted the deliberately still-pushed NOTIFY node as a
   stack fault, producing `faults == windows == 798` on a completely healthy
   run and failing its own acceptance gate. `end()` now publishes the
   closing depth as `end_depth_max` — 1 is the design, because `end()` is
   reached from inside the notify call and there is no instant at which
   NOTIFY could have been popped first — and counts a fault only for a real
   imbalance (depth > 1). A new `profiler_stack_balanced` check gates on the
   published depth directly.

   **Measured this run: `faults = 0`, `end_depth_max = 1`, and the harness
   exits 0 with every acceptance check passing** — including
   `no_profiler_faults`, which T2.4 could not pass by construction.

### Product cleanliness — proven at object level

Each of the three modified translation units was compiled at
`SATURN_DIAGNOSTIC_MODE=0` with `-g0`, using the build's **own** command
line lifted verbatim from this build's log, twice from the same source
paths — once with the entire changeset applied and once with **every touched
file** (including the header) reverted to `HEAD` — and the objects compared.
Reverting the whole set together is required, because `HEAD`'s `main.c`
references macros the working tree's header no longer defines.

| Translation unit | Mode-0 object at `-g0` |
| --- | --- |
| `src/port/saturn/gfx/saturn_actor_meshlets.c` | **byte-identical** (15,512 B) |
| `src/port/saturn/gfx/saturn_demo_render.c` | **byte-identical** (723,168 B) |
| `src/port/saturn/sourceboot/main.c` | **byte-identical** (660,904 B) |

**All three byte-identical.** This is strictly stronger than T2.4, which had
to except two four-digit `assert` `__LINE__` literals in
`saturn_demo_render.c`; T2.5's edits sit below those `assert`s, so no line
number shifts. **No instruction, no data byte and no symbol in the product
build changes.**

## 4. The audit — why a per-vertex cost is 100x too high

Done by reading the source and the **linked image**, independently of the
probe. Each finding carries a mechanism, a file:line, and an estimated
saving with stated confidence.

### Finding A — a 64-bit software division per multiply, ten per vertex

`actor_meshlet_live_depth_bounds()`
(`src/port/saturn/gfx/saturn_actor_meshlets.c:411-477`) computes, for every
tier-0 position of every meshlet, the world position and then its depth
along the view forward. It does this entirely through two helpers:

- `actor_saturating_add_i64()` — `saturn_actor_meshlets.c:88-93`
- `actor_saturating_mul_i64()` — `saturn_actor_meshlets.c:95-110`

`actor_saturating_mul_i64` checks for overflow **by dividing**:

```c
    if (left > 0) {
        if (right > 0 && left > INT64_MAX / right) return INT64_MAX;
        if (right < 0 && right < INT64_MIN / left) return INT64_MIN;
    } else {
        if (right > 0 && left < INT64_MIN / right) return INT64_MIN;
        if (right < 0 && left < INT64_MAX / right) return INT64_MAX;
    }
```

Both operands are runtime values, so each of those is a genuine **signed
64-bit division**. The SH-2 has no 64-bit divide; GCC emits a call to
libgcc's `___divdi3` — 620 bytes of normalise-and-shift delegating to
`___udivdi3` (604 bytes, with `___clz_tab`, `___lshrsi3_r0` and
`___ashlsi3_r0` helpers of its own). Confirmed in the linked image, not
inferred:

```
0606fdf8 <_actor_saturating_mul_i64>:
 606fe64:  d0 2c   mov.l  606ff18,r0  ! 600473c <___divdi3>
 606fe84:  40 0b   jsr    @r0
```

Per position visit, the call census of the disassembled loop body
(`0x06070074`–`0x0607037c`) is exactly:

| Callee | Static sites | Dynamic calls / visit |
| --- | ---: | ---: |
| `actor_saturating_mul_i64` | 8 | **10** |
| `actor_saturating_add_i64` | 7 | **11** |
| `actor_position_ref` | 1 | 1 |

(7 straight-line `mul` sites plus 1 inside the 3-iteration axis loop = 10;
5 straight-line `add` sites plus 2 in the axis loop = 11. Source and
disassembly agree exactly, which is what makes the static census a dynamic
one.)

So per frame: 1,408 position visits × 10 = **~14,080 64-bit software
divisions**, ~14,080 64-bit software multiplies, and ~15,500 further
out-of-line saturating-add calls.

**`actor_saturating_mul_i64` is the only 64-bit-division caller on any hot
path in the entire linked image.** A census of every `___divdi3` /
`___udivdi3` / `___moddi3` / `___umoddi3` reference across the whole
disassembly returns exactly three callers, and the other two are the
on-screen diagnostic profiler's bar drawing:

```
  1  _actor_saturating_mul_i64
  1  _draw_profiler_bar
  1  _draw_profiler_mode_1
```

**Mechanism → measurement.** 5,245,905 measured cycles ÷ 1,408 visits =
**3,725.8 cycles per position visit**; ÷ 10 saturating multiplies ≈ 373
cycles each, which is the right order for a libgcc `__divdi3` plus a
`__muldi3` plus call overhead on an SH-2 at `-Os`. The arithmetic closes
from both ends.

**Estimated saving:** with Finding B, reduces the depth walk from ~3,726 to
~100–150 cycles per visit — **~2.52 M cycles per pass**, ~5.5 VBlanks/frame
per pass. **Confidence: high** — mechanism confirmed at instruction level,
arithmetic reconciles with the measured total, divisor is a compile-time
constant.

### Finding B — the whole computation is per-vertex work that is per-actor algebra

Beyond the helper cost, the loop recomputes per vertex a quantity that is
constant per actor. Written out, `saturn_actor_meshlets.c:434-461` computes

```
depth(v) = dot( actor_pos + R_yaw · (S ⊙ v) − camera_pos , forward )
```

which expands to

```
depth(v) = dot(actor_pos − camera_pos, forward)  +  dot( S ⊙ v , R_yawᵀ · forward )
           ^^^^^^^^^^ per-actor constant ^^^^^^^^     ^^^^ per-actor constant vector ^^^^
```

Everything except `v` is invariant across all 704 visits: `actor_pos`,
`camera_pos`, `forward`, `yaw`, and `S` — which for Mario is fixed at unit,
because `saturn_actor_meshlets.c:720-722` sets all three scale axes to
`1 << 16` unconditionally. Precomputing the scalar and the rotated forward
vector **once per actor** reduces the inner loop to three multiplies and two
adds on 32-bit values.

Three specific redundancies inside that, each individually a red flag:

- `saturn_actor_meshlets.c:446-454` — `world[axis]` is built as
  `actor_saturating_mul_i64(x >> 16, 65536)`. That is a **multiply by 2^16
  routed through the divide-based saturating helper**. Three of the ten
  divisions per vertex are a left shift.
- `saturn_actor_meshlets.c:434-439` — `scaled_{x,y,z}` multiply the vertex
  by `transform->scale_q16[axis]`, which for Mario is always `65536`. Three
  more 64-bit multiplies that are shifts.
- `saturn_actor_meshlets.c:424-425` — `sine`/`cosine` are recomputed inside
  `actor_meshlet_live_depth_bounds`, i.e. 62 times per frame, from a
  `transform->yaw` that cannot change within the frame. Two table lookups
  each, so small in absolute terms, but it is the same hoisting error at a
  different scale.

The codebase **already has the correct primitives and this path uses
neither**: `sm64_saturn_q16_mul()`
(`src/port/saturn/gfx/saturn_matrix_kernels.h:75`) is a `dmuls.l`-based Q16
multiply with a documented overflow contract, and `matrix_apply()`
(`src/port/saturn/gfx/saturn_actor_pose.c:31-56`) already demonstrates the
safe in-repo pattern for exactly this shape — an `int64` accumulator over
`int16 × Q16` products, narrowed once with a shift, with no division and no
saturating helper.

**Estimated saving:** see Finding A — the two are one change.
**Confidence: high** for the algebra (it is an identity, not an
approximation); **medium** for the exact residual cycle figure, which is
then dominated by Finding D.

### Finding C — the entire depth computation is performed twice per frame

`actor_meshlet_core()` walks all 31 meshlets twice. Pass 1
(`saturn_actor_meshlets.c:513`) counts and bins; pass 2
(`saturn_actor_meshlets.c:628`) emits. **Pass 2 calls
`actor_meshlet_live_depth_bounds()` again for every meshlet**
(`saturn_actor_meshlets.c:634`), discards the return value with a `(void)`
cast, and re-derives the identical `depth_bounds` and `span` that pass 1
already computed and threw away:

```c
            (void)actor_meshlet_live_depth_bounds(source, transform, view,
                                                  meshlet, &depth_bounds);
            if (depth_bounds.furthest_q16 <= 0) continue;
            (void)actor_meshlet_span(source, meshlet,
                actor_lod_tier(depth_bounds.nearest_q16), &span);
```

Nothing between the passes can change the result: `transform` and `view` are
`const` inputs and neither pass writes them.

**The measurement proves it independently of the source reading:**
`meshlet_depth_admit` = 20,491.7 ticks and `meshlet_depth_emit` = 20,491.9
ticks — a difference of 0.2 ticks in 20,000, i.e. **0.001%**. Two
separately-instrumented nodes agreeing to one part in ten thousand is what
identical work looks like.

A 31-entry array of `{nearest_q16, furthest_q16, span}` carried from pass 1
to pass 2 removes the second walk entirely. Size: **868 bytes** naively
(`actor_meshlet_depth_bounds_t` 8 B + `actor_meshlet_span_t` 20 B, × 31), or
**620 bytes** if the span's four `uint32_t` fields are narrowed to `uint16_t`
(they index lists of 1,369 and 1,659 entries). It must be `static`, not a
stack local — the SH-2 stack in this port is not sized for it.

**Estimated saving: 20,492 ticks = 2,622,967 cycles = 5.72 VBlanks/frame =
10.4% of a 55.22-VBlank frame. Confidence: very high** — the duplication is
textual and unconditional, the saving is a directly measured node, and the
replacement is bit-identical by construction because it reuses values pass 1
already produced.

### Finding D — the hot arrays are read cache-through from the A-bus cart

Symbol placement in the linked image:

| Array | Address | Region |
| --- | --- | --- |
| `sm64_mario_animation_vertices` (the pose vertices) | `0x2273071e` | `.cart_rodata` |
| `sm64_mario_walking_animation_vertices` | `0x227009ee` | `.cart_rodata` |
| `sm64_mario_meshlet_lod_position_list` | `0x22743160` | `.cart_rodata` |
| `sm64_mario_meshlet_lod_position_offsets` | `0x22743e56` | `.cart_rodata` |
| `s_actor_opaque_refs` / `_translucent_refs` / `_transform_refs` | `0x0021xxxx` | `.lwram_bss` |

`.cart_rodata` is linked at `0x22400000`
(`src/port/saturn/sourceboot/sourceboot-cart.x:23`). On the SH-2 the
`0x20000000` partition is the **cache-through** window, and the port has
already established that no cached CS0 alias exists
(`docs/saturn/PERFORMANCE_DIAGNOSIS_2026-07-26.md`, hypothesis 2:
"CONFIRMED and unavoidable for the cart"). So every vertex coordinate and
every position-list index in this loop is an **uncached A-bus 16-bit read**.

Per position visit: 1 index read (`actor_position_ref`,
`saturn_actor_meshlets.c:394-404`) + 3 coordinate reads
(`saturn_actor_meshlets.c:434-439`) = 4. Per frame: 1,408 × 4 ≈ **5,632
uncached cart reads**, plus ~500 more from `actor_meshlet_span`.

**This is NOT the primary cost today** — the same document already refuted
"the cart is the bottleneck" arithmetically, and at ~10–20 cycles per A-bus
read this is ~60k–110k cycles, i.e. **1–2% of the stage**. It is recorded
because it becomes the **dominant residual** once A–C land, and therefore
sets the floor on what T2.6 can achieve without also staging this data into
HWRAM.

T2.2 disproved *bulk* memory-tier placement as a lever. This is a specific
pair of arrays on one hot loop — the case T2.2 explicitly left open.

**Estimated saving after A–C: ~25k–45k cycles (~0.06–0.10 VBlanks/frame).
Confidence: medium** — the region and the access count are certain; the
per-access latency is not measured here.

### Finding E — every helper on the path is an out-of-line call at `-Os`

The tuple compiles this translation unit at **`-Os -g`**
(`src/port/saturn/sourceboot/Makefile:707`; confirmed verbatim in this
build's own command line). No `noinline` attribute or pragma appears
anywhere in `saturn_actor_meshlets.c` — `-Os` alone declines to inline these
bodies because each has multiple call sites and a non-trivial size:

| Symbol | Size | Calls / frame |
| --- | ---: | ---: |
| `_actor_saturating_mul_i64` | 296 B | ~14,080 |
| `_actor_saturating_add_i64` | 100 B | ~15,500 |
| `_actor_position_ref` | 84 B | ~1,408 |
| `_actor_meshlet_live_depth_bounds` | 776 B | 62 |
| `_actor_meshlet_core` | 1,284 B | 1 |

No call goes through a function pointer; all are direct `jsr @rN`. Call
overhead here is ~6 register saves plus `sts.l pr` and the symmetric
restores — meaningful, but second-order beside Finding A. **This finding
carries no independent action:** fixing A and B *deletes* these call sites
rather than inlining them. It is recorded so T2.6 does not spend a change
budget on `always_inline` and expect a result.

### Finding F — no float, and no other integer-division helper, on this path

Checked in the linked image, not the source. `___addsf3`, `___mulsf3`,
`___divsf3`, `___subsf3`, `___fixsfsi`, `___floatsisf`, `___adddf3`,
`___muldf3` and `___floatsidf` all exist in the image (the port links them
for other reasons), and **none is reachable from
`0x0606fd94`–`0x060709d8`** — the address range spanning every function from
`actor_saturating_add_i64` through `sm64_saturn_actor_meshlets_prepare`. A
disassembly scan of that range for any `sf3` / `df3` / `float` / `fix`
literal returns nothing. Likewise no `___udivsi3`, `___umodsi3`, `___divsi3`
or `___modsi3` reference appears in any `actor_*` or `demo_*` symbol.

The one `%` on the wider Mario path — `frame % pose->frame_count` in
`sm64_saturn_mario_actor_pose()` (`saturn_actor_bridge.c:176`, `:184`) — is
a 32-bit unsigned modulo by a runtime value, so it *is* a helper call, but
it executes once per frame in the `actor_pose` stage measured here at **2.9
ticks**. Not material; recorded for completeness.

**The port's native-Q16 premise holds on this path.** The problem is not
float leakage, and it is not `__umodsi3`.

### Finding G — is anything recomputed across frames that need not be? Mostly no

T2.4 hypothesised that the meshlet set is pose/view-derived and might be
reusable across frames. **The strong form is false for this tuple**, and the
brief's specific guess — that `COMPLETE_MARIO_ANIMATION=0` makes the pose
static — does not hold:

`sm64_saturn_mario_actor_pose()` at
`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=0` selects
`sm64_mario_animation_vertices[frame]` or
`sm64_mario_walking_animation_vertices[frame]`
(`saturn_actor_bridge.c:169-189`), where `frame` tracks
`snapshot->animation_frame`. The neutral bank has **30** frames and the
walking bank **77** (`saturn_mario_actor_mesh.h:439`, `:13222`). The pose
genuinely changes frame to frame, as do `snapshot->position`,
`snapshot->yaw` and the camera. A cross-frame cache would need a four-way
invalidation key and would miss on essentially every moving frame.

**The redundancy that is real is intra-frame** (Finding C: an exact,
unconditional 2x, measured to one part in ten thousand) **and algebraic**
(Finding B: per-vertex work that is per-actor). Both are strictly better
than caching — they need no invalidation key and cannot go stale.

Recorded explicitly so T2.6 does not spend effort on a frame-to-frame memo
that the data does not support.

### Finding H — secondary: `demo_spatial_admit()` (26.4%) is a different shape

Not this task's target, but noted while reading, because it is now the
second-largest block and the one that sets the FRT wrap ceiling.
`demo_spatial_admit_node()` (`saturn_demo_render.c:762-841`) uses `int64`
dot products but **no** saturating helpers and **no** 64-bit division, so
Finding A does not apply to it. It does recompute per node per frame the
AABB centres (`:800-808`) from `sm64_saturn_bob_bsp_bounds_{min,max}`, which
are static `const` geometry — a bakeable per-node precompute. That is a much
smaller lever than anything above and belongs after T2.6.

## 5. Build and identity

Same 27-variable invocation as `sprint1-stage1-link-smoke.md` / T2.2 / T2.3
/ T2.4 (pool 208), with **exactly one variable different:
`SATURN_DIAGNOSTIC_MODE` 0 → 2**. Via
`tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`,
sourcing `../../.yaul.env`, then `unset COMPILER_PATH`.

The identity bootstrap requires Make-provided config to equal the profile's
`release_config`, so `tools/saturn/profiles/sourceboot-bob-demo-v1.json` had
`diagnostic_mode` flipped 0 → 2 as an **uncommitted, byte-canonical (LF, no
CRLF) edit**, reverted immediately after the build — T2.1's established
precedent, verified reverted (`git diff` on that file is empty and it reads
`"diagnostic_mode":0`).

**One build attempt, exit 0.** The g15 package-staleness cascade did not
require the documented repair.

Sealed identity **`id-4d501e08f75df139`**.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | 9,986,820 | `5a468138404c0715e82a88669ff18219a7953bc44c405e7a8de3ba7a7220bd11` |
| `sm64-saturn-sourceboot-e2.iso` | 5,171,200 | `1ef17a5575be1b4e22e14b80670bcfe77bc0c9be6df6840ff11c8be02a776c52` |
| `sm64-saturn-sourceboot-e2.cue` (88 B, not identity-bearing) | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `saturn-release-manifest-v1.json` | 3,271 | `efc1f7ac1e4605117744d219a9744efe2e624171d96768c4e35173af473e2a7b` |

Preservation (mandatory rule): the accepted and current candidates
(`id-aa57d83c898e3af1`, `id-6b7c7e5d5f71e809`, `id-86d3880727ed1d10` —
ELF/ISO/CUE/manifest) copied to `releases/2026-08-15_1715_t2_5-pre-build/`
**before** the build ran; this build's artifacts to
`releases/2026-08-15_1731_t2_5/`.

Probe symbols in the linked image (`sh-elf-nm`):
`g_sm64_saturn_prenotify_profile` at `0x002D8958` (LWRAM, NOLOAD),
`g_sm64_saturn_prenotify_profile_state` at **`0x260FA8E4`** — the
`.uncached` section through the P2 alias, which is the visible proof of
defect fix 2.

### Gate — `verify-memory-map` on the diagnostic build (verbatim)

```
verify-memory-map: checking .../e2-bob-identity-id-4d501e08f75df139/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FB314
  hwram_remaining = 0x4CEC bytes (required >= 0x1F00)
  lwram_end       = 0x002E8BC0
  lwram_remaining = 0x17440 bytes (floor >= 0x4000)
  RESULT          = OK
```

**RESULT OK.** True slack over the `0x1F00` floor is **11,756 B**, against
T2.4's 12,248 B: the bigger node table, `node_calls_last[]` and the
`.uncached` state cost **492 B of HWRAM**, and only in a diagnostic build.
LWRAM remaining fell by 160 B, consistent with the record's 288 → 452 B
growth.

### Gate — host contracts (on the diagnostic tree)

| Suite | Result |
| --- | --- |
| `verify-memory-map` | **RESULT OK** |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-audio-loop-contracts` | **OK** |
| `verify-pcm68k-model` | **OK** |
| `verify-terrain-depth-bins` | **PASS** |
| `verify-vdp1-frame-bank` | **OK** |
| `verify-demo-render-overlap` | **PASS** |
| `verify-render-overlap-integration` | **PASS** |
| **`verify-actor-meshlets`** | **PASS** — including its invalid-span mutation fixture |
| **`verify-render-job-runtime`** | **PASS** — the recipe T2.4 found broken; base HEAD `6b8cbe77` fixed it |
| `test_dual_sh2_work_storage_contract.py` | **OK** |
| `test_vdp1_staging_relocation.py` | **OK** |
| `test_render_job_runtime_source.py` | **OK — 5 tests** |

`verify-actor-meshlets` passing matters specifically: it exercises
`saturn_actor_meshlets.c`, the file this task instrumented most heavily, and
its mutation fixture confirms the suite is non-vacuous.

**Pre-existing failure, reproduced against `HEAD` and not caused here.**
`tools/saturn/test_render_snapshot_source.py` fails at its first assertion,
`test_vdp1_painter_chain_uses_all_existing_master_depth_tags` (`assert
"records[local].sort_key >> 16" in text`). Reproduced by stashing the entire
changeset and re-running: **identical failure, identical assertion**. T2.1–
T2.4 recorded the same suite failing; the script aborts at the first
assertion, which is why only one of the three is visible.

## 6. Capture

- Instrument: headless Ymir **build-agent2**
  (`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`),
  BIOS `sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`,
  absolute `--cue`, release-manifest bound, venv Python.
- Harness: `tools/saturn/capture_prenotification_profile.py` (T2.4's,
  updated for the v2 ABI and the new derived arithmetic).
- Run shape: proven USA-BIOS handoff, wait for the sealed identity in target
  RAM, then 24,000 post-BIOS frames in 300-frame chunks with a full 452-byte
  record read at every boundary. **26,181 emulated frames total, 309 s wall,
  89 samples (77 with a valid record).**
- **Route: `ROUTE_REPLAY=1`, movement positively witnessed** — **42
  distinct sampled Mario world positions**. An idle boot would not have been
  representative and was not used.
- On-target identity **MATCH**; the ELF the harness bound hashes to
  `5a468138…bd11`, the build above.
- **Zero SH-2 exceptions** — `sourceboot_exception_record.magic` was `0` in
  every one of the 77 valid samples.
- **No desktop launch. The owner holds the observation gate.**
- **798 completed pre-notification windows** contributed to the means — the
  same count as T2.4.

Artifact of record:
`docs/saturn/evidence/reports/sprint2-t2_5-prepare-mario-audit.json`.

### Acceptance — all checks pass, exit 0

```
capture_completed        true    profile_seen             true
exception_record_clear   true    profile_stable_sample    true
frt_wrap_headroom_ok     true    profile_version_ok       true
no_profiler_faults       true    profiler_stack_balanced  true
route_movement_observed  true    vdp_generations_climbing true
windows_accumulated      true    pass                     TRUE
```

T2.4's harness exited 1 on a healthy run. This one exits 0, and
`no_profiler_faults` is now a real check rather than a tautological failure.

### Two instruments, cross-checked

| | This run |
| --- | ---: |
| Rig: pre-notification VBlank crossings / frame | **16.868** |
| Profiler: window mean, FRT ticks | **60,475.6** |
| Implied ticks per VBlank (measured) | 3,589.7 |
| Ticks per VBlank from libyaul's own NTSC-320 constants | 3,509.1 |
| Agreement | **+2.30%** |

A ~2% gap between an FRT-derived duration and a VBlank-crossing count is the
accuracy of the assumed NTSC field-rate / SH-2 clock pair, not a
disagreement between instruments (T2.4 saw +2.1%). The VBlank-equivalent
column below is therefore computed as **share × the rig's own 16.868**, so
it sums to the rig's number exactly and carries no assumed constant.

Other rig figures from the same terminal sample (per frame, 797–798
frames): construction **22.066**, master finalization **5.198**, slave
overlap **3.085**, simulation **6.302**.

Slave (measured on the slave's own FRT, the one L12 field that survived):
**11,042.9 ticks = 1,413,494 cycles = 3.15 VBlank-equiv** mean over 798
entries, against a 3.085-VBlank overlap window — **busy 1.02× its own
window**, identical to T2.4. The slave is still saturated; there is no idle
slack inside the overlap.

## 7. Results — the ranked table

Mean and max are per pre-notification window, over 798 windows.
VBlank-equivalent = share × the rig's own 16.868 (see above).
**T2.5 sub-nodes are marked `↳`;** they nest under `prepare_mario`, whose
own row is therefore self time only.

| Rank | Sub-stage | Mean ticks | Mean cycles | VBlank-eq | % of window | Max ticks | Calls/win |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | ↳ `meshlet_depth_emit` | **20,491.9** | **2,622,967** | **5.716** | **33.89%** | 26,697 | **31** |
| 2 | ↳ `meshlet_depth_admit` | **20,491.7** | **2,622,938** | **5.716** | **33.88%** | 26,696 | **31** |
| 3 | `spatial_admit` | 15,942.8 | 2,040,675 | 4.447 | 26.36% | 18,591 | 1 |
| 4 | `work_order` | 1,920.2 | 245,791 | 0.536 | 3.18% | 2,425 | 1 |
| 5 | ↳ `meshlet_admit` | 509.9 | 65,267 | 0.142 | 0.84% | 519 | 1 |
| 6 | `position_set` | 450.8 | 57,697 | 0.126 | 0.75% | 565 | 1 |
| 7 | ↳ `meshlet_emit` | 258.6 | 33,095 | 0.072 | 0.43% | 266 | 1 |
| 8 | `mario_ctx` | 161.7 | 20,699 | 0.045 | 0.27% | 164 | 1 |
| 9 | ↳ `mario_draw_order` | 108.5 | 13,886 | 0.030 | 0.18% | 111 | 1 |
| 10 | **`window_residue` (unattributed)** | **30.0** | **3,846** | **0.008** | **0.050%** | 32 | 1 |
| 11 | `queue_contexts` | 20.6 | 2,641 | 0.006 | 0.03% | 22 | 1 |
| 12 | `frame_reset` | 16.4 | 2,102 | 0.005 | 0.03% | 18 | 1 |
| 13 | ↳ `meshlet_prefix` | 14.0 | 1,788 | 0.004 | 0.02% | 15 | 1 |
| 14 | `graph_publish` | 13.0 | 1,663 | 0.004 | 0.02% | 15 | 1 |
| 15 | `queue_reset` | 12.3 | 1,570 | 0.003 | 0.02% | 13 | 1 |
| 16 | ↳ `meshlet_prepare` (self) | 10.5 | 1,342 | 0.003 | 0.02% | 12 | 1 |
| 17 | **`prepare_mario` (self)** | **5.6** | **718** | **0.002** | **0.009%** | 7 | 1 |
| 18 | `bank_open` | 5.2 | 668 | 0.001 | 0.01% | 6 | 1 |
| 19 | ↳ `mario_setup` | 4.8 | 616 | 0.001 | 0.01% | 5 | 1 |
| 20 | `actor_pose` | 2.9 | 371 | 0.001 | <0.01% | 3 | 1 |
| 21 | `snapshot_acquire` | 2.2 | 278 | 0.001 | <0.01% | 3 | 1 |
| 22 | `notify` | 2.0 | 262 | 0.001 | <0.01% | 3 | 1 |
| 23 | `actor_closure` | 0 | 0 | 0 | 0 | 0 | 0 (compiled out) |
| | **window total** | **60,475.6** | **7,740,879** | **16.868** | 100% | 72,026 | — |

### `demo_prepare_mario()` rolled up

| Quantity | Ticks | Cycles | VBlank-eq | Share |
| --- | ---: | ---: | ---: | ---: |
| **Stage total** (self + all 8 children) | **41,895.4** | **5,362,617** | **11.686** | **69.28% of window** |
| ↳ depth-bounds (both passes) | 40,983.6 | 5,245,905 | 11.432 | **97.82% of stage** |
| ↳ everything else in the stage | 911.8 | 116,712 | 0.254 | 2.18% of stage |
| ↳ `prepare_mario` self (residue) | 5.6 | 718 | 0.002 | **0.013% of stage** |

- **Cycles per tier-0 position visit: 3,725.8** (1,408 visits/frame).
- **Cycles per mesh vertex: 12,647.7** (424 vertices) — the brief's ~12,600.
- Against T2.3's 55.22-VBlank frame (approximate — see §8), the stage is
  **21.2% of the entire frame** and the depth-bounds walk alone is **20.7%**.

### The parts do sum to the whole

**Unattributed remainder: 30.0 ticks, 0.0497% of the window.** The named
stages account for **99.95%**, and `window_residue` measured that directly
rather than inferring it. Inside `demo_prepare_mario` the residue is smaller
still: **0.013% of the stage**, so the eight sub-nodes reconstruct it almost
exactly.

### Stability across the route

The composition is not an artefact of the idle tail. Per-window figures from
three samples spanning the whole run:

| Sample | Window ticks | Stage ticks | Stage % of window | Depth % of stage |
| --- | ---: | ---: | ---: | ---: |
| window 9 (early route) | 71,452 | 54,068 | 75.67% | **98.31%** |
| window 406 (mid route) | 63,856 | 45,554 | 71.34% | **98.00%** |
| window 798 (tail) | 56,915 | 37,210 | 65.38% | **97.54%** |

Absolute cost varies by ~45% as the scene and pose change, and the stage's
share of the window moves between 65% and 76% — but **the depth-bounds share
of the stage never leaves 97.5–98.3%.** The finding is a property of the
code, not of one moment on the route.

## 8. Honesty — what is wrong with these numbers

1. **`meshlet_admit` and `meshlet_emit` are self time, not pass totals.**
   Ranks 5 and 7 exclude their depth children by construction. Pass 1's true
   total is 21,001.6 ticks and pass 2's is 20,750.5. Nothing is hidden — the
   nesting is stated — but the two rows must not be read as "the admission
   pass costs 510 ticks".
2. **Per-vertex cost is derived, not directly probed**, for the stated
   reason (§3). The divisor (704 per pass) is read out of the mesh tables and
   corroborated on target only indirectly, by `node_calls_last` confirming 31
   calls per pass. If a future mesh made `tier_zero.position_count`
   view-dependent, the derivation would silently break — it is not
   view-dependent today (`saturn_actor_meshlets.c:419`, tier is hard-coded
   `0U`).
3. **The perturbation is 16x T2.4's** — 0.208% versus 0.013% — and it is
   concentrated in exactly the stage being measured, because that is where
   the probes are. It is corroborated to within 2% by the unprobed
   `spatial_admit` control, but the depth nodes' absolute figures are ~0.3%
   high in a way the other nodes' are not.
4. **This build's window is 16.87 VBlanks/frame; T2.3 reported 18.92 for the
   whole pre-notification window and 55.22 VBlanks/frame overall.** Those are
   different measurements on different builds and must not be differenced.
   The "% of frame" figures here use T2.3's 55.22 as an approximate
   denominator and inherit its uncertainty; the *composition* is what this
   task delivers, and it is stable (§7).
5. **`work_order` moved +1.7% (1,887 → 1,920 ticks) with no probe added
   inside it.** Small, but it is a reminder that these are two different
   builds with different code layout, not a controlled A/B. `spatial_admit`
   moving −0.001% in the same pair is the reassuring counterpart.
6. **Route coverage, as always.** BOB entry, ~600 ticks of scripted
   movement, and an idle tail. Not owner free-roam, not object interactions
   (`dynamic_actor_closure=0`, which is why `actor_closure` reads exactly
   zero), not other levels or camera angles. Ymir, not hardware.
7. **The savings in §9 are estimates, not measurements.** Finding C's is a
   directly measured node and is as close to certain as an unimplemented
   change gets. Findings A+B's residual (~100–150 cycles per visit) is an
   instruction-count estimate whose dominant term is Finding D's unmeasured
   A-bus latency. **T2.6 must re-measure, not assume.**
8. **`test_render_snapshot_source.py` still fails**, pre-existing and
   reproduced against `HEAD` (§5). Three assertions are known to fail; the
   script aborts at the first, so only one is visible per run.

## 9. The ranked T2.6 action list

Ordered by **risk-adjusted value**, not by raw saving. Frame percentages use
T2.3's 55.22-VBlank frame.

### 1. Carry pass 1's depth bounds and span into pass 2 (Finding C)

- **Saving: 20,492 ticks = 2,622,967 cycles = 5.72 VBlanks/frame = 10.4% of
  the frame.** Directly measured as a node, not estimated.
- **Cost:** one `static` 31-entry array — 868 B naive, 620 B packed — in
  `actor_meshlet_core()`. HWRAM slack is 11,756 B in the diagnostic build and
  13,848 B in the product build, so this fits with room.
- **Risk: minimal.** The replacement is **bit-identical by construction** —
  it reuses values pass 1 already computed rather than recomputing them, so
  no numeric result can change. This is the only change on the list that
  cannot alter a single emitted command.
- **Gate:** `verify-actor-meshlets` (which already exercises this file and
  carries a non-vacuous mutation fixture) must pass unchanged; then a live
  capture on the scripted route showing `meshlet_depth_emit` at ~0 ticks and
  `node_calls_last[meshlet_depth_emit] == 0`, with the frame's VDP1 command
  count and Mario draw count unchanged.
- **Do this first even though item 2 subsumes most of it** — it is a
  low-risk floor, and it halves the surface item 2 has to be validated
  against.

### 2. Replace the depth loop's saturating int64 arithmetic with per-actor algebra (Findings A + B)

- **Saving: ~2.52 M cycles per remaining pass — after item 1, ~5.5
  VBlanks/frame, ~10.0% of the frame.** Combined with item 1, the depth walk
  goes from 5,245,905 cycles to an estimated **70k–110k**, i.e. **~11.2–11.3
  VBlanks/frame recovered, ~20.4% of the frame**, and `demo_prepare_mario`
  drops from 11.69 VBlanks to ~0.4.
  - Note honestly: **item 2 alone would recover ~11.05 VBlanks** (it applies
    to both passes), so items 1 and 2 are *not* additive. Item 1's real value
    is that it is free, safe, and reduces item 2's blast radius.
- **Shape:** hoist `dot(actor_pos − camera_pos, forward)` and
  `R_yawᵀ · forward` to once per actor; per vertex compute three `dmuls.l`
  products into an `int64` accumulator and narrow once — the pattern
  `matrix_apply()` (`saturn_actor_pose.c:31-56`) already uses in this repo.
  Delete `actor_saturating_mul_i64` / `actor_saturating_add_i64` from this
  path entirely.
- **Risk: moderate, and it is numeric, not structural.** `depth_bounds`
  feeds `actor_lod_tier()` and `actor_depth_bin()`, so any value change can
  shift an LOD tier or a painter bin and therefore change what is drawn. The
  new form must also justify its own overflow bounds, which the saturating
  helpers were (expensively) providing: vertex coordinates are `int16`
  (±32768) and the rotated forward is Q16 (≤65536), so the product reaches
  2^31 — the accumulator must be 64-bit, exactly as `matrix_apply` does.
- **Gate — commit the oracle before the swap, T2.3's proven pattern.** A
  host equivalence harness that runs the existing
  `actor_meshlet_live_depth_bounds` and the replacement over a swept set of
  (vertex, yaw, actor position, camera position, forward) and asserts
  identical `nearest_q16`/`furthest_q16`, or — if bit-identity is not
  achievable — identical `actor_lod_tier()` and `actor_depth_bin()`
  outcomes, which is the weaker contract that actually matters. Include the
  saturation corner cases the helpers exist for. Mutation-verify the harness.
  Then `verify-actor-meshlets`, then a live capture, then owner review of
  Mario's appearance (a bin or tier shift is a **visual** regression and
  §UI-bug rules apply — automated tests cannot clear it).

### 3. Re-measure before doing anything else

- After items 1–2, `spatial_admit` becomes the largest block in the window
  at ~4.45 VBlanks and `work_order` second at ~0.54. The whole ranking
  changes. **Re-run this profiler before choosing a third target.**
- The rig is ready for it: wrap headroom is 71.6% and `max_raw_interval`
  already identifies `spatial_admit` as the next ceiling.

### 4. Stage the two hot cart arrays into HWRAM (Finding D) — only if items 1–2 land

- **Saving: ~25k–45k cycles (~0.06–0.10 VBlanks/frame)** — negligible today,
  but it is a large fraction of what *remains* after items 1–2.
- **Cost:** the tier-0 position list (704 × 2 B = 1,408 B) plus the current
  frame's pose vertices (424 × 3 × 2 B = 2,544 B) ≈ **3,952 B of HWRAM**
  against a 13,848 B product-build slack. Tight, and the pose copy has to be
  refreshed per frame, which is itself ~2.5 KB of copying.
- **Risk: low functionally, real for the memory budget.** Requires the
  §"vertical-slice and memory-debt" record before it is written.
- **Gate:** `verify-memory-map` plus a live capture; expect a small,
  possibly unmeasurable win. **Do not do this speculatively** — T2.2
  disproved the bulk version of this hypothesis.

### 5. Bake `demo_spatial_admit_node()`'s AABB centres (Finding H) — later

- Static geometry recomputed per node per frame
  (`saturn_demo_render.c:800-808`). Unquantified; small. After item 3
  re-ranks.

### Explicitly NOT next

- **Mesh reduction.** See §10.
- **`always_inline` on the saturating helpers** (Finding E). Items 1–2
  delete the call sites; inlining a `__divdi3` call does not help.
- **Any master/slave rebalance.** The slave is still busy 1.02× its own
  window, unchanged from T2.4. There is no idle slack inside the overlap.
  Revisit only after items 1–2 shrink the master's pre-notification work,
  which changes the question entirely.
- **Ranks 6–23 of §7.** Together they are under 1% of the window.

## 10. The mesh-reduction verdict

**NO. Polygon count is not a material contributor to this bottleneck.**

The owner approved mesh-reduction tooling as a later lever. The numbers say
it should not be spent here.

| Scenario | Depth-walk cost | VBlanks/frame | % of 55.22-VBlank frame |
| --- | ---: | ---: | ---: |
| **Today** | 5,245,905 cycles | 11.43 | **20.7%** |
| **Halve the mesh, keep the arithmetic** | ~2,623,000 cycles | ~5.72 | **~10.4%** |
| **Keep the full mesh, fix the arithmetic** (items 1–2) | ~70k–110k cycles | **~0.16–0.24** | **~0.3–0.4%** |
| Both | ~35k–55k cycles | ~0.08–0.12 | ~0.2% |

The reasoning:

- The governing count is not 424 vertices; it is **704 tier-0 position
  visits per pass, walked twice** (§2). Mesh reduction scales that count
  **linearly**.
- The problem is the **constant**: 3,725.8 cycles per visit, against a
  defensible ~100–150 for a Q16 transform-and-dot with the cart reads
  included. That is a factor of ~25–37, and no amount of linear reduction
  fixes a constant.
- Halving the mesh leaves the stage at ~10.4% of the frame — still, on its
  own, larger than every non-`spatial_admit` block in the pre-notification
  window combined. It would not be a fix; it would be a fidelity cost that
  buys a bottleneck half as bad.
- Fixing the arithmetic at **full** mesh detail is roughly **30x better than
  halving the mesh**, and it costs no visual fidelity at all.
- The brief's framing is confirmed exactly: at 12,647.7 cycles/vertex,
  halving the vertex count still leaves ~6,300 cycles/vertex — about 50x
  what the operation should cost. **Per-vertex cost was the right primary
  suspect.**

Mesh reduction may still be worth doing later for VDP1 fill rate, command
count, or the post-notification emit stage — **none of which this task
measured.** It is simply not the lever for `demo_prepare_mario()`.

## 11. Reproduction

```
# Diagnostic build: the 27-variable product invocation from
# sprint1-stage1-link-smoke.md with SATURN_DIAGNOSTIC_MODE=2, and
# tools/saturn/profiles/sourceboot-bob-demo-v1.json's
# release_config.diagnostic_mode temporarily 0 -> 2 (canonical LF JSON,
# uncommitted, reverted after the build), via
# tools/saturn/with-msys-toolchain.ps1 -> MSYS sh --noprofile --norc -l,
# source ../../.yaul.env, unset COMPILER_PATH,
# make -f Makefile.saturn.mk -j1 sourceboot <27 vars>

make -f Makefile.saturn.mk verify-memory-map
make -f Makefile.saturn.mk verify-vdp1-painter-chain verify-audio-loop-contracts
make -f Makefile.saturn.mk verify-pcm68k-model verify-terrain-depth-bins
make -f Makefile.saturn.mk verify-vdp1-frame-bank verify-demo-render-overlap
make -f Makefile.saturn.mk verify-render-overlap-integration
make -f Makefile.saturn.mk verify-actor-meshlets verify-render-job-runtime
python tools/saturn/test_dual_sh2_work_storage_contract.py
python tools/saturn/test_vdp1_staging_relocation.py

python tools/saturn/capture_prenotification_profile.py \
  --ymir <abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe \
  --ipl  "<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --game <abs>/build/saturn/sourceboot/e2-bob-identity-id-4d501e08f75df139/sm64-saturn-sourceboot-e2.cue \
  --elf  <abs>/build/saturn/sourceboot/e2-bob-identity-id-4d501e08f75df139/obj/sm64-saturn-sourceboot-e2.elf \
  --release-manifest <abs>/.../saturn-release-manifest-v1.json \
  --output docs/saturn/evidence/reports/sprint2-t2_5-prepare-mario-audit.json
# exits 0; all twelve acceptance checks pass.
```

Product-cleanliness proof (mode-0, `-g0`, the build's own command line, same
paths compiled twice — once with the whole changeset, once with every
touched file reverted to `HEAD`): all three objects **byte-identical**.

Audit evidence (linked image `id-4d501e08f75df139`):

```
sh-elf-nm  -S <elf> | grep -E 'actor_saturating|actor_meshlet|divdi3'
sh-elf-objdump -d <elf> --start-address=0x0606fdf8 --stop-address=0x06070074   # the divide
sh-elf-objdump -d <elf> --start-address=0x06070074 --stop-address=0x0607037c   # the call census
sh-elf-objdump -d <elf> | <group ___divdi3/___udivdi3 references by enclosing symbol>
```
