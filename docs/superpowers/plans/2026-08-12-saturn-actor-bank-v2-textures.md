# Saturn Actor Bank v2 Textures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an additive texture/material-capable S64B v2, package the exact
reviewed generic BOB subset in an S64F-v3 dependency whose parser supports both
v1 and v2, run historical v1 Mario and v2 generic actors together through the
production dual-SH-2 scene path, and prove a normally spawned textured Cannon
in BOB under Ymir before rebuilding and resealing the release.

**Architecture:** The host captures complete closure-attested Fast3D material signatures and lowers only a reviewed BOB whitelist into deterministic, target-ready VDP1 tiles, palettes, and stable S64B-owned material recipes. S64B owns v1/v2 parsing; S64F remains an opaque mixed-bank container. At runtime the master SH-2 validates aggregate budgets, uploads all scene actor textures while VDP1 is idle, publishes one generation last, and emits final commands; worker SH-2s continue to write the existing eight-byte scalar output records without VDP1 pointers or mutable residency state.

**Tech Stack:** Python 3 deterministic asset compilers and `unittest`; freestanding C11 host/SH-2 validators; GNU Make 4.3+ through `tools/saturn/with-msys-toolchain.ps1`; Yaul/VDP1, SCU DMA, 32-Mbit DRAM cart, S64P/S64F/S64B formats, Ymir capture tooling, and the existing hermetic release-manifest/native-math v4 chain.

**Approved design:** `docs/superpowers/specs/2026-08-11-saturn-actor-bank-v2-textures-design.md`, architecture commit `62f16de8`, owner written-spec approval received 2026-08-12.

**Execution status (2026-08-12):** implementation plan self-reviewed and
committed as `3338de20`; ready for Task 1 RED under the owner-selected
subagent-driven workflow. No production/test behavior change has begun.

## Global Constraints

- S64B v1 remains version 1 with a 104-byte header; historical Mario JSON/S64B bytes and SHA-256 remain exact.
- S64B v2 remains self-contained and pointer-free with a 192-byte header. Bytes 0..103 retain v1 positions; bytes 104..191 are the exact approved extension.
- S64F v3 remains a 96-byte header with 64-byte family and 88-byte variant records. Its embedded bank span is opaque and may contain validated S64B v1 or v2.
- All serialized integers are big-endian. Every offset is root-relative, every reserved/padding byte is zero, and every addition/multiplication/alignment is checked before forming a pointer.
- The initial compiler supports only exact measured BOB signatures with reviewed Saturn recipes. Unknown, computed, partial, ambiguous, or unconsumed material state fails offline by source key and command/state name.
- Textured source triangles are never paired in v2. Each emits one `(A,B,C,C)` distorted-sprite tile; ordinary untextured v1 pairing remains unchanged.
- The shared VDP1 texture ceiling is 446,432 bytes, not an actor allocation. Scene planning reserves terrain, Mario, command tables, Gouraud tables, CLUTs, and HUD before proving the generic-actor share.
- Texture bytes and CLUT bytes have separate exact budgets. Command and Gouraud counts are also separate. A build never silently reduces live counts, drops variants, changes formats, or spills to heap.
- The reviewed actor arena remains exactly 65,536 bytes with 2,718 eight-byte output records. Package workspace remains two fixed claimant lanes; v2 adds no per-family or per-instance allocation.
- Immutable hot/cold actor-bank bytes stay in the fixed 32-Mbit DRAM cart. Cold texture/CLUT spans are uploaded to VDP1 only during master-owned scene activation while gameplay is suspended and VDP1 is idle.
- Cross-SH-2 publication contains scalar IDs, hashes, offsets, counts, lane indexes, and generations only. No pointer, VDP1 address, allocator state, command pointer, or residency-table pointer enters a worker descriptor or queue record.
- The master validates, uploads, publishes residency generation last, resolves material/tile ordinals, translates stable S64B enums to Yaul values, and emits final VDP1 commands. Workers evaluate pose/admission and write only the unchanged `{meshlet_id, primitive_id, sort_key}` records.
- No heap, recursive target parser, runtime Fast3D interpreter, runtime quantization/conversion, gameplay-time CD read, paging, eviction, rollback, decompression, or shared cross-bank texture pool is permitted.
- At least one normally spawned recognizable BOB non-Mario actor must compile and render through the production path. The required initial demo key is family 29, `MODEL_CANNON_BASE` (`0x0080`), behavior `bhvCannon`; a synthetic actor, Mario substitution, stale bank, or forced first record is not accepted.
- Every behavior commit updates `CHANGELOG.md` in the same commit, updates this plan and the active SDD ledger/report, stages explicit paths only, and receives independent spec-compliance plus code-quality review before the next task.
- Target-byte changes invalidate the prior Task 9 release. After the demo, clean A/B reproducibility, release-manifest equality, native-math measurement/seal/pin/v4, capacity/package/staging, Task 10 smoke/visual/desktop/manual, and total-game gates must be rerun; prior evidence is historical only.

## Reference and Reuse Record

- Direct reuse: `tools/saturn/vdp1_texture.py` RGB1555 conversion, transparency canonicalization, and BIOS-backed distorted-sprite weights.
- Close-port/direct reuse: `tools/saturn/bake_castle_uv.py` and `docs/superpowers/specs/2026-07-26-vdp1-textures-design.md` Fast3D tile-state evaluation, triangle sampling, CLUT16 quantization, canonical packing, and budget reporting.
- Shared-core extension: `tools/saturn/compile_actor_bank.py`, `tools/saturn/actor_family_bundle.py`, `src/port/saturn/gfx/saturn_actor_bank.*`, `saturn_actor_bundle.*`, `saturn_actor_meshlets.*`, `saturn_ir_texture.*`, and `saturn_texture_residency.h`.
- Lifecycle/ownership pattern reuse: `saturn_scene_package.*`, `saturn_scene_residency.*`, render snapshot generation-last publication, P2/TAS.B lane ownership, and the Task 16 actor runtime handoff.
- External prior art remains pattern-only/behavior-study as already recorded: SlaveDriver Engine `a8986591557b6e680550d3c23970284d3b38ff8f` (GPL-3.0-or-later) and SONIC Z-TREME `cff75451c1616aac1236fc2b44223902b55c706b` (license record contradictory). No external source is copied.

