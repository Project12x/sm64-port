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

The output-offset namespace gate is source-complete pending independent review.
Queue publication now derives one of four physical payload kinds from the
immutable type/callback pair and checks overlap only within that kind. Separate
terrain-admit, terrain-lower, actor-admit, and actor-lower banks may reuse local
offsets; same-kind overlap and malformed type/callback pairs fail closed. The
descriptor remains pointer-free and 16 bytes. Terrain command lookup from the
ordered descriptor stream, target link/cache evidence, an explicit
P2/cache-through callback-context publication contract, direct callback
corruption/cross-lane tests, and one atomic CPU-DUAL owner replacement remain
open.

The terrain route's claimant lane now reaches classification and every
queue-reachable projected read; a slave descriptor may begin at input offset
zero without accidentally taking the legacy master lane.

The accepted desktop-Ymir A3+A4 candidate remains the manual rollback baseline
at roughly 3–4 FPS (up from 1–2 FPS). It is qualitative evidence only; no new
target CUE or FPS claim is authorized until the live A5 cutover is reviewed,
built, and manually tested.
