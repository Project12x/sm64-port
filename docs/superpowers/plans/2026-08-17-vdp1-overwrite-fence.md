# VDP1 Overwrite Fence Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove sourceboot's unbounded VDP1 command-VRAM overwrite wait by
deferring the exact completed frame bank to a later observed VBlank, while
preserving the accepted gameplay artifact, bank ownership, publication
coherence, and the scheduler's existing two-tick unpublished-generation limit.

**Architecture:** Add an exact-generation `transfer_deferred` acknowledgement to
the master-owned frame pipeline. Sourceboot observes `vdp1_sync_busy()` once
before either VDP1 destination transfer. Idle follows the existing serial
command/Gouraud transfer and publication path; busy acknowledges the missed
transfer opportunity, leaves the source bank `READY`, consumes only the pending
presentation opportunity in scheduler accounting, and returns without DMA,
replot, quarantine, or generation replacement. Retry eligibility comes only
from a later scheduler VBlank epoch. The current profile layout stays at version
4, with legacy wait fields retained but zeroed and the historical
`vdp1_fence_waits` field relabeled as busy deferrals in summaries.

**Tech Stack:** C11 runtime and host contracts, SH-2/Yaul sourceboot target,
GNU Make/MSYS host and Saturn toolchains, Python 3.11 `unittest` source
contracts and evidence tooling, Ymir headless and SDL3 desktop observation,
Keep a Changelog documentation.

**Design contract:**
[`2026-08-17-vdp1-overwrite-fence-design.md`](../specs/2026-08-17-vdp1-overwrite-fence-design.md)

**Active phase plan:**
[`2026-08-17-saturn-shaped-port-phase-plan.md`](2026-08-17-saturn-shaped-port-phase-plan.md)

**Execution ledger:**
[`w0-vdp1-overwrite-fence.md`](../../saturn/evidence/reports/w0-vdp1-overwrite-fence.md)

## Global Constraints

- Work from branch `saturn/recovery` in
  `D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery`.
  Reconcile `git rev-parse HEAD`, the active phase plan, this plan, and the
  ledger before starting each task. Preserve unrelated untracked `.msys-home/`,
  `releases/`, and historical desktop-launch log files.
- The accepted T2.17 artifact `id-c0352f297034f653` is immutable. Do not
  rebuild into, overwrite, rename, or present it as W0. Every W0 build and CUE
  needs a new identity. Record source revision, profile path/hash, build time,
  ELF/ISO/CUE/manifest hashes, and target identity-probe hash before launch.
- Make one causal product behavior change: replace the overwrite wait with
  immediate epoch deferral. Do not add a draw-end interrupt owner, a new DMA
  route, bank cancellation, snapshot drop/coalescing, a new wire format, or a
  generalized recovery abstraction.
- A busy observation occurs before both destination writes. On busy, the exact
  bank remains `READY`; no CPU-DMAC or SCU-DMA is queued, no bank is poisoned or
  quarantined, and no plot/framebuffer-change request is issued. Once submission
  begins, the existing transfer-retirement and poison rules remain authoritative.
- Deferral does not reset `sim_ticks_this_presentation`, add simulation credit,
  alter the nonzero generation sequence, or free either render-snapshot slot.
  A permanently busy device is a bounded-state safe pause, not a playable-state
  promise. A transient busy state must retry and recover automatically.
- Host tests and target compilation may be labeled only
  `host-contract-passed` and `target-compiled`. Only an identity-bound Ymir
  observation may be labeled `live-observed`; only the owner's desktop check may
  be labeled `owner-accepted`.
- After two causal implementation attempts or two hours without a new live
  product observation, stop. Compare against the preserved T2.17 baseline and
  choose narrow rollback, bypass, or a smaller transplant. Do not start a third
  repair or another architecture lane.
- Run the earliest identity-bound normal-profile Ymir capture immediately after
  the minimum code, host gates, target build, memory check, and staging succeed.
  Independent review follows that first live observation, not before it.
- Every behavior-changing commit updates `CHANGELOG.md` `[Unreleased]` in the
  same commit. Update the active phase plan, `STATE.md`, `ROADMAP.md`, and the
  execution ledger in the same task transition. Never mark a target or owner
  gate complete from host-only evidence.
- Do not weaken or delete an existing assertion to make a gate pass. The known
  `verify-sourceboot-presentation-boundary` literal-drift failure may be recorded
  as pre-existing only if its output matches the pre-W0 failure exactly; any new
  W0-specific failure must be fixed narrowly.
- No new target buffers or package fields are permitted. The latest comparison
  ELF leaves HWRAM `0x4A78` bytes free with `0x2B78` above the `0x1F00` floor,
  and LWRAM `0x17620` bytes free against a `0x4000` floor. Remeasure both after
  normal and diagnostic target links.
- Reference-code record is fixed for W0: vendored Yaul/libyaul commit
  `6012f79f237773378c8014e70d8998ad95a38d98` (MIT), inspected
  `libyaul/scu/bus/b/vdp/vdp_sync.c`, reuse mode existing dependency/API
  semantics; SlaveDriver commit
  `a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later), inspected
  `DMA.C` and `SRUINS.C`, reuse mode pattern-only. Preserve existing notices.

## Preserved Product Baseline

| Identity item | Preserved value |
| --- | --- |
| Build identity | `id-c0352f297034f653` |
| ELF SHA-256 | `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd` |
| ISO SHA-256 | `49b68a07144b2b58ad6781dadb35404e038e0f9d75eaf7c354f8fa88e1d4c9cc` |
| CUE SHA-256 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| Release-manifest SHA-256 | `1d73590985a2702db2ccdbc93bb85e376248c9bd0808ea2aed5092938d3b35f3` |
| Target identity-probe SHA-256 | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` |
| Headless cadence | Mean 6.7181 FPS; median 6.6667 FPS; 1% low 6.0 FPS; 8.9310 VBlanks/frame |