## File and Responsibility Map

### Host format and compiler

- Create `tools/saturn/actor_bank_format.py`: sole host authority for S64B v1/v2 parsing, validated views, record enums, and shared overflow/span checks.
- Create `tools/saturn/test_actor_bank_format.py`: byte-layout, v1 preservation, v2 mutation, and host/target fixture coverage.
- Create `tools/saturn/actor_bank_v2.py`: v2 dataclasses, source identity, deterministic promotion of a validated v1 core to v2, and canonical tile/CLUT packing.
- Create `tools/saturn/test_actor_bank_v2.py`: exact 192-byte header/record/payload/deduplication tests.
- Create `tools/saturn/actor_material_v2.py`: closure-only Fast3D state capture, exact BOB lowering table, offline tile baking, and named unsupported errors.
- Create `tools/saturn/test_actor_material_v2.py`: real BOB signature fixtures, command/state mutations, Cannon admission, and unknown-state rejection.
- Modify `tools/saturn/actor_variant_bank.py`: compile materialized variants as v2 while retaining the v1 path and shared geometry encoder.
- Modify `tools/saturn/actor_family_bundle.py`: delegate embedded validation to `actor_bank_format`; keep S64F-v3 bytes unchanged.
- Create `tools/saturn/compile_actor_family_bundle.py`: real BOB orchestration, atomic bundle/report/dependency publication.
- Create `tools/saturn/test_compile_actor_family_bundle.py`: supported/unsupported inventory, relocation, no-clobber, and exact budget tests.
- Create `tools/saturn/inventory_actor_family_bundles.py` and test: scene-level aggregate capacities and generated fixed headers/catalog.

### Target format, residency, and rendering

- Modify `src/port/saturn/gfx/saturn_actor_bank.h/.c`: version dispatch, v2 spans/records/accessors, linear validation, and v1 behavior preservation.
- Modify `src/port/saturn/gfx/saturn_actor_bundle.h/.c`: opaque mixed v1/v2 delegation without duplicated v2 offsets.
- Create `src/port/saturn/gfx/saturn_actor_material.h/.c`: stable recipe-to-Yaul translation and command binding from validated bank/tile ordinals.
- Modify `src/port/saturn/gfx/saturn_ir_texture.h/.c`: widen texture width to `uint16_t` and preserve exact 8/248 behavior while accepting 256/504.
- Create `src/port/saturn/gfx/saturn_actor_texture_residency.h/.c`: fixed master-owned bank-to-VDP1 mapping and generation-last publication.
- Create `tools/saturn/actor_bank_v2_test.c`, `actor_material_test.c`, and `actor_texture_residency_test.c`: freestanding target-equivalent fixtures.
- Create/modify the scene bundle runtime/stream/sourceboot files named by the active generic-bundle plan; keep fixed storage and master-only CDFS/cart/VDP1 ownership.
- Modify `src/port/saturn/gfx/saturn_demo_render.c` and `src/port/saturn/sourceboot/main.c`: production feature-on queue drain, material emission, retirement, and telemetry; feature-off Mario shape remains exact.

### Build, evidence, and governance

- Modify `Makefile.saturn.mk` and `src/port/saturn/sourceboot/Makefile`: deterministic generators, host gates, source closure, ISO inputs, target link, and feature identity.
- Modify `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, this plan, the generic-bundle plan, Task 16 plan, and their SDD ledgers/reports during each transition.
- Generated artifacts stay under `build/saturn/`; deterministic evidence JSON/images stay under `docs/saturn/evidence/` only when a governing gate requires them.

---

### Task 1: Establish version-owned host S64B parsing

**Execution status:** source-complete-pending-review (2026-08-12). The shared
v1 parser moved to `actor_bank_format`; v2 dispatch fails closed at its named
unimplemented boundary. Fresh host tests and the historical Mario/S64F-v3
byte proofs passed; independent spec-compliance and code-quality reviews are
still required before Task 2.

**Files:**
- Create: `tools/saturn/actor_bank_format.py`
- Create: `tools/saturn/test_actor_bank_format.py`
- Modify: `tools/saturn/actor_family_bundle.py`
- Modify: `tools/saturn/actor_variant_bank.py`
- Modify: `tools/saturn/test_actor_family_bundle.py`
- Modify: `tools/saturn/test_actor_variant_bank.py`
- Modify: `CHANGELOG.md`, this plan, and SDD ledger/report

**Interfaces:**
- Consumes: exact S64B v1 bytes and the existing `_validate_s64b` semantics.
- Produces:

```python
@dataclass(frozen=True)
class ActorBankView:
    payload: bytes
    version: int
    family_ordinal: int
    model_id: int
    source_sha256: bytes
    lane_bytes: int
    maximum_scratch: int
    primitive_count: int
    material_count: int
    hot_end: int
    texture_payload_offset: int
    texture_payload_size: int
    clut_payload_offset: int
    clut_payload_size: int

validate_actor_bank(payload: bytes) -> ActorBankView
validate_actor_bank_expected(payload: bytes, source_sha256: bytes) -> ActorBankView
```

- [ ] **Step 1: Write the failing version-dispatch tests**

Add tests that import `validate_actor_bank`, validate the historical Mario v1
payload unchanged, reject version 0/3, and route version 2 to the exact named
`S64B v2 contract is not implemented` boundary owned by this module. Assert
`actor_family_bundle` no longer owns `_S64B_HEADER` or `_validate_s64b`.

```python
self.assertEqual(validate_actor_bank(mario).version, 1)
with self.assertRaisesRegex(ValueError, "unsupported S64B version"):
    validate_actor_bank(mario[:4] + b"\x00\x03" + mario[6:])
