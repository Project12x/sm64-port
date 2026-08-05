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
