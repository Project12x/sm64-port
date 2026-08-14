# SDD ledger — plan: docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md

> **Historical evidence only (superseded 2026-08-13).** This ledger no longer authorizes work or defines progress. Current authority is `docs/saturn/PRODUCT_GOAL.md`; execution is tracked only in `.superpowers/sdd/2026-08-13-mario-port-product-recovery/progress.md`.

Task 7: active — Step 1 lifecycle/wrong-region RED fixtures; base e4eb924e. A5.9 is closed, and A7 is the prerequisite to deferred A8 transfers. No target/FPS evidence claimed.
Task 7: design correction — A7 records current synchronous completion after the void emitter returns and represents zero-Gouraud as a satisfied NOOP obligation; real async submit/poll remains A8 scope.
Task 7: design correction — demo rendering gains an explicit success outcome; only success reaches READY, while pre-emission failure quarantines the incomplete build and retains the prior publication.
Task 7 Step 1: red — `vdp1_frame_bank_test.c` covers lifecycle order, exact ticket retirement, zero-Gouraud NOOP, stale generations, wrong regions, and quarantine retention. Host compile fails on the intentionally missing manager/header.
Task 7 Steps 2-3: red — memory-map mutations produce six expected failures against the old verifier; `verify-vdp1-frame-bank` is wired into `.PHONY`/`verify-all` and fails on the intentionally missing manager sources. Step 4 implementation active.

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

2026-08-04 A3+A4 manual result: the owner ran the fresh Pipe4 Route0/live-input
candidate in desktop Ymir with the project 32-Mbit DRAM-cart profile and
observed roughly 3–4 FPS, up from the earlier 1–2 FPS baseline. This is the
first positive qualitative performance observation. It retains open visual and
counter gates but authorizes future candidates to retain A3+A4 as the baseline.

Task 4/A4: **ACTIVE — source implementation and host gates complete; review
and target evidence open.** Watched RED: `verify-actor-meshlets` failed before
the missing `saturn_actor_meshlets.{h,c}` API existed. The generated Mario bank
now carries 31 source-ordered material/opacity meshlets (maximum 32 primitives)
with tight bounds and compact near/mid/far primitive/position remaps. The
serial master rejects behind meshlets before actor transform dispatch,
transforms each admitted position once, retains opaque source order, and places
only textured/translucent work in stable far-to-near bins; `s_actor_order` and
the quadratic insertion loop are absent from the accepted path. Green: explicit
MSYS host command ran `verify-actor-meshlets`, `verify-dual-actor-worker`,
`verify-terrain-depth-bins`, and `verify-terrain-command-template`; normal
fixtures pass and malformed-span/cached-owner mutations fail as required. No
target build/CUE/Ymir, counter, visual, or FPS gate ran. Independent spec
review, independent quality review, and target visual/counter evidence remain
required. A3 remains independently active; no target result was used to advance
it.

2026-08-04 A4 final source-only verification: Qt-host
`verify-actor-meshlets`, `verify-dual-actor-worker`,
`verify-terrain-depth-bins`, and `verify-terrain-command-template` are green;
the span and cached-owner mutation executables fail as required. The repository
tools environment also passes the focused Mario actor/profile tests (30 tests,
one expected skip). `verify-runtime-contracts` regenerated quad maps but its
host compile exited 1 without a diagnostic, so it remains uncredited; no target
build/CUE/Ymir was run. Commit: `7e419484` (`perf(saturn): cull and bin Mario
by meshlet`). Pending: two independent reviews and target visual/counter/FPS
evidence.

2026-08-04 A4 NO-GO remediation: review found that neutral-AABB-centre depth
ignored live yaw/animation and that the renderer rebuilt primitive corners
instead of consuming generated position spans. RED expanded actor fixture
failed its transform-stream telemetry assertion. GREEN projects live pose
vertices through the Mario yaw before admission (furthest for behind culling
and translucent binning, nearest for LOD), returns the exact globally
deduplicated selected-tier generated position union, and feeds it directly to
the actor worker. Focused actor/dual/depth/template plus Mario/profile gates
are green; span/cached-owner mutations remain caught. No target/CUE/Ymir.
Commit: `c5944bac` (`fix(saturn): admit Mario meshlets from live poses`). Fresh
independent spec and quality rereview, runtime-contract infrastructure closure,
and target visual/counter/FPS evidence remain open.

2026-08-04 Task 5/A5: **ACTIVE — queue contract source-complete; renderer
integration intentionally deferred.** A watched direct-host RED failed because
`saturn_render_job_queue.h` did not exist. The new project-owned bounded queue
uses fixed 16-byte pointer-free descriptors, uncached 32-bit generation/state/
claim words, cache-through access, generation-last publication, exactly-once
master/slave claims, terminal-only reset, and fail-closed full/stale/overlap
rejection. The focused host race fixture is green and the coherency source gate
plus six mutations (cached state, function pointer descriptor, missing
descriptor copy, non-final generation publication, premature reset, and
missing `tas.b`) pass. The
canonical Make wrapper attempted this target but hit the existing MSYS-to-
Windows `\\d\\Code...` repository-root translation/access failure before the
compiler; the same compiler invocation and Python gate pass directly. No
target build or Ymir run occurred. No A3/A4 renderer source was changed:
pending work is the slave polling consumer, static callback-table dispatch,
terrain/actor frame-queue integration, diagnostics, reviews, and target
coherency/visual/counter/FPS evidence. Behavior/docs commit: `11895af9`
(`feat(saturn): add immutable render job queue contract`). Independent
specification and quality reviews are **PENDING**; no verdict is claimed. See
`docs/saturn/evidence/reports/task5-a5-render-job-queue-2026-08-04.md`.

2026-08-04 A5 polling-contract increment: watched RED direct-host fixture
failed on the absent callback-table/drain APIs; GREEN adds local
callback-table resolution after an exact-once master/slave claim and drains
READY work until none remains. The direct C fixture and queue coherency gate
(including six mutations) pass. While preparing live terrain/actor wiring we
found a hard ownership incompatibility: existing worker callbacks derive their
cache lane from `begin == 0`, and fixed split ownership makes an opportunistic
master claim of a nominal slave range later read its own cached output through
the peer alias. No unsafe renderer seam, second `cpu_dual_slave_set`, target
build, or Ymir run was introduced. The next safe A5 task is descriptor-owned
output banks with an explicit claimed-CPU lane, then live queue integration;
the old fixed split remains diagnostic-only once that replacement exists.

2026-08-04 A5 descriptor-owned output-bank prerequisite: **SOURCE-COMPLETE —
live integration blocked on review.** Watched RED: the direct Qt-host fixture
failed because `saturn_render_output_bank.{h,c}` was absent. GREEN keeps the
queue's 16-byte pointer-free descriptor ABI: descriptor type selects terrain
or actor output storage, and the actual `CLAIMED_MASTER`/`CLAIMED_SLAVE` queue
owner atomically publishes the lane through P2-visible metadata. A reader uses
the recorded owner (never `begin == 0`) to select cached versus cache-through
output. The focused two-thread fixture passes descriptor-kind selection,
master-steal/cache-alias behavior, mismatch/overwrite failure, and exact-one
race publication; the structural gate passes and rejects five invalid
mutations. `saturn_demo_render.c` is deliberately untouched, so fixed split
workers remain the only accepted renderer path and master VDP1 painter order is
unchanged. No target build/CUE/Ymir/FPS evidence occurred. Commit: `0026a3a1`
(`feat(saturn): publish descriptor-owned output lanes`); independent spec/quality review and live queue
integration are open. See `task5-a5-output-bank-lanes-2026-08-04.md`.

2026-08-04 A5 output-bank critical-review remediation: **SOURCE-COMPLETE —
fresh review required.** The review correctly found that publication trusted a
callback-supplied claim lane. RED added a forged pre-claim publication attempt;
GREEN changes the API to receive the queue/job index only, locks and validates
the live queue release state, and derives the lane from that actual claim while
the queue record remains locked. The host fixture proves forged publication
fails and a real master claim succeeds; the race/cache-alias cases remain
green. The structural gate now rejects six mutations including absent
claimed-state validation. No renderer/target/Ymir change. Commit: `f0a3b99c`
(`fix(saturn): bind output lanes to queue claims`); independent review and
live integration remain open.

2026-08-04 Task 5.5/A5.5: **SOURCE-COMPLETE — independent review pending.**
RED: the direct Qt-host bridge fixture failed for the absent header. GREEN:
the project-owned bridge creates one local execution record only after a real
queue claim, routes terrain/actor ownership by exact descriptor index and
kind, and rejects a reader before that exact job reaches P2-visible `DONE`.
It contains no output-offset scan, fixed split, or `begin == 0` lane rule. A
single attach/notify lifecycle rejects a second polling callback registration;
it is not attached by the live renderer. Bridge and queue fixtures compile
with `-std=c11 -Wall -Wextra -Werror` and PASS. No target build/Ymir ran.
Pending: independent spec/quality review, then renderer conversion that removes
all fixed owner reads before enabling the queue callback.

2026-08-04 Task 5.5 fix round 1: **SOURCE-COMPLETE — fresh re-review
required.** NO-GO correctly found A5.5 compiled a second CPU-DUAL callback
registration beside the linked legacy fixed worker. RED source gate rejected
`cpu_dual_slave_set` and `cpu_dual_slave_notify`. GREEN retains only the
testable one-owner attachment state while compiling neither token; bridge and
queue host fixtures remain green. The atomic live cutover owns callback binding
only after all legacy dispatches are removed. No target build/Ymir ran.

2026-08-04 Task 5.5 fix round 2: **SOURCE-COMPLETE — fresh re-review
required.** NO-GO found source-only `slave_attach`/`slave_notify` names falsely
claimed a target callback lifecycle. GREEN renames them to `source_arm` and
`source_armed`; they record one static source-side owner and cannot activate a
slave or report a notification. The static gate scans queue plus bridge for
CPU-DUAL activation tokens and passes; bridge/queue host fixtures remain
green. No target build/Ymir ran.

