# Emergency A9.0: one VBlank, one presentation generation

**Goal:** remove the sourceboot scheduler feedback loop that can submit many
VDP1 plots for one displayed VDP2 field. This is a full-game-safe cadence
correction, not a BOB-specific optimization and not a camera replacement.

## Evidence and reference-code decision

Ymir counts VDP1 plot completions separately from displayed VDP2 fields. The
owner observed VDP1 about 60 FPS while VDP2 was about 1–2 FPS. Current
`sourceboot/main.c` starts `vdp1_sync_render()`/`vdp1_sync()` every outer-loop
iteration, and its four-tick catch-up loop adds newly elapsed VBlank credit
while it is still executing ticks. That can form a catch-up/submission death
spiral.

References inspected before design:

- SlaveDriver `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later:
  its VBlank-oriented ownership and completion patterns are architecture
  precedent only. Files: `V_BLANK.C`, `V_BLANK.H`, `DMA.C`, `DMA.H`.
- Sonic Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b`, GPLv3:
  `Projects/SONIC Z-TREME/ZTE/ZT_GAME.c` puts one `slSynch()` at the terminal
  frame boundary; `ZT_RENDERING.c` owns VBlank-facing display work. Reuse mode:
  pattern-only/clean-room; no GPL source is copied.
- Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, MIT: existing
  `vdp1_sync_*` and VDP2 API use remains unchanged except for call cadence.

## Global constraints

- Saturn-only; retain full source gameplay/geo walk and camera authority.
- Master SH-2 owns simulation, VDP1/VDP2 registers, presentation, and all
  completion state. No target build/Ymir launch runs concurrently with another.
- Do not invoke `sh-elf-*` from Windows; use `tools/saturn/with-msys-toolchain.ps1`.
- The scheduler must be scene-neutral; BOB is only the manual demonstrator.
- Update the master pipeline plan, architecture decision ledger, evidence report,
  and this plan/SDD ledger on every transition. Keep unrun target gates unchecked.

## Task 1: implement and verify the presentation boundary

**Files:**

- Modify: `src/port/saturn/sourceboot/main.c`
- Create: `tools/saturn/test_sourceboot_presentation_boundary.py`
- Modify: `Makefile.saturn.mk`
- Modify: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Modify: `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`
- Modify: `docs/superpowers/specs/2026-08-03-saturn-overlapped-render-pipeline-design.md`
- Modify: `CHANGELOG.md`, `STATE.md`, `ARCHITECTURE.md`, `HOWTO.md` only when
  needed by the portfolio documentation rule.

**Acceptance contract:**

1. Consume elapsed VBlank credit once at outer-loop entry. Tick execution must
   not sample/re-add that credit.
2. Execute one normal simulation tick and at most one recovery tick in one
   observed generation. Drop any remaining eligible credit, accumulating an
   explicit dropped-credit counter.
3. Submit at most one VDP1 plot for a newly observed VBlank generation. A loop
   with no completed fresh frame reuses the prior completed VDP1 list and does
   not rebuild/upload/sync another plot.
4. `vdp1_sync_render()`/`vdp1_sync()` and `sm64_saturn_vdp2_frame_commit()` are
   co-located in one terminal presentation boundary, keyed to the same
   generation. VDP2 remains geometry-free.
5. The behavior applies to the full sourceboot path; it neither suppresses the
   geo walk nor adds BOB branches.

- [x] **Step 1: Write the red scheduler/source mutation test.** It rejects
  `SOURCEBOOT_MAX_SIM_CATCHUP 4U`, re-adding VBlank credit inside the tick loop,
  more than one VDP1 submission path per generation, and a VDP2 commit outside
  the terminal VDP1 boundary. The pre-change source failed the real gate.
- [x] **Step 2: Add the named `verify-sourceboot-presentation-boundary` target
  and record the red command/result.**
- [x] **Step 3: Implement the smallest scene-neutral cadence state needed for
  the contract.** Keep counters observable in the existing diagnostic profile
  when a compatible location exists; do not begin A2 snapshots/banks/queues.
