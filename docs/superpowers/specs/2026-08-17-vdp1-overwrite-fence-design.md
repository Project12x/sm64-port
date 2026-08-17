# VDP1 Overwrite Fence Fail-Soft Design

**Date:** 2026-08-17
**Status:** Approved; implementation plan ready
**Active phase plan:**
[`2026-08-17-saturn-shaped-port-phase-plan.md`](../plans/2026-08-17-saturn-shaped-port-phase-plan.md)
**Implementation plan:**
[`2026-08-17-vdp1-overwrite-fence.md`](../plans/2026-08-17-vdp1-overwrite-fence.md)
**Scope:** W0 only: remove the unbounded VDP1 command-VRAM overwrite wait
without changing the normal frame-bank transfer, publication, or plotting path

## Decision

When a completed source command bank reaches its transfer-submission poll and
VDP1 is still plotting, the runtime will defer that exact bank and generation
until a later observed VBlank. It will not spin, submit DMA, poison the shared
VDP1 destination, quarantine the bank, or rebuild the generation. The current
presentation opportunity is consumed in the scheduler as previous-frame
retention, while the hardware naturally keeps the last complete framebuffer
visible. Sourceboot does not re-present that framebuffer on the busy branch.

The master returns to the scheduler, but deferral does not reopen simulation
credit. The existing normal-plus-recovery budget permits at most two
authoritative ticks while a generation remains unpublished. Once that budget
is exhausted, simulation, input consumption, and the source audio tick pause
until the retained generation publishes. This bounded-state safe pause is
deliberate:
it prevents hidden simulation from overrunning the two render-snapshot slots
or replacing the generation owned by the retained VDP1 bank.

The normal idle path is unchanged: an idle VDP1 permits the existing validated
command-prefix CPU-DMAC transfer followed by the existing validated Gouraud
SCU-DMA transfer. Completion and publication retain their current generation
checks.

This is the smallest Saturn-shaped correction because Yaul's VDP1 busy state
is retired by the VBlank-OUT synchronization path. Polling it repeatedly inside
one scheduler action cannot create the hardware event that clears it. The
scheduler already has an observed-field epoch and a safe previous-frame
retention contract, so retrying on the next epoch permits automatic recovery
without inventing another interrupt owner or transfer architecture.

## Product problem and causal hypothesis

W0 addresses one load-bearing risk in the accepted generic gameplay path:
`sourceboot_frame_poll_transfers()` performs an unbounded `vdp1_sync_wait()`
(or an equivalent diagnostic spin) before overwriting the sole resident VDP1
command arena. If VDP1 never becomes idle, the master SH-2 never returns to
input, simulation, camera, collision, audio service, or presentation
scheduling.

The causal hypothesis is narrow: replacing that software wait with one busy
observation plus an epoch-gated deferral removes the hang class while keeping
the existing safe-overwrite predicate and all transfer validation. A busy
result is not data corruption and is not a frame-bank fault; it is a missed
transfer opportunity.

The owner-visible invariant is therefore precise: no VDP1-busy observation may
trap the master in an unbounded software wait, start an unsafe destination
write, or expose an incomplete command list. A transient busy state must retry
and recover automatically after a later observed VBlank. A permanently busy
VDP1 is not claimed to remain playable: it retains the last complete frame and
enters the existing unpublished-generation pause with simulation advancement
capped rather than running ahead of owned render resources.

## Baseline and candidate identity

The preserved accepted gameplay artifact remains the T2.17 candidate. It is
never overwritten by W0:

| Identity item | Preserved value |
| --- | --- |
| Build identity | `id-c0352f297034f653` |
| ELF SHA-256 | `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd` |
| ISO SHA-256 | `49b68a07144b2b58ad6781dadb35404e038e0f9d75eaf7c354f8fa88e1d4c9cc` |
| CUE SHA-256 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| Release-manifest SHA-256 | `1d73590985a2702db2ccdbc93bb85e376248c9bd0808ea2aed5092938d3b35f3` |
| On-target identity-probe SHA-256 | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` |
| Headless cadence | 6.7181 FPS / 8.9310 VBlanks per presented frame; median 6.6667 FPS; 1% low 6.0 FPS |

The latest source-complete/headless candidate, `id-49894e8e2d3ea415`, is
comparison evidence rather than a replacement owner baseline. Its ELF is
`0a494205f2cc1e69178e9b757913267b15d2fe5ad974bb63cfdc1a09fa55ed62`
and its ISO is
`dc986500774538b560f3148cd214e4e272a2a4505e9fe2811ae591718b0eb350`.
It reported the same 6.7181 FPS / 8.9310-VBlank cadence but has not displaced
the accepted artifact.

W0 must produce a new build identity and uniquely named CUE. The launch record
must bind source state, profile, build time, ELF/ISO/CUE hashes, and the
on-target identity probe before Ymir opens.

## Scheduler contract

### New acknowledgement

The frame pipeline gains one explicit acknowledgement with the following
contract:

```c
bool sm64_saturn_frame_pipeline_transfer_deferred(
    sm64_saturn_frame_pipeline_t *pipeline, uint32_t generation);
