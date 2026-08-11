# State

Active prerequisite plan:
`docs/superpowers/plans/2026-08-07-task16-completion.md`.
The parent acceptance/release plan remains
`docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md` and
is paused at Task 10 until this prerequisite changes and reseals the target.

## Current lane: hermetic integrated release candidate

Task 9 repair round 2 is complete after both controller-owned same-reviewer
rereviews passed. Candidate source `df7894ac` produced two clean,
identical `-j1` releases at manifest `9110b40d...b99` and identity
`id-a40f992c085da2f0`; exact v4 passes at total 700 with both forbidden
callers absent, and capacity/package/staging/overwrite-refusal gates pass. The
previous release/audit/stage evidence is superseded.

Task 10 is `blocked-on-prerequisite` at its exact 20,100-frame smoke. The manifest-bound target
boots with exact code/identity, completes cart loading, and keeps a clear
exception record, but the first feature-on ACTOR_ADMIT job fails and its
dependent ACTOR_LOWER job is quarantined. The sealed profile enables
`SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`, while that production renderer path
is still the intentional fail-closed stub documented by the Task 16 completion
plan. Visual capture, desktop launch, and owner play remain stopped. The
required fix is the Task 16 production generic actor cutover; it changes target
bytes and therefore reopens Task 9 rebuild, reproducibility, v4, and staging.
The cutover's missing data prerequisite now has an approved written design:
`docs/superpowers/specs/2026-08-11-saturn-generic-actor-bundle-design.md`.
It is a Saturn-shaped S64F v3 scene bundle in the fixed DRAM-cart residency
region, with bounded 64-family/128-variant offset tables, embedded S64B banks,
  two fixed LWRAM SH-2 workspace lanes, scalar generation-last publication, and
  lease-drained master-owned CD scene transitions. Its implementation plan is
  written at
  `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md`;
  implementation has not begun, and completing/reviewing that plan remains the
  gate before Task 16 Task 2 can resume.
The reusable
`sm64-saturn-full` profile and identity/release architecture exist, but that
profile remains deliberately non-releasable until its complete content/system
inventory and game-wide target gates are implemented and reviewed.

The full-game completeness sprint preserves every completed A9A/Task 10 commit
and evidence item, but does not supersede Task 10's next-acceptance ownership.
It interleaves
complete source-selected Mario animation, generated BOB actor/effect closure,
full semantic music/SFX, and continued renderer/runtime optimization through
one reviewed integration spine. Whomp's Fortress area 1 is the second-level
package-generation/load proof. Intermediate feature regressions are measured
but do not block merges; the final all-features BOB build must measure at least
4.0 mean presentation FPS in the pinned setup (the accepted observed band is
4--6 FPS) before the next 12--15 FPS sprint.

Implementation begins with the existing dirty native-math verifier/oracle
repair, then immutable feature/package identity and the transitive scene
closure. The accepted rollback remains BOB with live input, Q16 camera,
32-Mbit DRAM cart, and renderer Pipeline 4. The detailed plan has 30 named,
reviewed tasks, immediate plan/ledger updates at every transition, and
serialized `-j1` build/Ymir gates for the busy host. The governing plan now
has 30 named tasks, including the explicit VDP2 gameplay-HUD task 23A.

## Current lane: memory-residency campaign

The prior short-term stability lane is now **closed, not merely paused**.
The geo-walk cutover from the production recursive traversal to the bounded
dual-SH2 enter/dispatch/leave runtime is COMPLETE: the geo source-policy
gate reports zero unaccounted recursive calls, and the now-dead reentrancy
guard was removed (`5ba8d85c`..`dd81d616`). Six further fix commits landed
2026-08-09 on top of that closure: `1eb30fee` (right-size the
geo-traversal arena via 16-frame capacity alignment), `2c08b009`
(const-sweep cold HWRAM `.data` tables to cart rodata), `b1f456a5` (halve
closure-mode SFX sample rate to fit the BOB resident budget), `a6c2032a`
(make the VDP2 HUD visible via CONFIG_3 PND encoding and sprite priority
cap), `16007c4d` (move HUD counters/timer/power-meter to the top of frame,
matching source), and `b9679f57` (clear the sticky SH-2 DIVU overflow flag
before atan2/native-math divides, fixing BOB's permanent camera/display
freeze). The first owner-played manual session on the resulting textured
demo build (identity `id-1335252b7f9383a6`) confirmed textures, HUD, and
the camera-freeze fix hold at a stable 2--4 FPS.

