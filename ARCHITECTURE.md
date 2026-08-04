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
