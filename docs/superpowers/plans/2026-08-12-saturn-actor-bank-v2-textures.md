# Saturn Actor Bank v2 Textures Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build an additive texture/material-capable S64B v2, package the exact
reviewed generic BOB subset in an S64F-v3 dependency whose parser supports both
v1 and v2, run historical v1 Mario and v2 generic actors together through the
production dual-SH-2 scene path, and prove a normally spawned textured Cannon
in BOB under Ymir before rebuilding and resealing the release.

**Architecture:** The host captures complete closure-attested Fast3D material signatures and lowers only a reviewed BOB whitelist into deterministic, target-ready VDP1 tiles, palettes, and stable S64B-owned material recipes. S64B owns v1/v2 parsing; S64F remains an opaque mixed-bank container. At runtime the master SH-2 validates all-resident data budgets and an atomic per-frame output/command/Gouraud credit policy, uploads all scene actor textures while VDP1 is idle, publishes one generation last, and emits final commands; worker SH-2s continue to write the existing eight-byte scalar output records without VDP1 pointers or mutable residency state.

**Tech Stack:** Python 3 deterministic asset compilers and `unittest`; freestanding C11 host/SH-2 validators; GNU Make 4.3+ through `tools/saturn/with-msys-toolchain.ps1`; Yaul/VDP1, SCU DMA, 32-Mbit DRAM cart, S64P/S64F/S64B formats, Ymir capture tooling, and the existing hermetic release-manifest/native-math v4 chain.

**Approved design:** `docs/superpowers/specs/2026-08-11-saturn-actor-bank-v2-textures-design.md`, architecture commit `62f16de8`, owner written-spec approval received 2026-08-12.

**Execution status (2026-08-12):** Tasks 1 through 4 are independently approved.
Task 2 behavior `83cfc1ad` plus repair `95de6457` owns the exact
192-byte S64B-v2 extension, deterministic v1-core rebasing, target-resource
deduplication, and fail-closed validation while historical Mario/S64F bytes
remain exact; scoped rereview passed C0/I0/M0 through `d5914059`. Task 3 is
complete at behavior commit `87be53b6`; the target owns v1/v2 dispatch,
linear v2 validation/accessors, and
opaque mixed-S64F delegation; 86 direct S64B mutations, 54 S64F mutations, 63
broader Python tests, the historical actor gates, and the freestanding SH-2
syntax check pass. Task 4 behavior `52c9c1af` plus repair `86de51cc` compiles
the exact 14 measured direct-textured BOB keys, including Cannon, and its scoped
rereview passed C0/I0/M0 through `f5a03808`. Task 5 is complete for its host
scope after fix round 2/5 at `cf8bef5c`; scoped rereview passed Spec/Quality,
C0/I0/M0 after round 1 received C0/I1/M0 for an incomplete transitive host-tool
prerequisite closure. Its
initial zero-edit preflight exposed and corrected
a design error: source-pool family ceilings are not simultaneous resource
allocations. Task 6 is complete for host and freestanding target-module scope:
same-reviewer rereview of `5ceb251c..d0fbc5aa` passed Spec/Quality, C0/I0/M0,
after fix round 1 at `661e54a4`. Task 7 is complete after repair commits
`4c4c24a9`, `c270f363`, and `d1408e00`; same-reviewer final rereview passed
Spec/Quality C0/I0/M0. Fixed all-resident planning and checked upload publish a
2,064-byte scalar scene-owned table for the exact 14-bank BOB subset, while
independent review remains mandatory. Tasks 8-13 and every runtime, demo,
release, reseal, smoke, visual, desktop,
manual, retail, and total-game gate remain open. No target runtime, residency,
renderer, or Ymir state changed.

## Owner convergence reset: working generic BOB by 2026-08-14

The prior Cannon-only witness is no longer the milestone. Cannon remains one
required regression, but the end-of-week target is a working normal BOB scene
through the generic path:

- the source level script, ordinary spawn/object registry, canonical S64P/S64F,
  generic actor queue, generic bank/material lookup, and production renderer are
  the only accepted path;
- no injected model, forced record, synthetic actor, object-specific renderer
  branch, or Cannon-only package may satisfy the gate;
- all 34 drawable BOB selections must be admitted by the generic compiler and
  runtime. Saturn-shaped common reductions for shadow/scale/effect semantics
  may be telemetry-visible, but no unsupported drawable may quarantine or stall
  the complete scene;
- Ymir must show advancing simulation and presentation, successful ACTOR jobs,
  zero exception/allocation failure, no stale generation, Mario/terrain/HUD,
  and normally spawned representative BOB actors including a coin, enemy,
  sign/cannon, and moving actor;
- the first short identity-bound Ymir smoke runs immediately after minimum
  Task 8 package and Task 9 cutover/build wiring. Task 10's exhaustive capacity
  proof and Tasks 12-13 release/reseal/manual sequence remain after that live
  feedback.

No new Saturn plan or wire-format generation may open before this generic BOB
gate without explicit owner approval. Newly exposed prerequisites are folded
into Tasks 8-9 and replace lower-priority proof work; they do not create another
parallel infrastructure sprint.

### Bound memory/ownership record for the convergence chain

| Object | Region / maximum | Owner and lifetime | Transport / first consumer |
| --- | --- | --- | --- |
| S64P + S64F + embedded S64B | immutable 32-Mbit DRAM CART; current BOB S64F 160,928 B | Task 8 scene residency; load through commit/unload generation | CDFS/cart load; generic bundle resolver |
| Actor cold upload stage | 2,560 B phase-borrowed from the idle VDP1 command bank during boot; zero persistent HWRAM | Task 8 source owner; serial reuse ends before the first frame exposes the command bank | CPU CART→borrowed HWRAM, checked SCU DMA HWRAM→VDP1, wait before reuse/return |
| Actor VDP1 texture/CLUT bytes | current aggregate 16,640 B texture + 2,816 B CLUT in separate partition regions | master scene activation; publication generation commits last | Task 6 generic material binder / production emitter |
| Actor texture publication | 2,064 B inside the fixed 5,556 B Task 8 LWRAM source owner, 128 mappings | scene/source residency reset/rollback/commit/unload | master lookup; no pointer enters worker records |
| Actor pose/meshlet workspace | fixed 1,280 B LWRAM generated ceiling; current bundle uses 1,091 B, margin 189 B; two lanes; overlaps boot-only 3,348 B root-validation view | Task 8 bundle runtime; lane claim through terminal job publication | transient bank resolution on master/slave; outside the actor output arena |
| Actor queue/output arena | fixed 65,536 B LWRAM, 2,718 eight-byte records | Task 9 queue/handoff generation | master snapshot→dual-SH-2 jobs→master merge |
| Frame command/Gouraud credits | 1,351 post-Mario commands, 892 post-Mario Gouraud; exact frame dry-sum | Task 9 master frame policy | generic actor set before optional terrain |
| Scene validation call stack | exact SH-2 Task 8 result: begin 92 B, commit 88 B, section load 112 B, source init 220 B; all ≤256 B after reusing state-owned views/slots | master scene transition only; no recursive/nested validator | S64P validation then generic bundle owner; GCC 14.3 `-m2 -mb -fstack-usage` |

Every changed bound must update this table before implementation. Task 10 must
replace host/measured values with linked-ELF and live telemetry margins, but it
does not delay the first short Ymir smoke.

## Global Constraints

