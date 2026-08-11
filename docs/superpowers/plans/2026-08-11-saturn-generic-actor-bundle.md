# Saturn Generic Actor Bundle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Materialize every supported scene-selected actor variant as a validated S64B inside a scene-local S64F v3 bundle, load that bundle into fixed Saturn DRAM-cart residency, and expose generation-safe scalar lookup plus two fixed SH-2 workspace lanes so Task 16 production queue wiring can resume without a demo-only fallback.

**Architecture:** The host emits one canonical S64F v3 dependency per scene package from source closure and per-variant S64B banks. The master SH-2 loads it from CD into the fixed post-`SOURCE.DAT` cart span, validates the complete package/bundle/bank chain, binds a generated-capacity two-lane LWRAM workspace, and publishes scalar generation state last. Workers resolve `(family ordinal, model ID, bank ID, source hash, package generation)` through bounded tables; S64F v2 remains historical tooling only.

**Tech Stack:** Python 3 deterministic asset compilers and `unittest`; freestanding C11 for SH-2 runtime/host fixtures; GNU Make 4.3+ through `tools/saturn/with-msys-toolchain.ps1`; existing S64P/S64B formats, scene residency, DRAM-cart/CDFS loader, P2/TAS.B ownership, and hermetic release tooling.

**Execution status (2026-08-11):** active under the owner-selected
subagent-driven workflow. Task 1 is complete and independently approved. Task
2 is source-complete at `b1133026`; its independent task review remains the
gate before Task 3. Target, release, smoke, visual, desktop, manual, and
total-game gates remain open.

## Global Constraints

- Follow `docs/superpowers/specs/2026-08-11-saturn-generic-actor-bundle-design.md` exactly; field sizes are S64F header 96, family record 64, variant record 88.
- S64F v3 tables accept at most 64 families and 128 drawable variants per scene; overflow is a build failure, never truncation or dynamic growth.
- Serialized integers are big-endian; offsets are root-relative uint32 values; all regions are four-byte aligned; padding and reserved fields are zero.
- S64F v2 historical parsing/tests remain byte-stable. Feature-on production accepts only S64F v3.
- One S64F v3 `ACTOR_DEPENDENCIES` payload per scene uses destination `CART`, alignment 4, nonzero artifact generation, and S64P `maximum_scratch = 0`.
- Immutable actor data stays in the fixed 32-Mbit DRAM-cart residency region. No global all-game actor blob, serialized pointer, heap allocation, per-family HWRAM cache, or per-instance whole-bank validation is allowed.
- One generated-capacity LWRAM workspace contains two SH-2 claimant lanes. Production lane bases use the bundle-wide stride; the reviewed single-bank binder remains ABI-compatible.
- Cross-SH-2 records contain scalar IDs, hashes, offsets, counts, lane index, and generations only. The master publishes the committed generation last.
- Scene I/O is master-owned, bounded to at most 16 CD sectors per loader step, and legal only while gameplay is suspended on the loading transition.
- Old scene bytes are not reclaimed until render, actor-bank, VDP1-bank, and audio leases retire. A failed post-reclaim load publishes no active generation.
- Unsupported actors remain explicitly absent and quarantine by name. Mario, the first record, a stale generation, and a partial bank are never fallbacks.
- Every behavior change updates `CHANGELOG.md` in the same commit. Every task updates this plan and `.superpowers/sdd/2026-08-07-task16-completion/progress.md`, records exact tests/review/open gates, and uses explicit-path staging.
- Reference-code-first baseline is this repository at design commit `0e87f7ac` / status `8a81d7ec`: reuse `saturn_scene_package`, `saturn_scene_residency`, `source_cart`, `saturn_actor_bank`, `dl_rigid_groups`, `saturn_mesh_ir`, and Task 14 registry patterns directly or by close-port. No external source is copied.

## File and responsibility map

- `src/port/saturn/runtime/saturn_sha256.h/.c`: one freestanding incremental SHA-256 primitive shared by package and actor validators.
- `tools/saturn/actor_family_bundle.py`: authoritative S64F v3 constants, dataclasses, canonical pack/parse/validate, and source-identity derivation.
- `src/port/saturn/gfx/saturn_actor_bundle.h/.c`: target S64F v3 parser, complete validation, bounded record access, and cheap per-variant S64B resolution.
- `tools/saturn/actor_variant_bank.py`: generic source-closure-to-S64B compiler for rigid, articulated, switch, billboard, alpha/translucent, and typed source-selected variants.
- `tools/saturn/compile_actor_family_bundle.py`: scene bundle orchestration, deterministic report, embedded-bank packing, and S64P dependency metadata.
- `tools/saturn/inventory_actor_family_bundles.py`: all-scene 64/128 proof and generated LWRAM/root/catalog capacity headers.
- `tools/saturn/gen_actor_identity_registry.py`: behavior/model lookup generated from per-variant S64B source identities in the validated v3 bundle.
- `src/port/saturn/gfx/saturn_actor_bundle_runtime.h/.c`: scalar generation-last bundle descriptor and two-lane claim/release/resolve boundary.
- `src/port/saturn/runtime/saturn_scene_stream.h/.c`: platform-neutral drain/reclaim/load/validate/commit state machine with bounded read callback.
- `src/port/saturn/sourceboot/source_scene_bundle.h/.c`: CDFS/cart adapter, generated scene catalog binding, fixed LWRAM root/workspace storage, and target telemetry.
- `Makefile.saturn.mk` and `src/port/saturn/sourceboot/Makefile`: deterministic generation, host gates, source closure, CD-image inclusion, and target link wiring.