2026-08-04 A5.6 runtime review repair: NO-GO found the runtime slave poll
directly read the cached queue generation. Red source coverage failed for the
absent P2 accessor; green adds a public cache-through generation reader and
forbids the direct dereference. Runtime and bridge C11/Werror fixtures and
source guards pass. Commit: `6eb532e2`; no renderer bind,
target build, Ymir, or FPS claim.

2026-08-04 Task 5.7: **ACTIVE — source foundation awaiting focused host rerun
and independent specification/quality review.** A5.6 preflight found that
independent descriptors cannot encode terrain transform→classify→ordered
multi-result merge or Mario transform→classify dependencies. RED graph fixture
preceded `saturn_render_job_graph.h`; GREEN adds P2-visible renderer-local
dependency masks, exact-index claims, failed-predecessor quarantine, and
`(job_index, output_index)` terrain merge identities. The fixed renderer,
legacy CPU-DUAL callback, and intentionally red live-cutover source gate remain
unchanged. No target build/Ymir. Pending commit and review.

2026-08-04 Task 5.7 fix round 1: **SOURCE-COMPLETE pending fresh independent
review.** Review found independent READY work was falsely expected to block,
cycle masks were accepted, and one-pass failure propagation could leave a
reverse-chain dependent READY. RED added those cases; GREEN rejects
self/cyclic masks, preserves independent eligibility, and iterates dependent
quarantine to a fixed point. Direct MinGW C11 `-Wall -Wextra -Werror` graph
fixture PASS; Python static graph guard PASS (2 tests). The separate live
cutover source gate remains intentionally RED; no target build/Ymir.

2026-08-04 A5.8 terrain descriptor-binding milestone: **ACTIVE — component
source foundation, not an activation.** TDD RED replaced the unavailable
configured `py -3` source guard with a host-compiled C live-cutover contract;
it correctly reports that the default frame remains legacy. GREEN component
initializes the renderer-owned queue/graph/output metadata and makes a future
WORLD callback reread its exact claim before deriving terrain record/command
payload pointers from descriptor output span plus claimant lane. It neither
publishes a live graph generation, registers CPU-DUAL, runs the callback, nor
marks terminal work. Direct Qt MinGW C11 payload-bank fixture passes with
`-Wall -Wextra -Werror`; no target build/Ymir. Remaining: complete terrain
producer + merge reader conversion, then Mario conversion, then two reviews
before one atomic runtime activation.

2026-08-04 A5.8 terrain producer/terminal-reader increment: **ACTIVE —
source-only, no activation.** Watched RED: direct Qt-host C source contract
reported absent exact terrain queue ownership. GREEN: the common terrain
compact producer now receives explicit descriptor bounds, writer lane, and
result arena; the new dormant WORLD_LOWER callback derives each from its exact
claimed descriptor and seals before graph runtime may publish DONE. Its
terminal reader requires the exact DONE descriptor before payload-bank record
and command reads. Direct C11/Werror route-source, payload-bank, and graph
fixtures PASS; `git diff --check` PASS. `make` is unavailable in this shell,
so no configured Make/MSYS result is credited; no target/Ymir ran. Open:
persistent per-job count/merge assembly, Mario conversion, fresh spec/quality
reviews, atomic CPU-DUAL activation, target evidence.

Task 5 / A5.8 terrain sub-prerequisite: source increment committed `8096c44c`
(`feat(saturn): route terrain work by claimed descriptor`). Fresh independent
specification review and code-quality review are required; no target evidence
or live-cutover acceptance is implied.

Task 5 / A5.8 terrain route fix round 1/5: review found queue-reachable
classification silently chose master when a legal slave descriptor began at
input offset zero. RED expanded the C source contract; GREEN passes explicit
claimant lane through `demo_classify_exact`, leaving range ownership only in
the legacy adapter. Direct C11/Werror route, payload-bank, and graph fixtures
pass; target/Ymir remain unrun. Fresh scoped re-review required.

Fix commit: `f4a39e16` (`fix(saturn): retain queue terrain claimant lane`).

2026-08-04 Task 5 / A5.8 terrain admit/merge metadata: **SOURCE-COMPLETE —
fresh independent specification and quality review required.** Watched RED:
the direct C11/Werror terrain route contract rejected missing WORLD_ADMIT
publication, per-job result publication, and metadata-owned DONE merge. GREEN:
WORLD_ADMIT publishes transformed-position completion keyed by exact claim;
WORLD_LOWER uses that transformed route, seals its arena, then publishes
generation/job/count/sequence/claimant-lane metadata before runtime can make
the job DONE. The terminal reader requires queue DONE plus matching
output-bank ownership and returns stored count/sequence. Route and bridge
fixtures PASS; `git diff --check` PASS. No queue activation, target build,
CUE/Ymir run, or FPS claim. Open: terrain merge-span assembly, Mario parity,
review, one atomic CPU-DUAL cutover, then target/cache/visual evidence.

2026-08-04 Task 5 / A5.8 fix round 1/5: **SOURCE-COMPLETE — fresh scoped
review required.** NO-GO found WORLD_LOWER had no callback-side proof of its
WORLD_ADMIT dependency. RED route contract and graph fixture covered missing
proof, an unready predecessor, and a terminal wrong-type predecessor. GREEN
adds exact claimed-lower P2 reread, one immutable predecessor requirement,
terminal WORLD_ADMIT type check, and P2 admit-output metadata validation before
classification. Route and graph C11/Werror fixtures PASS; no activation,
target build, CUE/Ymir run, or FPS claim. Open gates unchanged except this
consumer-proof defect is repaired.

2026-08-04 Task 5 / A5.8 terrain merge-span assembly: **SOURCE-COMPLETE —
fresh independent specification and quality review required.** Watched RED:
the C11/Werror terrain route fixture required terminal descriptor-stream
assembly, graph identity validation, and master stable ordering. GREEN:
the dormant route accepts only DONE WORLD_LOWER outputs through their
validated P2 metadata, rejects generation/sequence/count/claimant mismatch,
and preserves descriptor-owned record/command streams through the existing
master depth-bin order without reading legacy spans. Route and graph C11/Werror
fixtures and `git diff --check` PASS. No activation,
target build, CUE/Ymir run, or FPS claim. Open: ordered descriptor-command
lookup, Mario parity, review, atomic CPU-DUAL cutover, target/cache/visual
evidence.

2026-08-04 Task 5 / A5.8 terrain merge review repair: **SOURCE-COMPLETE —
fresh scoped review required.** NO-GO found the first assembler could skip a
current nonterminal WORLD_LOWER because it enumerated DONE descriptors only.
RED graph fixture required an immutable current-generation descriptor accessor
before DONE. GREEN adds a P2 published-descriptor accessor and requires the
dormant assembler to enumerate every graph descriptor, failing closed unless
each WORLD_LOWER is the same terminal DONE descriptor before metadata/payload
reads. Graph and route C11/Werror fixtures PASS; no activation, target build,
CUE/Ymir run, or FPS claim. Open: ordered descriptor-command lookup, Mario
parity, reviews, atomic CPU-DUAL cutover, target/cache/visual evidence.

2026-08-04 Task 5 / A5.8 terrain merge fix round 2/5: **SOURCE-COMPLETE —
fresh scoped review required.** NO-GO rereview found the incomplete-lower
guarantee was source-substring-only and zero-result all-culled frames failed.
RED executable graph fixture required READY/CLAIMED lower rejection, exact
DONE enumeration, and empty merge success. GREEN graph collection now
P2-enumerates every expected WORLD_LOWER and requires exact DONE identity;
zero identities validate only after at least one expected lower is proven DONE.
Strict queue, graph, and route fixtures plus `git diff --check` PASS. No
activation, target build, CUE/Ymir run, or FPS claim. Open: ordered
descriptor-command lookup, Mario parity, review, atomic CPU-DUAL cutover, and
target/cache/visual evidence.

2026-08-04 Task 5 / A5.8 dormant Mario queue parity: **SOURCE-COMPLETE —
fresh independent specification and quality review required.** Watched RED:
the direct C11 actor-route executable required descriptor-owned transform and
classification callbacks, exact ACTOR_ADMIT predecessor proof, terminal
count/sequence/claimant metadata, validate-before-mutate payload assembly, and
descriptor/local-order merge. The graph executable separately failed before
claimed/DONE actor predecessor and terminal collection APIs existed. GREEN:
the route snapshots the Castle-proven live posed vertices, lighting,
frame/bank, transform, and compact references; ACTOR_ADMIT and ACTOR_LOWER use
only exact claimed descriptors and claimant-selected payload lanes; the master
accepts only complete DONE lower coverage before restoring the existing
master-owned emission banks. Self-review retained once-per-range trig and
correct all-master ownership in the legacy non-slave path. Strict actor-route,
terrain-route, queue, bridge, graph, graph-runtime, and payload-bank C
executables PASS; configured Python is unavailable and uncredited. The
live-cutover executable remains expected RED. No target build, CUE/Ymir run,
or FPS claim. Open: review; choose per-payload-kind versus bounded-global
output offsets; ordered terrain command lookup; atomic CPU-DUAL replacement;
target link/cache proof; manual Ymir evidence.

2026-08-04 Task 5 / A5.8 dormant Mario queue parity review: **SOURCE-REVIEW
GO.** Implementation `1d1137f1`; independent audit `3ad0d23e`; seven strict C
fixtures independently PASS. No critical or important source defect. Minor
activation debt: add direct corrupt-identity, incomplete-coverage,
stale-sequence, and cross-lane callback tests plus explicit P2/cache-through
publication of callback context. Output-offset namespace, ordered terrain
command lookup, atomic CPU-DUAL cutover, target/cache, CUE/Ymir, and FPS gates
remain open and unchecked.

