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
  and target visual/counter evidence. The owner manually observed roughly
  3–4 FPS in the fresh A3+A4 desktop-Ymir candidate, up from the 1–2 FPS
  baseline; this is positive qualitative evidence, not a completed target
  acceptance gate. The A3
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
- [ ] **Task 4 / A4 — Mario meshlets and bounded ordering:** active. Generated
  full-game Mario meshlets carry tight bounds, material/opacity partitions,
  source ordinals, and compact near/mid/far remaps. The serial master path
  admits them before actor transform dispatch, keeps opaque source order, and bins only
  textured/translucent work; it no longer has `s_actor_order` insertion
  sorting. Focused host tests and mutation checks are green. Independent
  reviews and source-only-authorized target visual/counter evidence remain
  open; no performance claim is made.
  **Full-animation preservation correction (2026-08-04):** the legacy Castle
  demo already proved Mario's source-driven full animation in the Saturn
  renderer. A4 preserves that animation/pose path while making its rendering
  cheaper; it is not a plan to recreate Mario animation. Future enemies are
  the same renderer category as Mario—an animated actor with a pose, bounds,
  material partitions, and draw data—not a separate gameplay rewrite. Their
  additional work is coverage: extracting arbitrary object-family
  model/animation banks, bridging their live source pose and visibility state,
  and preserving each family's special rendering features.
- [x] **Task 5 / A5 — shared opportunistic SH-2 queue:** complete. The
  immutable, cache-through queue contract, host exact-once fixture, and
  coherency mutation gate are source-complete. Descriptor kind now selects a
  terrain or actor output bank, and the CPU that actually claims the descriptor
  publishes its cache lane atomically; this removes the unsafe `begin == 0`
  inference that prevented work stealing. That contract is now active in the
  accepted atomic-cutover renderer and has matching target evidence. The
  A5.5 bridge final review is GO for source-only scope at 87824a5a; it
  explicitly does not authorize activation. **Design correction (2026-08-04,
  A5.6):** inspection of the live renderer proved the bridge's descriptor
  release metadata is not a payload migration: terrain results are physically
  split between master/slave arrays, actor vertices use fixed owner metadata,
  and the linked generic worker owns the sole CPU-DUAL callback. A safe
  activation therefore requires a distinct atomic migration of payload banks,
  readers, and CPU-DUAL lifecycle. The watched
  test_render_job_live_cutover_source.py is RED against the current fixed
  dispatch; no partial queue bind is permitted. The accepted 3–4 FPS A3+A4
  CUE remains the rollback baseline. Those A5.6 gates were subsequently closed
  by the A5.8 atomic cutover, target build, reviews, and A5.9 live observation.
  The dated A5.8 notes below preserve each increment's at-the-time dormant/open
  status; they are history, not the current task state.
  **A5.8 terrain migration milestone (2026-08-04):** the live-cutover source
  gate is now a host-compiled C executable because the configured `py -3`
  launcher is unavailable. Its expected RED result proves the default frame
  remains legacy. The renderer now has an unactivated terrain binding seam
  that verifies an exact claimed WORLD descriptor and derives its terrain
  record/command span from `output_offset`, `output_capacity`, and recorded
  claimant lane through the existing payload-bank primitive. It does not run
  terrain work, register CPU-DUAL, or mark a descriptor terminal; activation
  remains forbidden until terrain and Mario producer/consumer callbacks are
  both complete.
  **A5.8 terrain producer/reader increment (2026-08-04):** the dormant
  `WORLD_LOWER` callback now passes its exact claimed descriptor's input span,
  claimant-derived lane, and descriptor-owned result arena into the shared
  transform/classify/compact producer. It seals that arena before returning to
  the graph runtime, which is the only code permitted to publish that claim's
  `DONE`. A matching master reader refuses anything but that exact terminal
  WORLD_LOWER descriptor and derives both record and command payload aliases
  through the bridge. The legacy wrapper remains the accepted default path and
  contains the fixed `begin == 0` rule; the dormant queue route contains none.
  The new host C source contract was watched RED then GREEN. This is still not
  a live migration: `WORLD_ADMIT` must first own descriptor-indexed
  transformed-position output/publication, and `WORLD_LOWER` must persist its
  per-job result count/sequence for exact DONE merge. Mario's matching
  producer/reader conversion is also required before atomic CPU-DUAL
  activation.
  **A5.8 terrain admit/merge increment (2026-08-04):** WORLD_ADMIT now owns
  transformed-position publication through a P2-visible record keyed by the
  exact claim. WORLD_LOWER no longer repeats that transform; after compacting
  it seals its descriptor-owned arena and records exact count, sequence,
  claimant state, and writer lane. The terminal reader accepts only a DONE
  WORLD_LOWER whose queue/output-bank identity agrees with that record, then
  returns the record's count/sequence instead of accepting caller metadata.
  These dormant callbacks remain unregistered; merge-span assembly, Mario's
  equivalent route, review, and one atomic CPU-DUAL activation are still open.
  **A5.8 review correction (2026-08-04):** scheduler eligibility alone is not
  consumer proof. WORLD_LOWER now asks the P2-visible graph for exactly one
  immutable predecessor, requires it to be the exact terminal WORLD_ADMIT
  descriptor, and validates that descriptor's output-bank/P2 metadata before
  classifying. Host mutations cover both an unready predecessor and a terminal
  wrong-type predecessor; both fail closed. This is still dormant source work,
  not authorization for CPU-DUAL activation.
  **A5.8 terrain merge-span increment (2026-08-04):** the dormant master
  assembler now visits only terminal WORLD_LOWER descriptors, revalidates each
  P2 result release and its actual claimant lane, requires one generation and
  one sequence across the frame, and constructs result identities in immutable
  descriptor/local-output order. The graph validates those identities before
  the existing stable depth-bin ordering runs. It retains descriptor-owned
  record/command streams rather than reconstructing the fixed legacy arenas;
  master final ordering remains sole owner. The callback is unregistered and
  the legacy worker is unchanged pending Mario parity and review.
  **A5.8 merge review repair (2026-08-04):** the first assembler iterated
  only `DONE` descriptors, so a current READY/CLAIMED WORLD_LOWER could be
  silently absent. It now enumerates every immutable P2 current-generation
  descriptor first and fails closed unless each WORLD_LOWER rereads as the
  identical `DONE` descriptor before metadata/payload consumption. The graph
  fixture now executes the collection contract and proves both READY and
  CLAIMED lower jobs reject merge. Once every lower is DONE, a zero-record
  all-culled frame validates and produces an empty stable merge rather than a
  false failure. No callback is activated.
  **Review repair (2026-08-04):** the first dormant callback still entered
  classification through a helper that inferred the lane from `begin == 0`.
  A legal slave claim at descriptor input offset zero would therefore select
  master cache ownership. `demo_classify_exact()` now receives the explicit
  claimant-derived lane from the exact producer; the range inference is
  confined to the legacy adapter. The focused route contract includes that
  slave-at-zero mutation and is GREEN; fresh re-review remains required.
  **A5.8 Mario queue-parity increment (2026-08-04):** source-reviewed GO at
  `1d1137f1`; independent review `3ad0d23e` found no critical or important
  source-scope defect. The dormant actor route snapshots the same live
  posed vertices, light intensities, animation frame/bank metadata, and yaw
  already proven by the legacy Castle demo. ACTOR_ADMIT transforms the exact
  descriptor-owned compact vertex-reference payload; ACTOR_LOWER fails closed
  unless its one immutable ACTOR_ADMIT predecessor is the exact completed
  descriptor, then classifies through the predecessor's claimant-selected
  payload alias. Both phases publish descriptor-keyed count, sequence,
  claimant state, and writer lane before runtime may expose DONE. The dormant
  master assembler requires every ACTOR_LOWER to be DONE, validates complete
  primitive coverage in descriptor/local order, validates all payload
  identities before mutating renderer state, and restores the legacy
  master-owned projected/ref banks so command emission and full animation
  remain unchanged. No callback is registered and the live-cutover gate stays
  RED. The output-offset namespace is now source-resolved through
  per-physical-payload-kind validation; independent review is GO at
  `db28fd87` with audit `c5da1516`. Activation
  remains blocked on terrain command lookup from ordered descriptor streams, one atomic
  CPU-DUAL owner replacement, target link/cache evidence, and manual Ymir
  validation. Before activation, add direct corrupt-identity, incomplete
  coverage, stale-sequence, and cross-lane callback tests and publish the
  callback context through an explicit P2/cache-through contract.
  **A5.8 payload namespace increment (2026-08-04):** source-review GO at
  `db28fd87`; audit `c5da1516` found no source-scope defect. Inspection proved
  a bounded global layout would add
  artificial offset pressure because all four phases already write separate
  physical arrays. Queue publication now derives WORLD_ADMIT, WORLD_LOWER,
  ACTOR_ADMIT, or ACTOR_LOWER payload kind from the exact immutable
  type/callback pair. It permits local offset reuse across those physical
  banks, retains disjoint-span enforcement within one kind, and rejects
  unknown or mismatched pairs. Watched RED/GREEN host mutations cover all-bank
  offset-zero reuse, same-kind corruption, callback mismatch, and unknown
  kind. The descriptor remains pointer-free and 16 bytes. No live CPU-DUAL
  activation, target build, CUE/Ymir run, or FPS claim occurred. Remaining:
  ordered terrain command lookup, callback-context P2 publication and
  corruption/cross-lane callback tests, atomic cutover, then
  target/cache/manual evidence.
  **A5.8 ordered-command/context increment (2026-08-04):** source-complete,
  pending independent review. The final terrain radix stream now keeps each
  validated result paired with its exact descriptor-local 32-byte command
  image without increasing the SH-2 emit-reference ABI beyond eight bytes;
  legacy refs remain on the accepted arena lookup until cutover. A new
  pointer-free 16-byte release record publishes callback generation,
  sequence, descriptor index, byte bound, phase, and producer lane through
  P2. Terrain and Mario dormant callbacks open their statically bounded
  contexts only after exact claimed-descriptor validation and select the
  cache-through alias for a peer claimant. Executable host cases cover both
  phases plus corrupt phase, stale generation/sequence, incomplete release,
  wrong claim, out-of-range identity, and cross-lane selection. No CPU-DUAL
  activation/default-path change, target build, CUE, Ymir run, or FPS claim
  occurred. Remaining: independent review, atomic sole-owner cutover, then
  target/cache/manual evidence.
  **A5.8 command/context review repair (2026-08-04):** initial review NO-GO at
  `891f64b2`. Although the 16-byte release ABI was correct, the renderer
  publisher had no preparation caller and slave callbacks could follow nested
  master-cached pointers. The repair copies Mario's dynamic vertex-reference
  list inline, replaces terrain's stack classify/spans dependency with a
  self-contained job/work-order snapshot, and gives the future atomic cutover
  one preclaim prepare/publish boundary. Four phase-specific opens execute for
  both claimant lanes; mutations now cover generation, sequence, stored index,
  phase, byte bound, producer lane, ready, claim, and range for WORLD_ADMIT,
  WORLD_LOWER, ACTOR_ADMIT, and ACTOR_LOWER. Fresh re-review is required; live
  activation and all target/manual gates remain unchecked.
  First re-review retained one test-coverage NO-GO: several mutations still
  named only lower callbacks. The final source repair parameterizes the entire
  matrix across all four phases and explicitly invokes a mismatched
  phase-specific opener. Final independent source re-review is GO. Target
  compile/link and section placement, the atomic sole-owner cutover, target
  cache behavior, CUE/Ymir, and FPS evidence remain unchecked.
  **A5.8 target compile repair (2026-08-04):** the first exact serialized
  Route-0/live-input/Pipe4 target gate reached the newly linked queue sources
  but failed before link because `saturn_render_job_queue.c` used Yaul's
  `CPU_CACHE_THROUGH` without importing its defining header. A watched source
  contract now requires the SH-only `<cpu/cache.h>` include, and the narrow
  fix is host-green. Independent repair review is GO at `8eef1c22`; the one
  serialized post-review rebuild then compiled, linked, and packaged at exit
  0. The map proves live queue/graph/context/payload initializers in HWRAM,
  P2 shared metadata placement, no unresolved symbols, 6,160 bytes of HWRAM
  margin, and 60,368 bytes of LWRAM margin. Target compile/link/section
  placement is therefore complete for the dormant route. Runtime cache
  behavior, atomic CPU-DUAL activation, Ymir, and FPS remain unchecked.