---

### Task 1: Extract the shared freestanding SHA-256 primitive

**Files:**
- Create: `src/port/saturn/runtime/saturn_sha256.h`
- Create: `src/port/saturn/runtime/saturn_sha256.c`
- Create: `tools/saturn/saturn_sha256_test.c`
- Modify: `src/port/saturn/runtime/saturn_scene_package.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bank.c`
- Modify: `Makefile.saturn.mk`
- Modify: `src/port/saturn/sourceboot/Makefile` (source-list correction: the
  shared object must link with both validators)
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md`
- Modify: `.superpowers/sdd/2026-08-07-task16-completion/progress.md`

**Interfaces:**
- Consumes: the existing private SHA-256 implementations in `saturn_scene_package.c:6-118` and `saturn_actor_bank.c:534-657`.
- Produces:

```c
typedef struct sm64_saturn_sha256 {
    uint32_t state[8];
    uint64_t total_bytes;
    uint8_t block[64];
    uint32_t used;
} sm64_saturn_sha256_t;

void sm64_saturn_sha256_init(sm64_saturn_sha256_t *state);
bool sm64_saturn_sha256_update(sm64_saturn_sha256_t *state,
                               const void *bytes, uint32_t byte_count);
bool sm64_saturn_sha256_finish(sm64_saturn_sha256_t *state,
                               uint8_t digest[32]);
bool sm64_saturn_sha256_digest(const void *bytes, uint32_t byte_count,
                               uint8_t digest[32]);
```

- [x] **Step 1: Write the RED vector and regression tests**

Add `saturn_sha256_test.c` with empty, `"abc"`, a 64-byte boundary, segmented updates, a null/nonzero rejection, and a `UINT32_MAX` accounting mutation. Extend package/family-bank tests to assert their current hashes and historical v2 bytes remain exact after the refactor.

```c
assert(sm64_saturn_sha256_digest("abc", 3U, digest));
assert(memcmp(digest, abc_sha256, 32U) == 0);
sm64_saturn_sha256_init(&state);
assert(sm64_saturn_sha256_update(&state, "a", 1U));
assert(sm64_saturn_sha256_update(&state, "bc", 2U));
assert(sm64_saturn_sha256_finish(&state, segmented));
assert(memcmp(digest, segmented, 32U) == 0);
```

- [x] **Step 2: Run RED**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-saturn-sha256
```

Expected: compile failure because `saturn_sha256.h` and its functions do not exist.

- [x] **Step 3: Close-port the existing implementation**

Move the reviewed block/update/final logic into `saturn_sha256.c`, make null/overflow failures explicit, replace both private copies with the public interface, and preserve package hash zeroing as segmented updates rather than allocating a payload copy.

```c
sm64_saturn_sha256_init(&sha);
if (!sm64_saturn_sha256_update(&sha, bytes, hash_offset) ||
    !sm64_saturn_sha256_update(&sha, zero_digest, 32U) ||
    !sm64_saturn_sha256_update(&sha, bytes + hash_offset + 32U,
                               byte_count - hash_offset - 32U) ||
    !sm64_saturn_sha256_finish(&sha, digest))
    return false;
```

- [x] **Step 4: Run GREEN and regressions**

Run `verify-saturn-sha256 verify-scene-package-runtime verify-actor-family-bank`. Expected: all pass and v2 payload/hash fixtures remain unchanged.

- [x] **Step 5: Update docs, commit, and request review**

Add a Keep-a-Changelog `Changed` entry explaining removal of duplicate target SHA code without changing bytes. Stage only the files above and commit `refactor(saturn): share target SHA-256 validation`. Independent review must pass before Task 2.

---

#### Task 1 live status (2026-08-11)

- Status: `complete; independent review PASS C0/I0/M0` at behavior commit
  `1faa2ffb` (`refactor(saturn): share target SHA-256 validation`). The close-port creates
  one freestanding incremental API consumed by the existing S64P and S64F v2
  validators; no S64F v3 behavior, target completion, or release claim is
  introduced. Evidence/report documentation is commit `eba1e1ba`
  (`docs(saturn): record shared SHA-256 task evidence`).
- TDD: the new public-header/vector fixture first failed exactly because
  `saturn_sha256.h` and `saturn_sha256.c` did not exist. GREEN passes empty,
  `"abc"`, 64-byte, segmented, null/nonzero, and `UINT32_MAX` accounting
  overflow coverage, plus byte-pinned S64P fixture and historical S64F v2
  payload hashes.
- Design correction: the initial file list omitted
  `src/port/saturn/sourceboot/Makefile`; it now adds the shared source to
  `SH_SRCS` ahead of both consumer objects. A focused source-list assertion
  passes. A Yaul dry-run reaches the pre-link identity bootstrap but cannot
  expand the link command because the unrelated generated source-closure input
  is absent; this is not target evidence.
- Reference reuse: same repository, base
  `0baac1a225d512f0f7eb95c36f2766fcef723c15`, close-port/shared-core reuse of
  `src/port/saturn/runtime/saturn_scene_package.c:6-118` and
  `src/port/saturn/gfx/saturn_actor_bank.c:534-657`; no external code used.
- Evidence: the required MSYS wrapper invocation with native forward-slash
  `SATURN_REPO_ROOT` passes `verify-saturn-sha256`,
  `verify-scene-package-runtime`, and `verify-actor-family-bank` (47 families,
  13 unsupported representatives across 14 records). Independent task review
  approved the interface, byte compatibility, tests, and source-list binding
  with no findings. The real target link remains unclaimed because its dry-run
  stopped at the pre-existing absent generated source-closure input. All
  target/P2/Ymir/manual/reseal/smoke gates and Tasks 2-11 remain open.

