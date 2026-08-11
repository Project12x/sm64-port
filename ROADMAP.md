# Roadmap

## Now — verify and play the hermetic integrated BOB candidate

Task 9 of the hermetic full-game release-identity plan is complete after both
same-reviewer rereviews passed. Source
`df7894ac` reproduced two clean `-j1` candidates at manifest
`9110b40d...b99` / identity `id-a40f992c085da2f0`; exact v4,
capacity/package, guarded staging, and overwrite refusal pass. Task 10 reached
the exact 20,100-frame smoke and is blocked there: the flags-on target
intentionally fails the first ACTOR_ADMIT callback because the Task 16 generic
actor production drain is not implemented, then quarantines ACTOR_LOWER and
stops after two source ticks. Visual proof, desktop launch, and owner manual
test remain unopened. Complete the Task 16 production generic actor cutover,
then rerun Task 9's build/repro/v4/staging chain before Task 10.

The staged result is the integrated BOB demo candidate only. The same profile,
closure, identity-v2, audit, release-manifest, capture-binding, and staging
architecture is the deployment path for the eventual total game, but
`sm64-saturn-full` remains non-releasable until all level/shared-data/actor/
animation/audio/texture packages and game-wide target evidence are complete.

## Now — full-game completeness and parallel optimization

Execute
`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`
with fresh implementer/reviewer subagents and immediate task/ledger updates.
Four interleaved lanes deliver all 209 source-selected Mario animations, the
generated complete BOB dynamic-family/effect closure, full original music/SFX
semantics on the MC68000/SCSP, and continued scene-neutral render/runtime
reductions. The generated scene/audio package workflow must also validate and
load Whomp's Fortress area 1 without adding a level-specific frame loop,
renderer, pose evaluator, or audio backend.

The accepted A9A 4--6 FPS build remains the immutable historical rollback.
Intermediate completeness work may regress when independently feature-switched
and measured. After the full-feature build measures at least 4.0 mean
presentation FPS in the pinned setup (4--6 FPS is the accepted band), the
next sprint pursues 12--15 FPS against the representative workload.

### Current execution lane — memory-residency campaign

The prior stability lane is **complete**: the geo-walk cutover to the
bounded dual-SH2 enter/dispatch/leave runtime reaches zero unaccounted
recursive calls at the source-policy gate, and the now-dead reentrancy
guard is removed. Six 2026-08-09 fix commits (`1eb30fee`, `2c08b009`,
`b1f456a5`, `a6c2032a`, `16007c4d`, `b9679f57`) landed on top of that
closure, and the owner's first manual session on the resulting textured
demo build (identity `id-1335252b7f9383a6`) confirmed textures, HUD, and
the camera-freeze fix hold at a stable 2--4 FPS.

The current execution lane is now the memory-residency campaign
(`docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`): a
measured 12,408-byte HWRAM link deficit on the flags-on
(`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`) demo-path build blocks that build
from linking at all. Closing this deficit is the prerequisite gate before
the post-manual-gate sprint's Lane A (Task 16 drain / Task 22 closure) can
resume its A2/A4 target-build steps. SeamAwareDecimater terrain decimation
and the VDP2 CLUT plan run as a parallel fidelity lane against
LWRAM/cart/VDP1, not against this HWRAM deficit — they are not on this
campaign's critical path and instead feed the later 12--15 FPS sprint.
This remains an execution lane inside the full-game roadmap, not a new
milestone or a BOB-only target; the accepted dual-SH2 BOB rollback remains
the comparison point, and no target/FPS claim is promoted until the
campaign closes the deficit.

After that gate, resume the larger sequence rather than restarting planning:
Task 21 production MC68000/sourceboot audio integration; Tasks 22--23A complete
BOB actor/effect, music/SFX, and VDP2 gameplay-HUD closure; Tasks 24--26 command, job-granularity,
and VDP1-transfer overlap; Task 27 Whomp's Fortress package transition; and
Tasks 28--29 the four-feature matrix and all-features recovery to at least
4.0 mean FPS. Only then does the representative 12--15 FPS sprint begin.

## Inherited — Task 10 source hardening and full-game publication

The A9 compatibility adapter is reviewed and exact-target green: it restores
30 Hz source pacing and raises the exact cadence from 1.622 to 4.463 FPS by
eliminating six source ticks per presented frame. It deliberately remains
synchronous. A9A Steps 1--9 now implement the next CPU-lifetime slice: start
immutable render generation `N`, return while its slave work remains live,
permit the master-owned queued source tick for `N+1`, and finalize/merge/lower
`N` only after positive slave retirement. Exactly one active render generation,
the pending snapshot/descriptor payloads/BUILDING bank, A8 transport ownership,
all A9 cadence/publication laws, and previous-complete-frame reuse are retained.
Failure quarantines `N` without replay. Focused lifecycle/source/transfer tests
and the version-2 cadence decoder are green. Independent review then found an
active-generation LOD race plus phase, quarantine, and production-harness gaps.
Fix Round 2 additionally placed the complete worker-visible LOD lifetime behind
the target's P2 `.uncached` boundary. The resulting reviewed target built, but
the repaired observer could not obtain target identity after either 600 or
4,096 startup VBlanks. Exact ELF inspection found `___end=0x061040D0`,
`0x40D0` bytes past physical HWRAM: the bulk tier/cluster arrays enlarged
`.uncached`, while the linker's margin subtraction underflowed and passed.