- [x] **Task 5.5 / A5.5 — descriptor-to-result ownership bridge:**
  source-complete; final independent source review is GO. The new bridge proves an exact
  queue descriptor index, actual claimant, and output kind select the
  writer/reader cache lane; consumers reject a job until it is `DONE`.
  Neither `begin == 0` nor a fixed terrain/actor split participates. It also
  defines one-owner source arming only, rejecting a second arm. A5.5
  deliberately compiles no CPU-DUAL registration or notification while the
  legacy worker is linked; only the atomic live cutover may
  bind it after removing all legacy dispatches. It is not
  now bound to the accepted renderer through the reviewed atomic cutover. Live
  target cache/ordering and automatic FPS/queue evidence are complete.
- [x] **Task 5.7 / A5.7 — queue job graph and phase barriers:** complete.
  Independent descriptors cannot safely replace terrain's transform→classify→
  multi-result merge or Mario's transform→classify chains. This foundation
  adds P2-visible dependency masks, exact result identities, consumer
  eligibility only after producer `DONE`, and failed-predecessor quarantine.
  It was introduced without binding the renderer or altering its sole legacy
  CPU-DUAL owner; A5.8 subsequently completed that one atomic conversion.
  **Review correction:** the graph rejects cyclic/self dependencies before queue
  publication and propagates a failed predecessor to every reverse-chain
  ready dependent before terminal merge/reset; independent READY jobs remain
  eligible and are not treated as blocked.
- [x] **Task 5.8.1 / A5.8 — graph-aware runtime prerequisite:** source-complete;
  independent rereview is GO. The queue runtime's ordinary ready-drain would
  bypass graph eligibility, so it cannot own a live callback for dependent
  terrain/Mario phases. The graph activation/drains now claim through the
  graph for both CPUs and propagate failed-predecessor quarantine. Its focused
  host test publishes `WORLD_LOWER` before its required `WORLD_ADMIT` and
  proves the producer runs first. Review repair: after a graph claim every
  descriptor fetch uses the queue's P2/cache-through claimed-job accessor and
  revalidates the exact claimant state; raw cached queue descriptor reads are
  mutation-tested forbidden. This is a scheduler prerequisite only:
  physical terrain/actor payload arrays, renderer readers, and the sole
  CPU-DUAL live cutover were bound by the later reviewed A5.8 activation. The
  A3+A4 3–4 FPS candidate remains the rollback baseline.
- [ ] **Task 6 / A6 — localized recovery and quarantine:** pending.
- [x] **Task 7 / A7 — alternating source-bank ownership:** complete. The A5.9
  live capture proved both SH-2s claimed useful work with no recorded queue
  retirement wait in the observed frame. A7 now makes command/Gouraud source
  ownership explicit so A8 can defer the remaining master-owned transfers
  without overwriting a building, transferring, or displayed bank.
  The lifecycle, sourceboot integration, exact memory-map checks, and focused
  host gates are green. Independent rereview is PASS/APPROVED and the exact
  audited Route0/live-input/Pipe4 `make -B -j1` exits zero with ELF
  `1eba8888...e99267c`. No Ymir, hardware, asynchronous-transfer, or FPS
  evidence is claimed by A7.
- [x] **Task 8 / A8 — deferred transfers and true wait telemetry:** complete.
  Repair `8b037a7d` is
  focused-host-green, both independent rereviews are PASS/APPROVED, and the
  first exact target ELF was `5926ff27...17d9cab5`. Automatic Ymir exposed a
  zero-admitted-actor publication failure before DMA. Its final fail-closed,
  scene-neutral repair is independently approved and exact-target green at ELF
  `10e92064...df569ab`. Ten-event evidence measures 1.63 FPS mean with every
  queue generation retired and no queue wait/failure; A8 closes without an
  uplift claim and hands measured CPU-cost isolation to A9.
- [x] **Task 9 / A9 — bounded cadence compatibility adapter:** source-complete,
  serialized target/capture green, and independently reviewed. It removes the
  six-tick catch-up death spiral while deliberately retaining synchronous
  construction. Manual owner-visible Ymir acceptance and the unrelated broad
  native-math census remain open and are not converted into green gates.
- [ ] **Task 9A / A9A — true frame-lifetime overlap:** Steps 1--10 are
  source-complete. Fix Round 2 closes the review findings and scoped rereview
  of `420b6ce8..050aa3bc` is specification PASS, code-quality PASS, with no
  findings and GO for exactly one serialized target build/capture. No target/
  FPS claim exists yet. The accepted
  synchronous renderer is split into start and
  poll/finalize phases so immutable render generation `N` remains active while
  the master may execute the one queued source tick for `N+1`. Exactly one
  render generation, the A8 transport owner, master-only final ordering/VDP1,
  and all A9 cadence/publication laws remain unchanged.
