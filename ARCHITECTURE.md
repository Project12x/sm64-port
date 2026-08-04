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
