# Saturn Actor Bank v2 Texture Design

**Status:** Owner-approved architecture and written specification. The spec was
self-reviewed and committed at `62f16de8`; owner written-spec approval was
received 2026-08-12. The implementation plan is
`docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`.

**Date:** 2026-08-11

**Scope:** Add the first texture/material-capable S64B actor-bank revision needed
to resume the real BOB S64F-v3 bundle lane. Preserve S64B v1 bytes and behavior,
keep the runtime shaped around Saturn and two SH-2s, and finish with a real BOB
emulator demo rather than a synthetic format-only artifact.

## Decision summary

S64B v2 is an additive, self-contained, pointer-free actor bank. It retains the
v1 animation, pose, vertex, meshlet, and primitive representation and adds a
target-level material directory, one render binding per primitive, a directory
of offline-baked VDP1 texture tiles, and cold CLUT16/RGB1555 payloads. The bank
stores no Fast3D command stream and no absolute VDP1 address.

The initial compiler supports only the exact measured BOB actor material
signatures that have a reviewed Saturn lowering. Unknown or later whole-game
states fail by name during compilation. This makes v2 **full-game-shaped**, not
semantically full-game-complete: future material semantics may require an
additive S64B version, but they do not require replacing the container,
identity, residency, or dual-SH-2 ownership model.

The first runtime policy is all-resident per scene. The master SH-2 validates
the complete S64P/S64F/S64B chain, proves aggregate cart/VDP1/command/Gouraud/
workspace budgets, uploads cold texture data after VDP1 becomes idle, and then
publishes a nonzero residency generation. Workers read immutable hot bank data
and write the existing scalar draw records only. Final VDP1 command emission
and residency lookup remain master-only.

S64B v1 and its historical Mario output remain exact. S64F v3 keeps its 96/64/
88-byte wire layout and opaque embedded-bank records, but delegates embedded
bank validation and resolution to the version-owned S64B parser so both v1 and
v2 can be carried without duplicating header offsets.

### Amendment relationship

This document is a narrow normative amendment to
`2026-08-11-saturn-generic-actor-bundle-design.md`. It supersedes only that
design's statement that S64B v1 is the sole embedded geometry/pose-bank format
for the migration and its instruction to compile every supported variant as
v1. The S64F-v3 96/64/88-byte wire contract, family/variant ordering, identity,
residency, generation, failure, and capacity rules remain normative. A mixed
bundle may embed historical v1 banks and new v2 banks; the S64B-owned parser is
the only authority for either embedded version.

## Why the prior boundary is blocked

The reviewed real-BOB replay at `44c903950632df17de24ec0b4781ca8d13d14563`
contains 47 canonical actor families, 34 nonzero drawable keys, and two exact
`MODEL_NONE` sentinels. The v1 compiler supports zero drawable keys. First-hit
reasons are 18 `GEO_SHADOW`, 14 textured rigid/material states, one
`GEO_SCALE`, and one `GEO_ASM`. Removing only shadow or scale does not create a
supported variant. S64F v3 correctly requires `variant_count` in `1..128`, so
an empty bundle is not an accepted substitute.

The first truthful expansion is texture/material support because fourteen
keys reach it directly and additional shadow/scale keys reach it after their
own future semantics. Retrofitting texture state into S64B v1 would invalidate
its reviewed byte contract. Allowing an empty S64F would preserve the exact
production blocker. A new S64B version is therefore the smallest forward path.

## Saturn and SH-2 constraints

The design treats these as hard constraints rather than optimization goals:

- VDP1 has no programmable UV coordinates, texture wrap/clamp, N64 combiner,
  perspective correction, or general per-pixel alpha.
- Distorted sprites consume a complete rectangular pattern through four
  implicit corners. A source triangle uses `(A,B,C,C)`.
- Character-pattern addresses and CLUT addresses are eight-byte aligned.
  Pattern width is a multiple of eight pixels and at most 504; height is
  `1..255`.
- The measured shared VDP1 texture ceiling is 446,432 bytes. It is not an actor
  allocation. Terrain, Mario, command tables, Gouraud tables, and CLUTs retain
  their own reservations before a generic-actor sub-budget is computed.
- The reviewed actor runtime arena remains exactly 65,536 bytes with 2,718
  eight-byte output records. V2 does not enlarge it.
- The package owns two fixed claimant scratch lanes. No per-family or
  per-instance workspace allocation is introduced.
