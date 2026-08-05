# Architecture

See `docs/saturn/` for the Saturn architecture and contracts.

Mario/actor ordering is a generated-bank boundary: tight material/opacity
meshlets retain source ordinals and compact tier references. Admission projects
live yawed pose vertices (including walking banks), then passes the generated
compact position union to transforms. The master keeps
the final opaque order, stable translucent depth bins, Gouraud/texture state,
terrain-relative insertion, VDP1 ownership, and presentation.

The active A5 queue uses one P2-visible descriptor-keyed release record per
terrain phase. WORLD_ADMIT publishes transformed-position completion after its
payload writes; WORLD_LOWER publishes the resulting record count, sequence,
claimant state, and writer lane before graph runtime can expose DONE. A merge
therefore reads its exact descriptor metadata and output lane rather than a
fixed CPU split or caller-provided count. Terrain and Mario now use this route
in the accepted atomic-cutover renderer.

WORLD_LOWER also independently revalidates its graph edge before consuming
that transform payload: it accepts exactly one completed WORLD_ADMIT
predecessor, rejects an unready or wrong-type descriptor, and then checks the
predecessor's P2 metadata against output-bank ownership. Scheduler eligibility
alone is not treated as permission to consume a payload.

Mario follows the same active producer/consumer law. A master snapshot first
copies the source-owned fully posed vertex bank, per-vertex lighting, animation
frame/bank metadata, actor transform, and compact meshlet vertex references.
ACTOR_ADMIT transforms that immutable snapshot into a descriptor-owned dense
`{vertex id, projected result}` payload. ACTOR_LOWER accepts exactly one DONE
ACTOR_ADMIT predecessor, uses the copied vertex-id-to-slot map for bounded
lookup, and publishes descriptor-owned primitive classification records. The
master consumes every DONE lower descriptor in descriptor/local order,
validates complete primitive coverage and all payload identities before any
renderer mutation, then copies results into the existing master-owned banks.
Thus the scheduler cutover changes SH-2 work ownership without changing the
Castle-proven animation or final VDP1 emission path.

Queue output offsets are local to four existing physical payload kinds:
WORLD_ADMIT transformed positions, WORLD_LOWER records/commands, ACTOR_ADMIT
projected vertices, and ACTOR_LOWER primitive references. Queue publication
derives the kind from the immutable type/callback pair, rejects an unknown or
mismatched pair, and requires spans to be disjoint only among descriptors that
write the same kind. This matches the bounded physical arrays without wasting
memory on a synthetic global arena and preserves the pointer-free 16-byte
descriptor ABI. The single atomic CPU-DUAL cutover is live, target-link green,
and live-observed. One coherent retired frame assigns WORLD phases 0--1 to the
master and ACTOR phases 2--3 to the slave (`QM=[1,1,0,0]`,
`QS=[0,0,1,1]`) with zero wait, failures, or quarantine. Thus A5.9 closes the
idle-slave and queue-wait hypotheses for the observed frame; it does not prove
that the coarse jobs have equal cycle cost or that final master-only merge and
VDP1 submission are cheap.
The host-side observation boundary resolves the three target records from the
exact supplied ELF (including local/leading-underscore symbols), validates one
immutable executable byte window in target memory before accepting telemetry,
then reads the shared records through P2 once per emulated VBlank. A queue
sample is evidence only when its queue generation is retired and its notify,
retire, and HUD publication sequences agree; each retired sequence can attach
to at most one VDP2 presentation edge. This diagnosis changes no target
scheduler behavior. The exact matching live CUE capture now passes with three
presentation edges and one coherent retired queue record; future scheduler
changes must preserve this identity/coherence boundary.

Callback contexts use a separate pointer-free P2 release record. The record
binds one immutable queue descriptor generation/index/phase to a nonzero
sequence, a bounded byte count, and the producing CPU lane; it never carries a
source-state or function pointer. A callback must prove the exact current
claim before opening its statically allocated terrain or Mario snapshot, and a
peer claimant receives only the cache-through alias. Likewise, the master-only
terrain order stream retains the exact descriptor-local command image beside
each result during sorting, so final VDP1 lowering never guesses a command
bank from a logical work range. Both contracts are live after cutover.

The payload behind that release must also be self-contained. Mario therefore
copies the frame-varying compact vertex-reference list into its snapshot and
resolves generated immutable banks by local symbols. Terrain copies the exact
transform job and bounded work-order stream; queue callbacks reconstruct a
caller-local classify view and do not retain the legacy stack classify/spans
pointers. One preparation boundary snapshots and publishes all four phases
before the active scheduler permits either SH-2 to claim work. The master
retains final deterministic assembly and VDP1 lowering after retirement.

VDP1 command and Gouraud staging now cross an explicit A7 source-bank lifetime
boundary. Each of two banks advances only through
`FREE -> BUILDING -> READY -> TRANSFERRING -> PUBLISHED`; failed construction
enters `QUARANTINED`. A bank cannot publish until its worker ticket and both
transfer obligations are satisfied, with a zero-length Gouraud transfer
represented as an explicit satisfied `NOOP`. Publication installs the new
complete bank before the prior published fallback may retire, and retirement
refuses the current fallback. Renderer failure quarantines only the incomplete
building bank and retains the last complete publication.
Publication also requires the candidate generation to follow the current bank
under the same signed-delta wrap rule used for build admission. A late completed
bank is quarantined rather than allowed to regress `published`. Manager setup
normalizes Saturn aliases and rejects physical command/Gouraud overlap,
misalignment, or duplicate bank objects.

Build/snapshot, published/submitted, and displayed generations are distinct.
The A7 adapter records today's internally blocking uploads as synchronously
retired after the renderer returns. It does not claim asynchronous transfer or
frame overlap; A8 must replace that adapter with real CPU-DMAC/SCU-DMA
submission and retirement polling.
Both current emitters nevertheless expose a truthful synchronous result: if a
Gouraud queue submission is still invalid after one drain/retry, they return
failure before command upload and sourceboot quarantines that build.
