# Saturn Overlapped Render Pipeline Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the low-single-digit same-frame serial BOB render path with a full-game, snapshot-driven Saturn pipeline that removes duplicate SM64 scene construction, rejects work before transformation, schedules both SH-2s opportunistically, and transfers complete VDP1 banks only at safe lifetime boundaries.

**Architecture:** The master SH-2 retains authoritative simulation, input, final ordering, VRAM ownership, and presentation. Immutable render snapshots feed one shared bounded job queue; the slave and master claim disjoint work, while VDP1/VDP2 operate on the last complete frame. Two LWRAM command banks use CPU-DMAC, two HWRAM Gouraud banks use SCU DMA, and a single terminal boundary publishes only positively retired generations.

**Tech Stack:** C11, SH-2 Q16.16, Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, VDP1, VDP2, SH-2 CPU-DMAC, SCU DMA, Python asset generators/tests, Ymir with the project `.ymir-profile` and 32-Mbit DRAM cart.

## Governing documents

- Architecture and live decision ledger:
  `docs/superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md`
- Prior component sprint and still-open publication gates:
  `docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md`
- Execution evidence ledger:
  `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Engine ownership contract: `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`
- Provenance boundaries: `docs/saturn/UPSTREAM_CODE_LEDGER.md`,
  `docs/saturn/SLAVEDRIVER_ADAPTATION.md`, and
  `docs/saturn/SGL_REFERENCE_NOTES.md`

## Live execution status — update on every task transition

`source-complete` means focused host/static tests and two-stage independent
review are green. It does not mean a target/Ymir gate passed. Update the
individual task steps below, this summary, the architecture decision ledger,
and the evidence report before starting another task.

- [ ] **Task 1 / A1 — duplicate source-render removal:** blocked in quality-fix
  round 1/5. Full-range review of `4a8fe1ce^..70cb3fe1` found that suppressing
  `geo_process_root()` also suppresses authoritative animation, painting/warp,
  environment-water, moving-texture, flying-carpet, camera, and matrix-derived
  object-state mutations. Safety commit `77ee306c` removes the sourceboot
  enable/disable calls, so normal/promotable Saturn builds fail closed with a
  full geo walk. No bounded state-only seam has been demonstrated; the
  controller-owned serial CUE/Ymir gate must not start.
- [x] **Task 1D / D1 — diagnostic-only geo-walk upper-bound CUE:** complete.
  The serial target CUE built successfully and the owner tested it in Ymir with
  the project DRAM profile. It remained approximately 2 FPS with no obvious
  improvement, so duplicate source geo walking is not the dominant bottleneck.
  A1 remains blocked for full-game correctness but is deferred behind the
  presentation-boundary correction; D1 remains deliberately broken,
  compile-time-only, and never a default, replay baseline, or full-game evidence.
- [x] **Emergency A9.0 — one VBlank, one presentation generation:**
  source-complete and desktop-Ymir launch-stable, but it did not produce the
  expected performance improvement. The earlier headless trace harness is
  invalid for target-health conclusions because it did not load paired ELF
  main bytes at their linked address. The owner's valid desktop-Ymir
  observation is VDP1 ≈2 FPS and VDP2 ≈60 FPS, reversing the prior counter
  reading: VDP2 is not the current bottleneck. A9.0 remains retained as a
  bounded-cadence correctness repair, not evidence of an FPS gain.
- [ ] **Task 2 / A2 — immutable snapshot banks:** source-complete; two
  independent reviews and target coherency evidence remain open. This is the full-game-safe
  prerequisite for A3's pre-transform VDP1 workload reduction; it is not
  itself claimed as an FPS improvement.
- [ ] **Task 3 / A3 — pre-transform cluster/LOD admission:** active. The
  scene-neutral host contract and deterministic BOB, active fragment-bank,
  and Mario compact tier streams are in place. The accepted terrain path now
  marks only the selected pre-transform span (with mandatory far-route work
  retained), while Mario's transform dispatch consumes its selected tier
  reference span with explicit original-vertex ownership. Generated BOB and
  fragment cluster metadata is host-checked for tight bounds, one material,
  mandatory retention, and FAR reduction; post-transform projected rejection
  remains. The generic runtime now admits those cluster records from the
  frame camera and builds the exact admitted position union before workers;
  tier state resets at scene transitions. Cluster depth is the conservative
  projection of the Q16 AABB onto the immutable Q16 camera-forward vector,
  rather than world Z; yaw/pitch admission coverage is host-tested. A3 also
  derives one nonzero generation before admission and transform publication,
  including `UINT32_MAX -> 1`, and rejects a mismatched result before its
  compact references are marked. It remains active for independent rereview
  and target visual/counter evidence; no performance claim is made. The A3
  render-cluster header also owns its GPL promotion dependency through a
  relative path, matching sourceboot's declared `gfx`-only Saturn include
  path; `verify-render-clusters` intentionally omits `gpl` so this boundary
  stays host-checked. A later target serial build exposed that the two bulk A3
  arrays consumed 38,148 bytes of HWRAM and overflowed the target by 29,680
  bytes. They now use the linker-owned CPU-only LWRAM section; the same route-0
  sourceboot configuration links and emits a fresh CUE with HWRAM ending at
  `0x060FDEF0` (0x2110 free, above the 4 KiB libyaul floor) and LWRAM ending at
  `0x002E33A0`. This proves only the target memory budget for that candidate;
  independent rereview and target visual/counter/FPS evidence remain open.
- [ ] **Task 4 / A4 — Mario meshlets and bounded ordering:** pending.
- [ ] **Task 5 / A5 — shared opportunistic SH-2 queue:** pending.
- [ ] **Task 6 / A6 — localized recovery and quarantine:** pending.
- [ ] **Task 7 / A7 — alternating source-bank ownership:** pending.
- [ ] **Task 8 / A8 — deferred transfers and true wait telemetry:** pending.
- [ ] **Task 9 / A9 — frame overlap and bounded cadence:** pending after the
  scoped Emergency A9.0 presentation-boundary correction.
- [ ] **Task 10 / A10 — full-game hardening and publication:** pending.

## Global Constraints

- Work only in
  `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`.
- Preserve unrelated dirty verifier, audit, route, and evidence files. Stage
  only files named by the active task.
- The supported target is Saturn only. PC/N64 build compatibility is not a
  task gate; reviewers consider non-Saturn behavior only when it affects
  Saturn code or host test tooling.
- Before each task, reconcile Git HEAD, this status summary, every checkbox in
  the task, the architecture decision ledger, and the evidence report.
- During the same task transition, record implementation commit, spec-review
  verdict, quality-review verdict, tests actually run, target evidence, and
  every remaining gate. Never batch documentation updates at the end.
- Use a fresh implementation subagent for each task, followed by a fresh
  specification reviewer and then a fresh code-quality reviewer. Do not begin
  the next task until both reviews are resolved and the agents' final reports
  have been received.
- Do not run target builds, Ymir, or other CPU-heavy commands concurrently.
- Run every MSYS/SH tool through `tools/saturn/with-msys-toolchain.ps1`; never
  launch `sh-elf-*` directly from an inherited Windows environment.
- Target builds use `make -B -j1` and a worktree-local writable
  `HOME`/`TMPDIR`/`TMP` inside `.msys-home`.
- Every Ymir run uses
  `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile` and the 32-Mbit
  DRAM cart. Do not launch Kronos.
- Experimental CUEs do not require the strict static native-math census.
  Strict native-math closure remains a final publication gate.
- BOB is the deterministic proving ground, not a scene-specific runtime
  boundary. New types and generators must accept arbitrary scene/actor banks.
- Master SH-2 alone owns live SM64 state, final order, VDP1/VDP2 registers,
  VRAM allocation, transfer submission, and presentation.
- Slave SH-2 sees immutable integer snapshots, generated read-only banks, and
  disjoint result spans only.
- A timed-out claimed span is never reclaimed while its worker may write.
  Retry requires acknowledged cancel/reset and positive retirement into a
  fresh generation; otherwise quarantine its bank and present the previous
  complete frame.
- Two 2,048-entry command banks remain in `.lwram_cmdts` (131,072 bytes total)
  and use CPU-DMAC or measured CPU-copy fallback. HWRAM Gouraud staging may use
  SCU DMA. SCU DMA must reject LWRAM sources.
- Preserve gameplay state, camera behavior, near clipping, material/texture
  identity, deterministic source order, command capacity, and zero unreported
  overflow/fault behavior.
- Counters diagnose work and stalls; no invented percentage threshold decides
  promotion. The manual goal is an obvious improvement from the current
  low-single-digit BOB build.
- Reference code: SlaveDriver
  `a8986591557b6e680550d3c23970284d3b38ff8f` is GPL-3.0-or-later;
  Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b` is GPLv3 for
  XL2-authored code; bundled SGL documentation/disassembly is pattern-only;
  Yaul `6012f79f237773378c8014e70d8998ad95a38d98` is the pinned MIT dependency.