- Immutable scene data belongs in the 32-Mbit DRAM cart. Cold texture payloads
  must not be copied into the actor runtime arena or treated as lane scratch.
- Cross-SH-2 publication carries scalar IDs, counts, hashes, and generations.
  No pointer, VDP1 address, allocator state, or command-table pointer crosses
  the queue boundary.
- No heap, recursive target parser, runtime Fast3D interpreter, runtime texture
  conversion, or gameplay-time CD read is permitted.

## Alternatives considered

### Self-contained S64B v2 — selected

Each bank carries its own target-ready material, tile directory, and cold tile
and palette bytes. It is independently hashable, relocatable, and validatable.
Duplicate source textures may produce different baked tiles because source UV
state is per primitive; bank-local exact-byte deduplication removes only true
duplicates.

### Shared S64F texture pool — rejected for this revision

A scene-wide pool can reduce repeated source data, but it makes an S64B valid
only in the presence of one S64F, adds cross-container offsets to the worker
path, complicates relocation and generation failure, and weakens the existing
independent-bank validation contract. The first revision accepts modest cart
duplication in exchange for bounded ownership.

### Runtime Fast3D material interpreter — rejected

Carrying N64 material commands to the target spends SH-2 time in the hottest
path, creates mutable command state, makes command and residency counts data
dependent, and still cannot create capabilities absent from VDP1. The in-tree
offline bake solves the mismatch before runtime and is the reviewed reference.

## Compatibility and version ownership

### S64B v1

- Magic remains `0x53363442` (`S64B`).
- Version remains `1`; header remains 104 bytes.
- Every historical v1 packer, parser, fixture, and Mario hash remains exact.
- No v2 field, recipe, texture byte, or relaxed validation is backported.

### S64B v2

- Magic remains `S64B`; version is `2`; header is 192 bytes.
- Bytes `0..103` retain the v1 field positions and meanings. `version` is 2
  and `header_size` is 192. The original span fields still describe animation
  records, indices, values, vertices, and the complete `GEO1` geometry span.
- V2 fields begin at byte 104. The version-2 parser validates the common prefix
  through shared helpers and then validates every v2-only span and reference.
- The generic public validator dispatches once by version. S64F and runtime
  callers never reproduce version-specific offsets.

### S64F v3

- The outer 96-byte header, 64-byte family record, and 88-byte variant record
  remain unchanged.
- Historical v3 fixtures containing v1 banks remain byte-exact and valid.
- Host and target S64F validators call the S64B-owned dispatching validator for
  every embedded bank.
- `sm64_saturn_actor_bundle_resolve` stops manually reconstructing a v1 view.
  It performs bounded variant lookup, verifies the expected hashes/IDs, then
  delegates the exact bank span to S64B validation.
- A v2-acceptance implementation that duplicates the v2 header layout inside
  `saturn_actor_bundle.c` is nonconforming.

## S64B v2 byte format

All integers are unsigned big-endian unless explicitly signed. All offsets are
from byte zero of the S64B. Reserved fields and alignment padding are zero.
Every add, multiply, alignment, and end computation is overflow-checked before
a pointer is formed.

### Common prefix: bytes 0..103

The v1 prefix is retained exactly. In v2:

- `version = 2`;
- `header_size = 192`;
- the existing source hash is the v2 canonical source identity;
- `records_offset`, `indices_offset/size`, `values_offset/size`,
  `vertices_offset/size`, and `meshlets_offset/size` retain their v1 meanings;
- `maximum_scratch` remains the worst-case two-lane reservation including
  three bytes of leading alignment headroom;
- `meshlets_offset + meshlets_size` ends the hot `GEO1` geometry region, not
  the complete v2 bank.

### Version-2 extension: bytes 104..191

