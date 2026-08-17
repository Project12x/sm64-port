# W0 VDP1 Overwrite Fence Execution Ledger

**Date opened:** 2026-08-17
**Status:** `active; W0.1/W0.2 host-contract-passed; normal live-observed;
diagnostic target-compiled; independent review PASS WITH FINDINGS; desktop
launched; owner gate pending`
**Active plan:**
[`2026-08-17-vdp1-overwrite-fence.md`](../../../superpowers/plans/2026-08-17-vdp1-overwrite-fence.md)
**Approved design:**
[`2026-08-17-vdp1-overwrite-fence-design.md`](../../../superpowers/specs/2026-08-17-vdp1-overwrite-fence-design.md)
**Phase plan:**
[`2026-08-17-saturn-shaped-port-phase-plan.md`](../../../superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md)

## Current proof statement

W0.1's scheduler acknowledgement and W0.2's sourceboot integration are
`host-contract-passed`. The normal diagnostic-mode-0 candidate is
`live-observed`; the diagnostic-mode-2 arm is `target-compiled` at a
tool/docs-only descendant revision, not artifact-identical to that frozen normal
candidate. W0.2 performs one busy observation, defers the exact `READY`
generation before any destination write, and records deferrals without retaining
a blocking diagnostic spin. Independent review returned `PASS WITH FINDINGS`
with no critical or important issue and authorized the desktop owner launch;
its two review-evidence findings are closed without a product-code change. The
accepted T2.17 gameplay artifact remains the only owner-accepted product
baseline; owner observation remains open. The exact normal candidate launched
and remained alive through a 20-second desktop monitor; this is process proof,
not a visual, audio, control, collision, or cadence acceptance claim.

| Work item | State | Evidence | Remaining gate |
| --- | --- | --- | --- |
| W0.1 scheduler acknowledgement | `host-contract-passed` | Exact-generation acknowledgement implemented; nominal scheduler, eight mutations, VDP1 frame-bank ownership checks, and full-state immutability on wrong/duplicate acknowledgement pass | Normal product observation and owner gate are separately recorded below |
| W0.2 sourceboot deferral and diagnostics | `host-contract-passed; normal live-observed; diagnostic target-compiled` | RED source contract failed on missing busy branch; GREEN `verify-frame-pipeline`, `verify-vdp1-frame-bank`, `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration` passed; source contract now proves exactly one busy observation and no busy-branch ownership mutation; normal mode 0 and diagnostic mode 2 links both passed memory floors | Known pre-W0 presentation-boundary literal drift; owner gate |
| W0.3 normal build and live product observation | `complete at evidence level` | Normal `id-e8720d58595d9a62` staged and passed the bounded retry: 30 events/29 intervals, 6.6923 mean, 6.0 1% low, coherent queues and delta 1; diagnostic `id-5f27c53e9ae67c9c` linked/sealed and passed both floors | No diagnostic capture: condition absent. Owner/review gates remain separately open. |
| W0.4 review and owner gate | `review PASS WITH FINDINGS; desktop launched; owner gate pending` | Review found no critical/important issue; `id-e8720d58595d9a62` remained alive after its 20-second identity-bound desktop monitor | Explicit owner judgment; no launch-time visual/audio inference |

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
LWRAM ends at `0x002E95E0`, leaving `0x16A20` bytes against its `0x4000`
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
| `facb9db7` | `fix(saturn): bound cadence observation retries` — tool-only bounded transient seqlock observation retry; no product source/profile/artifact change |
| `ebee7e54` | `docs(saturn): record W0 live product evidence` — normal candidate/stage and live-observation evidence |
| `d347cf61` | `docs(saturn): clarify W0 diagnostic evidence` — documentation/evidence descendant used when the diagnostic arm was built |
| `e13baf1e9832671efe7c81be4eb29d5b7fc26438` | `test(saturn): close W0 review evidence findings` — corrected LWRAM maximum, focused contracts, review record, and status reconciliation; no product-code change |
| `6044ba147c344f7379923ba1b41cbe6c809546a7` | `docs(saturn): record W0 desktop launch` — durable pre-launch and launch identity/process evidence; owner gate remains pending |
| `aa4b8ac5f3353afb84150c0270d0ec78cef7c05d` | `docs(saturn): record W0 owner partial evidence` — verbatim owner cadence, visual, and audio evidence without inferring acceptance |

Independent review completed after the first identity-bound live product
observation; see the review-evidence transition below.

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
and LWRAM free `0x16A20` (floor `0x4000`), and was atomically staged outside
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

