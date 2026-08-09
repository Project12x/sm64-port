# Task 14 Completion Plan (geo-walk cutover, identity registry, memory-budget closure)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish governing-plan Task 14 (`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md:751-803`): convert the production geo walk off the crash-proven recursive dispatcher, wire the generated actor-identity registry into the observer seam so real objects admit, close the HWRAM/LWRAM link budget, and produce the first green link + boot evidence this branch has had since the arenas landed.

**Architecture:** Three converging lanes. (1) The geo-walk cutover replaces 24 direct recursive `geo_process_node_and_siblings()` calls in `src/game/rendering_graph_node.c` with the already-landed bounded enter/leave runtime (`saturn_geo_walk_runtime.h/.c`, commits `937f0043`/`a48e5aac`) — this is also the accepted permanent fix for the decoded master-stack-overrun boot crash. (2) A build-time registry generator derives (geo layout, behavior) → (family_id, actor_bank_id, bank hash, package generation) from Task 11's generated actor family bank and feeds `saturn_source_observe_object_begin()`, unblocking `valid_observation()`'s fail-closed gate. (3) Budget closure is measure-first: a fresh RED link + `.map` inventory, then owner-approved relocations/right-sizing until all four linker asserts pass.

**Tech Stack:** SH-2 C (Yaul SDK), GNU ld linker scripts, Python 3 host tooling (`tools/saturn/*.py` conventions), MSYS2 `mingw32-make` via `tools/saturn/with-msys-toolchain.ps1`, Ymir headless captures.

**Sequencing vs. sibling plans:** This plan is first — `2026-08-07-task16-completion.md` needs Task 3 (registry) here for nonzero admission and needs Task 6's green link for any target evidence; `2026-08-07-task12-completion.md` is independent and can run in parallel. Within this plan, Task 2 (geo waves) and Task 3 (registry) are independent of each other; Tasks 4-5 (budget) can start after Task 1's baseline at any time.

**Standing constraints (owner-set, from the SDD ledger, `progress.md:243`):** "No single-SH2 substitution, texture workaround, capacity shrink, or link-margin weakening is authorized." Any right-sizing proposal below therefore requires explicit owner sign-off via AskUserQuestion before implementation, with the measured evidence in the question. The accepted 4-6 FPS BOB rollback baseline and the feature-off Mario path are immutable.

**Current ground truth (2026-08-07 research pass; all verified, not assumed):**
- All five review findings from the `7cb28651..508a8de1`-era FAIL rereview are repaired and independently re-reviewed PASS **except** I3's package-reservation half, which is deliberately deferred to Task 16's drain (plan doc lines 1366-1378) — this plan does NOT own it; `2026-08-07-task16-completion.md` Task 4 does.
- Geo cutover is 0% done: exactly 24 direct recursive call sites remain (`rendering_graph_node.c:405-1577`; dispatcher definition at `:1441`). Real test output: `python tools/saturn/geo_walk_source_policy_test.py` → `AssertionError: production Saturn geo path still has 24 direct recursive dispatcher calls`.
- Boot crash is root-caused, not hypothetical: decoded master exception record shows illegal instruction with `sp=0x060026DC` — 0x19324 bytes below `___master_stack=0x06004000` — from geo recursion consuming ~452 B/frame (`docs/saturn/evidence/reports/current-memory-fixed-desktop-crash-2026-08-06.md`, "Ymir memory-dump decode" section).
- Budget: last real `.map` measurement was 3,128 B HWRAM short / 784 B LWRAM over; commits `465fb8b0` (−128), `fce3f4b5` (−448), `0ebd5b05` (−40) land HWRAM-only reductions since, so the reconstructed gap is **~2,512 B HWRAM / 784 B LWRAM — unconfirmed by any fresh link**. Root cause: `39008658` (+65,536 B `.lwram_actor_runtime`) and `6dbaea8d`/`a48e5aac` (+4,096 B `.lwram_geo_traversal`, capacity 256 × 16 B frames vs. proven max depth 172).
- Registry gap: `valid_observation()` (`src/port/saturn/gfx/saturn_actor_instance.c:54-87`) rejects `family_id==0 || actor_bank_id==0 || !nonzero_hash(...)` (:64-68) and `scene_package_generation==0` (:70). The seam `saturn_source_observe_object_begin()` (`src/game/rendering_graph_node.c:103-145`) memsets the observation and never sets those fields (its own comment at :139-143: "Scene/family/bank identity is deliberately unresolved until the generated actor registry is authoritative"). Every real object is currently discarded at capture.
- The four plan-doc acceptance checkboxes (plan:794-803) are all still literally `[ ]`.