---

### Task 1: Remove duplicate source scene construction and publish the earliest manual CUE

**Files:**
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.c`
- Modify: `src/game/area.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Create: `tools/saturn/test_source_render_suppression.py`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `Makefile.saturn.mk`
- Create: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Modify: architecture spec and this plan

**Interfaces:**
- Produces `sm64_saturn_source_runtime_set_scene_graph_suppressed(bool)` and
  `sm64_saturn_source_runtime_scene_graph_suppressed(void)`.
- Produces counters `scene_graph_walks` and `scene_graph_walks_suppressed`,
  appended to `sm64_saturn_source_runtime_state_t` and the Fast3D profile. The
  policy is dormant in accepted sourceboot code: normal walks increment and
  suppressed walks remain zero.
- Intended to retain stateful `do_cutscene_handler()`,
  `print_displaying_credits_entry()`, `render_menus_and_dialogs()`, and warp
  transition state updates. The current implementation also suppresses
  stateful work inside `geo_process_root()` and is therefore not accepted.

- [x] **Step 1: Write the red source-policy test**

  Create a Python test that extracts `render_game()` from `src/game/area.c`
  and requires the scene-graph call to be guarded while stateful calls remain
  outside that guard:

  ```python
  def test_saturn_ir_path_skips_geo_but_keeps_state_updates():
      body = extract_c_function(AREA_C, "render_game")
      assert "sm64_saturn_source_runtime_scene_graph_suppressed" in body
      guarded = extract_if_block(body, "!scene_graph_suppressed")
      assert "geo_process_root(" in guarded
      for call in ("do_cutscene_handler(",
                   "print_displaying_credits_entry(",
                   "render_menus_and_dialogs(",
                   "render_screen_transition("):
          assert call in body
          assert call not in guarded
  ```

- [x] **Step 2: Add the red runtime contract**

  In `runtime_contract_test.c`, set suppression true/false and assert the
  getter and appended counters do not alias existing state fields. Add
  `verify-source-render-policy` to `Makefile.saturn.mk` to run both tests.

- [x] **Step 3: Run the focused gate and record the expected failure**

  Run:

  ```powershell
  & tools/saturn/with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-source-render-policy
  ```

  Expected: FAIL because the scene-graph suppression API and guarded source
  path do not exist. Record the command/output in the evidence report.

- [x] **Step 4: Implement the runtime policy and counters**

  Add this API shape without moving existing state fields:

  ```c
  void sm64_saturn_source_runtime_set_scene_graph_suppressed(bool suppressed);
  bool sm64_saturn_source_runtime_scene_graph_suppressed(void);
  void sm64_saturn_source_runtime_note_scene_graph_walk(bool suppressed);
  ```

  Append both counters at the end of runtime/profile structs and extend the
  existing profile decoder fixture for their exact offsets.

- [ ] **Step 5: Split render construction from render-time state updates — BLOCKED**

  In `render_game()`, evaluate `scene_graph_suppressed` once. Put
  `geo_process_root()`, viewport setup, HUD/text emission, and source-only
  scissor emission under `if (!scene_graph_suppressed)`. Keep the four named
  stateful calls and warp-transition completion/decrement logic active in both
  paths. Always clear `D_8032CE74` and `D_8032CE78` at function exit.

  The implementation in `4a8fe1ce` is not accepted: `geo_process_root()` also
  advances authoritative animation and stateful geo callbacks. Step 5 remains
  unchecked until an audited state-only seam and the required behavioral
  differential tests exist. Safety commit `77ee306c` prevents sourceboot from
  activating this dormant guard.

- [x] **Step 6: Keep scene-graph suppression fail closed in sourceboot**

  Safety correction `77ee306c` removes both scene-graph setter calls from
  `sourceboot_run_source_tick()`. Only final display submission remains paired
  around the source tick. A focused source test rejects any sourceboot call to
  the reserved scene-graph setter, so every normal/promotable Saturn build
  retains `geo_process_root()` until a behavior-tested state-only seam exists.
  The sole sealed Task 1D diagnostic exception is specified separately below.

- [ ] **Step 7: Run focused and aggregate host gates**

  Run `verify-source-render-policy`, `verify-runtime-contracts`, and
  `python tools/saturn/test_tools.py`. Expected: all PASS; the source-policy
  test proves the stateful whitelist and the runtime counter layout. The
  focused constituent tests pass, but the aggregate run also sees unrelated
  route-schema failures from preserved dirty state; see the evidence report.

  Safety-closure focused results: source-policy PASS (2 tests), compiled host
  runtime contract PASS when invoked directly, and profile decoder PASS
  (13 tests, 1 expected skip). Per task scope, the broad aggregate and all
  target/Ymir gates were not run. The wrapper still exits at the known
  MSYS/Windows executable handoff after successful host compilation.

- [x] **Step 8: Update live documentation before review**

  Mark each completed step here, set Task 1 to `source-complete` only after
  review, and add the exact source audit, commit candidate, tests, and the
  experimental target gate to the evidence report. Add any changed whitelist
  decision to the architecture ledger in the same commit.

- [ ] **Step 9: Commit and run two-stage review**

  Stage only Task 1 files and commit with
  `perf(saturn): bypass duplicate source scene construction`. Obtain spec
  review first, quality review second, resolve findings in follow-up commits,
  and record both final verdicts.

  Full-range quality review of `4a8fe1ce^..70cb3fe1`: **BLOCKING**. The source
  test inspects only the first suppression block and is not behavioral. The
  production guard skips graph-owned state mutations described in the evidence
  report. A replacement must first add red differential tests for animation
  progression, warp progression, unconditional cleanup, and paired sourceboot
  policy restoration, then implement an audited state-only seam without
  constructing source display lists.

  Safety closure `77ee306c`: **implemented, review pending**. It does not claim
  A1 completion; it removes the unsafe production activation while retaining
  the dormant policy/counter ABI for the eventual tested seam.