- [x] **Step 4: Run the focused gate and the directly relevant existing runtime
  contract gate.** The focused gate passes; the runtime wrapper reproduces the
  known Windows-path failure before compilation and remains unpassed.
- [x] **Step 5: Update all live records and commit the behavior with a same-
  commit changelog entry:** `perf(saturn): fence presentation to VBlank`.
- [x] **Step 6: Complete independent specification and quality reviews, resolve
  every Important finding, and record the final verdicts.** Specification review
  was GO. Initial quality review found I1 (the duplicate terminal-helper call
  mutant was not rejected); fix `2de483d9` added the exact-one assertion and
  mutant, addressed M1/M2, and scoped quality rereview was GO with no new
  Critical or Important findings.
- [ ] **Step 7: Build exactly one serial experimental CUE and manually test in
  Ymir using the project profile/32-Mbit DRAM cart.** Record VDP1/VDP2 counters,
  controls, BOB visibility, and qualitative speed. Do not treat emulator timing
  as retail-hardware proof. Serial build PASS on 2026-08-03 (294.1 seconds);
  Ymir was launched from a short staged CUE path with the project profile and
  manual owner observation is active. This step remains unchecked until the
  owner result is recorded.

## Completion criteria

Every acceptance-contract item has direct source/test evidence, the focused
test was observed red before implementation and green after it, both reviews
are clean, and one serial Ymir manual result is recorded. A visible speed gain
is the expected outcome but is not substituted for the structural invariants.

## Live Task 1 transition — implementation ready for review

The sourceboot loop now observes `sourceboot_vblank_out_count` once per outer
iteration. A stale generation waits and retains the completed VDP1 list; a new
generation runs at most two eligible source ticks, drops only additional whole
tick credit, constructs one list, and enters `sourceboot_present_generation()`.
That terminal function starts/completes one VDP1 plot and commits the
geometry-free VDP2 frame using the same observed generation. The appended
diagnostic profile fields expose the presentation generation and dropped
credit count.

Task 1 is **source-complete**; only Step 7 target/Ymir evidence remains open.
Implementation evidence is recorded in
`.superpowers/sdd/2026-08-03-vblank-presentation-boundary/task-1-report.md`.
The serial target/Ymir gate remains unchecked. No GPL source was copied: SlaveDriver and Z-Treme were
pattern-only/clean-room cadence references at their pinned revisions.

## Task 2: restore and prove bootstrap VDP2 retirement

**Why:** The first manual A9.0 CUE exited/froze immediately after BIOS. Read-
only investigation found no crash dump but identified one new boot sequencing
delta with strong correlation: Task 1 removed the predecessor's initial
`sm64_saturn_vdp2_frame_begin()`/`sm64_saturn_vdp2_frame_commit()` plus
`vdp2_sync_wait()` barrier. `user_init()` queues VDP2 sky DMA before this
point. The testable hypothesis is that the initial queue must retire before
the first combined VDP1/VDP2 presentation.

**Files:** `src/port/saturn/sourceboot/main.c`,
`tools/saturn/test_sourceboot_presentation_boundary.py`, the evidence report,
this plan, master plan, architecture ledger, and same-commit `CHANGELOG.md`
for behavior.

**Contract:**

1. Before scheduler generation initialization, exactly one bootstrap VDP2
   begin/commit with a null snapshot retires through `vdp2_sync_wait()`.
2. Bootstrap performs no VDP1 sync, source tick, geometry work, or generation
   publication.
3. Every post-bootstrap displayed generation still uses only
   `sourceboot_present_generation()` for the paired VDP1/VDP2 boundary.

- [x] **Step 1: Extend the focused source-mutation test and observe RED.**
  The real source failed only for its missing null-snapshot bootstrap begin;
  the gate also rejects absent/late/duplicate bootstrap plus VDP1/simulation
  work in the bootstrap interval.
- [x] **Step 2: Restore only the bootstrap barrier and run focused GREEN.**
  Restored the predecessor sequence verbatim in `815c4352`; the focused source
  mutation gate is GREEN (6 tests).