That manual acceptance closes the stability lane. The new active lane is
the memory-residency campaign
(`docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`); its
driving number is a measured **12,408-byte HWRAM link deficit** on the
flags-on (`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`) demo-path build, which must close
before that build can link and run at all. This remains **not a scope
reset**: it is the next Saturn-shaped prerequisite gate for the same
full-game sprint, and it must preserve the accepted 4--6 FPS BOB rollback
and the full-game ownership model.

Once the campaign closes that deficit, return to the larger track at the
still-open production audio bridge and package residency (Task 21),
generated BOB dynamic closure, semantic audio, and the VDP2 gameplay HUD
(Tasks 22--23A), final-draw and transfer overlap (Tasks 24--26), Whomp's
Fortress portability (Task 27), the complete feature matrix (Task 28), and
all-features FPS recovery/publication (Task 29). The 12--15 FPS sprint
remains downstream of that sequence, not replaced by either prerequisite
lane.

Task 9A/A9A is **rebuilt, exact-map green, independently reviewed, and captured
at runtime**. Specification is PASS; code-quality review found no
implementation defect. Its sole documentation-only Important—stale guidance
that could replace the hash-bound artifact with another rebuild—is corrected.
The accepted renderer still exposes exact-generation
`start_frame(N)` and `poll_frame(N)`: start publishes immutable jobs and
returns after one slave notification; poll waits for positive retirement,
then drains, merges, lowers once, and retires. Failure quarantines without
full-frame replay, and snapshot `N`, descriptor payloads, and its BUILDING bank
remain owned while the one queued master source tick advances `N+1`.

Step 11's sole forced target build passed in 334.1 seconds with ELF
`5afbc752...3065f0`, ISO `1d5f55f2...ab5411`, and CUE
`cdbf0bfa...f46dba7`. Fix Round 3 repaired the initial observer-layout defect
without rebuilding. Retries against that unchanged exact target then failed
identity after both 600 and 4,096 one-VBlank startup attempts. Both logs reach
disc authentication and load `A.BIN`, then produce no target identity. No FPS,
runtime phase, or target acceptance is credited.

Read-only inspection of that exact ELF identifies the boot defect:
`.uncached` begins at P2 `0x260FD7D0`, has size `0x6900`, and drives
`___end=0x061040D0`, which is `0x40D0` bytes past physical HWRAM. The previous
linker expression subtracted `___end` before checking the upper bound, so the
unsigned underflow satisfied the margin assertion. This diagnosis is strongly
consistent with the startup failures, but repaired-image boot remains an open
target gate.

Fix Round 4 moves only the bulk primitive-tier/cluster-LOD storage into one
`.lwram_bss` object and makes one cache-through accessor the canonical P2
address used by either SH-2. The small lifetime/generation record remains P2
`.uncached`. Linker and ELF verification now reject HWRAM overflow before
margin subtraction, require the P2 `.uncached` physical end to equal
`___end`, and enforce the final `0x4000` LWRAM margin for route 0 as well as
capture routes. Focused source/layout tests and the real production-linked
integration are green, including all six mutations. The authorized forced
`-j1` rebuild passed in 331.4 seconds. Exact ELF `1905ec8d...fc2e2` ends at
`0x060FDED8`, leaving `0x2128` HWRAM; `.lwram_bss` ends at `0x002F8B10`,
leaving `0x74F0` LWRAM. P2 `.uncached=0x260FD810+0x6C8` maps exactly to
`___end`, and is correctly emitted as `PROGBITS` because it includes Yaul's
`.uncached.function` cache helpers and initialized slave entry. Commit
`cfb07a7d` corrects the verifier to require `PROGBITS` exactly; 15/15 focused
tests and the exact hash-bound ELF now pass. The automated capture reports
5.294 FPS mean, 5.0 median/1% low,
identity match after 540 VBlanks, ten queue retirements, and zero queue or
worker failures. Manual visual/controls acceptance is complete: owner confirms
controls and camera are normal, BOB looks stable, and the visible result holds
4--6 FPS. No rebuild is authorized for that acceptance. The exact CUE is now also launched in visible
desktop Ymir with the profile-managed 32-Mbit DRAM cart; the window remains
alive/responding. Owner reports the scene looks okay and holds 4--6 FPS,
better than the prior 3--4 FPS range.

Task 10/A10 source-contract hardening evidence is retained, but execution is
paused behind Task 9 repair/rereview. Task 10 is the next active acceptance
lane; broad native-math verification and non-BOB/full-game coverage remain
open.