- S64B v1 remains version 1 with a 104-byte header; historical Mario JSON/S64B bytes and SHA-256 remain exact.
- S64B v2 remains self-contained and pointer-free with a 192-byte header. Bytes 0..103 retain v1 positions; bytes 104..191 are the exact approved extension.
- S64F v3 remains a 96-byte header with 64-byte family and 88-byte variant records. Its embedded bank span is opaque and may contain validated S64B v1 or v2.
- All serialized integers are big-endian. Every offset is root-relative, every reserved/padding byte is zero, and every addition/multiplication/alignment is checked before forming a pointer.
- The initial compiler supports only exact measured BOB signatures with reviewed Saturn recipes. Unknown, computed, partial, ambiguous, or unconsumed material state fails offline by source key and command/state name.
- Textured source triangles are never paired in v2. Each emits one `(A,B,C,C)` distorted-sprite tile; ordinary untextured v1 pairing remains unchanged.
- The shared VDP1 texture ceiling is 446,432 bytes, not an actor allocation. Scene planning reserves terrain, Mario, command tables, Gouraud tables, CLUTs, and HUD before proving the generic-actor share.
- Texture bytes and CLUT bytes have separate exact budgets. Command and Gouraud counts are also separate. Source-attested per-family live ceilings remain unchanged, but are not summed as simultaneous allocations. The complete observed frame is admitted atomically against exact per-variant credits or quarantined as a whole; a build/runtime never silently reduces live counts, drops variants, changes formats, or spills to heap.
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

**Execution status:** complete (2026-08-12). The shared v1 parser moved to
`actor_bank_format`; v2 dispatch fails closed at its named unimplemented
boundary. Fix round 1 explicitly owns and validates the final two reserved
header bytes. Fresh host tests and historical Mario/S64F-v3 byte proofs passed;
scoped independent rereview approved the result at C0/I0/M0.

**Fix round 1:** the complete 104-byte v1 header now owns its final two-byte
zero-reserved field. Mutating either byte fails closed; the historical Mario
and S64F-v3 proofs were rerun without a byte change.

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

- [x] **Step 1: Write the failing version-dispatch tests**

Add tests that import `validate_actor_bank`, validate the historical Mario v1
payload unchanged, reject version 0/3, and route version 2 to the exact named
`S64B v2 contract is not implemented` boundary owned by this module. Assert
`actor_family_bundle` no longer owns `_S64B_HEADER` or `_validate_s64b`.

```python
self.assertEqual(validate_actor_bank(mario).version, 1)
with self.assertRaisesRegex(ValueError, "unsupported S64B version"):
    validate_actor_bank(mario[:4] + b"\x00\x03" + mario[6:])
```

- [x] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_bank_format tools.saturn.test_actor_family_bundle tools.saturn.test_actor_variant_bank -v`. Expected: `ModuleNotFoundError: tools.saturn.actor_bank_format` and the ownership assertion fails.

- [x] **Step 3: Move the v1 parser without changing behavior**

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

- [x] **Step 4: Run GREEN and historical-byte proof**

Run the three suites, `verify-actor-family-bundle verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets`, and rehash historical Mario JSON/S64B against their committed expected values. Expected: all pass; no generated byte changes.

- [x] **Step 5: Commit and independent review**

Stage only the listed behavior/tests/docs and commit `refactor(saturn): centralize actor bank version parsing`. Require spec-compliance and code-quality PASS before Task 2.

---

### Task 2: Implement canonical host S64B v2 packing and validation

**Execution status:** complete and independently approved (2026-08-12),
behavior `83cfc1ad`, repair `95de6457`, evidence `d5914059`. The host now
promotes a validated v1 core into the exact
192-byte pointer-free v2 layout, rebases bank-absolute pose/span offsets,
packs dense first-use target resources, and reparses the completed bytes
through the version-owned validator. Focused and broader host gates are green;
scoped rereview passed C0/I0/M0. No target/runtime evidence is claimed; Task 3
may now open.

**Fix round 1/5:** complete and approved at behavior `95de6457`. The initial review
found two Important gaps (C0/I2/M0): v2 accepted zero required draw counts,
and aggregate output bounds were enforced after bytearray growth. V2 now
rejects zero meshlet/primitive counts in both parser and packer while v1 keeps
its historical behavior. A checked reduced-limit preflight covers the promoted
core, every table/alignment/texture/CLUT/record/copy span, and total size before
the sole output allocation. Sixteen focused and 62 broader tests plus all host
gates pass; canonical v2 and Mario/S64F fixture bytes remain exact. Scoped
rereview found both Important issues addressed with no new findings.

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

- [x] **Step 1: Write exact layout and mutation tests**

Cover all offsets 104..191, 8/8/16-byte records, canonical span order, alignment, padding, source digest, payload equations, dense ordinals, exact tile/CLUT first-use deduplication, distinct-identical target acceptance, transparency words, aggregate fields, overflow, and trailing bytes. Include one untextured v2 and one CLUT16 textured v2 fixture.

- [x] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_bank_v2 tools.saturn.test_actor_bank_format -v`. Expected: missing `actor_bank_v2` module and v2 validator rejection.

- [x] **Step 3: Implement deterministic v2 promotion and parser**

Rebase the validated v1 core from header 104 to 192, adjust every absolute animation/span offset, preserve GEO1-relative offsets, append records/payloads in the exact design order, and validate the finished payload before returning it.

```python
delta = S64B_V2_HEADER_SIZE - S64B_V1_HEADER_SIZE
rebased_records = _rebase_animation_records(core_v1, delta)
payload = bytearray(S64B_V2_HEADER_SIZE)
payload.extend(rebased_records_and_core_spans)
_append_v2_resources(payload, resources)
view = validate_actor_bank(bytes(payload))
```

- [x] **Step 4: Run GREEN plus v1 byte identity**

Run both suites, full actor-bank/family-bundle Python suites, `verify-actor-family-bundle verify-actor-pose-bank verify-actor-meshlets`, and compare a fresh Mario output byte-for-byte with the pre-task fixture. Expected: v2 green; v1 unchanged.

- [x] **Step 5: Commit and independent review**

Commit `feat(saturn): define canonical actor bank v2 bytes`. Review arithmetic, canonicalization, deduplication complexity, and absence of host paths before Task 3.

---

### Task 3: Add target S64B v2 and mixed S64F validation

**Execution status:** complete and independently approved at behavior
`87be53b6`, evidence `ca3318cd`. The target dispatches once between v1 and v2, retains the
historical no-write-on-failure v1 view contract, validates every v2 extension/
resource before access, and S64F validation/resolve delegates opaque embedded
bytes to the S64B owner. Independent parser/ABI review passed C0/I0/M0. No
material compiler, residency, renderer, or Task 4+ claim is made; Task 4 may
now open.

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

- [x] **Step 1: Write failing C fixtures**

Generate v1-only, v2-only, and mixed S64F-v3 fixtures from Python. The C test must match every host-view field, reject every mutated v2 field/payload, and prove S64F validation/resolve never reads v2 offsets itself.

- [x] **Step 2: Run RED**

Run `verify-actor-bank-v2 verify-actor-family-bundle`. Expected: missing constants/accessors and mixed bundle rejection.

- [x] **Step 3: Implement one version dispatch and linear v2 validation**

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

- [x] **Step 4: Run GREEN and freestanding compile**