2026-08-04 Task 5 / A5.8 payload-kind namespace: **SOURCE-COMPLETE — fresh
independent review required.** Design inspection selected per-physical-kind
validation because WORLD_ADMIT positions, WORLD_LOWER records/commands,
ACTOR_ADMIT vertices, and ACTOR_LOWER references already occupy separate
bounded arrays; a global layout would add offset pressure without safety.
Watched RED/GREEN queue mutations prove cross-kind local offset reuse,
same-kind overlap rejection, mismatched callback rejection, and unknown-kind
rejection. Descriptor ABI remains pointer-free and 16 bytes. Strict queue,
graph, bridge, graph-runtime, and payload C11/Werror fixtures plus `git diff
--check` PASS; Python coherency wrapper unavailable and uncredited. No
activation, target build, CUE/Ymir, cache, or FPS evidence. Open: independent
review, ordered terrain command lookup, callback-context P2 publication and
callback corruption/cross-lane tests, atomic CPU-DUAL cutover, target/manual
evidence.

2026-08-04 Task 5 / A5.8 payload-kind namespace independent review:
**SOURCE-REVIEW GO.** Implementation `db28fd87`; audit `c5da1516`; no critical,
important, or minor finding. Reviewer independently reran strict queue, graph,
bridge, graph-runtime, and payload-bank C11/Werror fixtures plus `diff --check`;
all PASS. No CPU-DUAL activation, target, cache, CUE/Ymir, or FPS claim. Open:
ordered terrain command lookup, callback-context P2 publication and direct
callback corruption/cross-lane tests, atomic cutover, target/manual evidence.

2026-08-04 Task 5 / A5.8 ordered-command/context increment:
**SOURCE-COMPLETE — fresh independent review required.** Watched RED command
fixture failed before ordered refs retained descriptor-local command images;
route fixtures failed before both terrain and Mario callbacks opened a P2
context publication. GREEN retains result/command pairing through the final
master radix order while preserving the eight-byte SH-2 ref ABI. A pointer-free
16-byte release record binds exact generation/index/phase, sequence, byte
bound, and producer lane; exact claim validation selects cache-through only
for the peer. Both phases reject corrupt phase, stale generation/sequence,
incomplete publication, wrong claim, and out-of-range identity. Strict command,
context, route, depth, queue, graph, bridge, and payload C11/Werror fixtures
PASS; target/CUE/Ymir/cache/FPS evidence remains open. No CPU-DUAL activation
or accepted live-path change. Open: review, atomic sole-owner cutover, target
link/cache proof, manual desktop-Ymir comparison.

2026-08-04 Task 5 / A5.8 callback-context repair round 1/5:
**SOURCE-COMPLETE — fresh scoped re-review required.** NO-GO audit `891f64b2`
found the renderer publisher unreachable and outer P2 alias insufficient for
nested frame-varying pointers. Watched RED route fixtures required an inline
Mario compact-ref copy plus a self-contained terrain job/work-order snapshot.
GREEN removes the terrain stack classify/spans dependency, reconstructs only
caller-local classify views, and connects preclaim snapshot to all-descriptor
publication. Four phase-specific opens execute WORLD/ACTOR ADMIT/LOWER for
both lanes; mutations reject generation, sequence, stored index, phase, byte
bound, producer lane, ready, claimant, and range corruption. Strict context,
terrain/actor route, and command-stream C11/Werror fixtures PASS. No activation,
target/CUE/Ymir/cache/FPS evidence. Open: re-review, target link/cache proof,
atomic sole-owner cutover, manual desktop-Ymir comparison.

2026-08-04 Task 5 / A5.8 callback-context repair round 2/5:
**SOURCE-COMPLETE — final scoped re-review required.** First re-review accepted
the self-contained payload and preclaim publication repairs but found the
documented full corruption matrix still split: phase/ready/sequence/claim/range
covered lower callbacks only. The executable now parameterizes the entire
matrix, including a mismatched phase-specific opener, across WORLD_ADMIT,
WORLD_LOWER, ACTOR_ADMIT, and ACTOR_LOWER. Strict callback-context fixture
PASS. No activation or target/manual evidence; open gates unchanged.

2026-08-04 Task 5 / A5.8 first target compile gate: **FAILED BEFORE LINK;
NARROW REPAIR HOST-GREEN, REVIEW REQUIRED.** The one exact guarded
Route-0/live-input/Pipe4 `-B -j1` build at `b1fb718a` reached the new queue
translation unit and failed at `saturn_render_job_queue.c:26` because
`CPU_CACHE_THROUGH` lacked its defining Yaul cache header. Watched RED/GREEN
adds a source contract and imports `<cpu/cache.h>` only for SH targets; all
four focused tests plus `git diff --check` pass. No link/map/artifact, queue
activation, CUE/Ymir run, cache proof, or FPS claim is credited. Open: fresh
repair review and one serialized target rebuild, then link/section/memory
evidence.

2026-08-04 Task 5 / A5.8 post-review target gate: **TARGET COMPILE/LINK/
SECTION PASS; RUNTIME GATES OPEN.** Independent include-repair review is GO at
`8eef1c22`. The sole post-review exact Route-0/live-input/Pipe4 `-B -j1`
rebuild exited 0 after 490.4 seconds and produced fresh ELF/ISO/CUE artifacts.
`sh-elf-nm -u` is empty; map/objdump place live initializers in HWRAM and
shared queue/graph/context metadata in P2 `.uncached`. Physical end is
`0x060fe7f0`, leaving `0x1810` (6,160 bytes) HWRAM; LWRAM ends `0x002f1430`,
leaving `0xebd0` (60,368 bytes). ISO SHA-256 is
`3dfb1627bd38aeb7dcd296f913e778f045685861f66fb3aaa153500125901f01` and ELF
SHA-256 is
`7c62b65e2fc83a70e81c0655a8607b74716f694070d9ebce6bdfe1331b010a48`.
No CPU-DUAL cutover or Ymir occurred. Cache behavior, live sole-owner
activation, manual comparison, and FPS remain unchecked.

2026-08-04 Task 5 / A5.8 atomic cutover: **SOURCE-COMPLETE — FRESH REVIEW
REQUIRED.** Watched RED/GREEN removes the accepted fixed terrain/Mario joins,
publishes a four-job dependency graph and self-contained contexts before
claim, installs the graph runtime as the sole CPU-DUAL owner, requires all
terminal plus positive notified-slave retirement, and validates both terminal
assemblies before master-only VDP1 lowering. Incomplete work preserves the
prior complete backend without serial replay. Strict live-cutover,
terrain/actor route, runtime, queue, graph, bridge, payload, and callback
context C11/Werror fixtures PASS. No target build/CUE/Ymir/cache/FPS evidence.

2026-08-04 Task 5 / A5.8 atomic cutover first review: **NO-GO, REPAIR
HOST-GREEN, REREVIEW REQUIRED.** Audit `8e64b482` found the sole terrain admit
waiting for a nonexistent peer and a stale cached owner-map hazard for
slave-admit to master-lower. The queue is now explicitly single-producer;
lower rebuilds local owner bytes from exact DONE admit claimant metadata. A
new executable two-generation poisoned-owner callback fixture plus the prior
nine strict gates PASS. No target/CUE/Ymir/cache/FPS evidence.

2026-08-04 Task 5 / A5.8 handoff repair re-review: **SCOPED SOURCE GO.**
Repair `1667958e`; audit `1819f2b4`. Reviewer inspected live renderer wiring
and independently passed the handoff fixture, prior nine gates, and
diff-check. Next gate: one serialized post-cutover target build. No CUE/Ymir,
cache-runtime, or FPS evidence.

2026-08-04 Task 5 / A5.8 atomic-cutover target memory gate: **LINK FAILED;
NARROW PLACEMENT REPAIR HOST-GREEN, REVIEW REQUIRED.** The sole exact
post-cutover `-B -j1` build at `1819f2b4` compiled but HWRAM `.bss` overflowed
by 10,032 bytes. Activation retained formerly GC'd callback state. Watched
RED/GREEN moves the two master-only 13,872-byte final terrain merge streams
to `.lwram_bss`; two focused cutover source tests and scoped diff-check PASS.
No fresh ELF/ISO/CUE, Ymir, cache, or FPS evidence. Open: review and one
serialized post-repair target rebuild.

2026-08-04 Task 5 / A5.8 live-cutover target gate: **TARGET LINK PASS;
DESKTOP RUNTIME OPEN.** Memory repair `0519f50d`; independent GO
`b997fea1`. The sole post-review exact `-B -j1` build exited 0 in 522.4
seconds and packaged fresh ELF/ISO/CUE. HWRAM ends `0x060fc0ec`, leaving
`0x3f14` (16,148 bytes); LWRAM ends `0x002f87b0`, leaving `0x7850` (30,800
bytes); `nm -u` is empty. All terrain/Mario callbacks, graph publication,
master drain, slave poll, and slave entry are live. Disassembly proves one
non-null application `cpu_dual_slave_set(render_job_slave_entry)`; the only
other call is libyaul reset with null and the old worker entry is absent. ISO
SHA-256 `122682dc5f775b0459b878f3fabc56965fb1ae74baf4eba0381a7fe21be91bc1`.
No Ymir/FPS claim; desktop cache/runtime comparison is the next gate.

