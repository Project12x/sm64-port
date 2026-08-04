# SDD ledger — plan: docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md

Workspace: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
Branch: sh2/native-math-purge
Plan commit: 1c262766586388937f1993d7accd7a6fda05b49f
Execution base: 1c262766586388937f1993d7accd7a6fda05b49f

Task 3/A3: **ACTIVE — host-contract seed complete.** The watched RED C
compile failed for the intentionally absent `saturn_render_cluster.h`; the
generator fixture then failed for absent compact position-stream fields. The
minimal generic contract and deterministic BOB near/mid/far unique-reference
streams make `verify-render-clusters` green under the explicit worktree
root/host compiler. This does not yet replace the fragment renderer's
full-position admission, so no target build/Ymir/FPS result is claimed.
Fragment/actor tier streams, runtime integration/counters, property gates,
review, and target visual evidence remain open. See `task-3-report.md`.

Preflight: existing linked worktree verified; plan conflict scan clean.
Preserved dirty scope: native-math verifier edits, prior audits/evidence,
temporary observation/build directories, and route files remain unrelated and
must not be staged by this plan.

Task 1: **BLOCKED**. Full-range quality review found that implementation
`4a8fe1ce` suppresses the whole `geo_process_root()` walk even though it owns
animation progression and stateful painting/DDD-warp, water, camera, moving-
texture, flying-carpet, and matrix-derived object updates. The Critical
optimization finding remains unresolved; Task 1 is not source-complete.

Runtime safety is **GO / fail-closed** at `77ee306c`, documented by
`19963fd3`: `sourceboot_run_source_tick()` cannot call the reserved
scene-graph suppression setter, so every accepted Saturn build retains the
full geo walk. Focused source policy PASS (2), runtime-contract executable
PASS, and profile decoder PASS (13, 1 skip). The policy/counter ABI remains
dormant; normal walks increment and suppressed walks remain zero.

No Task 1 optimization CUE build or Ymir launch is authorized while the
Critical finding is open. No target build, Ymir launch, or strict native-math
census was run by this task.
Baseline: `python tools/saturn/test_tools.py` timed out after 120 seconds while
still running; every emitted test before timeout passed. This is incomplete
baseline evidence, not a green aggregate gate and not a reported test failure.
Current aggregate: `python tools/saturn/test_tools.py` ran 193 tests in 204.286
seconds and failed with 17 pre-existing Bob parity route-schema errors from
preserved dirty route state. Its two Task 1 Fast3D-profile fixture failures
were corrected and their focused class now passes. The Make aggregate compiles
the runtime contract under the required wrapper but then exits nonzero from
the MSYS/Windows executable-path handoff; the exact compiler invocation and
resulting runtime-contract executable both pass directly.

Owner scope correction: this is a Saturn-exclusive repository. `a00cdd17`
removes the temporary PC/N64 `area.c` syntax gate; PC/N64 compilation is not a
Task 1 gate. “Ordinary interpreted” now explicitly means the Saturn
interpreted renderer. Focused Saturn source-policy, runtime-contract, and
profile tests pass; no target/Ymir work was run.

Quality safety fix round 2/5 is documentation-only. The tracked plan now leaves
the unsafe suppression Step 5 unchecked/BLOCKED and marks the CUE/Ymir step
NOT AUTHORIZED. No code or tests were run in this documentation transition;
only `git diff --check` is required before the tracked documentation commit.

Task 1D: **source-complete; target CUE/Ymir pending**. Runtime
containment is implemented at `98f26f26`; review-fix round 1 is `fe1074b8` and
quality-fix round 2 is `98670c48`. The scoped spec rereview marks both prior
Important findings **ADDRESSED** with no new Critical or Important findings.
Independent quality review is **NO-GO**:
trailing whitespace bypassed the demo/replay prerequisite check while still
activating the compile definition/tag, the tests admitted demo-keyed tagging
and setters outside the direct branch, and the plan contradicted the sealed D1
exception. Round 2's TDD red was 4 tests with 3 failures; after one intermediate
leading-space correction, green was 4 tests in 11.766s. The Makefile now rejects
observable whitespace and malformed values before activation. Tests compare
otherwise identical demo+replay configurations, scan the complete normal
compile-time path, and discover tool paths without clone-specific constants.
The plan/spec/evidence consistently define D1 as the sole owner-authorized,
non-promotable exception; it does not reopen A1. Both scoped rereviews are GO.
Focused source policy passed 4/4 and a fresh DLL-safe direct execution of
`build/saturn/host-tests/runtime-contract-test.exe` exited 0. The Make wrapper
path-translation failure remains an infrastructure gate; target build and Ymir
remain open. A1 remains **BLOCKED**. No target build or Ymir run occurred.
Task 1D: complete (commits 98f26f26..1e828879, spec and quality rereviews
clean). Serial `make -B -j1` through the audited wrapper built the tagged
diagnostic CUE in 258.5 seconds. The owner ran it in Ymir with the project
32-Mbit DRAM profile and observed approximately 2 FPS with no obvious
improvement. This is a negative upper-bound result: A1 stays BLOCKED for
correctness and is deferred behind Emergency A9.0. The runtime-contract Make
wrapper WinError 5 remains an infrastructure gate, not a D1 completion blocker.