The W0 busy branch is **`host-proven; target-compiled; live occurrence
unproven`**: normal live observation proves the product path, but its normal
profile exposes no deferral counter and did not classify a busy occurrence.

### Normal candidate identity, launch ordering, and capture command

The normal product candidate is
`releases/2026-08-17_w0-product/id-e8720d58595d9a62` from frozen source
`6697a3ce1c048d007a8d950d18fa1c653ad29923`, diagnostic mode 0. Its staged
ELF, ISO, CUE, and release manifest all have LastWriteTimeUtc
`2026-08-17T19:41:10Z`; their SHA-256 values are, respectively,
`021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`,
`186f8734944941a6fe2f9f2455483871007e8f11a13a7e86966235be13729846`,
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`, and
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`.
The profile path is `tools/saturn/profiles/sourceboot-bob-demo-v1.json`
(LastWriteTimeUtc `2026-08-16T23:24:20Z`, SHA-256
`a562c98760a893a474092799ba3d52b6feb9312afadb28c967271bd8da1c8b9c`), and
the expected ELF executable-window probe is
`d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5`.
The recoverable normal completion time is that staged-manifest timestamp; no
durable transcript retains the original build start or duration.

The identity block was printed immediately before the final emulator command in
the execution transcript. That raw combined console transcript was not saved as
a durable file, so this ledger does not fabricate a stronger replayable ordering
claim. The canonical capture JSON instead binds the named staged CUE/ELF/release
manifest, their hashes, the expected probe, and
`created_utc=2026-08-17T19:51:46.491200+00:00`. The exact command was:

```powershell
& C:\Users\estee\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe tools\saturn\capture_sourceboot_throughput.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --game releases\2026-08-17_w0-product\id-e8720d58595d9a62\sm64-saturn-sourceboot-e2.cue --elf releases\2026-08-17_w0-product\id-e8720d58595d9a62\obj\sm64-saturn-sourceboot-e2.elf --release-manifest releases\2026-08-17_w0-product\id-e8720d58595d9a62\saturn-release-manifest-v1.json --output docs\saturn\evidence\reports\w0-vdp1-overwrite-fence-throughput.json --startup-vblanks 4096 --warmup-ticks 30 --max-warmup-vblanks 20000 --max-vblanks 4096 --presentation-events 30 --timeout 1800
```

The separate untracked diagnostic profile changed only mode 0 to 2 (SHA-256
`bf4fed164b24fead55275aefcf750cbdddb85d46d91fc11c97de52ba477f5ca6`). Two
direct elevated MSYS Bash attempts, using the normal command tuple plus only
that profile and `SATURN_DIAGNOSTIC_MODE=2`, reached recursive
`identity-assets`, then had no worker, new identity, CPU/file progress, or
manifest across a bounded sample. Both stalled process trees were recorded and
terminated; no diagnostic target link/memory/capture claim is made.

Independent-review fix round 1 isolated `identity-assets` directly at
2026-08-17T20:17:23Z, but that historical command omitted the full override
tuple and its stderr selected default camv1/stage16 paths. It is not diagnostic
proof. The original outer recursive-make failure is therefore correctly
attributed to its *post-assets handoff*, not to identity-assets itself.

### Task 3 diagnostic arm: exact-tuple direct handoff (2026-08-17)

Round 2 used the entire normal override tuple, `SATURN_DIAGNOSTIC_MODE=2`, the
untracked mode-2 profile, scene generation 15, development release mode, and
the recovery worktree manifest root for every direct inner-Make invocation.
The directly invoked `identity-assets` and `identity-discovery` stages completed
with durable stdout/stderr logs and camv3/stage8/diag2 paths. A first
`print-identity-tag` query that omitted the tuple was safely rejected by the
profile equality check and produced no artifact; its corrected full-tuple query
returned `id-5f27c53e9ae67c9c`. The existing Makefile.saturn.mk 272--288
handoff was then executed directly: tagged sourceboot build followed by
`verify-sealed-inputs seal-release`. This is a narrow manual handoff bypass of
the historical outer post-assets process, not a new build abstraction.