- [ ] **Step 10: Build and manually test the early CUE — NOT AUTHORIZED**

  Build serially with the established `poly2/pipe3`, live-input,
  bootstrap-600, Q16-camera-3 role through the audited wrapper. Launch the
  resulting CUE in Ymir with `.ymir-profile`. Record CUE/ELF SHA-256, source
  commit, whether controls work, whether BOB/Mario remain visible, and the
  owner's qualitative speed result. Do not run the strict native-math census
  for this experimental checkpoint.

  This gate is prohibited while Step 5 and the Critical quality finding remain
  blocked. The fail-closed safety change is a runtime-safety GO only; it is not
  authorization to build or launch an A1 optimization CUE.

### Task 1D: Produce one non-promotable geo-walk upper-bound diagnostic CUE

**Purpose:** Distinguish a dominant duplicate-geo bottleneck from a marginal
one without re-enabling unsafe suppression in any normal/promotable Saturn
build. D1 is the sole sealed diagnostic exception. BOB is only the
deterministic demonstrator. The required destination remains a
scene-neutral full game, and this task must not introduce BOB-specific runtime
types, ownership rules, or production fallbacks.

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/test_source_render_suppression.py`
- Modify: `Makefile.saturn.mk` only if a focused host gate needs a named target
- Modify: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Modify: this plan and the architecture decision ledger
- Modify/Create: `CHANGELOG.md`, `STATE.md`, `ARCHITECTURE.md`, `HOWTO.md` as
  required by the portfolio documentation policy

**Diagnostic contract:**
- Default value is `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0`; normal/promotable
  sourceboot builds cannot skip the geo walk.
- Value `1` is legal only when all of `SATURN_DEMO_PATH=1`,
  `SATURN_SOURCEBOOT_ROUTE_REPLAY=1`, and an explicit diagnostic output tag are
  present. Reject every other configuration at Make parse time.
- The sourceboot tick enables scene-graph suppression only inside the explicit
  diagnostic build and restores false immediately after the one source tick;
  no state can leak to a following tick or a normal build.
- The diagnostic image name must state `diag-skip-geo`; no normal pipeline
  role/output directory may be overwritten.
- Host tests must mutation-check default-off, forbidden combinations, paired
  restoration, and absence of any BOB-only semantic branch in generic runtime
  code.
- The test report must say that animation, warp, camera, and graph-owned state
  are knowingly invalid; speed is an upper-bound observation only, never a
  promotion gate.

- [x] **Step 1: Write failing diagnostic-containment tests**

  Extend the focused source-policy test so the unimplemented diagnostic fails
  for a missing default-off macro/configuration guard, missing paired setter
  restoration, and missing diagnostic-only output tag.

- [x] **Step 2: Record the red host result**

  Run the focused source-policy target through
  `tools/saturn/with-msys-toolchain.ps1`; record the expected missing-contract
  failure. Do not build or launch a target image in this step.

- [x] **Step 3: Implement only the sealed diagnostic seam**

  Add the compile-time constraint, paired sourceboot setter, and output tag.
  Do not split or emulate geo callbacks; that is A1's future
  full-game state/render separation work.

- [x] **Step 4: Run focused containment and runtime-contract gates**

  Record exact commands/results. The normal default configuration must prove
  zero suppressed walks; the diagnostic configuration must prove the paired
  source-level policy only. No aggregate green claim substitutes for these
  focused gates.

  Focused containment is green after quality-fix round 2/5: 4 tests exercise
  real Make parsing, malformed/padded activation spellings, flag-keyed output
  identity, and the complete normal preprocessor path. The runtime-contract
  wrapper remains an open infrastructure gate because MSYS translates the worktree path to
  `\\d\\Code...`; Windows Python fails to create that path with WinError 5
  before the host contract executable runs. The already compiled
  `build/saturn/host-tests/runtime-contract-test.exe` was freshly run through
  `with-msys-toolchain.ps1` and exited 0.

- [x] **Step 5: Update records, commit, and complete two-stage review**

  Record that D1 is an upper-bound diagnostic, not an A1 resolution; append all
  tests, review verdicts, commits, and unpassed target gate to the evidence and
  SDD ledger. Commit the behavior and its same-commit changelog entry before
  the manual build.

  Initial spec review: **NO-GO**, two Important findings and one Minor audit
  note. Review-fix round 1 `fe1074b8` replaces inert Makefile string checks
  with real parse-time acceptance/rejection checks, inspects the matching
  normal `#else`, and adds complete Task 1D execution evidence. The scoped
  spec rereview marks both findings **ADDRESSED** with no new Critical or
  Important findings. Independent quality review is **NO-GO**; quality-fix
  round 2/5 `98670c48` addresses its three Important findings. Scoped quality
  rereview is **GO**: every prior Important/Minor finding is addressed with no
  new Critical/Important breakage. The live CUE/Ymir step remains unchecked.

- [x] **Step 6: Build and manually run exactly one serial Ymir CUE**

  Use the project profile and 32-Mbit DRAM cart, with the established BOB/live
  input/Q16 camera route. Record CUE/ELF hashes, controls, visibility, and the
  owner’s qualitative speed result. The 2026-08-03 serial build succeeded and
  produced the tagged CUE recorded in the evidence report. The owner ran it in
  Ymir with the project 32-Mbit DRAM profile and observed approximately 2 FPS,
  with no obvious speed increase. Because the diagnostic invalidates graph
  state, visual/control failure was expected and did not authorize repair
  churn. This negative upper-bound result defers A1 and prioritizes Emergency
  A9.0; do not repeat this CUE.

### Task 2: Add immutable snapshot banks with explicit generations

