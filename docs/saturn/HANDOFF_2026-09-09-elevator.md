# Saturn port handoff â€” 2026-09-09 elevator checkpoint

## Resume state

Work is PAUSED at the owner's request. This handoff does not resume implementation or launch an emulator.

Worktree: `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery`
HEAD: `828a8877607d9541648d69d3bfe84b0bc7e9b3cc`. Substantial preexisting dirty work is intentional; preserve it. No commit or independent review was made for the elevator change.

Product phase: active development. Confirmed actor visibility and original source motion are durable contracts; route/camera diagnostics are volatile. Defects leave an invisible or incorrectly oriented collision platform. The overall goal remains at least 12 FPS with playable presentation; it is unmet.

Read authority in this order:

1. `docs/saturn/PRODUCT_GOAL.md` and applicable AGENTS.md.
2. Active plan: `docs/superpowers/plans/2026-08-22-sprint3-playable-bob-wf-menu.md`.
3. Active ledger: `.superpowers/sdd/2026-08-13-mario-port-product-recovery/progress.md`.
4. This handoff and `docs/saturn/evidence/captures/2026-09-09-terrain/elevator-result.json`.

Older paragraphs in STATE/ROADMAP and historical plans contain superseded candidates. The latest elevator pause checkpoint in the active plan/ledger is authoritative for this task. Do not open another architecture plan.

## What is proven, and what is not

User reported that the elevator platform does not render. Existing BOB closure contains family 47 / model 0xCA (`MODEL_CHECKERBOARD_PLATFORM`). Actual-source compilation initially rejected it as `unsupported rigid-group source: textured`: its key was missing from the supported material admission list.

Added that exact key and capture digest to the existing generic actor asset pipeline. New generation 97 builds and boots. A normal hardware-pad route reaches all 18 checkpoints. Live object-pool reads confirm both elevator children move and their pitch changes. Opening cadence with SH-2 cache emulation ON is **6.062717770034843 FPS**, versus prior candidate's **6.083916083916084 FPS** on the same cache basis.

**Elevator visual correctness is NOT verified.** Inspected candidate pitch-start/5632/8192/10240 screenshots are obstructed by terrain and do not clearly establish checkerboard-platform drawing. Do not call the elevator fixed or the presentation gate passed. The gray surface in the pit is not sufficient evidence of the admitted checkerboard asset. Generic actor world lowering also applies yaw only, while the elevator source rotates pitch; no transform correction was attempted.

## Exact artifacts to preserve

New candidate directory:
`build/saturn/sourceboot/e2-bob-identity-id-304954ef4ce27590`

- Profile: `sourceboot-bob-manual`, generation 97; live input ON, replay OFF, bootstrap 0, camera 3, pipeline 4, polygon tier 2, slave actors/rendering ON.
- Build completed: **2026-09-09 11:31:32 EDT**, exit 0.
- Source closure SHA-256: `595cd5620b9676d5dc1bd3d1a444c503c4c011819d095c903b317e830d6b403b`.
- Effective configuration SHA-256: `304954ef4ce27590e81e0ba8a7a45aa0f0ff16ab6eb0885887b81d2111fa6672`.
- `sm64-saturn-sourceboot-e2.cue`: `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`; mtime **11:29:59.337131 EDT**.
- ISO: `7857b92d85d8ba8ef22d8a489ee9854313e3080180b5c84993c906c2955dd47a`.
- ELF: `ef4d913aea4342fce5c0a83bd4b226664e8b9ea32209f2af1bc18d5f4f183f01`.
- Artifact directory retained: **285,188,020 bytes**, for exact live comparison. CUE text hashes repeat between builds; pair the CUE with its directory, ISO and ELF identity.

Preserve preceding coin candidate `3845f2d7ee9b7a18`, generation 96. It has live covered/exposed coin evidence, 6.67 FPS cache OFF / 6.08 ON. Identity is in `coin-order-identity.json`. Preserve faster bridge candidate `658acfabda0878f1` (7.16 OFF / 6.49 ON) and original owner-accepted archives named in PRODUCT_GOAL. Do not rebuild over an accepted artifact.

No GUI Ymir launch occurred in this task. The last known manual session used older candidate `e2e2daffaa8630ac`; do not assume it is current or terminate it. Before any manual launch, state exact CUE hash, build time, source state and profile, and verify ISO/ELF identity.

## Changes and source references

