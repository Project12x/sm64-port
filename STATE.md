# State

See `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`.

Current active source slice: Task 5/A5.8 has made the source-complete atomic
cutover from the fixed terrain/Mario workers to one dependency-aware,
descriptor-owned SH-2 queue. The accepted frame now publishes four coarse
terrain/Mario admit/lower jobs, publishes both self-contained callback
contexts before notifying the one graph runtime, drains useful master work,
requires all descriptors terminal plus positive slave callback retirement,
then assembles descriptor-owned results before master-only VDP1 lowering. The
cutover is host-green. Its first independent review, audit `8e64b482`, was
NO-GO because the single terrain admit retained a nonexistent peer wait and a
slave-admit/master-lower handoff could read stale owner bytes. Both findings
are repaired with an executable two-generation handoff fixture. Fresh scoped
re-review is GO at audit `1819f2b4`; the next gate is one serialized
post-cutover target build. That build compiled but exposed a 10,032-byte
HWRAM link overflow once callbacks became reachable. The narrow repair moves
only the two master-owned 13,872-byte final terrain merge streams into
`.lwram_bss`; its focused host contract is green. Review is GO at
`b997fea1`, and the sole post-review serialized rebuild
now links and packages with 16,148 bytes HWRAM and 30,800 bytes LWRAM margin,
zero unresolved symbols, live graph callbacks/drains, and one non-null
CPU-DUAL application registration. A fresh post-cutover CUE exists; desktop
Ymir, observed cache behavior, and FPS remain open.

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
rereview is GO at `e98210ba`; one serialized target build and manual HUD
observation remain open. A5.8 must not be
called a performance win from source/link evidence alone.

The isolated audio lane now has a standalone soundtest CUE candidate. Its
2,918-byte source-built 68K image drives four SCSP PCM8 slots, and its generated
CC0 proof bank contains three sounds in 4,408 bytes. Host register, pitch,
endianness, loop/stop, bank, boot-order, bounds, timeout, and ring tests pass.
It is not linked into sourceboot. Independent rereview is GO for the source
and standalone build. On 2026-08-04 the owner manually confirmed audible
A/B/C playback and that X stops playback in desktop Ymir. The supplied screen
also showed READY, an advancing heartbeat, consumed commands, a started voice,
and zero drops. Automated telemetry and transport-cost gates remain open.