The unchanged invariants are master-only simulation/input/live state/
allocation/final order/VDP1/presentation, one active render generation, A9's
nonzero successor and 30 Hz remainder, one normal plus one recovery tick,
per-field service/poll epochs, exact publish acknowledgement, previous-frame
reuse, and A8's transfer ownership. Focused evidence now covers lifecycle
exact-once behavior and three mutations, pending scheduler generations, wrap,
missed deadline/reuse, failure/no replay, scene-neutral generic state, retained
A8 ownership, and versioned cadence decoding. The v2 target record is 76
bytes/19 words; it separates source tick, the non-additive slave overlap
window, and exclusive master finalization while retaining explicit v1/60-byte
decoding. Only after two-stage review may one serialized DLL-safe target
build/capture run. Task 10 is hardening/publication, not the next expected FPS
lever.

Task 5/A5.9 is closed after completing the atomic cutover from fixed
terrain/Mario workers to one dependency-aware descriptor queue and observing
that queue on the exact target image. Task 7/A7 is complete after consolidated
review found and repaired stale publication, untruthful emitter completion,
and storage-alias gaps. Independent rereview is PASS/APPROVED. The original lifetime
boundary landed at `650b911a`; the repair landed at `41a4ce7e`. The two VDP1
command/Gouraud source-bank lifetimes are now explicit before A8 defers their
transfers. The accepted A5 frame publishes
four coarse admit/lower jobs, publishes self-contained callback contexts before
notification, lets both SH-2s claim work, requires terminal descriptors plus
positive slave retirement, then performs final assembly and VDP1 lowering on
the master. Independent source reviews, the serialized target build, exact
ELF identity, and automatic desktop/headless evidence are green. The live
record splits the four jobs two-and-two across master and slave with no wait,
failure, or quarantine, while cadence remains 4.8 FPS mean (4.87 median).
Therefore the next optimization attacks admitted work volume and the
master-only final merge/VDP1 transfer path rather than assuming an idle slave.

A7 replaces the frame-local XOR selector with a two-bank lifecycle manager.
Only an explicitly successful render may become READY; transfer obligations
must retire before publication; zero Gouraud work is an explicit NOOP; and a
failed build is quarantined while the previous complete publication remains
available. Build, published, and displayed generations are no longer aliases.
The linked SH-2 image places `.lwram_cmdts` at `0x00200000` as a `0x20000`-byte
NOBITS section and `sourceboot_gouraud_staging` at `0x060D8FB8` with exact size
`0x6000`; `___end=0x060FC86C` leaves `0x3794` HWRAM bytes, above the required
`0x1B00` floor. A7 does not defer the renderer's waits and therefore makes no
FPS claim. The broad target verifier remains open on the pre-existing
native-math oracle error `_play_cutscene -> _cutscene_bbh_death`; A7 itself
compiled, linked, and passed its memory-map checks.

The A7 review repair is focused- and target-green. Wrap-safe publication
ordering quarantines late completion; both emitters fail before command upload
after two invalid Gouraud submissions; and manager initialization rejects
physical aliases, overlap, and misalignment. Fresh Pipe4 ELF SHA-256 is
`1eba88885b611f0c99dea3971dda871fcc30fdb8ac21c4fcfc5e051f0e99267c`.
The exact audited Route0/live-input/Pipe4 `make -B -j1` exits zero in 336.9
seconds. A8 deferred transfer is now active next; no A8 behavior, Ymir result,
or FPS evidence is claimed.

