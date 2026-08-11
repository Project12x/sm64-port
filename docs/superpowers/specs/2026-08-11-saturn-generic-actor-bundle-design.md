# Saturn Generic Actor Bundle Design

**Status:** Approved architecture; implementation plan and source changes remain
open.

**Date:** 2026-08-11

**Scope:** Close the missing data boundary between the reviewed Task 14 actor
identity registry and the reviewed Task 16 bank-generalized meshlet preparer.
The result must unblock the production actor queue for the integrated BOB demo
without creating a demo-only path, and must remain the actor-data path for the
eventual complete SM64 port.

## Decision summary

The production format is **S64F version 3**, one immutable actor-family bundle
per scene package. It contains a bounded family directory, a bounded drawable-
variant directory, and the validated S64B geometry/pose banks for those
variants. The scene bundle is loaded from CD into the unused, fixed region of
the Saturn 32-Mbit DRAM cartridge. It is not linked as a second global copy and
is not expanded into an HWRAM object graph.

Runtime descriptors carry scalar identities only. The master SH-2 validates
the package, the S64F v3 container, every embedded S64B bank, and all hashes and
spans before publishing a nonzero generation. Either SH-2 may then resolve a
variant to a small transient bank view and use one of two fixed LWRAM workspace
lanes. No serialized pointer, heap allocation, per-family HWRAM cache, or
per-instance full-bank validation is permitted.

Scene changes are bounded loading transitions, not seamless streaming. Once
all render, actor-bank, and audio leases on the old generation retire, the
master reclaims the fixed cart region and loads the next scene bundle in
bounded CD sectors. Gameplay never blocks on a CD read. The new generation is
published only after complete validation; a failed load stays in the loading
state with a named failure and never exposes partial or stale bytes.

## Why this is the selected architecture

The landed registry knows which source object maps to a family and model, but
S64F v2 contains only capability and string/blob metadata. The only S64B file
currently materialized is Mario-specific. Task 16 therefore has no truthful
bank to resolve for a generic BOB actor.

The selected design follows the Saturn and SH-2 shapes already present in the
repository:

- the 4 MiB cartridge is the large immutable scene-data store;
- HWRAM remains for latency-sensitive runtime state and is not consumed by a
  second actor-data graph;
- LWRAM owns one fixed, generated-capacity two-lane workspace;
- cross-SH-2 publication uses generation-last scalar records and P2-visible
  ownership rules;
- offsets, fixed-width big-endian fields, fixed ceilings, and build-time
  inventories replace pointers and dynamic growth;
- the existing S64P dependency and two-generation lease machinery remains the
  lifecycle owner.

The following alternatives are rejected:

1. **Use Mario's S64B for every actor.** This is identity-incompatible and
   would turn unsupported content into visually plausible corruption.
2. **One S64P dependency per S64B.** S64P has a 32-dependency ceiling, while
   BOB already has more supported families before other scene dependencies.
3. **One global all-game actor blob.** It makes the demo pass by consuming the
   eventual port's cart budget and removes the required scene transition.
4. **Copy every active bank into HWRAM.** It duplicates immutable data and
   violates the measured memory campaign.
5. **Allocate workspaces per family or instance.** It creates unbounded
   allocation pressure and complicates dual-SH-2 ownership.
6. **Disable dynamic actor closure for the demo.** It avoids the exact
   production path the full port needs and is not an acceptance option.

## Existing contracts retained

- S64P v1 remains the scene package root and dependency/lifetime authority.
- S64B v1 remains the embedded geometry/pose-bank format for this migration.
- S64F v2 remains readable by historical host tools. A feature-on production
  runtime requires v3 and rejects v2 before actor publication.
- Task 14's `(resolved model, exact behavior)` lookup remains the source-owned
  selection seam.
- Task 16 Task 1's legacy Mario entry point and feature-off byte identity stay
  frozen.
- `sm64_saturn_scene_residency_t` remains the generation and lease owner.
- The fixed 65,536-byte actor runtime arena remains queue/batch/output storage
  only, with its reviewed 2,718 output-record ceiling.

## S64F v3 byte format

All integer fields are unsigned big-endian unless stated otherwise. The file
is four-byte aligned, uses offsets from byte zero of the S64F file, contains no
serialized pointers, and has no trailing bytes. Every reserved field and every
alignment padding byte must be zero.