### Task 2: Implement canonical S64F v3 host and target validation

**Files:**
- Create: `tools/saturn/actor_family_bundle.py`
- Create: `tools/saturn/test_actor_family_bundle.py`
- Create: `src/port/saturn/gfx/saturn_actor_bundle.h`
- Create: `src/port/saturn/gfx/saturn_actor_bundle.c`
- Create: `tools/saturn/actor_family_bundle_test.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bank.h`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`
- Modify: plan and Task 16 ledger

**Interfaces:**
- Consumes: `sm64_saturn_sha256_*`, `sm64_saturn_actor_bank_validate_expected`, and exact design constants.
- Produces Python:

```python
S64F_V3_HEADER = struct.Struct(">4s6H12I32s")
S64F_V3_FAMILY = struct.Struct(">14I2HI")
S64F_V3_VARIANT = struct.Struct(">2H5I32s32s")
```

The public host callables are `source_identity(family_ordinal: int,
model_id: int, sources: Sequence[SourceRecord]) -> bytes`,
`pack_bundle(document: BundleDocument, bank_payloads:
Mapping[VariantKey, bytes]) -> bytes`, and `validate_bundle(payload: bytes) ->
BundleView`. They raise `ValueError` on malformed or noncanonical input and do
not publish a partial output.

- Produces C:

```c
typedef struct sm64_saturn_actor_bundle_view {
    const uint8_t *bytes;
    uint32_t byte_count, family_records_offset, variant_records_offset;
    uint32_t metadata_offset, metadata_size, bank_payloads_offset;
    uint32_t bank_payloads_size, workspace_lane_stride, maximum_scratch;
    uint32_t package_generation, content_hash_words[8];
    uint16_t family_count, variant_count;
} sm64_saturn_actor_bundle_view_t;

typedef struct sm64_saturn_actor_bundle_variant {
    uint16_t family_ordinal, model_id;
    uint32_t bank_offset, bank_size, bank_lane_bytes, bank_maximum_scratch;
    uint32_t payload_hash_words[8], source_hash_words[8];
} sm64_saturn_actor_bundle_variant_t;

bool sm64_saturn_actor_bundle_validate(const void *bytes, uint32_t byte_count,
                                       sm64_saturn_actor_bundle_view_t *out);
bool sm64_saturn_actor_bundle_variant(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      sm64_saturn_actor_bundle_variant_t *out);
bool sm64_saturn_actor_bundle_resolve(const sm64_saturn_actor_bundle_view_t *view,
                                      uint16_t family_ordinal, uint16_t model_id,
                                      uint32_t actor_bank_id,
                                      const uint32_t source_hash_words[8],
                                      sm64_saturn_actor_bank_view_t *out);
```

- [x] **Step 1: Write Python RED tests**

Cover exact 96/64/88 sizes, deterministic bytes, source-identity canonicalization, zero padding, 64/128 acceptance, 65/129 rejection, ordering, duplicate keys, zero/colliding bank IDs, every span overflow, hash mutation, S64B family/model/source/scratch mismatch, and v2 rejection by the v3 API.

```python
self.assertEqual(S64F_V3_HEADER.size, 96)
self.assertEqual(S64F_V3_FAMILY.size, 64)
self.assertEqual(S64F_V3_VARIANT.size, 88)
with self.assertRaisesRegex(ValueError, "variant limit"):
    pack_bundle(document_with_variants(129), banks)