Fix Round 4 keeps the small lifetime record uncached, moves the bulk tier and
cluster arrays into one LWRAM object, and gives both SH-2s one canonical P2
accessor. The linker and ELF verifier now check HWRAM/LWRAM upper bounds before
margin subtraction and enforce the route-0 `0x4000` LWRAM floor. Focused
source/layout and production-linked mutation gates are green. This is not
target proof; projected HWRAM and LWRAM margins must be replaced by values from
a reviewed fresh ELF.

The authorized serialized rebuild passed in 331.4 seconds. Exact ELF
`1905ec8d...fc2e2` now leaves `0x2128` HWRAM and `0x74F0` LWRAM, and its P2
`.uncached` physical end equals `___end`. The verifier initially demanded
`NOBITS`, while the real linker necessarily emits `PROGBITS`
for Yaul's initialized slave entry and `.uncached.function` cache helpers.
Commit `cfb07a7d` now requires `PROGBITS` exactly; 15/15 focused tests pass and
the same hash-bound ELF is exact-map green. Independent specification review is
PASS/GO with no Critical or Important findings; its two documentation-only
Minors are corrected. Code-quality review found no implementation defect; its
sole documentation-only Important removed contradictory rebuild instructions.
Run boot/identity and bounded overlap/FPS capture against the existing exact
ELF/CUE without rebuilding or rerunning tests. That capture now passes: mean
`5.294` FPS, median/1% low `5.0`, identity match, ten retirements, and zero
queue/worker failures. Owner-visible controls and camera are normal, and the
scene holds 4--6 FPS. Task 10 hardening is now active; broad native-math and
non-BOB/full-game gates remain open.

## Completed diagnosis — post-A8 CPU frame cost

Use the now-proven dual-SH-2 queue as the producer side of a smaller,
deferred VDP1 command stream. First reduce admitted geometry/command volume
before final lowering; then move command transfer behind an explicit
double-buffered ownership boundary. Gate: exact target identity, unchanged
scene/gameplay state, no stale bank, and automatic presentation/queue evidence.

