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

- [x] **Step 6: Verify generation-coherent VDP2 composition — SOURCE COMPLETE**

  Sky camera and HUD metrics now name the same displayed/rendered/simulation
  generations. VDP2 remains geometry-free: its terminal input is only the
  bank-owned immutable camera and a three-generation record. Displayed and
  rendered must equal that camera's bank generation; simulation is copied from
  the scheduler at the same terminal boundary, so a bounded recovery lead is
  explicit rather than mixed. Mismatch is fail-closed before sky, HUD, layers,
  or VBlank commit; a changed tuple bypasses rate-limited metric refresh so
  the sky and HUD switch together. TDD RED was the missing metadata/interface contract;
  focused VDP2, runtime-contract, and source-boundary mutation tests are
  GREEN. This is an internal contract repair: no external adaptation fits or
  is used. Independent rereview, target/manual, and native-math gates remain
  unchecked.

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