---

### Task 1: Fresh RED link baseline + residual memory inventory

**Files:**
- Create: `docs/saturn/evidence/reports/task14-budget-baseline-2026-08-07.md`
- No source changes in this task.

- [ ] **Step 1: Attempt the real link at HEAD and capture the exact failure**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label task14-budget-baseline --animation 1 --actors 1 --audio 0 --pipeline 4 --diagnostic-mode none --jobs 1 --output build\saturn\variants\task14-budget-baseline\artifact.json
```

Expected: link FAILS on `sourceboot-cart.x` assertions. Capture the exact assert text and, from the partial link attempt, whether a `.map` was produced. If `build_sourceboot_variant.py`'s preflight refuses before linking, fall back to the serialized direct make invocation the ledger's prior baselines used (sourced `.yaul.env`, `SATURN_SOURCE_CART_STAGE_SECTORS=4`, `SATURN_DEMO_HOT_PROMOTION=0` — see `progress.md:249` for the exact recorded shape) and capture its stderr.

- [ ] **Step 2: Extract the real deltas**

From the failure output and/or the newest produced `.map`, record: exact HWRAM shortfall (`___end` vs. `0x06100000` minus the required `0x1B00` TLSF margin, per `sourceboot-cart.x:138-141`), exact LWRAM overage against the `0x4000` slave-stack margin (`:228-231`), and confirm/deny the reconstructed ~2,512/784 figures. If the numbers differ from the reconstruction, the measured numbers win everywhere downstream.

- [ ] **Step 3: Rank the residual HWRAM occupants**

From the `.map`, list the top 25 HWRAM `.bss`/`.data` symbols by size with their owning object files. Explicitly separate: (a) SCU/DMA-visibility-constrained (VDP1 command banks, Gouraud staging — NOT movable, per the Task 14 runtime-contract history), (b) CPU-only mutable state (LWRAM-move candidates, gated on LWRAM headroom), (c) anything write-once the earlier const sweeps missed (cart candidates), (d) libyaul-owned symbols (out of scope). Write the report file with the table and the four assert margins.

- [ ] **Step 4: Commit**

```bash
git add docs/saturn/evidence/reports/task14-budget-baseline-2026-08-07.md
git commit -m "docs(saturn): record task 14 fresh link baseline and HWRAM inventory"
```

---

### Task 2: Geo-walk production cutover (4 waves of ~6 handlers)

**Files:**
- Modify: `src/game/rendering_graph_node.c` (the 24 call sites between :405 and :1577)
- Reference (read, do not modify): `src/port/saturn/gfx/saturn_geo_walk_runtime.h/.c`, `docs/superpowers/plans/2026-08-06-saturn-iterative-geo-walk.md` (its cutover task is the authoritative per-handler conversion design — this plan sequences and gates it, it does not re-derive it)
- Test (existing, currently RED): `tools/saturn/geo_walk_source_policy_test.py`, `verify-saturn-geo-walk-runtime`, `verify-saturn-geo-depth-manifest`

Wave discipline — repeat this identical loop four times (sites 24→18→12→6→0):

**Real completion status — REAL FINAL CLOSURE (2026-08-09, this session,
verified against HEAD `24b156fe`):** All four waves plus a real final
closure sub-wave landed and are independently reviewed (`5ba8d85c`,
`958caf5f`, `e92122c1`, `169141f5`, `51c46bfa`, `ea31881c`, `3cc5e84a`,
`ac37a009`, `442597e7`, `b5d57990`, `24b156fe`, and this session's own
guard-removal commit). **This task is now fully closed — the original
"Mario holding something" master-stack-overrun crash is structurally
fixed, not merely bounded-with-a-fallback.**

Earlier same-day work (`24b156fe`) converted the last two node types that
could still route real content into unbounded recursion:
`GRAPH_NODE_TYPE_START` (the literal first command of every actor's geo
layout, including `mario_geo_render_body`) and `GRAPH_NODE_TYPE_CULLING_
RADIUS` (25 actor files' first command). That closed the "Mario holding
something" gap for real: `saturn_geo_walk_enter`'s switch now has a real
case for every node type that can legitimately appear as a walk token —
20 types — with only `GRAPH_NODE_TYPE_ROOT` left uncased, which by
construction never appears as one.

That, in turn, made the `sSaturnGeoWalkActive` reentrancy guard (kept as
of `442597e7`'s policy-test rewrite, and still kept as of `24b156fe`'s own
commit message out of caution) **provably unreachable**: its only trigger
was a nested call to `saturn_geo_walk_process_children` from inside an
already-active walk, reachable only via `saturn_geo_walk_enter`'s
now-unreachable `default:` case. This session traced that concretely (not
just re-trusting the prior session's caution) and removed the guard, its
fallback branch, and the two state toggles as dead code —
`tools/saturn/geo_walk_source_policy_test.py` now reports
`PASS (2 allowlisted permanent call sites, 0 unaccounted)`, its lowest
achievable count: `geo_try_process_children` (still needed for
`GRAPH_NODE_TYPE_ROOT` and `GRAPH_RENDER_CHILDREN_FIRST`-flagged nodes) and
`geo_process_root`'s top-level kickoff — both structurally necessary
bridges, not hazards.

This session also re-measured Mario's real body-chain depth with a real,
compiled, and run probe against the actual `saturn_geo_walk_runtime.c`
(not the earlier wave 4 report's hypothetical/pre-closure estimate):
fully-equipped Mario (moving, cap state present, holding an object via the
real `GEO_HELD_OBJECT` node) now walks entirely on the bounded array —
**19 frames real peak**, against the manifest's current 240-frame usable
budget (`capacity=256`, `safety_margin=16`) — **221 frames of real
margin**. See
`docs/saturn/evidence/reports/task14-closure-mario-body-chain-real-depth-2026-08-09.md`
for the full node-by-node transcription, the probe source, and a real
target-link attempt (compiled clean; link blocked by a pre-existing
linker-script `INCLUDE` ordering defect, diagnosed to its precise root
cause this session; a diagnostic-only workaround pushed past it to reveal
a real, separately-tracked 3,992-byte HWRAM budget deficit — Task 4-6
territory below, not this task's).

- [x] **Step 1 (per wave): Read before writing**

Read the next ~6 unconverted handlers in `rendering_graph_node.c` in full, plus `saturn_geo_walk_runtime.h`'s real enter/leave/dispatch API and the design-correction constraints recorded in the ledger (`progress.md:436-441`: the production continuation frame is 16-byte SH-2 state — node, sibling, phase/action, saved matrix/context tokens; handlers must bind their existing global save/restore to the runner's child/sibling/dispatch/leave phase ordering, and must NOT use a node-filtering shortcut).

- [x] **Step 2 (per wave): Convert the wave's handlers**

Each direct `geo_process_node_and_siblings(child)` call becomes the runtime's deferred child dispatch; each handler's post-child restoration (matrix stack pops, render-mode restores) moves to its leave action. The iterative-geo-walk plan doc's cutover task holds the worked pattern — follow it exactly; where a handler has save/restore state the pattern doesn't cover, stop and record a design note in the SDD ledger rather than improvising a shortcut.

- [x] **Step 3 (per wave): Verify**

```bash
python tools/saturn/geo_walk_source_policy_test.py
```
Expected: the remaining-call-count assertion drops by exactly the wave's site count (24→18, then 18→12, 12→6, 6→0; final wave: test PASSES).

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-saturn-geo-walk-runtime verify-saturn-geo-depth-manifest
```
Expected: PASS both, every wave.

