# State

**Authoritative goal:** [`docs/saturn/PRODUCT_GOAL.md`](docs/saturn/PRODUCT_GOAL.md)
**Active plan:**
[`docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md`](docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md)
**Status:** recovery diagnostic active; no current development CUE is
accepted. One isolated historical donor has booted to a BOB frame, but is a
diagnostic only—not the presentation artifact.

## Product truth

The repository has a historical BOB slice that the owner accepted at 4–6 FPS,
with a measured 5.294 FPS mean and normal controls/camera. It is constrained
and incomplete, but it is the visual/control/performance rollback oracle.

The current development tree is not an accepted improvement. Recent manual
observations found:

- Mario rendered with incorrect colors/material association;
- flat or incorrect Gouraud shading;
- incorrect front/back occlusion and painter order;
- Bob-omb textures and ground placement were wrong;
- no audible game music or SFX;
- at least one current run was approximately 1 FPS; and
- stale or wrongly profiled artifacts were launched during integration.

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
