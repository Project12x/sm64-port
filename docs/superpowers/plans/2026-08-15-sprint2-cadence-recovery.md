# Sprint 2: Cadence Recovery — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Tasks T2.2+ are elaborated only after T2.1's capture lands (constitution: measure before design). AGENTS.md binds all work; FPS gates are owner-observed.

**Goal:** Recover cadence from ~1.1 FPS toward the 4-6 FPS band by returning
per-frame-hot data to 32-bit HWRAM and cutting per-frame algorithmic waste,
without regressing the accepted visuals/audio (tags `sprint1-r1-accepted`,
`sprint1-r2-audio-clean`).

**Measured basis (Sprint 2 T1, `sprint2-t1-hwram-attribution.md`):**
- Net HWRAM growth vs A9A is **+44 B** — the problem is composition, not bloat.
  The 131,072 B VDP1 cmdt double-buffer was repatriated to HWRAM, paid for by
  evicting **102,056 B** of hot data to 16-bit LWRAM: the 54,080 B three-way
  split **plus `_sourceboot_fast3d` (44,616 B, per-frame hot, previously
  unsuspected)**.
- True HWRAM slack today: 504 B. Cart free: **419,024 B** (corrected — not
  148 KB). LWRAM remaining: 0x96C0.
- libyaul silently owns ~55 KB of HWRAM `.bss` (`_private_pool` alone 40 KB).
- Cadence profile: 53 VBlanks/frame — scene construction ~24.5, master
  finalization ~5.8.

