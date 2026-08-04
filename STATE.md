# State

See `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`.

Current active source slice: Task 5/A5.8 is migrating the accepted BOB
renderer from its fixed terrain/Mario worker to one dependency-aware,
descriptor-owned SH-2 queue. A5.5 ownership bridging and A5.8.1 graph-aware
runtime drains are source-review GO; the current live frame remains legacy
until the dormant terrain and Mario routes can replace the sole CPU-DUAL
callback atomically. Mario parity is source-review GO at `1d1137f1` with audit
`3ad0d23e`; activation and target evidence remain explicitly unapproved.

Terrain's dormant WORLD_ADMIT callback now publishes transformed-position
completion by exact descriptor identity, and WORLD_LOWER records its exact
result count/sequence/claimant lane before its terminal reader may merge it.
WORLD_LOWER also fails closed unless its one immutable graph predecessor is
the exact completed WORLD_ADMIT descriptor with matching publication metadata.
The live legacy adapter is still authoritative. Terrain merge-span assembly
and Mario's equivalent transform/classify/terminal-merge route are now
source-complete and host-green; Mario's route has independent source-review
GO. Mario's route
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
payload size, and cache-through peer selection. This increment is pending
fresh re-review after `891f64b2` rejected the first version's unreachable
publisher and nested cached pointers. The repair makes Mario's dynamic refs
inline, terrain's queue snapshot self-contained, and publication an explicit
preclaim preparation step. Target link/cache evidence and one atomic CPU-DUAL
owner replacement remain open.

The first repair re-review found no remaining context/coherency defect but
kept source acceptance NO-GO because part of the mutation matrix covered only
lower phases. All mutations now run against all four phase-specific APIs and
final independent source re-review is GO. The first exact serialized target
build exposed a missing SH-only Yaul cache-header import in the queue source;
the narrow repair and independent review are GO. The one post-review serial
rebuild now compiles, links, and packages with 6,160 bytes HWRAM and 60,368
bytes LWRAM margin and no unresolved symbols. Live sole-owner cutover, target
cache behavior, CUE/Ymir runtime validation, and FPS evidence remain open.

The terrain route's claimant lane now reaches classification and every
queue-reachable projected read; a slave descriptor may begin at input offset
zero without accidentally taking the legacy master lane.

The accepted desktop-Ymir A3+A4 candidate remains the manual rollback baseline
at roughly 3–4 FPS (up from 1–2 FPS). It is qualitative evidence only; no new
target CUE or FPS claim is authorized until the live A5 cutover is reviewed,
built, and manually tested.

The isolated audio lane now has a standalone soundtest CUE candidate. Its
2,918-byte source-built 68K image drives four SCSP PCM8 slots, and its generated
CC0 proof bank contains three sounds in 4,408 bytes. Host register, pitch,
endianness, loop/stop, bank, boot-order, bounds, timeout, and ring tests pass.
It is not linked into sourceboot. Independent rereview is GO for the source
and standalone build. On 2026-08-04 the owner manually confirmed audible
A/B/C playback and that X stops playback in desktop Ymir. The supplied screen
also showed READY, an advancing heartbeat, consumed commands, a started voice,
and zero drops. Automated telemetry and transport-cost gates remain open.