Run `verify-actor-bank-v2 verify-actor-family-bundle verify-actor-pose-bank verify-actor-meshlets verify-actor-feature-off-wrapper`, plus the target compiler dry-run for `saturn_actor_bank.c`. Expected: host/target agreement and unchanged v1 callers.

- [x] **Step 5: Commit and independent review**

Commit `feat(saturn): validate mixed actor bank versions`. Require parser/ABI review before source material work.

---

### Task 4: Capture and lower the exact measured BOB material subset

**Execution status:** complete and independently approved after fix round 1.
Initial review found C0/I2/M1 at behavior commit `52c9c1af` (2026-08-12).
Repair commit `86de51cc` passed the full host/historical gate set. The real
34-drawable/47-family replay is frozen; all 14 measured direct-textured keys
compile as S64B v2, the 18 `GEO_SHADOW`, one `GEO_SCALE`, one `GEO_ASM`, 13
capability-unsupported families, and two `MODEL_NONE` variants retain named
fail-closed outcomes. Cannon emits 30 draws, eight unpaired textured inputs,
1,024 texture bytes, 256 CLUT bytes, eight texture commands, and 30 Gouraud
tables per instance. Scoped rereview of repair `86de51cc` passed C0/I0/M0;
evidence head `f5a03808`. These are host-packed records only, not target budget
or runtime evidence; Task 5 aggregate scene budgeting may now open.

**Reference-code provenance:** direct same-project adaptation at reconciled
`Project12x/sm64-port` commit
`05b77e6472a09facd3d4faf01100ad09b2d9882e` (repository has no blanket root
license, so no external code was copied): `tools/saturn/actor_variant_bank.py`
(last owning commit `68ceec9c66070d48d05d6442e11c76e2b9b6b903`, close-port/extension of its
tokenizer and sole Fast3D state machine), `tools/saturn/dl_rigid_groups.py`
(`a78db8c9b777074e1e60b7260f3aaaa9596f1c10`, structural-walk reuse),
`tools/saturn/vdp1_texture.py`
(`2e5b41e6dfc2fe45a5c28d5c2b6c3d4c0b40c161`, direct extension),
`tools/saturn/bake_castle_uv.py`
(`b295928ef54aeadc00b0b578c89bb6e700878134`, direct weight/quantizer reuse),
`tools/saturn/bake_bob_tiles.py`
(`4c60f4fe35ea918361ed6d1ee2c598ca62627516`, close-port of strict PNG and
per-triangle bake pattern), and Task 2 `tools/saturn/actor_bank_v2.py`
(`95de64570cc20eefa94839ec13b9a33a9c28096e`, dependency/reuse). Reuse mode is
direct same-repository adaptation/close-port; no third-party source or new
license obligation was introduced.

**Fix round 1 design/file-list correction:** exact admission now freezes the
evaluated command-local trace and final state across the complete reached
display-list graph, including recognized state after the final triangle and
ambient/diffuse light commands. `collect_scene_closure.py` now owns unique
texture declaration resolution and checked-in `.rgba16`/`.ia16` PNG path/hash
attestation for both `gsDPSetTextureImage` and `gsDPLoadTextureBlock` before
validation/publication; `_SourceIndex` requires and decodes those exact bytes,
and downstream `SourceRecord` synthesis is forbidden. Scalar shift operands
and counts are bounded before evaluation. This narrowly adds
`tools/saturn/collect_scene_closure.py`, `test_scene_closure.py`, and
`test_bob_scene_closure.py` to Task 4; schema, target ABI/runtime, Task 5, and
later scope remain unchanged.

**Files:**
- Create: `tools/saturn/actor_material_v2.py`
- Create: `tools/saturn/test_actor_material_v2.py`
- Modify: `tools/saturn/actor_variant_bank.py`
- Modify: `tools/saturn/test_actor_variant_bank.py`
- Modify: `tools/saturn/test_actor_source.py`
- Modify: `tools/saturn/vdp1_texture.py`
- Modify: `tools/saturn/collect_scene_closure.py`
- Modify: `tools/saturn/test_scene_closure.py`
- Modify: `tools/saturn/test_bob_scene_closure.py`
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

- [x] **Step 1: Freeze real BOB signatures and RED mutations**

Replay all 34 drawable keys. Assert the 14 direct textured keys reach a complete signature. Require family 29/model `0x0080`/`bhvCannon` to compile. Mutate texture image, tile size, mask/shift, wrap/clamp, combiner, geometry mode, layer, opacity, call/tail transfer, UV, and texture bytes; each must fail by exact state/source name. `GEO_SHADOW`, `GEO_SCALE`, and `GEO_ASM` remain named unsupported.

- [x] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_material_v2 tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v`. Expected: missing module and existing `unsupported rigid-group source: textured` for Cannon.

- [x] **Step 3: Implement strict material state and offline baking**

Extend the existing Fast3D compiler state machine rather than adding a second parser. Every recognized state-changing command updates an immutable canonical state; every unknown command rejects. For each accepted textured triangle, call the reviewed weight/bake helpers and emit one unpaired tile.

```python
resources, late_sources, material_report = compile_materials_v2(
    index, reached_lists, geometry.primitives, sources)
digest = source_identity_v2(family_ordinal, model_id,
                            sources,
                            resources.bake_policy_id,
                            material_report["policy"])