2026-08-04 Task 5 / A5.9 claim telemetry: **SOURCE-COMPLETE, HOST-GREEN,
REVIEW REQUIRED.** The flat 3–4 FPS A5.8 result triggered diagnosis rather
than another scheduler guess. Watched RED/GREEN adds one bounded uncached
runtime telemetry record, append-only profile fields, and VDP2 HUD output for
master/slave claims by world/actor admit/lower phase, exact notified/retired
generation, master retirement-wait iterations, failures, and quarantines. The
master adds no shared write/call inside the spin; it records one local count
after retirement. A delayed-slave four-job fixture proves the current ordering
permits `QM 1/1/1/1`, `QS 0/0/0/0`; a failure generation proves one failed
WORLD_ADMIT quarantines only dependent WORLD_LOWER. Strict runtime/VDP2 C11
fixtures and runtime/live-cutover source suites PASS. The stale graph-source
suite remains red at current HEAD because it asserts the already-landed graph
include is absent; broad tools timed out at 120 seconds. No target build/CUE/
Ymir/FPS evidence. Open: independent review, then one serialized HUD build.

2026-08-04 Task 5 / A5.9 first review repair: **HOST-GREEN, REREVIEW
REQUIRED.** Reviewer found a release-order race: SH-2 published positive
`retired_sequence` before retired telemetry, allowing a master snapshot of
stale zeros after leaving its wait. Watched RED/GREEN adds a source mutation
test and makes telemetry generation/sequence publish before the final positive
retirement marker in both SH-2 and host paths. Strict runtime C11/Werror and
five runtime source tests PASS. No target build/CUE/Ymir/FPS evidence.

2026-08-04 Task 5 / A5.9 repair rereview: **SOURCE GO; SERIAL TARGET BUILD
AUTHORIZED.** Repair `b990b24e`; audit `e98210ba`. Reviewer confirms retired
telemetry publishes and fences before the positive retirement release marker
in both SH-2 and host paths. Independent diff-check, strict runtime/VDP2 C11,
and 20 focused Python tests pass (one historical capture skip). No target
build/CUE/Ymir/FPS evidence yet; one serialized build and manual HUD read are
the remaining A5.9 gates.

2026-08-04 Task 5 / A5.9 telemetry target gate: **TARGET LINK PASS; MANUAL
HUD READ OPEN.** The effective exact Route-0/live-input/Pipe4 `-B -j1` build
exited 0 in 447.8 seconds and packaged fresh ELF/ISO/CUE. Two earlier starts
were environment-only failures (missing `.yaul.env`, then unwritable MSYS
`/tmp`) and produced no credited result or source change. Linked HWRAM ends
`0x060fc3cc`, leaving `0x3c34` (15,412 bytes); LWRAM ends `0x002f87b0`,
leaving `0x7850` (30,800 bytes); `nm -u` is empty. Runtime telemetry snapshot,
master/slave drains, and VDP2 begin/commit consumers are live. ISO SHA-256
`d21138b2fa759543de21cf70a8521a9193ad4741683aebb688e8dac423d84f3f`;
ELF SHA-256
`76bccfc48e55b3cd9d3f7cb3ef673aa51101b4fe3a10d944a227807816519f6a`.
No Ymir/FPS/scheduling claim; visible `QM/QS/QN/QR/QW/QF/QQ` transcription is
the remaining gate.

2026-08-04 Task 5 / A5.9 automatic observation correction: **ACTIVE.** The
owner confirmed the target candidate remains roughly 3--4 VDP1 FPS but asked
to remove manual title-bar/HUD transcription from the evidence path. Parallel
read-only investigations are checking (1) desktop Ymir's native performance
counter boundary and (2) target/profile memory capture for
`QM/QS/QN/QR/QW/QF/QQ`. Implementation will use watched RED/GREEN host tests,
the known-good `build-agent` desktop executable, project profile, and exact
CUE. No new target build, automated measurement, or scheduler conclusion has
occurred yet.

2026-08-04 Task 5 / A5.9 native desktop FPS capture: **LIVE-GREEN; REVIEW
OPEN; QUEUE READ OPEN.** Watched RED/GREEN added an exact-process Win32 title
sampler and restored the launcher to the proven `build-agent` executable with
explicit `--profile`/`--disc`. Eleven focused unit tests and `py_compile`
pass. The exact A5.9 CUE ran after a 35-second warmup and yielded ten valid
native Ymir samples: VDP2 median 60 FPS; VDP1 median 4 FPS, range 3--4. The
process remained alive. Evidence:
`docs/saturn/evidence/reports/a59-desktop-ymir-native-performance-2026-08-04.md`.
This confirms the flat result automatically; it does not provide
`QM/QS/QN/QR/QW/QF/QQ`, infer scheduler ownership, or close independent
review.

2026-08-04 Task 5 / A5.9 native desktop FPS first-review repair:
**HOST-GREEN; REREVIEW OPEN.** Review rejected the claim that repeated title
reads prove distinct counter-rollover intervals, integer truncation of even
medians, missing JSON on capture failure, and implicit Win32 callback types.
Watched RED/GREEN now records timestamped running-counter snapshots, preserves
fractional medians, writes initial/success/failure JSON, checks the exact PID
through capture, and declares the 64-bit Win32 ABI. Thirteen focused tests,
`py_compile`, and `git diff --check` pass. The prior live 3--4 FPS observation
is retained as ten snapshots, not ten proven-fresh intervals. No rerun,
queue-counter claim, or scheduler conclusion has occurred after this repair.

2026-08-04 Task 5 / A5.9 native desktop FPS rereview: **GO.** Independent
rereview found no critical or important defects and independently passed the
13-test scope. The remaining non-blocking failure-report hardening was then
pinned by watched RED/GREEN: post-launch failures retain stage, PID, and both
adjacent durable log paths. The final focused suite is 14/14 green with
`py_compile` and `git diff --check`. Automatic desktop VDP1/VDP2 capture is
source-complete and live-proven; queue telemetry capture remains open.

2026-08-04 Task 5 / A5.9 final automatic sampler check: **LIVE-GREEN.** The
final repaired collector attached read-only to the same running PID without a
second emulator or target build. Five timestamped snapshots report VDP2 median
60 FPS and VDP1 median 3 FPS, range 3--4. Final narrow rereview is GO and
independently passes 14/14 tests plus diff-check. Automatic FPS observation is
closed; queue telemetry remains open.

2026-08-04 Task 5 / A5.9 documentation reconciliation: **COMPLETE.** The root
architecture now describes the queue and callback contracts as live after the
atomic cutover; State no longer claims desktop/FPS evidence is open; Roadmap
records the already accepted standalone audio proof; and the active plan no
longer calls FPS review unchecked. Automatic FPS observation remains closed at
3--4 VDP1 FPS with VDP2 near 60. Queue telemetry is the sole immediate A5.9
observation gate. No code, target build, CUE, or new runtime claim is included.

2026-08-05 Task 5 / A5.9 queue-observation fix round 1/5: **4 ADDRESSED; 1 NEW
BLOCKER; LIVE READ OPEN.** Commit `81a74785` repairs repeated-sequence failure,
allocated/loadable executable-section eligibility, bounded notification
diagnostics, and the unused import. Root verification passes 12/12 capture
tests, 16/16 boot-trace tests, `py_compile`, and `git diff --check`. Scoped
rereview nevertheless found that section file and virtual containment did not
also prove the same `PT_LOAD` affine mapping. Fix round 2 must require
`section.address - segment.vaddr == section.offset - segment.offset` and add a
malformed mapping fixture. No target build or Ymir run is credited; Step 6 and
the queue evidence gate remain unchecked.

2026-08-05 Task 5 / A5.9 queue-observation fix round 2/5: **SOURCE-COMPLETE;
REVIEW GO; LIVE READ OPEN.** Commit `3e22f161` requires the exact affine
file-to-memory mapping within the same `PT_LOAD` and adds a malformed mapping
fixture. Root and independent reviewer each pass 13/13 capture tests, 16/16
boot-trace tests, and `py_compile`; scoped rereview is spec PASS / quality
APPROVED with no new blocker. No target build or Ymir run is credited. Step 6
and the queue evidence gate remain unchecked until an exact matching live
capture proves target identity, at least two presentation events, and at least
one coherent terminal queue record.

2026-08-05 Task 5 / A5.9 first live queue attempts: **FAIL-CLOSED; STARTUP
WAIT REPAIR ACTIVE.** Attempt one used the wrong 120-byte historical evidence
blob as IPL; direct Ymir diagnostics identify the required 524,288-byte size,
so it is discarded. Attempt two used the established 512-KiB USA IPL and exact
hashed CUE/ISO/ELF/Ymir, reached JSON-RPC and BIOS handoff, then correctly
rejected a target identity mismatch. A separate bounded boot-trace diagnostic
shows why: the exact ELF `main` bytes are absent through post-BIOS +540,
appear at +570, and sourceboot trace magic appears at +600. The collector
currently checks immediately after the BIOS macro. Repair round 3 must poll
identity one VBlank at a time within a separate bounded startup window, record
the wait, read no telemetry before identity, and fail if no match occurs. No
target build, queue values, or performance claim is credited; Step 6 remains
unchecked.

2026-08-05 Task 5 / A5.9 startup repair round 3/5: **SOURCE-COMPLETE; REVIEW
GO; LIVE RERUN OPEN.** Commit `3403554b` advances exactly one VBlank per exact
ELF identity attempt within an independent validated 1..4096 startup bound,
records bounded wait/attempt evidence, and reads no telemetry until identity
matches. Root and independent reviewer each pass 17/17 capture tests, 16/16
boot-trace tests, and module compilation; scoped verdict is PASS/APPROVED with
no new blocker. No target build or queue claim is credited. A corrected live
run with the real IPL is now the sole immediate gate.

2026-08-05 Task 5 / A5.9 repaired live attempt: **IDENTITY GREEN; STALE
HEADLESS BINARY BLOCKED CART.** Exact ELF identity matched after 540 startup
VBlanks. The following 600-VBlank observation produced no presentation edge
because target `SCAR` telemetry reports stage 6 FAILED, `cart_id=0`,
`cart_size=0`, status 1 (`MISSING_4MIB`), with PC in `main`'s failure loop.
Source inspection and artifact timestamps identify the cause: the used
`build-agent` executable dates July 18, while headless `--dram-cart` support
was added in `bf3e4a4a` on July 21. The already-present `build-agent2` binary
postdates that change and is the next exact candidate. No queue values or FPS
claim is credited; no target/emulator rebuild is needed.