- [ ] **Task 10 / A10 — full-game hardening and publication:** pending. This
  includes scene-neutral level extraction and validation, generalized dynamic
  actor/enemy banks using the same actor pipeline proven by Mario, and
  per-level coarse BSP/frustum/portal-window admission. It does **not** yet
  promise a full arbitrary occlusion/PVS system: that remains a separately
  measured extension only if coarse admission leaves VDP1-bound levels too
  expensive.

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
- Create: `src/port/saturn/gfx/saturn_lod_lifetime.h`
- Create: `src/port/saturn/gfx/saturn_lod_lifetime.c`
- Create: `src/port/saturn/runtime/saturn_render_overlap_phase.h`
- Create: `src/port/saturn/runtime/saturn_render_overlap_phase.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Create: `src/port/saturn/gfx/saturn_gouraud_transfer.h`
- Create: `src/port/saturn/gfx/saturn_gouraud_transfer.c`
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

- [x] **Step 1: Write failing actor fixtures**

  Cover culled meshlets causing zero transforms, shared-position remaps,
  opaque source order, translucent stable-bin order, command capacity, invalid
  generated spans, and serial output equivalence for a fully admitted pose.

- [x] **Step 2: Add an anti-regression source assertion**

  Require that the accepted `demo_prepare_mario` path contains no nested
  insertion loop over `s_actor_draw_count` and does not transform
  `SM64_MARIO_VERTEX_COUNT` unconditionally.

- [x] **Step 3: Add `verify-actor-meshlets` and record red evidence**

  Expected: missing API and source assertion failure.

- [x] **Step 4: Generate bounded Mario meshlets and per-LOD remaps**

  Partition by material/opacity and a fixed maximum of 32 primitives per
  meshlet. Preserve source primitive IDs and texture-tile relationships.

- [x] **Step 5: Implement early meshlet admission and compact transforms**

  Reject by bounds, select LOD, transform referenced positions once, then run
  primitive backface/zero-area/window rejection. Batch opaque meshlets without
  global comparison sorting; feed translucent refs to existing stable bins.

- [x] **Step 6: Integrate serially before changing the worker scheduler**

  Keep the existing dual-worker mode disabled for this gate so failures are
  attributable to meshlet/ordering changes. Preserve master-owned Gouraud,
  texture, slot allocation, and terrain-relative insertion.

- [x] **Step 7: Run actor-meshlet, dual-actor, depth-bin, and command-template gates**

  Expected: all PASS and no insertion-sort source pattern.

- [ ] **Step 8: Update documents, commit, and complete two-stage review — ACTIVE**

  Documentation/evidence is updated with the watched red, green host gates,
  explicit source-only scope, and remaining review/target gates. Behavior
  commit `7e419484` is recorded; independent specification then quality
  reviews remain open.

  The first independent A4 review returned **NO-GO**: neutral-AABB centre
  admission ignored yaw/live animation and the renderer reconstructed rather
  than consumed compact position spans. The active remediation projects each
  live pose vertex after yaw, culls by furthest extent, chooses LOD by nearest
  extent, bins translucent work by furthest extent, and publishes the exact
  globally deduplicated generated position union to the transform worker. Its
  yaw/view-plane, walking-pose, compact-stream, and existing mutation gates
  are in remediation commit `c5944bac`; they require a fresh independent
  rereview, and no target evidence is authorized here.

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

- [x] **Step 1: Write failing exactly-once queue tests**

  Use host threads to race master/slave claims. Assert every job has one owner,
  no output overlap, stable completion merge order, queue-full fail-closed,
  stale-generation rejection, and useful master claims while the slave is
  occupied.

- [x] **Step 2: Extend coherency mutation tests**

  Reject cached state words, publication before descriptor completion,
  function pointers in descriptors, missing cache-through peer reads, and
  reset before terminal retirement.

- [x] **Step 3: Add `verify-render-job-queue` and record red evidence**

  Expected: missing queue API/source patterns.

- [x] **Step 4: Implement the host state machine and SH-2 polling consumer — SOURCE-COMPLETE**

  Use uncached 32-bit claim/state words and generation-last publication. The
  slave polling entry repeatedly claims `READY` work until no work remains;
  the master calls `claim_master` after simulation and between final-order
  tasks. Use callback IDs for world admission, world lowering, actor admission,
  and actor lowering. The host state machine is present and explicitly tested;
  the SH-2 polling entry and callback-table integration remain open because
  the live A3/A4 candidate must not be changed by this source-only task. The
  queue now has host-tested `drain_master`/`drain_slave` polling loops and a
  local callback-table ABI: callbacks receive the claimed CPU state
  after descriptor claim, while descriptors remain pointer-free. **Design
  correction (2026-08-04):** this is not yet safe to bind to the live fixed
  terrain/Mario callbacks. Those callbacks derive the output/cache lane from
  `begin == 0` and their position/result banks encode a preassigned CPU split.
  Allowing the master to steal a nominal slave range would make the master
  later read its own cached write through the peer alias. Replace that fixed
  lane coupling with descriptor-owned output banks plus an explicit execution
  lane before making the queue the accepted CPU-DUAL callback. No dormant or
  compile-disabled second `cpu_dual_slave_set` path is permitted. **Source
  prerequisite complete (2026-08-04):**
  `saturn_render_output_bank.{h,c}` derives bank identity only from the
  immutable descriptor kind (world = terrain, actor = actor). The successful
  queue release record—not a callback argument—publishes `MASTER` or `SLAVE`
  as the output owner through an atomic, P2-visible release record; consumers
  select their cached/P2 range only from that publication. The focused fixture
  rejects a forged pre-claim publication, races both CPUs, verifies a
  master steal of a nominally slave-offset world job remains master-cached, and
  verifies the peer P2 selection. Six source mutations are rejected. This is
  not live renderer integration and does not move master VDP1 lowering or its
  stable painter order. Behavior/docs commits: `0026a3a1`
  (`feat(saturn): publish descriptor-owned output lanes`) and `f0a3b99c`
  (`fix(saturn): bind output lanes to queue claims`). Independent reviews
  remain open.

- [x] **Step 5: Integrate terrain and actor jobs into one frame queue — SOURCE-COMPLETE, REVIEW REQUIRED**

  Remove separate terrain join, Mario transform join, and Mario classify join
  from the accepted path. Merge only after `all_terminal(generation)`.

- [ ] **Step 6: Preserve old fixed-split worker as a diagnostic build mode — DEFAULT PATH REMOVED; EXPLICIT MODE OPEN**

  The diagnostic mode may compare output but cannot be the production default
  or trigger full-span replay.

- [ ] **Step 7: Run queue/coherency/actor/cluster/runtime gates — ACTIVE**

  Expected: all PASS; a fixture with a deliberately slow slave proves the
  master claims other work rather than spinning.

- [x] **Step 8: Update provenance and live documents**

  Record this scheduler as project-owned pattern-informed code unless exact
  upstream lines were close-ported; cite Z-Treme/SGL persistent consumer and
  SlaveDriver disjoint ownership without claiming their exact scheduler.

- [ ] **Step 9: Commit and complete two-stage review**

  Commit with `perf(saturn): schedule render work opportunistically`.

### Task 5.5: Convert fixed range ownership into descriptor-owned result routing

**Files:**
- Create: `src/port/saturn/gfx/saturn_render_job_bridge.{h,c}`
- Create: `tools/saturn/render_job_bridge_test.c`
- Modify: `src/port/saturn/gfx/saturn_render_job_queue.{h,c}`
- Modify: `src/port/saturn/sourceboot/Makefile`, `Makefile.saturn.mk`
- Modify: architecture, evidence, ledger, plan, and changelog

- [x] **Step 1: Red bridge fixture.** The direct host compile failed for the
  absent bridge header. Its fixture requires an actual master steal of a
  former-slave input range to stay cached, the peer to select the same exact
  job only after `DONE`, a slave actor job to route in reverse, and a wrong
  descriptor index to fail closed.
- [x] **Step 2: Source bridge and one polling owner.** `begin_output()`
  publishes only through the matching descriptor-kind bank and returns the
  queue-validated claimant lane. `read_output()` accepts only the immutable
  descriptor index and requires its terminal `DONE` state before using the
  recorded owner. The queue records a single source-side arm only; it cannot
  activate a slave or report a notification.
- [x] **Step 3: Focused host proof.** The bridge fixture and existing queue
  fixture pass with `-Wall -Wextra -Werror` under the Qt MinGW host compiler.
- [x] **Step 3a: Source-only coexistence proof.** A Python source gate rejects
  either `cpu_dual_slave_set` or `cpu_dual_slave_notify` in A5.5. The live
  cutover owns the only permitted registration after legacy worker removal.
- [x] **Step 4: Independent specification and quality review.**
- [x] **Step 5: Live renderer transition — SOURCE-COMPLETE, CUTOVER REVIEW OPEN.** Replace every terrain/Mario
  producer/read with bridge routing, replace source arming with the queue as the sole slave
  callback, publish terrain/actor jobs, and remove accepted fixed joins only
  after `all_terminal()`. This remains a separate source/target gate; no
  target build or Ymir run is authorized by Steps 1–3 alone.

### Task 5.6: Descriptor-owned payload banks and atomic CPU-DUAL cutover

**Status:** source-complete atomic cutover, host-green and scoped re-review GO
at audit `1819f2b4`; target link/section proof is green after the reviewed
memory-fit repair. Desktop Ymir boots the cutover but automatic measurement
confirms no uplift: sustained 3--4 VDP1 FPS. Audit `8e64b482`
rejected the first cutover because its
single WORLD_ADMIT waited for a nonexistent peer and cross-lane lower could
read stale owner bytes. The repair makes queue transform explicitly
single-producer and rebuilds lower's local owner map from exact DONE admit
claimant metadata; a two-generation poisoned-owner callback fixture passes.

**Target-memory repair (2026-08-04):** the first exact serialized post-cutover
build compiled all sources but failed at link because live callbacks retained
enough formerly GC'd state to overflow HWRAM by 10,032 bytes. The two
master-only 13,872-byte terrain final-merge streams now reside in
`.lwram_bss`; a focused source contract preserves that placement and tracks
the accepted graph/runtime public calls rather than obsolete direct queue
calls. Host RED/GREEN is complete. Fresh review and one post-repair serialized
link were required. Independent review is GO at `b997fea1`; the one
post-review rebuild exits 0 and packages a fresh CUE. Linked HWRAM margin is
`0x3f14` (16,148 bytes), LWRAM margin is `0x7850` (30,800 bytes), all four
callbacks plus master/slave graph drains are live, and exactly one non-null
application CPU-DUAL registration targets `render_job_slave_entry`. Target
compile/link/section evidence and desktop FPS observation are complete;
per-phase claim ownership and terminal waits remain unchecked.

**Manual cutover result (2026-08-04):** the owner launched the fresh reviewed
atomic-cutover CUE in desktop Ymir and still observed roughly 3–4 FPS, with no
visible uplift over the A3+A4 candidate. The source/link cutover is therefore
not performance-complete. Before adding recovery or transfer overlap, record
per-CPU claim counts, phase ownership, slave-retirement timing, and terminal
waits to determine whether four coarse dependency-chained jobs leave the
slave idle or merely move the same serial critical path between CPUs.

- [x] **Step 1: Record the live seam and red gate.** The current frame still
  calls the fixed terrain dispatcher and chained Mario dispatcher. The new
  test_render_job_live_cutover_source.py fails before production changes:
  the renderer has no bridge include, queue publish/drain/terminal boundary,
  or queue runtime lifecycle.
- [x] **Step 2: Make terrain and actor payload ownership descriptor-indexed.**
  Replace physical master/slave result arrays and fixed actor owner reads with
  output-bank slots selected by exact queue descriptor identity; readers must
  reject non-DONE output and choose cached/P2 only from the recorded claimant.
  The TDD-covered physical payload-bank helper derives a writer address from
  the bridge execution record and a reader address from exact DONE metadata;
  the atomic source cutover now routes both terrain and actor call sites
  through it.
- [x] **Step 3: Add the one-owner queue CPU-DUAL runtime.** Bind exactly one
  polling callback only after default legacy dispatch is gone; persistent slave
  drain and master drain share the same local callback table and immutable
  renderer context.
- [x] **Step 4: Atomically switch the accepted frame path.** Publish terrain
  and actor descriptors, drain opportunistically, and merge only after
  all_terminal(generation). Preserve master-only final VDP1 lowering and
  painter order. The fixed worker may remain only behind an isolated explicit
  diagnostic configuration.
  The cutover uses one four-job graph, publishes callback contexts before
  notify, positively observes the slave polling entry return, validates both
  terminal assemblies before mutating the command backend, and never replays
  a failed generation serially. Pre-notify publication failures quarantine
  and retire their unclaimed descriptors so later frames are not poisoned.
- [ ] **Step 5: Green source gates and two independent reviews.** Run bridge,
  queue, coherency, actor/cluster/runtime gates plus the live-cutover static
  guard. A target build/Ymir run remains prohibited until this source scope
  receives both reviews.
  First NO-GO/re-review loop is closed: repair `1667958e` received scoped GO
  at audit `1819f2b4`, with the new handoff fixture and prior nine gates
  independently green. A separate second quality review remains unchecked if
  required before promotion; this scoped verdict authorizes only the next
  serialized target-build gate, never a Ymir/FPS claim by itself.

### Task 5.9: Observe coarse-graph claim ownership before rescheduling

**Status:** source-complete, host-green, and repair rereview GO at `e98210ba`.
The serialized Route-0/live-input/Pipe4 target build is green with the
append-only profile/HUD telemetry linked and memory-fit; a fresh CUE exists.
No scheduling-policy change, Ymir HUD observation, or FPS claim is included
yet.

- [x] **Step 1: Write the delayed-slave schedule RED.** The host fixture now
  models the live four-job graph and its exact notify → immediate master drain
  → slave-retirement ordering. Before production changes it failed to compile
  because no per-phase/runtime telemetry contract existed.
- [x] **Step 2: Add bounded P2-safe runtime telemetry.** One uncached runtime
  record distinguishes master and slave claims for WORLD_ADMIT, WORLD_LOWER,
  ACTOR_ADMIT, and ACTOR_LOWER; records exact notified/retired generation and
  sequence; and reports callback failure and terminal quarantine. The master
  counts its existing retirement loop locally and publishes once after the
  loop, avoiding shared writes or calls inside the hot wait.
- [x] **Step 3: Expose the last completed generation.** Append-only frontend
  profile fields feed a three-line VDP2 diagnostic: `QM` and `QS` use phase
  order world admit/lower, actor admit/lower; `QN/QR/QW/QF/QQ` report notify,
  retire, wait, failure, and quarantine respectively.
- [x] **Step 4: Prove the diagnostic distinguishes idle from failure.** A
  delayed slave permits the master to consume all four jobs (`QM 1/1/1/1`,
  `QS 0/0/0/0`). A second generation fails WORLD_ADMIT, counts the one failed
  slave callback, quarantines dependent WORLD_LOWER, and still completes the
  independent actor chain.
- [x] **Step 5: Independent source review.** Review P2 ownership, counter
  races, profile ABI append-only placement, and bounded HUD cost. Keep this
  unchecked until a fresh reviewer records a verdict. First review found a
  release race: `retired_sequence` became visible before retired telemetry.
  Watched RED/GREEN now requires telemetry publication before the final
  positive retirement marker in both SH-2 and host paths. Fresh rereview is
  GO at audit `e98210ba`; strict runtime/VDP2 C11 and focused Python gates
  independently pass.
- [x] **Step 6: Serialized target build and automatic desktop-Ymir
  observation.** The guarded `-B -j1` build exits 0, retains the runtime
  telemetry snapshot and VDP2 HUD consumers, has zero unresolved symbols, and
  leaves 15,412 bytes HWRAM plus 30,800 bytes LWRAM. The owner then confirmed
  the candidate still runs at roughly 3--4 VDP1 FPS, but the queue HUD was not
  transcribed. The active correction is a host-tested desktop capture command
  that launches the exact profile/CUE, samples Ymir's native VDP1/VDP2/draw
  counters without OCR, and records bounded machine-readable evidence. Where
  feasible it must also collect `QM/QS/QN/QR/QW/QF/QQ`; otherwise that target
  telemetry remains explicitly open rather than inferred. These observations
  choose the next scheduler repair; no optimization is guessed from the host
  schedule alone.
  Automatic FPS half complete: 14 focused host tests and module compilation
  pass. The exact desktop command captured ten running-counter snapshots after warmup:
  VDP2 median 60 FPS and VDP1 median 4 FPS (range 3--4), with the emulator left
  alive. Queue values remain unchecked. First review
  was NO-GO on evidence semantics and failure durability. The repair timestamps
  snapshots without claiming distinct rollovers, preserves fractional medians,
  writes structured failure JSON, checks process liveness, and pins the Win32
  ABI. Rereview is GO; failed reports also retain stage, PID when launched, and
  adjacent log paths. Queue values remain open. **Queue-observation correction
  (2026-08-05):** `capture_sourceboot_throughput.py` is source-complete and
  host-green. It binds the matching CUE/ELF/Ymir identities, verifies an ELF
  code window in target memory, resolves exact local/underscore symbols
  in-process, and accepts P2 samples only after coherent retirement while
  advancing one VBlank at a time. This is not target evidence: no valid live
  report has been captured, so the step and queue gate remain unchecked.
  First review's four findings and the scoped rereview's remaining identity
  defect are now repaired. The identity probe requires each candidate section
  to share the selected `PT_LOAD`'s exact affine file-to-memory mapping
  (`section.address - segment.vaddr == section.offset - segment.offset`),
  pinned by a malformed-ELF regression fixture whose ranges otherwise fit.
  Thirteen capture tests and sixteen boot-trace tests are host-green. Scoped
  rereview is spec PASS / quality APPROVED and independently repeats those
  tests plus module compilation. Source and review are complete; a valid live
  report remains required, so the step and queue evidence gate stay unchecked.
  The first live attempt used a 120-byte evidence blob instead of a 512-KiB
  IPL and is discarded. With the correct established IPL, the capture reached
  the target-identity gate before sourceboot had loaded. A bounded boot-trace
  diagnostic proves exact ELF `main` bytes first appear at post-BIOS +570
  VBlanks and sourceboot trace magic at +600. The collector now waits for
  identity one VBlank at a time within separate `--startup-vblanks` bound
  (default 600), reports its exact wait/attempt count, and fails closed without
  a telemetry read if identity never appears. Seventeen capture tests and
  sixteen boot-trace tests are host-green. Root and independent reviewer each
  repeat those tests plus module compilation; scoped rereview is spec PASS /
  quality APPROVED. This resolves only startup timing; valid live observation
  remains the Step 6/queue-evidence gate. The first repaired live run proves
  exact target identity after 540 startup VBlanks but then observes no
  presentations because the chosen `build-agent` Ymir executable predates
  headless `--dram-cart` support. Target probe `SCAR` reports stage FAILED,
  `cart_id=0`, `cart_size=0`, status `MISSING_4MIB`, and the linked PC remains
  in `main`'s cart-failure loop. A newer existing `build-agent2` executable was
  built after commit `bf3e4a4a` added the 32-Mbit cart flag; it is the next
  live candidate, without rebuilding either target or emulator. That newer
  binary leaves the failure loop and logs sustained `SOURCE.DAT` sector reads;
  the 600-VBlank observation bound expires while the 3.2-MiB cart bank is
  still copying. The next diagnostic changes only the existing observation
  bound to its 4096-VBlank maximum so startup I/O can finish before requiring
  two presentation edges. The bounded run then passes: exact ELF identity
  matches after 540 startup VBlanks; three presentation edges occur by sample
  1185; cadence is 4.8 FPS mean, 4.87 median, and 4.29 1%-low. Coherent retired
  sequence 1 reports `QN=QR=3`, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and
  `QW=QF=QQ=0`, with zero CPU failures. The automatic A5.9 observation gate
  is complete. This rules out an idle slave and retirement waiting for the
  observed frame, while leaving unequal job cost and master-only final
  merge/VDP1 lowering as active hypotheses.

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
- Modify: `src/port/saturn/gfx/saturn_demo_render.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Create: `tools/saturn/vdp1_frame_bank_test.c`
- Create: `tools/saturn/gouraud_transfer_test.c`
- Modify: `tools/saturn/test_verify_sourceboot_memory_map.py`
- Modify: `tools/saturn/dual_actor_worker_test.c`
- Modify: `tools/saturn/render_job_live_cutover_source_test.c`
- Modify: `tools/saturn/test_render_cluster_generation.py`
- Modify: `tools/saturn/test_sourceboot_presentation_boundary.py`
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

  **A7/A8 interface correction (2026-08-05):** the four original functions
  cannot truthfully express `TRANSFERRING` or transfer retirement because the
  current emitters wait internally and return `void`. A7 therefore also adds
  explicit begin/retire transfer metadata plus a synchronous-complete adapter
  called only after today's renderer returns. A zero-count Gouraud prefix is a
  satisfied `NOOP` obligation, not an invalid/missing ticket. A7 does not move
  submission or polling out of the emitters; A8 replaces the synchronous
  adapter with real asynchronous queue tickets and polling.

  **Render-outcome correction (2026-08-05):** the demo renderer's former
  `void` boundary has pre-emission failure returns. It now returns an explicit
  success outcome. Sourceboot may call `ready` only after success; failure
  quarantines the incomplete build and preserves the previously published
  bank. Profile serial counters are diagnostics and never substitute for this
  outcome.