# late_sources is retained only for interface compatibility and must be empty.
payload, packed = pack_actor_bank_v2(core, digest, resources)
```

- [x] **Step 4: Run GREEN and full-key inventory**

Run the focused suites, then replay all 47 families. Expected: Cannon and every exact whitelisted textured signature compile as v2; all remaining keys have one deterministic named reason; two `MODEL_NONE` entries remain non-drawable. Re-run pose/meshlet and historical Mario hash gates.

- [x] **Step 5: Commit and independent review**

Commit `feat(saturn): bake BOB actor materials for VDP1`. Review source closure, state exhaustiveness, Saturn fidelity declarations, non-pairing, and demo-key truth before Task 5.

---

### Task 5: Build the real mixed BOB S64F and prove aggregate budgets

**Execution status:** complete for host scope after fix round 2/5 at repair
`cf8bef5c`; scoped rereview passed Spec/Quality, C0/I0/M0. Initial behavior
commit `65a3fdb9` received C0/I3/M0 for an
unreconciled family report, stale-generation Make graph, and manual package-
profile intake. The repair recomputes exact family semantics through the owning
compiler (excluding only generated payload pathname), consumes only those
values, routes package inventory and Make dependency discovery through the
canonical target-profile validator, and verifies all four sidecars against a
private current-input rebuild. Round-1 rereview passed those three boundaries
but returned C0/I1/M0 because eleven transitive, output-affecting local Python
imports were absent from Make's normal prerequisite list. Round 2 adds that
exact closure and a deterministic recursive AST regression that requires every
repository-local import to be declared and every declared tool path to exist.
Source-ceiling
correction `acef808d`, frame-policy/partition correction `833c9bfb`. Applying the original
family-sum equation to the real 14-bank set counted 5,288 mutually incompatible
live contributions against the global 64-observer cap and produced impossible
85,512-record / 65,788-command / 29,352-Gouraud totals. These family fields are
source-pool safety ceilings (often the same recurrent 240-object bound, then
summed for shared families), not a joint scene allocation. The corrected
Saturn-shaped contract preserves and reports every ceiling, proves all-resident
data plus single-bank admissibility and a positive guaranteed service floor,
and reserves exact output/command/Gouraud credits for the complete observed set
atomically at runtime. No subset selection or count rewrite is allowed. Task 5
remains host-only; Task 9 owns that runtime preflight and Task 11 must prove the
actual complete BOB route has positive margins.

The share derivation follows the existing renderer's essential-actor-before-
optional-terrain policy: setup/END plus maximum Mario leave 1,351 command and
892 Gouraud credits for generic actors, while their output arena is the dedicated
2,718 records. With measured maxima 46 / 24 / 46, any supported mixture has a
conservative 19-actor floor; actual cheaper mixtures may admit up to 64. Actor
residency requires 16,640 texture plus 2,816 CLUT bytes and Task 7 must
repartition the existing 52,672-byte Yaul `remaining` region, leaving 33,216
bytes. Task 5 records that future layout requirement; it does not claim the
current binders can address `remaining` directly.

**Files:**
- Create: `tools/saturn/compile_actor_family_bundle.py`
- Create: `tools/saturn/test_compile_actor_family_bundle.py`
- Create: `tools/saturn/inventory_actor_family_bundles.py`
- Create: `tools/saturn/test_inventory_actor_family_bundles.py`
- Modify: `tools/saturn/actor_family_bundle.py`
- Modify: `tools/saturn/actor_family_bundle_test.c` (real-artifact validation mode)
- Modify: `tools/saturn/actor_capability_opaque_test.c` (generated trust anchor only)
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

- [x] **Step 1: Write RED orchestration/resource tests**

Require at least Cannon, nonzero variant count, a separate mixed-v1/v2 S64F
fixture, byte-identical relocated real-BOB builds, exact ten-class BOB package
ownership, separate texture/CLUT totals, preserved family source ceilings, the
named unconstrained source-ceiling diagnostic, exact per-bank credit costs, and
a positive post-reservation guaranteed service floor. Require the fixed
64-observer/65,536-byte/2,718-record limits, the 446,432-byte shared ceiling
and exact future texture/CLUT repartition equation after existing terrain/Mario
reservations, 32-Mbit cart fit, no host paths, report-last publication, no overwrite,
and named failure for every one-byte overflow. The real generic BOB S64F may
contain only v2 banks; historical v1 Mario remains a separately owned scene
bank and proves the mixed-version scene path.

- [x] **Step 2: Run RED**

Run both new suites. Expected: missing compiler/inventory modules.

- [x] **Step 3: Implement canonical orchestration and planner**

Compile each unique supported key once, keep unsupported rows, pack/validate
S64F, compute unique-bank residency, preserve per-family source ceilings, and
compute exact per-variant admission credits. Report the impossible unconstrained
source-ceiling envelope as diagnostic only. Compute the guaranteed service floor
from the maximum supported per-instance cost and the dedicated output / maximum-
Mario-after-setup command/Gouraud shares; require it to equal 19 for the current
measured set and require every individual-bank margin to be positive. Compute
the exact future Yaul texture/CLUT partition sizes and remaining total; do not
treat the current `remaining` base as an actor binder region. Write a
private staging directory and publish payload/dependency/header first and report
last with no-clobber semantics.

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

- [x] **Step 4: Run real BOB build and GREEN wave**

Run `compile-actor-family-bundle inventory-actor-family-bundles verify-actor-family-bundle-build verify-actor-bank-v2 verify-actor-capability-bank verify-actor-capability-articulated`. Record exact supported/unsupported counts, bytes, hashes, static-residency/individual-bank margins, guaranteed floors, and the explicitly non-acceptance unconstrained diagnostic; no target or actual-frame-fit claim yet.

Task-owned build/inventory tests, mixed-S64F C validation, S64B-v2 C
validation, and both capability validations pass. The opaque capability
fixture's expected-output trust anchor was narrowly resealed from its
pre-Task-4 `56e9a35c...` digest to twice-regenerated PNG-attested closure digest
`60c329ab...`; production parser/runtime semantics and the separate effect
oracle remain unchanged.

- [x] **Step 5: Commit and independent review**

Behavior `65a3fdb9`, repair rounds `2cc767c0` and `cf8bef5c`; scoped
fix-round-2 rereview passed Spec/Quality, C0/I0/M0. Generated inventory and
capacity equations are host-approved; target rendering remains a later gate.

---

### Task 6: Bind stable v2 materials to exact VDP1 commands

**Execution status (2026-08-12):** complete for host and freestanding
target-module scope from
reconciled base `a553b500` at behavior commit `863b4646`, with fix round 1
landed as `661e54a4` from frozen review head `5ceb251c`. Initial independent review returned Spec
FAIL / Quality needs fixes, C0/I3/M0: IR and actor aggregate address checks
validated only their start rather than the final byte, post-parse CLUT tile
ordinals were not locally bounded before global mapping, and the historical
family-bank trust anchor was stale. The repair uses checked integer address
spans through the final required byte, defines zero aggregate bytes as touching
no address, rechecks the local dense CLUT count, and reseals only the test
anchor after two identical `db611af6...` / header `60c329ab...` regenerations.
Both IR binders use a checked `uint16_t` width and
the new master-only final-emission boundary translates all seven stable
S64B-owned recipes to exact Yaul command fields only after validating the bank,
mapping generation/identity, tile/material ordinals, complete texture/CLUT
partition spans, address arithmetic, and four vertices. The prescribed RED
and focused GREEN passed; S64B-v2 86-mutation, mixed-S64F 54-mutation, pose,
meshlet, feature-off, and 40-test host gates passed, and both changed modules
passed exact SH-2 `-m2 -mb -ffreestanding -Werror` syntax/object compilation.
The formerly adjacent family-bank target is now authorized test-only scope and
passes against the twice-reproduced Task-5 payload; production family parsing
and the separate effect oracle are unchanged.
No residency publication, runtime activation, renderer/Ymir, or Task 7 work is
claimed. Same-reviewer rereview of exact range `5ceb251c..d0fbc5aa` passed
Spec PASS / Quality PASS, C0/I0/M0, with fresh focused/full host gates, exact
SH-2 syntax/object compilation, and scoped diff/cleanliness checks. Task 7
remains closed until this status commit records that approval.

**Task 6 reference-code provenance:** dependency/API adaptation against the
pinned libyaul gitlink `6012f79f237773378c8014e70d8998ad95a38d98`
(MIT), specifically `libyaul/scu/bus/b/vdp/vdp1/cmdt.h` command structure,
stable Yaul enum values, source/size/CLUT encoders, and
`libyaul/scu/bus/b/vdp/vdp1/vram.h` partitions. Same-project direct extension
uses `src/port/saturn/gfx/saturn_ir_texture.*` and the validated Task 3
`saturn_actor_bank.*` accessors at base `a553b500`; Task 4 host records in
`tools/saturn/actor_material_v2.py` and `actor_bank_v2.py` are inspected for
wire-enum parity only. Reuse mode is dependency/API use plus same-project
shared-core extension; no external source is copied.

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_material.h`
- Create: `src/port/saturn/gfx/saturn_actor_material.c`
- Create: `tools/saturn/actor_material_test.c`
- Create: `tools/saturn/ir_texture_test.c`
- Modify: `src/port/saturn/gfx/saturn_ir_texture.h`
- Modify: `src/port/saturn/gfx/saturn_ir_texture.c`
- Modify: `Makefile.saturn.mk` to add `verify-ir-texture` and `verify-actor-material`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`, this plan, and ledger/report
- Fix-round-1 authorized test-only modify:
  `tools/saturn/actor_family_bank_test.c`

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

- [x] **Step 1: Write RED width/recipe tests**

Require CLUT16/RGB1555 flat/Gouraud/half-transparent recipes, stable-enum translation, end-code disabled, exact source-address/size/CLUT calculations, and width boundaries 8, 248, 256, 504. Reject 0, 505, stale mapping, wrong bank, invalid tile/material, and no command mutation on failure.

- [x] **Step 2: Run RED**

Run `verify-ir-texture verify-actor-material`. Expected: uint8 truncation at 256/504 and missing actor material API.

- [x] **Step 3: Widen width and implement master-only translation**

Change both IR binders to `uint16_t width`, retain the exact current encoding for 8/248, validate 8..504/multiple-of-eight before converting to the VDP1 size field, and map only the approved stable recipes to Yaul enums.

```c
if (width < 8U || width > 504U || (width & 7U) != 0U)
    return false;