```

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_bank_format tools.saturn.test_actor_family_bundle tools.saturn.test_actor_variant_bank -v`. Expected: `ModuleNotFoundError: tools.saturn.actor_bank_format` and the ownership assertion fails.

- [ ] **Step 3: Move the v1 parser without changing behavior**

Copy the reviewed validation logic into the new authority, factor common prefix/GEO1/workspace helpers, and make both callers import it. The v2 branch may initially reject with the exact version-specific error; do not relax v1 spans or hashes.

```python
def validate_actor_bank(payload: bytes) -> ActorBankView:
    version = _read_u16(payload, 4, "S64B version")
    if version == 1:
        return _validate_v1(payload)
    if version == 2:
        raise ValueError("S64B v2 contract is not implemented")
    raise ValueError("unsupported S64B version")
```

- [ ] **Step 4: Run GREEN and historical-byte proof**

Run the three suites, `verify-actor-family-bundle verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets`, and rehash historical Mario JSON/S64B against their committed expected values. Expected: all pass; no generated byte changes.

- [ ] **Step 5: Commit and independent review**

Stage only the listed behavior/tests/docs and commit `refactor(saturn): centralize actor bank version parsing`. Require spec-compliance and code-quality PASS before Task 2.

---

### Task 2: Implement canonical host S64B v2 packing and validation

**Files:**
- Create: `tools/saturn/actor_bank_v2.py`
- Create: `tools/saturn/test_actor_bank_v2.py`
- Modify: `tools/saturn/actor_bank_format.py`
- Modify: `tools/saturn/test_actor_bank_format.py`
- Modify: `tools/saturn/compile_actor_bank.py`
- Modify: `CHANGELOG.md`, this plan, and SDD ledger/report

**Interfaces:**
- Consumes: one validated canonical v1 core produced by `pack_actor_bank`, a v2 source digest, and immutable target resources.
- Produces:

```python
@dataclass(frozen=True)
class RenderBindingV2:
    material_id: int
    tile_id: int

@dataclass(frozen=True)
class TargetMaterialV2:
    recipe: int
    layer: int
    alpha_mode: int
    selector_kind: int = 0

@dataclass(frozen=True)
class TextureTileV2:
    pixels: bytes
    width: int
    height: int
    format: int
    clut: bytes | None

@dataclass(frozen=True)
class ActorBankResourcesV2:
    bindings: tuple[RenderBindingV2, ...]
    materials: tuple[TargetMaterialV2, ...]
    tiles: tuple[TextureTileV2, ...]
    bake_policy_id: int

source_identity_v2(family_ordinal: int, model_id: int,
                   sources: Sequence[SourceRecord], bake_policy_id: int,
                   policy_document: Mapping[str, object]) -> bytes
pack_actor_bank_v2(core_v1: bytes, source_sha256: bytes,
                   resources: ActorBankResourcesV2) -> tuple[bytes, dict[str, object]]
```

- [ ] **Step 1: Write exact layout and mutation tests**

Cover all offsets 104..191, 8/8/16-byte records, canonical span order, alignment, padding, source digest, payload equations, dense ordinals, exact tile/CLUT first-use deduplication, distinct-identical target acceptance, transparency words, aggregate fields, overflow, and trailing bytes. Include one untextured v2 and one CLUT16 textured v2 fixture.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_bank_v2 tools.saturn.test_actor_bank_format -v`. Expected: missing `actor_bank_v2` module and v2 validator rejection.

- [ ] **Step 3: Implement deterministic v2 promotion and parser**

Rebase the validated v1 core from header 104 to 192, adjust every absolute animation/span offset, preserve GEO1-relative offsets, append records/payloads in the exact design order, and validate the finished payload before returning it.

```python
delta = S64B_V2_HEADER_SIZE - S64B_V1_HEADER_SIZE
rebased_records = _rebase_animation_records(core_v1, delta)
payload = bytearray(S64B_V2_HEADER_SIZE)
payload.extend(rebased_records_and_core_spans)
_append_v2_resources(payload, resources)
view = validate_actor_bank(bytes(payload))
```

- [ ] **Step 4: Run GREEN plus v1 byte identity**

Run both suites, full actor-bank/family-bundle Python suites, `verify-actor-family-bundle verify-actor-pose-bank verify-actor-meshlets`, and compare a fresh Mario output byte-for-byte with the pre-task fixture. Expected: v2 green; v1 unchanged.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): define canonical actor bank v2 bytes`. Review arithmetic, canonicalization, deduplication complexity, and absence of host paths before Task 3.

---

### Task 3: Add target S64B v2 and mixed S64F validation

**Files:**
- Create: `tools/saturn/actor_bank_v2_test.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bank.h`
- Modify: `src/port/saturn/gfx/saturn_actor_bank.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bundle.c`
- Modify: `tools/saturn/actor_family_bundle_test.c`
- Modify: `tools/saturn/test_actor_family_bundle.py`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, this plan, and SDD ledger/report

**Interfaces:**
- Consumes: Task 2 valid/mutated fixtures.
- Produces stable enums and accessors:

```c
#define SM64_SATURN_ACTOR_BANK_VERSION_V1 1U
#define SM64_SATURN_ACTOR_BANK_VERSION_V2 2U
#define SM64_SATURN_ACTOR_BANK_VERSION SM64_SATURN_ACTOR_BANK_VERSION_V1
#define SM64_SATURN_ACTOR_BANK_V2_HEADER_SIZE 192U

bool sm64_saturn_actor_bank_render_binding(
    const sm64_saturn_actor_bank_view_t *view, uint16_t primitive,
    sm64_saturn_actor_render_binding_t *out);
bool sm64_saturn_actor_bank_target_material(
    const sm64_saturn_actor_bank_view_t *view, uint16_t material,
    sm64_saturn_actor_target_material_t *out);
bool sm64_saturn_actor_bank_texture_tile(
    const sm64_saturn_actor_bank_view_t *view, uint16_t tile,
    sm64_saturn_actor_texture_tile_t *out);
```

- [ ] **Step 1: Write failing C fixtures**