### Header: 96 bytes

| Offset | Size | Field | Contract |
|---:|---:|---|---|
| 0 | 4 | `magic` | `0x53363446` (`S64F`) |
| 4 | 2 | `version` | `3` |
| 6 | 2 | `header_size` | `96` |
| 8 | 2 | `family_count` | `1..64` |
| 10 | 2 | `variant_count` | `1..128` |
| 12 | 2 | `family_record_size` | `64` |
| 14 | 2 | `variant_record_size` | `88` |
| 16 | 4 | `family_records_offset` | exactly `96` |
| 20 | 4 | `variant_records_offset` | exact canonical end of family table |
| 24 | 4 | `metadata_offset` | exact canonical end of variant table |
| 28 | 4 | `metadata_size` | bounded metadata byte count |
| 32 | 4 | `bank_payloads_offset` | aligned canonical end of metadata |
| 36 | 4 | `bank_payloads_size` | complete embedded-bank region |
| 40 | 4 | `workspace_lane_stride` | four-byte-aligned maximum lane bytes |
| 44 | 4 | `maximum_scratch` | exactly `3 + 2 * workspace_lane_stride` |
| 48 | 4 | `package_generation` | nonzero immutable artifact generation |
| 52 | 4 | `flags` | zero for v3 |
| 56 | 4 | `total_size` | exact file size |
| 60 | 4 | `reserved_zero` | zero |
| 64 | 32 | `content_sha256` | SHA-256 of the complete file with these 32 bytes zeroed |

The canonical layout is header, family table, variant table, metadata, zero
alignment padding, then the bank-payload region. Derived table sizes are
`family_count * 64` and `variant_count * 88`; separate table-size fields are
deliberately absent. All addition, multiplication, alignment, and end checks
are overflow-safe before any pointer is formed.

### Family record: 64 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | `stable_family_hash` |
| 4 | 4 | `capability_mask` |
| 8 | 4 | `runtime_capability_mask` |
| 12 | 4 | `maximum_live_instances` |
| 16 | 4 | `source_actor_count` |
| 20 | 4 | `flags` |
| 24 | 4 | `name_offset` |
| 28 | 4 | `name_size` |
| 32 | 4 | `source_offset` |
| 36 | 4 | `source_size` |
| 40 | 4 | `unsupported_offset` |
| 44 | 4 | `unsupported_size` |
| 48 | 4 | `metadata_blob_offset` |
| 52 | 4 | `metadata_blob_size` |
| 56 | 2 | `variant_first` |
| 58 | 2 | `variant_count` |
| 60 | 4 | `reserved_zero` |

Family records are sorted by the existing stable family key/hash order and
must have unique nonzero `stable_family_hash` values. Runtime `family_ordinal`
is `record_index + 1`, preserving Task 14's reviewed 16-bit observer ABI.
Unsupported families may remain as metadata records with a zero variant span;
the production identity registry omits their drawable variants. Supported
families require at least one variant.

All four metadata spans are relative to `metadata_offset`, contained within
`metadata_size`, non-overlapping unless they are exact aliases of identical
bytes, and validated by host tools. Target lookup does not scan or compare
strings.

### Drawable-variant record: 88 bytes

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | `family_ordinal` |
| 2 | 2 | `model_id` |
| 4 | 4 | `flags` |
| 8 | 4 | `bank_offset` |
| 12 | 4 | `bank_size` |
| 16 | 4 | `bank_lane_bytes` |
| 20 | 4 | `bank_maximum_scratch` |
| 24 | 32 | `bank_payload_sha256` |
| 56 | 32 | `bank_source_sha256` |

Variant records are sorted by `(family_ordinal, model_id)`. Both values are
nonzero, the key is unique, and `flags` is zero for v3. `bank_offset` is
relative to `bank_payloads_offset`, four-byte aligned, and the complete S64B
span is inside `bank_payloads_size`.

The embedded S64B header must report the same family ordinal and model ID.
`bank_payload_sha256` hashes the exact embedded S64B bytes.
`bank_source_sha256` equals the S64B source-identity field consumed by Task 16
Task 1. `bank_maximum_scratch` equals the S64B value and must be exactly
`3 + 2 * bank_lane_bytes`. The bundle's `workspace_lane_stride` is the maximum
four-byte-aligned `bank_lane_bytes` across all variants.