- [x] **Step 1: Write bank lifecycle and wrong-region tests** — red

  Assert build/ready/transfer/publish/reuse order, ticket retirement, stale
  generation rejection, command-source LWRAM classification, Gouraud-source
  HWRAM classification, and quarantine exclusion.

- [x] **Step 2: Strengthen the memory-map test** — red

  Require `.lwram_cmdts` size exactly
  `2 * 2048 * sizeof(vdp1_cmdt_t)`, 32-byte alignment, command banks outside
  HWRAM, Gouraud staging inside HWRAM, and linker margin at least `0x1B00`.

- [x] **Step 3: Add `verify-vdp1-frame-bank` and record red evidence** — red

  Expected: missing bank manager and ownership metadata.

- [x] **Step 4: Implement the state/ticket manager** — host-green

  Bind the existing backend and Gouraud structures through one bank object.
  `begin_build` succeeds only for `FREE`; `publish` succeeds only after worker
  and both transfer obligations retire, including the explicit zero-Gouraud
  `NOOP` state described above.

- [x] **Step 5: Replace ad-hoc XOR and misleading generation variables** — source-complete

  Remove `sourceboot_vdp1_cmdts_bank ^= 1U` as the ownership decision. Select a
  bank through `begin_build`, retain the previous published bank when none is
  free, and expose accurate build/published/displayed framebuffer generations.

- [x] **Step 6: Run bank, memory-map, DMA queue, command-template, and runtime gates** — focused-green; broad census open

  `verify-vdp1-frame-bank`, the 10 memory-map tests, DMA queue, terrain command
  template, runtime contracts, presentation-boundary, and live-cutover focused
  gates pass. The linked ELF proves `.lwram_cmdts` is a `0x20000`-byte NOBITS
  section at `0x00200000`, Gouraud staging is exactly `0x6000` bytes at
  `0x060D8FB8`, and `___end=0x060FC86C` leaves `0x3794` HWRAM bytes. The broad
  The first integration attempt stopped on the then-pre-existing native-math
  census edge `_play_cutscene -> _cutscene_bbh_death`; the final exact audited
  `make -B -j1` later exits zero. The updated dual-actor structural gate still stops
  earlier on its pre-existing live-pointer helper, and the full cluster suite
  still requires absent generated Mario LOD symbols; the A7-specific live
  renderer signature/merge and generation-slice checks pass independently.

- [x] **Step 7: Update documents, commit, and complete two-stage review** — complete

  Implementation and synchronized behavior documents are committed at
  `650b911a` (`feat(saturn): track VDP1 source-bank lifetimes`). Independent
  Consolidated repair commit `41a4ce7e` and documentation commit `deebd06e`
  are independently PASS/APPROVED.

  **Consolidated review repair (2026-08-05): source-green; target integration green; rereview pending.** Publication rejects
  a completed generation that is not wrap-safely newer than the current
  publication; the rejected late bank is quarantined. Both the demo and normal
  emitters must return false when Gouraud submission fails again after the one
  bounded drain/retry, so sourceboot cannot publish an incomplete image.
  Manager initialization must reject misaligned, aliased, or overlapping
  command and Gouraud storage. Direct and mutation-resistant fixtures precede
  the repair. The direct bank/transfer fixtures, two source-mutation checks,
  6/6 presentation tests, 10/10 memory-map tests, and the live-cutover source
  gate pass. One serialized Pipe4 build compiled and linked fresh ELF
  `b0ede6f9...190401cc`; broad verification then reached the unchanged unrelated
  native-math oracle error. Repair rereview is the remaining A7 gate.
  The final object-pointer overlap/alignment strengthening was host-compiled
  after that single target run; it was not target-rebuilt because the task cap
  permitted at most one serialized target validation in that repair turn. The
  root's final audited clean rebuild subsequently closes the exact target gate:
  exit 0 in 336.9 seconds, ELF SHA-256
  `1eba88885b611f0c99dea3971dda871fcc30fdb8ac21c4fcfc5e051f0e99267c`.
  Consolidated repair commit: `41a4ce7e`
  (`fix(saturn): fail closed on stale VDP1 banks`).

### Task 8: Transfer command and Gouraud banks without immediate waits