Generate v1-only, v2-only, and mixed S64F-v3 fixtures from Python. The C test must match every host-view field, reject every mutated v2 field/payload, and prove S64F validation/resolve never reads v2 offsets itself.

- [ ] **Step 2: Run RED**

Run `verify-actor-bank-v2 verify-actor-family-bundle`. Expected: missing constants/accessors and mixed bundle rejection.

- [ ] **Step 3: Implement one version dispatch and linear v2 validation**

Split common prefix/GEO1 checks from version tails. Populate v1 v2-only view fields with zero; populate v2 fields after exact linear span/record/payload checks. Replace S64F manual v1 assumptions with `sm64_saturn_actor_bank_validate_expected`.

```c
switch (parsed.bank.version) {
case SM64_SATURN_ACTOR_BANK_VERSION_V1:
    return validate_v1_tail(bytes, byte_count, &parsed);
case SM64_SATURN_ACTOR_BANK_VERSION_V2:
    return validate_v2_tail(bytes, byte_count, &parsed);
default:
    return false;
}
```

- [ ] **Step 4: Run GREEN and freestanding compile**

Run `verify-actor-bank-v2 verify-actor-family-bundle verify-actor-pose-bank verify-actor-meshlets verify-actor-feature-off-wrapper`, plus the target compiler dry-run for `saturn_actor_bank.c`. Expected: host/target agreement and unchanged v1 callers.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): validate mixed actor bank versions`. Require parser/ABI review before source material work.

---

### Task 4: Capture and lower the exact measured BOB material subset

**Files:**
- Create: `tools/saturn/actor_material_v2.py`
- Create: `tools/saturn/test_actor_material_v2.py`
- Modify: `tools/saturn/actor_variant_bank.py`
- Modify: `tools/saturn/test_actor_variant_bank.py`
- Modify: `tools/saturn/test_actor_source.py`
- Modify: `tools/saturn/vdp1_texture.py`
- Modify: `CHANGELOG.md`, this plan, and SDD ledger/report

**Interfaces:**
- Consumes: closure-attested GeoLayout/Gfx/Vtx/texture sources, the existing strict tokenizers, `vdp1_texture` conversion/weights, and Task 2 packing.
- Produces:

```python
@dataclass(frozen=True)
class MaterialSignatureV2:
    texture_path: str | None
    texture_sha256: bytes | None
    combine_mode: tuple[str, ...]
    geometry_mode: tuple[str, ...]
    tile_state: tuple[int, ...]
    layer: str
    opacity: int

compile_materials_v2(index, display_lists, primitives,
                     source_identity_inputs) -> tuple[
                         ActorBankResourcesV2, tuple[SourceRecord, ...],
                         dict[str, object]]
```

- [ ] **Step 1: Freeze real BOB signatures and RED mutations**

Replay all 34 drawable keys. Assert the 14 direct textured keys reach a complete signature. Require family 29/model `0x0080`/`bhvCannon` to compile. Mutate texture image, tile size, mask/shift, wrap/clamp, combiner, geometry mode, layer, opacity, call/tail transfer, UV, and texture bytes; each must fail by exact state/source name. `GEO_SHADOW`, `GEO_SCALE`, and `GEO_ASM` remain named unsupported.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_material_v2 tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v`. Expected: missing module and existing `unsupported rigid-group source: textured` for Cannon.

- [ ] **Step 3: Implement strict material state and offline baking**

Extend the existing Fast3D compiler state machine rather than adding a second parser. Every recognized state-changing command updates an immutable canonical state; every unknown command rejects. For each accepted textured triangle, call the reviewed weight/bake helpers and emit one unpaired tile.

```python
resources, material_sources, material_report = compile_materials_v2(
    index, reached_lists, geometry.primitives, sources)
digest = source_identity_v2(family_ordinal, model_id,
                            sources + material_sources,
                            resources.bake_policy_id,
                            material_report["policy"])
payload, packed = pack_actor_bank_v2(core, digest, resources)
```

- [ ] **Step 4: Run GREEN and full-key inventory**

Run the focused suites, then replay all 47 families. Expected: Cannon and every exact whitelisted textured signature compile as v2; all remaining keys have one deterministic named reason; two `MODEL_NONE` entries remain non-drawable. Re-run pose/meshlet and historical Mario hash gates.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): bake BOB actor materials for VDP1`. Review source closure, state exhaustiveness, Saturn fidelity declarations, non-pairing, and demo-key truth before Task 5.

---

### Task 5: Build the real mixed BOB S64F and prove aggregate budgets

**Files:**
- Create: `tools/saturn/compile_actor_family_bundle.py`
- Create: `tools/saturn/test_compile_actor_family_bundle.py`
- Create: `tools/saturn/inventory_actor_family_bundles.py`
- Create: `tools/saturn/test_inventory_actor_family_bundles.py`
- Modify: `tools/saturn/actor_family_bundle.py`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, this plan, generic-bundle plan, and SDD ledger/report

**Interfaces:**
- Produces:

```python
compile_scene_bundle(root: Path, closure_path: Path,
                     family_report_path: Path, model_ids_path: Path,
                     package_generation: int,
                     output_dir: Path) -> dict[str, object]

inventory_bundles(root: Path,
                  package_generation: int) -> dict[str, object]