**Reclamation packages (from T1's ranked table):**
- (a) return the 43,776 B workarea: VDP1 cmdt capacity 2048→1664 (24,576 B)
  + libyaul `_private_pool` 0xA000→0x4000 (24,576 B, **external patch — the
  dependency is read-only; use a patch file/fork per global rules**) = 49,152 B.
- (b) full un-split + `_sourceboot_fast3d` home requires adding
  `GFX_POOL_SIZE` 6400→4096 (18,432 B) → 63,488 B, SMPC pool as reserve.
- ALL capacity shrinks are gated on T2.1's instrumented capture of observed
  peaks (VDP1 command_count max, TLSF high-water, display-list entries).

**Owner directives in force:** consult SlaveDriver/Z-Treme before designing
(T2.0); use the 4 MB cart wisely — cold tenants may move to cart (419 KB free),
hot tenants never (A-bus latency).

---

### Task T2.0: Reference sweep — SlaveDriver & Z-Treme (read-only)

`work/upstream/` clones. Extract, with file:line citations: (1) memory-tier
placement policy — what each engine keeps in HWRAM vs LWRAM vs cart-mapped
space, especially VDP1 command lists and per-frame scratch; (2) draw-order
machinery — how Z-Treme orders VDP1 commands (bucket/radix/counting sort?)
for the T2.4 counting-sort design; (3) command-buffer sizing — how large their
per-frame VDP1 lists are and whether they double-buffer in work RAM or build
in place. Deliverable: `docs/saturn/evidence/reports/sprint2-t2_0-reference-sweep.md`
with a "lessons applied to T2.2-T2.4" section. No code.

### Task T2.1: Instrumented peak capture (gates every capacity shrink)

Discover what the existing profile/trace rails already publish (render
snapshot counters, cadence trace, VDP1 frame-bank stats) before adding
anything. Required peaks over a full scripted-route run (ROUTE_REPLAY=1
exercises movement — do NOT rely on an idle boot; input-less-run lesson):
max per-frame VDP1 command_count; libyaul TLSF/`_private_pool` high-water;
max `gGfxPool` display-list usage; SMPC pool usage. Missing counters get
NOLOAD diagnostic fields gated behind `SATURN_DIAGNOSTIC_MODE=1` — the
product build stays untouched. Deliverable: measured peaks + verdict per
shrink candidate (safe / unsafe / needs-margin), committed as evidence.

### Task T2.2: Reclamation package + un-split — **complete (memory), cadence NEGATIVE**

Apply the verdict-approved shrinks; libyaul change via patch file under
`tools/patches/` (or the project's established mechanism — investigate; never
edit `third_party/` in place). Return the workarea + actor scratch (and
`_sourceboot_fast3d` if package (b) clears) to HWRAM. Gates: link +
`verify-memory-map` OK with ≥0x1F00; `verify-audio-loop-contracts` +
`verify-pcm68k-model` green; FPS capture vs the 1.1 baseline; owner
look-and-listen (no visual/audio regression).

**Status 2026-08-15 — evidence
`docs/saturn/evidence/reports/sprint2-t2_2-reclaim-unsplit.md`, candidate
`id-6b7c7e5d5f71e809`, commits `971f8f93`, `26ae9c38`, `98dab715`,
`03c697f5`:**

- Memory objective MET. 67,584 B recovered (cmdt 2048→1664,
  `GFX_POOL_SIZE` 6400→4096, libyaul `_private_pool` 0xA000→0x4000 via a
  build-time-staged patched copy of the one MIT translation unit, linked
  ahead of `-lyaul`; SMPC 14→4 skipped — it needs a full driver-TU
  supersede and the arithmetic closes without it). Full 54,080 B hot set
  returned to HWRAM. `verify-memory-map` RESULT OK; `hwram_remaining`
  0x20D8 → **0x5578**, true slack over the floor **472 B → 13,944 B**.
- Gates green: `verify-memory-map` OK, `verify-audio-loop-contracts` 24,
  `verify-pcm68k-model` 18, work-storage contract 4 (retargeted to the
  all-HWRAM policy + mutation-verified), staging-relocation contract
  repaired (it was already failing at base HEAD).
- **Cadence objective NOT met: 1.068 FPS sustained vs the R1 baseline's
  comparable 1.071 (−0.24%, no material change).** All phases unchanged
  (construction 24.72 VBlanks/frame, master finalization 5.80). No
  regression either: queue clean, zero SH-2 exceptions.
- Confound recorded: `_sourceboot_fast3d` (44,616 B, per-frame hot) is
  still in LWRAM — T1 measured full A9A hot-set residency at ~98,192 B, so
  the LWRAM hypothesis is half-tested, not refuted. Next rung is T2.0 L3's
  build-in-VRAM staging window, not more tier shuffling.
- **Owner gate still open:** look-and-listen on `id-6b7c7e5d5f71e809` for
  any visual/audio regression from the capacity cuts.
- Design correction for T2.3+: the evidence now points at the algorithmic
  levers (construction = 44% of a 56-VBlank frame), which is exactly
  T2.3's counting sort. Cadence recovery should not be expected from
  further memory-tier work.

### Task T2.3: Painter relink counting-sort — **complete, cadence +1.72%**

O(64×N)/frame → one counting pass. Design informed by T2.0's Z-Treme
findings. Host test pins chain equivalence (same far-to-near order, stable
within bins) before/after. FPS capture isolates its contribution.

**Status 2026-08-15 — evidence
`docs/saturn/evidence/reports/sprint2-t2_3-painter-counting-sort.md`,
candidate `id-aa57d83c898e3af1`, commits `55449eb2`, `0a5b5ccd`:**

- Correctness objective MET. The relink is now validate → intrusive per-bin
  chain scatter through `cmd_link` itself (T2.0 L7's out-of-band `NEXT`;
  128 B of stack, no side buffer) → far-to-near drain. Output is
  **byte-identical** to the predecessor, pinned by a three-way host harness
  (retained reference + shipped code + an independent model) over 16 cases
  up to the arena's full 1664-command capacity, with the three required
  mutations — within-bin stability, bin direction, END/tail — each verified
  KILLED by the harness alone.
- 42,900 → 2,078 record visits/frame at T2.1's measured peak (20.6x);
  109,626 → 5,111 at capacity. Costs +80 B `.text`, zero storage.
- Gates green: `verify-memory-map` RESULT OK (`hwram_remaining` 0x5518,
  true slack 13,848 B, −96 B vs T2.2 — all of it `.text`),
  `verify-vdp1-painter-chain` PASS, `verify-audio-loop-contracts` 24,
  `verify-pcm68k-model` 18, work-storage 4, staging relocation,
  terrain depth bins, frame bank 4, demo-render-overlap.
- **Cadence 1.0682 → 1.0866 FPS (+1.72%)**, 56.17 → 55.22 VBlanks/frame.
  Attribution exact: master finalization 5.80 → 4.83 (−58 crossings over 60
  frames) while the pre-notification window (1,135), slave overlap (164) and
  simulation (370) totals are **bit-identical** across the two builds.
- Honest correction to L8: its "115,200 steps" assumed ~1,800 live commands;
  T2.1 measured **653 published / 650 drawable**, so the stage was ~4% of
  construction, not the bulk. Removing all of it buys 1.7%.
- Redirect: the **pre-notification window (18.92 of construction's 23.75
  VBlanks/frame)** is now the largest unmeasured block. T2.0 L14's FRT tree
  profiler is the instrument; L12's master-spin measurement is the
  companion. L10 (per-BSP-leaf ordering) is explicitly NOT next.

### Task T2.4: decompose the pre-notification window — **complete (measurement)**

**Status 2026-08-15 — evidence
`docs/saturn/evidence/reports/sprint2-t2_4-prenotification-profile.md`
(+ `.json`), diagnostic identity `id-5b28a329c1e8f9de`, commits
`a683c48a` (instrumentation) and this one:**

- Built T2.0 L14's instrument: a diagnostic-gated FRT sub-stage profiler
  (`src/port/saturn/runtime/saturn_prenotify_profile.h`) with SlaveDriver
  `PROFILE.C`'s shape — fixed nodes, zero allocation, nestable push/pop —
  at φ/128 rather than its φ/32, because a 16-bit FRT at φ/32 wraps every
  ~4.7 VBlanks. Product build proven byte-clean at object level.
- **The window is two stages: `demo_prepare_mario()` 69.24% (11.67
  VBlanks/frame, ~21% of the whole frame) and `demo_spatial_admit()`
  26.42% (4.45 VBlanks/frame) — 95.7% together.** Ranks 3–15 are 4.3%;
  ranks 6–15 total 0.15 VBlanks/frame. **Unattributed remainder 0.048%** —
  the parts sum to the whole.
- 798 windows over a scripted-route run (24,000 post-BIOS frames,
  movement witnessed at 42 distinct Mario positions, zero SH-2
  exceptions). Cross-checked against the cadence rig: 3,582 measured
  ticks/VBlank vs 3,509 from libyaul's own NTSC-320 constants, +2.1%.
- **L12 answered:** master spin on the slave is **zero by construction**
  (the lifecycle returns PENDING; the only blocking spin, `dual_worker_run`,
  sits in three functions the compiler reports dead in this tuple), and the
  slave is **busy 1.02× its own 3.085-VBlank overlap window**. There is no
  idle-slave slack in the split; the master simply keeps 16.85 VBlanks to
  itself before notifying.
- Gates green on the diagnostic build: `verify-memory-map` RESULT OK
  (`hwram_remaining` 0x4ED8, true slack 12,248 B), painter chain, audio
  loop 24, pcm68k, terrain bins, frame bank 4, demo-render-overlap,
  render-overlap-integration, work-storage 4, staging relocation.
- **Known defects in the instrument, recorded not hidden:** the RETIRED
  marker fires on the *slave*, so the profiler's `notify_to_retire` and
  `finalize_ticks` compare two per-CPU FRTs and are **invalid** (discarded;
  the rig's VBlank figures stand), and that path makes the slave write
  three bytes of cached master-owned state — move it to `__uncached`
  before reuse. The harness exits 1 because `faults == windows` by design
  (the NOTIFY node is closed by `end()`), which also proves zero abandoned
  windows.

### Task T2.5: sub-probe `demo_prepare_mario()` — **complete (measurement + audit)**

**Status 2026-08-15 — evidence
`docs/saturn/evidence/reports/sprint2-t2_5-prepare-mario-audit.md`
(+ `.json`), diagnostic identity `id-4d501e08f75df139`, commits
`b7eea788` (instrumentation) and `053ee24c` (evidence):**

- **The stage is one function.** `actor_meshlet_live_depth_bounds()`
  (`src/port/saturn/gfx/saturn_actor_meshlets.c:411-477`) is **97.82% of
  `demo_prepare_mario()`** — 40,984 ticks / 5,245,905 cycles / 11.43
  VBlanks/frame / **20.7% of the whole frame**. The stage total (41,895
  ticks, 5,362,617 cycles, 69.28% of the window) reproduces T2.4's to
  within 0.27%, and `demo_prepare_mario`'s own self time is 5.6 ticks —
  0.013% of the stage.
- **Root cause, confirmed at instruction level:** `actor_saturating_mul_i64()`
  (`:95-110`) checks overflow by *dividing*, so every call emits libgcc's
  620-byte `___divdi3`. Ten calls per position visit × 1,408 visits/frame =
  **~14,080 64-bit software divisions per frame**, and a whole-image
  census shows this is **the only 64-bit-division caller on any hot path**.
  Measured 3,725.8 cycles per position visit, 12,647.7 per mesh vertex.
- **The work is done twice.** Pass 2 of `actor_meshlet_core()` recomputes
  every meshlet's depth bounds and span (`:634`), discarding the return
  value. The two instrumented passes measure **20,491.7 and 20,491.9
  ticks — 0.001% apart**.
- **Mesh confirmed:** 424 vertices / 644 primitives / 31 meshlets. But the
  governing count is **704 tier-0 position visits per pass**, walked twice,
  independent of camera/pose/LOD.
- **Mesh-reduction verdict: NO.** Halving the mesh leaves 5.72
  VBlanks/frame; fixing the arithmetic at *full* detail leaves ~0.2. The
  fix is ~30x better and costs no fidelity. Poly count is a linear factor
  on a constant that is ~25–37x too large.
- **No float and no other integer-division helper is reachable on this
  path** (checked in the linked image, not the source). The port's native-Q16
  premise holds here. `-Os` confirmed for the TU; no `noinline` anywhere.
- **T2.4's instrument debt is cleared.** The two cross-CPU FRT fields and
  both `mark_*()` entry points are **removed** (the RETIRED observer runs on
  the slave); with them go the only slave writes to master-owned state,
  which is additionally now `__uncached` (verified at `0x260FA8E4`, the P2
  alias). The fault accounting is fixed — `end_depth_max` is published and
  only depth > 1 is a fault — so **the harness now exits 0 with all twelve
  acceptance checks passing** (`faults = 0`, `end_depth_max = 1`). T2.4's
  wrap-headroom defect is resolved by construction: `max_raw_interval` fell
  from 54,192 to **18,591 (71.6% headroom)**, now equal to
  `spatial_admit`'s maximum to the tick.
- **Instrument honesty:** perturbation rose from 0.013% to a measured
  0.208% (164 probe events, `__uncached` state), cross-checked against
  `spatial_admit` — an unprobed 26%-of-window stage that reproduces across
  two independent builds and runs at 15,943 vs 15,942.8 ticks. 89% of the
  added cost lands inside the stage that received the probes.
- 798 windows, 26,181 emulated frames, movement witnessed at 42 distinct
  Mario positions, zero SH-2 exceptions, identity MATCH. Composition is
  stable across the route (depth share of stage 98.31% / 98.00% / 97.54% at
  three points spanning the run).
- Gates green on the diagnostic build: `verify-memory-map` RESULT OK
  (`hwram_remaining` 0x4CEC, true slack 11,756 B), painter chain, audio
  loop, pcm68k, terrain bins, frame bank, demo-render-overlap,
  render-overlap-integration, **verify-actor-meshlets** (the file most
  heavily instrumented, mutation fixture included), **verify-render-job-runtime**
  (the recipe T2.4 found broken; base HEAD `6b8cbe77` fixed it),
  work-storage, staging relocation, render-job-runtime source.
  `test_render_snapshot_source.py` still fails — **pre-existing**,
  reproduced by stashing the whole changeset.
- Product build **byte-identical** at object level for all three modified
  translation units — stronger than T2.4, which had to except two `assert`
  `__LINE__` literals.
- **Nothing was optimised.** T2.6 implements.

### Task T2.6: fix the depth-bounds walk — ranked on T2.5's table

Ordered by risk-adjusted value; full detail, gates and confidence in
`sprint2-t2_5-prepare-mario-audit.md` section 9.

1. **Carry pass 1's depth bounds and span into pass 2.** Saving **5.72
   VBlanks/frame (10.4% of the frame)** — a directly measured node, not an
   estimate. Cost: one `static` 31-entry array (620–868 B). **Bit-identical
   by construction** — it reuses values pass 1 already computed. Do this
   first even though item 2 subsumes most of it: it is free, safe, and it
   halves the surface item 2 must be validated against.
2. **Replace the depth loop's saturating `int64` arithmetic with per-actor
   algebra.** `depth(v) = dot(actor_pos − camera, forward) + dot(S⊙v,
   R_yawᵀ·forward)` — both leading terms are per-actor constants; per vertex
   it is three `dmuls.l` into an `int64` accumulator, the pattern
   `matrix_apply()` (`saturn_actor_pose.c:31-56`) already uses in-repo.
   Combined with item 1: **~11.2–11.3 VBlanks/frame, ~20.4% of the frame**;
   `demo_prepare_mario` drops from 11.69 VBlanks to ~0.4. **Not additive
   with item 1** — item 2 alone recovers ~11.05. Risk is numeric, not
   structural: `depth_bounds` feeds `actor_lod_tier()` and
   `actor_depth_bin()`, so a value change can shift an LOD tier or a painter
   bin and therefore change what is drawn. **Commit the equivalence oracle
   before the swap (T2.3's proven pattern), and a bin/tier shift is a visual
   regression that only the owner can clear.**
3. **Re-measure before choosing a third target.** After 1–2,
   `demo_spatial_admit()` (~4.45 VBlanks) becomes the largest block and the
   whole ranking changes. The rig is ready: 71.6% wrap headroom.
4. **Stage the two hot cart arrays into HWRAM** — only if 1–2 land.
   `sm64_mario_animation_vertices` and `sm64_mario_meshlet_lod_position_list`
   are in `.cart_rodata` at `0x22400000`, the SH-2 cache-through partition,
   so the loop makes ~5,632 uncached A-bus reads per frame. Worth ~1–2%
   today but a large share of what *remains* after 1–2. ~3,952 B of HWRAM
   against 13,848 B of product slack; needs the memory/ownership record
   first. Do not do this speculatively — T2.2 disproved the bulk version.
5. **Bake `demo_spatial_admit_node()`'s AABB centres** (static geometry
   recomputed per node per frame, `saturn_demo_render.c:800-808`) — later,
   after item 3 re-ranks.

**Explicitly NOT next:** mesh reduction (see the verdict above);
`always_inline` on the saturating helpers (items 1–2 delete the call sites);
any ±1 master/slave rebalance (the slave is still busy 1.02× its own
window — unchanged from T2.4); ranks 6–23 of T2.5's table (under 1% of the
window combined); further memory-tier work beyond item 4 (T2.2 disproved
it); further painter ordering (T2.3 measured the remainder at a fraction of
1.7%).

**Sprint gate:** owner-observed cadence materially above 1.1 FPS with accepted
visuals/audio intact. The 4 FPS floor re-binds on the sprint's accepted result.