**Status:** source-, target-, and runtime-publication complete; cadence active. Repairs are
committed at `8b037a7d`; independent contract and quality rereviews are
PASS/APPROVED. The exact Route0/live-input/Pipe4 build exits zero after the
target-only Yaul integer-address conversion repair and produces ELF SHA-256
`5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.
The failed first automatic run and green repaired run are both retained. The
repair publishes terrain-only when actor admission is empty. It advances both
VDP1/VDP2 generations with no transfer/bank faults or measured waits, but the
first one-interval cadence is only 1.62 FPS; longer automatic and manual FPS
evidence remain unchecked.

**A8 design correction (2026-08-05):** the final command and Gouraud
destinations remain single VDP1-VRAM ranges in A8, so no transfer may begin
until the previous displayed/plotting list has crossed `vdp1_sync_wait()`.
A8 therefore overlaps source-bank construction with the prior VDP1 plot and
makes both post-boundary submissions wait-free; it does not claim that the
VRAM writes themselves overlap the prior plot. True frame-over-frame transfer
overlap requires destination ownership/banking and remains A9 scope.
The first implementation's `cpu_dmac_status_get().channel_busy` guard was
rejected: pinned Yaul does not reliably report DE=1/TE=0 as busy. The repair
must configure/start queue-owned channel 0 through public APIs and retire only
from a queue-owned completion callback; `cpu_dmac_transfer()` is not accepted
as a nonblocking primitive.
The frame queue exclusively owns CPU-DMAC channel 0 after
`saturn_dma_queue_init()`; all boot-time channel-0 work must finish before that
handoff, and source gates reject other frame-loop owners. The overwrite-safe
gate is explicit and precedes submission, not publication, because submission
is the first VRAM mutation. Command and Gouraud descriptors are reserved and
committed atomically so a failed second descriptor cannot leave the first
queued against a quarantined/reusable bank. A8 intentionally retains
SlaveDriver's serial bounded FIFO—CPU-DMAC command retirement can be observed
before SCU-DMA Gouraud retirement, but the transports do not run concurrently.
The demo and normal/full-game emitters both become construction-only; the
shared bank transport owns every frame upload.

**Files:**
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.h`
- Modify: `src/port/saturn/gpl/slavedriver_dma_queue.c`
- Modify: `src/port/saturn/gfx/saturn_vdp1_backend.h`
- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.h`
- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.c`
- Create: `src/port/saturn/gfx/saturn_vdp2_camera_snapshot.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c`
- Modify: `src/port/saturn/gfx/saturn_gouraud_transfer.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_vdp2_frame.c`
- Modify: `tools/saturn/dma_queue_test.c`
- Create: `tools/saturn/vdp1_transfer_pipeline_test.c`
- Create: `tools/saturn/test_vdp1_transfer_pipeline_source.py`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Modify: `tools/saturn/test_tools.py`
- Modify: `Makefile.saturn.mk`
- Modify: `docs/saturn/SLAVEDRIVER_ADAPTATION.md`
- Modify: architecture spec, this plan, and evidence report

**Interfaces:**
- Adds queue mode `SATURN_DMA_QUEUE_CPU_DMAC` for LWRAM command sources and
  retains `SATURN_DMA_QUEUE_SCU` for valid HWRAM Gouraud sources.
- Produces `sm64_saturn_vdp1_frame_bank_submit_transfers()` and
  `sm64_saturn_vdp1_frame_bank_poll_transfers()`; submit returns immediately.
- The accepted sourceboot path does not block on a transport ticket: it
  services the serial lane during ordinary and stale-loop iterations and
  publishes only after later positive completion. The retained blocking helper
  is compatibility/exceptional-recovery API, not ordinary publication.

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

- [x] **Step 1: Write red transfer-selection tests**

  Mock CPU-DMAC and SCU DMA separately. Assert LWRAM commands select CPU-DMAC,
  HWRAM Gouraud selects SCU DMA, LWRAM+SCU is rejected, both tickets are needed
  before publication, and submit performs zero waits.

- [x] **Step 2: Add source anti-pattern checks**

  Reject `vdp1_sync_wait(); saturn_dma_queue_kick();
  saturn_dma_queue_wait(...)` inside `sm64_saturn_demo_render_frame()` and
  reject direct `sm64_saturn_vdp1_backend_upload()` in its accepted pipeline.

- [x] **Step 3: Add `verify-vdp1-transfer-pipeline` and record red evidence**

  Expected: current immediate-wait sequence and CPU-copy upload are detected.

- [x] **Step 4: Add CPU-DMAC queue transport using pinned Yaul APIs**

  Adapt the public channel configuration lifecycle from pinned Yaul
  `libyaul/scu/bus/cpu/cpu_dmac.c` without copying internal implementation.
  At the post-boot ownership handoff, stop/reset queue-owned channel 0; later
  configure/start it with `cpu_dmac_channel_config_set()` and
  `cpu_dmac_channel_start()`. Retire only from the configured completion IHR.
  Pinned `cpu_dmac_status_get().channel_busy` is explicitly non-authoritative
  for DE=1/TE=0 and is used only for terminal error flags.

- [x] **Step 5: Submit both transfers after bank construction**

  Finish command links, publish CPU cache writes, submit command CPU-DMAC and
  Gouraud SCU DMA, return from render construction, and poll tickets from the
  main loop. Remove the disabled `#if 0 && ...` upload branch.

- [x] **Step 6: Publish only after transfer and VDP1 list safety**

  Wait for old VDP1 list ownership only at the actual VRAM overwrite/publish
  boundary. Never plot old metadata after either single resident range may be
  partially overwritten: transfer failure poisons the destination and disables
  plotting until a future explicit restore. Poll/kick during stale iterations
  so the serial CPU/SCU stages are not quantized to fields, while publication
  stays VBlank-owned. Capture camera state into the immutable frame bank and
  coalesce VDP2 from the exact bank published to VDP1.

- [x] **Step 7: Measure the real waits**

  Add distinct counters for command CPU-DMAC wait, Gouraud SCU-DMA wait,
  VDP1-list overwrite wait, bank-reuse wait, and terminal fence. Update HUD and
  decoder so `VDP1W` no longer samples only later nonblocking calls. The
  ordinary accepted path explicitly reports zero command/Gouraud and terminal
  wait ticks because it does not wait there; QNS increments once only when the
  post-kick exact command ticket is neither started nor retired.

- [x] **Independent-review repair checklist (source/host)**

  Callback-only CPU-DMAC retirement and false-idle regression; stale-loop
  transport progression; fail-closed partial-write poison; immutable
  VDP1/VDP2 camera-generation snapshot; truthful wait/QNS telemetry; full
  command/Gouraud capacity bounds and 8-byte Gouraud destination alignment.