```

The report includes each family/key, bank version/hash/source identity, texture/CLUT bytes, draw/texture-command/Gouraud counts, live-count contribution, unsupported reason, bundle totals, all shared-profile reservations, remaining margins, and one canonical S64P `ACTOR_DEPENDENCIES` record.

- [ ] **Step 1: Write RED orchestration/resource tests**

Require at least Cannon, nonzero variant count, a separate mixed-v1/v2 S64F
fixture, byte-identical relocated real-BOB builds, exact ten-class BOB package
ownership, separate texture/CLUT totals, family worst-case command/Gouraud
math, 446,432-byte shared ceiling after reservations, 65,536/2,718 arena
limits, 32-Mbit cart fit, no host paths, report-last publication, no overwrite,
and named failure for every one-byte overflow. The real generic BOB S64F may
contain only v2 banks; historical v1 Mario remains a separately owned scene
bank and proves the mixed-version scene path.

- [ ] **Step 2: Run RED**

Run both new suites. Expected: missing compiler/inventory modules.

- [ ] **Step 3: Implement canonical orchestration and planner**

Compile each unique supported key once, keep unsupported rows, pack/validate S64F, compute unique-bank residency and per-family worst cases, write a private staging directory, publish payload/dependency/header first and report last with no-clobber semantics.

```python
dependency = {
    "kind": "ACTOR_DEPENDENCIES",
    "stable_id": "bob-area1-actors-v3",
    "generation": package_generation,
    "destination_class": "CART",
    "lifetime": "SCENE",
    "alignment": 4,
    "max_scratch": 0,
    "byte_count": len(payload),
    "sha256": hashlib.sha256(payload).hexdigest(),
}
```

- [ ] **Step 4: Run real BOB build and GREEN wave**

Run `compile-actor-family-bundle inventory-actor-family-bundles verify-actor-family-bundle-build verify-actor-bank-v2 verify-actor-capability-bank verify-actor-capability-articulated`. Record exact supported/unsupported counts, bytes, hashes, and every positive margin; no target claim yet.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): build bounded textured BOB actor bundle`. Review generated inventory and capacity equations before target rendering.

---

### Task 6: Bind stable v2 materials to exact VDP1 commands

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_material.h`
- Create: `src/port/saturn/gfx/saturn_actor_material.c`
- Create: `tools/saturn/actor_material_test.c`
- Create: `tools/saturn/ir_texture_test.c`
- Modify: `src/port/saturn/gfx/saturn_ir_texture.h`
- Modify: `src/port/saturn/gfx/saturn_ir_texture.c`
- Modify: `Makefile.saturn.mk` to add `verify-ir-texture` and `verify-actor-material`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`, this plan, and ledger/report

**Interfaces:**

```c
typedef struct sm64_saturn_actor_texture_mapping {
    uint32_t bank_id;
    uint32_t texture_base_offset;
    uint16_t clut_base_index;
    uint16_t tile_count;
    uint32_t generation;
} sm64_saturn_actor_texture_mapping_t;

bool sm64_saturn_actor_material_bind(
    vdp1_cmdt_t *cmdt, const vdp1_vram_partitions_t *partitions,
    const sm64_saturn_actor_bank_view_t *bank, uint16_t primitive_id,
    const sm64_saturn_actor_texture_mapping_t *mapping,
    uint32_t active_generation,
    const int16_vec2_t vertices[4]);
```

- [ ] **Step 1: Write RED width/recipe tests**

Require CLUT16/RGB1555 flat/Gouraud/half-transparent recipes, stable-enum translation, end-code disabled, exact source-address/size/CLUT calculations, and width boundaries 8, 248, 256, 504. Reject 0, 505, stale mapping, wrong bank, invalid tile/material, and no command mutation on failure.

- [ ] **Step 2: Run RED**

Run `verify-ir-texture verify-actor-material`. Expected: uint8 truncation at 256/504 and missing actor material API.

- [ ] **Step 3: Widen width and implement master-only translation**

Change both IR binders to `uint16_t width`, retain the exact current encoding for 8/248, validate 8..504/multiple-of-eight before converting to the VDP1 size field, and map only the approved stable recipes to Yaul enums.

```c
if (width < 8U || width > 504U || (width & 7U) != 0U)
    return false;
cmd_size = (uint16_t)(((width / 8U) << 8) | height);
```

- [ ] **Step 4: Run GREEN and SH-2 compile**

Run IR/material/bank/family host gates and compile the two changed modules with the exact SH-2 flags. Expected: all boundaries pass and feature-off calls are byte/behavior compatible.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): lower actor bank materials to VDP1`. Review enum separation, width arithmetic, command mutation, and master-only ownership.

---

### Task 7: Add master-owned actor texture residency and generation publication

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_texture_residency.h`
- Create: `src/port/saturn/gfx/saturn_actor_texture_residency.c`
- Create: `tools/saturn/actor_texture_residency_test.c`
- Modify: `src/port/saturn/gfx/saturn_texture_residency.h`
- Modify: `src/port/saturn/runtime/saturn_scene_residency.h/.c`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`, this plan, and ledger/report

**Interfaces:**

```c
#define SM64_SATURN_ACTOR_TEXTURE_MAPPING_CAPACITY 128U

typedef struct sm64_saturn_actor_texture_publication {
    sm64_saturn_actor_texture_mapping_t mappings[128];
    uint32_t texture_bytes, clut_bytes, generation;
    uint16_t mapping_count;
    volatile uint8_t committed;
    uint8_t reserved;
} sm64_saturn_actor_texture_publication_t;

bool sm64_saturn_actor_texture_residency_activate(
    sm64_saturn_actor_texture_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    const vdp1_vram_partitions_t *partitions,
    uint32_t generation, bool gameplay_suspended, bool vdp1_idle);
bool sm64_saturn_actor_texture_residency_lookup(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation, uint32_t bank_id,
    sm64_saturn_actor_texture_mapping_t *out);
```

- [ ] **Step 1: Write RED lifecycle/DMA tests**

Prove sorted variant assignment, exact deduped bank count, separate texture/CLUT ranges, checked SCU-DMA calls, generation/committed written last, lookup by scalar identity, no worker pointer, and no publication for false suspended/idle, bad hash, short VRAM, stale generation, transfer failure, or any aggregate mismatch.

- [ ] **Step 2: Run RED**

Run `verify-actor-texture-residency verify-scene-residency`. Expected: missing API.

- [ ] **Step 3: Implement fixed all-resident activation**

Extend the existing bounded uploader with
`sm64_saturn_texture_residency_init_region(residency, base, capacity)`, then
initialize independent bounded uploaders for `partitions->texture_base` /
`texture_size` and `partitions->clut_base` / `clut_size`. Assign mappings in
canonical S64F order; upload texture and CLUT spans only after validating every
bank and the complete plan; fence, write scalars/table, fence, then publish
nonzero generation/committed.

- [ ] **Step 4: Run GREEN and lease regressions**

Run residency, scene residency, VDP1 frame bank, transfer pipeline, Gouraud transfer, bundle, and feature-off gates. Expected: no old-generation reuse is claimed after a failed new activation.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): publish actor texture residency generations`. Review DMA bounds, publication ordering, and failure semantics before runtime cutover.