cmd_size = (uint16_t)(((width / 8U) << 8) | height);
```

- [x] **Step 4: Run GREEN and SH-2 compile**

Run IR/material/bank/family host gates and compile the two changed modules with the exact SH-2 flags. Expected: all boundaries pass and feature-off calls are byte/behavior compatible.

- [x] **Step 5: Commit and independent review**

Behavior `863b4646`, initial evidence `5ceb251c`, repair `661e54a4`, and repair
evidence `d0fbc5aa`. Same-reviewer rereview of `5ceb251c..d0fbc5aa` passed Spec
PASS / Quality PASS, C0/I0/M0. Fresh IR/material/bank/family/history gates and
both exact SH-2 freestanding syntax/object compiles pass; Task 6 is approved
without claiming residency, runtime activation, renderer integration, or Ymir.
A nonblocking later effects/runtime gate remains stale at oracle header/payload
hashes `56e9a35c...` / `861be66d...`; its oracle is intentionally untouched.

---

### Task 7: Add master-owned actor texture residency and generation publication

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_texture_residency.h`
- Create: `src/port/saturn/gfx/saturn_actor_texture_residency.c`
- Create: `tools/saturn/actor_texture_residency_test.c`
- Modify: `src/port/saturn/gfx/saturn_texture_residency.h`
- Modify (authorized correction): `src/port/saturn/gfx/saturn_ir_texture.c`
- Modify (authorized type move only): `src/port/saturn/gfx/saturn_actor_material.h`
- Modify: `src/port/saturn/runtime/saturn_scene_residency.h/.c`
- Modify: `tools/saturn/ir_texture_test.c`, `scene_residency_test.c`,
  `Makefile.saturn.mk`, `CHANGELOG.md`, this plan, and ledger/report

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
    void *staging, uint32_t staging_capacity,
    uint32_t generation, bool gameplay_suspended, bool vdp1_idle);
bool sm64_saturn_actor_texture_residency_lookup(
    const sm64_saturn_actor_texture_publication_t *publication,
    uint32_t generation, uint32_t bank_id,
    sm64_saturn_actor_texture_mapping_t *out);
```

- [x] **Step 1: Write RED lifecycle/DMA tests**

Prove sorted variant assignment, exact unique selected-v2 mapping count,
separate texture/CLUT ranges, checked queue submit/wait, generation/committed
written last, lookup by scalar identity, no worker pointer, and no publication
for false suspended/idle, bad hash, short VRAM, stale generation, transfer
failure, or any aggregate mismatch.

- [x] **Step 2: Run RED**

Run `verify-actor-texture-residency verify-scene-residency`. Expected: missing API.

- [x] **Step 3: Implement fixed all-resident activation**

Extend the existing bounded uploader with
`sm64_saturn_texture_residency_init_region(residency, base, capacity)`, then
initialize independent bounded uploaders for `partitions->texture_base` /
`texture_size` and `partitions->clut_base` / `clut_size`. Assign mappings in
canonical S64F order; upload texture and CLUT spans only after validating every
bank and the complete plan; fence, write scalars/table, fence, then publish
nonzero generation/committed.

- [x] **Step 4: Run GREEN and lease regressions**

Run residency, scene residency, VDP1 frame bank, transfer pipeline, Gouraud transfer, bundle, and feature-off gates. Expected: no old-generation reuse is claimed after a failed new activation.

- [x] **Step 5: Commit and independent review**

Commit `feat(saturn): publish actor texture residency generations`. Review DMA bounds, publication ordering, and failure semantics before runtime cutover.

---

### Task 8: Package, stream, and retain the mixed actor scene dependency

**Files:**
- Modify: `tools/saturn/compile_scene_package.py`
- Modify: `tools/saturn/validate_scene_package.py`
- Modify: `tools/saturn/emit_scene_package_header.py`
- Modify: `tools/saturn/test_scene_package_schema.py`
- Modify: `tools/saturn/test_scene_package_determinism.py`
- Create: `src/port/saturn/gfx/saturn_actor_bundle_runtime.h/.c`
- Create: `tools/saturn/actor_bundle_runtime_test.c`
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`
- Modify: `tools/saturn/actor_meshlet_test.c`
- Modify: `src/port/saturn/runtime/saturn_scene_residency.h/.c`
- Create: `src/port/saturn/sourceboot/source_scene_bundle.h/.c`
- Create: `tools/saturn/source_scene_bundle_test.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_actor_texture_residency.h/.c`
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

Task 8 owns the lifetime of one 32-byte-aligned 2,560-byte cold-upload span but
does not add a persistent HWRAM array. During boot, before frame commands are
visible, sourceboot borrows the first 2,560 bytes of the idle VDP1 command bank,
passes that span to Task 7 activation, waits after every checked SCU-DMA, and
returns the bank before gameplay. The bundle remains in CART.

- [x] **Step 1: Write RED package/runtime/owner fixtures**

Require a non-provisional S64P with exactly one actor dependency in CART,
exact hash/generation, zero root scratch, direct CART aliasing, generation-last
publication, and lane drain before reuse.
Capture the existing 3,920/3,420-byte scene-validation stack RED, then require
both transition paths to use the already-owned staging view/identity slot with
no scene-package-view or resident-identity local and at most 256 bytes per call.
For two banks with different lane sizes, require concurrent lane 0/1 claims at
the bundle-wide stride for raw workspace residues 0..3, exact alignment and
nonoverlap, texture activation before descriptor publication, and
generation-last failure behavior.

- [x] **Step 2: Run RED**

Run `verify-scene-package-schema verify-actor-bundle-runtime
verify-source-scene-bundle`. Expected: missing strict payload-root,
runtime-publication, and source-owner interfaces.

- [x] **Step 3: Implement fixed storage and lifecycle**

Retain validated aliases to S64P/S64F linked into their final CART image; do not
add a second CD streamer or copy. The root package owner emits the deterministic
assembly input and sourceboot consumes/seals it. Refactor
scene validation to reuse its state-owned staging view/slot instead of large
target-stack locals, validate hashes, activate actor textures, bind the fixed
1,280-byte two-lane LWRAM workspace, and publish actor descriptors only after
package and texture generations agree.

```c
if (!sm64_saturn_actor_texture_residency_activate(
        &owner->textures, &owner->bundle, partitions,
        owner->actor_upload_stage, sizeof(owner->actor_upload_stage),
        next_generation, gameplay_suspended, vdp1_idle))
    return fail(owner, SM64_SATURN_SCENE_BUNDLE_TEXTURE_FAILURE);
owner->active_generation = next_generation;
```

