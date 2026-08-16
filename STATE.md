# State

**Authoritative goal:** [`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md)
**Active branch:** `saturn/recovery` (worktree `.worktrees/saturn-recovery`)
**Active plan:**
[`docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md`](docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md)
**Status:** **Sprint 1 (R0+R1) COMPLETE and owner-accepted 2026-08-15.**
**Sprint 2 (cadence recovery) — investigation phase COMPLETE 2026-08-15.**

**Current measured cadence: 4.9432 FPS / 12.1379 VBlanks per frame** on
`id-05046d9d5d8a5593` (T2.12), against A9A's 5.294 FPS / 11.333 VB. The gap is
now **1.071x** — 0.805 VB/frame — and the medians are equal at 5.0. Source:
`docs/saturn/evidence/reports/sprint2-t2_12-hierarchical-admission.md`,
29 intervals, `summarize_cadence`, 1% low 4.6154. Predecessors on the same
basis: `id-b46f60d0a6d129dd` 4.3176 / 13.8966 (T2.10+T2.11, owner-accepted),
`id-6eca5970628d581d` 3.8753 / 15.4828.

> **Retired figures — do not cite.** The 1.0682 (T2.2), 1.0866 (T2.3) and
> 1.4634 (T2.6) FPS numbers were hand-computed from capture *failure
> diagnostics* over `vblanks_advanced`, which includes the ~1,549-VBlank
> pre-gameplay ramp. A9A's 5.294 always came from `summarize_cadence` and
> was never contaminated. All cadence figures must come from the summarizer.

What the sprint established, in order:

- **T2.1** — every capacity-shrink gate measured safe (VDP1 peak 653 of 2048).
- **T2.2** — 67,584 B of HWRAM reclaimed and the full 54,080 B hot working set
  returned to 32-bit memory: **no cadence change.** The memory-tier hypothesis
  is disproved as the lever. Banked anyway: true slack 472 B -> 13,944 B, and a
  real latent `gGfxPool` overflow corruption fixed en route.
- **T2.3** — painter counting sort, 20.6x fewer steps, but only ~+1.7%: the
  reference sweep's step estimate assumed ~1,800 live commands where T2.1 had
  measured 653.
- **T2.4/T2.5/T2.6** — `demo_prepare_mario()` was 21% of the frame because
  `actor_saturating_mul_i64()` **checked overflow by dividing**
  (~14,080 libgcc `___divdi3` calls/frame) and the meshlet walk ran twice.
  Both fixed; that stage fell **95%** (3,725.8 -> 102.9 cycles/visit) with
  bit-identical output across 685,456 equivalence cases.
- **T2.7** — the contamination above; the real gap is one module.
- **T2.8** — **VDP1 measured idle: 0 waits across 1,349 fence events**,
  `vdp1_sync()` proven non-blocking, ~552 commands/frame against a 1,664
  capacity. **We are CPU-bound.** Every fill-rate lever (user clipping,
  command-count LOD, HSS, the Mario double-emit) is demoted as a cadence lever.
- **Arithmetic census** — divides essentially fixed, 64-bit healthy, but
  **1,827 hot-reachable soft-float sites survive, 150 of them soft-double**,
  including double-precision `sinf`/`cosf` on the per-frame matrix
  path. Also: the in-tree native-math verifier now fails (1,402 helpers vs a
  pinned 582) — number established, cause not.
- **T2.9** — **`spatial_admit` does not need to cost what it costs.**
  `SM64_SATURN_BOB_ADMISSION_NODE_COUNT` is **1**: the spatial index is a
  single node holding all 867 cluster refs, a flat list with a tree's type
  signature, so no early-out is possible and cost never falls with visibility
  (`clusters_tested` = 867, min = max, every sample). 71.8% of it is
  literally constant and 27.7% (a dedup scan that has **never found a
  duplicate**, K(K-1)/2 = 39,903 compares/frame) gets *worse* the more is
  visible. Defensible cost 0.5-0.8 VB; **recoverable ~3.8-4.0 VB = 24-26% of
  the frame, all generically.**

- **T2.10** — T2.9's three generic fixes landed. `demo_spatial_admit()` fell
  **40.5%** (16,588.1 -> 9,871.8 FRT ticks) with **zero** divergences across
  516,090 classifier cases and 1,024 admission poses. Item 3
  (cross-multiplied divides) is bit-identical but a **measured regression**,
  +0.075 VB. No FPS could be reported: the summarizer aborted twice.
- **T2.11** — **the summarizer is repaired and T2.10 is confirmed at the frame
  level: 4.3176 FPS / 13.8966 VB/frame, −1.586 VB and +11.4% against
  `id-6eca5970628d581d`**, with the whole saving landing in `construction`
  where the three fixes were made. Root cause of the abort: `construction`
  subtracts a slave-work window whose opening boundary `retirement_vblank` is
  stamped **on the slave SH-2**, while the master is still inside the source
  tick the frame pipeline admitted — so those crossings were charged to both
  `construction` and `simulation`. The guard now subtracts
  `min(simulation, master_finalization)`, an upper bound on that double count
  read from the same trace, and is unchanged (zero allowance) for v1 traces.
  Control reproduces T2.7 to every digit.
- **T2.12** — **the spatial index is a real tree at last: 255 nodes over the
  867 BOB clusters, with the INSIDE short-circuit. 4.9432 FPS / 12.1379 VB,
  −1.7587 VB and +14.5%** against `id-b46f60d0a6d129dd`; −1.2414 VB of that
  lands in `construction`, where `demo_spatial_admit()` lives, meeting T2.9's
  1.0-1.2 VB estimate at its upper end. `clusters_tested` finally **varies with
  the view — 0 to 548** against a flat pass that read 867 every frame without
  exception; total tests fall to 31.6% of flat. Both halves fit: the generator
  owned the whole admission section in one function (+95/−20 lines of Python)
  and the node array lives in cart `.cart_rodata` (9,180 B, no work RAM). The
  1,183-node BSP was correctly **rejected** as the data source — it indexes primitive
  spans, not cluster refs. Admitted output is **byte-identical**: 48,200
  comparisons over the real cluster bank, 0 divergences, new gate `verify-admission-hierarchy`.
  Two hazards the brief did not anticipate, both found by failing first: AABB
  quantisation makes OUTSIDE pruning **non-monotone** (fixed with a derived
  8-unit margin on node tests; 102 poses caught), and output order had to stop
  depending on traversal shape (96 mandatory clusters/frame sit in pruned
  subtrees). Mutations: OUTSIDE-inverted, INTERSECTS-dropped and margin-removed
  all KILLED; both INSIDE short-circuit removals SURVIVE **by construction** — a
  correct short-circuit is unobservable in output, which is why the prune and
  descent mutations are the real guards.

**Next:** the arithmetic census's 1,827 soft-float sites (incl. double-precision
`sinf`/`cosf` on the per-frame matrix path), the Mario double-emit (two VDP1
commands per textured primitive, the lower one provably invisible), or T2.9's
remaining items 5-8. The BOB bypass stays demoted to diagnostic value only —
the generic fixes recovered comparable time and keep charter D6 intact, and
T2.12 in particular scales *better* as levels grow.

**Owner gate CLOSED 2026-08-16.** `id-b46f60d0a6d129dd` was look-and-listened
on desktop Ymir: **visuals good, sound working,
owner observed 4-5 FPS by eye.** This closes the gate on `id-6eca5970628d581d`
as well — it is superseded on cadence and bit-identical in output. The
naked-eye reading independently corroborates `summarize_cadence`'s 4.3176 FPS
mean (1% low above 4), which is further confirmation that the T2.7
measurement contamination is behind us: observed and measured cadence now agree.

**Open owner gate: `id-05046d9d5d8a5593` (T2.12).** It is byte-identical in
admitted content to the build the owner just accepted, so it carries no known
visual or audio risk — but it has not been look-and-listened, and the measured
jump from 4.3176 to 4.9432 FPS is the kind of change worth confirming by eye.

**Known RED gate, not T2.12's doing:** `verify-render-clusters` fails because its
recipe passes `-I src/port/saturn/gfx` while `1ec76248` gave `ztreme_hot_promotion.c`
a `src`-relative include. Same class as the 22 latent Makefile recipe defects
T2.10 flagged. Filed, not fixed.

**Watch item:** T2.11's cadence allowance is being spent 3.5x harder — needed on
**14 of 29** intervals here vs 4 of 29 at T2.10, though still only 1 crossing each
inside a 4.66 allowance. Not blocking at 12 VB/frame; it will be at 8.

## Product truth

**There is now an owner-accepted current CUE: `id-86d3880727ed1d10`.**
ELF `b8754557…b501`, ISO `d952aea4…521a`, preserved at
`releases/2026-08-15_0705/id-86d3880727ed1d10/`.

Owner-observed and accepted at the gate:

- **Audible looping music**, started by the source game's own `play_music`
  call through the semantic API into the MC68000/SCSP driver — the first game
  audio in this project's history.
- **Visuals accepted** as non-regressed against the A9A oracle (Mario,
  terrain, camera, input).
- Cadence ~1.1-2 FPS: recorded, and explicitly **non-blocking for R1** by
  owner instruction (`0ad5fb31`). It is Sprint 2's first objective.

The A9A slice (5.294 FPS) remains the immutable historical oracle at
`build/saturn/baselines/a9a-2026-08-05/`. The regressions it was contrasted
against were diagnosed as configuration-attributable and are neutralised in
this candidate's feature tuple.

One owner-observed artifact — a periodic piercing noise — was investigated to
mechanism and **dispositioned as a Ymir host-audio underrun, not a port
defect** (Ymir's `ProcessAudioCallback` drains without underrun detection;
this build is a pathological slow producer). Full elimination chain:
[`docs/saturn/evidence/reports/sprint1-r1-owner-gate.md`](docs/saturn/evidence/reports/sprint1-r1-owner-gate.md).

## What Sprint 1 delivered

- The donor worktree's entire unlanded state preserved in git (117 tracked
  modifications + 53 zero-history files), byte-verified.
- `AGENTS.md` product-gate constitution restored.
- MC68000 driver diet: state 2,080 B -> 104 B off a 1,020 B stack — the root
  cause of the `0x0340` audio failure; image 13,520 B -> 5,883 B.
- Music as a hardware-looped SCSP sample (`wav_to_pcm8.py`, packager
  `--music-pcm`, loop-bit validator), plus `render_m64_wav.py` to produce the
  owner's ROM-derived music asset.
- `SEQ_START`/`SEQ_STOP` handlers, music pinned to slot 0, SFX on slots 1-3.
- Audio boot fail-open, hardened for never-zeroed LWRAM.
- Three-way work-storage split after measuring 49,648 B of committed HWRAM
  overflow; memory-map margin is now a build output (`verify-memory-map`).
- Continuous SFX no longer re-keyed every frame.

Host tests, SH-2 compilation, manifest verification, and review verdicts do not
override those observations.

## Accepted rollback artifact

Path: `build/saturn/baselines/a9a-2026-08-05/`

- ELF:
  `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
- ISO:
  `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
- CUE:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`

This artifact must not be rebuilt or overwritten.

## Active presentation milestone

Produce one newly built and uniquely identified CUE that:

1. runs normal BOB with correct controllable Mario, correct fixed Gouraud,
   correct occlusion/order, a normally spawned textured Bob-omb at the correct
   height, real music, and a game-triggered SFX;
2. runs a minimum Whomp’s Fortress proof in the same executable using the same
   game loop, renderer, Mario path, scene/package path, and audio backend;
3. never falls below 4.0 mean FPS during integration and reaches at least 6.0
   mean FPS before presentation acceptance; and
4. has no exception, allocation failure, stale generation, or scene-wide stall
   caused by unsupported content.

Both levels are required for the end-of-week artifact. The immediate target is
6–10 FPS; performance claims bind one exact CUE/profile/capture.

## Architecture status

### Proven and retained

- Original SM64 gameplay, level, Mario, object, camera, collision, and animation
  ownership.
- The immutable A9A artifact and its measured cadence.
- SH-2 native-math and cadence corrections with target evidence.
- Cartridge/linker placement and bounded address/DMA primitives with target
  evidence.
- The standalone owner-accepted MC68000/SCSP sound path as an integration donor.

### Probationary

- S64P/S64F/S64B actor packages and material compilers.
- Generic actor queues, meshlet workspaces, texture residency, and scene
  publication.
- Scene-derived audio bundles and semantic event transport.
- Render overlap/publication changes not preserved by a current accepted CUE.

Probationary work is reused only when it advances the next live artifact within
two causal attempts or two hours. Otherwise it is bypassed or selectively
transplanted onto the accepted baseline.

### Not yet proven

- Whomp’s Fortress scene generation/load; no generated WF scene closure exists.
- Retail title/menu/file-select flow; sourceboot currently bypasses it for BOB.
- A normal, audible, visually correct generic actor path.
- Current all-features performance or release readiness.

## Immediate execution boundary

The working tree contains extensive uncommitted product and infrastructure
changes. Preserve it as a donor; do not flatten, reset, or broadly stage it.
Before further behavior work:

1. verify the immutable A9A artifact and capture its visual oracle;
2. record the exact current candidate source/profile/artifact identity;
3. compare current Mario/BOB/audio/FPS against A9A;
4. choose the smallest donor transplant for the first failing product gate; and
5. build and boot after that one change.

No release reproduction, new wire format, all-actor campaign, generalized level
framework, or broad review wave is active.

## Task 1 evidence status — 2026-08-13

The A9A archive hashes were recomputed and exactly match the immutable values in
`PRODUCT_GOAL.md`; it remains the historical accepted rollback oracle. Its
parent Ymir profile configuration is historically pinned and currently matches
SHA-256 `33a155e765dac9bd2871ca725ed7f444d1fbbb95876d43c955c0b688e6931566`.
The worktree-local profile was not used.

Task 1 launched bounded, identity-bound headless observations using explicit
USA-BIOS, game-CUE, and `--dram-cart` arguments with build-agent2 headless
Ymir. The historically pinned parent `Ymir.toml` is comparator evidence only;
the headless client did not consume it. The A9A archive layout cannot satisfy
the diagnostic tool's required `obj/<cue>.elf` layout, so the same-hash original
sibling-layout tuple was used without changing the archive; its boot-trace
diagnostic failed after 1,680 emulated frames. The newest generated tuple
reached exact linked-code and embedded build-identity matches, then failed
cadence decode before any presentation event. Neither headless diagnostic
captured video, so neither establishes a Mario/BOB visual gate or advances
acceptance. Exact reports and hashes are in
`docs/saturn/evidence/reports/current-product-gate.json`.

## Task 2 status — scene-ready donor observed; no acceptance

The current sourcebuild graph still rejects its actor-family package before
SH-2 compilation even when dynamic actors are disabled. That failure blocks a
new current-tree CUE.

An isolated historical donor at `d7b04d61` plus donor-only extractor
compatibility commit `5808cdbf` did build and boot a separate CUE. At 3,420
emulated VBlanks it produced BOB terrain and a Mario draw; the exact CUE/ELF/ISO
and VDP1 command evidence are recorded in
`docs/saturn/evidence/reports/2026-08-13-bob-convergence-handoff.md` and
`docs/saturn/evidence/reports/current-product-gate.json`. The current manual
desktop launch uses that named donor CUE under the parent Ymir profile whose TOML
still hashes to the pinned `33a155e7…931566`.

This is **diagnostic evidence only**. The observed donor frame does not prove
correct Mario scale, animation, face order, occlusion, controls/camera,
normally spawned Bob-omb rendering, audible game audio, or cadence. A source
comparison and target command probe closed the proposed duplicate Mario
color-mode patch: both donor and current lowerers already emit the accepted
per-material Gouraud plus RGB1555 `CC_REPLACE` contract. Do not build a duplicate
renderer-only CUE. The next behavior change must follow either a manual verdict
on the named donor or a distinct target-observed discrepancy.

The four direct sourceboot-audio CUE variants are terminal negative evidence;
their rebuilt sound-active guard remained zero after target observation. The
next audio task is target telemetry for the game-entry-to-driver activation
boundary—not a fifth sourceboot audio variant. Normal Bob-omb, audio,
performance, and Whomp's Fortress remain closed until a current generic BOB
artifact is observed and accepted.

## Documentation authority

The authority order is:

1. current owner instruction;
2. `docs/saturn/PRODUCT_GOAL.md`;
3. this file and `ROADMAP.md`;
4. the single active product plan and ledger;
5. architecture/build documentation; and
6. historical plans, audits, handoffs, and evidence.

Historical documents retain useful measurements and source research but cannot
authorize implementation.