| Offset | Size | Field | Contract |
|---:|---:|---|---|
| 104 | 2 | `render_binding_record_size` | exactly 8 |
| 106 | 2 | `target_material_record_size` | exactly 8 |
| 108 | 2 | `texture_tile_record_size` | exactly 16 |
| 110 | 2 | `v2_flags` | zero |
| 112 | 4 | `render_bindings_offset` | canonical end of `GEO1` |
| 116 | 4 | `render_bindings_size` | `primitive_count * 8` |
| 120 | 4 | `target_materials_offset` | canonical end of bindings |
| 124 | 4 | `target_materials_size` | `geometry_material_count * 8` |
| 128 | 4 | `texture_tiles_offset` | canonical end of materials |
| 132 | 4 | `texture_tiles_size` | multiple of 16 |
| 136 | 4 | `texture_payload_offset` | next eight-byte-aligned byte |
| 140 | 4 | `texture_payload_size` | complete padded character-pattern span |
| 144 | 4 | `clut_payload_offset` | next eight-byte-aligned byte |
| 148 | 4 | `clut_payload_size` | multiple of 32 |
| 152 | 4 | `texture_resident_bytes` | exactly `texture_payload_size` in v2 |
| 156 | 4 | `clut_resident_bytes` | exactly `clut_payload_size` in v2 |
| 160 | 4 | `draw_records_per_instance` | exactly `primitive_count` |
| 164 | 4 | `texture_commands_per_instance` | count of textured bindings |
| 168 | 4 | `gouraud_tables_per_instance` | count of Gouraud recipes |
| 172 | 4 | `total_size` | exact S64B byte count |
| 176 | 4 | `bake_policy_id` | nonzero reviewed policy ID |
| 180 | 12 | `reserved_zero` | zero |

Canonical layout is header, animation records, animation indices, animation
values, vertices, `GEO1`, render bindings, target materials, texture tile
records, zero alignment padding, texture payload, zero alignment padding, CLUT
payload, and end of file. Spans may not reorder, overlap, leave unexplained
gaps, or carry trailing bytes.

The logical hot region ends at `texture_payload_offset`. Texture and CLUT
payloads are cold cart data. A runtime may retain all bytes in cart and expose
one view, but it may not copy the cold spans into the fixed actor arena merely
because they share one serialized S64B.

### Render-binding record: 8 bytes

| Offset | Size | Field | Contract |
|---:|---:|---|---|
| 0 | 2 | `material_id` | equals the material ID in the corresponding `GEO1` primitive |
| 2 | 2 | `tile_id` | dense tile ordinal or `0xFFFF` for no texture |
| 4 | 2 | `flags` | zero in v2 |
| 6 | 2 | `reserved_zero` | zero |

There is exactly one record per primitive, in primitive order. An untextured
recipe requires `tile_id = 0xFFFF`; a textured recipe requires a valid tile.
V2 does not encode two-pass base-plus-detail rendering.

### Target-material record: 8 bytes

| Offset | Size | Field | Contract |
|---:|---:|---|---|
| 0 | 2 | `recipe` | stable S64B-owned recipe enum |
| 2 | 1 | `layer` | stable S64B-owned target layer enum |
| 3 | 1 | `alpha_mode` | stable S64B-owned alpha enum |
| 4 | 1 | `selector_kind` | zero (`STATIC`) in v2 |
| 5 | 1 | `flags` | zero in v2 |
| 6 | 2 | `reserved_zero` | zero |

Material ordinals match `GEO1`. The existing four-byte `GEO1` material record
continues to carry base RGB555 and a zero byte. V2 target records add lowering
semantics; they do not duplicate source Fast3D commands.

Target layers are `0 = OPAQUE`, `1 = CUTOUT`, and `2 = TRANSLUCENT`. Alpha
modes are `0 = OPAQUE`, `1 = BINARY_ZERO_TRANSPARENT`, and
`2 = HALF_TRANSPARENT`. The compiler accepts only reviewed layer/alpha/recipe
combinations; the target validator rejects every inconsistent combination.

V2 owns these target recipe values:

1. `FLAT_GOURAUD`
2. `CLUT16_REPLACE`
3. `CLUT16_GOURAUD`
4. `RGB1555_REPLACE`
5. `RGB1555_GOURAUD`
6. `CLUT16_HALF_TRANSPARENT`
7. `RGB1555_HALF_TRANSPARENT`

Defining a recipe does not authorize the compiler to emit it. The real BOB
inventory and its reviewed lowering table determine which values are enabled.
Raw Yaul `vdp1_cmdt_*` numeric values are never serialized.

### Texture-tile record: 16 bytes

| Offset | Size | Field | Contract |
|---:|---:|---|---|
| 0 | 4 | `payload_offset` | relative to `texture_payload_offset`, eight-byte aligned |
| 4 | 4 | `payload_size` | exact format-derived byte count |
| 8 | 2 | `width` | multiple of 8, range `8..504` |
| 10 | 2 | `height` | range `1..255` |
| 12 | 2 | `clut_id` | dense CLUT ordinal or `0xFFFF` for RGB1555 |
| 14 | 1 | `format` | 1 = CLUT16, 2 = RGB1555 |
| 15 | 1 | `flags` | zero in v2 |