2026-08-05 Task 5 / A5.9 `build-agent2` live attempt: **CART ACTIVE;
600-VBLANK WINDOW TOO SHORT.** The post-`bf3e4a4a` binary accepts the DRAM cart:
the target leaves `main`'s missing-cart loop and Ymir logs sustained
`SOURCE.DAT` reads across successive eight-sector chunks. Exact ELF identity
again matches after 540 startup VBlanks. The separate 600-VBlank observation
window expires mid-copy before a presentation edge, so it remains failed
evidence. The next single-variable diagnostic uses the collector's existing
4096-VBlank maximum; no source change or rebuild is involved.

2026-08-05 Task 5 / A5.9 automatic queue observation: **LIVE-GREEN;
STEP 6 COMPLETE.** Using the already-built post-DRAM-cart `build-agent2`
executable, exact ELF identity matches after 540 startup VBlanks and the
bounded run records three presentation events by observation sample 1185.
Cadence is 4.8 FPS mean, 4.87 median, and 4.29 1%-low. Coherent retired
sequence 1 reports `QN=QR=3`, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and
`QW=QF=QQ=0`, with zero CPU failures. Both SH-2s perform useful work and the
master records no retirement wait in this frame. This closes A5.9 automatic
observation and redirects the next optimization toward admitted work volume
and the master-only final merge/VDP1 transfer path. Evidence:
`docs/saturn/evidence/reports/a59-sourceboot-queue-throughput-2026-08-05.json`.

2026-08-05 Task 5 / A5.9 evidence review: **NUMERIC/INFERENCE PASS; STATUS-DOC
REPAIR ACTIVE.** Independent review verified every JSON hash, cadence value,
coherence field, and the frame-scoped inference. It rejected publication only
because the plan summary still labeled Task 5 and Task 5.7 active/unbound.
Those boxes and current-state sentences are now reconciled as complete while
the dated increment notes remain explicitly historical. Task 7 is named as
the immediate bank-lifetime prerequisite to A8.

2026-08-05 Task 5 / A5.9 final evidence rereview: **GO; A5 COMPLETE.** Commit
`5edeef07` reconciles the active-plan summary and Roadmap with the already
verified live result. The same independent reviewer confirms Task 5 and 5.7
are consistently complete, dormant/unbound increment notes are explicitly
historical, and A7 is correctly identified as A8's prerequisite. No remaining
A5.9 gate is open. Task 7/A7 begins from `5edeef07`; no target behavior is
claimed by this documentation transition.

2026-08-05 Task 7 / A7 Steps 1-3 RED: the lifecycle fixture covers ordered
build/ready/transfer/publish/reuse, exact ticket retirement, zero-Gouraud
NOOP, stale/wrapped generations, source regions, and quarantine retention;
its target fails on the intentionally absent manager. Six memory-map
mutations fail against the old verifier, and the new aggregate target fails
on the same missing sources. Step 4 state/ticket implementation is active;
no target/Ymir/FPS evidence is claimed.

2026-08-05 Task 7 Step 4: host-green. The manager enforces FREE-only builds,
three-command minimum/capacity bounds, exact worker and transfer obligations,
zero-Gouraud NOOP, modulo generation freshness including UINT32_MAX→1,
publish-new-before-retire-old, current-fallback retention, no-free refusal,
and permanent quarantine exclusion. `verify-vdp1-frame-bank` passes. Step 5
sourceboot ownership integration is active; target evidence remains open.

2026-08-05 Task 7 Steps 5-6: **SOURCE-COMPLETE; FOCUSED HOST/TARGET-LINK
GREEN; REVIEW ACTIVE.** Sourceboot now uses the explicit bank manager, requires
an explicit renderer success outcome before READY/publication, records the
current blocking upload through the synchronous-complete adapter, quarantines
failed builds, retains the previous publication, and separates build,
published, and displayed generations. Both command prefixes initialize
unconditionally. Focused bank, 10 memory-map, DMA queue, command-template,
runtime-contract, presentation-boundary, and live-cutover gates pass. The
serialized SH-2 image compiles/links; ELF `45387e...6867b` proves exact
`0x20000` LWRAM command staging, exact `0x6000` HWRAM Gouraud staging, and
`0x3794` HWRAM margin. Broad target verification remains unchecked on the
unrelated pre-existing native-math oracle error
`_play_cutscene -> _cutscene_bbh_death`. No Ymir/FPS/asynchronous-transfer
claim. Step 7 documentation/commit/review is active.

2026-08-05 Task 7 implementation commit: `650b911a`
(`feat(saturn): track VDP1 source-bank lifetimes`). Behavior docs, source,
focused tests, and build integration are committed together. Independent
specification and quality reviews remain open; Task 7 is not complete.

2026-08-05 Task 7 consolidated review: **NO-GO; REPAIR ACTIVE.** Required:
wrap-safe rejection/quarantine of late stale publication, real false outcomes
from both emitters after repeated Gouraud submit failure, and init rejection
for aliased/overlapping/misaligned command or Gouraud storage. Add direct red
fixtures before production repair; no target/Ymir/FPS evidence credited.

2026-08-05 Task 7 repair RED: frame-bank fixture aborts on accepted aliased
command storage; subsequent cases pin overlap/alignment and wrap-order stale
publication. `verify-gouraud-transfer` fails on the intentionally absent
result-bearing helper. Production repair is active; no target work started.

2026-08-05 Task 7 repair focused GREEN: frame-bank direct fixture passes stale
wrap-order quarantine and all storage alias/overlap/alignment cases;
Gouraud-transfer fixture passes repeated failure after one drain; 2/2 source
mutation checks pin both emitter outcomes and main propagation; presentation
6/6 and memory map 10/10. One serialized target validation is next.

2026-08-05 Task 7 repair target integration: **COMPILE/LINK GREEN; REREVIEW
OPEN.** One incremental Pipe4 build produced ELF `b0ede6f9...190401cc` with
both repaired emitters and the new helper. Broad verify stopped afterward on
the unchanged unrelated `_play_cutscene -> _cutscene_bbh_death` oracle edge.
No second build, Ymir run, or FPS claim. Live-cutover source gate passes.

Final host strengthening adds Gouraud manager-object overlap/alignment cases
and rejects them before dereference; aggregate frame-bank/transfer gates remain
green. Per the one-target-run cap this last narrow manager edit was not
target-rebuilt and is recorded honestly for rereview.

2026-08-05 Task 7 consolidated repair committed at `41a4ce7e`
(`fix(saturn): fail closed on stale VDP1 banks`). Independent repair rereview
is the remaining Task 7 gate.

2026-08-05 Task 7 final closure: **COMPLETE; PASS/APPROVED; TARGET GREEN.**
Independent rereview on `deebd06e` passes both strict host fixtures. Exact
audited Route0/live-input/Pipe4 `make -B -j1` exits 0 in 336.9 seconds and
produces ELF SHA-256 `1eba8888...e99267c`. A8 becomes active next with no
behavior-complete claim. No A7 Ymir/hardware/FPS claim.

2026-08-05 Task 8 / A8 design transition: **ACTIVE.** The two final VDP1-VRAM
ranges remain single-owned destinations, so the prior list must retire before
either transfer begins. A8 will overlap source construction with the old plot,
then submit CPU-DMAC commands and SCU-DMA Gouraud work without an immediate
transport wait; A9 retains destination banking/true cross-frame transfer
overlap. Pinned Yaul's public `cpu_dmac_transfer()` internally waits, so A8
must first prove channel 0 idle and treat that wait as a proved-zero
precondition. Red transport/lifecycle and source anti-pattern tests are next;
no target build, Ymir run, or FPS result is claimed.

2026-08-05 A8 audit correction: CPU-DMAC channel 0 becomes an explicit
post-boot exclusive queue resource; an idle status poll guards Yaul's
internally waiting helper. The VDP1 overwrite-safe fence precedes submission,
the two descriptors reserve/commit atomically, and staggered ticket retirement
is represented even though A8 deliberately preserves SlaveDriver's serial
FIFO. Both demo and normal/full-game emitters become construction-only and the
shared frame-bank pipeline becomes their sole uploader. These are testable A8
requirements; no implementation/target/FPS claim is made by this correction.

2026-08-05 A8 Steps 1-3 RED / queue GREEN: strict host fixtures first failed
on the absent CPU-DMAC mode, retirement query, atomic pair API, and frame-bank
submit/poll/wait contract. The minimal serial queue transport now guards Yaul
entry with channel-0 idle, treats CPU address/NMI faults as failures, commits
descriptor pairs atomically, and passes its direct C/Werror fixture. The
frame-bank fixture passes transport selection, zero submit waits, staggered
retirement, atomic failure, and actual-wait accounting. Source anti-pattern
tests are written and remain RED against both blocking emitters and sourceboot's
synchronous adapter. Step 4 integration is active; no target/Ymir/FPS claim.

2026-08-05 A8 Steps 4-7: **SOURCE-INTEGRATED; FOCUSED HOST GREEN; REVIEW
PREP ACTIVE.** Both emitters are construction-only. After the master-owned
single-destination overwrite fence, sourceboot atomically queues guarded
CPU-DMAC command and SCU-DMA Gouraud work, kicks, and returns without a
transport wait. TRANSFERRING persists across fields; exact staggered tickets
must retire before one arm/force-put/publication and presentation uses the
published snapshot generation. First-ticket failure drains its sibling before
quarantine. Focused queue, frame-bank, transfer-pipeline, source-policy, VDP2,
and profile-layout/decode gates pass. Telemetry reports actual overwrite and
terminal ticks, channel-blocked queued-not-started events, bank-unavailable
skips, and transfer faults; it does not invent zero-duration bank waits.
Presentation/runtime/memory-map gates, independent review, and one serialized
target build remain open. No target/Ymir/FPS claim.