---

### Task 8: Package, stream, and retain the mixed actor scene dependency

**Files:**
- Modify: `tools/saturn/compile_scene_package.py`
- Modify: `tools/saturn/test_scene_package_schema.py`
- Modify: `tools/saturn/test_scene_package_determinism.py`
- Create: `src/port/saturn/gfx/saturn_actor_bundle_runtime.h/.c`
- Create: `tools/saturn/actor_bundle_runtime_test.c`
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`
- Modify: `tools/saturn/actor_meshlet_test.c`
- Create: `src/port/saturn/runtime/saturn_scene_stream.h/.c`
- Create: `tools/saturn/scene_stream_test.c`
- Create: `src/port/saturn/sourceboot/source_scene_bundle.h/.c`
- Create: `tools/saturn/source_scene_bundle_test.c`
- Modify: `src/port/saturn/sourceboot/source_cart.h/.c`
- Modify: `Makefile.saturn.mk`, sourceboot Makefile, CHANGELOG/docs/ledgers

**Interfaces:**
- Consumes: Task 5 S64F/dependency/catalog/capacity, fixed cart span, two-lane workspace, and Task 7 texture publication.
- Produces a scalar generation-last bundle publication, one transient
resolution per claimed lane, and the source owner:

```c
typedef struct sm64_saturn_actor_bundle_publication {
    uint32_t residency_generation, package_generation, cart_offset, byte_count;
    uint32_t content_hash_words[8], workspace_lane_stride, maximum_scratch;
    uint16_t family_count, variant_count;
    volatile uint8_t lane_claim[2];
    volatile uint8_t committed;
    uint8_t reserved;
} sm64_saturn_actor_bundle_publication_t;

typedef struct sm64_saturn_actor_bundle_resolution {
    sm64_saturn_actor_bundle_variant_t variant;
    sm64_saturn_actor_bank_view_t bank;
    sm64_saturn_actor_meshlet_bank_output_t workspace;
    uint32_t residency_generation, package_generation;
    uint8_t lane;
} sm64_saturn_actor_bundle_resolution_t;

bool sm64_saturn_actor_meshlets_bind_bundle_workspace(
    const sm64_saturn_actor_bank_view_t *bank,
    void *workspace, uint32_t workspace_capacity,
    uint32_t lane_stride, uint8_t lane,
    sm64_saturn_actor_output_record_t *records, uint16_t draw_capacity,
    sm64_saturn_actor_meshlet_workspace_t *output);

bool sm64_saturn_actor_bundle_runtime_publish(
    sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    uint32_t residency_generation, uint32_t cart_offset);
bool sm64_saturn_actor_bundle_runtime_claim(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane);
bool sm64_saturn_actor_bundle_runtime_resolve(
    const sm64_saturn_actor_bundle_publication_t *publication,
    const sm64_saturn_actor_bundle_view_t *bundle,
    void *workspace, uint32_t workspace_capacity,
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_bundle_resolution_t *output);
bool sm64_saturn_actor_bundle_runtime_release(
    sm64_saturn_actor_bundle_publication_t *publication,
    uint32_t residency_generation, uint8_t lane);

bool sm64_saturn_source_scene_bundle_init(uint16_t level_id, uint16_t area_id);
bool sm64_saturn_source_scene_bundle_step(bool gameplay_suspended);
bool sm64_saturn_source_scene_bundle_resolve(
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_bank_view_t *bank,
    sm64_saturn_actor_meshlet_bank_output_t *workspace);
void sm64_saturn_source_scene_bundle_release(uint8_t lane,
                                             uint32_t generation);
```

- [ ] **Step 1: Write RED package/runtime/stream fixtures**

Require a non-provisional S64P with exactly one actor dependency in CART,
exact hash/generation, zero root scratch, bounded 16-sector reads while
suspended, lease drain before reclaim, and direct-to-cart no-copy validation.
For two banks with different lane sizes, require concurrent lane 0/1 claims at
the bundle-wide stride for raw workspace residues 0..3, exact alignment and
nonoverlap, texture activation before descriptor publication, and
generation-last failure behavior.

- [ ] **Step 2: Run RED**

Run `verify-scene-package-schema verify-actor-bundle-runtime verify-scene-stream verify-source-scene-bundle`. Expected: missing strict dependency-manifest/runtime/stream/source owner interfaces.

- [ ] **Step 3: Implement fixed storage and lifecycle**

Load S64P/S64F from generated ISO names into the fixed root/cart spans, validate hashes, retain the bundle view, activate actor textures, bind the generated two-lane workspace, and publish actor descriptors only after both package and texture generations agree.

```c
if (!sm64_saturn_actor_texture_residency_activate(
        &owner->textures, &owner->bundle, partitions,
        next_generation, gameplay_suspended, vdp1_idle))
    return fail(owner, SM64_SATURN_SCENE_BUNDLE_TEXTURE_FAILURE);
owner->active_generation = next_generation;
```

- [ ] **Step 4: Run GREEN and exact package binding**

Run package schema/runtime/determinism, scene residency/stream, bundle runtime, texture residency, source owner, actor meshlet/pose/queue/batch, and feature-off gates. Parse the real BOB S64P/S64F and confirm exact hashes/bytes/generation plus no linked duplicate bank.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): retain textured actor scene bundles`. Review lifecycle, cart/VRAM/LWRAM ownership, leases, and no-copy aliases before Task 9.

---

### Task 9: Cut the production generic actor queue and emitter over to v2

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/gfx/saturn_actor_instance.c`
- Modify: existing actor handoff/render host fixtures
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`, Task 16 plan/ledger, this plan/ledger