Tile ordinals are dense. Multiple render bindings may reference one tile. Tile
payload spans are ordered by first semantic use, non-overlapping, and not
aliased. The packer deduplicates exact tile records before assigning ordinals.
Distinct non-overlapping records with identical payload bytes are valid input
to the linear target validator, though the canonical producer never emits
them; target validation does not perform a quadratic content-deduplication
search.

For CLUT16, `payload_size = width * height / 2` and `clut_id` is valid. For
RGB1555, `payload_size = width * height * 2` and `clut_id = 0xFFFF`. Every
multiplication is overflow-checked.

### CLUT payload

CLUT payload is a dense array of 32-byte, sixteen-entry RGB1555 palettes.
`clut_count = clut_payload_size / 32`. IDs are assigned by first semantic use
after exact-byte deduplication. Entry zero is exactly zero. Nontransparent
entries have bit 15 set. The canonical producer emits no duplicate palette,
but the linear target validator does not compare every palette pair.

### Character-pattern payload

All payloads are already in Saturn target byte order. CLUT16 index zero and
RGB1555 word zero are transparent. Every nonzero RGB1555 word has bit 15 set.
The runtime always disables VDP1 end-code interpretation for these fixed-size
patterns. V2 carries no compressed texture data and performs no target-side
conversion.

## Offline compiler contract

The host compiler owns every expensive or source-specific operation.

1. Rebuild the canonical BOB closure and replay every selected drawable key.
2. Walk only closure-attested GeoLayout, Gfx, Vtx, animation, and texture
   sources. Unique symbol resolution and every reached source hash remain
   mandatory.
3. Capture the complete source-material signature: texture image, UVs,
   `gsSPTexture`, tile/load state, wrap/clamp/mask/shift, combiner, geometry
   mode, layer, opacity, and reached call/tail-transfer state.
4. Match that signature against an explicit BOB lowering table. A partial,
   unknown, computed, ambiguous, or unconsumed state raises a named compiler
   error.
5. Compile animation, joints, vertices, and untextured geometry through the
   existing shared encoder.
6. For every accepted textured source triangle, preserve source order and
   prohibit pairing. Bake one `(A,B,C,C)` distorted-sprite tile by evaluating
   the full source tile state offline through the repository's BIOS-probed
   VDP1 weights.
7. The initial BOB bake policy emits deterministic 16x16 or 32x32 tiles. CLUT16
   is preferred. RGB1555 requires a named reviewed recipe and budget result.
8. Quantize, canonicalize transparency, deduplicate exact bank-local tiles and
   CLUTs, assign dense first-use ordinals, and pack v2.
9. Reparse the finished bytes through production-equivalent host validation.
10. Publish atomically with the report last and never overwrite an existing
    artifact.

Textured triangle pairing is deliberately absent in v2. A future version may
pair only when two source triangles have continuous compatible material/UV
state and one baked quad reproduces both. Ordinary untextured v1 pairing is
unchanged.

“Exact BOB support” means exact recognition of the measured source signature
and use of its reviewed Saturn recipe. It is not a pixel-exact N64 combiner
claim. VDP1 textured Gouraud is additive rather than N64 texture-times-shade;
that fidelity difference must be named in the lowering table and visual report.

## Source identity and reproducibility

V1 source identity remains unchanged. V2 source identity uses a distinct
domain prefix and binds:

- family ordinal and model ID;
- every closure-attested repository-relative source path and SHA-256;
- every source texture byte span and selected material-state signature;
- `bake_policy_id`, tile-size policy, quantizer policy, transparency policy,
  and target recipe table version;
- every compiler/generator input already covered by the hermetic source
  closure.

Paths are slash-normalized, root-relative UTF-8 with no empty, `.`/`..`, NUL,
casefold-colliding, or host-path components. Timestamps, checkout roots, and
temporary paths never enter identity. The complete S64B payload hash remains a
separate SHA-256 in S64F.

Relocated clean worktrees must produce byte-identical v2 banks, S64F bundles,
reports, and source identities.

## Scene-level resource planning

Bank-local totals are necessary but insufficient because VDP1 and cart are
shared scene resources. Before packaging or target activation, the planner:

