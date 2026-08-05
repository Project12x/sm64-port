# Task 7 implementation report — compact Mario animation bank

## Result

Source-complete at implementation commit `902ada8a`, pending independent
review.  The new deterministic big-endian
`S64B` payload contains every `MARIO_ANIM_*` ID (209 records from 193 source
files) as source index/value channels.  Shared streams are deduplicated and
length-prefixed, so malformed records cannot bleed into a neighboring stream.
The payload is 596,896 bytes; expanding the same 8,140 source frames into the
current 424 posed vertices plus light inputs would require 24,159,520 bytes.

The bank also contains the 20-joint hierarchy, joint-local compatibility
vertices, per-vertex joint and selected-branch ownership, source node/branch
ordinals, materials, 644 primitives, 31 meshlets, checkout-independent source
hashes, and a 3,928-byte maximum scratch claim.  Its generated report exposes
an `ANIMATION_DEPENDENCIES` S64P descriptor (`mario-animation-bank-v1`).

Payload identity from the final focused run:

- payload SHA-256: `a1969781426d4fe9e44a56774f17125fe855050667951096c8b55c35a1f376de`
- canonical source-set SHA-256: `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`

## TDD and evidence

RED was observed first as missing `actor_source` and `compile_actor_bank`
modules.  A later stream-prefix mutation was observed passing before
length-prefix validation was implemented, and the C skeleton-count mutation
was observed failing the gate before the target validator was hardened.

Final focused GREEN:

- `python tools/saturn/test_actor_source.py`: 8/8 pass.  This proves the exact
  193-file/209-ID bijection, shared-array header references, signed decoding,
  corrupt-span and unsupported-GeoLayout rejection, a complete 20-joint
  skeleton, the historical converter byte count/hash, and byte-identical
  regeneration of tracked `saturn_mario_actor_mesh.h`.
- `python tools/saturn/test_actor_bank.py`: 7/7 pass.  This proves deterministic
  compilation, hashes, compactness, all-ID lookup, shared-stream reuse,
  missing/duplicate/corrupt/reordered mutations, and exact differential output
  for all 30 legacy idle plus 77 walking frames: zero posed-vertex mismatches
  and zero light-input mismatches.
- `python tools/saturn/test_tools.py MarioActorPoseTests`: 17/17 pass,
  including the pre-existing full legacy-header regeneration/differential
  contract after the parser extraction.
- DLL-preflight `mingw32-make -f Makefile.saturn.mk -j1
  verify-actor-pose-bank`: pass.  The C decoder validated 209 animations,
  stream lengths/spans, nonzero source identity, 20-joint hierarchy, 424
  owners, 644 primitives, 31 meshlets, source clamping, shared streams, and
  scratch.  Overflowing stream length, bad owner, incomplete skeleton, and
  zero-hash mutations fail closed.
- The prescribed combined make invocation built and passed the actor-pose
  binary, then hit the inherited MSYS `/usr/bin/sh: unexpected EOF while
  looking for matching '"'` while launching `verify-actor-meshlets`.  The
  already-built normal meshlet fixture passed directly, and the invalid-span
  mutation was rebuilt through the DLL-preflight wrapper and caught by
  `expect_failure.py`.  This is recorded as an exact-command launcher defect,
  not reported as a green combined Make gate.

## Compatibility and scope boundary

`tools/mario_anims_converter.py` now consumes the shared strict parser but its
2,388,236-byte Windows output remains SHA-256
`08b1fc5b7cb03bceb0b64660656facdd6e353fadc6ed2758ff446d0d066c84b9`.
The tracked 1.32-MiB legacy Mario mesh header is retained and regenerates
byte-for-byte.

The payload truthfully marks `switch_variant_geometry_complete=false`.
Its mesh/material tables cover the existing normal-cap, front-eye, open-hand
compatibility selection only.  Full source switch variants (cap effects,
eye/hand/cap/wing choices and render-range bodies), production animation-frame
selection, actor runtime cutover, and enemy reuse remain Task 10 work.  No
target build, Ymir, manual, final S64P-link, FPS, or broad native-math claim is
made.

## Provenance

Reuse mode is direct deterministic data conversion from the repository's
inherited SM64 sources: `include/mario_animation_ids.h`, all
`assets/anims/*.inc.c`, and `actors/mario/{geo,model}.inc.c`.  No new external
library or upstream code was copied.  Existing generator/matrix/meshlet logic
was reused in place; the new bank decoder/compiler is project-authored.

