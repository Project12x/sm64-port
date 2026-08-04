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

The queue currently validates output spans in one global namespace, while the
physical terrain and actor payload banks are type-local. The dormant routes do
not paper over that mismatch. Atomic activation must explicitly choose
per-payload-kind overlap validation or a bounded global offset layout and prove
its memory budget before publishing a combined terrain/actor graph.