- [x] **Step 4: Run GREEN and exact package binding**

Run package schema/runtime/determinism, scene residency/stream, bundle runtime, texture residency, source owner, actor meshlet/pose/queue/batch, and feature-off gates. Parse the real BOB S64P/S64F and confirm exact hashes/bytes/generation plus no linked duplicate bank.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): retain textured actor scene bundles`. Review lifecycle, cart/VRAM/LWRAM ownership, leases, and no-copy aliases before Task 9.

#### Task 8 source-complete transition (2026-08-12)

- Initial status was `source-complete-pending-review` at behavior commit
  `b84103cd`, with evidence/status commit `0a180e39`. Independent review
  returned Spec FAIL / Quality needs fixes, C0/I3/M0; the repair is now
  source-complete-pending-rereview and Task 9 remains closed.
  Generation 14 emits one 740-byte
  S64P root (`9b0a0a4a...d101`) and one 160,928-byte S64F-v3 dependency
  (`3eee00fd...a523`) through deterministic relocation-neutral assembly
  (`bde84bb...15fb`). The embedded package identity is
  `d249a76c...042c`; no terrain/collision/sky payload is duplicated.
- The real package contains 47 families, 14 S64B-v2 variants, 20 named
  unsupported drawable selections, and two `MODEL_NONE` source objects. The
  common runtime successfully resolves and prepares a real v2 bank with
  nonzero bounded draw output. There is no Cannon/model/behavior-specific
  production branch. Task 9 must still broaden the 20 drawable selections and
  cut all 34 through production actor jobs/emission before the live BOB gate.
- The fixed source owner is 5,556 bytes of LWRAM: a 2,208-byte owner plus one
  3,348-byte union whose boot-only root view overlaps the 1,280-byte runtime
  two-lane workspace. Scene residency adds 320 bytes of scalar dependency
  metadata. Exact SH-2 stack is source init 220 B, scene begin 92 B, commit
  88 B, and section load 112 B.
- The prior planned persistent HWRAM upload buffer and second scene streamer
  were rejected as duplicate ownership. The package already has a final CART
  address; cold upload phase-borrows 2,560 bytes from the idle VDP1 command
  bank, waits before reuse, and returns it before the first frame. Actor
  texture/CLUT regions consume 16,640/2,816 bytes of Yaul remaining capacity,
  leaving 33,216 bytes.
- Initial host gates passed package schema 16/16, determinism 3/3, generic bundle/source
  owner/texture/scene residency, S64B-v2 86 mutations, mixed S64F 54
  mutations, pose, meshlets plus invalid-span mutation, instance queue,
  batches/neutrality 2/2, and feature-off 6/6. Installed GCC 14.3 exact
  `-m2 -mb -ffreestanding -Werror` object/stack gates pass for all five
  amended/new modules, and the generated assembly produces a `0x277a4`-byte
  `.rodata` object with exact root/bundle symbols.
- Behavior commit `b84103cd` includes the required CHANGELOG entry. A full
  hermetic sourceboot candidate link remains unchecked because this
  development worktree's `build/us_pc` fixture resolves outside its root;
  the identity-assets gate correctly rejects that pre-existing fixture. No
  MSYS DLL loader failure occurred. Review, linked target, Task 9 production
  cutover, Ymir, release, and manual gates remain open.

#### Task 8 independent review and repair round 1 (2026-08-12)

- Review of `dfa8b286..0a180e39` found no Critical or Minor issue and three
  Important defects. The cold stage overwrote command bank 0 after its fixed
  VDP1 prefix had been initialized; activation did not bind the exact actor
  dependency stable ID and scene lifetime; and root/sidecar/assembly outputs
  were overwritten unconditionally despite the claimed no-clobber contract.
- RED reproduced all three boundaries: source order placed both backend
  initializers before scene activation; separately resealed stable-ID and
  lifetime mutations still activated; and foreign root/report/header/ABI or
  assembly bytes were replaced. GREEN moves both command-prefix writes after
  cold-stage retirement, validates exact `bob-area1-actors-v3` plus scene
  lifetime, and close-ports Task 5's exclusive private-link publisher for the
  S64P, payload manifest, assembly, reports, and generated headers.
- Package schema is now 19/19 and determinism remains 3/3. The exact repository
  MSYS wrapper runs the repaired `verify-source-scene-bundle` gate twice; the
  second run leaves hashes and write times unchanged for all seven published
  files. The complete focused actor/package/scene host wave passes, generated
  assembly keeps SHA-256 `bde84bb...15fb`, and `source_scene_bundle.c` compiles
  with installed GCC 14.3 to an ELF32 big-endian SuperH object. Two old derived
  JSON files differed only by CRLF and were moved recoverably to
  `.superseded-crlf-*` before canonical LF publication.
- Repair behavior commit is `dc81808b`; the evidence/status commit and
  same-reviewer verdict remain to be recorded.
  Step 5, Task 9, linked target, Ymir, release, visual, and manual gates stay
  unchecked until rereview passes.

#### Task 8 repair round 2/5 (2026-08-12)

- Rereview of `0a180e39..c8a2fdf8` returned Spec FAIL / Quality needs fixes,
  C0/I1/M2. Both Saturn runtime findings were closed. The remaining Important
  issue was generation-set atomicity: a divergent late payload manifest or ABI
  could fail after an earlier root/header had been linked. Minor evidence
  corrections are source `init_from` stack 96 B, not 92, and that the tested
  repository-wrapper route is DLL-healthy without claiming every direct/manual
  MSYS invocation or the unrun full link.
- RED covers a divergent target at every one of seven final-output positions,
  the header-before-divergent-ABI case, ownership rollback at every link
  position, and mixed concurrent producers. GREEN computes the full set first,
  preflights every target, privately stages all bytes, publishes report/ready
  last, and rolls back only links whose exact file identity still belongs to
  the failed transaction. Identical concurrent producers converge; divergent
  contenders cannot leave a mixed generation.
- Package schema is 22/22; determinism remains 3/3. The full focused
  actor/package/scene Make wave passes through the exact repository MSYS
  wrapper, and its generation-set inventory remains hash-and-mtime identical
  7/7. Exact GCC 14.3 evidence is init 220 B, init-from 96 B, resolve 44 B and
  an ELF32 big-endian SuperH object. Repair commit is `3b81456b`; evidence
  status and rereview remain open;
  Step 5 and Task 9 stay unchecked.

---

### Task 9: Cut the production generic actor queue and emitter over to v2

**Files:**
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_demo_render.c`
- Modify: `src/port/saturn/gfx/saturn_actor_instance.c`
- Modify: `tools/saturn/actor_variant_bank.py`, `actor_material_v2.py`, and
  their exact BOB source/closure tests