- [x] **Step 8: Run DMA, transfer, bank, VDP2, memory-map, and runtime gates**
  — repaired strict DMA, transfer, bank, VDP2, runtime-contract, presentation,
  memory-map (10/10), and profile layout/decode (21 tests, one historical
  capture skip) gates are green. The first target attempt correctly failed at
  `main.c:906` because host mocks represented `VDP1_VRAM(0)` as a pointer while
  pinned Yaul exposes an integer address. A RED-then-green source contract now
  requires the explicit `(void *)(uintptr_t)` conversion. The exact serialized
  incremental Pipe4 target build exits zero; ELF SHA-256 is
  `5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.

  Expected: all PASS; submit-before-wait ordering and transport selection are
  enforced by mutation tests.

- [x] **Step 9: Update provenance/live documents, commit, and review**

  Correct `SLAVEDRIVER_ADAPTATION.md` to state that upstream active DMA is
  serial and this asynchronous lifecycle is project hardening. Initial commit
  `862c7f7c` received NO-GO; callback/fail-closed repair commit `8b037a7d` is
  complete. Independent contract rereview is PASS/APPROVED. Independent
  quality rereview is PASS/APPROVED after `bf160e53` corrected the interrupt
  ownership text. Runtime Ymir/FPS evidence is deliberately a separate,
  still-open acceptance gate rather than part of source review.

- [x] **Step 10: Pass automatic Ymir publication and cadence — COMPLETE;
  NO A8 UPLIFT**

  The first exact capture is retained at
  `docs/saturn/evidence/reports/a8-deferred-transfer-throughput-2026-08-05.json`.
  Target identity matches, but the observation fails closed with fewer than
  two presentation events across 4,096 VBlanks. Direct P2 reads show BOB ready,
  the render runtime active, a valid 424-vertex Mario pose, zero admitted actor
  transform references, `frame_serial=0`, `pipeline_faults=4`, no DMA tickets
  or transfer faults, and both source banks quarantined. Make zero visible
  actor meshlets a valid scene-neutral terrain-only/no-op actor frame, retain
  fail-closed behavior for actual errors, then repeat this exact capture.

  The repair makes the terrain dependency chain the first two descriptors and
  appends actor admit/lower only when actor positions exist. Its exact
  incremental target build exits zero in 64.4 seconds; ELF SHA-256 is
  `b246b39c1137a4374ced8ae7b3d276436f5633ae9194df390e1df2dde7a88bdd`.
  The repeat capture at
  `docs/saturn/evidence/reports/a8-deferred-transfer-throughput-repair-2026-08-05.json`
  passes with two presentation events and no queue failures. It measures one
  37-field interval (1.62 FPS). Direct profile reads after three rendered
  frames show `pipeline_faults=0`, zero transfer/bank/wait counters, retired
  DMA (`head=tail=6`), and submitted/displayed generation 1047 while build
  generation 1084 is ready for the next transfer. Require a longer event series
  before accepting or rejecting A8's cadence effect.

  Post-repair direct executable fixtures are green for render-job runtime,
  actor meshlets, DMA queue, and VDP1 transfer pipeline. The aggregate Make
  wrapper remains red before test execution because MSYS rewrites the Windows
  worktree path to `\\d\\Code\\...`; this is recorded as an infrastructure
  gate failure, not substituted for or counted as a test result.

  Independent review rejected the first terrain-only repair because the old
  count return conflated successful zero admission with actor-preparation
  failure. A watched RED/GREEN contract now requires boolean success plus an
  output count; only success with count zero selects the two-job graph, while
  invalid pose/meshlet preparation fails before graph publication. Independent
  rereview is PASS with no Important issue. The exact serialized rebuild exits
  zero with ELF SHA-256
  `10e92064175f1d277039322f6be874f646b71a486c7f18ccd8a2e9786df569ab`.
  The configurable-depth capture tool is 22/22 tests green. Its exact ten-event
  report at
  `docs/saturn/evidence/reports/a8-deferred-transfer-throughput-long-2026-08-05.json`
  records nine 36--38-field intervals: 1.63 FPS mean, 1.62 median, 1.58 1%-low,
  `QN=QR=10`, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and `QW=QF=QQ=0`. A8 has no
  measured uplift; use construction/simulation timing to scope A9.

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

- [x] **Step 0: Attribute the measured post-A8 frame interval — COMPLETE**

  Extend exact-identity automatic evidence to sample existing simulation and
  render/construction timing at each presentation edge, using wrap-safe deltas
  and no target scheduling change. If current profile publication cannot
  support that split, add the smallest diagnostic-only counters first. Record
  the phase split before implementing scheduler policy; A8's stable 36--38
  fields and zero transfer waits are the baseline.

  **Design correction (2026-08-05):** the existing simulation/render FRT
  accumulators add modulo-16-bit phase deltas and can lose multiple wraps at
  the measured 1.6 FPS; the frontend profile also lacks an explicit
  cache-through publication boundary. They are gauges, not evidence-grade
  absolute attribution. Add one fixed-size P2-published cadence trace with
  wrap-safe VBlank-crossing accumulators and phase counts, then sample it at
  the same exact presentation edges. The current evidence already localizes
  the interval: each presentation generation trails its pre-render bank stamp
  by 36--38 fields while transfer waits remain zero.

  Exact ELF `1ffb47cc...e4edfe6` and ten-edge report
  `docs/saturn/evidence/reports/a9-phase-attribution-throughput-2026-08-05.json`
  account for every one of 333 interval fields: simulation 283 (85.0%), frame
  construction 49 (14.7%), transport/presentation 1 (0.3%), unattributed 0.
  Every presented frame spans six authoritative simulation ticks and the nine
  intervals drop another 222 VBlank credits. The existing two-tick bound is
  per outer iteration, not per presentation generation; repeated outer
  iterations recreate the death spiral before one frame can publish.

- [ ] **Step 0.1: Materialize terrain commands in `WORLD_LOWER` — DEFERRED;
  CONSTRUCTION IS 14.7%**

  Use the descriptor-owned 32-byte command image to copy and XY-patch the
  resolved template in the worker, after publishing the immutable template
  bank for P2/cache-through reads. The master retains stable painter merge,
  final-bank copy/link/END, bounded Gouraud allocation/GRDA patching, and actor
  tail ownership. Preserve the current fallback for malformed or unresolved
  templates. This is pattern-only from libyaul
  `6012f79f237773378c8014e70d8998ad95a38d98` (`libmic3d/render.c:381-430,
  525-568,920-952`) and the already-attributed SlaveDriver
  `a8986591557b6e680550d3c23970284d3b38ff8f` worker-result ownership shape
  (`WALLS.C:1240-1408,1803-1950`); no upstream code is copied. Do not combine
  this bounded experiment with direct alternating-bank writes.

- [x] **Step 1: Write a deterministic scheduler model test — COMPLETE;
  FINAL REREVIEW GO**

  Feed synthetic VBlank/render/transfer completion events. Assert snapshot N
  presentation while simulation is N+1, no mixed generations, useful job
  service before terminal wait, previous-frame reuse on missed deadline, and
  no four-tick burst after a slow render.

- [x] **Step 2: Add mutation coverage for the death spiral — SOURCE COMPLETE;
  REVIEWED FOR ORDINARY GENERATIONS**

  Reject `SOURCEBOOT_MAX_SIM_CATCHUP 4U`, catch-up loops that add VBlank credit
  while executing a tick, and publication of non-complete banks.

- [x] **Step 3: Add `verify-frame-pipeline` and record red evidence — COMPLETE;
  REREVIEW GO**

  Expected: missing scheduler and existing catch-up mutation failure.

- [x] **Step 4: Implement the pure scheduler first — COMPLETE; FINAL REREVIEW
  GO**

  Keep it hardware-free and drive all state transitions through explicit
  events. Increment dropped-credit and previous-frame-reuse counters rather
  than silently hiding overload.

  RED began with the missing module, then caught repeated reuse reopening the
  two-tick budget, same-field post-reuse service loops, and a queued N+1
  snapshot not being charged to the next lifetime. GREEN keeps the budget
  across reuse, resets it only on complete publication, carries an already
  consumed queued tick, and permits at most one SERVICE/POLL opportunity per
  observed VBlank. The normal fixture passes; four-tick, repeated-credit, and
  incomplete-publication mutants are rejected. The aggregate native Make run
  compiled successfully but its `/usr/bin/sh` executable handoff hit the known
  quoting defect; the four produced executables were run directly and passed
  their expected normal/failure contracts. Independent review is NO-GO before
  Step 5: generation `UINT32_MAX -> 0` collides with zero-valued unset
  sentinels and can make incomplete work publishable, while publish/promote
  currently resets generation-local SERVICE/POLL flags and permits more work
  in the same VBlank. Adapter design also found that `PUBLISH_FRAME` currently
  commits scheduler state before the fallible runtime arm/publish/retire/VDP
  sequence succeeds. The repair must add wrap-safe validity state,
  field-scoped service/poll epochs, and exact-generation publish completion
  acknowledgement; cover all three defects in RED/GREEN tests; and include
  `verify-frame-pipeline` in `verify-all`.

  Repair RED/GREEN now covers active and queued generation-zero wrap, forbids
  same-field SERVICE/POLL after publish/promote, and makes `PUBLISH_FRAME` an
  intent whose exact generation must be acknowledged success or failure.
  Failed or absent acknowledgement retains the prior display and cadence
  budget. Fresh direct compilation/run is GREEN for the nominal fixture and
  catches all three mutations; `verify-all` now includes the gate. The native
  Make recipe still hits the known `/usr/bin/sh` quoted-Windows-executable
  handoff on this host after compilation, so direct executable evidence is
  retained and the wrapper issue is not misreported as a model failure.

- [x] **Step 5: Integrate the outer sourceboot loop — SOURCE COMPLETE;
  30/30 GREEN; FINAL REREVIEW GO**

  Advance authoritative simulation, publish immutable snapshot, notify the
  persistent slave, let the master claim work after its sim phase, poll DMA,
  and publish only complete frame banks. Replace the current catch-up loop and
  direct same-iteration render call.

  A new RED-only integration contract currently fails 7/7 against the legacy
  loop. It requires one six-action dispatcher; exact-generation tick, render,
  transfer, and publish helpers; publish acknowledgement only after target
  bank publication; immutable previous-frame reuse; WAIT-only VBlank blocking;
  and scheduler-owned dropped-credit telemetry.

  Steps 1--4 and the Step 5 RED contract are checkpointed at `d49b8677` after
  independent rereview GO. The Step 5 compatibility adapter is now delegated
  for GREEN implementation; target build and uplift evidence remain unchecked.

  The compatibility adapter and reconciled A8/presentation/cadence/boot/source
  contracts are now GREEN 29/29. Pre-target review then found the pure model
  converts every elapsed VBlank directly into simulation credit, which would
  run healthy SM64 logic at 60 Hz instead of the required one tick per two
  fields. Target build is blocked while a fractional two-field accumulator is
  added inside the scheduler; SERVICE/POLL/presentation must remain field-rate.

  The 30 Hz repair is healthy, but combined review remains NO-GO on two focused
  seams. Successful target publication currently commits VDP2/cadence evidence
  before scheduler acknowledgement refreshes displayed generation and credit
  state; required order is target publish, exact success acknowledgement,
  telemetry refresh/presentation, then cadence append. The scheduler also
  admits wrapped generation zero while render snapshots and VDP1 banks reject
  zero. Repair uses one documented nonzero generation policy across scheduler
  and sourceboot rather than widening every downstream ownership API.

  Both repairs are source-complete. Scheduler and sourceboot now share one
  `UINT32_MAX -> 1` successor helper, keeping zero reserved. Successful publish
  order is target bank publish, exact acknowledgement with the actual result,
  telemetry refresh, terminal presentation, then cadence append; failure
  refreshes telemetry but cannot present or append. The combined focused suite
  is GREEN 30/30 plus nominal scheduler PASS and three caught mutations.
  Final combined rereview is GO for the planned serial target build. Legacy
  `vblank_credit` telemetry identifiers are retained for compatibility, but A9
  values now count whole discarded 30 Hz simulation-tick credits rather than
  raw fields; capture comparisons must use that documented unit change.

- [x] **Step 6: Verify generation-coherent VDP2 composition — SOURCE COMPLETE; FIX ROUND 2 RECONCILED**

  Commit `2377bf8b` makes VDP2 consume only the immutable camera from the
  displayed VDP1 bank plus one displayed/rendered/simulation tuple, label it
  in HUD output, force its identity refresh with the sky, and reject invalid
  or mismatched ownership before callbacks. Direct VDP2/runtime tests and
  sourceboot boundary mutations passed. Fix Round 2 restores the ledger at a
  paragraph boundary and makes the displayed-zero fixture otherwise coherent
  (`snapshot == displayed == rendered == 0`), isolating the reserved-zero
  guard; simulation-zero remains isolated. Scoped Fix Round 2 rereview is
  **PASS / APPROVED** with every prior finding addressed, so Step 6 closes as
  source-complete. Target capture, manual Ymir, and the broad native-math gate
  remain explicitly unchecked.

- [ ] **Step 7: Run frame-pipeline, snapshot, queue, recovery, transfer, VDP2, runtime, and replay-host gates — ACTIVE (TARGET BUILD FIRST)**

  Expected: all PASS with explicit one-generation presentation lag.

  The serialized forced target build passes in 331 seconds through
  `with-msys-toolchain.ps1`, producing exact ELF
  `6685d058...6073f689` (8,694,212 bytes), ISO
  `7fbb6427...ab1834b`, and CUE `cdbf0bfa...f46dba7`. The broader `make verify`
  rebuild reaches and passes its narrow source tests but remains unchecked: it
  exits on the pre-existing native-math census error `INDIRECT_EDGE has
  unreachable dispatcher: _play_cutscene -> _cutscene_bbh_death`. Do not
  substitute the target compile for that broader gate. Automated cadence
  capture is active against the exact ELF.

  The exact corrected capture completes ten edges with no queue wait/fault
  (`QN=QR=10`, `QW=QF=QQ=0`) and one simulation tick per edge. Field deltas are
  `13,12,13,13,14,14,14,14,14`: 4.463 FPS mean and 4.286 median/1%-low versus
  Step 0's 1.622 mean, a 2.752x / +175% uplift. The capture summarizer now uses
  coherent cadence `observed_vblank_generation` for ISR-field time while
  retaining source presentation-generation coherence separately; 29/29 tests
  pass. Independent measurement rereview is GO.
  Independent measurement rereview is GO: report identity/hashes match disk,
  all ten events are generation-coherent, the 121 total fields yield the stated
  4.4628-FPS mean, and legacy fallback remains intact. Explicit mixed-clock and
  cadence/event-mismatch negative tests pass.

- [x] **Step 8: Update documents, commit, and complete two-stage review — COMPLETE**

  Commit with `perf(saturn): overlap render snapshots with simulation`.

  Landed as `36f4fe58` (`perf(saturn): bound simulation per presented frame`)
  after scheduler, adapter, combined-tree, and measurement rereviews all GO.
  The implementation deliberately remains a synchronous-render compatibility
  adapter; full construction overlap is deferred. Broad native-math census and
  manual Ymir gates remain explicitly unchecked.

- [ ] **Step 9: Build one serial integrated experimental CUE and manually test — BUILD/CAPTURE GREEN; MANUAL YMIR TEST NEXT**

  Use the same BOB/live-input/Q16-camera role as Task 1. Record hashes,
  controls, visible geometry, fault counters, displayed/rendered/simulation
  generations, and qualitative speed. This experimental CUE still does not
  require strict native-math closure.

  Desktop Ymir launch is active at PID 34856 through the profile-backed helper.
  The exact CUE/ISO identities match the capture, the project profile declares
  a 32-Mbit DRAM cart, and the GUI remained live/responding after the bounded
  20-second monitor. Launch report:
  `docs/saturn/evidence/reports/a9-step5-desktop-launch-2026-08-05.json`.
  Owner-visible speed/controls/geometry observations remain unchecked.

### Task 9A: Implement true frame-lifetime overlap before hardening

**Status:** Independent review of `0350a473..d45c0a41` and Fix Round 1 review
of `d45c0a41..420b6ce8` were FAIL/NO-GO. Fix Round 2 closes the remaining C1,
I1, and I3 source findings and is focused-host/source-green; fresh specification
and quality rereview are next. No target build, capture, or FPS evidence exists
yet, and Task 9A is not source-complete before those reviews.
The original RED/implementation checkpoints are `ec81ddc6` and `0f5ccd65`.
Task 10 is hardening/publication and scene-neutral coverage, not the next
expected FPS lever.

**Files:**
- Modify: `src/port/saturn/gfx/saturn_demo_render.h`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify only if the existing action model cannot express pending service:
  `src/port/saturn/runtime/saturn_frame_pipeline.h`
- Modify only with matching model RED/GREEN evidence:
  `src/port/saturn/runtime/saturn_frame_pipeline.c`
- Create: `tools/saturn/demo_render_overlap_test.c`
- Create: `tools/saturn/render_overlap_integration_test.c`
- Modify: `tools/saturn/frame_pipeline_test.c`
- Modify: `tools/saturn/test_a9_frame_pipeline_integration_contract.py`
- Modify: `tools/saturn/test_a9_sourceboot_cadence_trace_contract.py`
- Modify: `tools/saturn/test_capture_sourceboot_throughput.py`
- Modify: `tools/saturn/capture_sourceboot_throughput.py`
- Modify the monolithic-symbol consumers:
  `tools/saturn/render_job_live_cutover_source_test.c`,
  `tools/saturn/render_job_terrain_route_source_test.c`,
  `tools/saturn/test_render_job_live_cutover_source.py`,
  `tools/saturn/test_render_cluster_generation.py`,
  `tools/saturn/test_a8_deferred_transfer_runtime_contract.py`,
  `tools/saturn/test_vdp1_transfer_pipeline_source.py`, and
  `tools/saturn/dual_actor_worker_test.c`
- Modify: `Makefile.saturn.mk`
- Modify during the implementation transition: this plan, `STATE.md`,
  `ROADMAP.md`, `ARCHITECTURE.md`, `CHANGELOG.md`,
  `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`, and
  `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Append the task transition without staging unrelated history:
  `.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/progress.md`