The durable exact-stage log intervals (UTC) are assets
`20:30:40--20:33:13`, discovery `20:34:17--20:35:41`, tagged build
`20:38:09--20:44:35`, and sealed-input verification/release
`20:46:42--20:49:34`. The logged commands are exactly the full-tuple scripts
`task-3-diag-identity-assets.sh`, `task-3-diag-identity-discovery.sh`,
`task-3-diag-sealed-build.sh`, and `task-3-diag-seal-release.sh` under
`.superpowers/sdd/2026-08-17-vdp1-overwrite-fence/`, launched with
`C:\\msys64\\usr\\bin\\bash.exe`; the latter two contain, respectively,
`timeout 900s make SOURCEBOOT_SEALED_IDENTITY=id-5f27c53e9ae67c9c <full tuple>`
and `timeout 300s make verify-sealed-inputs seal-release
SOURCEBOOT_SEALED_IDENTITY=id-5f27c53e9ae67c9c <same full tuple>`.

The resulting diagnostic manifest is
`build/saturn/sourceboot/e2-bob-identity-id-5f27c53e9ae67c9c/saturn-release-manifest-v1.json`
(SHA-256 `9cc86c488238302f5739c4ceae055a50042241ff9f2f611c04d455f336b39ef8`,
LastWriteTimeUtc `2026-08-17T20:49:34Z`). Its ELF is
`build/saturn/sourceboot/e2-bob-identity-id-5f27c53e9ae67c9c/obj/sm64-saturn-sourceboot-e2.elf`
(SHA-256 `aff9145af07bda5c8afceaf2ab8b1fdf6e2fafdb4dba26f1d7ab80cd901207cb`,
LastWriteTimeUtc `2026-08-17T20:40:48Z`). The mode-2 profile is
`build/saturn/w0/sourceboot-bob-demo-diag2-v1.json` (SHA-256
`bf4fed164b24fead55275aefcf750cbdddb85d46d91fc11c97de52ba477f5ca6`), changing
only diagnostic mode from the normal profile.

The manifest provenance is `d347cf612133e046afee4d498a8d3c367ef707b7`, not
the frozen normal product source `6697a3ce1c048d007a8d950d18fa1c653ad29923`.
The durable comparison `git diff --name-status 6697a3ce..d347cf61` enumerated
exactly these tracked paths:

```text
M CHANGELOG.md
M ROADMAP.md
M STATE.md
A docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-throughput-attempt-1-failed.json
A docs/saturn/evidence/reports/w0-vdp1-overwrite-fence-throughput.json
M docs/saturn/evidence/reports/w0-vdp1-overwrite-fence.md
M docs/superpowers/plans/2026-08-17-saturn-shaped-port-phase-plan.md
M docs/superpowers/plans/2026-08-17-vdp1-overwrite-fence.md
M tools/saturn/capture_sourceboot_throughput.py
M tools/saturn/test_capture_sourceboot_throughput.py
```

It contains no target C/header, `Makefile.saturn.mk`, sourceboot Makefile,
normal profile, package/compiler input, or target-behavior change. Thus the
truthful classification is **`target-equivalent diagnostic arm compiled at
tool/docs-only descendant revision; not artifact-identical to frozen normal
candidate`**. Its map verification passed: `___end=0x060FC274`, HWRAM remaining
`0x3D8C >= 0x1F00`, `lwram_end=0x002E9960`, and LWRAM remaining
`0x166A0 >= 0x4000`.

### Independent review and review-evidence transition (2026-08-17)

Independent review verdict: **`PASS WITH FINDINGS`**; **Ready for desktop
owner launch: Yes**. It validated the exact-generation predicate, same-field
epoch preservation, two-tick budget, `READY`-bank ownership, no busy-branch
DMA/replot, idle-path continuity, diagnostic ABI semantics, final normal
evidence, and the observer behavior. It found no critical or important issue.

The first narrow review-evidence finding was that
`verify_sourceboot_memory_map.py` omitted `.lwram_geo_traversal` from the
LWRAM maximum. A failing regression fixture was observed before the one-name
accounting correction. Reverification against the unchanged normal build ELF
now reports `lwram_end=0x002E95E0`, LWRAM remaining `0x16A20 >= 0x4000`; the
unchanged diagnostic build ELF reports `lwram_end=0x002E9960`, LWRAM remaining
`0x166A0 >= 0x4000`. Both retain their recorded HWRAM margins (`0x4A58` normal,
`0x3D8C` diagnostic) and no target rebuild occurred.

The second finding is closed by strengthening
`test_vdp1_transfer_pipeline_source.py` to require exactly one
`vdp1_sync_busy()` observation and prohibit transfer/bank-owner/poison/present
mutations on the busy branch. `frame_pipeline_test.c` now snapshots and
compares the complete relevant scheduler state for wrong-generation and
duplicate deferral acknowledgements. Production code is unchanged.

