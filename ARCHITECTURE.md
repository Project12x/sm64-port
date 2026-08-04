# Architecture

See `docs/saturn/` for the Saturn architecture and contracts.

Mario/actor ordering is a generated-bank boundary: tight material/opacity
meshlets retain source ordinals and compact tier references. Admission projects
live yawed pose vertices (including walking banks), then passes the generated
compact position union to transforms. The master keeps
the final opaque order, stable translucent depth bins, Gouraud/texture state,
terrain-relative insertion, VDP1 ownership, and presentation.

The dormant A5 queue uses one P2-visible descriptor-keyed release record per
terrain phase. WORLD_ADMIT publishes transformed-position completion after its
payload writes; WORLD_LOWER publishes the resulting record count, sequence,
claimant state, and writer lane before graph runtime can expose DONE. A merge
therefore reads its exact descriptor metadata and output lane rather than a
fixed CPU split or caller-provided count. This remains unactivated until the
matching Mario route is complete and reviewed.

WORLD_LOWER also independently revalidates its graph edge before consuming
that transform payload: it accepts exactly one completed WORLD_ADMIT
predecessor, rejects an unready or wrong-type descriptor, and then checks the
predecessor's P2 metadata against output-bank ownership. Scheduler eligibility
alone is not treated as permission to consume a payload.

Mario follows the same dormant producer/consumer law. A master snapshot first
copies the source-owned fully posed vertex bank, per-vertex lighting, animation
frame/bank metadata, actor transform, and compact meshlet vertex references.
ACTOR_ADMIT transforms that immutable snapshot into a descriptor-owned dense
`{vertex id, projected result}` payload. ACTOR_LOWER accepts exactly one DONE
ACTOR_ADMIT predecessor, uses the copied vertex-id-to-slot map for bounded
lookup, and publishes descriptor-owned primitive classification records. The
master consumes every DONE lower descriptor in descriptor/local order,
validates complete primitive coverage and all payload identities before any
renderer mutation, then copies results into the existing master-owned banks.
Thus the eventual scheduler cutover changes SH-2 work ownership without
changing the Castle-proven animation or final VDP1 emission path.

Queue output offsets are local to four existing physical payload kinds:
WORLD_ADMIT transformed positions, WORLD_LOWER records/commands, ACTOR_ADMIT
projected vertices, and ACTOR_LOWER primitive references. Queue publication
derives the kind from the immutable type/callback pair, rejects an unknown or
mismatched pair, and requires spans to be disjoint only among descriptors that
write the same kind. This matches the bounded physical arrays without wasting
memory on a synthetic global arena and preserves the pointer-free 16-byte
descriptor ABI. This source contract remains dormant until the single atomic
CPU-DUAL cutover and target/cache validation.

Callback contexts use a separate pointer-free P2 release record. The record
binds one immutable queue descriptor generation/index/phase to a nonzero
sequence, a bounded byte count, and the producing CPU lane; it never carries a
source-state or function pointer. A callback must prove the exact current
claim before opening its statically allocated terrain or Mario snapshot, and a
peer claimant receives only the cache-through alias. Likewise, the master-only
terrain order stream retains the exact descriptor-local command image beside
each result during sorting, so final VDP1 lowering never guesses a command
bank from a logical work range. Both contracts remain dormant until cutover.