The latest source-complete comparison is `id-49894e8e2d3ea415`, ELF
`0a494205f2cc1e69178e9b757913267b15d2fe5ad974bb63cfdc1a09fa55ed62`,
ISO `dc986500774538b560f3148cd214e4e272a2a4505e9fe2811ae591718b0eb350`.
It has the same measured cadence but is neither owner-accepted nor the baseline.

## File Map

| File | Task-local change |
| --- | --- |
| `src/port/saturn/runtime/saturn_frame_pipeline.h:71-77` | Declare and document exact-generation transfer deferral acknowledgement. |
| `src/port/saturn/runtime/saturn_frame_pipeline.c:282-295` | Implement fail-closed deferral transition without publication or credit changes. |
| `tools/saturn/frame_pipeline_test.c:694-806` | Add exact-state, epoch, retry, bounded-budget, and invalid-ack tests. |
| `Makefile.saturn.mk:1404-1474` | Add two compile-time mutation builds to `verify-frame-pipeline`. |
| `src/port/saturn/sourceboot/main.c:879-993,1720-1791` | Remove diagnostic spin and replace normal/diagnostic waits with one busy observation plus scheduler acknowledgement. |
| `src/port/saturn/runtime/saturn_prenotify_profile.h:221-232` | Correct profile field semantics while preserving v4 layout. |
| `tools/saturn/test_vdp1_transfer_pipeline_source.py:1-64` | Prove busy returns before submission/re-presentation and reject all wait loops. |
| `tools/saturn/capture_prenotification_profile.py:500-574` | Summarize deferrals, not fabricated zero-cost waits. |
| `tools/saturn/test_capture_prenotification_profile.py` | Add focused summary-label and zero-event tests. |
| `Makefile.saturn.mk:1680-1696` | Run the new telemetry test under `verify-vdp1-transfer-pipeline`. |
| `CHANGELOG.md:3` | Explain the behavior change, root cause, tradeoff, and compatibility effect. |
| `STATE.md:291-299` | Move W0 from planned to the strongest evidence actually reached. |
| `ROADMAP.md:134-138` | Keep the correctness prerequisite and remaining live/owner gates accurate. |
| `docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md:155-175` | Track W0 task state and link this plan/ledger. |
| `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md` | Record identities, commits, exact tests, review verdict, live proof, and remaining gates. |

## Task 1: Add the exact-generation scheduler deferral contract

**Files:**

- Modify: `src/port/saturn/runtime/saturn_frame_pipeline.h:71-77`
- Modify: `src/port/saturn/runtime/saturn_frame_pipeline.c:282-295`
- Modify: `tools/saturn/frame_pipeline_test.c:694-806`
- Modify: `Makefile.saturn.mk:1404-1474`
- Modify: `docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md:155-175`
- Modify: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md`

- [ ] **Step 1: Reconcile source and documentation before editing.**

  Run:

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' rev-parse HEAD
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' status --short
  rg -n "W0|Status|implementation plan|execution ledger" docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md docs/superpowers/plans/2026-08-17-vdp1-overwrite-fence.md docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md
  ```

  Expected: HEAD equals the planning commit recorded in the ledger; only the
  known unrelated untracked paths are present; W0 is `planned` before the
  transition. If tracked files differ, reconcile the phase plan and ledger
  before changing runtime code.

