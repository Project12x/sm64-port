# W0 VDP1 Overwrite Fence Execution Ledger

**Date opened:** 2026-08-17
**Status:** `active; W0.1/W0.2 source-complete and host-contract-passed; target and live gates pending`
**Active plan:**
[`2026-08-17-vdp1-overwrite-fence.md`](../../../superpowers/plans/2026-08-17-vdp1-overwrite-fence.md)
**Approved design:**
[`2026-08-17-vdp1-overwrite-fence-design.md`](../../../superpowers/specs/2026-08-17-vdp1-overwrite-fence-design.md)
**Phase plan:**
[`2026-08-17-saturn-shaped-port-phase-plan.md`](../../../superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md)

## Current proof statement

W0.1's scheduler acknowledgement and W0.2's sourceboot integration are
`source-complete; host-contract-passed`. W0.2 performs one busy observation,
defers the exact `READY` generation before any destination write, and records
deferrals without retaining a blocking diagnostic spin. Target compilation,
CUE construction, and Ymir observation remain pending. The accepted T2.17
gameplay artifact remains the only preserved product baseline.

| Work item | State | Evidence | Remaining gate |
| --- | --- | --- | --- |
| W0.1 scheduler acknowledgement | `source-complete; host-contract-passed` | Exact-generation acknowledgement implemented; nominal scheduler, eight mutations, VDP1 frame-bank ownership checks, and finalized commit ledger pass | Sourceboot integration and target gates remain |
| W0.2 sourceboot deferral and diagnostics | `source-complete; host-contract-passed` | RED source contract failed on missing busy branch; GREEN `verify-frame-pipeline`, `verify-vdp1-frame-bank`, `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration` passed | Normal/diagnostic target compile, memory floors, identity-bound live observation; presentation-boundary literal drift remains recorded below |
| W0.3 normal build and live product observation | `planned` | Preserved baseline and exact build/capture recipe recorded | Unique build, memory check, atomic staging, identity-bound headless Ymir capture |
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
| W0.2 behavior commit | Pending the atomic sourceboot/documentation commit; its exact SHA is recorded only by the permitted documentation-only follow-up |

Independent review is not requested until after the first identity-bound live
product observation.

## Tests and observations

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

## Remaining gates

- [x] Scheduler deferral host contract, two new mutations, and VDP1 frame-bank
  ownership gate pass.
- [x] Sourceboot busy branch is source-complete with no wait, DMA, replot, or
  bank ownership change.
- [x] `verify-frame-pipeline`, `verify-vdp1-frame-bank`,
  `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration`
  pass without weakened assertions. `verify-sourceboot-presentation-boundary`
  remains unchecked: all seven tests fail at the known pre-W0 bootstrap literal
  `bootstrap must contain exactly one null-snapshot VDP2 begin`, before any W0
  path assertion runs.
- [ ] Normal and diagnostic target configurations link and retain memory floors.
- [ ] A unique W0 normal artifact is staged with exact identity hashes.
- [ ] Earliest headless product observation passes the 4 FPS floor and generic
  gameplay coherence checks.
- [ ] Independent code review passes or its one narrow repair is re-observed.
- [ ] Owner observes and accepts boot, play, fidelity, camera, collision,
  ordinary actors, audio, presentation, and cadence on the exact W0 artifact.