- `tools/saturn/actor_material_v2.py`: add `(47, 0x00CA)` to `BOB_DIRECT_TEXTURED_KEYS` and capture SHA `c5de7c417a3aa277e2c3aabd4918813ad9ff6150b025682644c8f07d5b59f57e` to `_EXPECTED_CAPTURE_HASHES`.
- `tools/saturn/inventory_actor_family_bundles.py`: texture credit 36,096 -> 39,424 bytes; CLUT credit 7,392 -> 7,648. No physical pool expansion.
- `tools/saturn/test_actor_material_v2.py`: focused `ActorMaterialV2Test.test_checkerboard_elevator_source_is_admitted_within_existing_buffers`, checking current family/model association and actual compilation/resources. Other historical all-roster tests in this file contain older frozen admission expectations; they were not run or broadly rewritten.
- Existing evidence observer `docs/saturn/evidence/captures/2026-09-08-12fps/capture_manual_input.py`: read-only checkerboard child observations during routes. Object size 0x260, behavior pointer offset 0x20c, active flags 0x74, position 0xa0, face angles 0xd0, action 0x14c. Reuses existing pool scan; no game-memory writes.
- `elevator-route.json`: 18 ordinary-pad checkpoints, ending near (1612,768,4950). Reached live, but subsequent camera settling can put Mario in the pit; not a rideability or unobstructed-view proof.
- CHANGELOG, STATE, ROADMAP, active plan and ledger updated with candidate/open status.

Inspected existing project source, reused directly without external algorithm or format:

- `levels/bob/script.c`: group at (1612,300,4611), parameter 0x00280000.
- `src/game/behaviors/checkerboard_platform.inc.c`: two children initially offset Z Â±145, scaled (0.7,1.5,0.7), vertical movement and pitch half-turns; collision remains source-owned.
- `data/behavior_data.c`: group spawn/deactivation and child init/loop.
- `actors/checkerboard_platform/{geo.inc.c,model.inc.c}` and original PNG assets.
- Existing actor compiler/admission and `src/port/saturn/gfx/saturn_demo_render.c`.

`demo_generic_actor_world_vertex` currently rotates around Y only. Existing `sm64_saturn_mtxq_rotate_zxy_and_translate` in `saturn_matrix_ctors.h` mirrors source `mtxf_rotate_zxy_and_translate` in `src/engine/math_util.c`; inspect and reuse applicable existing math before proposing a new transform implementation. Do not add platform-ID renderer branches.

## Memory and publication

Compiled platform: 8 vertices, 12 triangles, 4,600-byte bank, 144-byte lane, 291-byte scratch, 3,328-byte texture span, 256-byte CLUT, 12 commands per instance. The existing 6,144-byte workspace and 4,096-byte upload stage suffice; whole-bank size is not upload-span size.

Unchanged actor VDP1 pool: 52,672 bytes. New total credited residency: 47,072 bytes; **5,600 bytes remain**. Existing loader stages CART spans through LWRAM to VDP1, owning lifetime, scene reset, generation and publication. No new arena, DMA route, ownership transfer or stale-generation policy.

Link margins unchanged: HWRAM **8,180 bytes**, LWRAM **49,568 bytes**. Protected HWRAM floor is 7,936 bytes: only **244 bytes beyond the floor**, so a later code change must check the target map. Earlier owner authorization allows bounded cold-code CART placement up to 160 KiB; do not silently start a new capacity campaign.

Generation 96 is preserved. Generation 97 publication is immutable; do not overwrite it if asset inputs change. First build failed because the sourceboot path uses `--verify-publication` and fresh 97 did not exist. Resolved by running the existing `compile_actor_family_bundle.py` producer with closure/family report/model IDs and generation 97, then retrying unchanged build flags. No build-system repair was made.

## Verification and evidence

Evidence root: `docs/saturn/evidence/captures/2026-09-09-terrain/`.

- `elevator-source-probe.json`: measured pre-change in-memory admission probe.
- `elevator-focused-test.log`: 1 focused actual-source test PASS.
- `elevator-build.log`: incremental target build exit 0; build-mandated schema tests 27 (2 skipped), determinism tests 3 pass.
- `elevator-prerequisite-failure.log`: missing fresh publication failure, retained for diagnosis.
- `elevator-baseline-live.json` and PNGs: old candidate and live invisible-asset baseline; no claim of identical replay state.
- `elevator-candidate-live.json` and PNGs: identity/manual-input/route/cadence evidence.
- `elevator-identity.json`, `elevator-result.json`, `elevator-cleanup.json`.
- Changed-file `git diff --check` passed. No independent review or commit. No audio fidelity, rideability, arbitrary terrain regression or two-level acceptance claim.

Scratch `D:/tmp/codex/sm64-elevator-20260909` removed and absence verified after exact-path/reparse checks; **4,280,599 bytes** reclaimed. Logs and manifest retained with hash verification. Elevator evidence measured **10,035,831 bytes** before result-file addition (roughly 10 MB), deliberately retained. No disposable elevator scratch remains.

## Next bounded action after owner resumes