1. Parses every unique embedded bank once.
2. Sums all v2 texture and CLUT residency because every BOB variant may become
   visible and v2 has no eviction.
3. Separately computes worst-case family draw demand. For each family, multiply
   its maximum live count by the maximum per-instance draw, texture-command,
   and Gouraud demand among its variants; then sum families.
4. Adds terrain, Mario, command-table, Gouraud, CLUT, HUD, and other profile
   reservations before comparing with hardware partitions.
5. Proves the complete S64F dependency and scene package fit the fixed cart
   destination and 32-Mbit package/image boundaries.
6. Proves the maximum two-lane scratch, fixed actor arena, output-record ceiling,
   command count, and Gouraud count without enlarging reviewed storage.

The build fails rather than selecting a smaller hidden working set, dropping a
variant, reducing live counts, changing a texture class, or spilling to heap.
Every budget and its contributing bank/family appears in the deterministic
report.

## Runtime residency and dual-SH-2 ownership

The first runtime supports all-resident scene activation only.

1. Master suspends gameplay on the existing scene transition and waits for old
   render/actor/VDP1/audio leases and VDP1 drawing to retire.
2. Master validates S64P, S64F, every S64B, hashes, source identities, and all
   aggregate budgets before the first texture upload.
3. Master assigns deterministic texture byte bases and CLUT base indices in
   sorted S64F variant order. These bases are runtime state, not serialized.
4. Master uploads cold payloads through the existing cart-to-WRAM staging and
   bounds-checked SCU-DMA residency path.
5. After all transfers complete, master publishes one residency generation and
   a fixed bounded `bank identity -> texture base / CLUT base` table.
6. Actor descriptor publication occurs only after that generation is visible.

Workers read immutable animation/pose/geometry/material/tile-directory data,
perform pose/project/admission work, and write the existing eight-byte
`{meshlet_id, primitive_id, sort_key}` records. They never read the residency
table, texture bytes, CLUT bytes, VDP1 state, or allocator state.

Final master emission uses the descriptor's validated bank identity and the
current residency generation to resolve a local tile ordinal. It translates
S64B-owned target enums to Yaul values at this boundary and calls the existing
`sm64_saturn_ir_texture_bind_clut16` or
`sm64_saturn_ir_texture_bind_rgb1555` helper. Those helpers currently accept an
eight-bit width even though VDP1 encodes widths through 504 pixels. The v2
implementation must widen the helper width parameter to `uint16_t`, retain the
exact existing call behavior, and test the 8/248/256/504 boundaries; silently
truncating the serialized v2 width is forbidden. Missing, stale, mismatched,
or unresident mappings quarantine the instance before a command is emitted.

Preflight/planning failures publish no new residency generation. An unexpected
failure after DMA begins is fatal for the new scene activation; the runtime
does not claim the overwritten old texture generation remains usable. V2 does
not implement rollback, eviction, paging, or per-frame uploads.

## Validation and failure behavior

Target validation is bounded and linear in file size plus record counts. Dense
ordinals and canonical ordering avoid target hash tables and quadratic alias
searches.

V2 rejects at least:

- every wrong magic/version/header or record size;
- zero/overflowing required counts and derived sizes;
- reordered, overlapping, aliased, noncanonical, or trailing spans;
- nonzero padding or reserved fields;
- wrong source or payload identity;
- invalid animation/pose/GEO1 data under all existing checks;
- render-binding/material mismatch;
- tile presence inconsistent with recipe;
- unknown recipe, layer, alpha, selector, format, or flag;
- invalid tile dimensions, payload equation, alignment, or CLUT reference;
- invalid transparency words or palette entry zero;
- aliased, overlapping, or inconsistent tile/CLUT payload spans;
- incorrect scratch, draw, texture-command, Gouraud, texture, CLUT, or total
  size fields;
- S64F family/model/source/scratch mismatch;
- aggregate cart, VDP1, command, Gouraud, output-record, or scratch overflow;
- stale package, render, queue, or residency generations.

Compilation failures name the exact source key, command/state, and file. Runtime
activation failures name the exact bank/resource. Individual actor quarantine
is reserved for post-publication stale/mismatch evidence; invalid content never
enters an active generation.

## Verification strategy

### Host and target format gates

- Historical v1 Mario JSON/S64B byte and SHA-256 proof.
- V1-only and mixed v1/v2 S64F fixtures.
- Host/Python and freestanding-C agreement on every valid/mutated v2 fixture.
- Mutation coverage for every v2 field, reference, payload, padding byte,
  identity, and aggregate resource equation.