**Files:**
- Create: `src/port/saturn/gfx/saturn_render_snapshot.h`
- Create: `src/port/saturn/gfx/saturn_render_snapshot.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bridge.h`
- Modify: `src/port/saturn/sourceboot/main.c`
- Create: `tools/saturn/render_snapshot_bank_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces `sm64_saturn_render_snapshot_t` containing generation, scene/area
  identity, integer camera state, copied Mario snapshot, pose selector, and
  immutable generated-bank identifiers; no live game pointer is permitted.
- Produces two-slot `sm64_saturn_render_snapshot_bank_t` with states `FREE`,
  `WRITING`, `READY`, `RENDERING`, `COMPLETE`, and `QUARANTINED`.
- Produces `begin_write`, `publish`, `acquire_ready`, `complete`, `retire`,
  `quarantine`, and generation-validation functions.

  ```c
  typedef struct sm64_saturn_render_view {
      int32_t view_projection_q16[4][4];
      int32_t camera_position_q16[3];
      int32_t camera_focus_q16[3];
      uint32_t generation;
  } sm64_saturn_render_view_t;

  bool sm64_saturn_render_snapshot_begin_write(
      sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation,
      sm64_saturn_render_snapshot_t **out);
  bool sm64_saturn_render_snapshot_publish(
      sm64_saturn_render_snapshot_bank_t *bank,
      sm64_saturn_render_snapshot_t *snapshot);
  const sm64_saturn_render_snapshot_t *
  sm64_saturn_render_snapshot_acquire_ready(
      sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation);
  bool sm64_saturn_render_snapshot_complete(
      sm64_saturn_render_snapshot_bank_t *bank,
      const sm64_saturn_render_snapshot_t *snapshot);
  bool sm64_saturn_render_snapshot_retire(
      sm64_saturn_render_snapshot_bank_t *bank,
      const sm64_saturn_render_snapshot_t *snapshot);
  bool sm64_saturn_render_snapshot_quarantine(
      sm64_saturn_render_snapshot_bank_t *bank, uint32_t generation);
  ```

- [x] **Step 1: Write failing bank-lifecycle tests**

  Cover generation zero rejection, write/publish/acquire/complete/retire,
  stale acquire rejection, mixed camera/actor generation rejection, double
  acquire rejection, and quarantined-bank non-reuse:

  ```c
  sm64_saturn_render_snapshot_t *slot = NULL;
  assert(sm64_saturn_render_snapshot_begin_write(&bank, 7U, &slot));
  slot->camera.generation = 7U;
  slot->actor_generation = 7U;
  assert(sm64_saturn_render_snapshot_publish(&bank, slot));
  assert(sm64_saturn_render_snapshot_acquire_ready(&bank, 7U) == slot);
  assert(!sm64_saturn_render_snapshot_retire(&bank, slot));
  assert(sm64_saturn_render_snapshot_complete(&bank, slot));
  assert(sm64_saturn_render_snapshot_retire(&bank, slot));
  ```

- [x] **Step 2: Add `verify-render-snapshot-bank` and record red evidence**

  Compile the fixture with `-std=c11 -Wall -Wextra -Werror`; expected failure
  is the missing header/API.

- [x] **Step 3: Implement the minimal state machine**

  Use fixed-width fields and compiler fences. Keep bulk snapshot data separate
  from the uncached release record. Reject every illegal state transition
  without mutating the bank.

- [x] **Step 4: Capture authoritative state once per simulation generation**

  In `sourceboot/main.c`, fill a `WRITING` slot only after the source tick.
  Copy Q16 camera state and scalar actor/pose selectors; reference only
  generated immutable vertex/material banks. Publish generation last.

- [x] **Step 5: Prove the slave-facing snapshot has no live pointers**

  Add `_Static_assert`/source checks forbidding `MarioState *`, graph-node
  pointers, VDP1 backend pointers, and VRAM pointers in snapshot types.

- [x] **Step 6: Run `verify-render-snapshot-bank`, runtime contracts, and the dual-frame-bank fixture**

  The snapshot-bank lifecycle/source fixture and dual-frame-bank fixture are
  green at the final terminal-state repair. The fresh runtime-contract run is
  green at `533471e5`: its former line-4018 assertion expected a complete
  VDP1 command from the worker even though `e50fc478` intentionally changed
  the worker to publish private dynamic payloads and leaves template copying
  to the master. The contract covers the zero-shade and post-light-shade
  variants: bytes 0..7 are zero or four worker-owned RGB1555 shades, bytes
  8..11 and 28..31 remain zero, and bytes 12..27 preserve coordinates. The
  resolved template still owns immutable material. `90fc3c76` gates the writer
  shade copy on that flag, so ignored shade inputs cannot populate a clear-flag
  payload. No snapshot or master VDP1 behavior changed, and no target build or
  Ymir gate was run.

- [ ] **Step 7: Update documents, commit, and complete two-stage review — ACTIVE**

  Record every transition/test and commit with
  `feat(saturn): add immutable render snapshot banks`.

  A2 implementation has a watched missing-header/API red compile, then green
  lifecycle/source-boundary test, green dual-frame-bank fixture, and green
  runtime contract. The line-4018 terrain mismatch was a stale host assertion,
  diagnosed in `runtime-contract-4018-report.md`, not a snapshot defect. Its
  first independent runtime-contract review was NO-GO because the initial
  repair incorrectly required bytes 0..11 to be zero. The live shades-path
  regression now addresses that finding, but a fresh independent rereview is
  still required. Target build/Ymir and the remaining independent A2 review
  are unexecuted; A2 is source-complete only, never a performance claim.

  Fix round 1 corrects the specification review’s two Important findings and
  one Moderate finding: reset is free-slot-only (quarantine and all in-flight
  states remain owned), release words and peer payload use target P2
  cache-through aliases, and the public generation validator is exercised
  directly. The focused fixture has new red evidence for reset reuse, missing
  cache-through accessors, and missing validation API, then passes green.
  Runtime-contract closure and a fresh independent review remain open.

  Fix round 2 makes the slave-facing `READY → RENDERING` claim single-winner.
  The uncached release record now includes a fixed-width claim byte acquired
  by SH-2 `tas.b`; a deterministic held-first-claim test proves a second public
  acquire returns `NULL`. The cache-through source test requires both TAS and
  claim/release use in the implementation. Runtime-contract closure and fresh
  independent reviews remain open.

  Fix round 3 resolves the quality review’s Critical P1/P2 publication gap.
  The producer payload accessor now uses the same P2 cache-through address as
  the peer, and `begin_write`, reset, and retire never bulk-write through P1.
  A structural producer-visibility gate rejects a P1-only write path before
  `READY`; the host fixture remains lifecycle evidence, not target cache proof.
  Runtime-contract closure, target cache evidence, and fresh reviews remain
  open.

  Fix round 4 serializes quarantine, completion, and positive retirement with
  the same release claim used by acquisition. Each transition locks then
  revalidates its legal predecessor before writing a terminal state, so a
  quarantined generation cannot be overwritten by an already-validating
  claimant. The new deterministic race fixture is red against the prior
  lock-bypassing quarantine path and green after the repair; focused host
  evidence only, with target coherency evidence and fresh reviews still open.

### Task 3: Generate tight clusters and choose compact LODs before transformation

**Files:**
- Create: `src/port/saturn/gfx/saturn_render_cluster.h`
- Modify: `tools/saturn/emit_bob_scene.py`
- Modify: `tools/saturn/bake_bob_bsp_fragments.py`
- Modify: `tools/saturn/extract_mario_actor.py`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `tools/saturn/test_tools.py`
- Create: `tools/saturn/render_cluster_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces scene-neutral `sm64_saturn_render_cluster_t` records with tight Q16
  bounds, primitive span, per-tier compact position-ref span, mandatory flag,
  material partition, and stable source ordinal.
- Produces
  `sm64_saturn_render_cluster_admit(cluster, view, lod_state, result)`; the
  result names the chosen tier and exact compact position/primitive spans.
  It conservatively projects the Q16 AABB onto the immutable Q16
  `view_forward_q16` direction; it must never infer depth from world Z.
- Per-primitive projected area/window rejection remains after transform.

  ```c
  typedef struct sm64_saturn_render_cluster_result {
      uint32_t generation;
      uint16_t primitive_first;
      uint16_t primitive_count;
      uint16_t position_ref_first;
      uint16_t position_ref_count;
      uint8_t lod_tier;
      uint8_t admitted;
  } sm64_saturn_render_cluster_result_t;

  typedef struct sm64_saturn_render_lod_state {
      saturn_lod_thresholds_t thresholds;
      saturn_lod_tier_t previous;
  } sm64_saturn_render_lod_state_t;

  bool sm64_saturn_render_cluster_admit(
      const sm64_saturn_render_cluster_t *cluster,
      const sm64_saturn_render_view_t *view,
      sm64_saturn_render_lod_state_t *lod_state,
      sm64_saturn_render_cluster_result_t *result);
  ```