- [ ] **Step 2: Write the failing nominal scheduler tests first.**

  Add `#include <string.h>` and two tests before
  `test_followup_polls_are_bounded_to_the_submit_field()`. Use failure codes
  208 through 242. The first test must execute this state sequence:

  ```c
  static int test_transfer_deferral_is_epoch_gated_and_recoverable(void)
  {
      sm64_saturn_frame_pipeline_t pipeline;
      uint32_t dropped_before;
      int failure;

      sm64_saturn_frame_pipeline_init(&pipeline, 500U, 0U);
      failure = expect_action(&pipeline, 504U,
                              SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 208);
      if (failure != 0) return failure;
      failure = expect_action(&pipeline, 504U,
                              SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 209);
      if (failure != 0) return failure;
      if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 210;
      failure = expect_action(&pipeline, 504U,
                              SM64_SATURN_FRAME_RUN_SIM_TICK, 2U, 211);
      if (failure != 0) return failure;
      if (!pipeline.queued_snapshot_valid ||
          pipeline.queued_snapshot_generation != 2U) return 212;
      failure = expect_action(&pipeline, 504U,
                              SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 213);
      if (failure != 0) return failure;

      if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 2U)) return 214;
      if (!sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 215;
      if (pipeline.transfer_started || pipeline.transfer_submit_vblank_valid)
          return 216;
      if (!pipeline.transfer_poll_vblank_valid ||
          pipeline.transfer_poll_vblank != 504U) return 217;
      if (!pipeline.render_active || !pipeline.render_completed_valid ||
          pipeline.render_generation != 1U ||
          pipeline.render_completed_generation != 1U) return 218;
      if (!pipeline.queued_snapshot_valid ||
          pipeline.queued_snapshot_generation != 2U ||
          pipeline.displayed_generation != 0U ||
          pipeline.sim_ticks_this_presentation != 2U ||
          pipeline.previous_frame_reuse_count != 1U ||
          pipeline.presentation_pending) return 219;
      if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 220;
      failure = expect_action(&pipeline, 504U,
                              SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 221);
      if (failure != 0) return failure;

      failure = expect_action(&pipeline, 505U,
                              SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 222);
      if (failure != 0) return failure;
      if (!sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U)) return 223;
      failure = expect_action(&pipeline, 505U,
                              SM64_SATURN_FRAME_WAIT_VBLANK, 0U, 224);
      if (failure != 0) return failure;

      dropped_before = pipeline.dropped_sim_tick_credits;
      failure = expect_action(&pipeline, 506U,
                              SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 225);
      if (failure != 0) return failure;
      if (pipeline.sim_ticks_this_presentation != 2U ||
          pipeline.dropped_sim_tick_credits <= dropped_before) return 226;
      if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 227;
      failure = expect_action(&pipeline, 506U,
                              SM64_SATURN_FRAME_PUBLISH_FRAME, 1U, 228);
      if (failure != 0) return failure;
      if (!sm64_saturn_frame_pipeline_publish_complete(&pipeline, 1U, true))
          return 229;
      failure = expect_action(&pipeline, 506U,
                              SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 2U, 230);
      if (failure != 0) return failure;
      return 0;
  }

  static int test_transfer_deferral_rejects_invalid_state_without_mutation(void)
  {
      sm64_saturn_frame_pipeline_t pipeline;
      sm64_saturn_frame_pipeline_t before;
      int failure;

      sm64_saturn_frame_pipeline_init(&pipeline, 600U, 0U);
      failure = expect_action(&pipeline, 602U,
                              SM64_SATURN_FRAME_RUN_SIM_TICK, 1U, 231);
      if (failure != 0) return failure;
      failure = expect_action(&pipeline, 602U,
                              SM64_SATURN_FRAME_SERVICE_RENDER_JOBS, 1U, 232);
      if (failure != 0) return failure;
      if (!sm64_saturn_frame_pipeline_render_complete(&pipeline, 1U)) return 233;

      before = pipeline;
      if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U) ||
          memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 234;
      failure = expect_action(&pipeline, 602U,
                              SM64_SATURN_FRAME_POLL_TRANSFERS, 1U, 235);
      if (failure != 0) return failure;
      before = pipeline;
      if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 0U) ||
          memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 236;
      if (!sm64_saturn_frame_pipeline_transfer_complete(&pipeline, 1U)) return 237;
      before = pipeline;
      if (sm64_saturn_frame_pipeline_transfer_deferred(&pipeline, 1U) ||
          memcmp(&pipeline, &before, sizeof(pipeline)) != 0) return 238;
      return 0;
  }
  ```

  Register both tests in `main()` immediately before the existing T2.17
  follow-up-poll test.

- [ ] **Step 3: Run the scheduler test and confirm RED for the missing API.**

  ```powershell
  $hostGate = 'HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS TMP=D:/tmp TEMP=D:/tmp'
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-frame-pipeline $hostGate
  ```

  Expected: compile/link failure naming
  `sm64_saturn_frame_pipeline_transfer_deferred`. A failure in an old test is
  not the intended RED and must be investigated before implementation.

- [ ] **Step 4: Declare and implement the minimal acknowledgement.**

  Add this declaration immediately before `transfer_complete` in the header:

  ```c
  /* Acknowledge that the exact completed generation found VDP1 busy before
   * hardware transfer submission. The source bank stays owned and complete;
   * this consumes at most the current presentation opportunity and preserves
   * the poll epoch so retry is possible only after another observed VBlank. */
  bool sm64_saturn_frame_pipeline_transfer_deferred(
      sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation);
  ```

  Add this implementation immediately before `transfer_complete`:

  ```c
  bool sm64_saturn_frame_pipeline_transfer_deferred(
      sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation)
  {
      if (pipeline == NULL || generation == 0U ||
          !pipeline->render_active ||
          pipeline->render_generation != generation ||
          !pipeline->render_completed_valid ||
          pipeline->render_completed_generation != generation ||
          !pipeline->transfer_started ||
          pipeline->transfer_generation != generation ||
          !pipeline->transfer_poll_vblank_valid ||
          pipeline->transfer_poll_vblank != pipeline->last_vblank_count ||
          (pipeline->transfer_completed_valid &&
           pipeline->transfer_completed_generation == generation)) {
          return false;
      }
  #if !defined(SM64_SATURN_FRAME_PIPELINE_TEST_DEFER_LEAVES_STARTED)
      pipeline->transfer_started = false;
  #endif
      pipeline->transfer_submit_vblank_valid = false;
  #if defined(SM64_SATURN_FRAME_PIPELINE_TEST_DEFER_CLEARS_POLL_STAMP)
      pipeline->transfer_poll_vblank_valid = false;
  #endif
      if (pipeline->presentation_pending) {
          pipeline_finish_presentation(pipeline);
          pipeline->previous_frame_reuse_count++;
      }
      pipeline->action_generation = pipeline->displayed_generation;
      return true;
  }
  ```

  Do not clear the poll stamp in production and do not touch
  `sim_ticks_this_presentation`, `available_sim_credit`, queued-snapshot fields,
  render completion, transfer completion, or generation counters.

- [ ] **Step 5: Add mutations that prove the two load-bearing assignments.**

  Append two builds to `verify-frame-pipeline`, following the existing mutation
  pattern exactly. Compile one with
  `-DSM64_SATURN_FRAME_PIPELINE_TEST_DEFER_LEAVES_STARTED=1` and label its
  `expect_failure.py` invocation
  `frame pipeline deferral leaves transfer started mutation`. Compile the other
  with `-DSM64_SATURN_FRAME_PIPELINE_TEST_DEFER_CLEARS_POLL_STAMP=1` and label it
  `frame pipeline deferral clears epoch stamp mutation`. Both compile
  `tools/saturn/frame_pipeline_test.c` plus
  `src/port/saturn/runtime/saturn_frame_pipeline.c` with the nominal includes
  and warnings. Use distinct executables under `build/saturn/host-tests/`.

