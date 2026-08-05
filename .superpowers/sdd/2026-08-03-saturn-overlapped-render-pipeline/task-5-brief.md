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
- [ ] **Step 6: Serialized target build and automatic desktop-Ymir
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
  host-green. It hashes the explicit CUE/ELF/Ymir artifacts, resolves exact
  ELF record sizes in-process, proves target code identity before accepting a
  sample, reads P2 telemetry one VBlank at a time, and fails closed on an
  in-flight or reused queue sequence. No matching live invocation has occurred;
  retain this step and the queue gate unchecked. **Review repair 1/5
  (2026-08-05):** coherent sequence reuse across two presentation edges now
  fails end-to-end rather than being omitted, identity proof accepts only
  allocated `SHT_PROGBITS` bytes contained in `PT_LOAD`, and diagnostics cap
  notification count and serialized bytes independently of the Ymir client.
  Twelve focused capture tests and sixteen boot-trace tests are host-green;
  no target evidence exists and this step remains unchecked. **Review repair
  2/5 (2026-08-05):** identity proof now additionally requires the exact
  affine `PT_LOAD` mapping (`section.address - segment.vaddr ==
  section.offset - segment.offset`), rejecting a malformed section whose
  independent ranges fit but whose file bytes map elsewhere. Thirteen capture
  tests and sixteen boot-trace tests are host-green; fresh review and valid
  live evidence remain open.
## Controller amendment — execute only A5.9 automatic queue observation

This dispatch does **not** reopen the completed A5 queue/cutover work below.
Implement only the remaining Task 5.9 Step 6 observation boundary: a bounded
host tool that reads coherent live target queue telemetry and target
presentation cadence from the already-built matching CUE/ELF. Do not change
target C/C++, build a target, change scheduling policy, or claim an FPS gain.

### Required files and behavior

- Create `tools/saturn/capture_sourceboot_throughput.py` and
  `tools/saturn/test_capture_sourceboot_throughput.py` using strict TDD.
- Reuse existing newline JSON-RPC client/process patterns from
  `capture_hwtest.py`, `capture_camera_idle.py`, and `capture_route_views.py`.
  Directly adapt local battle-tested helpers where they fit; do not invent a
  second loose RPC protocol.
- Require explicit matching `--game` CUE and `--elf`. Hash both plus Ymir in
  the report. Before accepting any target observation, prove the running image
  contains immutable bytes from the exact ELF at their linked address. This
  closes the prior invalid headless-trace failure where paired ELF main bytes
  were absent. A boot trace alone is insufficient.
- Resolve local/leading-underscore ELF symbols without hardcoded addresses.
  Required exact sizes are `_sourceboot_boot_trace` 32 bytes, `_s_runtime` 92
  bytes, and `_s_render_job_queue` 232 bytes. Reject missing, duplicate,
  stripped, or wrong-sized symbols.
- Decode big-endian fields. Boot trace offsets: observed VBlank generation
  `+16`, VDP2 presentation generation `+28`. Runtime offsets: QN `+28`, QR
  `+32`, notify sequence `+36`, retired sequence `+40`, QM[4] `+44`, QS[4]
  `+60`, QW `+76`, master failures `+80`, slave failures `+84`, QQ `+88`;
  QF is the two failure counts summed. Queue generation is `+224`.
- Accept a queue snapshot only when queue generation is zero,
  notify-sequence equals retired-sequence and is nonzero, and QN equals QR and
  is nonzero. Never attach one queue sequence to two presentation events.
- Advance one emulated VBlank at a time so presentation transitions cannot be
  skipped. Count only changes in VDP2 presentation generation. Handle 32-bit
  wrap. Require at least two presentation events and one coherent queue record
  or fail closed.
- Compute mean guest FPS from target VBlank delta and median/1%-low from
  adjacent presentation intervals using an explicit nominal refresh rate
  (default 60.0). Counters diagnose; no arbitrary promotion threshold.
- Emit bounded structured JSON containing schema/evidence kind, artifact
  identities, symbol addresses/sizes, measurement summary, latest coherent
  queue values, and presentation events. Any RPC/identity/coherence failure
  must produce a structured failed report, never a success-shaped partial.
- Never invoke `sh-elf-*` directly from inherited Windows PATH. Use an
  in-process ELF parser or the audited `with-msys-toolchain.ps1` boundary.
- Update `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, this plan's Step 6, the
  architecture decision text, and the A5.9 evidence report in the same commit.
  Keep the queue gate unchecked until a valid live capture exists.
- Preserve the unrelated dirty native-math verifier files, the mixed SDD
  ledger, and all untracked audit/evidence files. Stage only this task's files.

### Test-first contract

Watch failures before production code for: symbol resolution/sizes, ELF target
byte identity, every big-endian offset and phase order, coherence rejection,
sequence reuse rejection, generation wrap, cadence math including skipped
VBlanks, fewer-than-two events, and a fake-Ymir integration with an in-flight
snapshot followed by a coherent one. Record exact RED and GREEN commands and
outputs in the report.

---