- [ ] **Step 1: Add deterministic generator failures**

  Require byte-identical output, in-range spans, each emitted primitive in one
  material partition, tight bounds enclosing every referenced position, and
  each LOD position list equal to the unique positions used by that LOD.

- [x] **Step 2: Add the red runtime fixture**

  Test outside/inside/intersecting bounds, mandatory far clusters, hysteresis,
  empty spans, bad indices, and that FAR admission exposes fewer position refs
  than NEAR for a synthetic cluster. The focused fixture also covers
  non-axis-aligned yaw and pitch: front clusters with negative world Z,
  behind clusters with positive world Z, mandatory behind work, exact returned
  compact spans, a yawed MID hysteresis boundary, and the exact
  `UINT32_MAX -> 1` admission-generation transition after resetting a prior
  MID tier to the scene-default NEAR state.

- [ ] **Step 3: Add `verify-render-clusters` and record both red commands**

  Run the focused Python generator selection plus the C fixture. Expected:
  missing generated fields and admission API.

- [ ] **Step 4: Emit compact per-tier position/remap streams**

  Extend generated BOB and actor banks without BOB-specific runtime symbols.
  Stamp format version, content SHA-256 identity, counts, and offsets. Reject
  stale same-size banks at load.

- [ ] **Step 5: Implement coarse admission before transforms**

  Perform BSP/frustum and metadata-derived near/window/backface tests, choose
  hysteretic tier, then mark only that tier's compact positions. Do not perform
  primitive projected-area tests before projected vertices exist.

- [ ] **Step 6: Delete accepted-path full-position marking**

  Keep the predecessor admission set as a host oracle only. Add counters for
  clusters tested/admitted and positions admitted/transformed.

- [ ] **Step 7: Run generator, cluster, visible-position, hot-promotion, BSP-header, and runtime-contract gates**

  Expected: all PASS; synthetic FAR work transforms strictly fewer positions.

- [ ] **Step 8: Update documents, commit, and complete two-stage review**

  Commit with `perf(saturn): select compact scene LODs before transform` and
  record generated identities plus remaining target visual gate.

### Task 4: Replace Mario's full-mesh transform and quadratic sort with actor meshlets

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_meshlets.h`
- Create: `src/port/saturn/gfx/saturn_actor_meshlets.c`
- Modify: `tools/saturn/extract_mario_actor.py`
- Modify: `src/port/saturn/gfx/saturn_mario_actor_mesh.h` through regeneration
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Create: `tools/saturn/actor_meshlet_test.c`
- Modify: `tools/saturn/dual_actor_worker_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces generated actor meshlets with bounds, material, opacity class,
  stable source ordinal, primitive span, and compact position/remap span.
- Produces
  `sm64_saturn_actor_meshlets_prepare(snapshot, pose, view, output, capacity,
  stats)` returning opaque batches in stable meshlet order and translucent
  refs in stable depth bins.
- Removes `s_actor_order` insertion sorting from the accepted target path.

  ```c
  typedef struct sm64_saturn_actor_draw_ref {
      uint16_t meshlet_id;
      uint16_t primitive_id;
      uint32_t sort_key;
  } sm64_saturn_actor_draw_ref_t;

  typedef struct sm64_saturn_actor_meshlet_output {
      sm64_saturn_actor_draw_ref_t *opaque;
      sm64_saturn_actor_draw_ref_t *translucent;
      uint16_t opaque_count;
      uint16_t translucent_count;
  } sm64_saturn_actor_meshlet_output_t;

  bool sm64_saturn_actor_meshlets_prepare(
      const sm64_saturn_render_snapshot_t *snapshot,
      const sm64_saturn_mario_actor_pose_t *pose,
      const sm64_saturn_render_view_t *view,
      sm64_saturn_actor_meshlet_output_t *output, uint16_t capacity,
      sm64_saturn_fast3d_profile_t *stats);
  ```

- [ ] **Step 1: Write failing actor fixtures**

  Cover culled meshlets causing zero transforms, shared-position remaps,
  opaque source order, translucent stable-bin order, command capacity, invalid
  generated spans, and serial output equivalence for a fully admitted pose.

- [ ] **Step 2: Add an anti-regression source assertion**

  Require that the accepted `demo_prepare_mario` path contains no nested
  insertion loop over `s_actor_draw_count` and does not transform
  `SM64_MARIO_VERTEX_COUNT` unconditionally.

- [ ] **Step 3: Add `verify-actor-meshlets` and record red evidence**

  Expected: missing API and source assertion failure.

- [ ] **Step 4: Generate bounded Mario meshlets and per-LOD remaps**

  Partition by material/opacity and a fixed maximum of 32 primitives per
  meshlet. Preserve source primitive IDs and texture-tile relationships.

- [ ] **Step 5: Implement early meshlet admission and compact transforms**

  Reject by bounds, select LOD, transform referenced positions once, then run
  primitive backface/zero-area/window rejection. Batch opaque meshlets without
  global comparison sorting; feed translucent refs to existing stable bins.

- [ ] **Step 6: Integrate serially before changing the worker scheduler**

  Keep the existing dual-worker mode disabled for this gate so failures are
  attributable to meshlet/ordering changes. Preserve master-owned Gouraud,
  texture, slot allocation, and terrain-relative insertion.

- [ ] **Step 7: Run actor-meshlet, dual-actor, depth-bin, command-template, and runtime gates**

  Expected: all PASS and no insertion-sort source pattern.

- [ ] **Step 8: Update documents, commit, and complete two-stage review**

  Commit with `perf(saturn): cull and bin Mario by meshlet`.

### Task 5: Replace fixed terrain/Mario joins with one opportunistic SH-2 queue

