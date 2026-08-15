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

### Task T2.5: attack `demo_prepare_mario()` — planned on T2.4's table

Primary target, measured: **Mario actor meshlet preparation, 69.2% of the
pre-notification window.** Sub-probe it before changing it (it has never
been decomposed internally), then choose between reducing it (pose/view
reuse across frames) and moving it into the job graph — the latter being
the only credible route to the split imbalance, since the slave is already
saturated inside its own window. Secondary: `demo_spatial_admit()` (26.4%).
Explicitly NOT next: a ±1 master/slave rebalance (no idle-slave slack
exists), ranks 3–15 (4.3% combined), further memory-tier work (T2.2
disproved it), further painter ordering (T2.3 measured the remainder at a
fraction of 1.7%). Clear the instrument debt above first.

**Sprint gate:** owner-observed cadence materially above 1.1 FPS with accepted
visuals/audio intact. The 4 FPS floor re-binds on the sprint's accepted result.