- [ ] **Step 6: Run the focused scheduler and ownership gates.**

  ```powershell
  $hostGate = 'HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS TMP=D:/tmp TEMP=D:/tmp'
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-frame-pipeline $hostGate
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-vdp1-frame-bank $hostGate
  ```

  Expected: nominal scheduler prints `frame pipeline contract: PASS`; all eight
  mutations are rejected; frame-bank binary and source checks pass. This is
  `host-contract-passed`, not target evidence.

- [ ] **Step 7: Update operational state and commit Task 1.**

  Mark W0 `active; scheduler host contract passed; sourceboot and target gates
  pending` in the phase plan and ledger. Record the exact RED command, PASS
  commands, mutation labels, and remaining gates. Commit only Task 1 files:

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' diff --check
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' add src/port/saturn/runtime/saturn_frame_pipeline.h src/port/saturn/runtime/saturn_frame_pipeline.c tools/saturn/frame_pipeline_test.c Makefile.saturn.mk docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' commit -m "feat(saturn): acknowledge deferred VDP1 transfers"
  ```

  Then record the exact commit SHA in the ledger in a documentation-only
  evidence commit. Do not claim sourceboot behavior or target compilation yet.

## Task 2: Replace sourceboot's wait and correct diagnostic semantics

**Task status (2026-08-17):** `host-contract-passed; target compile and live
gates pending`. The first four focused host gates pass; the presentation-boundary
gate remains unchecked for the known pre-W0 `bootstrap must contain exactly one
null-snapshot VDP2 begin` literal drift, before any W0 path assertion. W0.2 is
not `source-complete` until normal and diagnostic target-equivalent consumers
compile.

**Files:**

- Modify: `src/port/saturn/sourceboot/main.c:879-993,1720-1791`
- Modify: `src/port/saturn/runtime/saturn_prenotify_profile.h:221-232`
- Modify: `tools/saturn/test_vdp1_transfer_pipeline_source.py:1-64`
- Modify: `tools/saturn/capture_prenotification_profile.py:500-574`
- Create: `tools/saturn/test_capture_prenotification_profile.py`
- Modify: `Makefile.saturn.mk:1680-1696`
- Modify: `CHANGELOG.md:3`
- Modify: `STATE.md:291-299`
- Modify: `ROADMAP.md:134-138`
- Modify: `docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md:155-175`
- Modify: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md`

- [x] **Step 1: Write the failing source contract before editing sourceboot.**

  Add a general `braced_block_after(text, pattern)` helper to
  `test_vdp1_transfer_pipeline_source.py`. It must find `pattern`, locate the
  following `{`, walk nested braces, and return the complete block, failing on
  missing pattern or unterminated braces.

  Rewrite `test_sourceboot_arms_resident_list_once_after_safe_transfer()` so it
  extracts `sourceboot_frame_poll_transfers`, the `if (overwrite_busy)` block,
  `sourceboot_frame_publish`, and `sourceboot_frame_pipeline_dispatch`, then
  asserts:

  ```python
  busy = transfer.find("vdp1_sync_busy()")
  defer = transfer.find("sm64_saturn_frame_pipeline_transfer_deferred")
  submit = transfer.find("sm64_saturn_vdp1_frame_bank_submit_transfers")
  poll = transfer.find("sm64_saturn_vdp1_frame_bank_poll_transfers")
  self.assertTrue(0 <= busy < defer < submit < poll)
  self.assertIn("sm64_saturn_frame_pipeline_transfer_deferred", busy_block)
  self.assertIn("goto finish;", busy_block)
  self.assertNotIn("sm64_saturn_vdp1_frame_bank_submit_transfers", busy_block)
  self.assertNotIn("sourceboot_frame_reuse_previous", busy_block)
  self.assertNotIn("sourceboot_present_generation", busy_block)
  self.assertNotIn("vdp1_sync_wait()", transfer)
  self.assertNotRegex(transfer, r"while\s*\(\s*vdp1_sync_busy\s*\(\s*\)\s*\)")
  self.assertNotIn("sourceboot_vdp1_fence_spin", source)
  ```

  Retain the existing arm-before-force-before-bank-publish ordering, dispatch
  poll-before-publish ordering, exactly one frame-bank submit call, exactly one
  `vdp1_sync_force_put()`, no `wait_for_publish`, and the CPU-DMAC channel-zero
  ownership test.

- [x] **Step 2: Write the failing telemetry-summary tests.**

  Create `tools/saturn/test_capture_prenotification_profile.py` using
  `unittest`. Import `vdp1_fence_summary` from
  `capture_prenotification_profile`. One test supplies 8 events, 2 historical
  `vdp1_fence_waits`, and fixed EDSR/COPR/LOPR values; assert
  `busy_deferrals == 2`, `deferral_share_of_events == 0.25`, and preservation
  of the status fields. Assert these obsolete cost keys are absent:
  `waits`, `wait_share_of_events`, `mean_ticks`, `mean_cycles`,
  `mean_vblank_equiv`, `max_ticks`, `max_vblank_equiv`, `mean_iterations`,
  `max_raw_interval`, and `max_raw_interval_headroom`. A second test supplies
  zero events and asserts both deferral and CEF shares are `None`.

  Add this command to `verify-vdp1-transfer-pipeline` after the source test:

  ```make
	"$(SATURN_TOOLS_PYTHON)" "$(SATURN_REPO_ROOT)/tools/saturn/test_capture_prenotification_profile.py"
  ```