**Files:**
- Create: `src/port/saturn/gfx/saturn_render_job_queue.h`
- Create: `src/port/saturn/gfx/saturn_render_job_queue.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Create: `tools/saturn/render_job_queue_test.c`
- Modify: `tools/saturn/verify_dual_cpu_coherency.py`
- Modify: `Makefile.saturn.mk`
- Modify: `docs/saturn/UPSTREAM_CODE_LEDGER.md`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces bounded states `EMPTY`, `READY`, `CLAIMED_MASTER`,
  `CLAIMED_SLAVE`, `DONE`, `FAILED`, and `QUARANTINED`.
- Produces `sm64_saturn_render_job_queue_publish`, `claim_master`,
  `claim_slave`, `complete`, `fail`, `all_terminal`, and `reset_retired`.
- Job descriptors contain type, snapshot generation, immutable input span,
  disjoint output span/capacity, and a callback ID resolved from a static
  table; they contain no arbitrary function or live-state pointers.

  ```c
  bool sm64_saturn_render_job_queue_publish(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      const sm64_saturn_render_job_t *jobs, uint16_t count);
  bool sm64_saturn_render_job_queue_claim_master(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t *job_index);
  bool sm64_saturn_render_job_queue_claim_slave(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t *job_index);
  bool sm64_saturn_render_job_queue_complete(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t job_index, sm64_saturn_render_job_state_t claimed_state);
  bool sm64_saturn_render_job_queue_fail(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t job_index, sm64_saturn_render_job_state_t claimed_state);
  bool sm64_saturn_render_job_queue_all_terminal(
      const sm64_saturn_render_job_queue_t *queue, uint32_t generation);
  bool sm64_saturn_render_job_queue_reset_retired(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation);
  ```

- [ ] **Step 1: Write failing exactly-once queue tests**

  Use host threads to race master/slave claims. Assert every job has one owner,
  no output overlap, stable completion merge order, queue-full fail-closed,
  stale-generation rejection, and useful master claims while the slave is
  occupied.

- [ ] **Step 2: Extend coherency mutation tests**

  Reject cached state words, publication before descriptor completion,
  function pointers in descriptors, missing cache-through peer reads, and
  reset before terminal retirement.

- [ ] **Step 3: Add `verify-render-job-queue` and record red evidence**

  Expected: missing queue API/source patterns.

- [ ] **Step 4: Implement the host state machine and SH-2 polling consumer**

  Use uncached 32-bit claim/state words and generation-last publication. The
  slave polling entry repeatedly claims `READY` work until no work remains;
  the master calls `claim_master` after simulation and between final-order
  tasks. Use callback IDs for world admission, world lowering, actor admission,
  and actor lowering.

- [ ] **Step 5: Integrate terrain and actor jobs into one frame queue**

  Remove separate terrain join, Mario transform join, and Mario classify join
  from the accepted path. Merge only after `all_terminal(generation)`.

- [ ] **Step 6: Preserve old fixed-split worker as a diagnostic build mode**

  The diagnostic mode may compare output but cannot be the production default
  or trigger full-span replay.

- [ ] **Step 7: Run queue/coherency/actor/cluster/runtime gates**

  Expected: all PASS; a fixture with a deliberately slow slave proves the
  master claims other work rather than spinning.

- [ ] **Step 8: Update provenance and live documents**

  Record this scheduler as project-owned pattern-informed code unless exact
  upstream lines were close-ported; cite Z-Treme/SGL persistent consumer and
  SlaveDriver disjoint ownership without claiming their exact scheduler.

- [ ] **Step 9: Commit and complete two-stage review**

  Commit with `perf(saturn): schedule render work opportunistically`.

### Task 6: Add cancellation, localized recovery, and permanent quarantine

**Files:**
- Modify: `src/port/saturn/gfx/saturn_render_job_queue.h`
- Modify: `src/port/saturn/gfx/saturn_render_job_queue.c`
- Modify: `src/port/saturn/gfx/saturn_render_snapshot.h`
- Modify: `src/port/saturn/gfx/saturn_render_snapshot.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Create: `tools/saturn/render_job_recovery_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces `request_cancel`, `acknowledge_cancel`, `retire_generation`, and
  `quarantine_generation`.
- A failed/unclaimed job may be republished only into a fresh generation and
  different output bank. A merely timed-out `CLAIMED_SLAVE` job cannot be
  executed by the master.
- Produces explicit profile counters for cancel requests/acks, localized
  retries, quarantined banks, previous-frame reuse, and degraded renderer.

  ```c
  bool sm64_saturn_render_job_queue_request_cancel(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t job_index);
  bool sm64_saturn_render_job_queue_acknowledge_cancel(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation,
      uint16_t job_index);
  bool sm64_saturn_render_job_queue_retire_generation(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation);
  bool sm64_saturn_render_job_queue_quarantine_generation(
      sm64_saturn_render_job_queue_t *queue, uint32_t generation);
  ```

- [ ] **Step 1: Write deterministic timeout/recovery failures**

  Use a delayed worker that writes after timeout. Assert the old span is never
  reset or reused, the previous complete frame remains selected, acknowledged
  retirement permits only the missing job to retry in a new bank, and a worker
  that never acknowledges leaves its bank quarantined permanently.

- [ ] **Step 2: Add `verify-render-job-recovery` and record red evidence**

  Expected: current queue cannot represent cancellation/quarantine.

- [ ] **Step 3: Implement cooperative cancellation and positive retirement**

  Check cancellation between bounded cluster/meshlet items. Publish the ack
  only after the callback can no longer write. Keep generation/bank ownership
  unchanged until that release is observed.

- [ ] **Step 4: Implement localized retry and degraded presentation**

  Copy only failed or never-claimed descriptors into a fresh generation. If
  positive retirement is absent, mark renderer degraded, retain the previous
  complete frame, and exclude the quarantined bank from allocation.

- [ ] **Step 5: Delete whole-range serial recovery from accepted paths**

  Add a source mutation test rejecting calls that rerun `0..count` after a
  partial master/slave generation.

- [ ] **Step 6: Run recovery, queue, coherency, snapshot, actor, and cluster gates**

  Expected: all PASS, including delayed old-generation writes.

- [ ] **Step 7: Update documents, commit, and complete two-stage review**

  Commit with `fix(saturn): recover render jobs without replay races`.

### Task 7: Make command/Gouraud source-bank ownership explicit

**Files:**
- Create: `src/port/saturn/gfx/saturn_vdp1_frame_bank.h`
- Create: `src/port/saturn/gfx/saturn_vdp1_frame_bank.c`
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h`
- Modify: `src/port/saturn/gfx/saturn_gouraud_bank.h`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Create: `tools/saturn/vdp1_frame_bank_test.c`
- Modify: `tools/saturn/test_verify_sourceboot_memory_map.py`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces `sm64_saturn_vdp1_frame_bank_t` with command pointer/count,
  Gouraud pointer/count, snapshot generation, worker ticket, command-transfer
  ticket, Gouraud-transfer ticket, and state `FREE`, `BUILDING`, `READY`,
  `TRANSFERRING`, `PUBLISHED`, or `QUARANTINED`.
- Commands remain in two `.lwram_cmdts` 2,048-entry banks. Gouraud staging
  remains two aligned HWRAM banks.

  ```c
  bool sm64_saturn_vdp1_frame_bank_begin_build(
      sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation,
      sm64_saturn_vdp1_frame_bank_t **out);
  bool sm64_saturn_vdp1_frame_bank_ready(
      sm64_saturn_vdp1_frame_bank_t *bank, uint16_t command_count,
      uint16_t gouraud_count, uint32_t worker_ticket);
  bool sm64_saturn_vdp1_frame_bank_publish(
      sm64_saturn_vdp1_frame_bank_set_t *banks,
      sm64_saturn_vdp1_frame_bank_t *bank);
  bool sm64_saturn_vdp1_frame_bank_retire(
      sm64_saturn_vdp1_frame_bank_set_t *banks, uint32_t generation);
  ```

- [ ] **Step 1: Write bank lifecycle and wrong-region tests**

  Assert build/ready/transfer/publish/reuse order, ticket retirement, stale
  generation rejection, command-source LWRAM classification, Gouraud-source
  HWRAM classification, and quarantine exclusion.

- [ ] **Step 2: Strengthen the memory-map test**

  Require `.lwram_cmdts` size exactly
  `2 * 2048 * sizeof(vdp1_cmdt_t)`, 32-byte alignment, command banks outside
  HWRAM, Gouraud staging inside HWRAM, and linker margin at least `0x1B00`.