**Interfaces:**
- Replace sourceboot's accepted-path call to the monolithic
  `sm64_saturn_demo_render_frame()` with exactly these generation-bound
  operations:

  ```c
  typedef enum sm64_saturn_demo_render_status {
      SM64_SATURN_DEMO_RENDER_PENDING = 0,
      SM64_SATURN_DEMO_RENDER_COMPLETE,
      SM64_SATURN_DEMO_RENDER_FAILED
  } sm64_saturn_demo_render_status_t;

  bool sm64_saturn_demo_render_start_frame(
      sm64_saturn_vdp1_backend_t *backend,
      sm64_saturn_gouraud_bank_t *gouraud_bank,
      sm64_saturn_fast3d_profile_t *profile,
      const sm64_saturn_mario_actor_snapshot_t *snapshot,
      const sm64_saturn_mario_actor_pose_t *pose,
      uint32_t generation);

  sm64_saturn_demo_render_status_t sm64_saturn_demo_render_poll_frame(
      sm64_saturn_fast3d_profile_t *profile,
      uint32_t generation);
  ```

- `start_frame(N)` validates nonzero `N`, snapshots every descriptor payload,
  publishes the immutable graph, notifies the slave, and returns. It must not
  call `sm64_saturn_render_job_runtime_drain_master()`, spin or record a master
  retirement wait, reset/retire the graph, begin/finish the VDP1 backend,
  reserve/finalize Gouraud state, or lower a command.
- `poll_frame(N)` returns `PENDING` until
  `sm64_saturn_render_job_runtime_slave_retired()` positively retires `N`.
  Only that successful poll drains remaining READY master work, validates all
  terminal descriptor/result identities, performs the existing stable merge,
  reserves Gouraud entries, calls VDP1 begin/lower/finish exactly once, resets
  the retired queue generation exactly once, and returns `COMPLETE`.
- `FAILED` quarantines generation `N`; it never calls the old monolithic entry
  and never replays terrain, actor, or the full frame serially. A late result
  from `N` cannot lower, transfer, publish, or reopen a bank.
- Sourceboot retains one `sourceboot_active_render_snapshot`, one explicit
  BUILDING `sm64_saturn_vdp1_frame_bank_t *sourceboot_active_build_bank`, the
  Mario pose/snapshot, descriptor contexts/payload banks, and generation `N`
  across `PENDING`. It calls
  `sm64_saturn_frame_pipeline_render_complete(N)` only after `COMPLETE`, then
  marks the bank READY and retires the snapshot. On `FAILED` it quarantines
  both and clears the transaction without publishing.
- There is exactly one active render generation. The scheduler may create the
  single immutable queued snapshot `N+1` and run its authoritative source tick
  while `N` is pending, but sourceboot cannot acquire that snapshot, begin its
  BUILDING bank, or call `start_frame(N+1)` until `N` has completed, passed A8
  transfer, received exact publish acknowledgement, and retired/promoted.
- Do not change master ownership of source simulation, input, live game state,
  allocation, final merge/order, Gouraud reservation, VDP1 lowering, VRAM,
  VDP2 composition, or presentation. The slave consumes only existing
  immutable integer snapshots, pointer-free descriptors, and published payload
  contexts.
- Preserve A9's shared nonzero successor (`UINT32_MAX -> 1`), two-field 30 Hz
  remainder, one-normal-plus-one-recovery budget per presentation lifetime,
  field-rate service/poll, per-observed-field service/poll epochs, exact-
  generation publication acknowledgement, previous-complete-frame reuse, and
  unchanged telemetry units. Preserve A8 as the sole command CPU-DMAC/Gouraud
  SCU-DMA/resident-list owner; Task 9A ends at READY construction.
- Generic scheduler/runtime state (`saturn_frame_pipeline.{h,c}` and any new
  generic lifecycle record) may contain no `bob`, `mario`, `castle`, generated
  BOB-bank, or demo-renderer symbol dependency.
- Extend the cache-through cadence evidence as version 2 with four appended
  cumulative words before `sequence_end`:
  `slave_work_vblank_crossings`, `slave_work_count`,
  `master_finalize_vblank_crossings`, and `master_finalize_count`. The new
  target record is 76 bytes/19 words. The slave lifetime runs from successful
  notify publication to positive retirement and may overlap source simulation,
  so the host report must label it an overlap window rather than add it to the
  exclusive attribution total. Master finalization is an explicit subset of
  complete exclusive construction. The decoder retains explicit 60-byte/
  version-1 support for direct or saved buffers; live target observation
  requires the version-2 symbol and 76-byte payload.

**Pinned reference-code record (no new source copying):**
- SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later,
  inspected `WALLS.C:1240-1408,1803-1950`, `DMA.C`, `DMA.H`, and
  `V_BLANK.C:94-145`: existing attributed worker adapters remain close ports;
  the Task 9A lifetime split is pattern-only project code.
- Sonic Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0,
  inspected `ZT_RENDERING.c:406-505,718-786`, `ZT_FRUSTUM.c:126-161`,
  `ZT_LOADING.c:118-176,299-355`, and `workarea.c:12-25`: pattern-only for
  early dispatch and fixed build/present ownership.
- Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, MIT, inspected public
  CPU-DMAC/SCU-DMA/VDP1 APIs and `libmic3d/render.c`: retained dependency/API
  use only; no implementation copied.
- Jo Engine `556d081146211b6a1cfa6591d70f9487d406758b`, MIT plus file-level
  BSD-style notices, inspected `jo_engine/vdp1_command_pipeline.c` and
  `jo_engine/3d.c`: pattern-only; no allocator or command pipeline copied.
- `malucard/sm64-psx` `3073845688ea273da78d539b20c45110d8a868c3`, no
  repository-wide license found, inspected paths recorded in the upstream
  ledger: behavior-study only; no PS1 source or packet format copied.

- [x] **Step 1: Write the renderer-lifecycle RED fixture**

  Add `tools/saturn/demo_render_overlap_test.c` with controlled fake queue and
  backend hooks. Its exact sequence is: start nonzero `N`; assert one graph
  activation/notify and zero drain/wait/reset/VDP1/Gouraud/lower calls; poll
  before retirement and expect `PENDING` with all zero finalization counts;
  publish positive retirement; poll `N` and expect one drain, one terminal
  validation/merge/reset, one VDP1 begin/finish pair, and `COMPLETE`; poll `N`
  again and expect `FAILED` with no additional side effect. Add cases for a
  second concurrent start, wrong-generation poll, failed descriptor, and late
  retirement after quarantine. All reject without full-frame replay.

- [x] **Step 2: Add the source integration and scene-neutral RED contracts**

  Update `test_a9_frame_pipeline_integration_contract.py` and
  `test_a9_sourceboot_cadence_trace_contract.py` to require start-before-return,
  later poll/finalize, retained snapshot/build-bank identity across `PENDING`,
  render-complete only after `COMPLETE`, and quarantine-only `FAILED`. Reject
  `sm64_saturn_demo_render_frame(` in sourceboot and reject drain, retirement
  loops, VDP1 begin, or queue reset from the start phase. Add a generic-state
  scan rejecting case-insensitive `bob|mario|castle|saturn_demo_render` in
  `saturn_frame_pipeline.{h,c}`.

- [x] **Step 3: Run RED and record the intended failures**

  Run:

  ```powershell
  python tools/saturn/test_a9_frame_pipeline_integration_contract.py
  python tools/saturn/test_a9_sourceboot_cadence_trace_contract.py
  make -f Makefile.saturn.mk verify-demo-render-overlap
  ```

  Expected: the Python contracts fail on the direct monolithic call and absent
  pending retention; Make fails because `verify-demo-render-overlap` and the
  start/poll symbols do not exist. Record commands and failure text in the
  aggregate evidence report before implementation.

  RED recorded 2026-08-05. The integration contract errors on the absent
  `sm64_saturn_demo_render_start_frame()` call, the cadence source contract
  fails the still-version-1/60-byte ABI and absent start/poll boundary, and the
  new C11 lifecycle fixture fails compilation because the production
  `saturn_render_lifecycle.{h,c}` seam does not exist. The capture-tool suite
  independently fails because the decoder still advertises 60 bytes/version 1
  and has no overlapping-slave-window phase. The host's plain `python` and
  `make` aliases are unavailable; the recorded reruns use the repository venv
  and `mingw32-make` without invoking MSYS/SH directly.