1. Reconcile HEAD/dirty source, active plan/ledger and saved identities. Do not rebuild just to familiarize yourself.
2. Use candidate 3049 and normal pad input to obtain an unobstructed elevator view. Trace family 47/model CA through identity registry, instance queue and actual VDP1 emission. Distinguish missing submission from terrain occlusion/camera clipping. Current screenshots alone cannot select a cause.
3. If platform commands exist, inspect original pitch against emitted vertices. Fix one demonstrated cause through existing generic path, retaining source collision and motion. The yaw-only limitation is known, but not yet a demonstrated explanation for total invisibility.
4. Build a uniquely identified candidate and immediately make the earliest live comparison before broadening. Do not advance on host tests alone. Stop after two causal attempts or two hours without new live observation and compare/bypass/rollback as required by PRODUCT_GOAL.
5. Update active plan and ledger with actual outcomes; keep failed/unrun gates open. Preserve the user's pause unless they resume work.

## Tools and repeatable observation

Python: `.venv-saturn-tools/Scripts/python.exe`.
Headless Ymir: `D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`.
BIOS: `D:/Code/RetroDev/sm64-saturn-port/work/upstream/sonic-z-treme/Emulators/mednafen/firmware/mpr-17933.bin`.
NM: `D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-nm.exe`.

Run the existing observer with `--build build/saturn/sourceboot/e2-bob-identity-id-304954ef4ce27590`, `--summit-route .../elevator-route.json`, `--camera-pitch-sweep`, `--sh2-cache`, and a NEW evidence output name. `--measure-cadence` adds opening observations and changes route timing; compare cache modes separately. The observer validates build identity and quits its own headless process in finally.

One coordinator owns builds. No new agent or task was created. The user's broader requests remain: improved camera, better slave/VDP use, terrain warping/occlusion, cannons/King Bob-omb, other stages, and 12 FPS. Skybox/facing/autoplay independence have prior work but are not revalidated by this elevator checkpoint.

## Resumed 2026-09-09 -- registry fix (cause closed; visibility gate still open)

The handoff's step 2 trace was completed statically: family 47 / model 0xCA was in the gen-97 bundle but absent from the identity registry, because `actor-families.json` marked it unsupported for `GEO_CULLING_RADIUS` and the registry generator trusts that flag before the compiled set. Two producers held opposite verdicts on one node; the bundle's allowlist selection hid it. Fixed by accepting `GEO_CULLING_RADIUS` in `tools/saturn/actor_source.py` (structural; no geometry; this port culls at cluster granularity).

New candidate: `build/saturn/sourceboot/e2-bob-identity-id-fc2d2bbb622189f4`, generation 98, `sourceboot-bob-manual`, built 13:41:11 EDT exit 0. Registry 45->47 rows; 28 banks unchanged; HWRAM margin 8,180 B unchanged (registry table is CART). Generation 97 (`304954ef4ce27590`) preserved. Full identity, static and live figures: `docs/saturn/evidence/captures/2026-09-09-terrain/elevator-g98-result.json`; live run `elevator-g98-candidate-live.json` + PNGs.

Live: cache-ON elevator route passes; spatially matched telemetry near the elevator +12.5 actor prims/frame and +19 VDP1 commands/frame vs 3049, matching one child in frustum. Consistent with submission, **not proof of drawing** -- no capture frames the pit. Do not call the elevator fixed.

Next bounded action: an unobstructed view (route/camera framing the pit), then the pitch-vs-yaw transform. Tests updated to follow the fix (admission map, effect oracle). **No commit was made**: the tree carries ~48 dirty files from prior sessions with no commit boundaries, and staging `CHANGELOG`/`STATE`/`ROADMAP` would sweep their uncommitted entries in; that is the owner's call.

### Pit-view attempt (2026-09-09, STOPPED; drawing still unverified): elevator-route-pit.json appended waypoint [1612,180,4650] to drop into the pit so route-final's trailing camera would frame the platform at close range. Result STUCK at waypoint 18 -- Mario stayed on the rim at (1616,768,4963); the rim is fenced and the walker only jumps for rising targets, so the 588 u drop is never attempted; camera->child 1,040-1,631 u at every label; route-final is the same bridge view. Sweep-row comparison at identical yaw/pitch was mixed (VDP1 +29/+21/+31 at three points, -14 at one) because Mario is 100-400 u apart between runs -- suggestive of submission, not decisive. The camera already faces the pit (yaw 0, off 0-3 deg); final_yaw is not the lever, distance is. Two observation attempts made after the one causal fix: stopping per the handoff rule. Next bounded action: approach from BELOW (the elevator is ridden upward from the lower gate area), or a replay-identical 97-vs-98 A/B so a per-frame command/frame-hash delta is unambiguous. Evidence: elevator-g98-result.json (amended), elevator-g98-pit-live.json + PNGs, elevator-route-pit.json.