2026-08-05 A8 first review: **NO-GO; REPAIR RED ACTIVE.** Pinned Yaul's
`cpu_dmac_status_get().channel_busy` does not reliably distinguish DE=1/TE=0,
so polling can retire CPU-DMAC immediately. The repair must own/configure/start
channel 0 and retire only from its completion callback. The serial lane must
also progress during stale-loop service instead of one stage per fresh field;
VDP2 camera state must be coalesced with the published VDP1 generation;
partial destination failure must remain non-presentable; telemetry must time
only real waits and count only real blocked starts; and complete destination
capacities/alignment must fit VDP1 VRAM. No target build is authorized before
focused rereview.

2026-08-05 A8 review repair: **SOURCE-COMPLETE; FOCUSED HOST GREEN; REREVIEW
NEXT.** Queue initialization stops queue-owned CPU-DMAC channel 0 after boot
work; each command transfer uses pinned Yaul's public config/start API and a
completion IHR. A host model deliberately reports `channel_busy=0` throughout
an active DE=1/TE=0-like transfer and proves no early retirement. Sourceboot
polls/kicks pending serial transfers before its stale-field wait and immediately
re-enters while incomplete, but keeps publication VBlank-owned. Any partial
resident-range failure poisons the destination and disables plotting. Each
BUILDING bank captures the immutable VDP2 camera used with that bank's
published VDP1 generation. Ordinary command/Gouraud and terminal wait fields
are explicitly zero; QNS is single and start-state guarded. Full declared VRAM
capacities and Gouraud 8-byte alignment are enforced. Fresh strict DMA,
transfer-pipeline, frame-bank, and 15 source/mutation tests pass. Fresh
VDP2 and runtime-contract direct C/Werror gates also pass; memory-map unit
coverage is 10/10 and profile layout/decode is 21 tests with one historical
capture skip. An aggregate MSYS make attempt was discarded because its POSIX
`realpath` reached Windows Python as `\d\...`; direct configured host tools ran
the exact fixtures. Repair commit remains before rereview; serialized target
build remains prohibited until rereview. No target/Ymir/FPS claim.

2026-08-05 A8 repair commit: `8b037a7d` (`fix(saturn): harden deferred
VDP1 completion`). Fresh pre-commit evidence: five strict C/Werror host
executables pass; 36 Python source/mutation/profile tests pass with one
historical-capture skip; memory-map coverage passes 10/10; cached diff check
is clean. Focused independent rereview is next. No target build was run.