A8's first independent review returned NO-GO; its source repairs are committed
at `8b037a7d`, and the contract and quality rereviews are now PASS/APPROVED.
Both renderer paths stop after command/Gouraud construction. Queue-owned
CPU-DMAC channel 0 uses public config/start plus a completion IHR because
pinned Yaul busy status can report false idle. Sourceboot services the serial
CPU/SCU lane during stale iterations, suppresses presentation while the single
VDP1 destinations are partial, and permanently fails closed after any partial
resident-write failure. Published banks carry their immutable VDP2 camera and
snapshot generation. The ordinary path explicitly reports zero at nonexistent
transport/terminal waits. CPU-DMAC and SCU-DMA remain serial by design. The
first target attempt exposed a host-mock blind spot: Yaul's target
`VDP1_VRAM(0)` is an integer address, not a pointer. The explicit
`(void *)(uintptr_t)` conversion and a source regression contract repair that
compile failure. The exact incremental Route0/live-input/Pipe4 target build
then exits zero and produces ELF SHA-256
`5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.
A8's first automatic Ymir capture exposed a zero-actor runtime failure. The
scene-neutral repair publishes a two-job terrain graph when actor meshlet
admission is empty and retains the four-job graph when Mario is visible.
Independent review then found that the old count return conflated successful
culling with actor-preparation failure. The repaired boolean-success plus
out-count contract retains fail-closed errors. The final target ELF SHA-256 is
`10e92064175f1d277039322f6be874f646b71a486c7f18ccd8a2e9786df569ab`.
The repeat exact capture is runtime-green: both queue shapes retire, VDP1/VDP2
presentation advances, DMA is idle after retirement, and transfer faults,
bank-unavailable skips, overwrite waits, transport waits, and terminal waits
are all zero. A deeper exact capture records ten presentation events and nine
stable 36--38-field intervals: 1.63 FPS mean, 1.62 median, and 1.58 1%-low.
`QN=QR=10`, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and `QW=QF=QQ=0`. This is a
measured regression from the 4.8-FPS A5.9 baseline, not an A8 uplift; the next
investigation must split CPU construction/simulation cost because the
deferred-transfer wait counters do not explain the 37-field cadence.
A9 is active at an evidence-first Step 0: sample wrap-safe deltas from the
existing simulation and render/construction timing counters at each exact
presentation edge. Scheduler behavior remains unchanged until that split
identifies which CPU phase owns the stable 36--38-field interval.
Inspection corrected the telemetry design: the existing phase accumulators
lose 16-bit FRT wraps at this cadence and are not explicitly cache-through.
Step 0 therefore uses a small P2-published VBlank-crossing trace rather than
claiming absolute time from those counters.
Step 0 is now exact-target complete at ELF `1ffb47cc...e4edfe6`. Nine intervals
account for all 333 fields: simulation 283 (85.0%), construction 49 (14.7%),
transport/presentation 1 (0.3%), unattributed 0. Six simulation ticks execute
per presented frame and 222 additional credits are dropped. The root cause is
now concrete: the two-tick catch-up bound resets per outer iteration, so three
iterations repeat it before one presentation. Task 9 Step 1 is active to make
that budget presentation-generation scoped.
Task 9 Steps 1--4 reached first source-complete: a hardware-free scheduler keeps the
two-tick budget across previous-frame reuse and resets it only after complete
publication, carries queued N+1 work into the next budget, bounds service/poll
to once per VBlank, and rejects stale/incomplete completion. Direct normal and
three mutation executables pass, but independent review is NO-GO before Step
5: wrapped generation zero aliases unset sentinels, same-field publish can
reopen SERVICE/POLL for a promoted generation, and the gate is absent from
`verify-all`. The integration seam also proves publication must be two-phase:
the scheduler cannot claim a displayed generation until the fallible target
publish sequence acknowledges exact-generation success. Wrap-safe validity,
field-scoped epochs, publish acknowledgement, and regression coverage are now
source-complete. Fresh direct nominal and three mutation runs pass their exact
contracts, and the gate is now in `verify-all`; independent rereview is GO.
Step 5 is active with its adapter contract independently RED 7/7 against the
legacy loop.

Step 5's source adapter now passes 29/29 focused contracts, but pre-target
review reopened the scheduler: elapsed VBlanks currently become simulation
credits 1:1, making healthy source logic 60 Hz rather than 30 Hz. A two-field
fractional accumulator is active repair scope. No target build or FPS claim is
permitted until it and rereview pass.

The 30 Hz accumulator now passes review. Combined review still blocks target
build because presentation telemetry is emitted before scheduler publication
acknowledgement and because scheduler generation zero conflicts with downstream
snapshot/frame-bank rejection. Ack-before-evidence ordering and a consistent
skip-zero generation policy are active parallel repairs.

Both combined-review repairs are source-complete: scheduler and sourceboot use
the same nonzero successor, and publish acknowledgement now precedes telemetry,
presentation, and cadence evidence. The focused adapter/source sweep is GREEN
30/30; scheduler nominal and mutation contracts remain GREEN. Final rereview is
active before target compilation.

Final combined rereview is GO. Steps 1--5 are source-complete and the first
serial target build is active. Legacy `vblank_credit` field names are retained,
but their A9 values now mean discarded whole 30 Hz simulation-tick credits, not
raw VBlank fields; captures must not compare those units without conversion.

The first serialized Step 5 target build passes in 331 seconds. Exact ELF is
`6685d058...6073f689` (8,694,212 bytes), ISO `7fbb6427...ab1834b`, and CUE
`cdbf0bfa...f46dba7`. The broader verifier remains red only at the retained
native-math census error for `_play_cutscene -> _cutscene_bbh_death`; its broad
gate stays unchecked. Exact-ELF cadence capture is active.

The corrected exact capture proves the death spiral is gone: each of nine
intervals has one simulation tick instead of six, field deltas are 12--14, and
all ten queue generations retire without waits/faults. FPS is 4.463 mean and
4.286 median/1%-low, 2.752x / +175% over the exact 1.622-FPS Step 0 baseline.
The summarizer's decoupled-clock regression is fixed with 29/29 tests and
measurement rereview is GO with exact artifact identity and coherent 10-edge
evidence. Mixed-clock and cadence/event-mismatch negative cases pass. The
checkpoint is active; manual Ymir remains next.
Checkpoint `36f4fe58` lands the reviewed A9 adapter and exact 2.752x evidence.
Desktop Ymir manual confirmation is the active A9 gate; the unrelated native-
math census failure remains separately unchecked.
Desktop Ymir PID 34856 is live/responding with the exact A9 CUE and the verified
profile-managed 32-Mbit DRAM cart. Manual speed, controls, and geometry results
await the owner; no observation is inferred from process survival.
The post-repair direct host fixtures for render-job runtime, actor meshlets,
DMA queue, and VDP1 transfer pipeline also exit zero. The aggregate Make gate
did not execute them because the known MSYS-to-Windows path conversion defect
rewrote the worktree path; that wrapper failure is retained as infrastructure
evidence and is not reported as a product-test failure.

Terrain's live WORLD_ADMIT callback publishes transformed-position
completion by exact descriptor identity, and WORLD_LOWER records its exact
result count/sequence/claimant lane before its terminal reader may merge it.
WORLD_LOWER also fails closed unless its one immutable graph predecessor is
the exact completed WORLD_ADMIT descriptor with matching publication metadata.
Terrain merge-span assembly and Mario's equivalent transform/classify/
terminal-merge route are source-complete and host-green; their prerequisite
routes have independent source-review GO, while the combined cutover awaits
fresh review. Mario's route
copies the existing live pose, lighting, frame/bank, position, and yaw state;
it does not replace the animation system already proven by the Castle demo.

The output-offset namespace gate is source-review GO at `db28fd87`, with audit
`c5da1516` finding no critical, important, or minor source-scope defects.
Queue publication now derives one of four physical payload kinds from the
immutable type/callback pair and checks overlap only within that kind. Separate
terrain-admit, terrain-lower, actor-admit, and actor-lower banks may reuse local
offsets; same-kind overlap and malformed type/callback pairs fail closed. The
descriptor remains pointer-free and 16 bytes. Ordered terrain commands and
the pointer-free P2 callback-context publication contract are now host-green:
both terrain and Mario require exact generation/phase/claim identity, bounded
payload size, and cache-through peer selection. The repair following
`891f64b2` makes Mario's dynamic refs inline, terrain's queue snapshot
self-contained, and publication an explicit preclaim preparation step. Its
final prerequisite re-review is GO. Post-cutover target link/cache evidence
is now split: link/section evidence is green, while observed cache behavior
remains open for desktop Ymir.

The first repair re-review found no remaining context/coherency defect but
kept source acceptance NO-GO because part of the mutation matrix covered only
lower phases. All mutations now run against all four phase-specific APIs and
final independent source re-review is GO. The first exact serialized target
build exposed a missing SH-only Yaul cache-header import in the queue source;
the narrow repair and independent review are GO. The one post-review serial
rebuild now compiles, links, and packages with 6,160 bytes HWRAM and 60,368
bytes LWRAM margin and no unresolved symbols. That proof predates the live
cutover; a new target build, cache behavior, CUE/Ymir runtime validation, and
FPS evidence remain open.

The terrain route's claimant lane now reaches classification and every
queue-reachable projected read; its single coarse admit descriptor rebuilds
complete position ownership from the actual claimant, so a slave descriptor
at input offset zero cannot retain half of the removed logical split.

The owner manually tested the fresh A5.8 atomic-cutover CUE in desktop Ymir on
2026-08-04 and still observed roughly 3–4 FPS, matching the accepted A3+A4
candidate rather than improving it. This proves the cutover boots and renders,
but not that useful work is balanced across the two SH-2s. The next active
diagnostic is now source-complete and host-green: append-only profile/HUD
telemetry records per-phase master/slave claims, notified/retired generation,
master retirement-wait iterations, failures, and quarantines. Its delayed-slave
fixture demonstrates that the current notify-then-master-drain order permits
the master to claim all four coarse jobs before the slave runs. This is a
scheduling possibility, not yet target evidence. The first review found and
the implementer repaired a release-order race: retired telemetry now publishes
before the positive retirement marker, pinned by a source mutation test. Fresh
rereview is GO at `e98210ba`. The serialized Route-0/live-input/Pipe4 target
build now compiles, links, and packages with the appended telemetry ABI live,
15,412 bytes HWRAM and 30,800 bytes LWRAM margin, and zero unresolved symbols.
The owner confirmed this candidate still runs at roughly 3--4 VDP1 FPS, so it
is not a performance win. Manual HUD transcription did not produce the queue
values. The active correction is an automatic, host-tested desktop-Ymir
capture path for native VDP1/VDP2/draw measurements and, where the emulator's
debug boundary permits, `QM/QS/QN/QR/QW/QF/QQ`. Those queue values remain open
until captured; they must not be inferred from the host delayed-slave fixture.
The automatic desktop path now has a live exact-CUE proof: ten native Ymir
running-counter snapshots after warmup give VDP2 median 60 FPS and VDP1 median
4 FPS, range 3--4.
The emulator remained alive. This closes manual title transcription, not the
queue-counter gate or A5.9 independent review.
The first review was NO-GO because unchanged title strings could not prove ten
distinct rollover intervals, even-sample medians truncated, failures lacked a
JSON report, and Win32 types were implicit. The repair uses honest snapshot
wording/timestamps, fractional medians, structured failure evidence, exact
process-liveness checks, and explicit Win32 prototypes. Fourteen focused tests
pass; rereview is GO. Failed reports also retain stage, PID when available,
and adjacent log paths.
The final repaired collector then attached to the same running desktop process
for five more snapshots: VDP2 median 60 FPS and VDP1 median 3 FPS, range 3--4.
The combined automatic evidence therefore confirms sustained 3--4 VDP1 FPS.

The queue observation boundary is source-complete, reviewed, and live-green:
`capture_sourceboot_throughput.py` requires the exact matching CUE and ELF,
hashes them with Ymir, proves immutable ELF code bytes are present at their
linked target address, and accepts P2 telemetry only after queue retirement
and both runtime sequence pairs agree. It advances exactly one emulated VBlank
per sample and records VDP2 presentation cadence without inferring a queue
owner from the host delayed-slave fixture. The valid `build-agent2` run records
three presentation events, 4.8 FPS mean / 4.87 median / 4.29 1%-low, and one
coherent sequence: `QN=QR=3`, `QM=[1,1,0,0]`, `QS=[0,0,1,1]`, and
`QW=QF=QQ=0`, with zero master/slave failures. A5.9 automatic observation is
closed. This is diagnostic evidence, not a performance improvement.

The isolated audio lane now has an accepted standalone soundtest proof. Its
2,918-byte source-built 68K image drives four SCSP PCM8 slots, and its generated
CC0 proof bank contains three sounds in 4,408 bytes. Host register, pitch,
endianness, loop/stop, bank, boot-order, bounds, timeout, and ring tests pass.
It is not linked into sourceboot. Independent rereview is GO for the source
and standalone build. On 2026-08-04 the owner manually confirmed audible
A/B/C playback and that X stops playback in desktop Ymir. The supplied screen
also showed READY, an advancing heartbeat, consumed commands, a started voice,
and zero drops. Sourceboot/game integration, automated telemetry, and
transport-cost gates remain open.

Task 9 Step 6 is source-complete at `2377bf8b`: VDP2 composes only the immutable
camera from the displayed VDP1 bank with its displayed/rendered/simulation
metadata, labels the tuple, and rejects mismatched or zero ownership. The
initial independent review was **NO-GO** solely for missing governing-plan and
execution-ledger records, with code **APPROVED WITH MINOR FOLLOW-UP**. Fix
Round 1 reconciled those records and added direct zero-generation coverage.
Fix Round 2 (`7ec933a3`) restored the surgically staged ledger boundary and
isolated the displayed-zero guard. Scoped rereview is **PASS / APPROVED** with
all prior findings addressed. Target evidence, manual Ymir, and broad native-
math remain open.
