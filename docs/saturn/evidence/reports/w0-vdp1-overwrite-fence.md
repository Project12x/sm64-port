# W0 VDP1 Overwrite Fence Execution Ledger

**Date opened:** 2026-08-17
**Status:** `active; W0.1/W0.2 host-contract-passed; normal target-compiled;
normal live-observed; diagnostic target compile blocked; review and owner gates pending`
**Active plan:**
[`2026-08-17-vdp1-overwrite-fence.md`](../../../superpowers/plans/2026-08-17-vdp1-overwrite-fence.md)
**Approved design:**
[`2026-08-17-vdp1-overwrite-fence-design.md`](../../../superpowers/specs/2026-08-17-vdp1-overwrite-fence-design.md)
**Phase plan:**
[`2026-08-17-saturn-shaped-port-phase-plan.md`](../../../superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md)

## Current proof statement

W0.1's scheduler acknowledgement and W0.2's sourceboot integration are
`host-contract-passed`; W0.2 is not source-complete until the normal and
diagnostic target-equivalent consumers compile. W0.2 performs one busy observation,
defers the exact `READY` generation before any destination write, and records
deferrals without retaining a blocking diagnostic spin. Target compilation,
CUE construction, and Ymir observation remain pending. The accepted T2.17
gameplay artifact remains the only preserved product baseline.

| Work item | State | Evidence | Remaining gate |
| --- | --- | --- | --- |
| W0.1 scheduler acknowledgement | `host-contract-passed` | Exact-generation acknowledgement implemented; nominal scheduler, eight mutations, and VDP1 frame-bank ownership checks pass | Sourceboot integration and target gates remain |
| W0.2 sourceboot deferral and diagnostics | `host-contract-passed; normal target-compiled` | RED source contract failed on missing busy branch; GREEN `verify-frame-pipeline`, `verify-vdp1-frame-bank`, `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration` passed; normal diagnostic-mode-0 link and both floors passed | Diagnostic target-equivalent compile; normal live proof; presentation-boundary literal drift remains recorded below |
| W0.3 normal build and live product observation | `active; first observation inconclusive` | Unique normal candidate `id-e8720d58595d9a62`, its memory floors, staging, and identity binding are recorded; attempt 1 reached generation 50 with coherent queues but stopped on an unstable cadence seqlock | One bounded tool-only retry against the same staged candidate; no target rebuild or behavior change |
| W0.4 review and owner gate | `planned` | Review and desktop acceptance criteria recorded | Independent verdict, identity-bound desktop launch, explicit owner judgment |

## Preserved accepted product baseline

W0 must not overwrite, rebuild into, rename, or present this artifact as
current work:

| Identity item | Preserved value |
| --- | --- |
| Build identity | `id-c0352f297034f653` |
| ELF SHA-256 | `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd` |
| ISO SHA-256 | `49b68a07144b2b58ad6781dadb35404e038e0f9d75eaf7c354f8fa88e1d4c9cc` |
| CUE SHA-256 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| Release-manifest SHA-256 | `1d73590985a2702db2ccdbc93bb85e376248c9bd0808ea2aed5092938d3b35f3` |
| Target identity-probe SHA-256 | `d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` |
| Accepted cadence | Mean 6.7181 FPS; median 6.6667 FPS; 1% low 6.0 FPS; 8.9310 VBlanks/frame |

The latest source-complete/headless comparison is
`id-49894e8e2d3ea415`, ELF
`0a494205f2cc1e69178e9b757913267b15d2fe5ad974bb63cfdc1a09fa55ed62`,
ISO `dc986500774538b560f3148cd214e4e272a2a4505e9fe2811ae591718b0eb350`.
It measured the same cadence but is not owner-accepted and does not replace
the baseline.

## Planning record