- [ ] **Step 3: Add `verify-vdp1-frame-bank` and record red evidence**

  Expected: missing bank manager and ownership metadata.

- [ ] **Step 4: Implement the state/ticket manager**

  Bind the existing backend and Gouraud structures through one bank object.
  `begin_build` succeeds only for `FREE`; `publish` succeeds only after worker
  and both non-invalid transfer tickets retire.

- [ ] **Step 5: Replace ad-hoc XOR and misleading generation variables**

  Remove `sourceboot_vdp1_cmdts_bank ^= 1U` as the ownership decision. Select a
  bank through `begin_build`, retain the previous published bank when none is
  free, and expose accurate build/published/displayed framebuffer generations.

- [ ] **Step 6: Run bank, memory-map, DMA queue, command-template, and runtime gates**

  Expected: all PASS and no command-bank migration into HWRAM.

- [ ] **Step 7: Update documents, commit, and complete two-stage review**

  Commit with `feat(saturn): track VDP1 source-bank lifetimes`.

### Task 8: Transfer command and Gouraud banks without immediate waits

**Files:**
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.h`
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.c`
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h`
- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.h`
- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_vdp2_frame.c`
- Modify: `tools/saturn/dma_queue_test.c`
- Create: `tools/saturn/vdp1_transfer_pipeline_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: `docs/saturn/SLAVEDRIVER_ADAPTATION.md`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Adds queue mode `SATURN_DMA_QUEUE_CPU_DMAC` for LWRAM command sources and
  retains `SATURN_DMA_QUEUE_SCU` for valid HWRAM Gouraud sources.
- Produces `sm64_saturn_vdp1_frame_bank_submit_transfers()` and
  `sm64_saturn_vdp1_frame_bank_poll_transfers()`; submit returns immediately.
- The only blocking wait is at source-bank reuse or the safe publication
  boundary when the required ticket has not retired.

  ```c
  bool sm64_saturn_vdp1_frame_bank_submit_transfers(
      sm64_saturn_vdp1_frame_bank_t *bank,
      const sm64_saturn_vdp1_transfer_targets_t *targets);
  bool sm64_saturn_vdp1_frame_bank_poll_transfers(
      sm64_saturn_vdp1_frame_bank_t *bank);
  bool sm64_saturn_vdp1_frame_bank_wait_for_publish(
      sm64_saturn_vdp1_frame_bank_t *bank,
      sm64_saturn_vdp1_wait_stats_t *waits);
  ```

- [ ] **Step 1: Write red transfer-selection tests**

  Mock CPU-DMAC and SCU DMA separately. Assert LWRAM commands select CPU-DMAC,
  HWRAM Gouraud selects SCU DMA, LWRAM+SCU is rejected, both tickets are needed
  before publication, and submit performs zero waits.

- [ ] **Step 2: Add source anti-pattern checks**

  Reject `vdp1_sync_wait(); saturn_dma_queue_kick();
  saturn_dma_queue_wait(...)` inside `sm64_saturn_demo_render_frame()` and
  reject direct `sm64_saturn_vdp1_backend_upload()` in its accepted pipeline.

- [ ] **Step 3: Add `verify-vdp1-transfer-pipeline` and record red evidence**

  Expected: current immediate-wait sequence and CPU-copy upload are detected.

- [ ] **Step 4: Add CPU-DMAC queue transport using pinned Yaul APIs**

  Adapt the public `cpu_dmac_transfer(0, dst, src, size)` lifecycle from pinned
  Yaul `libyaul/scu/bus/cpu/cpu_dmac.c`. Do not copy internal implementation.
  Poll `cpu_dmac_status_get()` and channel-0's `channel_busy` bit; call
  `cpu_dmac_transfer_wait(0)` only at bank reuse/publication when polling has
  not already observed retirement.

- [ ] **Step 5: Submit both transfers after bank construction**

  Finish command links, publish CPU cache writes, submit command CPU-DMAC and
  Gouraud SCU DMA, return from render construction, and poll tickets from the
  main loop. Remove the disabled `#if 0 && ...` upload branch.

- [ ] **Step 6: Publish only after transfer and VDP1 list safety**

  Wait for old VDP1 list ownership only at the actual VRAM overwrite/publish
  boundary. Never overwrite the displayed/plotting list. Coalesce VDP2 state at
  the same VBlank-owned transition.

- [ ] **Step 7: Measure the real waits**

  Add distinct counters for command CPU-DMAC wait, Gouraud SCU-DMA wait,
  VDP1-list overwrite wait, bank-reuse wait, and terminal fence. Update HUD and
  decoder so `VDP1W` no longer samples only later nonblocking calls.

- [ ] **Step 8: Run DMA, transfer, bank, VDP2, memory-map, and runtime gates**

  Expected: all PASS; submit-before-wait ordering and transport selection are
  enforced by mutation tests.

- [ ] **Step 9: Update provenance/live documents, commit, and review**

  Correct `SLAVEDRIVER_ADAPTATION.md` to state that upstream active DMA is
  serial and this asynchronous lifecycle is project hardening. Commit with
  `perf(saturn): defer VDP1 source-bank transfers` and complete both reviews.

### Task 9: Overlap snapshot rendering with simulation and bound catch-up

**Files:**
- Create: `src/port/saturn/runtime/saturn_frame_pipeline.h`
- Create: `src/port/saturn/runtime/saturn_frame_pipeline.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/gfx/saturn_vdp2_frame.c`
- Create: `tools/saturn/frame_pipeline_test.c`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Modify: `Makefile.saturn.mk`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Produces `sm64_saturn_frame_pipeline_step(pipeline, vblank_count)` returning
  explicit actions `RUN_SIM_TICK`, `SERVICE_RENDER_JOBS`,
  `POLL_TRANSFERS`, `PUBLISH_FRAME`, `WAIT_VBLANK`, or
  `REUSE_PREVIOUS_FRAME`.
- Authoritative simulation may be one generation ahead of the framebuffer
  selected for display; camera, Mario, actors, and terrain within one render
  snapshot always share a generation.
- Catch-up executes at most one normal tick plus one bounded recovery tick per
  outer iteration; further accumulated time is dropped with a counter.

  ```c
  typedef enum sm64_saturn_frame_action {
      SM64_SATURN_FRAME_RUN_SIM_TICK,
      SM64_SATURN_FRAME_SERVICE_RENDER_JOBS,
      SM64_SATURN_FRAME_POLL_TRANSFERS,
      SM64_SATURN_FRAME_PUBLISH_FRAME,
      SM64_SATURN_FRAME_WAIT_VBLANK,
      SM64_SATURN_FRAME_REUSE_PREVIOUS_FRAME
  } sm64_saturn_frame_action_t;

  sm64_saturn_frame_action_t sm64_saturn_frame_pipeline_step(
      sm64_saturn_frame_pipeline_t *pipeline, uint32_t vblank_count);
  ```

- [ ] **Step 1: Write a deterministic scheduler model test**

  Feed synthetic VBlank/render/transfer completion events. Assert snapshot N
  presentation while simulation is N+1, no mixed generations, useful job
  service before terminal wait, previous-frame reuse on missed deadline, and
  no four-tick burst after a slow render.