2026-08-04 task transition: the valid desktop-Ymir run is launch-stable but
still visibly slow. The owner reports VDP1 ≈2 FPS and VDP2 ≈60 FPS. This
supersedes the earlier reversed counter interpretation and invalidates VDP2
as the active bottleneck; headless BIOS traces are not target evidence because
they did not load paired ELF main bytes at linked address. Emergency A9.0 is
source-complete/launch-stable with no claimed FPS gain. Task 2/A2 is ACTIVE at
Step 1: failing immutable snapshot-bank lifecycle tests. Its remaining gates
are the red test, minimal state machine, sourceboot capture integration,
pointer-free static proof, focused host gates, two reviews, and a later target
test. No build or emulator run occurred in this documentation transition.

Task 2/A2: **source-complete; verification/review pending**. The task adds a
Saturn-only fixed-width two-slot snapshot bank with fenced `FREE → WRITING →
READY → RENDERING → COMPLETE → FREE` lifecycle plus terminal quarantine. The
master alone captures live SM64 state after every source tick; the published
record retains scalar Mario/camera data and generated-bank identifiers only.
The watched direct-host red failed on the intentionally missing header/API;
the normal wrapper first hit the known `\\d\\Code...` Windows-Python path
translation failure. With an explicit forward-slash repository-root override,
`verify-render-snapshot-bank` and `verify-dual-frame-bank` passed. The former
also runs the source pointer-field guard. `verify-runtime-contracts` compiled
but failed at the preserved terrain command byte assertion
`runtime_contract_test.c:4018`; do not credit that gate. No target build/Ymir
was run. Task behavior/docs commit: `56866662`
(`feat(saturn): add immutable render snapshot banks`). Independent
specification and quality reviews, runtime-contract closure, and target
evidence remain required.

Task 2/A2 fix round 1: the first independent specification review was NO-GO
for terminal-quarantine reset escape, missing target cache-through accesses,
and a missing public generation validator. Red lifecycle/source/API tests
observed each gap. The repair preserves every non-FREE state through reset,
uses `CPU_CACHE_THROUGH` release and peer-payload accessors on SH-2 with host
identity aliases, and exports `sm64_saturn_render_snapshot_generation_valid`.
The focused snapshot-bank gate is green; runtime contracts still fail at the
preserved terrain-command comparison and no target/Ymir gate was run. Fresh
independent spec and quality review remain required.

Fix-round behavior/docs commit: `4a590101`
(`fix(saturn): harden render snapshot publication`).

Task 2/A2 fix round 2: specification rereview found `READY → RENDERING` was a
non-atomic read/validate/store sequence. A red deterministic contention test
first failed because no claim API existed, and the source gate separately
failed without SH-2 `tas.b`. The repair adds an uncached fixed-width claim byte
to the release record; `tas.b` selects one SH-2 winner and a host atomic models
the same contract. Acquisition holds the claim through validation and the
state transition, so a second contender receives no payload. Focused snapshot
bank gate is green; no target/Ymir and no runtime-contract rerun occurred.
Fresh review and the existing terrain-command runtime-contract gate remain
open.

Task 2/A2 fix round 3: quality review found the Critical P1-producer/P2-peer
coherency gap. The new source gate was RED before the owner cache-through
payload accessor and producer use sites existed. `begin_write` now returns P2
payload memory, and reset/retire clears also use P2, so sourceboot cannot dirty
P1 payload lines before it publishes the uncached release word. The focused
snapshot gate is green. This does not claim target coherency proof; target
evidence, fresh reviews, and the preserved unrelated runtime-contract failure
remain open.

Fix-round behavior/docs commit: `a358927e`
(`fix(saturn): publish snapshot payload uncached`).

Fix-round behavior/docs commit: `52aec1e1`
(`fix(saturn): serialize render snapshot claims`).

Task 2/A2 fix round 4: the independent cache-publication rereview was NO-GO
despite accepting the P2 producer repair, because quarantine could write
`QUARANTINED` while an in-flight claimant held the release byte and then be
overwritten by `RENDERING`. The red deterministic fixture held that byte and
proved the old public quarantine wrote around it; the companion source gate
was also red because quarantine, completion, and retirement did not all use
the claim. `c95feda8` gives reset/begin/publication and every terminal
transition the same claim/revalidate/publish/release protocol. While a claim
is owned, quarantine now fails closed for retry; when quarantine owns it
first, later acquisition observes a non-`READY` state and cannot return,
complete, or retire that generation. The stale cached-producer header comment
was corrected to the P2 protocol. Focused source and host snapshot gates pass;
`verify-runtime-contracts` remains the unrerun/uncredited terrain-command
failure at `runtime_contract_test.c:4018`; no target build/Ymir was run.
Fresh independent rereview and target coherency/multicore evidence remain
required.

Fix-round behavior/docs commit: `c95feda8`
(`fix(saturn): serialize snapshot terminal states`).