**Interfaces:**
- Consumes: `sm64_saturn_source_scene_bundle_resolve`, Task 16 generalized meshlet workspace, queue/batch ABI, Task 6 material binder, Task 7 current texture mapping.
- Produces feature-on ACTOR_ADMIT/ACTOR_LOWER work that drains real descriptors; master merge resolves each output record to its validated bank primitive/material/tile and emits painter-ordered commands.

- [ ] **Step 1: Write RED production-shaped host tests**

Exercise observer/registry capture through handoff population, either-SH-2 lane claim, pose/meshlet admission, descriptor-owned output, terminal publication, master merge/material bind, acknowledge, and queue-owned retirement. Require zero actors to retain the two-world-job graph and a stale texture/package generation to quarantine before command mutation. Assert feature-on wrappers no longer contain unconditional `return false`.

- [ ] **Step 2: Run RED**

Run the actor runtime handoff, instance queue, batches, render overlap, demo render, material, and feature-off wrapper gates. Expected: current feature-on ACTOR jobs fail closed at the compat wrappers.

- [ ] **Step 3: Replace only the auditable feature-on seam**

Populate the real queue from admitted snapshots in `main.c`; replace the feature-on compat bodies with queue drain calls; leave the four-job graph/dependencies unchanged; master merge uses current bundle/residency generations and material binder; all retirement flows through the handoff. Feature-off retains the exact Mario path.

- [ ] **Step 4: Run GREEN, capacity, and rollback proof**

Run the full actor/render wave. Accept 64 live/2,718 records; reject 0 capacity, 65 live, 2,719 records, overlap, stale generations, wrong hashes, and mid-merge failure. Build feature-off and compare affected object code or the established exact source-policy proof with pre-task HEAD.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): render textured generic actors in production`. Require two-stage review of dual-SH-2 ownership, painter order, retirement, and feature-off identity.

---

### Task 10: Wire deterministic build closure and prove target capacity

**Files:**
- Modify: `Makefile.saturn.mk`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: source closure/feature identity tests
- Modify: `docs/saturn/BUILDING.md`, `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, plans/ledgers

**Interfaces:**
- Consumes every generator/source/report/package/bundle/catalog/recipe from Tasks 1-9.
- Produces one serialized development candidate under `build/saturn/variants/actor-bank-v2-bob` with exact ELF/ISO/CUE/map/telemetry and BOB S64P/S64F ISO entries.

- [ ] **Step 1: Write RED Make/source-closure assertions**

Require the feature-on graph to generate/verify all v2 inputs before compile, hash them in discovery and post-link closure, include non-provisional S64P/S64F in ISO, link each target module once, and omit v2/runtime symbols and generated assets under feature-off.

- [ ] **Step 2: Run RED and implement Make wiring**

Run hermetic Make and feature-identity tests; expect missing targets/inputs. Add bounded list-file transport where argv scale requires it and preserve release-mode fail-closed ordering.

- [ ] **Step 3: Run the complete host gate wave**

Run all new suites plus existing actor family/variant/pose/meshlet/queue/batch/handoff/render, scene package/residency/stream, texture/VDP1 transfer, hermetic Make, and feature-off gates. Run Python `compileall` and scoped `git diff --check`.

- [ ] **Step 4: Build and inspect the development target**

Run:

```powershell
python tools/saturn/build_sourceboot_variant.py --label actor-bank-v2-bob --animation 1 --actors 1 --audio 0 --pipeline 4 --diagnostic-mode none --jobs 1 --output build/saturn/variants/actor-bank-v2-bob
```

Require positive HWRAM/LWRAM/cart/VDP1 texture/CLUT/command/Gouraud margins, exact 65,536-byte actor arena and 2,718 records, one two-lane workspace, no duplicate S64B/S64F, direct `sh-elf-ar`/`sh-elf-nm` attestation, and correct package/bank/residency generation telemetry.

- [ ] **Step 5: Commit and independent review**

Commit `build(saturn): wire textured actor bank closure`. Review exact map and generated inventory before emulator acceptance.

---

### Task 11: Prove the real textured Cannon demo in Ymir

**Files:**
- Modify only if a diagnosed harness defect exists: existing Ymir capture/probe tests and helper
- Create: `docs/saturn/evidence/reports/bob-textured-cannon-s64b-v2-2026-08-12.json`
- Create: `docs/saturn/evidence/screenshots/bob-textured-cannon-s64b-v2-2026-08-12.png`
- Modify: `STATE.md`, `ROADMAP.md`, plans/ledgers/reports

**Interfaces:**
- Consumes the exact Task 10 development ELF/ISO/CUE and established canonical Ymir BIOS/profile.
- Produces identity-bound telemetry and visual evidence for family 29/model `0x0080`/`bhvCannon` through normal BOB spawning.

- [ ] **Step 1: Verify the candidate before launch**

Rehash ELF/ISO/CUE/S64P/S64F, validate package/bundle/banks, confirm Cannon's registry/bank/material/tile identities, and record exact BIOS/profile hashes. Stop if any binding differs.

- [ ] **Step 2: Run the exact headless smoke/capture route**

Launch the established BOB route with dynamic actor closure on and enough frames to reach a normally spawned Cannon. Require advancing source/presentation cadence, cart copy complete, package/bank/texture generation exact, exception zero, allocation failures zero, actor quarantine zero, ACTOR_ADMIT/LOWER terminal success, Cannon draw/texture command counts nonzero, and positive live margins.

- [ ] **Step 3: Capture and visually inspect the actor**

Save JSON plus PNG/video. The Cannon must be recognizable and textured in its normal BOB location; a forced camera may frame it, but object spawning/model selection may not be injected. Inspect the image directly and record known VDP1-vs-N64 fidelity differences.

- [ ] **Step 4: Run transition/stale-generation mutations**

Repeat the host transition fixture and one emulator reload/scene-transition route. Require old leases to retire, new texture generation to publish last, and stale descriptor/material mapping to quarantine without command mutation.

- [ ] **Step 5: Commit evidence status and obtain review**