The ordered terrain-command lookup and pointer-free P2 callback-context gate
are source-review GO and target compile/link/section-green. The single-owner
live cutover is source-complete, reviewed, target-green, and manually boots,
but its roughly 3–4 FPS result did not improve on A3+A4. Per-phase claim,
notify/retire, wait, failure, and quarantine telemetry is now source-complete
and host-green. Repair rereview is GO at `e98210ba`; one serialized target
build is target-green with the appended profile ABI linked and both memory
margins intact. The owner confirmed it remains roughly 3--4 VDP1 FPS, but
manual HUD transcription is unreliable. The active tooling correction
automates desktop Ymir's native VDP1/VDP2/draw counters and, if the available
debug boundary supports it, the queue telemetry too. Those live values
determine the scheduler repair. The FPS half is now live-proven: ten automatic
running-counter snapshots report VDP2 median 60 FPS and VDP1 median 4 FPS
(range 3--4). The first review's evidence-durability repairs are host-green
and rereview is GO. The queue half now has a strict-TDD, host-green capture
tool that binds and hashes the explicit CUE/ELF/Ymir artifacts, proves an ELF
code window exists in target memory, and rejects incoherent P2 telemetry while
sampling each VBlank. Queue counter capture is live-green. The exact coherent
record reports
`QM=[1,1,0,0]`, `QS=[0,0,1,1]`, `QN=QR=3`, and `QW=QF=QQ=0` with no CPU
failures. The measured three-edge cadence remains only 4.8 FPS mean. This rules
out idle-slave ownership and retirement waiting as the primary observed
bottleneck, so no speculative queue reschedule is authorized. The next FPS
slice should reduce geometry/command volume before final lowering and move
VDP1 command transfer behind an explicit deferred/double-buffered boundary,
then use A9 frame overlap once bank lifetime is proven.
The A7 source slice is complete: command/Gouraud banks now have explicit
build, transfer, publication, quarantine, and retirement states; renderer
failure retains the previous publication; and build/published/displayed
generations are separate. Its SH-2 image compiles and links with the exact
bank memory-map contract. Independent review and the unrelated strict
native-math census repair are closed by independent PASS/APPROVED rereview and
the exact audited exit-zero Pipe4 rebuild. Because A7 still uses the
synchronous completion adapter around today's blocking renderer, visible FPS
uplift is expected from A8/A9 rather than from this ownership-only slice. A8 is
source- and target-complete with focused host gates green and independent
PASS/APPROVED rereview. Its serial CPU-DMAC/SCU-DMA lane carries transfer
ownership across fields and removes the accepted path's immediate transport
wait. The exact Pipe4 target rebuild exits zero and produces ELF SHA-256
`5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.
The first automatic Ymir run exposed and retained a runtime-red zero-actor
publication failure. The scene-neutral repair now treats zero admitted Mario
meshlets as a valid terrain-only two-job graph while actor-preparation errors
remain fail-closed. The final exact ELF is `10e92064...df569ab`. A ten-event
automatic run yields nine stable 36--38-field intervals: 1.63 FPS mean, 1.62
median, and 1.58 1%-low. All ten queue generations retire with the same 2+2
master/slave split and `QW=QF=QQ=0`. A8 therefore closes a correctness and
lifetime prerequisite but does not improve cadence. Split CPU construction
from simulation timing and use that result to scope A9 overlap; do not spend a
manual-test cycle looking for an uplift the automatic evidence disproves.
The field-resolution split instead proves simulation dominance: 283 of 333
measured fields (85.0%) are spent across six source simulation ticks per
presented frame; construction is 49 fields (14.7%), transport/presentation is
one field, and nothing is unattributed. The two-tick catch-up limit resets on
each outer iteration, allowing six ticks before one presentation and dropping
222 more credits across nine intervals. Fix the scheduler so normal+recovery
credit is bounded per presentation generation. Worker-materialized terrain
commands remain a source-backed later reduction, not the current dominant
lever.
The pure presentation-scoped scheduler model reached first source-complete with
normal, four-tick, repeated-credit, and incomplete-publication coverage.
Independent review returned NO-GO: wrapped generation zero aliases unset
sentinels, publish can reopen SERVICE/POLL during the same field, and the new
gate is not yet in `verify-all`. Publication must also be an intent followed by
exact-generation success acknowledgement because the target arm/publish path
is fallible. Those repairs are now source-complete with independent rereview
GO, while the adapter's seven source-contract tests are RED against the legacy
loop. Step 5 is active; the compatibility adapter does not own sourceboot sequencing yet;
the existing renderer and A8 transport remain unchanged for that first
visible-uplift experiment.
The Step 5 adapter is source-GREEN across 29 focused contracts, but target
build is blocked by a newly exposed cadence defect: scheduler simulation credit
is presently field-rate rather than the source game's 30 Hz two-field rate.
Repair the model accumulator without slowing field-rate service/poll/present,
then rereview before target evidence.
The half-rate accumulator is now healthy; two integration repairs remain:
acknowledge successful publication before VDP2/cadence evidence, and align all
layers on a nonzero generation policy because existing snapshot/frame-bank
ownership reserves zero as invalid.
Both repairs are source-complete with 30/30 focused source tests; final
rereview precedes the first serial target compile.
Final rereview is GO; the serial target compile is now the active gate. Legacy
credit telemetry names remain ABI-stable but A9 interprets their values as
whole discarded 30 Hz tick credits rather than fields.

The first serial target compile is GREEN at ELF `6685d058...6073f689`; exact
cadence capture is next. The broader verifier remains separately blocked by the
pre-existing native-math census dispatcher error and is not treated as green.
The corrected exact A9 capture removes the six-tick death spiral and reduces
presentation intervals from 36--38 to 12--14 fields: 4.463 FPS mean and 4.286
median/1%-low, 2.752x / +175% over baseline. The summarizer's stale clock
assumption is repaired with 29/29 tests and measurement rereview is GO. Commit
the checkpoint, then run manual Ymir confirmation.
Checkpoint `36f4fe58` is landed; manual desktop Ymir confirmation is active.
The first cutover build reached link and exposed a 10,032-byte HWRAM overflow;
the active narrow repair relocates 27,744 bytes of master-only terrain merge
scratch to LWRAM. Independent review and the one target rebuild now pass; the
flat desktop result is retained as the A5.8 baseline.

## Next — A9 true frame lifetime overlap

This work is now the active A9A slice above. A6's broader recovery/generalized
quarantine remains a separate pending hardening concern; A9A may use only the
already-proven fail-closed quarantine/no-replay behavior needed by its one
active generation.

## Then — full-game hardening

Generalize generated scene banks, animated actor/enemy banks, and coarse
BSP/frustum/portal-window admission beyond BOB. Add full occlusion/PVS only
when level evidence proves the coarse path is insufficient.
Task 10/A10 is this hardening/publication and scene-neutral coverage gate. It
is not the next expected FPS lever for the exact BOB capture.

## Parallel prototype — Saturn PCM audio

The standalone PCM68K soundtest is independently reviewed and owner-accepted:
heartbeat advances, commands are consumed, A/B/C are audible, X stops, and
drops remain zero. The next audio boundary is an explicitly approved
sourceboot integration slice with measured transport cost. Positional audio,
sample extraction, music, and sequencing remain later work.