| Item | Record |
| --- | --- |
| Recovery branch | `saturn/recovery` |
| Design approval HEAD | `9d6c98170f7e97219cb7e26d5bce20b8360f3d79` |
| Design commit | `9d6c9817 docs(design): correct VDP1 deferral recovery semantics` |
| Implementation-plan commit | `530578cc715debc52db295f9617f715dc14744f9 docs(plan): add W0 VDP1 overwrite fence execution plan` |
| Grok Build verdict | `APPROVE WITH CHANGES`, independently reconciled on 2026-08-17 |
| Owner design decision | Corrected epoch-deferral design approved on 2026-08-17 |
| Planning tests | PASS: 4 tasks/26 checkbox steps enumerated; required API and mutation types consistent; local Markdown links resolve; placeholder scan found no unresolved planning marker in the new plan/ledger; `git diff --cached --check` passed before commit |

### W0.2 acceptance-test correction

Immutable base `112cd9f8ce0b54229578b6c0e5741ba64e1089a0` proves two stale
A8 transfer-runtime literals were pre-W0 harness drift: the persistent poison
flag already carries `SOURCEBOOT_LWRAM_STATE`, and the exact
`.command_vram = (void *)(uintptr_t)VDP1_VRAM(0),` initializer already lives in
`sourceboot_post_cart_init()` rather than `main()`. The test now accepts the
annotation while still requiring a static persistent bool and examines the
actual initializer. No runtime code was altered for these stale assertions.

## Memory, ownership, and transport debt record

W0 adds no target buffer, changes no serialized package field, and changes no
physical partition. The latest comparison ELF ends HWRAM at `0x060FB588`,
leaving `0x4A78` bytes and `0x2B78` above the required `0x1F00` final floor.
LWRAM ends at `0x002E89E0`, leaving `0x17620` bytes against its `0x4000`
floor. Both margins must be remeasured for normal and diagnostic W0 links.

| Resource | Physical regions, maximum, alignment | Lifetime and owner | Producer, first consumer, route | Failure atomicity and stale generation |
| --- | --- | --- | --- | --- |
| Command bank | `sourceboot_vdp1_cmdts[2][1664]` in HWRAM; `0xD000`/53,248 bytes per bank; 32-byte aligned. Destination VDP1 VRAM begins `0x05C00000`; maximum 1,664 commands. | Two boot-initialized banks. Render service owns `BUILDING`; frame-bank state owns `READY` through retirement. | Render emits a validated prefix; CPU-DMAC copies it to `VDP1_VRAM(0)`; resident-list publication and VDP1 plotting consume it. | Busy deferral writes zero bytes and leaves the exact generation `READY`. Started/partial submission keeps existing poison and quarantine behavior. |
| Gouraud bank | `sourceboot_gouraud_staging[2][1536]` in HWRAM; `0x3000`/12,288 bytes per bank; 8-byte aligned. Destination is validated `partitions.gouraud_base`. | Paired one-to-one with command bank and generation; frame-bank retirement owns reuse. | Render emits tables; serial SCU-DMA follows accepted command submission; resident Gouraud references consume it. | Busy is checked before both writes, so no command/Gouraud mismatch is created. Existing validation remains authoritative after submission begins. |
| Scheduler metadata | Existing pipeline and frame-bank records in current linked state; no new array or wire field. | Master SH-2 only; generation zero reserved; existing initialization owns reset. | Render completion produces active generation; sourceboot dispatches; pipeline/frame-bank validators consume. | Exact, nonzero, current-poll generation required. Stale, duplicate, pre-poll, or post-completion deferral fails without mutation. |

The first real W0 consumer is
`sourceboot_frame_poll_transfers()` on the normal generic BOB path. The earliest
live exercise is the identity-bound normal-profile headless throughput capture
specified in Task 3, immediately after the minimum target build and memory
check.

## Reference-code record

| Upstream | Pin | License | Files inspected | Reuse mode and conclusion |
| --- | --- | --- | --- | --- |
| Yaul/libyaul | `6012f79f237773378c8014e70d8998ad95a38d98` | MIT | `libyaul/scu/bus/b/vdp/vdp_sync.c` | Existing dependency/API-semantics reuse. Busy state clears through VBlank-OUT synchronization, supporting one observation and later-epoch retry. Existing notices remain unchanged. |
| SlaveDriver | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later | `DMA.C`, `SRUINS.C` | Pattern-only. Sprite-draw-end DMA confirms an event-driven Saturn alternative but is not copied because it would change interrupt/DMA ownership beyond W0. |

## Commits and review