- [x] **Step 3: Run the transfer gate and confirm RED against old semantics.**

  ```powershell
  $hostGate = 'HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS TMP=D:/tmp TEMP=D:/tmp'
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-vdp1-transfer-pipeline $hostGate
  ```

  Expected: the new source contract reports the old `vdp1_sync_wait()` or
  diagnostic spin, and/or the telemetry test reports missing
  `vdp1_fence_summary`. Existing DMA/frame-bank tests must reach their normal
  result before the intended new RED.

- [x] **Step 4: Remove the diagnostic spin and implement immediate deferral.**

  Delete `sourceboot_vdp1_fence_spin()` and rewrite its T2.8 block comment to
  document one pre-submit gate observation rather than a blocking fence. In
  `sourceboot_frame_poll_transfers()`, after validating the `READY` bank and
  before any submission, use this structure:

  ```c
  #if SATURN_DIAGNOSTIC_MODE != 0
          const uint32_t edsr_entry = sourceboot_vdp1_edsr();
          const uint32_t copr_entry = sourceboot_vdp1_copr();
  #endif
          const bool overwrite_busy = vdp1_sync_busy();
          sourceboot_fast3d.profile.vdp1_overwrite_wait_ticks_last = 0U;
  #if SATURN_DIAGNOSTIC_MODE != 0
          {
              volatile sm64_saturn_prenotify_profile_t *const record =
                  sm64_saturn_prenotify_profile_visible();
              record->vdp1_fence_events++;
              if (overwrite_busy) record->vdp1_fence_waits++;
              record->vdp1_edsr_entry_last = edsr_entry;
              if ((edsr_entry & SOURCEBOOT_VDP1_EDSR_CEF) != 0U)
                  record->vdp1_edsr_cef_entry_count++;
              record->vdp1_copr_entry_last = copr_entry;
              record->vdp1_copr_exit_last = sourceboot_vdp1_copr();
              record->vdp1_lopr_last =
                  (uint32_t)sourceboot_vdp1_ioregs()->lopr;
              record->vdp1_fence_ticks_last = 0U;
              record->vdp1_fence_iterations_last = 0U;
          }
  #endif
          if (overwrite_busy) {
              if (!sm64_saturn_frame_pipeline_transfer_deferred(
                      &sourceboot_frame_pipeline, generation))
                  sourceboot_fast3d.profile.pipeline_faults++;
              goto finish;
          }
  ```

  Leave the existing idle submit/poll path unchanged. Do not clear
  `sourceboot_vdp1_render_ready`, assign
  `sourceboot_vdp1_transfer_pending`, update the submitted generation, touch
  poison/quarantine state, call `sourceboot_frame_reuse_previous()`, or call a
  present/plot function on the busy branch. Remove normal and diagnostic tick
  accumulation from this gate; legacy accumulated fields remain zero from
  initialization because no W0 wait occurs.

- [x] **Step 5: Preserve profile ABI while correcting field meaning.**

  In `saturn_prenotify_profile.h`, keep every field in place and leave profile
  version 4 unchanged. Replace the blocking-fence comment with: events count
  pre-submit gate observations; `vdp1_fence_waits` is a historical ABI name
  counting busy deferrals; tick and iteration fields are retained for decoder
  compatibility but remain zero; EDSR/COPR/LOPR sample status around the one
  observation.

  In `capture_prenotification_profile.py`, add:

  ```python
  def vdp1_fence_summary(final: dict[str, Any]) -> dict[str, Any]:
      events = final["vdp1_fence_events"]
      deferrals = final["vdp1_fence_waits"]
      return {
          "events": events,
          "busy_deferrals": deferrals,
          "deferral_share_of_events": deferrals / events if events else None,
          "edsr_entry_last": final["vdp1_edsr_entry_last"],
          "edsr_cef_entry_count": final["vdp1_edsr_cef_entry_count"],
          "edsr_cef_entry_share": (
              final["vdp1_edsr_cef_entry_count"] / events if events else None
          ),
          "copr_entry_last": final["vdp1_copr_entry_last"],
          "copr_exit_last": final["vdp1_copr_exit_last"],
          "lopr_last": final["vdp1_lopr_last"],
      }
  ```

  Set `present_summary(...)["fence"]` to `vdp1_fence_summary(final)`. Keep raw
  v4 decoding intact, but remove every obsolete wait-cost key named in Step 2
  from the fence summary. Update the docstring: W0 performs no wait/spin; legacy
  tick fields decode for ABI compatibility but are not summarized as cost;
  EDSR CEF remains the meaningful draw-state observation.

- [x] **Step 6: Run all focused host gates without weakening assertions.**

  ```powershell
  $hostGate = 'HOST_CC_ENV=env -u GCC_EXEC_PREFIX -u COMPILER_PATH -u LIBRARY_PATH -u C_INCLUDE_PATH -u CPLUS_INCLUDE_PATH -u CFLAGS -u CPPFLAGS -u LDFLAGS TMP=D:/tmp TEMP=D:/tmp'
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-frame-pipeline $hostGate
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-vdp1-frame-bank $hostGate
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-vdp1-transfer-pipeline $hostGate
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-render-overlap-integration $hostGate
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-sourceboot-presentation-boundary $hostGate
  ```

  Expected: first four gates PASS. Run the fifth and record its actual output.
  If it matches the known pre-W0 literal-drift failure, leave it unchecked in
  the ledger with that reason. If it detects a W0-specific presentation-path
  change, fix that change before proceeding.