- [x] **Step 4: Split the renderer at the existing queue-retirement boundary**

  Move only immutable preparation, graph activation/publication, and slave
  notify into `sm64_saturn_demo_render_start_frame()`. Preserve current static
  descriptor payload banks until retirement. Move the existing post-retirement
  drain, telemetry snapshot, all-terminal validation, deterministic terrain/
  actor merge, Gouraud reservation, VDP1 lowering, queue reset, and frame
  accounting into `sm64_saturn_demo_render_poll_frame()`. Store exact active
  generation and phase validity; reserve zero and fail closed on any mismatch.
  Delete the accepted-path monolithic entry rather than wrapping it.

  Implemented with a scene-neutral `saturn_render_lifecycle` controller and
  renderer-owned transaction payload. Start publishes the immutable graph and
  notifies once; poll waits for positive retirement, drains/finalizes once,
  and clears the exact generation. The monolithic public entry is deleted.

- [x] **Step 5: Run the renderer fixture GREEN and catch failure mutations**

  Add `verify-demo-render-overlap` to `Makefile.saturn.mk` with C11
  `-Wall -Wextra -Werror` normal and `expect_failure.py` variants that (1)
  finalize before positive retirement, (2) lower twice, and (3) replay on
  failure. Run that target. Expected: nominal PASS and all three mutations are
  rejected.

  `mingw32-make -f Makefile.saturn.mk verify-demo-render-overlap` is GREEN:
  nominal lifecycle PASS and finalize-before-retirement, double-lowering, and
  serial-replay mutations are all caught.

- [x] **Step 6: Extend the scheduler model for a genuinely pending render**

  In `frame_pipeline_test.c`, drive: start render `N`; observe a later field;
  execute the allowed source tick and publish only the queued immutable
  snapshot `N+1`; reject render-complete/transfer/publish for `N+1`; return to
  service and keep `N` active; acknowledge `render_complete(N)`, transfer, and
  exact publish `N`; only then promote `N+1`. Repeat for
  `UINT32_MAX -> 1`, missed deadline/previous-frame reuse, publish failure, and
  a pending render spanning repeated fields. Assert at most one normal plus one
  recovery tick per lifetime and at most one service and one poll per field.
  Change `saturn_frame_pipeline.{h,c}` only if this RED sequence proves the
  current state model cannot express it.

  The existing scheduler expressed the pending lifetime without production
  changes. The extended fixture keeps `N` active across repeated fields,
  rejects completion/transfer/publication for queued `N+1`, reuses the prior
  frame, and promotes `N+1` only after exact publication acknowledgement for
  `N`; existing wrap, failure, and bounded-budget cases remain GREEN.

- [x] **Step 7: Integrate sourceboot retention without changing A8 ownership**

  Make the first `SERVICE_RENDER_JOBS(N)` acquire snapshot/build bank once,
  bind the immutable camera/pose, and call `start_frame(N)`. A later service
  calls `poll_frame(N)`. On `PENDING`, return immediately to the action loop
  with `N`'s snapshot, descriptor payloads, and BUILDING bank intact. On
  `COMPLETE`, mark that exact bank READY, acknowledge render completion, then
  complete/retire the snapshot. Leave `POLL_TRANSFERS` and `PUBLISH_FRAME`
  sequencing under the existing A8/A9 code. On `FAILED`, quarantine once,
  retain the prior published frame, increment fault/reuse evidence, and never
  invoke a serial fallback.

  Sourceboot now retains one active snapshot and explicit BUILDING bank across
  PENDING. COMPLETE alone marks READY and acknowledges renderer completion;
  FAILED quarantines the bank/snapshot and records a generation tombstone.
  A8 transfer and presentation helpers are unchanged owners.

- [x] **Step 8: Repair every old monolithic-symbol contract and run GREEN**

  Update the named live-cutover, terrain, cluster, A8, transfer, and dual-actor
  contracts to inspect start/poll ownership rather than the deleted function.
  Run serially:

  ```powershell
  make -f Makefile.saturn.mk verify-demo-render-overlap
  make -f Makefile.saturn.mk verify-frame-pipeline
  python tools/saturn/test_a9_frame_pipeline_integration_contract.py
  python tools/saturn/test_a9_sourceboot_cadence_trace_contract.py
  make -f Makefile.saturn.mk verify-render-job-runtime
  make -f Makefile.saturn.mk verify-render-job-live-cutover
  make -f Makefile.saturn.mk verify-vdp1-frame-bank
  make -f Makefile.saturn.mk verify-vdp1-transfer-pipeline
  ```

  Expected: every focused gate passes; existing normal/mutation scheduler
  outcomes, queue exact-once ownership, frame-bank quarantine, and A8 deferred
  transfer ownership remain unchanged. Do not run target builds yet.

  All eight listed commands pass serially using the repository Python venv and
  `mingw32-make`. The terrain-route and migrated cluster-generation assertions
  also pass. The optional full dual-actor executable compiles but stops first
  at its pre-existing worker-context check, before its migrated renderer
  assertion; this is not substituted for a required green gate.

- [x] **Step 9: Add RED/GREEN versioned phase evidence**

  First extend `test_capture_sourceboot_throughput.py` with a 76-byte/version-2
  fixture and mutations for torn seqlock, wrong size/version, wrap, slave
  lifetime shorter than its finalization boundary, and accidental additive
  attribution of the overlapping slave window. Watch it fail against the
  60-byte-only decoder. Then append the four target words, publish them through
  the existing exact P2 seqlock, retain version-1 decoding, and report separate
  `source_tick`, `slave_work_overlap_window`, and `master_finalization` deltas.
  Run both capture-tool tests and the A9 cadence source contract GREEN.

  Sourceboot publishes the 76-byte/19-word version-2 record with four appended
  counters. The decoder accepts only explicit v1/60-byte or v2/76-byte records,
  keeps the slave lifetime outside additive attribution, and reports master
  finalization separately. Capture tests pass 31/31 and cadence source tests
  pass 3/3, including torn/version/size/wrap/overlap mutations.

  Steps 1--9 are checkpointed by `ec81ddc6..0f5ccd65`. Independent review,
  target build/capture, broad verify, native-math, and manual Ymir remain
  unchecked.

  Fix Round 1 watched RED reproduced the review gaps: the new production-
  linked integration target failed before the LOD-lifetime and overlap-phase
  controllers existed, and the v2 capture fixture reported only four of five
  attributable crossings. GREEN now uses renderer-owned exact-generation LOD
  lifetime state, lifecycle notification/retirement events, complete
  construction accounting from first service through final lowering, and a
  terminal quarantine refresh before queue reset. The real job runtime, graph,
  queue, lifecycle, frame scheduler, LOD path, and overlap controller pass one
  N/N+1 executable harness; mutations for active-state reset, omitted start
  work, pre-notify timing, and skipped quarantine refresh are all rejected.
  Direct/saved v1 buffers remain supported; live observation requires v2.
  Fix Round 1 implementation is `24528bf6`.
  Fresh independent rereview, target/Ymir, broad verify, and native-math gates
  remain unchecked.

  Fix Round 2 watched RED first failed the new target-aware source contract
  3/3 on cached worker-visible LOD state, cached phase clock/record, and absent
  runtime release hooks. A direct C11/Werror integration compile then failed on
  the absent marker type and registration API. GREEN moves primitive tiers,
  cluster LOD state, and the exact-generation lifetime to the linker's P2
  `.uncached` partition; sourceboot's marker clock, phase record, and acceptance
  flag use the same target-coherent boundary. Runtime marker callbacks carry
  timestamps captured at notify and positive-retirement release, with phase
  publication ordered before slave wake or retirement visibility. The
  production-linked failure case combines a deferred scene transition with
  terminal quarantine and asserts no early reset, post-finish reset, one slave
  failure, and nonzero `QQ`. The normal integration and six mutations pass;
  `verify-render-job-runtime` and `verify-demo-render-overlap` also pass.
  Fix Round 2 implementation and governing-doc transition is `162f2a7d`.
  Scoped rereview of `420b6ce8..050aa3bc` is specification PASS and code-
  quality PASS with no findings. C1 target-coherent LOD ownership, I1 exact
  marker timing, and I3 production-path assurance are closed. Step 11 target/
  capture, manual Ymir, broad verify, and native-math remain unchecked.

- [x] **Step 10: Complete two-stage source review before any target build — PASS / GO**

  Request specification review against this Task 9A contract, then quality
  review across the exact scoped diff. Required verdict is GO/PASS from both.
  Any lifecycle, ownership, replay, generation, phase-evidence, or test gap is
  repaired with watched RED/GREEN evidence and rereview. Record review commit/
  range and verdict in the plan, aggregate report, and SDD ledger. A review
  failure leaves the target build checkbox unchecked.

  Initial review was FAIL/NO-GO. Fix Rounds 1 and 2 landed through `050aa3bc`.
  Final scoped rereview of `420b6ce8..050aa3bc` is specification PASS and
  code-quality PASS with no Critical, Important, or Minor findings. The
  reviewer explicitly authorizes exactly one serialized Step 11 build/capture;
  actual ELF/map P2 addresses, memory margins, and runtime uplift remain open.

- [ ] **Step 11: Run exactly one serialized DLL-safe target build and capture**

  After both reviews pass, verify no `make`, SH compiler, or sibling target
  build is running, then use the exact wrapper below with `make -B -j1`. Never
  run target builds in parallel. Record exit, duration, ELF/ISO/CUE hashes,
  sizes, map margins, and feature flags. Against that exact ELF/CUE, run one
  bounded automatic cadence capture and record source-tick fields, slave work
  overlap window, master-finalization fields, generations, queue claims/
  retirement/failure/quarantine, transfer faults/waits, reuse, and FPS. The
  capture is comparative emulator evidence, not a promised uplift or manual
  acceptance.

- [ ] **Step 12: Reconcile, commit, and review the completed transition**

  Mark Task 9A `source-complete` only after focused tests and two-stage review;
  mark target evidence complete only after Step 11. Update every active doc in
  the Files list with actual commands/results/hashes and keep manual Ymir,
  broad native-math, and any failed gate unchecked. Append the mixed progress
  ledger without staging unrelated content. Commit the scoped implementation
  and same-commit changelog/docs as
  `perf(saturn): overlap render work with next source tick`, then request final
  scoped rereview. Do not start Task 10 until plan, ledger, evidence, and Git
  head agree.

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
