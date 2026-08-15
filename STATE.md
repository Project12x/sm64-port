# State

**Authoritative goal:** [`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md)
**Active branch:** `saturn/recovery` (worktree `.worktrees/saturn-recovery`)
**Active plan:**
[`docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md`](docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md)
**Status:** **Sprint 1 (R0+R1) COMPLETE and owner-accepted 2026-08-15.**
**Sprint 2 (cadence recovery):** T2.0 reference sweep, T2.1 peak capture,
T2.2 reclamation+un-split and T2.3 painter counting sort all COMPLETE
(2026-08-15). **T2.2's result is the
load-bearing one: recovering 67,584 B of HWRAM and returning the full
54,080 B hot working set to 32-bit memory did NOT move cadence** (1.068 FPS
vs the R1 baseline's comparable 1.071). Memory objective met — true slack
over the floor went 472 B → 13,944 B, and a real latent `gGfxPool` overflow
corruption was fixed en route. Cadence objective not met. The memory-tier
hypothesis is narrowed, not refuted (`_sourceboot_fast3d`, 44,616 B, is still
in LWRAM; its unlock is the T2.0 L3 staging-window architecture, which needs
its own CUE). **T2.3 painter counting sort COMPLETE (2026-08-15,
candidate `id-aa57d83c898e3af1`): correctness objective met — the relink is
a byte-identical counting sort, 20.6x fewer record visits — and cadence
moved 1.0682 -> 1.0866 FPS (+1.72%), a real but small gain.** The saving is
0.97 VBlanks/frame and its attribution is exact: master finalization
5.80 -> 4.83 while the pre-notification window (18.92), slave overlap (2.73)
and simulation (6.07) counters are bit-identical. Honest read: the rescan
was ~4% of construction, not the bulk of it — T2.0 L8's "115,200 steps"
assumed ~1,800 live commands, but T2.1 measured 653.
**T2.4 and T2.5 COMPLETE (2026-08-15, measurement only — nothing
optimised): the pre-notification window has been decomposed twice, and the
bottleneck is now named, located and explained.** T2.4 built T2.0 L14's FRT
sub-stage profiler and found the window is two stages —
`demo_prepare_mario()` 69.2% and `demo_spatial_admit()` 26.4%, 95.7%
together, with the parts summing to 99.95% of the whole. T2.5 sub-probed
the first: **97.8% of `demo_prepare_mario()` is one function,
`actor_meshlet_live_depth_bounds()`, at 5,245,905 cycles/frame = 11.43
VBlanks = 20.7% of the entire frame.** Root cause confirmed at instruction
level: `actor_saturating_mul_i64()` checks overflow *by dividing*, so every
call emits libgcc's `___divdi3` — **~14,080 64-bit software divisions per
frame**, and it is the only 64-bit-division caller on any hot path in the
image. The walk also runs **twice** per frame (the two instrumented passes
measure 0.001% apart). Measured cost: 3,725.8 cycles per position visit,
12,647.7 per mesh vertex, against a defensible ~100–150. **Mesh reduction
is NOT the lever** — halving the mesh leaves 5.72 VBlanks/frame; fixing the
arithmetic at full detail leaves ~0.2. **T2.6 implements**, ranked in the
plan: (1) carry pass 1's bounds into pass 2 — 5.72 VBlanks, bit-identical
by construction; (2) replace the saturating `int64` arithmetic with
per-actor algebra — ~11.2 VBlanks combined, ~20% of the frame, but it can
shift an LOD tier or painter bin so it needs an equivalence oracle and
owner sign-off. T2.5 also cleared T2.4's recorded instrument debt: the two
cross-CPU FRT fields are removed, the profiler state is `__uncached`, the
fault accounting is fixed (the harness now exits 0 with all twelve checks
passing), and FRT wrap headroom went 17% -> 71.6%. Open: owner
look-and-listen on `id-6b7c7e5d5f71e809` (T2.2's capacity cuts, inherited by
this build). Evidence:
`docs/saturn/evidence/reports/sprint2-t2_2-reclaim-unsplit.md`,
`sprint2-t2_3-painter-counting-sort.md`,
`sprint2-t2_4-prenotification-profile.md`,
`sprint2-t2_5-prepare-mario-audit.md`.

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