- [x] **Step 3: Update records, commit, and complete spec/quality reviews.**
  Specification and quality reviews are GO; records are `815c4352` and
  `2a5a909a`. The bootstrap hypothesis remains runtime-unproven.
- [ ] **Step 4: Build one serial CUE and manually launch with `.ymir-profile`;
  record whether post-BIOS execution remains alive before evaluating speed.**

## Task 3: persistent post-BIOS boundary trace

**Why:** The Task 2 replacement CUE still fails immediately after BIOS. The
bootstrap hypothesis is therefore not yet sufficient. Do not attempt another
performance or recovery change without target-readable evidence.

**Files:** `src/port/saturn/sourceboot/main.c`, a focused source/host trace
test under `tools/saturn/`, target capture tooling only if required to read the
existing Ymir debug protocol, and this plan/evidence/ledger/changelog as needed.

**Contract:**

1. Export a stable, volatile, symbol-resolvable boot-trace record in target RAM
   with magic/version, monotonically written stage, observed VBlank generation,
   scheduler credit, and VDP1/VDP2 presentation generation.
2. Record unambiguous stages before and after bootstrap retirement,
   `thread5_game_loop`, stale wait, source tick, VDP1 sync/render, and VDP2
   commit. Stage writes must not call VDP sync, allocate, or branch on BOB.
3. A bounded Ymir debug-service capture reads the record after BIOS handoff and
   reports the last stage plus raw words. It is diagnostic only, not a manual
   GUI launch or performance measurement.
4. The trace remains default-on in debug sourceboot builds until this failure
   is resolved, then may be retained as low-cost fault telemetry.

- [x] **Step 1: Add a failing focused trace-contract/mutation test.** The
  real source failed for its missing magic word; the new reader test failed
  explicitly because the capture module did not yet exist.
- [x] **Step 2: Implement trace writes and a symbol-aware debug-service reader;
  show focused GREEN.** `30123c1b` publishes the target-RAM record and the
  bounded headless reader. Focused source, reader, and retained presentation
  mutation gates are green; no target artifact was built or launched.
- [x] **Step 3: Review the diagnostic change and build one serial trace CUE.**
  Both review streams are clean after three TDD fix rounds. The trace CUE built
  serially; the reader resolves the matching ELF symbol at `0x060eb53c`.
- [ ] **Step 4: Run one bounded Ymir debug capture and record the exact last
  observed stage before deciding on any repair.**

## Task 4: trace pre-main sourceboot entry

The first bounded capture read eight zero words at the resolved trace address.
Therefore no claim can yet be made that `main()` executes. Extend the same
cache-through record with stages at the earliest reachable sourceboot C entry:
`user_init()` entry and after display/VBlank callback registration. The source
test must reject an initialization trace only after VDP configuration or a
cached-only write. Build one serial diagnostic CUE and capture it before
changing scheduler, VDP1, or VDP2 behavior again.

- [x] **Step 1: Extend focused source and reader tests; observe RED.** The
  source trace gate failed for its missing `user_init()` entry stage, and the
  reader gate decoded stage 1 as `main-entry` rather than `user-init-entry`.
- [x] **Step 2: Publish cache-through stages at `user_init()` entry and after
  VBlank callback registration; show focused GREEN.** The source mutation gate
  rejects a pre-main trace moved after VDP configuration or replaced by a
  cached-only writer. The reader now maps both new stage IDs.
- [ ] **Step 3: Build one serial diagnostic CUE and run one bounded headless
  Ymir capture with its matching ELF.** Record the exact final stage and raw
  words before any scheduler, VDP1, or VDP2 repair decision.

## Live Task 4 transition — source-complete; target evidence pending

The fixed eight-word cache-through record is unchanged. `user_init()` now
publishes `user-init-entry` before peripheral or VDP setup, then
`user-init-callbacks-registered` immediately after
`vdp_sync_vblank_out_set()`, before the first INTBACK request. This isolates
whether sourceboot reaches its earliest C hook and callback registration while
preserving the existing scheduler and VDP behavior. Focused source and reader
mutation gates are green; no target build, CUE construction, Ymir launch, or
capture ran. Independent review plus the serial CUE/capture gate remain open.