- [x] **Step 4 (per wave): Commit + independent review**

```bash
git add src/game/rendering_graph_node.c CHANGELOG.md
git commit -m "feat(saturn): convert geo handler wave N to bounded walk runtime"
```
Dispatch a spec-compliance reviewer per wave (fresh subagent, diff-scoped) before starting the next wave — the ledger's history shows every geo/actor task caught real bugs only under independent review; do not batch all 24 into one review.

---

### Task 3: Actor identity registry generator + observer-seam wiring

**Files:**
- Create: `tools/saturn/gen_actor_identity_registry.py`
- Create: `tools/saturn/test_gen_actor_identity_registry.py`
- Modify: `src/game/rendering_graph_node.c` (`saturn_source_observe_object_begin`, :103-145)
- Modify: `src/port/saturn/sourceboot/Makefile` + `Makefile.saturn.mk` (generated-header wiring, mirroring the `SOURCEBOOT_HUD_GLYPH_HEADER` / order-only-prerequisite pattern at `sourceboot/Makefile:855-857`)
- Test: extend `tools/saturn/test_actor_snapshot_source.py` (this is plan-doc acceptance checkbox 1 — RED cases per typed field, including "generic `feature_state` bits never substitute for source-owned values")

- [ ] **Step 1: Read the real inputs first**

