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

**Status:** active plan; no implementation, test, review, target build, or FPS
evidence exists yet. Task 9A is the next measured CPU-lifetime experiment.
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
  exclusive attribution total. Master finalization is exclusive construction
  work. The decoder retains explicit 60-byte/version-1 historical support.

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

- [ ] **Step 1: Write the renderer-lifecycle RED fixture**

  Add `tools/saturn/demo_render_overlap_test.c` with controlled fake queue and
  backend hooks. Its exact sequence is: start nonzero `N`; assert one graph
  activation/notify and zero drain/wait/reset/VDP1/Gouraud/lower calls; poll
  before retirement and expect `PENDING` with all zero finalization counts;
  publish positive retirement; poll `N` and expect one drain, one terminal
  validation/merge/reset, one VDP1 begin/finish pair, and `COMPLETE`; poll `N`
  again and expect `FAILED` with no additional side effect. Add cases for a
  second concurrent start, wrong-generation poll, failed descriptor, and late
  retirement after quarantine. All reject without full-frame replay.

- [ ] **Step 2: Add the source integration and scene-neutral RED contracts**

  Update `test_a9_frame_pipeline_integration_contract.py` and
  `test_a9_sourceboot_cadence_trace_contract.py` to require start-before-return,
  later poll/finalize, retained snapshot/build-bank identity across `PENDING`,
  render-complete only after `COMPLETE`, and quarantine-only `FAILED`. Reject
  `sm64_saturn_demo_render_frame(` in sourceboot and reject drain, retirement
  loops, VDP1 begin, or queue reset from the start phase. Add a generic-state
  scan rejecting case-insensitive `bob|mario|castle|saturn_demo_render` in
  `saturn_frame_pipeline.{h,c}`.

- [ ] **Step 3: Run RED and record the intended failures**

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

- [ ] **Step 4: Split the renderer at the existing queue-retirement boundary**

  Move only immutable preparation, graph activation/publication, and slave
  notify into `sm64_saturn_demo_render_start_frame()`. Preserve current static
  descriptor payload banks until retirement. Move the existing post-retirement
  drain, telemetry snapshot, all-terminal validation, deterministic terrain/
  actor merge, Gouraud reservation, VDP1 lowering, queue reset, and frame
  accounting into `sm64_saturn_demo_render_poll_frame()`. Store exact active
  generation and phase validity; reserve zero and fail closed on any mismatch.
  Delete the accepted-path monolithic entry rather than wrapping it.

- [ ] **Step 5: Run the renderer fixture GREEN and catch failure mutations**

  Add `verify-demo-render-overlap` to `Makefile.saturn.mk` with C11
  `-Wall -Wextra -Werror` normal and `expect_failure.py` variants that (1)
  finalize before positive retirement, (2) lower twice, and (3) replay on
  failure. Run that target. Expected: nominal PASS and all three mutations are
  rejected.

- [ ] **Step 6: Extend the scheduler model for a genuinely pending render**

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

- [ ] **Step 7: Integrate sourceboot retention without changing A8 ownership**

  Make the first `SERVICE_RENDER_JOBS(N)` acquire snapshot/build bank once,
  bind the immutable camera/pose, and call `start_frame(N)`. A later service
  calls `poll_frame(N)`. On `PENDING`, return immediately to the action loop
  with `N`'s snapshot, descriptor payloads, and BUILDING bank intact. On
  `COMPLETE`, mark that exact bank READY, acknowledge render completion, then
  complete/retire the snapshot. Leave `POLL_TRANSFERS` and `PUBLISH_FRAME`
  sequencing under the existing A8/A9 code. On `FAILED`, quarantine once,
  retain the prior published frame, increment fault/reuse evidence, and never
  invoke a serial fallback.

- [ ] **Step 8: Repair every old monolithic-symbol contract and run GREEN**

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

- [ ] **Step 9: Add RED/GREEN versioned phase evidence**

  First extend `test_capture_sourceboot_throughput.py` with a 76-byte/version-2
  fixture and mutations for torn seqlock, wrong size/version, wrap, slave
  lifetime shorter than its finalization boundary, and accidental additive
  attribution of the overlapping slave window. Watch it fail against the
  60-byte-only decoder. Then append the four target words, publish them through
  the existing exact P2 seqlock, retain version-1 decoding, and report separate
  `source_tick`, `slave_work_overlap_window`, and `master_finalization` deltas.
  Run both capture-tool tests and the A9 cadence source contract GREEN.

- [ ] **Step 10: Complete two-stage source review before any target build**

  Request specification review against this Task 9A contract, then quality
  review across the exact scoped diff. Required verdict is GO/PASS from both.
  Any lifecycle, ownership, replay, generation, phase-evidence, or test gap is
  repaired with watched RED/GREEN evidence and rereview. Record review commit/
  range and verdict in the plan, aggregate report, and SDD ledger. A review
  failure leaves the target build checkbox unchecked.

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