- [x] **Step 7: Update behavior documentation and commit Task 2 atomically.**

  Add a Keep a Changelog `[Unreleased]` `Changed` entry explaining: the old
  wait could not retire inside its own scheduler action because Yaul clears the
  flag on VBlank-OUT; W0 now retains the exact bank and retries on a later
  observed field; the tradeoff is a safe two-tick-bounded pause under permanent
  busy; transient busy recovers automatically; profile v4 remains compatible
  but its historical wait count is now a deferral count. Update phase plan,
  `STATE.md`, `ROADMAP.md`, and ledger to `host-contract-passed; target compile
  and live gates pending`, including exact tests
  and any known pre-existing gate failure.

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' diff --check
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' add src/port/saturn/sourceboot/main.c src/port/saturn/runtime/saturn_prenotify_profile.h tools/saturn/test_vdp1_transfer_pipeline_source.py tools/saturn/capture_prenotification_profile.py tools/saturn/test_capture_prenotification_profile.py Makefile.saturn.mk CHANGELOG.md STATE.md ROADMAP.md docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' commit -m "fix(saturn): defer busy VDP1 overwrites"
  ```

  Record the exact commit SHA in a documentation-only evidence commit. All
  tracked source must then be clean before either target build.

## Task 3: Build, stage, and observe the normal product candidate

**Files:**

- Create: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-throughput.json`
- Modify: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md`
- Modify: `docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md:155-175`
- Modify: `STATE.md:291-299`
- Modify: `ROADMAP.md:134-138`
- Create untracked build input: `build/saturn/w0/sourceboot-bob-demo-diag2-v1.json`

- [ ] **Step 1: Freeze and record the normal build inputs.**

  Require a clean tracked diff, record HEAD, and hash the normal profile:

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' rev-parse HEAD
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' status --short
  Get-FileHash -Algorithm SHA256 tools\saturn\profiles\sourceboot-bob-demo-v1.json
  ```

  Expected profile SHA-256:
  `a562c98760a893a474092799ba3d52b6feb9312afadb28c967271bd8da1c8b9c`.
  Record source state, profile path/hash, visible hypothesis (transient VDP1
  busy defers without hang or incomplete upload), and preserved baseline
  identity in the ledger before building.

- [ ] **Step 2: Build the exact normal-profile product once.**

  ```powershell
  .\tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 sourceboot SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=0 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0 SATURN_FEATURE_SEMANTIC_AUDIO=1 SATURN_RENDERER_PIPELINE=4 SATURN_SOURCEBOOT_LEVEL_ID=9 SATURN_SOURCEBOOT_AREA_ID=1 SATURN_SOURCEBOOT_ROUTE_ID=0 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_CAMERA_VARIANT=3 SATURN_ATAN2_VARIANT=2 SATURN_DIAGNOSTIC_MODE=0 SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0 SATURN_CART_MBIT=32 SATURN_SOURCE_CART_STAGE_SECTORS=8 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=2 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_DEMO_FRAGMENT_MODE=0 SATURN_DEMO_BSP_FRAGMENT_FLAT=0 SATURN_OBJECT_POOL_CAPACITY=208 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_FAST3D_Q16_TRACE=0
  ```

  Expected: a new `build/saturn/sourceboot/e2-bob-identity-id-*` whose manifest
  `git_revision` equals frozen HEAD and effective config has diagnostic mode 0.
  Do not select a build solely by directory timestamp.

- [ ] **Step 3: Verify memory and stage atomically under a new destination.**

  Resolve the build by manifest provenance and require exactly one match. Run:

  ```powershell
  $python = 'C:\Users\estee\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
  $head = git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' rev-parse HEAD
  $matches = @(Get-ChildItem build\saturn\sourceboot\e2-bob-identity-id-* -Directory | ForEach-Object {
      $manifest = Get-ChildItem $_.FullName -Filter saturn-release-manifest-v1.json -File | Select-Object -First 1
      if ($manifest) {
          $document = Get-Content -Raw $manifest.FullName | ConvertFrom-Json
          if ($document.provenance.git_revision -eq $head -and
              $document.effective_config.diagnostic_mode -eq 0) {
              [pscustomobject]@{ Directory = $_.FullName; Manifest = $manifest.FullName; Document = $document }
          }
      }
  })
  if ($matches.Count -ne 1) { throw "Expected one normal W0 manifest, found $($matches.Count)" }
  $normal = $matches[0]
  $normalElf = Join-Path $normal.Directory $normal.Document.outputs.elf.path
  $normalId = (Split-Path $normal.Directory -Leaf) -replace '^e2-bob-identity-', ''
  $normalStage = Join-Path (Join-Path $PWD 'releases\2026-08-17_w0-product') $normalId
  & $python tools\saturn\verify_sourceboot_memory_map.py verify --elf $normalElf --required-final-margin 0x1F00
  & $python tools\saturn\stage_saturn_release.py --manifest $normal.Manifest --destination $normalStage
  $stagedManifest = Join-Path $normalStage 'saturn-release-manifest-v1.json'
  $stagedElf = Join-Path $normalStage (Split-Path $normal.Document.outputs.elf.path -Leaf)
  $stagedCue = Join-Path $normalStage (Split-Path $normal.Document.outputs.cue.path -Leaf)
  ```

  The staging tool must create a new directory and must not overwrite an
  existing release. Record HWRAM end/free/margin and LWRAM end/free/margin;
  both declared floors must pass. Keep the resolved variables for Steps 4-5.