```

- [x] **Step 2: Write C RED tests and Make target**

The C fixture receives one generated valid v3 file, validates all banks once, resolves an exact variant, and applies one mutation for each serialized field class. Assert output views remain zeroed on failure.

- [x] **Step 3: Run RED**

Run `python -m unittest tools.saturn.test_actor_family_bundle -v` and `verify-actor-family-bundle`. Expected: missing module/header failures.

- [x] **Step 4: Implement minimal canonical writer and both validators**

Use overflow-safe add/multiply/alignment helpers. Target validation must hash the bundle with bytes 64..95 treated as zero, validate every embedded S64B during the one master pass, and make `resolve` perform only binary search plus scalar/span checks.

- [x] **Step 5: Run GREEN plus historical gates**

Run Python tests, `verify-actor-family-bundle verify-actor-family-bank verify-actor-pose-bank verify-scene-package-runtime`. Expected: all pass, including unchanged S64F v2 behavior.

- [ ] **Step 6: Update docs, commit, and review**

CHANGELOG states v3 is additive and feature-on selection is not wired yet. Commit `feat(saturn): define generic actor family bundle v3`; independent spec/code review before Task 3.

---

#### Task 2 live status (2026-08-11)

- Status: `source-complete; independent review pending` at behavior commit
  `b1133026` (`feat(saturn): define generic actor family bundle v3`). Task 3
  remains blocked on the controller-owned task review. No sourceboot selection,
  target build, P2, Ymir, release, smoke, visual, desktop, or manual claim is
  made.
- TDD: the Python RED failed with `ModuleNotFoundError: No module named
  'actor_family_bundle'`; the C RED failed on absent
  `saturn_actor_bundle.h/.c`. GREEN passes seven Python cases and the
  freestanding target fixture's 53 resealed field/identity/span mutations.
  Python covers 46 malformed-input assertions, for 99 mutation/rejection
  assertions across both boundaries.
- Verification: the required native-forward-slash-root Make wave passes
  `verify-actor-family-bundle verify-actor-family-bank
  verify-actor-pose-bank verify-scene-package-runtime`. This preserves the
  historical S64F-v2 47-family fixture (13 unsupported representatives across
  14 closure records), the Mario S64B pose fixture, and S64P runtime behavior.
- Design decision: v3 canonicalizes metadata by first appearance with exact
  byte aliases, packs sorted non-aliased variant banks at four-byte boundaries,
  and requires exact `3 + 2 * lane` scratch. Master validation fully validates
  every S64B before publishing a view; resolve is binary search plus immutable
  scalar/span reconstruction only. Failure zeros every C output view.
- Reference reuse: same-repository direct use/close-port from base
  `e7b1c2bcb04705e5454684cea4487b3ff9c8692f`, specifically
  `src/port/saturn/gfx/saturn_actor_bank.h/.c`,
  `src/port/saturn/runtime/saturn_scene_package.c`, and
  `tools/saturn/compile_actor_bank.py`. No external code was used and no new
  license/notice obligation was introduced; the repository has no root
  license file.
- Open: independent review, Tasks 3-11, real BOB/all-scene inventory, target
  link/map/capacity, heterogeneous dual-SH-2 lanes, sourceboot production
  selection, feature-off identity, transition, Task 9 reseal/repro/v4/staging,
  and Task 10 smoke/visual/desktop/manual gates.

---

### Task 3: Compile deterministic generic S64B variant banks

**Files:**
- Create: `tools/saturn/actor_variant_bank.py`
- Create: `tools/saturn/test_actor_variant_bank.py`
- Modify: `tools/saturn/compile_actor_bank.py`
- Modify: `tools/saturn/actor_source.py`
- Modify: `tools/saturn/dl_rigid_groups.py` only if a real source construct requires an already-modeled token to expose data
- Modify: `tools/saturn/test_actor_source.py`
- Modify: `tools/saturn/test_tools.py`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: closure `root_provenance`, `dl_rigid_groups.walk_geo_layout`, `extract_mario_actor` block/vertex/Fast3D parsing, `saturn_mesh_ir.compile_mesh_ir`, and S64B packing/validation.
- Produces:

```python
@dataclass(frozen=True)
class CompiledActorVariant:
    family_ordinal: int
    model_id: int
    source_sha256: str
    payload_sha256: str
    lane_bytes: int
    maximum_scratch: int
    sources: tuple[SourceRecord, ...]
    payload: bytes
    report: dict[str, object]

```

The compiler entry point is `compile_actor_variant(root: Path,
family_ordinal: int, model_id: int, records: Sequence[dict[str, object]]) ->
CompiledActorVariant`. It either returns a fully validated artifact/report or
raises a named unsupported/malformed-source exception without writing output.

- [ ] **Step 1: RED with synthetic rigid and articulated fixtures**

The rigid fixture has one GeoLayout, nested display list, one material, and no animation table; require one root joint and one neutral one-frame animation. The articulated fixture has two `GEO_ANIMATED_PART` joints and a real compact animation source; require joint-local vertices and exact animation samples. Add separate switch, billboard, alpha, and translucent fixtures whose typed selection remains metadata while their variant geometry is exact.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_actor_variant_bank -v`. Expected: import failure for `actor_variant_bank`.

- [ ] **Step 3: Expose one shared S64B encoder**

Refactor `compile_actor_bank.py` so Mario and generic variants call:

The shared encoder entry point is `pack_actor_bank(*, family_id: int,
model_id: int, max_instances: int, source_digest: bytes, joints:
Sequence[Joint], animations: Sequence[AnimationRecord], vertices:
Sequence[Vertex], geometry: Geometry) -> tuple[bytes, dict[str, object]]`.

Run the existing Mario bank tests immediately; payload bytes and legacy hashes must remain exact.

- [ ] **Step 4: Implement source-selected generic extraction**

Resolve the exact GeoLayout/model/animation sources from closure provenance; walk reached lists in source order; preserve Fast3D vertex-cache and material state; map each rigid group to one joint; emit joint-local vertices; compile primitives through Mesh IR; and parse only closure-selected animation tables. Unknown nodes, lists, vertices, animation bindings, or source-hash drift raise a named error rather than dropping geometry.

- [ ] **Step 5: Run GREEN and mutation gates**

Run variant tests, `verify-actor-pose-bank verify-actor-meshlets`, and existing actor-source/rigid-group tests. Mutate a source byte, list target, joint owner, switch variant, animation span, and material layer; each must either change the exact identity/output or fail closed.

- [ ] **Step 6: Commit and review**

CHANGELOG explains the new generic compiler and strict unsupported boundary. Commit `feat(saturn): compile source-selected actor variant banks`; independent review before Task 4.

---

### Task 4: Emit the real BOB S64F v3 bundle

**Files:**
- Create: `tools/saturn/compile_actor_family_bundle.py`
- Create: `tools/saturn/test_compile_actor_family_bundle.py`
- Modify: `tools/saturn/compile_actor_bank.py` CLI only to keep v2 historical mode explicit
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: validated closure, current S64F v2 capability report for preserved metadata ordering, `compile_actor_variant`, and `pack_bundle`.
- Produces report schema `sm64-saturn-actor-family-bundle-v3` with `payload`, `payload_size`, `payload_sha256`, `content_sha256`, `package_generation`, counts, lane stride, maximum scratch, complete family/variant records, source list, and one S64P dependency document.