Read Task 11's generator and its outputs to learn the authoritative schema: the family bank build products (find via `Makefile.saturn.mk`'s `compile-actor-banks` target), the 47-family S64F bank (99,105 B, SHA `97dc231b…`, from the 86-record BOB closure), and the closure JSON (`build/saturn/packages/bob/1/closure.json`). The registry key is `(sharedChild geo layout, behavior script)` resolved the way plan:792 specifies (via `gLoadedGraphNodes[]`); the values are exactly the four fields `valid_observation()` gates on: `family_id`, `actor_bank_id`, `actor_bank_hash_words[8]`, `scene_package_generation`.

- [ ] **Step 2: RED tests for the generator**

`test_gen_actor_identity_registry.py` (unittest, house style of `test_gen_sourceboot_sky_gradient.py`): deterministic output byte-stability across two runs; every closure record with a supported family resolves to a nonzero (family_id, bank_id) pair; unsupported/unknown behaviors are explicitly ABSENT from the table (so the seam's lookup miss keeps them fail-closed — never emit a zero-identity row); bank hash words match the real bank manifest's SHA-256 split big-endian into 8×u32.

- [ ] **Step 3: Implement the generator**

Emit a generated header (`build/saturn/sourceboot/generated/actor_identity_registry.h`) containing a sorted-by-key `static const` table plus a binary-search lookup function declaration, following the established generated-header conventions (banner comment, `#pragma once`, gitignored build output). The table is `const`, so the linker's existing cart rule places it on cart — zero HWRAM cost.

- [ ] **Step 4: RED tests at the seam, then wire it**

First extend `test_actor_snapshot_source.py` with the plan-doc checkbox-1 cases (each typed field sourced from the authoritative game state; `feature_state` substitution attempt must fail). Run; confirm RED. Then modify `saturn_source_observe_object_begin()`: replace the identity-memset block (:111-143) with a registry lookup on `(node->sharedChild, node->behavior-equivalent)` — populating the four identity fields on hit, leaving them zero on miss (preserving fail-closed for unsupported families, which stays correct per Task 16's research) — and capture the typed source fields the plan's checkbox names (visibility/render-range/switch/opacity; `held/parent` stays `NO_PARENT` per the Task 19 design correction, cite it in a comment).

- [ ] **Step 5: Verify**

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-instance-snapshot verify-render-snapshot-bank
```
Plus `python -m unittest test_gen_actor_identity_registry -v` and the extended `test_actor_snapshot_source.py`. Expected: all PASS; the snapshot tests must now show nonzero admission for a fixture object with a registered family.

- [ ] **Step 6: Commit + review**

```bash
git add tools/saturn/gen_actor_identity_registry.py tools/saturn/test_gen_actor_identity_registry.py src/game/rendering_graph_node.c tools/saturn/test_actor_snapshot_source.py src/port/saturn/sourceboot/Makefile Makefile.saturn.mk CHANGELOG.md
git commit -m "feat(saturn): wire generated actor identity registry into observer seam"
```
Independent two-stage review (spec, then quality) — this seam is the single gate every downstream actor feature admits through.

---

### Task 4: LWRAM closure (owner decision + implementation)

**Files:**
- Modify (leading option): the geo-depth manifest generator (`tools/saturn/geo_depth_manifest.py`) capacity policy + its test `tools/saturn/test_geo_depth_manifest.py`
- Modify: `CHANGELOG.md`

- [ ] **Step 1: Present the decision to the owner (AskUserQuestion)**

Leading option: change the generator's capacity policy from 256 to measured-max-plus-headroom (proven depth 172 → capacity 192), saving 1,024 B of `.lwram_geo_traversal` — clears the 784 B overage with 240 B to spare. This is right-sizing above proven worst case with the runtime's existing fail-closed overflow latch (`937f0043`) still armed — the same pattern as the accepted HUD 40→24 shrink — but the standing constraint quoted in this plan's header forbids "capacity shrink" without authorization, so it is the owner's call, not the implementer's. Alternative if declined: find 784 B among other LWRAM occupants from Task 1's map (present the real candidates from the inventory).

- [ ] **Step 2: RED then implement whichever option is chosen**

For the leading option: extend `test_geo_depth_manifest.py` with a capacity-policy case (generated capacity ≥ proven max + fixed headroom; a mutation forcing capacity < proven depth must fail closed), run RED, change the policy, run GREEN, regenerate, and confirm `sourceboot-cart.x:190`'s size-match assert tracks the generated value automatically.

- [ ] **Step 3: Commit**

```bash
git add tools/saturn/geo_depth_manifest.py tools/saturn/test_geo_depth_manifest.py CHANGELOG.md
git commit -m "fix(saturn): right-size geo traversal arena to measured depth (owner-approved)"
```

---

### Task 5: HWRAM closure (measure-ranked, owner-gated)

**Files:** determined by Task 1's inventory — this task is deliberately option-shaped, not pre-decided, because fabricating relocation targets without the fresh map would repeat the exact class of error this project's review discipline exists to catch.

- [ ] **Step 1: Propose from evidence**

From Task 1's ranked table, assemble a proposal covering ≥ the measured HWRAM shortfall using only levers consistent with the standing constraints, in preference order: (a) write-once data the source-level sweeps missed (const → cart, zero risk — the four commits `91f02ffd`/`465fb8b0`/`fce3f4b5`/`0ebd5b05` establish the pattern and the review bar), (b) CPU-only mutable state → `.lwram_bss` via the existing `SOURCEBOOT_LWRAM_STATE` attribute + `sourceboot_reset_lwram_state()` zeroing contract — gated on LWRAM headroom remaining after Task 4, (c) deduplication of any map-revealed redundancy. If (a)-(c) cannot cover the gap, present the shortfall and remaining structural options to the owner via AskUserQuestion rather than force a constrained lever.

- [ ] **Step 2: Implement approved items one commit each**

Each relocation follows the established per-item discipline: trace every write site before moving (the `sSkyboxTextures` commit `0ebd5b05` is the worked example), update `sourceboot_reset_lwram_state()` for any LWRAM move, CHANGELOG entry in the same commit, independent review per commit.

---

### Task 6: Green link, boot evidence, and reconciliation

**Files:**
- Create: `docs/saturn/evidence/reports/task14-green-link-2026-08-07.md`
- Modify: `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md` (check the four Task 14 boxes at :794-803), `.superpowers/sdd/.../progress.md` (ledger entry), `CHANGELOG.md`

- [ ] **Step 1: Link green**

Re-run Task 1 Step 1's exact command. Expected: all four `sourceboot-cart.x` asserts pass; artifact.json produced with ELF/CUE/ISO hashes. Run the existing map verifier (`tools/saturn/verify_sourceboot_memory_map.py` path per `progress.md:245-246`) against the fresh ELF and record margins.

- [ ] **Step 2: Headless boot evidence**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_throughput.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --game <fresh CUE> --elf <fresh ELF> --startup-vblanks 600 --max-vblanks 3600 --output docs\saturn\evidence\reports\task14-postgeo-boot-2026-08-07.json
```
Expected: post-BIOS frames advancing, both SH-2s live, and — the point of the geo cutover — no exception record, sustained past the prior ~228-live-frame recursion-crash window. Chunk any longer soak at ≤3600 frames per request (Ymir hard cap).

- [ ] **Step 3: Reconcile and close**

Check the four plan-doc boxes only for what the evidence actually proves (target FPS/manual gates stay open — they belong to later tasks). Append the ledger entry in the established voice (bounded claims, explicit open gates). CHANGELOG entry. Final commit + independent final review of the whole task-14 diff series.

```bash
git add docs/ .superpowers/ CHANGELOG.md
git commit -m "docs(saturn): reconcile task 14 completion evidence and ledger"
```