```

It succeeds only when all of these facts hold:

- `generation` is nonzero and equals the active `render_generation`;
- `render_completed_valid` is true and its generation equals `generation`;
- `transfer_started` is true and `transfer_generation` equals `generation`;
- the submitting poll has a valid `transfer_poll_vblank` stamp equal to the
  pipeline's current `last_vblank_count`; and
- no completed-transfer record exists for `generation`.

On success it:

1. clears `transfer_started` and `transfer_submit_vblank_valid` so the next
   observed field can submit the retained generation;
2. preserves `render_active`, `render_generation`, the completed-render
   record, the queued snapshot, and the displayed generation;
3. preserves the current `transfer_poll_vblank` stamp, preventing a second
   submission attempt in the same observed field;
4. if a presentation opportunity is pending, consumes it and increments
   `previous_frame_reuse_count` exactly once;
5. does not increment reuse when no presentation opportunity is pending; and
6. reports the displayed generation as the scheduler's action generation.

It does not mark a transfer complete, publish a generation, free or quarantine
a bank, or alter the simulation-generation sequence. Invalid, stale,
duplicate, or post-completion acknowledgements return `false` without changing
state.

### Sourceboot busy branch

For the active generation, `sourceboot_frame_poll_transfers()` performs one
`vdp1_sync_busy()` observation before any DMA queue submission.

- If idle, it follows the existing `sm64_saturn_vdp1_frame_bank_submit_transfers()`
  path unchanged.
- If busy, it calls the new transfer-deferred acknowledgement and returns.
  The bank remains `READY`, retains its exact generation and source prefixes,
  and is retried only after another VBlank is observed.

The busy branch must not call `sourceboot_frame_reuse_previous()`. VDP1 is
already plotting; issuing another plot or framebuffer-change request would add
a second hardware action to a path whose purpose is to do nothing until the
current plot retires. "Reuse" here is a scheduler/accounting result: the
hardware continues showing the last complete framebuffer naturally.

If the acknowledgement unexpectedly rejects the current generation, the
runtime increments the existing pipeline fault evidence and returns without
submitting DMA or re-presenting. That is an internal state violation, unlike an
ordinary VDP1-busy deferral; it must not fall through to the idle submit path.

## Frame and generation sequence

For a completed render generation `N`:

1. The scheduler emits `POLL_TRANSFERS(N)` once in observed field `F` and
   stamps that field.
2. Sourceboot reads VDP1 busy once.
3. If busy, no destination write begins. The pipeline acknowledges deferral,
   counts one previous-frame reuse opportunity, and retains source bank `N` in
   `READY`.
4. The next scheduler step in `F` is `WAIT_VBLANK` with the displayed
   generation. It is not `POLL_TRANSFERS` or `REUSE_PREVIOUS_FRAME`, so
   sourceboot neither retries DMA nor calls `sourceboot_present_generation()`.
5. On the first eligible poll after a later observed VBlank, the same bank and
   generation `N` are reconsidered. Any remaining simulation credit may be
   consumed first according to the existing scheduler ordering.
6. Once VDP1 is idle, the existing command and Gouraud transfers start, retire,
   and publish `N` through the unchanged completion path.

A queued snapshot may remain queued during repeated deferrals. It does not
replace `N`, and no source bank can be reused until its existing ownership
contract retires it. Thus a late idle observation cannot upload a mixture of
generations.

### Bounded overload and snapshot behavior

Deferral does not reset `sim_ticks_this_presentation`, manufacture simulation
credit, or alter `SM64_SATURN_FRAME_MAX_SIM_TICKS`. Repeated busy fields can
consume only the existing normal-plus-recovery allowance for the unpublished
generation. This is load-bearing ownership behavior:

- the completed VDP1 bank for `N` remains `READY`;
- at most one later simulation snapshot is queued for promotion;
- the two-slot render-snapshot bank is not asked to preserve an untracked
  third generation; and
- once `N` publishes, the queued snapshot is promoted under the existing
  budget accounting and authoritative simulation can resume.

Input interrupts and VBlank handling may continue at the platform level, but
`game_loop_one_iteration()`—including input consumption and the source audio
tick—runs only on `RUN_SIM_TICK`. W0 therefore promises automatic recovery from
a transient busy fence and a safe pause with bounded simulation advancement
for a permanent hardware stall, not invisible indefinite gameplay behind a
frozen display. Reopening
the simulation budget would require a separately designed snapshot
drop/coalescing and frame-bank cancellation contract and is outside W0.

## Memory, ownership, and transport record

W0 allocates no new target buffer and changes no partition size. The latest
source-complete ELF (`id-49894e8e2d3ea415`) ends HWRAM at `0x060FB588`, leaving
`0x4A78` (19,064) bytes; the required final floor is `0x1F00` (7,936), leaving
`0x2B78` (11,128) bytes above that floor before W0's code-size change. LWRAM
ends at `0x002E89E0`, leaving `0x17620` bytes against its `0x4000` floor. The W0
target link must remeasure both margins.

| Resource | Physical regions, maximum, alignment | Lifetime and owner | Producer, consumer, and route | Deferral and failure atomicity |
| --- | --- | --- | --- | --- |
| VDP1 command staging bank | Source symbol `sourceboot_vdp1_cmdts[2][1664]` in HWRAM (`0x06000000`-`0x060FFFFF`); 53,248 bytes (`0xD000`) per bank, 32-byte aligned. Destination begins at VDP1 VRAM `0x05C00000`; submitted prefix is at most 1,664 commands / 53,248 bytes and must remain 32-byte compatible. | Two boot-initialized frame banks. Render service owns `BUILDING`; the frame-bank state machine owns `READY` through transfer/publication retirement. The destination is the single resident command arena and persists across frames. Reset/scene transition may reclaim only through the existing frame-bank initialization/retirement rules. | Render service produces the validated `READY` source prefix. CPU-DMAC copies it to `VDP1_VRAM(0)`. Publication arms the resident list; `vdp1_sync_render()`/VDP1 plotting is the first hardware consumer. | Busy deferral writes zero bytes and leaves the bank `READY`. A started or partial queue submission retains the existing poison/quarantine rules; W0 does not reinterpret it as a deferral. The old complete resident list and framebuffer remain authoritative. |
| Gouraud staging bank | Source symbol `sourceboot_gouraud_staging[2][1536]` in HWRAM; 12,288 bytes (`0x3000`) per bank, 8-byte aligned. Destination is `partitions.gouraud_base` inside VDP1 VRAM, after the validated command and texture partitions; prefix is at most 1,536 tables / 12,288 bytes. | Paired one-to-one with the command frame bank and generation. Render service owns production; frame-bank retirement owns reuse. Destination persists until a later successfully submitted generation replaces its prefix. | Render service produces tables. The existing serial transfer queue submits them through SCU DMA only after the command-prefix submission is accepted. The resident command list's Gouraud references are the first real consumer. | A busy command-arena deferral occurs before either transfer, so command and Gouraud destinations remain mutually consistent. Existing validation rejects misalignment, overlap, capacity excess, or unsafe source regions before submission. |
| Scheduler/frame-bank metadata | Pipeline state and transfer-target records remain in their existing LWRAM/HWRAM-linked sections; W0 adds no target array and no serialized package field. | Master SH-2 is the sole owner. Generation 0 remains reserved. State survives across presentation opportunities and is reset only by existing pipeline/frame-bank initialization. | Render completion produces the active generation; sourceboot is the transfer dispatcher; the pipeline and frame-bank validators are the first consumers. No slave SH-2 or package parser gains ownership. | Deferral preserves the exact render/bank generation. Stale, mismatched, duplicate, and post-completion acknowledgements fail closed and cannot make a bank publishable. |

Because the two VDP1 payloads share one generation and serial submission
contract, the busy check occurs before the first destination write. W0 cannot
defer between command and Gouraud DMA. Once submission starts, existing
transfer retirement and poison rules remain authoritative.

## Diagnostics and compatibility

The diagnostic path must not retain a hidden unbounded loop. W0 keeps the
existing profile layout and serialized report fields so diagnostic artifacts
remain decodable:

- `vdp1_fence_events` counts transfer-submit gate observations;
- `vdp1_fence_waits` counts busy deferrals (the historical field name is kept
  for ABI compatibility and documented as a deferral count);
- `vdp1_fence_iterations` is zero because no spin iterations occur; and
- fence/overwrite-wait tick fields are zero because the runtime performs no
  wait in this path.

The raw field names and profile version remain unchanged because the binary
layout does not change. The profile decoder and evidence labels must expose
`vdp1_fence_waits` as `busy_deferrals` and derive a
`deferral_share_of_events`; they must not publish zero ticks/iterations as a
mean wait cost. Source comments that still name `vdp1_sync_wait()` as the live
fence must be updated. `previous_frame_reuse_count` remains the product-level
count of missed presentation opportunities; it may include causes other than
VDP1 busy, so it is not a substitute for the deferral count.

## Reference-code record

| Upstream | Pin and license | Files inspected | Reuse mode and conclusion |
| --- | --- | --- | --- |
| Yaul/libyaul | Vendored commit `6012f79f237773378c8014e70d8998ad95a38d98`; MIT | `libyaul/scu/bus/b/vdp/vdp_sync.c` | Existing dependency plus direct API-semantics reuse. `vdp1_sync_busy()` reads the synchronization flag; `vdp1_sync_wait()` spins until the VBlank-OUT synchronization path clears it after draw end/framebuffer handling. This supports one busy observation followed by an epoch retry. Yaul copyright/license notices remain unchanged. |
| SlaveDriver engine | Commit `a8986591557b6e680550d3c23970284d3b38ff8f`; GPL-3.0-or-later | `DMA.C`, `SRUINS.C` | Pattern-only. It demonstrates a Saturn-native event-driven alternative: queued DMA begins from sprite-draw-end handling, and frame closure later waits for draw completion before display. W0 does not copy it because adopting that pattern would change interrupt, callback, and DMA ownership beyond the bounded fence fix. |

The SGL-style whole-frame `slSynch()` cadence found in the Sonic Z-Treme
reference is not adopted. It couples simulation and presentation to a blocking
global frame barrier and does not fit this port's explicit snapshot, transfer,
and previous-frame-reuse state machine.

An event-driven draw-end DMA trigger remains a possible measured follow-up if
natural busy-deferral telemetry proves material. It is not a W0 prerequisite
and cannot be introduced without a separate ownership and interrupt design.

## Adversarial review correction

Grok Build returned `APPROVE WITH CHANGES` on 2026-08-17. Its material claims
were independently checked against the current source rather than accepted by
authority. The review correctly identified that the original draft overstated
indefinite simulation progress, did not explicitly require a same-field
`WAIT_VBLANK` assertion, left the acknowledgement predicate ambiguous, and
needed stronger diagnostic/source-contract wording. Those corrections are in
this revision.

The review did not identify a reason to replace epoch deferral with sprite-end
DMA or a bounded in-function VBlank wait. The core design remains unchanged.
The owner approved the corrected written specification on 2026-08-17; the
linked implementation plan is the sole authorized W0 execution sequence.

## Verification design

### Host contracts

The implementation plan must make these checks executable:

1. Extend the frame-pipeline host test with a completed generation that is
   polled and deferred. It must prove that the displayed generation stays
   unchanged, the candidate generation remains complete, no transfer is
   complete, and reuse increments exactly once when a presentation was
   pending.
2. Prove that the next same-field action is `WAIT_VBLANK` for the displayed
   generation—not `POLL_TRANSFERS` or `REUSE_PREVIOUS_FRAME`—and that the busy
   sourceboot branch cannot reach `sourceboot_present_generation()`.
3. Advance VBlank once and prove that the exact retained generation becomes
   eligible for `POLL_TRANSFERS` again, then can complete and publish through
   the normal path.
4. Prove that wrong-generation, duplicate, pre-poll, and post-completion
   deferral acknowledgements fail without mutation.
5. Repeat deferrals across observed fields and prove that they neither reopen
   the two-tick unpublished-generation budget nor create an untracked snapshot;
   then prove eventual idle submission/publication resumes the normal budget.
6. Add targeted compile-time mutations that leave `transfer_started` set or
   clear the epoch stamp and require the nominal harness to reject both.
   Existing frame-pipeline mutation checks remain enabled.
7. Update the VDP1 transfer source contract to reject both
   `vdp1_sync_wait()` and `while (vdp1_sync_busy())` in the normal and
   diagnostic pre-submit paths. It must prove that the busy branch
   acknowledges deferral and returns before transfer submission or
   re-presentation, and that the existing frame-bank submit function remains
   the sole command/Gouraud writer path.
8. Run `verify-frame-pipeline` and `verify-vdp1-frame-bank` without weakening
   existing ownership, alignment, capacity, or generation assertions.

Passing these checks is `host-contract-passed`, not target proof.

### Target-equivalent and live gates

The minimum target gate is a compile and link through
`tools/saturn/with-msys-toolchain.ps1` with the established 27-variable
toolchain environment, followed by sourceboot memory-map verification against
the HWRAM/LWRAM floors. This establishes target-equivalent consumption and
updated memory margin, but remains `source-complete` until an identity-bound
Ymir run observes the real normal-level consumer.

The earliest live observation is the generic standard scene-package path in
BOB, using the normal registry, queue, residency, frame-bank, DMA, renderer,
and presentation route. After the gameplay stage is visibly rendered, observe:

- normal Mario animation, controls, camera, collision, normal actors, and
  audible output before and after any transient busy deferral;
- automatic recovery after a transient deferral, with no unbounded fence
  stall, torn/partial command list, duplicate plot, flicker, or
  mixed-generation frame;
- at least the accepted 4 FPS floor, with the T2.17/T2.25 6.7181-FPS cadence
  used as the regression comparison rather than a guaranteed exact result;
- exact build/profile/artifact identity in the launch and capture record; and
- diagnostic busy-deferral count plus previous-frame reuse count.

If natural gameplay records one or more busy deferrals and later publishes the
retained generation without a product regression, the transient fail-soft
branch is `live-observed`. If it records zero, the normal path may be
`live-observed`, but the busy branch remains `host-proven/unproven-on-target`;
the evidence must say so explicitly. A VDP1 that remains permanently busy does
not pass the owner product gate even though the bounded safe-pause contract is
working. W0 does not authorize an injected model, forced record,
object-specific branch, or synthetic normal-level substitute.

## Stop and rollback policy

W0 is one causal behavior change. After two implementation attempts or two
hours without a new live product observation, stop and compare against the
preserved accepted artifact. Choose a narrow correction, bypass, or rollback;
do not add another scheduler abstraction, wire format, transfer framework, or
parallel implementation lane.

Any regression in boot, Mario fidelity/animation, controls, camera, collision,
normal actor rendering, audio, frame completeness, or the 4 FPS floor blocks
advancement. The candidate is kept only if the identity-bound live observation
passes; otherwise the change is reverted or bypassed without touching the
accepted artifact.

## Anticipated implementation surface

- `src/port/saturn/runtime/saturn_frame_pipeline.h`: declare the exact
  generation acknowledgement.
- `src/port/saturn/runtime/saturn_frame_pipeline.c`: implement fail-closed
  deferral and epoch retention.
- `src/port/saturn/sourceboot/main.c`: replace both normal and diagnostic
  unbounded waits with one busy check and deferral.
- `tools/saturn/frame_pipeline_test.c` and its build/mutation rules: exercise
  the scheduler behavior and kill a same-field-retry mutation.
- `tools/saturn/test_vdp1_transfer_pipeline_source.py`: revise the source
  contract from "wait before submit" to "defer before submit when busy."
- `tools/saturn/capture_prenotification_profile.py` and profile/report
  documentation: expose deferral semantics without changing the binary layout
  or profile version.
- `CHANGELOG.md`, the active phase plan, `STATE.md`, and the active SDD
  ledger/evidence report: record behavior, rationale, exact tests, commit,
  review verdict, artifact identity, observation status, and all remaining
  gates in the same implementation transition.

The implementation plan may narrow this file list after source tracing, but it
may not broaden W0 into interrupt ownership, VRAM double-buffering, a new wire
format, or a new architecture sprint.

## Acceptance criteria

W0 is complete only when all of the following are true:

- no production or diagnostic sourceboot command-VRAM overwrite path contains
  an unbounded VDP1 busy wait;
- a busy observation starts no DMA, changes no VDP1 destination, and retains
  the exact `READY` bank/generation for a later observed field;
- the acknowledgement consumes a pending presentation exactly once; the next
  same-field action is `WAIT_VBLANK`, with no DMA retry or hardware
  re-presentation;
- repeated deferrals do not reopen the existing two-tick
  unpublished-generation budget or create an untracked snapshot, and a later
  successful publication resumes the normal pipeline;
- idle submission, DMA retirement, publication, poison, quarantine, alignment,
  capacity, and stale-generation contracts remain unchanged;
- nominal and mutation host contracts pass, target compile/link succeeds, and
  post-link HWRAM/LWRAM margins meet their existing floors;
- the active plan, ledger/evidence, state, and changelog state the actual proof
  level and remaining gates; and
- a new identity-bound Ymir candidate preserves the product gates and 4 FPS
  floor without replacing the accepted artifact.

A permanently busy VDP1 is specified as a safe pause with bounded simulation
advancement, not as playable degradation, and cannot advance the owner gate
until the normal hardware path recovers.

Only `live-observed` or owner acceptance advances the product milestone.
Host tests, source completeness, target compilation, memory margins, review,
and manifest equality are supporting evidence, not substitutes.