Multiple source objects may resolve to the same variant record. That is the
normal deduplication boundary. An exact embedded bank span may be shared only
by records with identical family, model, payload hash, source hash, lane size,
and scratch size; because duplicate variant keys are forbidden, v3 does not
use cross-identity binary aliasing. A future interned-substream scheme requires
a new format version.

## Identity model

The package/bundle identity and per-variant bank identity are deliberately
separate.

- S64P binds the S64F dependency stable ID, exact payload SHA-256,
  `package_generation`, destination, and byte count.
- S64F `content_sha256` binds the complete family/variant tables, metadata,
  embedded banks, and artifact generation.
- A variant's `actor_bank_hash_words[8]` are the eight big-endian 32-bit words
  of `bank_source_sha256`, matching Task 16 Task 1's existing identity check.
- A variant's `actor_bank_id` is the first big-endian word of
  `bank_source_sha256`. Zero or a collision between distinct variant keys is a
  build failure; the full 256-bit identity remains authoritative.
- `scene_package_generation` in actor observations/descriptors is the nonzero
  immutable S64F/S64P artifact generation, not the transient render-frame
  generation and not the residency slot number.
- Render, queue, and residency generations remain separate scalar fields and
  must match their respective owners.

The actor identity registry is generated only after the final S64F v3 bundle
and embedded S64B banks exist. It copies the family ordinal, model ID,
per-variant bank ID/source hash, and package generation. It no longer derives
every actor's bank identity from the outer S64F payload digest.

## Build pipeline

For each source-selected scene/area package, the host build performs this
closed sequence:

1. Derive the source actor closure and exact `(resolved model, behavior)` rows.
2. Group drawable rows by stable family and model ID.
3. Compile one deterministic S64B v1 bank for each unique supported
   `(family ordinal, model ID)` variant. Geometry is joint-local and pose data
   remains in the bank; no target pointer is emitted.
4. Compute each bank's lane requirement using the reviewed Task 16 workspace
   formula and embed the exact bank bytes.
5. Emit canonical S64F v3 tables, metadata, hashes, global lane stride, and
   package generation.
6. Fully validate the just-written S64F and every S64B by the same shared host
   validator used by target fixtures.
7. Generate the actor identity registry from the validated v3 bundle, closure,
   source model IDs, and exact behavior symbols.
8. Emit one S64P `ACTOR_DEPENDENCIES` record for the S64F v3 payload with
   destination `CART`, alignment 4, exact size/hash/generation, and
   `maximum_scratch = 0`. Actor workspace is a separate fixed LWRAM runtime
   reservation, not cart-adjacent scratch.
9. Seal the package set, source closure, build identity, and release manifest.

The whole-game inventory enumerates every source-selected scene/area and must
prove no scene exceeds 64 family records or 128 variant records. An overflow
fails generation; the tool never truncates or grows tables dynamically.

The build also emits the maximum `maximum_scratch` required across all scene
bundles. The sourceboot link reserves one fixed LWRAM actor-workspace object of
that generated size, rounded upward to 256 bytes. The exact map span and the
generated capacity are release inputs and target gates. No guessed constant is
accepted in place of the inventory.

## Runtime placement and lookup

The S64F bytes live in the source-cart residency span returned after the
linked `SOURCE.DAT` prefix. They are copied from CD in bounded sectors by the
master SH-2. The cart region is immutable while its generation is committed.

The actor workspace lives in one fixed LWRAM object. It contains two lanes at
the bundle-wide `workspace_lane_stride`, plus the three bytes of leading
alignment headroom required by Task 16 Task 1. It is reused across banks and
across scenes only after all current actor claims and scene leases retire. It
is not part of the 65,536-byte actor queue/batch/output arena.

Task 1's reviewed standalone binder calculates both lane positions from one
selected bank's own lane size. That remains correct for a single-bank payload
but is not sufficient when master and slave select banks with different lane
sizes. Production therefore adds a bundle-stride binding entry point (sharing
the existing layout core) that accepts the validated global lane stride and
the selected bank's required lane bytes. The original binder and its ABI tests
remain unchanged. The production entry point rejects a stride smaller than the
selected bank, capacity below `3 + 2 * stride`, or any arithmetic/overlap
failure before returning workspace pointers.