2026-08-04 runtime-contract classification/repair: the fresh explicit-host
`verify-runtime-contracts` red reproduced at
`runtime_contract_test.c:4018`. Git history showed `d80020cab` originally
published fully patched worker command images, while `e50fc478` intentionally
made worker command images private dynamic payloads and moved immutable
template copying to master-owned
`demo_emit_terrain_result()`. The retained full-command comparison was
therefore obsolete, not an A2 or target-renderer defect. `533471e5`
(`test(saturn): align terrain worker command contract`) replaces it with
stronger checks for the private payload boundary, preserved coordinates, and
complete resolved-template construction. The same fresh Make target passes
under the explicit forward-slash repository/compiler override; `git diff
--check` passes. No snapshot logic, target build, or Ymir run was
changed/performed. Independent review is not yet obtained; fresh
A2 reviews and target cache/coherency evidence remain required.

2026-08-04 runtime-contract review correction: the independent review
`runtime-contract-4018-review.md` is NO-GO for the overbroad claim that worker
bytes 0..11 are always zero. The live producer uses
`sm64_saturn_terrain_result_write_with_shades()` and may retain four
post-light RGB1555 words at bytes 0..7. The repaired fixture uses both the
no-shades wrapper and `publish_with_shades()` through the same sorted
master/slave spans. It verifies the exact dynamic layout: bytes 0..7 are zero
without shades or equal the four supplied shade words with the post-light flag;
bytes 8..11 and 28..31 remain zero; bytes 12..27 preserve coordinates; and
the master-side resolved template remains complete and separate. The new
shade regression was mutation-tested RED by temporarily replacing the shade
copy with zeros (failure at its shade `memcmp`), then restored and rerun GREEN
with the explicit-host runtime-contract command. The test-first clear-flag
case was also RED against the current writer: it passed non-null shades with
`POST_LIGHT_SHADES` clear and observed those forbidden bytes. `90fc3c76`
gates the copy on the flag; the live flagged shade path remains green. No
snapshot, target build, or Ymir gate ran, and a fresh independent rereview
remains required.
2026-08-04 A3: active terrain path now consumes the selected generated compact
fragment position span before transform; fragment and Mario generated banks
carry deterministic tier streams, mandatory far-route primitives stay
admitted, and profile counters record coarse cluster/position admission and
transform work. Host green: verify-visible-position-set, verify-render-clusters,
actor reproducibility/synthetic streams, profile decoder, diff --check. No
target/Ymir; A3 still needs generated per-cluster property checks, independent
review, and target evidence.
Commit: incremental A3 compact-span substitution (`feat(saturn): admit compact
terrain position spans before transform`).
2026-08-04 review remediation: red->green actor selected-tier source gate and
BOB/fragment metadata properties. Mario now transforms 228 FAR references
through original-vertex owner mapping; host gates include generated BOB headers,
visible-position set, render clusters, actor reproducibility, and profile
decoder. No target/Ymir. A3 remains active for generic cluster records on the
runtime admission boundary, independent rereview, and target evidence.
2026-08-04 generic A3 remediation: TDD Q16 contract conversion first failed
its MID assertion, then passed after the generic helper converts Q16 view
depth to world-unit LOD thresholds. Generated BOB/fragment headers now contain
`sm64_saturn_render_cluster_t` and per-cluster exact tier refs. Terrain calls
the helper per candidate and marks only returned spans; scene changes reset
cluster hysteresis. Host green: generator/property, cluster C fixture,
generated BOB headers, visible position set, and prior actor/profile gates.
No target/Ymir; independent rereview and target evidence remain.
2026-08-04 A3 final-review critical remediation: test-first yaw and pitch
fixtures proved that a front cluster may have negative world Z and a behind
cluster may have positive world Z. The immutable render view now carries the
Q16 camera-forward vector; generic admission conservatively projects each
AABB onto it for optional rejection and hysteretic LOD, preserving the exact
returned compact span. The focused `verify-render-clusters` MSYS host gate
passed. No target build/Ymir ran. This closes only the final review's Critical
world-Z defect: a fresh independent rereview, the documented generation-wrap
Minor, and target visual/counter gates remain open. Commit: `fix(saturn): use
view depth for compact cluster admission`.

2026-08-04 A3 target-budget repair: the exact serial route-0 A3 sourceboot
build reached `ld` and failed because `ram` overflowed by 29,680 bytes. The
map isolated the A3-owned 0x5ED4-byte per-cluster LOD array and 0x3630-byte
admitted-result array (38,148 bytes total) in HWRAM BSS. A watched RED source
placement gate failed for both arrays; the minimal repair moves only those
CPU-only arrays to linker-owned `.lwram_bss`, which is not an SCU-DMA source.
The focused generator/placement suite passes 7/7. The same `-j1` target
configuration now produces a CUE/ISO: HWRAM ends at `0x060FDEF0` with 0x2110
bytes above the required 0x1000 libyaul floor, and LWRAM ends at `0x002E33A0`
with 0x1CC60 free. This is target link/memory evidence only—no Ymir, counter,
or FPS evidence and no independent A3 rereview. Commit:
`fix(saturn): fit A3 cluster scratch in LWRAM`.