- [ ] **Step 4: Print launch identity before opening Ymir.**

  For the staged ELF, ISO, CUE, release manifest, and normal profile, print
  `Get-Item` full path and `LastWriteTimeUtc`, then `Get-FileHash -Algorithm
  SHA256`. Calculate the exact executable-window probe expected from the staged
  ELF. Run before the first emulator command:

  ```powershell
  $stagedIso = Join-Path $normalStage (Split-Path $normal.Document.outputs.iso.path -Leaf)
  Get-Item $stagedElf, $stagedIso, $stagedCue, $stagedManifest, tools\saturn\profiles\sourceboot-bob-demo-v1.json | Format-Table FullName, LastWriteTimeUtc
  Get-FileHash -Algorithm SHA256 $stagedElf, $stagedIso, $stagedCue, $stagedManifest, tools\saturn\profiles\sourceboot-bob-demo-v1.json
  & $python -c "from pathlib import Path; import sys; sys.path.insert(0, 'tools/saturn'); from capture_sourceboot_throughput import build_elf_identity_probe; print(build_elf_identity_probe(Path(sys.argv[1]))['expected_sha256'])" $stagedElf
  ```

  Record all values and the expected target identity-probe hash in the ledger.
  Confirm none resolves inside the preserved T2.17 artifact directory. This
  identity block must precede the first emulator command in the transcript.

- [ ] **Step 5: Run the earliest headless product observation.**

  ```powershell
  $python = 'C:\Users\estee\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
  & $python tools\saturn\capture_sourceboot_throughput.py --ymir 'D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe' --ipl 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin' --game $stagedCue --elf $stagedElf --release-manifest $stagedManifest --output docs\saturn\evidence\reports\w0-vdp1-overwrite-fence-throughput.json --startup-vblanks 4096 --warmup-ticks 30 --max-warmup-vblanks 20000 --max-vblanks 4096 --presentation-events 30 --timeout 1800
  ```

  Validate the JSON: exactly 29 intervals; mean and 1% low at least 4 FPS;
  every `presentation_generation_delta` equals 1; the latest coherent queue
  sample has `master_failures`, `slave_failures`, `qf`, `qq`, and `qw` equal
  zero; notification, retirement, and sequence counts are coherent. Record
  actual cadence and values, not only PASS. This normal profile does not expose
  a deferral count: label the busy branch host-proven and target-compiled but
  unproven-live unless diagnostic evidence later observes it.

  If boot, terrain, Mario, input path, camera, collision, actors, audio service,
  generation coherence, or the 4 FPS floor regresses, stop immediately. Make
  one narrow repair or restore the accepted baseline; do not continue to review
  or desktop acceptance with a failed product capture.

  **Observation acceptance correction (2026-08-17).** An `exec.run_for`
  boundary can leave the SH-2 stopped while the cadence writer owns its seqlock.
  The observer may retry only that strict transient decode by advancing at most
  two later stopped fields. It must not append an event or advance
  `last_presentation` on a torn sample, and it must retain the retry count and
  budget in failure diagnostics. This is a tool-only, final second-observation
  allowance: no target source/profile/staged artifact may change, and a second
  failed observation blocks W0 without diagnostic capture, review, or owner
  acceptance.

- [ ] **Step 6: Compile the diagnostic arm without changing tracked profile.**

  Copy the exact normal JSON to
  `build/saturn/w0/sourceboot-bob-demo-diag2-v1.json` as an untracked build
  input and change only `release_config.diagnostic_mode` from 0 to 2. Hash and
  record it. Build with the same full command from Step 2 plus
  `SOURCEBOOT_TARGET_PROFILE=build/saturn/w0/sourceboot-bob-demo-diag2-v1.json`
  and `SATURN_DIAGNOSTIC_MODE=2`. Verify manifest provenance and run the same
  memory-map command. Expected: a separate identity links successfully and
  both memory floors pass. This proves the `#if SATURN_DIAGNOSTIC_MODE != 0`
  arm compiles; it does not replace the normal candidate.

  Run `capture_prenotification_profile.py` on this diagnostic candidate only if
  the normal headless/manual observation records a suspected VDP1-busy deferral
  that cannot be classified from normal-profile evidence, or an independent
  reviewer explicitly requires target deferral telemetry. Otherwise record the
  branch as `host-proven; target-compiled; live occurrence unproven` and avoid an
  unnecessary second product capture.

- [ ] **Step 7: Record live state and commit Task 3 evidence.**

  Update the ledger with normal and diagnostic identities, hashes, build times,
  memory margins, exact capture command, actual JSON checks, and proof labels.
  Update the phase plan, `STATE.md`, and `ROADMAP.md` to `live-observed` only if
  Step 5 passed. Keep desktop owner acceptance and independent review unchecked.

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' diff --check
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' add docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-throughput.json docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md STATE.md ROADMAP.md
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' commit -m "docs(saturn): record W0 live product evidence"
  ```

## Task 4: Obtain independent review and close the desktop owner gate

**Files:**

- Create: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-desktop-launch.json`
- Modify: `docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md`
- Modify: `docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md:155-175`
- Modify: `STATE.md:291-299`
- Modify: `ROADMAP.md:134-138`

- [ ] **Step 1: Request an independent code review after live observation.**

  Invoke `superpowers:requesting-code-review`. Give the reviewer the design,
  this plan, Task 1 and Task 2 commit SHAs, the exact diff from pre-W0 to current
  code, host-gate results, target memory results, and the normal headless JSON.
  Require review of: exact-generation predicate, same-field epoch preservation,
  two-tick budget preservation, READY-bank ownership, no busy-branch DMA/replot,
  idle-path non-regression, diagnostic ABI semantics, and source-test strength.
  Record `PASS`, `PASS WITH FINDINGS`, or `FAIL` and every actionable finding in
  the ledger.