At scene commit, the master builds a small fixed runtime descriptor containing
only:

- cart base offset and byte count;
- package generation and residency generation;
- S64F content hash words;
- family/variant counts;
- variant-table and bank-region offsets;
- workspace base, capacity, and lane stride;
- a committed byte written last through the existing P2-visible publication
  discipline.

The runtime descriptor stores no per-family bank views. Resolution uses the
snapshot's `(family_ordinal, model_id, actor_bank_id, source_hash,
scene_package_generation)` to binary-search the bounded variant table, checks
the scalar record and bank span, then constructs a transient
`sm64_saturn_actor_bank_view_t` over the already-validated immutable S64B
bytes. This is not a full hash or whole-bank validation pass. The view is valid
only while the matching scene lease and actor-bank token are held.

Worker-visible queue/job records carry offsets, counts, IDs, hashes, lane
index, and generations only. They never carry P1/P2/cart pointers. Each SH-2
reconstructs its local cart/P2 view from the committed descriptor.

## Dual-SH-2 lane ownership

There are exactly two claimant lanes: master `0` and slave `1`. A live actor
job claims the CPU's lane through the existing job/queue ownership boundary.
Two banks with different individual lane sizes remain safe because both lane
bases use the bundle-wide stride through the production strided binder, while
the selected bank's internal layout consumes only its exact smaller
`bank_lane_bytes` within that lane.

A lane cannot be reused until the owning descriptor reaches its terminal
state and its output publication is visible. Double claim, wrong CPU/lane,
stale generation, insufficient bundle capacity, or workspace/output overlap
quarantines that descriptor before output writes. The master publishes the
scene generation only after zeroing/resetting the workspace ownership record.

## Scene transition state machine

The master owns these states:

1. `ACTIVE`: gameplay may resolve the committed scene bundle.
2. `DRAIN_REQUESTED`: new actor/render publication stops; existing work may
   finish.
3. `DRAINING`: wait for render, actor-bank, VDP1-bank, and audio lease counts to
   reach zero. No CD read occurs here.
4. `RECLAIMED`: unload the old residency generation and reclaim its fixed cart
   span and shared workspace.
5. `LOADING`: while the gameplay loop is suspended on the transition screen,
   read bounded CD sectors into the private cart span.
6. `VALIDATING`: validate S64P, S64F v3, all directories/spans/hashes, all
   embedded S64B banks, the registry binding, and workspace capacity.
7. `COMMITTING`: copy scalar identity fields, clear lane ownership, and publish
   the nonzero generation last.
8. `ACTIVE`: resume gameplay.

The design does not promise seamless double buffering. If sufficient cart
space exists, the existing two-slot residency layout may stage a future
optimization, but acceptance requires only the bounded drain/reclaim/load
path. After `RECLAIMED`, a load failure cannot reactivate stale old pointers;
it remains on the transition/error screen with no active generation.

## Validation and failure policy

Validation is atomic at the generation boundary. Before commit it checks:

- S64P target validity, non-provisional status, dependency kind, destination,
  size, alignment, generation, stable ID, and content hash;
- S64F v3 canonical layout, counts, ordering, zero padding/reserved bits,
  content hash, exact size, and workspace arithmetic;
- every family and variant span, ordinal, model ID, unique key, capability
  contract, and metadata span;
- every embedded S64B magic/version/size/hash/source identity/family/model,
  geometry/pose span, and scratch calculation;
- registry rows against the exact bundle and source closure;
- fixed cart and LWRAM workspace capacities;
- unchanged package/residency identity before and after validation.

Named fail-closed outcomes include at least: missing family, missing variant,
unsupported family, stale package generation, stale residency generation,
bank ID mismatch, bank source-hash mismatch, malformed S64F, malformed S64B,
payload-hash mismatch, workspace capacity, invalid lane, lane already claimed,
output capacity, lease mismatch, and transition I/O failure.

No failure substitutes Mario, the first family, a previous-generation bank, or
a partially validated view. Descriptor failures leave their output span
unchanged and publish a named quarantine. Bundle/load failures publish no
generation.

## Compatibility and migration

1. Keep S64F v2 parsing and exact historical tests for tooling only.
2. Add strict v3 writer/parser/validator tests before changing production.
3. Compile the BOB v3 bundle and regenerate its identity registry.
4. Add the fixed LWRAM workspace and cart residency binding.
5. Resume Task 16 Task 2 using the v3 resolver; then complete Tasks 3-5.
6. Prove feature-off objects remain byte-identical.
7. Remove the now-duplicated statically linked generic actor banks/data from
   `SOURCE.DAT`; do not count temporary duplicate layouts as final capacity.
8. Rebuild two clean hermetic candidates, rerun native-math v4, capacity,
   package, and guarded staging.
9. Resume Task 10 smoke, visual, desktop, and owner manual play.
10. Run the whole-game scene inventory and at least one non-BOB scene
    transition before calling the path total-game deployable.

## Required verification

### Host and mutation gates

- v2 historical bytes and semantics unchanged; feature-on target rejects v2;
- v3 exact header/record sizes, endianness, canonical layout, and zero padding;
- count 64/128 accepted, 65/129 rejected, arithmetic overflow rejected;
- family/variant order, duplicate keys, zero IDs, ID collision, and span
  mutations rejected;
- every embedded S64B hash/source/family/model/pose/geometry/scratch mutation
  rejected before commit;
- registry hit resolves the exact bank; miss/unsupported/stale identity has no
  fallback and no output writes;
- two heterogeneous banks run concurrently in different SH-2 lanes without
  workspace overlap;
- stale generation, lane double-claim, short workspace, and output overlap
  produce the exact quarantine;
- transition refuses unload with any live lease, performs no gameplay CD read,
  and never publishes partial data;
- feature-off wrapper/object hashes remain byte-identical.

### Build and target gates

- real BOB bundle contains the source-selected supported variants and the
  generated registry is derived from it;
- whole-game source inventory proves every scene fits 64/128 and the generated
  LWRAM workspace ceiling;
- target map proves one cart-resident scene bundle, one fixed LWRAM workspace,
  the unchanged actor arena, and no legacy duplicate generic banks;
- cart high-water remains within 4 MiB and HWRAM/LWRAM margins remain positive;
- exact feature-on target shows both SH-2 lanes resolving different actor
  banks, terminal queue results, zero unexpected quarantine, and stable leases;
- CD transition loads and commits a second scene without a gameplay-loop read;
- Task 9 reproducibility, v4 audit, package inventory, staging, and overwrite
  refusal are repeated after the target-byte change;
- Task 10 exact smoke, visual evidence, desktop launch, and owner manual play
  are repeated against the resealed manifest.

## Implementation ownership and sequence

This design creates a prerequisite lane ahead of Task 16 Task 2:

1. S64F v3 format, shared validator, deterministic writer, and inventory.
2. Generic per-variant S64B compilation and v3 bundle emission.
3. Registry regeneration from per-variant source identities.
4. Cart-residency loader, fixed LWRAM workspace, generation-last bundle
   descriptor, and bounded transition state machine.
5. Independent review of the prerequisite lane.
6. Resume Task 16 Task 2 handoff wiring, then Tasks 3-5.
7. Rebuild/reseal Task 9 and resume Task 10 acceptance.

No production implementation begins from this document alone. The next step is
an implementation plan that assigns exact files, TDD RED/GREEN gates, commit
boundaries, independent reviews, and target evidence without weakening any
open acceptance gate.

## Reference and reuse record

This is a same-repository architecture extension. The design inspected and
reuses the contracts in:

- `src/port/saturn/runtime/saturn_scene_package.h/.c`;
- `src/port/saturn/runtime/saturn_scene_residency.h/.c`;
- `src/port/saturn/sourceboot/source_cart.h/.c`;
- `src/port/saturn/gfx/saturn_actor_bank.h/.c`;
- `src/port/saturn/gfx/saturn_actor_instance*.h/.c`;
- `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`;
- `tools/saturn/gen_actor_identity_registry.py`;
- the reviewed Task 11 family-bank generator/validator and Task 14 registry
  tests.

Reuse mode is direct use plus close-port of existing in-tree validation,
residency, generation, and lease patterns. No external code is copied and no
new license obligation is introduced.