## Independent-review repair

The first independent review returned SPEC/QUALITY FAIL with two Important
findings.  Repair commit `68f9dd10` adds fail-closed C validation for the complete packed
GEO1 material, meshlet, primitive, reference, part-ownership, and scratch
contract, with a distinct mutation for every table span/count/index family.
It also adds an expected-source-identity validation entry point; zero identity
and a nonzero digest differing from the caller's pinned eight words both fail.

Host provenance validation at repair `68f9dd10` proved unique canonical paths
and a self-consistent 193-file partition implied by IDs 0..208 (including all
16 dual-record files), lowercase hex hashes, per-animation path/hash
membership, a freshly recomputed `_source_digest(sources)`, the report
`source_sha256`, and payload bytes 26..57.  It did not yet bind that
self-consistent partition to the repository's actual filename set.  The JSON
schema is explicitly labeled advisory; executable validation remains
authoritative.  The compatibility geometry caveat and Task 10 switch/runtime
ownership are unchanged.

Fresh repair verification: actor source 8/8, actor bank 7/7, inherited Mario
pose suite 17/17, DLL-preflight `verify-actor-pose-bank` PASS, direct legacy
meshlet fixture PASS, and freshly wrapper-built invalid-span mutation caught.
No target, Ymir, manual, final-package, or FPS gate was run or claimed.

## Second independent-rereview repair

The rereview of `902ada8a..68f9dd10` retained two Important findings.  The C
validator now enforces the internal relationships emitted by the GEO1
compiler, not merely their ranges: strictly increasing joint node ordinals;
branch sentinel-or-node identity; source-ordered tier-0 ownership; exact
tier-0/tier-1 equality; the ordered modulo-eight tier-2 subset; meshlet to
primitive material ownership; first-seen tier vertex unions; and valid
triangle-or-four-distinct-vertex primitive shape.  In-range mutations cover
meshlet material/source, all tier primitive/vertex lists, primitive
material/vertex fields, joint node/branch metadata, and part joint ownership.

The compiler manifest now records the actual `assets/anims/*.inc.c` inventory
as observed at repository commit `68f9dd10`: 193 sorted repository-relative
paths canonicalized with the documented `S64B-ANIMATION-PATHS\0\1` framing and
pinned by SHA-256
`2d7c66e966281974652ab0581f8522c6e9b115c5ddca7fd43fe8ae74201c67e1`.
Both compilation and report validation compare against that independent pin.
A valid-looking `anim_01` / `anim_02_03` repartition, with every source hash,
document digest, header digest, and payload hash resealed, now fails closed.

Fresh focused GREEN: actor-bank 8/8; DLL-preflight native-path C compile with
`-std=c11 -Wall -Wextra -Werror`; actor-pose fixture PASS.  The direct MSYS Make
spelling remains an infrastructure caveat in this worktree because its
POSIX-root path was passed unconverted to Windows Python; this narrower native
path invocation does not claim that Make gate.  The Task 10 switch-variant and
production-runtime cutover caveat is unchanged.  No target, Ymir, manual,
final-package, or FPS gate was run or claimed.

The next scoped rereview found that per-meshlet source ownership did not yet
prove the tier-0 runs formed one global partition.  A coordinated mutation was
then observed RED: it copied meshlet 0's same-sized, same-material tier data
over meshlet 4, duplicating primitives 0..31 and leaving 101..132 unowned while
preserving every per-meshlet relationship.  The validator now carries a global
source primitive cursor, requires each meshlet to begin exactly at that cursor,
and requires final coverage to equal the GEO1 primitive count.  It also requires
triangle vertices A/B/C to be distinct (with D==C only as the triangle marker)
and all four quad vertices to be distinct.  Fresh DLL-preflight native-path
C11/Werror compile and the actor-pose fixture pass with the coordinated
duplicate/gap mutation rejected.  This fix changes no Task 10, target, Ymir,
manual, package, or FPS claim.

Final scoped rereview of `9ae757d5..b573b6bb`: SPEC PASS / QUALITY PASS,
C0/I0.  The reviewer confirmed the global tier-0 cursor and final coverage,
the coordinated duplicate/gap regression, and triangle/quad distinctness;
the repository-pinned 193-path finding remained addressed.  No new Critical
or Important issue was found.