- [ ] **Step 2: Handle review without opening a repair campaign.**

  If review passes, proceed. If it finds a real W0 defect, make one narrow fix,
  update `CHANGELOG.md` and operational docs in the same behavior commit, rerun
  all Task 2 focused gates, create a new unique normal target identity, rerun the
  memory check and earliest headless observation, and request verification of
  the fix. This is causal attempt two. A second failed attempt or two elapsed
  hours without new live proof ends W0 for rollback/bypass selection.

- [ ] **Step 3: Reprint exact candidate identity and launch desktop Ymir.**

  Reprint staged CUE/ELF/manifest/profile full paths, build times, and SHA-256
  hashes immediately before launch. Resolve exactly one staged normal candidate
  whose manifest hash matches the committed Task 3 throughput evidence and
  whose diagnostic mode is 0, then run:

  ```powershell
  $python = 'C:\Users\estee\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
  $throughput = Get-Content -Raw docs\saturn\evidence\reports\w0-vdp1-overwrite-fence-throughput.json | ConvertFrom-Json
  $releaseMatches = @(Get-ChildItem releases\2026-08-17_w0-product -Directory | ForEach-Object {
      $manifest = Join-Path $_.FullName 'saturn-release-manifest-v1.json'
      if (Test-Path -LiteralPath $manifest) {
          $document = Get-Content -Raw $manifest | ConvertFrom-Json
          $manifestHash = (Get-FileHash -Algorithm SHA256 $manifest).Hash.ToLowerInvariant()
          if ($manifestHash -eq $throughput.release_manifest_sha256 -and
              $document.effective_config.diagnostic_mode -eq 0) {
              [pscustomobject]@{ Directory = $_.FullName; Manifest = $manifest; Document = $document }
          }
      }
  })
  if ($releaseMatches.Count -ne 1) { throw "Expected one staged normal W0 release, found $($releaseMatches.Count)" }
  $release = $releaseMatches[0]
  $stagedCue = Join-Path $release.Directory (Split-Path $release.Document.outputs.cue.path -Leaf)
  $stagedElf = Join-Path $release.Directory (Split-Path $release.Document.outputs.elf.path -Leaf)
  Get-Item $release.Manifest, $stagedCue, $stagedElf, tools\saturn\profiles\sourceboot-bob-demo-v1.json | Format-Table FullName, LastWriteTimeUtc
  Get-FileHash -Algorithm SHA256 $release.Manifest, $stagedCue, $stagedElf, tools\saturn\profiles\sourceboot-bob-demo-v1.json
  & $python tools\saturn\launch_ymir_desktop.py --release-manifest $release.Manifest --cue $stagedCue --ymir 'D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-sdl3\Release\ymir-sdl3.exe' --profile 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile' --launch --monitor-seconds 20 --output docs\saturn\evidence\reports\w0-vdp1-overwrite-fence-desktop-launch.json
  ```

  Capture screenshots or video only after gameplay is visibly rendered. Never
  show an older CUE as the current candidate.

- [ ] **Step 4: Ask the owner to judge the complete product gate.**

  Record explicit observations for boot, Mario visual fidelity and animation,
  controls, camera, collision, ordinary actor rendering, audible output, no
  tearing/flicker/partial frame/duplicate plot, and an observed cadence floor of
  at least 4 FPS. A permanent-busy frozen frame is not acceptable gameplay.
  Any regression blocks advancement and requires the narrow attempt-two loop or
  rollback; host tests and review cannot override it.

- [ ] **Step 5: Close documentation at the strongest actual proof level.**

  If the owner accepts, mark W0 `complete; owner-accepted` in the phase plan,
  `STATE.md`, `ROADMAP.md`, and ledger. Record the exact accepted identity and
  hashes, launch JSON, owner wording, review verdict, all tests run, known
  pre-existing gate failures, and whether busy deferral was observed live or
  remains host-proven only. If the owner has not answered, leave W0
  `live-observed; owner gate pending`; do not mark complete.

  ```powershell
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' diff --check
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' add docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-desktop-launch.json docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md STATE.md ROADMAP.md
  git -c safe.directory='D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery' commit -m "docs(saturn): close W0 overwrite fence gate"
  ```

  Before advancing to W1, require a clean tracked diff and exact agreement among
  Git HEAD, phase-plan status, ledger evidence, and accepted artifact identity.

## Completion Conditions

W0 is complete only when all conditions below are true:

- The unbounded normal wait and diagnostic spin are absent from the pre-submit
  path, and source contracts prevent their return.
- Exact-generation deferral, same-field `WAIT_VBLANK`, later-epoch retry,
  invalid-ack immutability, two-tick budget preservation, and eventual normal
  publication pass nominal and mutation tests.
- Busy starts neither destination transfer and leaves the exact bank `READY`;
  idle retains the existing serial command/Gouraud transport and publication.
- Profile v4 layout is unchanged, raw fields remain decodable, and evidence
  summaries report busy deferrals without inventing zero wait costs.
- Normal and diagnostic target configurations compile and pass declared memory
  floors; the accepted T2.17 artifact remains untouched.
- A new identity-bound normal candidate passes the earliest headless product
  observation and independent review.
- The owner accepts the new desktop artifact for boot, play, presentation,
  audio, and at least the accepted 4 FPS floor.
- The phase plan, ledger, `STATE.md`, `ROADMAP.md`, and `CHANGELOG.md` reflect
  exactly the proof achieved and every remaining limitation.