- Modify: existing actor handoff/render host fixtures
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`, Task 16 plan/ledger, this plan/ledger

**Interfaces:**
- Consumes: `sm64_saturn_source_scene_bundle_resolve`, Task 16 generalized meshlet workspace, queue/batch ABI, Task 6 material binder, Task 7 current texture mapping.
- Produces feature-on ACTOR_ADMIT/ACTOR_LOWER work that drains real descriptors; master merge resolves each output record to its validated bank primitive/material/tile and emits painter-ordered commands.

- [ ] **Step 1: Write RED complete-BOB and production-shaped host tests**

Replay all 34 drawable BOB selections and require a valid generic bank/result for
each. `GEO_SHADOW` is one common telemetry-visible shadow-omission policy while
its child geometry still compiles; static `GEO_SCALE` is baked into the common
bank transform; the exact `geo_update_layer_transparency` `GEO_ASM` consumes the
already-captured source opacity rather than adding an object-specific path.
Every newly exposed texture/material signature remains closure-attested and
uses the same v2 lowering. No new wire format or per-object renderer branch is
allowed.

Exercise observer/registry capture through handoff population, either-SH-2 lane claim, pose/meshlet admission, descriptor-owned output, terminal publication, master merge/material bind, acknowledge, and queue-owned retirement. Before descriptor publication, require checked atomic reservation for the complete observed set against 64 live, the post-reservation actor output share, texture-command share, and Gouraud share. Exact fit succeeds; every one-credit overflow quarantines the whole actor generation with zero descriptor/output/VDP1 mutation. Require zero actors to retain the two-world-job graph and a stale texture/package generation to quarantine before command mutation. Assert feature-on wrappers no longer contain unconditional `return false`.

- [ ] **Step 2: Run RED**

Run the 34-key compiler replay plus actor runtime handoff, instance queue,
batches, render overlap, demo render, material, and feature-off wrapper gates.
Expected: the 20 current unsupported rows fail the complete-BOB gate and the
feature-on ACTOR jobs still fail closed at the compat wrappers.

- [ ] **Step 3: Replace only the auditable feature-on seam**

First close the three common Geo envelopes and the exact newly exposed material
signatures so the canonical BOB report has 34 supported drawable rows and zero
unsupported drawable rows. Then, after computing the actual Mario obligations,
dry-sum all selected variants' validated S64B credit fields in canonical snapshot order, then populate the real queue only if the complete essential actor set fits the command/Gouraud frame-bank remainder and dedicated actor output arena. Terrain remains optional and receives only the credits left after Mario plus generic actors. Replace the feature-on compat bodies with queue drain calls; leave the four-job graph/dependencies unchanged; master merge uses current bundle/residency generations and material binder; all retirement flows through the handoff. No partial actor subset is published on overflow. Feature-off retains the exact Mario path.

- [ ] **Step 4: Run GREEN, capacity, and rollback proof**

Run the full actor/render wave. Accept exact per-resource fits, including 64 lightweight actors when their complete credit sums fit; reject 0 capacity, 65 live, 2,719 records, one-credit command/Gouraud overflow, overlap, stale generations, wrong hashes, and mid-merge failure. Prove overflow leaves no partial descriptor/output/command publication. Build feature-off and compare affected object code or the established exact source-policy proof with pre-task HEAD.

- [ ] **Step 5: Commit and independent review**

Commit `feat(saturn): render textured generic actors in production`. Require two-stage review of dual-SH-2 ownership, painter order, retirement, and feature-off identity.

---

### Task 10: Wire the minimum deterministic target and run the first live smoke

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

- [ ] **Step 3: Run the affected host/target gate wave**

Run all affected actor family/variant/pose/meshlet/queue/batch/handoff/render,
scene package/residency/stream, texture/VDP1 transfer, Make, and feature-off
gates. Defer unrelated exhaustive proof waves until the first live smoke.

- [ ] **Step 4: Build and inspect the development target**

Run:

```powershell
python tools/saturn/build_sourceboot_variant.py --label actor-bank-v2-bob --animation 1 --actors 1 --audio 0 --pipeline 4 --diagnostic-mode none --jobs 1 --output build/saturn/variants/actor-bank-v2-bob
```

Require link-time safety only: positive HWRAM/LWRAM/cart bounds, exact 65,536-byte actor arena and 2,718 records, one two-lane workspace, no duplicate S64B/S64F, and correct package/bank/residency generation telemetry. Immediately run a short identity-bound Ymir smoke. It must advance beyond the historical two-tick stall with successful ACTOR jobs, zero exception/allocation failure, and no stale package/texture generation. Stop and diagnose before any exhaustive capacity or release work if it fails.

- [ ] **Step 5: Commit and independent review**

Commit `build(saturn): wire generic BOB actor path`. Review the exact map,
generated inventory, and short-smoke evidence before the full visual gate.

---

### Task 11: Prove the normal generic BOB scene in Ymir

**Files:**
- Modify only if a diagnosed harness defect exists: existing Ymir capture/probe tests and helper
- Create: `docs/saturn/evidence/reports/bob-generic-actors-2026-08-14.json`
- Create: `docs/saturn/evidence/screenshots/bob-generic-actors-2026-08-14.png`
- Modify: `STATE.md`, `ROADMAP.md`, plans/ledgers/reports

**Interfaces:**
- Consumes the exact Task 10 development ELF/ISO/CUE and established canonical Ymir BIOS/profile.
- Produces identity-bound telemetry and visual evidence for the normal BOB actor
  set through one generic package/queue/material/render path.

- [ ] **Step 1: Verify the candidate before launch**

Rehash ELF/ISO/CUE/S64P/S64F, validate package/bundle/all 34 drawable banks,
confirm zero unsupported drawable rows and representative registry/bank/material
identities, and record exact BIOS/profile hashes. Stop if any binding differs.

- [ ] **Step 2: Run the exact headless smoke/capture route**

Launch the established normal BOB route with dynamic actor closure on. Require
advancing source/presentation cadence, cart copy complete, package/bank/texture
generation exact, exception zero, allocation failures zero, actor quarantine
zero, ACTOR_ADMIT/LOWER terminal success, and positive live margins. Telemetry
must prove normally spawned coin, enemy, sign/cannon, and moving-actor identities
use the same generic resolver and emitter.

- [ ] **Step 3: Capture and visually inspect the actor**

Save JSON plus PNG/video. Mario, terrain, HUD, and representative normally
spawned BOB actors must be recognizable; a forced camera may frame them, but
object spawning/model selection may not be injected. Inspect the image directly
and record telemetry-visible generic Saturn fidelity reductions.

- [ ] **Step 4: Run transition/stale-generation mutations**

Repeat the host transition fixture and one emulator reload/scene-transition route. Require old leases to retire, new texture generation to publish last, and stale descriptor/material mapping to quarantine without command mutation.

- [ ] **Step 5: Commit evidence status and obtain review**

Commit `docs(saturn): prove working generic BOB`. Independent evidence review
must verify artifact hashes, telemetry, visual identities, common-path use, and
normal-spawn claims before release rebuilding.

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
- [x] Texture, CLUT, cart, LWRAM, and HWRAM residency budgets are separate and scene-aggregate; output, command, and Gouraud live demand use atomic complete-frame credit admission rather than summing mutually incompatible source-pool ceilings.
- [x] Master/worker responsibilities are consistent in compiler, residency, queue, and emitter tasks.
- [x] Cannon demo evidence cannot be satisfied by a synthetic object or injected model.
- [x] Unknown material/Geo states remain named offline failures.
- [x] Release rebuild/reseal and Task 10 gates follow target-byte changes.
- [x] Every behavior task includes RED, GREEN, exact commit scope, CHANGELOG, ledger, and two-stage review.
- [x] No placeholder language, unchecked invented dependency, or unowned generated source path remains.

## Execution Order and Stop Rules

Execute Tasks 1-13 serially. A task may not begin until the prior task's behavior commit and both independent reviews pass. Stop immediately on a format/interface contradiction, inability to compile the exact Cannon key, nonpositive all-resident or individual-bank margin, nonpositive guaranteed service floor, host/target parser disagreement, dirty/unbound source input, target crash/stall/quarantine on the accepted route, nonidentical A/B release, failed v4, or failed smoke/visual/manual gate. The unconstrained source-ceiling diagnostic is not a simultaneous-scene acceptance margin. Record every failure without weakening flags, budgets, source identity, or acceptance.

Task 4 of `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md` resumes only through Tasks 1-5 here. Its downstream Tasks 5-11 and Task 16 Tasks 2-5 are satisfied/reconciled through Tasks 7-13 here; do not run the stale v1-only Task 4 instructions in parallel.

## Task 7 active transition (2026-08-12)

- Reconciled plan, ledger, and HEAD `ce28a7d9`; Task 7 is active for fixed
  all-resident actor texture/CLUT planning, checked master-owned DMA queue
  transfer, scalar generation-last publication, and scene-residency ownership
  only. Task 8/runtime/renderer/Ymir/release gates remain closed.
- File-map correction authorized: the existing residency implementation lives
  in `saturn_ir_texture.c`, so Task 7 may add `init_region` there and make the
  old partition initializer delegate without changing Task 6 binders.
- DMA correction authorized: pinned libyaul's raw SCU calls are void. Actor
  activation uses the already-owned in-tree GPL-3.0-or-later
  `saturn_dma_queue_submit(..., SATURN_DMA_QUEUE_SCU)` plus checked
  `saturn_dma_queue_wait`; no queue source changes and no activation-time queue
  initialization are permitted. Complete planning precedes the first submit;
  a later failure leaves possibly dirty VRAM bytes unreachable and publishes
  no generation.
- Reference inspection pinned libyaul gitlink `6012f79f` (MIT), its VDP1 VRAM
  partition and SCU-DMA declarations, the in-tree SlaveDriver-derived checked
  queue (`a898659...`, GPL-3.0-or-later), and current actor bundle/bank,
  material, texture residency, scene residency, frame-bank, and Gouraud
  generation/fence patterns. Reuse is dependency/API use plus same-repository
  close-port/shared-core extension.
- TDD gate is open: the actor/scene residency RED fixtures must fail at the
  absent API before production edits. No Task 7 GREEN, SH-2 compile, behavior
  commit, review, or target evidence is claimed yet.

## Task 7 source-complete transition (2026-08-12)

- Status is source-complete-pending-review at behavior commit `7cbd06ed`. The
  implementation owns a fixed
  128-entry / 2,064-byte scalar publication in scene residency, keeps the
  public scene header Yaul-free, and retains Task 6's exact 16-byte mapping
  layout by moving only its typedef into the new lightweight header. Failed
  staging, a scene commit without the matching actor generation, reset, and
  matching inactive unload invalidate the publication; matching commit keeps
  it. Existing scene begin/commit/unload return semantics are unchanged.
- Two more authorized corrections refine the active design: residency
  generation is independent from immutable bundle package generation (Task 8
  will carry both), and the S64F validator's unique nonzero scalar-bank-ID rule
  means residency performs no deduplication. Thus mapping count is exactly the
  number of canonical selected v2 variants; the real BOB bundle publishes 14.
- Complete planning revalidates S64F and every S64B before DMA, checks each
  texture/CLUT source and destination through its inclusive last byte, forms
  pointers only after checked `uintptr_t` addition, and accepts a legal span
  ending at `UINTPTR_MAX`. Upload uses only checked queue submit/wait. Any
  post-transfer failure may dirty bytes but leaves generation/committed zero.
- Focused residency/scene/IR/material GREEN, real-Bundle 14/16,640/2,816/Cannon
  assertions, historical Python bank/bundle 26/26, S64B 86 mutations, S64F 54
  mutations, family 47/13/14, VDP1 frame-bank, DMA queue, Gouraud, pose,
  meshlet, feature-off 6/6, variant/source 40/40, and exact SH-2 freestanding
  syntax/object compilation pass. The untouched adjacent A8 deferred-transfer
  source contract remains 5/7 at its pre-existing destination-poison and
  VDP2-camera/bank requirements; Task 7 does not edit sourceboot.
- Step 5 remains unchecked until the behavior commit is recorded and fresh
  independent spec/quality review passes. Task 8/runtime activation/renderer,
  target link/run, Ymir, release, visual/manual, and total-game gates remain
  closed.

## Task 7 review and fix round 1/5 active (2026-08-12)

- Review of frozen `97dae9b2` returned C0/I4/M1. Task 7 is
  source-complete-pending-fix-and-rereview; Task 8 remains closed.
- Approved repair corrections: staging access may expose only canonical empty
  state or the exact staging generation; one central bounded publication
  validator gates both activation prestate and lookup; activation accepts a
  caller-owned HWRAM staging span and preflights every queue request before the
  first DMA; the full S64F remains in CART and each cold span is CPU-copied to
  stage then sent to VDP1 sequentially. The checked queue exposes only its
  existing private request validation as a read-only API and retains all
  scheduling semantics.
- Real BOB requires at most 2,560 staged bytes. This is measured evidence, not
  a hardcoded universal capacity or a target HWRAM-margin claim. Task 8 owns
  the fixed staging buffer and must carry it through the planned activation
  call; later link/budget gates prove its placement.
- The repair also makes `verify-actor-texture-residency` build/verify the real
  Task 5 artifact and replaces signed-cast generation comparison with explicit
  nonzero unsigned half-range serial arithmetic. Focused RED/GREEN is complete:
  exact/P2 HWRAM top fits, one-byte overflow/CART/LWRAM and overlap with bundle,
  publication, or VDP1 ownership reject before DMA; the fresh real gate rebuilt
  and C-validated 47 families / 14 variants and passed all 13 publication and
  inventory tests in 209.3 seconds. The isolated output was safely removed.
  Repair behavior is committed as `4c4c24a9`. Rereview then found one hidden
  2,076-byte SH-2 stack frame in the scene empty-publication check; focused
  RED `-fstack-usage` captured it and the follow-up scans the owned fields in
  place. Rereview then found the stage classifier accepted P1/P3/P4 cache-
  control aliases after physical masking. The follow-up permits only the P0
  cached and P2 cache-through shapes before HWRAM range validation; hostile
  `0x460`, `0x660`, and `0xC60` shapes reject.

## Task 7 approval and Task 8 active transition (2026-08-12)

- Same-reviewer final rereview of `97dae9b2..d1408e00` passed Spec PASS,
  Quality PASS, C0/I0/M0. It independently repeated the absent-output real
  47-family/14-variant bundle, residency/material/queue/scene gates, and exact
  SH-2 compiles. Actor activation stack is 476 bytes and the scene staging
  accessor is 0 bytes.
- Original C0/I4/M1 plus the publication-stack and cache-control-alias findings
  are closed. P0/P2 upload shapes alone are valid; hostile `0x460`, `0x660`,
  and `0xC60` shapes reject before copy/DMA. Task 7 Step 5 is complete.
- Task 8 opened only for the minimum canonical scene-bundle owner. Its required
  acceptance was to eliminate the measured 3,920/3,420-byte scene-validation
  frames to <=256 bytes per call, alias the CART package, bind a 2,560-byte
  cold-stage lifetime and 1,280-byte LWRAM workspace, and avoid a redundant
  streamer. The source-complete transition above records the resulting
  phase-borrowed command-bank stage. Runtime cutover, renderer, target link/run,
  and Ymir remain open.