Commit `docs(saturn): prove textured Cannon actor demo`. Independent evidence review must verify artifact hashes, telemetry, visual identity, and normal-spawn claim before release rebuilding.

---

### Task 12: Rebuild, reproduce, reseal, and restage the release

**Files:**
- Modify generated contract/pin/evidence only as required by exact Task 9 workflow
- Modify: Task 9 release report, hermetic plan/ledger, `STATE.md`, `ROADMAP.md`

**Interfaces:**
- Consumes the reviewed source commit after Task 11 and the exact Task 9 command/profile/flag tuple.
- Produces two independently built byte-identical candidates, a new manifest, unsealed measurement, one-shot v4 contract/pin, passing v4 result, capacity/package report, and canonical no-clobber stage.

- [ ] **Step 1: Prepare clean candidate A/B prerequisites**

Create/refresh two owned clean worktrees at one common source commit; prove tracked cleanliness, pinned libyaul, baserom hash, non-reparse copied `build/us_pc` inventory, LF canonical files, and absent candidate `build/saturn` outputs.

- [ ] **Step 2: Run exact serialized candidate A and B builds**

Use the recorded Task 9 release-mode `-j1 verify-sourceboot` tuple unchanged. Require each outer command exit 0 and direct manifest verification. Compare canonical manifests and require `identical:true`, zero differing fields, and byte-identical ELF/SOURCE.DAT/ISO/CUE.

- [ ] **Step 3: Measure, seal, pin, and execute v4**

Run measurement first with no contract/pin creation; require `measured-unsealed`, root `_game_loop_one_iteration`, and forbidden callers absent. Generate the contract once, pin its exact SHA by TDD, preserve v2/v3 audit bytes, then run exact v4 and require status `passed`.

- [ ] **Step 4: Recompute capacity/package and stage**

Measure `___end`, HWRAM physical/usable margins, cart span/headroom, SOURCE.DAT object/CD/ISO equality, ISO LBA/blocks, exact package classes/root, actor texture/CLUT/command/Gouraud margins, and occupancy floor/gaps. Publish the five-file canonical stage, verify it, rerun staging to prove overwrite refusal, and confirm inventory unchanged.

- [ ] **Step 5: Commit evidence and independent rereviews**

Commit behavior/pin separately from evidence/status. Run Task 9 code-quality and evidence rereviews; no Task 10 or manual claim is made here.

---

### Task 13: Rerun Task 10 smoke, visual, desktop, and owner gates

**Files:**
- Modify/create exact Task 10 evidence reports and screenshots
- Modify: `STATE.md`, `ROADMAP.md`, active plans/ledgers/reports

**Interfaces:**
- Consumes the new canonical staged release and canonical Ymir profile/BIOS.
- Produces final sourceboot smoke/visual/desktop/manual status without substituting the development demo for release evidence.

- [ ] **Step 1: Run exact staged-release automated smoke**

Require loaded code/build identity, cart copy complete, exception zero, advancing simulation/presentation beyond the prior two-tick actor-wrapper stall, full sample count, pool peak below 208 with zero failures, successful ACTOR jobs, correct Cannon/material telemetry, and no stale/quarantine failures.

- [ ] **Step 2: Run release-bound visual capture**

Capture the staged release on the normal BOB route and verify the textured Cannon, Mario, terrain, HUD, cadence, and absence of corruption. Hash JSON/image/video and bind them to the staged manifest/ELF/ISO.

- [ ] **Step 3: Run desktop launch and owner checklist**

Verify immutable snapshot lifetime, exact CLI CUE/ISO equality, live process cleanup, and owner manual gameplay checks. Retail-hardware evidence stays separately open unless actually executed.

- [ ] **Step 4: Run final host/target verification and reviews**

Freshly run every focused suite, full native-math verifier with honest baseline accounting, pycompile, direct staged-manifest verification, show/diff checks, code-quality review, and evidence review.

- [ ] **Step 5: Reconcile final status**

Record exact commits, hashes, tests, visual/manual verdicts, remaining unsupported BOB/full-game semantics (`GEO_SHADOW`, `GEO_SCALE`, `GEO_ASM`, and unapproved materials), and every open retail/total-game gate. Mark only gates actually passed.

## Self-Review Checklist

Self-review completed 2026-08-12. It corrected the real-Bundle/mixed-scene
distinction, retained S64P alignment 4, added independent bounded CLUT upload,
made bundle-wide heterogeneous workspace stride explicit, and bound material
emission to the active texture generation.

- [x] Every S64B-v2 design requirement maps to at least one task and exact test.
- [x] V1 bytes, S64F-v3 wire bytes, actor arena, output record, queue, and feature-off contracts remain explicit.
- [x] Host and target public type/function names match across tasks.
- [x] Texture, CLUT, command, Gouraud, cart, LWRAM, and HWRAM budgets are separate and scene-aggregate.
- [x] Master/worker responsibilities are consistent in compiler, residency, queue, and emitter tasks.
- [x] Cannon demo evidence cannot be satisfied by a synthetic object or injected model.
- [x] Unknown material/Geo states remain named offline failures.
- [x] Release rebuild/reseal and Task 10 gates follow target-byte changes.
- [x] Every behavior task includes RED, GREEN, exact commit scope, CHANGELOG, ledger, and two-stage review.
- [x] No placeholder language, unchecked invented dependency, or unowned generated source path remains.

## Execution Order and Stop Rules

Execute Tasks 1-13 serially. A task may not begin until the prior task's behavior commit and both independent reviews pass. Stop immediately on a format/interface contradiction, inability to compile the exact Cannon key, nonpositive shared-hardware margin, host/target parser disagreement, dirty/unbound source input, target crash/stall/quarantine, nonidentical A/B release, failed v4, or failed smoke/visual/manual gate. Record the exact failure without weakening flags, budgets, source identity, or acceptance.

Task 4 of `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md` resumes only through Tasks 1-5 here. Its downstream Tasks 5-11 and Task 16 Tasks 2-5 are satisfied/reconciled through Tasks 7-13 here; do not run the stale v1-only Task 4 instructions in parallel.