2026-08-05 A8 closeout: **SOURCE/TARGET COMPLETE; YMIR/FPS OPEN.** Contract
and quality rereviews are independently PASS/APPROVED; the latter passed after
`bf160e53` corrected the architecture's interrupt-ownership description. The
first authorized target attempt rebuilt A8 objects but stopped at `main.c:906`:
pinned Yaul exposes `VDP1_VRAM(0)` as an integer address whereas host mocks had
hidden the pointer conversion requirement. A new source contract failed first,
then passed after the explicit `(void *)(uintptr_t)` conversion; its 5/5 tests
and `git diff --check` pass. The exact serialized incremental Route0/live-input/
Pipe4 build exits 0 in 64.3 seconds and links a fresh 8,681,312-byte ELF with
SHA-256 `5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.
No Ymir, queue/presentation capture, manual FPS, or hardware claim is credited.

2026-08-05 A8 automatic runtime gate: **RED; REPAIR ACTIVE.** The exact CUE/
ELF/Ymir/BIOS/DRAM-cart capture binds ELF SHA-256 `5926ff27...17d9cab5` and
passes loaded-code identity, then fails closed because fewer than two VDP2
presentation generations occur in 4,096 VBlanks. Direct P2 diagnosis records
BOB ready, render runtime active, Mario snapshot valid with 424 pose vertices,
but zero admitted actor transform refs, `frame_serial=0`, `pipeline_faults=4`,
no pending/started DMA, zero transfer faults, and both source banks
quarantined. The fixed four-job graph currently mistakes a legitimate fully
culled/offscreen actor for renderer failure. A scene-neutral zero-actor/no-op
repair and repeated automatic capture are required; no FPS claim is credited.

2026-08-05 A8 zero-actor repair: **RUNTIME PUBLICATION GREEN; CADENCE OPEN.**
The renderer now publishes `[WORLD_ADMIT, WORLD_LOWER]` for a fully culled actor
and appends actor admit/lower only when actor positions exist; Mario callback
context, terminal assembly, emission, and job accounting follow that dynamic
shape. Six focused runtime source contracts and three transfer-policy tests
pass. The serialized incremental target build exits 0 in 64.4 seconds and
produces ELF SHA-256 `b246b39c...e7a88bdd`. The repeat exact automatic capture
passes: two- and four-job queue generations retire with no failures, VDP1/VDP2
presentation advances, and live profile reads show no pipeline, transfer,
bank-unavailable, overwrite-wait, transport-wait, or terminal-wait fault.
The sole interval is 37 fields (1.62 FPS), worse than the A5.9 baseline but too
shallow to call stable. Longer automatic cadence sampling remains active; no
uplift or manual/hardware claim is credited.

2026-08-05 A8 deep-cadence and fail-closed repair: **COMPLETE.** Independent
review found that zero returned from actor preparation conflated valid full
culling with invalid pose/meshlet failure. Strict RED/GREEN coverage now
requires a boolean success result plus output count, and only successful zero
selects the two-job graph. Rereview is PASS with no Important issue. Seven A8
contracts and 22 configurable-depth capture tests pass. The serialized target
build exits zero with ELF `10e92064...df569ab`. Exact ten-event evidence records
nine 36--38-field intervals (1.63 FPS mean, 1.62 median, 1.58 1%-low), all ten
queue generations retired, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and
`QW=QF=QQ=0`. A8 does not improve cadence; split CPU construction/simulation
cost next.

2026-08-05 Task 9 Step 0: **ACTIVE.** Before changing scheduler policy, extend
the exact presentation-edge evidence with wrap-safe deltas from the existing
simulation and render/construction counters. The immutable baseline is A8 ELF
`10e92064...df569ab`: nine 36--38-field intervals, all queue generations
retired, and zero transfer/queue waits or failures. No A9 behavior or uplift is
claimed yet.

2026-08-05 Task 9 Step 0: **TARGET EVIDENCE GREEN; REREVIEW PENDING.** The
60-byte P2 seqlock trace was repaired after review to mark both ends odd,
include snapshot/Mario-pose preparation, cover arm/publish work, expose zero
unattributed fields, and retain diagnostics on torn decode. Exact ELF
`1ffb47cc...e4edfe6` produces ten events/nine intervals. Of 333 fields,
simulation owns 283 (85.0%), construction 49 (14.7%), and
transport/presentation 1 (0.3%). Six simulation ticks execute per presented
frame and another 222 credits are dropped. Step 1 is active: scope the bounded
normal+recovery tick budget to one presentation generation.
Independent repair rereview is PASS: all 333 fields are accounted for, exact
ELF/report identity matches, and queue retirement remains fault-free.

2026-08-05 Task 9 Steps 1--4: **SOURCE COMPLETE; REVIEW ACTIVE.** Strict RED
coverage began with the missing scheduler, then exposed repeated reuse
reopening simulation credit, same-field post-reuse service, and uncharged
queued N+1 work. GREEN uses a presentation-scoped two-tick budget, resets only
on complete publish, carries queued work, and bounds SERVICE/POLL to one per
VBlank. The direct normal fixture passes and all three mutants are caught.
Aggregate Make compiled the binaries but hit the known MSYS shell handoff
quote defect before execution; direct executable results are recorded instead.

2026-08-05 Task 9 Steps 1--4 independent review: **NO-GO; REPAIR ACTIVE.**
Ordinary generations and all three mutations behave as intended, but generation
`UINT32_MAX -> 0` aliases unset completion/queued-snapshot sentinels and can
publish incomplete work. Publish/promote also resets generation-local
SERVICE/POLL flags, permitting additional work during the same observed
VBlank, and `verify-frame-pipeline` is absent from `verify-all`. Step 5 remains
blocked until wrap-safe validity, field-scoped service/poll state, two-phase
exact-generation publish acknowledgement, regression tests, aggregate gate
inclusion, and independent rereview all pass. The acknowledgement is required
because the runtime arm/publish/retire/VDP sequence is fallible; returning a
publish intent cannot itself advance displayed generation or reset cadence.

2026-08-05 Task 9 Steps 1--4 repair: **SOURCE COMPLETE; REREVIEW ACTIVE.**
RED/GREEN adds explicit validity for wrapped generation zero, field-global
SERVICE/POLL epochs that survive publish/promote, and two-phase exact-generation
publish acknowledgement. Failed, wrong, or absent acknowledgement cannot
advance displayed generation or reset cadence. `verify-frame-pipeline` is now
in `verify-all`. Fresh direct compilation/run passes the nominal fixture and
catches all three mutations; the native Make recipe compiles then encounters
the retained MSYS quoted-Windows-executable handoff defect. Step 5's new
integration contract is independently RED 7/7 against the legacy loop and
names the six-action adapter seam. No target or FPS claim is made.

2026-08-05 Task 9 Steps 1--4 rereview: **GO; COMPLETE.** The reviewer confirms
wrapped active/queued generation zero remains valid, field-global work epochs
survive publication, exact-generation success/failure acknowledgement is
fail-closed, and the aggregate gate includes the scheduler. Fresh nominal and
mutation binaries pass their intended contracts. Step 5 is now active from its
independent 7/7 RED adapter contract.

Scheduler/model checkpoint `d49b8677` records Steps 1--4, rereview GO, fresh
nominal/mutation evidence, and the intentional Step 5 RED contract. The Step 5
compatibility adapter is active; no target build or uplift evidence exists yet.

2026-08-05 Task 9 Step 5 first GREEN: **SOURCE GREEN 29/29; TARGET BLOCKED.**
The six-action adapter replaces the legacy catch-up loop and passes the A9
integration, presentation, A8 transfer, cadence trace, boot trace, and source
suppression contracts. Pre-target review found the pure scheduler grants one
simulation credit per VBlank, which would run healthy SM64 logic at 60 Hz; the
removed sourceboot path required two fields per 30 Hz tick. A fractional
two-field model repair is active. SERVICE/POLL/presentation remain field-rate.

2026-08-05 Task 9 combined review: **NO-GO; TWO REPAIRS ACTIVE.** The 30 Hz
quotient/remainder model is healthy, but successful publication records VDP2
and cadence evidence before exact scheduler acknowledgement updates generation
and credit state. Required order is target publish, success ack, telemetry
refresh/presentation, cadence append. Scheduler generation zero also conflicts
with render-snapshot and VDP1-bank APIs that reserve zero as invalid. Parallel
repairs establish ack-before-evidence and a consistent skip-zero generation
policy. Target build remains withheld.

2026-08-05 Task 9 combined-review repair: **SOURCE COMPLETE; REREVIEW ACTIVE.**
One public successor keeps zero reserved across scheduler and sourceboot.
Successful publication now orders target bank commit, exact actual-result ack,
telemetry refresh, presentation, and cadence; failure cannot present/append.
Focused source coverage is GREEN 30/30, scheduler nominal PASS, and all three
mutations are caught. No target or FPS claim exists yet.

2026-08-05 Task 9 Step 5 final rereview: **GO; TARGET BUILD ACTIVE.** Shared
nonzero generation advancement, 30 Hz remainder pacing, publish-before-ack
ordering, failure suppression, and focused host/source evidence all pass.
Legacy `vblank_credit` names are retained, but the A9 unit is whole discarded
30 Hz simulation-tick credits rather than raw fields; this is documented before
capture interpretation. No target or FPS result exists yet.

2026-08-05 Task 9 Step 5 target build: **PASS; CADENCE CAPTURE ACTIVE.** The
DLL-safe, forced, serialized build exits zero in 331 seconds. Exact ELF
`6685d058d876119118b2a5a65a5a682111a29596aff4ce24901954ee6073f689`
is 8,694,212 bytes; ISO is `7fbb6427...ab1834b`; CUE is
`cdbf0bfa...f46dba7`. A subsequent broad `make verify` rebuild passes narrow
source tests but exits on the retained native-math census error for
`_play_cutscene -> _cutscene_bbh_death`; that gate remains unchecked.

2026-08-05 Task 9 corrected exact capture: **RUNTIME UPLIFT GREEN; MEASUREMENT
REREVIEW ACTIVE.** Ten edges complete with one sim tick per edge, 12--14 real
fields per edge, and `QN=QR=10`, `QW=QF=QQ=0`. Corrected exact result is 4.463
FPS mean and 4.286 median/1%-low, 2.752x / +175% over the 1.622-FPS Step 0
mean. Summarizer TDD now separates ISR-field time from source presentation
generation and passes 29/29; manual Ymir remains open.
Independent measurement rereview is GO: exact artifact identity, event
coherence, ISR deltas, FPS math, and queue retirement all verify. Explicit
mixed-clock and generation-mismatch negative tests pass; checkpoint is active.

Checkpoint `36f4fe58` lands the reviewed A9 adapter, scheduler repairs,
measurement repair, and exact 2.752x evidence. Manual desktop Ymir is active;
the broad native-math census remains separately unchecked.

2026-08-05 Task 9 manual launch: **ACTIVE; OWNER OBSERVATION REQUIRED.** Desktop
Ymir PID 34856 uses the exact CUE/ISO and project 32-Mbit DRAM profile and is
live/responding after 20 seconds. Launch report is
`a9-step5-desktop-launch-2026-08-05.json`. Speed, controls, and geometry remain
unchecked until the owner reports what is visible.

2026-08-05 Task 9 Step 6: **SOURCE COMPLETE** (`2377bf8b`). VDP2 now accepts
only a bank-owned immutable camera and displayed/rendered/simulation metadata,
labels the tuple in HUD output, force-refreshes it on tuple change, and
fails closed before VDP2 callbacks on camera/bank mismatch. Direct host VDP2
and runtime-contract binaries pass; sourceboot-boundary mutations pass 7/7;
`git diff --check` is clean. Independent rereview, target/manual, and
native-math gates remain unchecked. This mixed ledger entry is deliberately
unstaged.

2026-08-05 Task 9 Step 6 Fix Round 1: review of `2377bf8b` was specification
**NO-GO** solely for absent governing-plan/ledger records and code **APPROVED
WITH MINOR FOLLOW-UP**. The governing plan, `STATE.md`, and evidence now record
the source-complete transition; direct VDP2 fixture checks confirm zero
displayed or simulation generations cause no backend effects. This exact Step 6
ledger hunk is staged surgically; focused rereview, target/manual evidence, and
broad native-math remain unchecked.

2026-08-05 Task 9 Step 6 Fix Round 2 rereview: **PASS / APPROVED; SOURCE
COMPLETE.** The reviewer confirms the Task 5.7 prose is contiguous, Step 6
records begin at a valid ledger boundary, and the displayed-zero fixture
isolates the explicit reserved-zero guard. All prior findings are addressed.
Target capture, manual Ymir, and broad native-math remain unchecked.

2026-08-05 Task 9A plan transition: **ACTIVE PLAN; IMPLEMENTATION NOT STARTED.**
Insert true frame-lifetime overlap before Task 10: start immutable render `N`
and return, retain its snapshot/payloads/BUILDING bank through PENDING, allow
only the master-owned queued source tick for `N+1`, then finalize/merge/lower
`N` once after positive slave retirement. FAILED quarantines without full-
frame replay; one active render generation, A9 cadence/publication laws, A8
transport ownership, and master-only source/final/presentation ownership remain
fixed. Lifecycle/wrap/reuse/failure/scene-neutral tests, both reviews, and the
single post-review serialized target build/capture remain unchecked. Task 10
is hardening/publication, not the next expected FPS lever. This mixed ledger
 entry remains deliberately unstaged.

2026-08-05 Task 9A Steps 1--3: **RED COMPLETE; IMPLEMENTATION ACTIVE.** The
new fake-hook lifecycle fixture fails on the absent production lifecycle
module, the source adapter contracts fail on the monolithic render call and
missing retained PENDING transaction, and the cadence fixtures fail on the
60-byte/version-1-only decoder and absent overlap-window phase. Exact commands
and failure summaries are in the aggregate evidence report. No target, Ymir,
broad verify, review, or native-math gate ran. This mixed ledger append remains
deliberately unstaged.

2026-08-05 Task 9A Steps 4--9: **SOURCE IMPLEMENTED; FOCUSED HOST GREEN;
REVIEW NEXT.** The monolithic demo-render entry is deleted in favor of one
exact-generation start/poll lifecycle. Sourceboot retains snapshot/payload/
BUILDING-bank ownership across PENDING, finalizes only after positive slave
retirement, and quarantines failure without replay; A8 remains the transfer and
publication owner. The existing scheduler required no production change and
now has a long-pending/queued-N+1 proof. Cadence v2 is 76 bytes/19 words with a
non-additive slave overlap window and separate master finalization; explicit
v1/60-byte decode remains. All eight required focused Step 8 commands pass,
capture tests pass 31/31, cadence source tests pass 3/3, and lifecycle/frame
mutations are caught. RED checkpoint is `ec81ddc6`; implementation is
`0f5ccd65`. Steps
10--12, target/Ymir, broad verify, and native-math remain unchecked. This mixed
ledger append remains deliberately unstaged.

2026-08-05 Task 9A Fix Round 1: **REVIEW FINDINGS REPAIRED; REREVIEW NEXT.**
Independent review of `0350a473..d45c0a41` was specification/code-quality FAIL
and target-build NO-GO. Watched RED reproduced missing production integration
and incomplete phase attribution. GREEN now defers scene/LOD reset until exact
generation retirement, timestamps actual lifecycle notify/retire events,
attributes first-service plus terminal construction, and refreshes quarantine
telemetry before reset. The production-linked integration fixture passes and
catches active-reset, omitted-start, pre-notify, and skipped-refresh mutations;
capture tests pass 32/32, cadence source tests 4/4, and frame integration tests
9/9. Direct/saved v1 decode remains supported; live observation requires v2.
Fresh two-stage review, target/Ymir, broad verify, and native-math remain
unchecked. Fix Round 1 implementation is `24528bf6`. This mixed ledger append
remains deliberately unstaged.

## 2026-08-05 — Task 9A Fix Round 2 source remediation

- Reconciled base `420b6ce8` with the FAIL/NO-GO review of
  `d45c0a41..420b6ce8`; Step 10 and all target/manual/broad gates remain open.
- Watched RED: target-coherency contract failed 3/3; the exact integration
  compile failed on the absent runtime marker type/API.
- GREEN: worker-visible LOD lifetime moved to P2 `.uncached`; runtime notify
  and retirement hooks carry exact release-site timestamps and publish phase
  state before cross-CPU visibility; integration combines deferred scene reset,
  failure quarantine, reset ordering, and `QF=1, QQ=1`.
- Focused commands green:
  `verify-render-overlap-integration` and
  `verify-render-job-runtime verify-demo-render-overlap`. Normal paths pass;
  all six integration mutations and all three lifecycle mutations are caught.
- Prior-art/reuse record unchanged: pinned SlaveDriver/Z-Treme/Yaul/Jo Engine/
  sm64-psx sources remain dependency/API or pattern-only; no upstream source
  copied or closely ported.
- No target build, CUE, Ymir, broad verify, or native-math census was run.
- Scoped implementation/docs commit: `162f2a7d`.

## 2026-08-05 — Task 9A Step 11 capture observer Fix Round 3

- Sole target build retained unchanged; exact ELF is
  `5afbc7527bf470e9c9b099d5874f13030f4a4406dc93d9a751b057584c3065f0`.
- First report failed before Ymir at symbol resolution because the observer
  required legacy 92-byte `s_runtime`; reviewed marker runtime is 104 bytes.
- Watched RED ran 35 tests with three exact layout errors. GREEN passes 35/35.
- Observer accepts only source-validated layouts 92/telemetry-28 and
  104/telemetry-40, reads the resolved size, and rejects unknown nearby sizes.
- Read-only exact-ELF resolution selects 104/40. No rebuild, Ymir, capture
  retry, FPS, broad verify, or native-math census occurred. Independent review
  remains required before retry.
- Scoped repair/docs/evidence commit: `39b99c21`.

## 2026-08-05 — Task 9A Step 10 source review closeout

- Fix Round 2 commits: `162f2a7d`, `050aa3bc`.
- Scoped rereview of `420b6ce8..050aa3bc`: specification PASS, code-quality
  PASS, no Critical/Important/Minor findings.
- C1 target-coherent LOD ownership, I1 exact release-marker timing, and I3
  production-path assurance are closed.
- GO for exactly one serialized Step 11 target build/capture. Actual ELF/map
  P2 placement, memory margins, manual Ymir, broad verify, and native-math
  remain unchecked.

## 2026-08-05 — Task 9A Step 11 target build / first capture

- Sole DLL-safe forced `-B -j1` build PASS in 334.1 seconds.
- ELF `5afbc752...3065f0` (8,745,796 bytes), ISO
  `1d5f55f2...ab5411` (4,679,680 bytes), CUE
  `cdbf0bfa...f46dba7` (88 bytes).
- First capture failed closed before Ymir startup: `s_runtime` is 104 bytes in
  the reviewed target while the observer contract requires 92.
- Failed report: `a9a-step11-overlap-throughput-2026-08-05.json`.
- Capture/FPS, linked P2 addresses, and memory margins remain unchecked. Repair
  and review the observer against the exact ELF; no target rebuild.

## 2026-08-05 — Task 9A Fix Round 4 HWRAM boot repair

- Observer retries against unchanged ELF `5afbc752...3065f0` failed target
  identity after 600 and 4,096 startup VBlanks. Both reports are retained; no
  FPS/runtime claim is credited.
- Exact failed map: `___bss_end=0x060FD7D0`, P2
  `.uncached=0x260FD7D0+0x6900`, `___end=0x061040D0` (`0x40D0` beyond HWRAM).
  The old subtract-first margin assertion wrapped and accepted the image.
- Watched RED: target-coherency contract 3 failures/2 passes; sourceboot
  memory-map contract 3 failures/10 passes. Missing behavior covered explicit
  HWRAM overflow, bulk LOD placement, cached-P1 exclusion, `.uncached`/`___end`
  identity, and route-0 LWRAM floor.
- GREEN: bulk primitive tiers and cluster LOD state share one `.lwram_bss`
  object and one canonical P2 accessor for both CPUs; the small lifetime record
  remains `.uncached`. Linker and verifier now check both WRAM tops before
  margins. Focused source tests pass 5/5 and 13/13; real production-linked
  integration passes and rejects all six mutations. Exact old ELF now fails
  explicitly `ELF end is past HWRAM top`.
- Projected margins only: about `0x2168` HWRAM and `0x74E0` LWRAM. Serialized
  repaired build/map, boot identity, capture, FPS, manual Ymir, broad verify,
  and native math remain unchecked.
- No target build, Ymir launch, or capture ran for Fix Round 4. This mixed
  progress ledger append remains deliberately unstaged.
- Scoped implementation/tests/docs/evidence commits: `d8dfe35f` and
  `1e1fbe92`. Independent review is specification PASS and quality PASS with no
  Critical or Important findings; its sole stale-status Minor is corrected in
  the tracked plan. The reviewer authorizes exactly one serialized repaired
  build, fail-closed exact map validation, then exact-artifact identity/boot
  capture.

## 2026-08-05 — Task 9A repaired rebuild and verifier stop

- Documentation review closure committed as `6e0397ba`.
- The sole authorized forced `-j1` DLL-safe rebuild passed in 331.4 seconds.
  No missing-DLL interruption occurred.
- Exact hashes: ELF `1905ec8d...fc2e2`, ISO `1ccaef4f...cfaf96`, CUE
  `cdbf0bfa...f46dba7`.
- Exact map: `___bss_end=0x060FD810`, P2
  `.uncached=0x260FD810+0x6C8`, `___end=0x060FDED8`, HWRAM margin `0x2128`;
  `.lwram_bss=0x00220000+0xD8B10`, LWRAM margin `0x74F0`.
- The exact-ELF verifier failed before capture because `.uncached` is
  `PROGBITS`, while its contract requires `NOBITS`. The map attributes the
  initialized portion to Yaul `.uncached.function` cache-helper code. No Ymir
  launch or FPS claim occurred.
- Next gate: watched fail-closed verifier correction, independent review, then
  validate and capture this same hash-bound image without rebuilding.
- Independent read-only audit verdict: require `PROGBITS` exactly. Pinned Yaul
  `6012f79f237773378c8014e70d8998ad95a38d98` (MIT) proves the section combines
  initialized dual-CPU state and executable `.uncached.function` cache code;
  `NOBITS` would discard required bytes. Reuse is pattern-only/contract
  alignment with no copied upstream code.
- Verifier correction `cfb07a7d` used watched RED/GREEN and now requires
  `PROGBITS` exactly while retaining every P2/end/margin/LWRAM predicate.
  Focused tests pass 15/15. The same ELF `1905ec8d...fc2e2` passes exact-map
  validation with 8,488 HWRAM and 29,936 LWRAM bytes free; JSON evidence is
  `a9a-step11-repaired-memory-map-2026-08-05.json`. Independent review remains
  the only gate before hash-bound boot/identity capture; no rebuild/Ymir ran.
- Independent specification review is PASS/GO with no Critical or Important
  findings. Two documentation-only Minors (stale target/map status and an
  imprecise JSON loadability key) were corrected. Code-quality review remains
  open before capture.
- Independent code-quality review found no implementation Critical, Important,
  or Minor. Its sole documentation-only Important was contradictory text that
  could trigger another rebuild and replace the hash-bound artifact. The plan
  and aggregate report now authorize only same-ELF/CUE identity/Ymir capture,
  with no rebuild or test rerun. Quality GO is satisfied. Paused before Ymir at
  the user's request.
- Resumed exact-artifact capture completed. Target identity matched after 540
  startup VBlanks. Nine intervals report 5.2941176471 FPS mean, 5.0 median,
  and 5.0 1% low; sequence/retirement 10/10, notify/retire 10/10, `qw=0`,
  `qq=0`, and zero master/slave failures. Capture report:
  `a9a-step11-overlap-throughput-repaired-2026-08-05.json`. This closes Step 11
  automated runtime evidence; manual visual/controls acceptance remains open.
- Exact repaired CUE launched through the profile-backed desktop helper with
  the project 32-Mbit DRAM profile. Launch report:
  `build/saturn/ymir-desktop-launches/a9a-repaired-manual-20260805.json`.
  The final visible launch used PID 29164 and showed the loaded sourceboot
  disc; owner observation subsequently closed Step 6.
- Owner confirmation now closes manual Step 6: controls and camera are normal,
  BOB traversal/visual stability are acceptable, and the scene holds 4--6 FPS.
  Task 9A is complete for this demonstration; Task 10/A10 is active at its
  source-contract hardening step. Broad native-math and non-BOB/full-game gates
  remain open.

## 2026-08-05 — Task 10 Step 1 source-contract hardening

- Added `tools/saturn/test_overlapped_pipeline_source.py` and ran it with the
  bundled Saturn-tools Python: **3/3 passed**.
- RED exposed that `SATURN_RENDERER_PIPELINE` was only an output-directory
  label. The Makefile now rejects unreviewed values (only `2`, `3`, `4`) and
  passes the selector to `SH_CFLAGS` as `-DSATURN_RENDERER_PIPELINE=...`.
- The source closure contract names the frame scheduler, overlap phase,
  immutable snapshot/job/output banks, VDP1/VDP2 paths, DMA queue, dual worker,
  and Z-Treme frustum sources. This is source-complete for Step 1; the full
  serial host gate and linked target census remain unchecked for Step 2/4.

## 2026-08-05 — Task 10 Step 2 source-contract reconciliation

- The first serial `test_*source.py` sweep exposed two stale assertions:
  `test_render_job_graph_source.py` still prohibited the now-live graph include,
  and `test_task8_lod_source.py` required literal adjacency that the accepted
  display-suppression boundary intentionally places between the game tick and
  scene observer.
- Reconciled both tests to the current reviewed contracts, then reran all nine
  source-contract scripts serially: **all passed**. This is only the source
  slice of Step 2; verify-tools, runtime contracts, dual-frame-bank, generator,
  native-math, and linked target gates remain unchecked.
- The separately dirty native-math verifier suite was also sampled (219 tests):
  218 passed and one errored in
  `test_checked_in_renderer_oracle_matches_source_worker_edge` because the
  current dirty tree has zero `terrain_worker` definitions. This is not caused
  by the Task 10 selector change and remains an external native-math census
  blocker; no target build is authorized while it is unresolved.