| Commit | Purpose |
| --- | --- |
| `4b66543e850e0b601ea4ec2cfc22062853da5771` | `feat(saturn): acknowledge deferred VDP1 transfers` — rewritten W0.1 implementation with the mandatory same-commit changelog entry |
| `da41248f40dd4236147f3c3ea4dbb52225772c35` | `fix(saturn): defer busy VDP1 overwrites` — W0.2 sourceboot busy deferral, profile-summary correction, focused contracts, approved A8 acceptance-test repair, and same-commit behavior documentation |

Independent review is not requested until after the first identity-bound live
product observation.

## Tests and observations

### Task 3 pre-build identity freeze (2026-08-17)

Before invoking the normal target build, recovery worktree `saturn/recovery`
was at immutable task base `6697a3ce1c048d007a8d950d18fa1c653ad29923` with
no tracked diff. The only pre-existing untracked paths were `.msys-home/`,
`releases/`, and the two historical A9 desktop-launch logs; they are preserved
and excluded from this task. The exact normal profile is
`tools/saturn/profiles/sourceboot-bob-demo-v1.json`, SHA-256
`a562c98760a893a474092799ba3d52b6feb9312afadb28c967271bd8da1c8b9c`.

The W0 hypothesis being observed is: a transient VDP1 busy state immediately
defers its exact ready generation and later retries it, without a hang or an
incomplete upload. The owner-accepted T2.17 artifact remains immutable:
`id-c0352f297034f653` (CUE SHA-256
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`);
the normal build must create and stage a new identity outside that artifact.

W0.1 scheduler gates run on 2026-08-17. The mandated RED command first failed
at the compiler with an implicit declaration of
`sm64_saturn_frame_pipeline_transfer_deferred`; an initial sandbox invocation
stopped earlier because `D:\tmp` was not writable, which was environmental.
The approved-toolchain rerun produced the intended RED. The GREEN scheduler
gate printed `frame pipeline contract: PASS` and all eight mutation checks were
caught, including `frame pipeline deferral leaves transfer started mutation`
and `frame pipeline deferral clears epoch stamp mutation`. The VDP1 frame-bank
gate compiled and passed its four checks. Planned gates, in order:

1. Scheduler RED, nominal test, and eight mutation checks.
2. VDP1 frame-bank ownership gate.
3. Sourceboot transfer RED and corrected source/telemetry gates.
4. Render-overlap integration and presentation-boundary check.
5. Normal and diagnostic target link plus memory-map validation.
6. Unique normal CUE headless Ymir product observation.
7. Independent code review.
8. Identity-bound desktop Ymir owner observation.

### W0.2 exact host evidence

The Task 2 RED command was `with-msys-toolchain.ps1 mingw32-make -f
Makefile.saturn.mk verify-vdp1-transfer-pipeline` with the documented
sanitized `HOST_CC_ENV` and `TMP/TEMP=D:/tmp`. After the environmental sandbox
attempt could not write `D:\tmp`, the approved-toolchain rerun reached the
intended contract failure: `missing pattern
'if\\s*\\(\\s*overwrite_busy\\s*\\)'` after the existing DMA and frame-bank
transfer checks completed.

The GREEN run used that same environment for these commands:

1. `verify-frame-pipeline` — PASS, including the existing mutation fixtures
   and both deferral mutations.
2. `verify-vdp1-frame-bank` — PASS; 4 source tests.
3. `verify-vdp1-transfer-pipeline` — PASS; 3 W0 source tests, 2 new telemetry
   summary tests, 7 repaired A8 runtime-contract tests, and 5 A9 coherence
   tests, after the existing DMA/frame-bank executable checks.
4. `verify-render-overlap-integration` — PASS, nominal integration plus all
   existing mutation fixtures.
5. `verify-sourceboot-presentation-boundary` — known pre-W0 literal-drift
   failure, not a W0-specific result: all 7 tests stop at `bootstrap must
   contain exactly one null-snapshot VDP2 begin` before their W0 path checks.

Pre-commit scoped review found no actionable W0.2 or A8-correction issue; its
local wrapper could not rerun tests, so the commands above remain the actual
test evidence. Independent review remains deferred until the first
identity-bound live product observation.

### Task 3 first live observation and observer correction (2026-08-17)

The normal target build is `id-e8720d58595d9a62` from product source
`6697a3ce1c048d007a8d950d18fa1c653ad29923`, diagnostic mode 0, and normal
profile hash `a562c98760a893a474092799ba3d52b6feb9312afadb28c967271bd8da1c8b9c`.
It links with HWRAM free `0x4A58` (margin `0x2B58` over the required `0x1F00`)
and LWRAM free `0x17620` (floor `0x4000`), and was atomically staged outside
the immutable T2.17 artifact.

Attempt 1 is preserved, not overwritten, as
`w0-vdp1-overwrite-fence-throughput-attempt-1-failed.json` (SHA-256
`abfaae9267cf8293cbfe2a6f431a4c3b7cbf77c3df530103b2d0a866d09f8a`). It stopped
after 21/30 events with `cadence trace seqlock is not stable`. Its last stable
cadence sequence was 98/presentation generation 49; runtime reached generation
50 with `notify=qn=qr=retired=50`, all queues zero, and both failure counters
zero. This is a capture-race hypothesis, not a live product PASS or a proven
absence of regression.

Controller authorized one final observer-only retry. The observer now retries
only that strict seqlock error for at most two later stopped fields, retaining
the old presentation edge until it has a coherent cadence record. It leaves
`decode_cadence_trace` strict and changes no target/runtime/product source;
the staged candidate remains from `6697a3ce` while this observer revision is
newer. Focused RED/GREEN tests cover transient torn-then-stable success and
persistent torn bounded failure. The full tool module has one unrelated local
ACL failure in `test_release_binding_retains_verified_snapshot_after_source_mutation`
while attempting to lock `C:\\Users\\estee`; the three cadence-focused tests pass.

The final identity-bound retry passed on the same staged candidate: evidence
SHA-256 `2110a8138ad9a628c70658ca5c3c176504b4caa9f77fd88cdea9e7d44ef7d6ed`,
created `2026-08-17T19:51:46Z`, with 30 events/29 intervals, mean
6.6923077 FPS, 1% low 6.0 FPS, every generation delta 1, target/probe matches,
and coherent terminal `notify=qn=qr=retired=58` with failures and qf/qq/qw
all zero. One transient cadence retry was used. This is `live-observed`; it is
not desktop owner acceptance.

The separate untracked diagnostic profile changed only mode 0 to 2 (SHA-256
`bf4fed164b24fead55275aefcf750cbdddb85d46d91fc11c97de52ba477f5ca6`). Two
direct elevated MSYS Bash attempts, using the normal command tuple plus only
that profile and `SATURN_DIAGNOSTIC_MODE=2`, reached recursive
`identity-assets`, then had no worker, new identity, CPU/file progress, or
manifest across a bounded sample. Both stalled process trees were recorded and
terminated; no diagnostic target link/memory/capture claim is made.

## Remaining gates

- [x] Scheduler deferral host contract, two new mutations, and VDP1 frame-bank
  ownership gate pass.
- [x] Sourceboot busy branch has host-contract evidence for no wait, DMA,
  replot, or bank ownership change; target-equivalent compile is still pending.
- [x] `verify-frame-pipeline`, `verify-vdp1-frame-bank`,
  `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration`
  pass without weakened assertions. `verify-sourceboot-presentation-boundary`
  remains unchecked: all seven tests fail at the known pre-W0 bootstrap literal
  `bootstrap must contain exactly one null-snapshot VDP2 begin`, before any W0
  path assertion runs.
- [ ] Normal and diagnostic target configurations link and retain memory floors.
  Normal mode 0 links and passes both floors; diagnostic mode 2 is blocked at
  the repeated environment/build handoff and has no link or memory evidence.
- [x] A unique W0 normal artifact is staged with exact identity hashes.
- [x] Earliest headless product observation passes the 4 FPS floor and generic
  gameplay coherence checks on the exact staged normal candidate.
- [ ] Independent code review passes or its one narrow repair is re-observed.
- [ ] Owner observes and accepts boot, play, fidelity, camera, collision,
  ordinary actors, audio, presentation, and cadence on the exact W0 artifact.