Focused evidence: the new memory-map regression test passed after its observed
RED; `verify-frame-pipeline` passed with all existing mutations caught; and
`verify-vdp1-transfer-pipeline` passed its binary, source, profile, and A8
contract checks. No target rebuild or headless Ymir run was performed: this is
a review/tool/test/documentation transition and the review authorized desktop
launch on the existing normal candidate.

### Desktop launch record (2026-08-17)

The durable pre-launch transcript
[`w0-vdp1-overwrite-fence-desktop-prelaunch.json`](w0-vdp1-overwrite-fence-desktop-prelaunch.json)
was written before the GUI command. It selects exactly one normal staged
release, `id-e8720d58595d9a62`: manifest SHA-256
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990` matches
the canonical throughput JSON, diagnostic mode is `0`, manifest provenance and
frozen product source are both `6697a3ce1c048d007a8d950d18fa1c653ad29923`,
and the expected executable probe is
`d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5`.
It records full staged manifest/CUE/ELF/profile paths, UTC build times, and
SHA-256 identities. T2.17 was not selected or modified.

The bounded desktop launcher wrote
[`w0-vdp1-overwrite-fence-desktop-launch.json`](w0-vdp1-overwrite-fence-desktop-launch.json)
at `2026-08-17T21:24:37.029935+00:00` after starting Ymir SDL3 PID `27380`
with the project profile and the manifest-verified CUE snapshot. At the end of
its 20-second monitor the process was still alive (`exit_code: null`), and its
durable stdout/stderr logs are retained beside the JSON. The snapshot CUE and
ISO hashes equal the staged candidate hashes. No screenshot or video was
captured, and no visual, audio, control, camera, collision, actor, presentation,
busy-occurrence, or FPS judgment is inferred from process liveness.

### Owner partial observation (2026-08-17)

The owner stated verbatim: **`I see 6-7 FPS and things look the same`** and
**`things look and sound good`**. This is evidence that the observed cadence is
at least 4 FPS, there is no obvious visual regression against the owner’s
baseline, and audible output is good. It is not full owner acceptance.
Controls, camera, collision, ordinary actors, and explicit absence of tearing,
flicker, partial frame, duplicate plot, and a permanent-busy frozen frame
remain unconfirmed. W0 therefore remains `owner gate pending`.

No diagnostic capture ran: the normal observation neither recorded an
unclassifiable busy deferral nor received a reviewer requirement for target
deferral telemetry. The W0 busy branch remains **`host-proven; target-compiled;
live occurrence unproven`**.

## Remaining gates

- [x] Scheduler deferral host contract, two new mutations, and VDP1 frame-bank
  ownership gate pass.
- [x] Sourceboot busy branch has host-contract evidence for no wait, DMA,
  replot, or bank ownership change; both target configurations compile and pass
  their declared memory floors.
- [x] `verify-frame-pipeline`, `verify-vdp1-frame-bank`,
  `verify-vdp1-transfer-pipeline`, and `verify-render-overlap-integration`
  pass without weakened assertions. `verify-sourceboot-presentation-boundary`
  remains unchecked: all seven tests fail at the known pre-W0 bootstrap literal
  `bootstrap must contain exactly one null-snapshot VDP2 begin`, before any W0
  path assertion runs.
- [x] Normal and diagnostic target configurations link and retain memory floors.
  Normal mode 0 is the frozen staged product candidate; diagnostic mode 2 is a
  separately sealed, target-equivalent tool/docs-only descendant and is not
  artifact-identical to that candidate.
- [x] A unique W0 normal artifact is staged with exact identity hashes.
- [x] Earliest headless product observation passes the 4 FPS floor and generic
  gameplay coherence checks on the exact staged normal candidate.
- [x] Independent review `PASS WITH FINDINGS`; no critical/important issue.
  Its memory-accounting and source/scheduler-test findings are closed by the
  focused review-evidence transition above; no product-code change occurred.
- [x] Exact normal candidate `id-e8720d58595d9a62` launched with desktop Ymir;
  the bounded 20-second monitor observed a live process and preserved stdout,
  stderr, manifest, CUE, and snapshot identity records.
- [ ] Owner accepts boot, play, fidelity, camera, collision, ordinary actors,
  audio, presentation, and cadence on the exact W0 artifact. Partial owner
  evidence is limited to the verbatim `I see 6-7 FPS and things look the same`
  and `things look and sound good`: >=4 FPS, no obvious visual regression, and
  audible output only.