## Live Task 2 transition — source-complete; target evidence pending

`815c4352` restores only the predecessor's boot-time null-snapshot VDP2
begin/commit plus `vdp2_sync_wait()` barrier after `dbgio_flush()` and before
frontend/scheduler initialization. The existing one-VBlank scheduler and its
single `sourceboot_present_generation()` VDP1/VDP2 terminal boundary remain
unchanged. The source mutation test was observed RED before the production
edit and GREEN afterward; independent reviews and one replacement serial
CUE/Ymir observation remain required, so the post-BIOS hypothesis is not yet
runtime-proven.

## Live Task 3 transition — source-complete; review and target capture pending

`30123c1b` adds the global volatile `sourceboot_boot_trace` record. Its magic
and version identify an eight-word, symbol-resolvable RAM contract; monotonically
incremented `stage` publishes a named `stage_id` plus observed VBlank,
scheduler-credit, and VDP1/VDP2 presentation generations. Writes bracket
bootstrap retirement, the `thread5_game_loop()` handoff, stale waits, source
ticks, VDP1 render/sync, and VDP2 commit without performing synchronization,
allocation, or BOB-specific work. `capture_sourceboot_boot_trace.py` resolves
the global from the matching ELF, runs the existing bounded BIOS handoff macro
through headless Ymir, and reports the decoded last stage with raw words. It
does not build a target, launch a GUI, or measure performance. Independent
review, one serial trace CUE, and one bounded capture remain unchecked.

**Fix round 1/5:** Specification review rejected the first Task 3 range
because `--post-bios-frames 0` reached Ymir's invalid `exec.run_for` request.
`2c1acca8` now rejects zero before starting the headless client; its focused
reader test was RED first and is GREEN with the retained source/presentation
gates. Independent rereview is still required, so Step 3 remains unchecked.

**Fix round 2/5:** Quality review rejected cached P1 trace publication because
the selected debugger reads backing WRAM. `b317a2e5` keeps the exported
symbol but writes via its `CPU_CACHE_THROUGH` alias, following the in-repo
hwtest telemetry precedent, and pins the exact eight-word/32-byte ABI with a
C static assertion plus mutation coverage. The focused gate was RED before
the fix and is GREEN afterward; quality rereview is still required, so Step 3
remains unchecked.

**Fix round 3/5:** The first bounded capture stopped before emulation during
direct `sh-elf-nm` resolution. `2ba3ba64` routes symbol lookup through the
audited DLL-safe MSYS wrapper and accepts the target ABI's leading underscore;
the newest linked ELF now resolves read-only at `0x060eb53c`. The focused
reader gate was RED first and is GREEN afterward. No build/capture ran, and
quality rereview plus the serial trace-CUE/capture gates remain unchecked.

## Live diagnostic transition — artifact-bound capture, root cause still open

Task 5 seeded the trace header in ELF `.data` (`2f764356`) so a zero record
could no longer be mistaken for the target's pre-main state. Task 6 then made
the reader fail-safe and provenance-bound: `981b221e` writes raw target words,
Ymir notifications, and bounded stderr even when the trace header is invalid;
`5ae1cff8` requires the CUE `FILE` target, ISO, and CUE-local matching ELF to
be named and recorded together before Ymir can start. Focused reader tests are
green (9) and Python compilation/diff checks pass.

One bounded, no-build headless capture used the explicitly staged pair:
CUE SHA-256 `cdbf0bfa…`, ISO SHA-256 `04c0c479…`, and ELF SHA-256
`d89897b8…`. Ymir reported `Filesystem built successfully`, reached live game
PCs, and repeatedly hit the requested frame limit rather than exiting. The
trace address resolved from that ELF was `0x0608B43C`, but its raw header was
`0x045E02AA`, not the required `0x53394254`. Therefore the capture is valid
provenance evidence but **not** target evidence that the trace record was
read; it must not authorize another scheduler/VDP repair. The remaining gate
is an ELF/RAM mapping or overwrite diagnosis that explains this contradiction,
then one repeat bounded capture with the corrected observation point.