- [ ] **Step 2: Add mutation coverage for the death spiral**

  Reject `SOURCEBOOT_MAX_SIM_CATCHUP 4U`, catch-up loops that add VBlank credit
  while executing a tick, and publication of non-complete banks.

- [ ] **Step 3: Add `verify-frame-pipeline` and record red evidence**

  Expected: missing scheduler and existing catch-up mutation failure.

- [ ] **Step 4: Implement the pure scheduler first**

  Keep it hardware-free and drive all state transitions through explicit
  events. Increment dropped-credit and previous-frame-reuse counters rather
  than silently hiding overload.

- [ ] **Step 5: Integrate the outer sourceboot loop**

  Advance authoritative simulation, publish immutable snapshot, notify the
  persistent slave, let the master claim work after its sim phase, poll DMA,
  and publish only complete frame banks. Replace the current catch-up loop and
  direct same-iteration render call.

- [ ] **Step 6: Verify generation-coherent VDP2 composition**

  Sky camera and HUD metrics must name the same displayed/rendered/simulation
  generations. VDP2 remains geometry-free.

- [ ] **Step 7: Run frame-pipeline, snapshot, queue, recovery, transfer, VDP2, runtime, and replay-host gates**

  Expected: all PASS with explicit one-generation presentation lag.

- [ ] **Step 8: Update documents, commit, and complete two-stage review**

  Commit with `perf(saturn): overlap render snapshots with simulation`.

- [ ] **Step 9: Build one serial integrated experimental CUE and manually test**

  Use the same BOB/live-input/Q16-camera role as Task 1. Record hashes,
  controls, visible geometry, fault counters, displayed/rendered/simulation
  generations, and qualitative speed. This experimental CUE still does not
  require strict native-math closure.

### Task 10: Prove scene-neutral behavior and publish the hardened candidate

**Files:**
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/verify_dual_cpu_coherency.py`
- Modify: `tools/saturn/verify_sh2_native_math.py` only if a real target edge
  requires a separately reviewed verifier correction
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Create: `tools/saturn/test_overlapped_pipeline_source.py`
- Modify: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Modify: architecture spec and this plan

**Interfaces:**
- Final role may activate source-render suppression only after A1 supplies its
  independently approved state-only seam and differential evidence. Until
  then it retains the full source geo walk while integrating immutable
  snapshot banks, pre-transform cluster/meshlet LOD, shared job queue,
  localized recovery, explicit source banks, CPU-DMAC command upload,
  SCU-DMA Gouraud upload, one-frame presentation lag, and bounded cadence.
- Produces immutable ELF/CUE hashes, build flags, replay state, generation
  telemetry, worker ownership, transfer waits, command/geometry counters, and
  manual result.

- [ ] **Step 1: Add the final source-contract test**

  Require every architecture component in the linked source set, reject the
  old fixed joins/full-span fallback/immediate waits/insertion sort/four-tick
  catch-up, and verify `SATURN_RENDERER_PIPELINE` is an actual validated
  compile-time feature rather than an output-directory label.

- [ ] **Step 2: Run the complete host gate serially**

  Run the focused targets from Tasks 1–9, `verify-tools`,
  `verify-runtime-contracts`, `verify-dual-frame-bank` (which invokes
  `verify_dual_cpu_coherency.py`), generator tests, and all native-math
  verifier unit tests. Record every command and result;
  do not substitute a narrow green test for a broader failed gate.

- [ ] **Step 3: Build reference and candidate serially through the audited wrapper**

  Use identical ROM, route, camera, input bootstrap, cart staging, clipping,
  BSP, and LOD flags. Change only the named pipeline feature. Set worktree-local
  MSYS `HOME`, `TMPDIR`, and `TMP` before `make -B -j1`.

- [ ] **Step 4: Run linked target inspection**

  Prove queue/slave callbacks, cache-through publication, CPU-DMAC command
  path, SCU-DMA Gouraud path, absence of accepted-path full replay and
  insertion sort, HWRAM/LWRAM section ownership, and no unresolved illegal hot
  math edges. Any verifier change is a separate TDD commit with independent
  review; it cannot fabricate edges or bless missing evidence.

- [ ] **Step 5: Run deterministic BOB replay and coherency checks**

  Require matching final Mario action/position, camera state under the approved
  snapshot relationship, scene/content identity, visible non-sky terrain and
  Mario, zero overflow/corruption, no stale generation, and no quarantined bank
  in the ordinary route.

- [ ] **Step 6: Run manual Ymir acceptance with DRAM cart**

  Confirm controls, BOB traversal, camera feel, geometry/material stability,
  and obvious speed relative to the Task 1 and pre-architecture CUEs. Record
  observed limitations honestly; emulator timing is comparative, not retail
  hardware proof.

- [ ] **Step 7: Exercise a non-BOB generated fixture**

  Compile and host-run a synthetic second scene/actor bank through cluster,
  snapshot, queue, ordering, and bank-capacity contracts. Reject BOB symbol
  dependencies in generic runtime modules.

- [ ] **Step 8: Reconcile every document and gate**

  Check every individual step in this plan, update all ten summary statuses,
  append final decisions/deviations to the architecture spec, and complete the
  evidence report with commits, reviews, tests, hashes, and remaining retail
  hardware gate. Keep any unpassed gate unchecked with its exact reason.

- [ ] **Step 9: Commit publication records and request final independent review**

  Commit documentation/evidence with
  `docs(saturn): publish overlapped pipeline evidence`. Run specification and
  quality reviews across the full commit range and resolve all blockers before
  claiming completion.

## Exact target-build wrapper

Use this shape for the two named experimental gates and Task 10. Never run two
instances concurrently:

```powershell
$w = 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\sh2-native-math-purge'
$cmd = @'
export PATH=/mingw64/bin:/usr/bin:$PATH
export HOME=/d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge/.msys-home
export TMPDIR=$HOME/tmp
export TMP=$HOME/tmp
mkdir -p "$TMPDIR"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
source ../../.yaul.env
export SHELL=/usr/bin/sh
make -C src/port/saturn/sourceboot -B -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_RENDERER_PIPELINE=4
'@
& "$w\tools\saturn\with-msys-toolchain.ps1" 'C:\msys64\usr\bin\sh.exe' '--noprofile' '--norc' '-lc' $cmd
```

`SATURN_RENDERER_PIPELINE=4` must be validated and passed into target
preprocessor flags by Task 10. The former Task 1 `pipe3` scene-graph
suppression checkpoint is prohibited after `77ee306c`. The only exception is
the owner-authorized, default-off, demo+replay-sealed Task 1D
`diag-skip-geo` upper-bound diagnostic; it is non-promotable and cannot count
as an A1 seam, replay baseline, or full-game evidence. No other experimental
role may re-enable suppression without the approved A1 seam and tests.

## Completion criteria

All ten tasks and their individual steps are checked with contemporaneous
commits, two-stage reviews, actual test results, and remaining gates. The
manual BOB build shows an obvious improvement over the pre-architecture
low-single-digit candidate, controls work, source gameplay/camera remain
authoritative, both SH-2s perform useful nonduplicated work, VDP1 and VDP2 own
their distinct layers, and no partial/stale bank is published. Final
publication additionally passes strict linked native-math, coherency, replay,
content-identity, memory-map, and command-capacity gates. Retail hardware
performance remains explicitly unproven until run on retail hardware.