The orchestration entry point is `compile_scene_bundle(root: Path,
closure_path: Path, family_report_path: Path, model_ids_path: Path,
package_generation: int, output_dir: Path) -> dict[str, object]`. It writes to
an output-parent staging directory, verifies the staged payload/report, and
publishes the report last by no-clobber replacement.

- [ ] **Step 1: RED orchestration tests**

Require two relocatable builds of BOB to produce byte-identical bundle/report identities; all currently supported drawable `(family ordinal, model ID)` keys have exactly one bank; unsupported families have zero variants; every source object maps to either one supported variant or one explicit unsupported reason.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_compile_actor_family_bundle -v`. Expected: missing compiler module.

- [ ] **Step 3: Implement bundle orchestration**

Derive family ordinals from canonical S64F ordering, expand each record's model variants, group duplicate source objects by variant key, compile each supported key once, compute global stride, pack v3, validate it again, and publish files atomically with the report last.

```python
dependency = {
    "kind": "ACTOR_DEPENDENCIES", "stable_id": "bob-area1-actors-v3",
    "generation": package_generation, "destination_class": "CART",
    "lifetime": "SCENE", "alignment": 4, "max_scratch": 0,
    "byte_count": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
}
```

- [ ] **Step 4: Wire `compile-actor-family-bundle` and verify target**

Add a new Make target without changing `compile-actor-banks` v2. Add `verify-actor-family-bundle-build` to run Python validation and the C validator against the real BOB artifact.

- [ ] **Step 5: Run GREEN**

Run `compile-actor-banks compile-actor-family-bundle verify-actor-family-bundle-build verify-actor-capability-bank verify-actor-capability-articulated`. Expected: real BOB v3 validates; every compiler-supported row is bound; unsupported rows remain explicit.

- [ ] **Step 6: Commit and review**

Commit `feat(saturn): build BOB generic actor bundle`; review the exact generated report/inventory and code before Task 5.

---

### Task 5: Prove whole-game table bounds and generate fixed capacities

**Files:**
- Create: `tools/saturn/inventory_actor_family_bundles.py`
- Create: `tools/saturn/test_inventory_actor_family_bundles.py`
- Create generated outputs under: `build/saturn/packages/actor-bundle-inventory.json`, `build/saturn/sourceboot/generated/actor_bundle_capacity.h`, `build/saturn/sourceboot/generated/actor_scene_catalog.h`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: repository level/area sources, `collect_scene_closure`, bundle compiler reports, and exact source hashes.
- Produces:

The inventory callables are `discover_scene_areas(root: Path) ->
tuple[SceneKey, ...]` and `inventory_bundles(root: Path, package_generation:
int) -> dict[str, object]`. Both return canonically sorted immutable records;
any duplicate numeric key, missing source, or capacity overflow raises before
output publication.

Generated header contract:

The generated header emits literal decimal integer macros for the fixed limits
64 and 128, the exact discovered scene count, and the root/workspace maxima
rounded upward with `(maximum + 255) & ~255`. Tests parse those literal macros
and compare them with the canonical inventory; symbolic host expressions and
unchecked hand-entered capacities are rejected.

- [ ] **Step 1: RED discovery/capacity tests**

Use a temporary repository with two levels/areas; verify stable ordering, 64/128 acceptance, 65/129 rejection, generated 256-byte rounding, unique ISO names `S%02X%02X.PKG` / `S%02X%02X.ACT`, unsupported counts retained, and no host path in output bytes.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_inventory_actor_family_bundles -v`. Expected: missing module.

- [ ] **Step 3: Implement inventory and atomic header/report publication**

Discover only source-valid `levels/<level>/areas/<positive integer>` pairs, resolve numeric level IDs from the existing level enum/source binding, compile/inventory each scene, reject duplicate numeric keys or ISO names, and derive capacity from the maximum validated report. The report must list every scene, even when capability gaps remain.

- [ ] **Step 4: Run repository inventory**

Run `inventory-actor-family-bundles`. Expected: every discovered scene is present and every count fits 64/128. Capability gaps remain evidence, not table-limit failures. Record exact maximum counts, workspace bytes, and unsupported totals in the ledger.

- [ ] **Step 5: Commit and review**

Commit only generator/tests/Make/docs/CHANGELOG; generated `build/` artifacts remain evidence, not source. Message: `feat(saturn): inventory bounded actor scene bundles`. Independent review confirms no scene omission before Task 6.

---

### Task 6: Regenerate actor identities from per-variant S64B source hashes

**Files:**
- Modify: `tools/saturn/gen_actor_identity_registry.py`
- Modify: `tools/saturn/test_gen_actor_identity_registry.py`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, Task 14 ledger/report, and Task 16 ledger

**Interfaces:**
- Consumes: v3 bundle report, closure, model IDs, and package generation.
- Produces the existing C registry row shape unchanged, but `actor_bank_id` and `actor_bank_hash_words` now come from the exact selected variant's `bank_source_sha256`.

The registry entry point is `build_registry_rows(bundle: BundleView, report:
dict[str, object], closure: dict[str, object], model_ids: dict[str, int],
package_generation: int) -> list[RegistryRow]`. It returns byte-sorted rows
only after the bundle/report/closure/model maps and generation agree exactly.

- [ ] **Step 1: RED identity tests**

Replace the v2 payload-hash expectation with two variants that share a family but have different models/source hashes. Require source objects sharing the same variant to share one bank identity; require outer bundle hash changes not to alter a variant ID when its source set is unchanged; reject zero/colliding leading words, stale report, wrong generation, missing variant, and unsupported row fallback.