- Relocated clean-worktree byte identity.
- Explicit source-state whitelist tests and unknown-command rejection.
- Textured triangle non-pairing and command-count proof.
- Bank-local tile/CLUT deduplication determinism.

### Real BOB admission gate

- Replay all 47 canonical families and every model variant.
- Record each supported v2 bank and every unsupported key with a named reason.
- Require at least one normally spawned, visibly recognizable BOB non-Mario
  actor. If none survives the reviewed BOB material subset, implementation is
  blocked; a synthetic actor, Mario bank, stale bank, or forced first record is
  not a substitute.

### Target demo gate

After S64F packaging, residency, queue, and emitter integration:

- rebuild the exact BOB sourceboot candidate with dynamic actor closure on;
- require correct package/bank/residency identity and generation telemetry;
- require successful fixed residency with positive cart/VDP1/command/Gouraud/
  LWRAM/HWRAM margins;
- require zero allocation failures, zero stale-generation failures, and zero
  unexpected actor quarantine;
- require advancing simulation and presentation cadence;
- capture a headless Ymir report plus image/video showing the real selected
  BOB actor textured and recognizable during the normal BOB route.

This is emulator-backed Saturn evidence. Retail-hardware evidence remains a
separate gate.

Because target bytes change, the implementation must then reopen and rerun the
Task 9 clean-worktree A/B reproducibility build, release-manifest comparison,
native-math measurement/seal/pin/v4 acceptance, memory/cart/package proofs,
release staging/overwrite refusal, Task 10 smoke, visual, desktop, and owner
manual gates. No prior sealed candidate remains current.

## Non-goals for v2

- Pixel-exact N64 combiner equivalence.
- `GEO_SHADOW`, `GEO_SCALE`, or `GEO_ASM` support unless separately specified
  and reviewed.
- Animated or per-instance-selected textures.
- Environment mapping, texture generation, mipmapping, or multi-texture.
- Textured triangle pairing.
- Scene-time eviction, paging, streaming, or seamless CD reads.
- Shared cross-bank texture pools.
- Runtime decompression or quantization.
- Enlarging the actor arena, object pool, or hardware partitions.
- Synthetic/demo-only actor fallbacks.

## Reference and reuse record

The implementation starts from reviewed in-repository code rather than a new
parallel texture stack:

- `tools/saturn/vdp1_texture.py`: direct reuse of RGB1555 conversion,
  transparency canonicalization, and BIOS-backed distorted-sprite weights.
- `tools/saturn/bake_castle_uv.py` and the sourceboot BOB bake path described by
  `docs/superpowers/specs/2026-07-26-vdp1-textures-design.md`: close-port/direct
  reuse of offline Fast3D tile-state evaluation, repeated-vertex triangle
  sampling, CLUT16 quantization, canonical tile packing, and budget reporting.
- `src/port/saturn/gfx/saturn_ir_texture.*` and
  `saturn_texture_residency.h`: direct reuse of target command binding and
  bounded SCU-DMA residency.
- `src/port/saturn/gfx/saturn_actor_bank.*`,
  `saturn_actor_bundle.*`, `saturn_actor_meshlets.*`, and Task 16 workspace
  APIs: shared-core extension/close-port, preserving v1 and the reviewed queue
  ABI.

The 2026-07-26 texture design already records the external prior-art study:
SlaveDriver Engine at pinned commit
`a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later, was used
pattern-only for VDP1 partitioning and distorted-sprite/corner strategy;
SONIC Z-TREME at `cff75451c1616aac1236fc2b44223902b55c706b` was behavior-study
only because its licensing record is contradictory. This design copies no
external source and introduces no new license obligation.

## Status and next gate

The verbal architecture was approved by the owner on 2026-08-11. The written
specification passed scoped self-review and binary-layout arithmetic/reference
checks, was committed as `62f16de8`, and received owner approval on 2026-08-12.
The dependency-ordered implementation plan is
`docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`. Task 4
remains `blocked-before-RED` until that plan is committed and execution is
dispatched; no v2 production or test change is authorized by this design
document alone.

Task 4, Tasks 5-11, Task 16 Tasks 2-5, target/release/reseal, sourceboot,
capacity/map, P2/Ymir, transition, smoke, visual, desktop, owner-manual,
retail-hardware, and total-game gates remain open.