- [ ] **Step 2: Run RED**

Run `python -m unittest tools.saturn.test_gen_actor_identity_registry -v`. Expected: current generator emits the outer S64F hash for every row and fails new assertions.

- [ ] **Step 3: Implement v3 input and keep v2 historical mode explicit**

Add required production `--bundle-report`; preserve `--family-report` only under an explicit `--historical-v2` test/tooling flag. Validate the v3 payload internally, cross-check every report record, then map closure behavior/model pairs to the exact variant.

- [ ] **Step 4: Run GREEN and real BOB registry gate**

Run `verify-actor-identity-registry verify-actor-family-bundle-build verify-actor-instance-snapshot verify-render-snapshot-bank`. Expected: generated observer/admission executable proves exact hit fields and zeroed miss fields.

- [ ] **Step 5: Commit and independent review**

Commit `fix(saturn): bind actor registry to variant banks`. Record this as a Task 14 Task 3 identity-contract evolution, not a reopening of its already-approved source lookup seam.

---

### Task 7: Emit the non-provisional S64P scene root and CD catalog inputs

**Files:**
- Modify: `tools/saturn/compile_scene_package.py`
- Modify: `tools/saturn/test_scene_package_schema.py`
- Modify: `tools/saturn/test_scene_package_determinism.py`
- Modify: `tools/saturn/validate_scene_package.py`
- Modify: `Makefile.saturn.mk`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: BOB bundle report's exact `s64p_dependency`, source scene sections, and generated catalog names.
- Produces `build/saturn/packages/bob/1/release/scene.s64p`, its report/validation, and ISO inputs `S0901.PKG` / `S0901.ACT` (using the repository's actual numeric BOB level ID from generation, not a hardcoded unchecked constant).

- [ ] **Step 1: RED dependency-manifest CLI tests**

Add required `--dependency-manifest PATH` parsing. Require exact fields, content rehash, `ACTOR_DEPENDENCIES`, `CART`, alignment 4, max scratch 0, nonzero generation, canonical order, and refusal of provisional output in `compile-scene-package`.

```python
dependency = DependencyInput(
    kind=item["kind"], stable_id=item["stable_id"],
    data=payload_path.read_bytes(), destination_class=item["destination_class"],
    lifetime=item["lifetime"], max_scratch=item["max_scratch"],
    generation=item["generation"], alignment=item["alignment"],
)
```

- [ ] **Step 2: Run RED, implement, and run GREEN**

Run package schema/determinism tests; expect unrecognized CLI first. Implement strict manifest loading and atomic outputs, then rerun `verify-scene-package-schema verify-scene-package-runtime verify-scene-residency`.

- [ ] **Step 3: Add `compile-scene-package` and ISO input targets**

Keep `compile-provisional-scene-package` historical. The new target depends on the v3 bundle and creates a non-provisional root, validates it without `--allow-provisional`, and copies package/bundle to the generated catalog names under the sourceboot CD input directory.

- [ ] **Step 4: Verify exact package binding**

Parse S64P and assert one actor dependency, exact bundle size/hash/generation, CART destination, no scratch, and no embedded duplicate bundle bytes in the root. Re-run after relocating the worktree; bytes must match.

- [ ] **Step 5: Commit and review**

Commit `feat(saturn): package scene-local actor bundles`; review package/ISO inventory before runtime work.

---

### Task 8: Add the bundle-stride runtime and dual-SH-2 lane ownership

**Files:**
- Create: `src/port/saturn/gfx/saturn_actor_bundle_runtime.h`
- Create: `src/port/saturn/gfx/saturn_actor_bundle_runtime.c`
- Create: `tools/saturn/actor_bundle_runtime_test.c`
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.h`
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.c`
- Modify: `tools/saturn/actor_meshlet_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: validated bundle/cart offsets, fixed workspace, actor snapshots, Task 1 workspace layout, and S64B resolver.
- Produces:

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

bool sm64_saturn_actor_bank_workspace_bind_strided(
    const sm64_saturn_actor_bank_view_t *bank, void *workspace,
    uint32_t workspace_capacity, uint32_t lane_stride, uint8_t lane,
    sm64_saturn_actor_meshlet_bank_output_t *output);
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
```

- [ ] **Step 1: RED heterogeneous-lane fixture**

Build two valid banks with different lane sizes. Claim master lane 0 for the smaller bank and slave lane 1 for the larger bank simultaneously; require absolute alignment, global-stride separation, exact transient views, no pointer in the publication struct, generation-last visibility, and no output mutation on short capacity/stale hash/double claim/wrong release.

- [ ] **Step 2: Run RED**

Run `verify-actor-bundle-runtime verify-actor-meshlets`. Expected: missing runtime and strided binder APIs.

- [ ] **Step 3: Implement shared-core strided binding and publication**

Keep the reviewed standalone binder unchanged. Factor its per-lane layout so the new entry accepts the validated global stride. Use P2/cache-through publication and SH-2 `tas.b` claim style already established by render snapshots; host atomics model it.

- [ ] **Step 4: Run GREEN and concurrency mutations**

Run runtime, meshlet, pose, instance-queue, batch, and feature-off wrapper gates. Repeat with lane sizes reversed and every raw workspace residue modulo four.

- [ ] **Step 5: Commit and review**

Commit `feat(saturn): bind actor bundles to dual SH-2 work lanes`; independent concurrency/ABI review before Task 9.

---

### Task 9: Implement lease-drained bounded scene streaming

**Files:**
- Create: `src/port/saturn/runtime/saturn_scene_stream.h`
- Create: `src/port/saturn/runtime/saturn_scene_stream.c`
- Create: `tools/saturn/scene_stream_test.c`
- Modify: `src/port/saturn/runtime/saturn_scene_residency.h`
- Modify: `src/port/saturn/runtime/saturn_scene_residency.c`
- Modify: `tools/saturn/scene_residency_test.c`
- Modify: `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`, plan, and ledger

**Interfaces:**
- Consumes: S64P root bytes, direct-to-cart payload destination, residency lease APIs, bundle validator/runtime, and fixed root/workspace capacities.
- Produces:

```c
typedef bool (*sm64_saturn_scene_stream_read_fn)(
    void *context, const char *name, uint32_t offset,
    void *destination, uint32_t byte_count);

typedef enum sm64_saturn_scene_stream_state {
    SM64_SATURN_SCENE_STREAM_ACTIVE,
    SM64_SATURN_SCENE_STREAM_DRAIN_REQUESTED,
    SM64_SATURN_SCENE_STREAM_DRAINING,
    SM64_SATURN_SCENE_STREAM_RECLAIMED,
    SM64_SATURN_SCENE_STREAM_LOADING_ROOT,
    SM64_SATURN_SCENE_STREAM_LOADING_ACTORS,
    SM64_SATURN_SCENE_STREAM_VALIDATING,
    SM64_SATURN_SCENE_STREAM_COMMITTING,
    SM64_SATURN_SCENE_STREAM_FAILED,
} sm64_saturn_scene_stream_state_t;

typedef struct sm64_saturn_scene_stream_request {
    char root_name[13], actor_name[13];
    uint32_t root_byte_count, actor_byte_count, package_generation;
    uint32_t root_hash_words[8], actor_hash_words[8];
} sm64_saturn_scene_stream_request_t;

typedef struct sm64_saturn_scene_stream_config {
    sm64_saturn_scene_stream_read_fn read;
    void *read_context;
    sm64_saturn_scene_residency_t *residency;
    sm64_saturn_actor_bundle_publication_t *actor_publication;
    void *root_storage, *cart_storage, *workspace;
    uint32_t root_capacity, cart_capacity, workspace_capacity;
} sm64_saturn_scene_stream_config_t;

bool sm64_saturn_scene_stream_init(
    sm64_saturn_scene_stream_t *state,
    const sm64_saturn_scene_stream_config_t *config);
bool sm64_saturn_scene_stream_request(
    sm64_saturn_scene_stream_t *state,
    const sm64_saturn_scene_stream_request_t *request,
    uint32_t next_residency_generation);
bool sm64_saturn_scene_stream_step(sm64_saturn_scene_stream_t *state,
                                   bool gameplay_suspended,
                                   uint16_t sector_budget);
```

- [ ] **Step 1: RED state-machine tests**

Prove: live leases prevent unload and read calls; false `gameplay_suspended` prevents reads; budget 0 and >16 reject; each step reads no more than `sector_budget * 2048`; exact source==cart destination is a validated no-copy residency load; root/hash/I/O/bank/workspace mutations publish no generation; commit writes generation/committed last; after reclaim a failure cannot reactivate old bytes.

- [ ] **Step 2: Run RED**

Run `verify-scene-stream verify-scene-residency`. Expected: missing stream API and no direct-to-destination residency contract.

- [ ] **Step 3: Implement preloaded-cart and stream state machine**

Add an explicit residency load mode that accepts only exact source/destination identity after hash validation and performs no `memcpy`; all non-alias cases retain existing copy behavior. Implement the master state machine with bounded counters and named failure enum.

- [ ] **Step 4: Run GREEN and existing lease gates**

Run stream, residency, package runtime, render snapshot, VDP1 frame-bank, actor runtime, and audio residency gates. Inject every lease class independently and together.

- [ ] **Step 5: Commit and review**

Commit `feat(saturn): stream scene bundles after lease drain`; independent lifecycle/TOCTOU review before Task 10.

---

### Task 10: Bind the real sourceboot cart, CDFS adapter, and generated storage

**Files:**
- Create: `src/port/saturn/sourceboot/source_scene_bundle.h`
- Create: `src/port/saturn/sourceboot/source_scene_bundle.c`
- Create: `tools/saturn/source_scene_bundle_test.c`
- Create: `tools/saturn/test_source_scene_bundle_make.py`
- Modify: `src/port/saturn/sourceboot/source_cart.h`
- Modify: `src/port/saturn/sourceboot/source_cart.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`
- Modify: `tools/saturn/test_sourceboot_feature_identity.py`
- Modify: `tools/saturn/test_sourceboot_hermetic_build_make.py`
- Modify: `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, plan, and ledger

**Interfaces:**
- Consumes: generated capacity/catalog headers, `sm64_saturn_source_cart_residency_span`, CDFS, scene stream/runtime, non-provisional S64P and S64F ISO files.
- Produces one `.lwram_actor_bundle_workspace`, one fixed root buffer, one residency/stream/runtime owner, and target telemetry with state, generation, hashes, counts, lane claims, bytes loaded, failure, and lease counts.

```c
bool sm64_saturn_source_scene_bundle_init(uint16_t level_id,
                                          uint16_t area_id);
bool sm64_saturn_source_scene_bundle_step(bool gameplay_suspended);
bool sm64_saturn_source_scene_bundle_resolve(
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    sm64_saturn_actor_bank_view_t *bank,
    sm64_saturn_actor_meshlet_bank_output_t *workspace);
void sm64_saturn_source_scene_bundle_release(uint8_t lane,
                                             uint32_t generation);
```

- [ ] **Step 1: RED source/Make/host tests**

Require the feature-on build to compile/link the owner, catalog, S64P, S64F, runtime, stream, handoff prerequisites, and ISO files; feature-off must not reference the v3 resolver. The host adapter uses an in-memory CDFS fixture and proves BOB boot plus a second catalog scene transition.

- [ ] **Step 2: Run RED**

Run `verify-source-scene-bundle`, its Make policy test, and feature identity. Expected: missing owner and ISO wiring.

- [ ] **Step 3: Implement fixed storage and CDFS/cart adapter**

Place capacity-header-sized arrays in explicit LWRAM sections, bind the cart high-water span, read exact catalog names, initialize residency/stream/runtime, retain the active view rather than discarding the boot package, and expose only scalar telemetry. Guard the complete path with `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE` so feature-off code shape remains frozen.

- [ ] **Step 4: Wire boot/loading transition and source closure**

Replace the local discarded `scene_package_view` block with `sm64_saturn_source_scene_bundle_init`. The initial load occurs before gameplay; later loads run only in the source-owned loading mode. Add every generator, report, package, bundle, catalog, source, recipe, and ISO input to discovery/post-link closure verification.

- [ ] **Step 5: Run the full host and dry-run wave**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-package verify-actor-family-bundle-build verify-actor-identity-registry verify-actor-bundle-runtime verify-scene-stream verify-scene-residency verify-source-scene-bundle verify-actor-meshlets verify-actor-pose-bank verify-actor-instance-queue verify-actor-batches verify-actor-feature-off-wrapper
```

Then run Python syntax and scoped `git diff --check`. Expected: all green; no Task 16 production queue wrapper is changed yet.

- [ ] **Step 6: Build one serialized development target and inspect the map**

Run `build_sourceboot_variant.py --label actor-bundle-prereq --animation 1 --actors 1 --audio 0 --pipeline 4 --diagnostic-mode none --jobs 1 --output build/saturn/variants/actor-bundle-prereq`. This is development evidence, not a Task 9 release. Require positive HWRAM/LWRAM margins; exactly one workspace; one cart bundle; no linked generic S64B/S64F duplicate; exact S64P/S64F ISO entries; successful boot bundle generation/hash/count telemetry; and zero loader failure.

- [ ] **Step 7: Commit, review, and reconcile the handoff**

Commit `feat(saturn): load generic actor scene bundles in sourceboot`. Request same-scope spec and code-quality reviews. After PASS, mark this prerequisite `source-complete-review-passed`, update Task 16 Task 2 from blocked to ready-for-RED, and leave Tasks 2-5, Task 9 reseal, and Task 10 smoke/visual/manual explicitly open.

---

### Task 11: Final prerequisite verification and transition to Task 16

**Files:**
- Modify: `docs/superpowers/plans/2026-08-07-task16-completion.md`
- Modify: `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md`
- Modify: `.superpowers/sdd/2026-08-07-task16-completion/progress.md`
- Create: `.superpowers/sdd/2026-08-07-task16-completion/generic-actor-bundle-report.md`
- Modify: `STATE.md`
- Modify: `ROADMAP.md`

**Interfaces:**
- Consumes: all task commits/reviews/tests, generated inventory, development ELF/map/ISO telemetry, and clean scoped Git range.
- Produces: one authoritative handoff naming the exact bundle source commit, reports/hashes/capacities, review verdicts, and every still-open gate.

- [ ] **Step 1: Fresh verification from committed HEAD**

Run every exact host command from Tasks 1-10, the all-scene inventory, Python `compileall` for changed tools, `git show --check` for every behavior/status commit, and scoped range `git diff --check`. Do not reuse pre-commit output.

- [ ] **Step 2: Validate evidence and dirt boundaries**

Rehash the BOB S64F/S64P, every embedded S64B, registry header inputs, inventory JSON, generated capacity/catalog headers, development ELF/ISO/CUE, and ISO-extracted package/bundle files. Confirm unrelated tracked/untracked work and the two preserved Task 14 WIP stashes are unchanged.

- [ ] **Step 3: Write the report and status commit**

Record exact SHAs, tests, counts, workspace/root bytes, cart high water, HWRAM/LWRAM margins, unsupported capability gaps, target telemetry, and review verdicts. Explicitly state: Task 16 Tasks 2-5 have not run; no actor queue drain, Task 9 release, v4 reseal, Task 10 smoke, visual, desktop, owner manual, or total-game acceptance is claimed.

- [ ] **Step 4: Commit documentation and stop at the review gate**

Commit `docs(saturn): hand off generic actor bundle prerequisite`. The next action is Task 16 Task 2 RED from `docs/superpowers/plans/2026-08-07-task16-completion.md`, not an unplanned renderer edit.

## Self-review checklist

- Every exact S64F v3 design requirement maps to Tasks 1-10.
- S64B source identity, registry identity, S64P bundle identity, residency generation, and render generation remain distinct.
- Heterogeneous master/slave banks use the global stride API; the reviewed standalone binder remains unchanged.
- Immutable bytes occupy cart; mutable lanes/root state occupy generated fixed LWRAM; the 65,536-byte actor arena is unchanged.
- The initial BOB path and later CD scene transition use one loader/lease contract.
- Historical v2, feature-off byte identity, unsupported-family failure, whole-game inventory, map proof, and hermetic follow-up all have explicit gates.
- No task marks target/release/manual evidence complete from host-only tests.
