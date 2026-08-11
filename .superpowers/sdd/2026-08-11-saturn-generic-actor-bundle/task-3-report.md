# Task 3 Report — Deterministic Generic S64B Variant Banks

Date: 2026-08-11

Status: source-complete; independent review pending. Task 4 and every
production/target/release gate remain open.

## Scope and boundary

Implemented `compile_actor_variant(root, family_ordinal, model_id, records)`
and the exact frozen `CompiledActorVariant` interface from the Task 3 brief.
The API returns a fully host-validated, big-endian S64B and typed report without
writing output. It does not orchestrate or publish an S64F scene bundle, wire
sourceboot, add target pointers, allocate runtime heap storage, or alter the
fixed SH-2 bank/workspace ABI.

Selection is rooted in the closure's exact `root_provenance`. Symbol discovery
searches only closure-attested sources. Every selected behavior/model/GeoLayout
binding, reached GeoLayout/display-list/Vtx/light source, animation table, and
animation definition used for output enters the sorted `SourceRecord` set and
the approved `actor_family_bundle.source_identity`. Host absolute paths never
enter the canonical stream.

## TDD evidence

Required RED, before production edits:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_actor_variant_bank -v
ImportError: Failed to import test module: test_actor_variant_bank
ModuleNotFoundError: No module named 'actor_variant_bank'
Ran 1 test in 0.000s
FAILED (errors=1)
```

The separately introduced generic-animation parser test was also captured RED
before parser implementation:

```text
ImportError: cannot import name 'parse_animation_table_text' from 'actor_source'
```

Focused GREEN after the closure-hardening self-review correction:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 14 tests in 3.358s
OK
```

The Make target `verify-actor-variant-bank` runs those same 14 tests through
the repository's pinned tool Python.

## Exact synthetic fixture assertions

### Rigid/nested-list fixture

- Exact 358-byte S64B; family/model `(7, 11)`.
- Exact lane/scratch: `104` and `211 == 3 + 2 * 104`.
- Payload SHA-256:
  `3aa76aa8d63d012e5117ed28748bf21f144e31010ac8aea03038f3fedb57a043`.
- Source SHA-256:
  `129e2d833160e29caba88e00552512b739484bc8ee14b1861475b3e0da5033f1`.
- Exact five-source ordered identity (GeoLayout, model/list/Vtx/light,
  behavior binding, model-ID source, model binding).
- One implicit root joint at `(0,0,0)`, four exact joint-local vertices, one
  exact quad `[0,1,2,3]`, and one exact tiered meshlet.
- Exact light/material state: RGB555 `[31,16,8]`, `test_light`, no texture or
  combine/env/alpha state, back-face culling, opaque layer.
- Exactly one neutral one-frame animation; decoded channels are six zeroes.

### Articulated fixture

- Exact 468-byte S64B; family/model `(9, 13)`.
- Exact lane/scratch: `192` and `387 == 3 + 2 * 192`.
- Payload SHA-256:
  `1e688dc471c5590c672632377c31d8c3f4906bb31f7c3d476e8cf310069c395e`.
- Source SHA-256:
  `9efc768508379b3d07c36d76b54171f9382eee42c6c6e423cb8af4c4319f5ef2`.
- Exact two-joint hierarchy and translations `(10,0,0)` then `(0,5,0)`;
  exact GeoLayout node ordinals and joint ownership.
- Six exact joint-local vertices, two exact part bindings, and exact fallback
  triangle primitives `[0,1,2,2]` and `[3,4,5,5]`.
- Exact decoded frame samples:
  `[10,20,30,40,50,60,70,80,90]` and
  `[11,21,31,41,51,61,71,81,91]`.
- Exact typed animation table/source/symbol-to-ID binding.

### Typed variants

- Switch: exact callback `test_switch`, case count 2, ordered case display
  lists `test_case_a_dl`/`test_case_b_dl`, and exact geometry for both cases.
- Billboard: exact reached display-list metadata and exact three local
  vertices/triangle geometry.
- Alpha and translucent: exact layer labels, distinct typed opacity labels,
  meshlet opacity `1`, and exact quad geometry. The S64B binary opacity stays
  bounded while the report retains the source distinction.

## Mutation matrix

- Append one byte/comment to an attested model source without changing the
  closure digest: `ActorSourceDriftError`.
- Rewrite a GeoLayout display-list target to a missing symbol after rehash:
  `ActorSourceSelectionError`.
- Remove the reached model/list/Vtx/light file from the closure source set:
  `ActorSourceSelectionError` (`missing Gfx source`), proving closure-bounded
  discovery.
- Replace one switch case with a missing case target:
  `ActorSourceSelectionError`.
- Replace the child `GEO_ANIMATED_PART` with a bare display list:
  `ActorJointOwnershipError`.
- Move an animation channel offset beyond the value array:
  `ActorAnimationBindingError`.
- Change `LAYER_ALPHA` to `LAYER_OPAQUE` after rehash: both source and payload
  hashes change and meshlet opacity changes exactly from 1 to 0.

Malformed/unknown GeoLayout, display-list, vertex-cache, material-state,
animation, and ownership cases use named subclasses of `ActorVariantError`.
Unsupported nodes/states are rejected; geometry is never silently skipped and
there is no Mario or first-record fallback.

## Shared encoder and legacy byte proof

`compile_actor_bank.py` now exposes only the required shared
`pack_actor_bank(...)` byte-layout boundary. The historical Mario compiler
converts its existing documents to `Joint`, `Vertex`, and `Geometry` records
and calls the same encoder; source selection and report policy remain outside
the encoder.

The generated pre/refactor comparison is sequence-equal (no differing byte):

- S64B size: `596896`.
- S64B SHA-256:
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
- Exact JSON report SHA-256:
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`.
- Source SHA-256 remains
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- Historical geometry remains 424 vertices, 644 primitives, 31 meshlets;
  maximum scratch remains 11,043 bytes.
- Existing CLI arguments, output behavior, and report shape are unchanged.

## Reference-code-first record

Pinned repository/base:
`D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`
at `891a77a43d3587f62e21631acd23d6b584a78b58` before Task 3 edits. No external
source was needed and no new licensing obligation was introduced.

- `tools/saturn/compile_actor_bank.py`: direct factor/adaptation of the
  historical S64B header, compact animation, geometry, workspace, and report
  packing; both Mario and generic callers use the factored encoder.
- `tools/saturn/actor_source.py`: direct extension of the existing
  `AnimationRecord`/compact-channel parser shapes for exact selected tables and
  animation definitions.
- `tools/saturn/dl_rigid_groups.py`: direct dependency for reached GeoLayout
  and display-list structure/ordinal validation. No edit was needed.
- `tools/saturn/extract_mario_actor.py`: direct imports and close-port of
  `blocks`, `vertex_rows`, `ints`, matrix helpers, persistent 32-slot Fast3D
  cache, and material-state traversal.
- `tools/saturn/saturn_mesh_ir.py`: direct `compile_mesh_ir` dependency for
  deterministic primitive pairing and exact source-triangle attribution.
- `tools/saturn/actor_family_bundle.py`: direct `SourceRecord`,
  `source_identity`, and complete S64B host validator reuse from approved Task
  2 semantics.

## Verification

All Make invocations used:

```text
SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
```

Results:

- `verify-actor-variant-bank`: PASS, 14/14 focused variant/source tests.
- `DisplayListRigidGroupTests GeoLayoutRigidGroupTests`: PASS, 25/25.
- `verify-actor-pose-bank verify-actor-meshlets`: PASS. Mario pose fixture,
  meshlet fixture, and the expected invalid-span/legacy-byte mutations were
  caught; Make exited 0.
- `python -m compileall -q` on the five changed Python modules/tests: PASS.
- Scoped `git diff --check` for Task 3 paths: PASS (only platform line-ending
  conversion warnings).

These are source/host gates only. No target, P2, Ymir, sourceboot, release,
smoke, visual, desktop, manual, or total-game claim is made.

## Changed files

- `tools/saturn/actor_variant_bank.py` (new compiler and named boundaries)
- `tools/saturn/test_actor_variant_bank.py` (exact fixtures/mutations)
- `tools/saturn/compile_actor_bank.py` (shared S64B encoder)
- `tools/saturn/actor_source.py` (selected generic animation parsing)
- `tools/saturn/test_actor_source.py` (parser exact/failure tests)
- `Makefile.saturn.mk` (focused verification target)
- `CHANGELOG.md`
- `docs/superpowers/plans/2026-08-11-saturn-generic-actor-bundle.md`
- `.superpowers/sdd/2026-08-07-task16-completion/progress.md`
- this report

`dl_rigid_groups.py` and `test_tools.py` were inspected and exercised but left
unchanged because their existing token/rigid-group contract already exposed
the required data and its existing 25 exact tests remained sufficient.

## Commits

- Behavior/source transition: `031e1620`
  (`feat(saturn): compile source-selected actor variant banks`).
- Evidence/status transition: `cdd9637d`
  (`docs(saturn): record actor variant bank evidence`).

## Scoped self-review

Reviewed the complete Task 3 diff against the brief and approved source
identity semantics. The review tightened source discovery from repository-wide
search to closure-attested files only, made every reached source require an
attestation, preserved duplicate switch/billboard bindings in source order,
validated exact triangle macro arity, and rejected malformed typed model
variants. Focused tests were rerun after those corrections.

No unrelated dirty file is staged or modified by this task. Generated build
artifacts are ignored and are not part of the commit.

## Concerns and open gates

- Independent Task 3 review is mandatory before Task 4 and is still pending.
- The supported source-language boundary is deliberately narrow. Transform
  nodes such as scale/rotate/translate, arbitrary `GEO_ASM`, display-list
  matrix ownership, unsupported layers/textures/material states, malformed
  macros, and ambiguous or unattested symbols fail closed. Task 4 may expose a
  real BOB construct requiring a separately tested, explicit boundary
  extension; this task does not silently broaden syntax.
- Scene S64F orchestration/publication, real BOB compilation, whole-game
  capacity, registry regeneration, cart residency, fixed dual-SH-2 workspace,
  production sourceboot, feature-off identity, target link/map, P2/Ymir,
  transition, release/reseal, smoke/visual/desktop/manual, and total-game gates
  remain open.

---

## Review repair round 1 — 2026-08-11

Status: source-complete review repair at behavior commit `11c86f6a`, with
evidence/status commit `4e32ca2e`; independent rereview pending. This section supersedes the earlier fixture
identities/counts where the corrected numeric model selection changes the
canonical source stream. It does not reopen or claim Task 4, S64F scene
orchestration, sourceboot, target, release, or manual gates.

### Findings repaired

1. Numeric `model_id` is now a selector, not merely an output label. For every
   record, the compiler parses the exact attested model-ID source, validates
   every `model_variants` row against its `root_provenance.models` binding, and
   requires exactly one numeric match and one consistent
   `(model, geo_root, geo_source)` across records. Primary ID 1 selects
   `MODEL_TEST/test_geo`; alternate ID 2 selects
   `MODEL_ALT/test_alt_geo` and its exact one-triangle geometry; missing ID 3,
   duplicate ID aliases, provenance mismatches, and cross-record conflicts
   raise `ActorSourceSelectionError`.
2. S64B v1 cannot serialize texture coordinates or texture/combine/cull/env/
   alpha/tile/load state. The compiler therefore rejects, by
   `UnsupportedActorSourceError`, each reviewed mutation:
   `gsDPSetTextureImage`, `gsDPLoadTextureBlock`, `gsSPTexture`,
   `gsDPSetCombineMode`, `gsSPSetGeometryMode`, `gsSPClearGeometryMode`,
   `gsDPSetEnvColor`, `gsDPSetAlphaCompare`, `gsDPLoadSync`, `gsDPLoadBlock`,
   `gsDPSetTile`, `gsDPTileSync`, `gsDPSetTileSize`, and ambient/alternate
   `gsSPLight`. `gsDPPipeSync` and the final display-list terminator remain
   syntax-checked synchronization/structure only.
3. Task-3-selected GeoLayout, Gfx, Vtx, animation table/header, and numeric
   arrays now use coverage-preserving tokenization. Every unexplained token,
   missing comma, unbalanced call/initializer, duplicate selected definition,
   wrong terminator, wrong arity, invalid integer expression/octal, and
   out-of-range encoded scalar fails closed. Historical Mario parsers were not
   globally rewritten.
4. The root GeoLayout must exist exactly once in the declared `geo_source`.
   A same-named definition in another attested source cannot replace a missing
   declared root. Cross-file search is limited to reached symbols without
   explicit provenance and rejects ambiguity.
5. The public compiler validates family/model/live counts, vertex/channel
   scalars, joint/material/primitive/meshlet fields and encoded counts before
   packing. Expected Mesh IR, identity, conversion, packing, and validation
   errors are translated to `ActorSourceSelectionError`,
   `ActorAnimationBindingError`, or `MalformedActorSourceError`; raw
   `ValueError`, `TypeError`, `OverflowError`, and `struct.error` no longer
   escape the selected public boundary.

### Repair TDD RED

The first repair RED was captured before production changes:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 22 tests in 4.772s
FAILED (failures=24, errors=1)
```

The subtest failures reproduced the review findings exactly: alternate model
selection returned `MODEL_TEST` instead of `MODEL_ALT`; model mismatch and
numeric alias ambiguity were accepted; a missing declared root fell back to a
same-named attested source; all fourteen unrepresentable material/texture/
alternate-light states were accepted; unexplained/malformed GeoLayout and Gfx
tokens, a malformed reached Vtx row, animation expression `5 + 5`, and a bad
parts macro were ignored. An int16 coordinate of `40000` escaped as raw
`ValueError` from Mesh IR validation, and the direct selected-animation parser
accepted an invalid numeric initializer.

Three narrower REDs captured holes discovered while implementing the repair:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_source.ActorSourceTest.test_generic_animation_binding_rejects_unknown_entries_and_bad_spans -v
Ran 1 test in 0.001s
FAILED (failures=1)
# Duplicate selected animation-table definitions were accepted.

> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_public_boundary_translates_scalar_count_and_packing_failures -v
ERROR: TypeError: 'NoneType' object is not iterable
# records=None escaped the named public boundary.

> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_public_boundary_translates_scalar_count_and_packing_failures -v
ERROR: ValueError: invalid literal for int() with base 0: '08'
# Invalid C octal escaped instead of becoming MalformedActorSourceError.
```

### Repair GREEN and regression output

Focused current-tree GREEN:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 22 tests in 6.050s
OK
```

Historical rigid-group GREEN, using the two actual classes in
`tools.saturn.test_tools` (an initial stale module-name invocation failed at
import and exercised no code):

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_tools.DisplayListRigidGroupTests \
    tools.saturn.test_tools.GeoLayoutRigidGroupTests -v
Ran 25 tests in 0.003s
OK
```

Required combined Make GREEN:

```text
> powershell -ExecutionPolicy Bypass -File \
    tools/saturn/with-msys-toolchain.ps1 mingw32-make \
    -f Makefile.saturn.mk -j1 \
    SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
    verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
actor pose bank fixture: PASS
actor meshlet fixture: PASS
actor meshlet invalid-span mutation caught by fixture
Ran 22 tests in 4.064s
OK
legacy Mario output/object bytes changed
Exit code: 0
```

The final `legacy Mario output/object bytes changed` line is the gate's
deliberately mutated executable proving its rejection path; the combined Make
command exited zero. Python syntax also passed:

```text
> .venv-saturn-tools\Scripts\python.exe -m compileall -q \
    tools/saturn/actor_variant_bank.py tools/saturn/actor_source.py \
    tools/saturn/compile_actor_bank.py tools/saturn/test_actor_variant_bank.py \
    tools/saturn/test_actor_source.py
Exit code: 0
```

Scoped `git diff --check` on the five source/test files and three tracked
transition documents passed; Git printed only Windows line-ending conversion
warnings.

### Exact repaired fixture evidence

Rigid primary fixture:

- exact identity `(family_ordinal, model_id) == (7, 1)`;
- 358 bytes; lane 104; scratch `211 == 3 + 2 * 104`;
- payload SHA-256
  `d356417800cc21a0f982e18647be8ff1ffba27b4c1f4c50d52d4d21976771f30`;
- source SHA-256
  `780d1b65c6c27a8c7d1c77867f816ec07fd239f7fffcfa0c84a48a338fad68f7`;
- exact five source paths: declared GeoLayout, reached Gfx/Vtx/light source,
  behavior source, model-ID source, and binding source (deduplicated and
  sorted); no host path is canonicalized;
- one root joint, exact four local vertices, RGB555 `[31,16,8]`, one quad
  `[0,1,2,3]`, one opaque meshlet, and one neutral frame whose six decoded
  channels are zero.

Articulated fixture:

- exact identity `(9, 1)`; 468 bytes; lane 192; scratch
  `387 == 3 + 2 * 192`;
- payload SHA-256
  `1bff9db7ae5c3cea3512f748a721706a3709b662cb9c8a88ad3b4c0528b0634e`;
- source SHA-256
  `0d617e2444ef50ce6d16e41aaac6572e47eacdff534c5aab8efcc08cdb118a60`;
- exact two-joint hierarchy/translations `(10,0,0)` and `(0,5,0)`, exact six
  joint-local vertices, exact branch/part ownership, and exact triangle
  primitives `[0,1,2,2]` and `[3,4,5,5]`;
- exact decoded samples `[10,20,30,40,50,60,70,80,90]` and
  `[11,21,31,41,51,61,71,81,91]`, with exact selected table/source/symbol ID.

Switch, billboard, alpha, and translucent assertions remain exact: switch
callback/case count/order and both triangle primitives; billboard reached-list
metadata and three local vertices; alpha/translucent layer labels, typed
opacity, meshlet opacity 1, and exact quad geometry.

### Exact new mutation coverage

- Primary ID, alternate ID, unknown ID, duplicate numeric alias, variant/
  provenance mismatch, and conflicting record selection.
- Missing declared root despite a same-named attested candidate, and duplicate
  root definitions inside the declared source.
- Fourteen Fast3D state mutations listed above, each requiring the named
  unsupported error.
- Unexplained token, missing command comma, and unterminated call in both
  GeoLayout and Gfx; invalid expression in a reached Vtx initializer.
- Animation numeric expression, wrong `ANIMINDEX_NUMPARTS` macro, duplicate
  selected table, invalid header flags, s16 value `40000`, and corrupt span.
- Vertex coordinates `40000` and invalid C integer `08`; boolean/string/
  overflowing maximum-live values; boolean family/model IDs; malformed
  records sequence; bounded count/packing fields.

All earlier source drift, missing target/source, joint-owner, switch-case,
animation-span, and material-layer identity mutations remain green. No test
accepts nonempty output as sufficient: geometry, pose samples, source lists,
typed metadata, sizes, workspace values, and exact hashes are pinned.

### Legacy byte/hash proof

The current Make-generated and saved Task-3 refactor artifacts remain pairwise
identical:

```text
mario.s64b size=596896
sha256=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
task3-refactor.s64b size=596896
sha256=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
mario-actor-bank.json size=562096
sha256=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
task3-refactor.json size=562096
sha256=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
```

Thus the validation added around the factored encoder changes no historical
Mario payload byte, JSON report byte, CLI behavior, or consumer shape.

### Changed files and reference reuse

Repair behavior commit `11c86f6a` changes:

- `tools/saturn/actor_variant_bank.py`: exact numeric selection, declared-root
  ownership, task-local coverage tokenizer, unrepresentable-state rejection,
  and named public-boundary translations;
- `tools/saturn/actor_source.py`: strict selected generic table/header/numeric
  array parsing without changing historical Mario parsing;
- `tools/saturn/compile_actor_bank.py`: pre-pack scalar/count/span validation
  around the existing historical encoder;
- `tools/saturn/test_actor_variant_bank.py` and
  `tools/saturn/test_actor_source.py`: exact repair RED/GREEN coverage;
- `CHANGELOG.md`, the active implementation plan, and tracked Task 16 ledger:
  source-only repair transition and open-gate record.

Evidence/status commit `4e32ca2e`
(`docs(saturn): record actor variant repair evidence`) pins the behavior SHA in
the active plan and tracked Task 16 ledger. This ignored report records both
commits but is intentionally not force-added.

Reference-code-first remains in-tree at base
`891a77a43d3587f62e21631acd23d6b584a78b58`: direct/shared use of
`actor_family_bundle.py` source identity and validator;
`dl_rigid_groups.py` structure walker; `saturn_mesh_ir.py` compiler;
`compile_actor_bank.py` historical byte encoder; and close-port of the selected
block/Vtx/integer/matrix shapes in `extract_mario_actor.py`. The repair narrows
the generic selected-source boundary instead of globally modifying those
historical parsers. No external source or licensing input was used.

### Scoped self-review, concerns, and open gates

The complete repair diff was reviewed against all five findings and the
approved source-identity/embedded-bank design. Selected definitions are
closure-attested and hash-checked before use; every reached source enters
`source_identity`; exact root provenance cannot fall back; no unencoded
runtime material state is accepted; expected malformed values cross the
public API through named errors; and the encoder layout remains historical.
Only the eight explicit Task-3 tracked paths were staged for the behavior
commit. Unrelated dirty files and generated artifacts were not staged,
cleaned, reset, checked out, or stashed.

Concern/open gate: independent Task 3 rereview is still mandatory. The narrow
source-language boundary will likely reject real BOB constructs until a later
task adds each required semantic with exact tests; this is deliberate and no
unknown construct is dropped. Real BOB/whole-game compilation and capacity,
scene S64F orchestration, registry regeneration, cart residency/fixed LWRAM
workspace, Task 16 Tasks 2-5, feature-off identity, target/P2/Ymir/map,
transition, Task 9 rebuild/reseal, Task 10 smoke/visual/desktop/owner-manual,
release, and total-game acceptance all remain open and unclaimed.

---

## Review repair round 2 — 2026-08-11

Status: source-complete at behavior commit `b21bb197`; independent rereview
pending. This round fixes only the two findings left open after round 1.

### Binding-source authority

`root_provenance` metadata no longer proves a model binding by itself. For the
numeric model candidate selected from the attested model-ID source, the
compiler now parses the exact attested `binding_source` and requires exactly
one source-backed mapping to the selected GeoLayout symbol:

- `LOAD_MODEL_FROM_GEO(MODEL_X, geo_root)` and
  `LOAD_MODEL_FROM_DL(MODEL_X, geo_root)` calls are comment-stripped, balanced,
  required to have exactly two identifier arguments, and required to end at a
  comma, semicolon, newline, or EOF boundary;
- the collector's fallback where `binding_source == model-ID source` requires
  one exact `#define MODEL_X numeric_id // geo_root` binding;
- missing, duplicate, conflicting, malformed, unterminated, literal-bearing,
  and suffix-contaminated bindings raise `ActorSourceSelectionError`;
- the bound GeoLayout symbol must then exist exactly once in the declared,
  attested `geo_source`, preserving the round-1 strict source ownership.

The exact contradiction RED changes `geo_root`, `geo_symbol`, and `geo_source`
consistently to `alternate_geo` while leaving the source command bound to
`test_geo`; it is now rejected. Additional exact mutations cover missing,
duplicate identical, conflicting-root, unterminated, and trailing-token
commands. A model-ID-comment binding is accepted only when its source bytes
match. A real two-record fixture defines `MODEL_TEST == MODEL_ALT == 1` but
binds them to `test_geo` and `alternate_geo`; it requires the named
`conflicting model/GeoLayout provenance` error rather than a fallback.

### Empty animation fields

Selected generic animation table and nine-field Animation header parsing now
splits without discarding positions. Any empty field is rejected, including
the exact `&walk_anim,, NULL` table mutation, a double comma in an otherwise
nine-field header, and terminal commas after `NULL` or the ninth header field.
The valid synthetic selected table/header formatting was made unambiguous by
removing those terminal empty fields. Numeric channel-array parsing and all
historical Mario parsers remain unchanged.

### Exact RED evidence

Initial RED, before either production edit:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_model_binding_source_must_match_selected_provenance \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_cross_record_model_id_conflict_fails_closed \
    tools.saturn.test_actor_source.ActorSourceTest.test_generic_animation_table_and_header_reject_empty_fields -v
test_model_binding_source_must_match_selected_provenance ... FAIL
test_cross_record_model_id_conflict_fails_closed ... ok
test_generic_animation_table_and_header_reject_empty_fields ... FAIL (4 subtests)
Ran 3 tests in 0.079s
FAILED (failures=5)
```

The first failure was `ActorSourceSelectionError not raised` for the consistent
metadata/source contradiction. The four animation failures were `ValueError
not raised` for table double/trailing and header double/trailing empty fields.
The real cross-record conflict test was a characterization of the existing
set-conflict guard and passed before production changes; no false RED is
claimed for it.

While closing the exact command boundary, a second RED proved that unexplained
suffix tokens were still ignored:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_model_binding_source_must_match_selected_provenance -v
... (label='trailing_token') ... FAIL
AssertionError: ActorSourceSelectionError not raised
Ran 1 test in 0.111s
FAILED (failures=1)
```

### Exact GREEN and regression evidence

Targeted GREEN:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest [three tests above] -v
Ran 3 tests in 0.119s
OK
```

Focused current-tree GREEN:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 25 tests in 5.518s
OK
```

Historical rigid-group and syntax GREEN:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_tools.DisplayListRigidGroupTests \
    tools.saturn.test_tools.GeoLayoutRigidGroupTests -v
Ran 25 tests in 0.003s
OK

> .venv-saturn-tools\Scripts\python.exe -m compileall -q \
    tools/saturn/actor_variant_bank.py tools/saturn/actor_source.py \
    tools/saturn/compile_actor_bank.py tools/saturn/test_actor_variant_bank.py \
    tools/saturn/test_actor_source.py
Exit code: 0
```

Required combined Make GREEN used the native forward-slash worktree root:

```text
> powershell -ExecutionPolicy Bypass -File \
    tools/saturn/with-msys-toolchain.ps1 mingw32-make \
    -f Makefile.saturn.mk -j1 \
    SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
    verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
actor pose bank fixture: PASS
actor meshlet fixture: PASS
actor meshlet invalid-span mutation caught by fixture
Ran 25 tests in 4.307s
OK
legacy Mario output/object bytes changed
Exit code: 0
```

As in round 1, the final changed-bytes line is the deliberately mutated
negative executable; Make exited zero.

### Fixture and legacy identity proof

Rigid remains exactly 358 bytes, lane/scratch 104/211, payload/source
`d356417800cc21a0f982e18647be8ff1ffba27b4c1f4c50d52d4d21976771f30` /
`780d1b65c6c27a8c7d1c77867f816ec07fd239f7fffcfa0c84a48a338fad68f7`.

Removing terminal empty fields from the selected articulated source changes
its canonical source identity and therefore the embedded source digest, but no
geometry/pose/workspace byte shape. The repinned artifact is exactly 468 bytes,
lane/scratch 192/387, payload/source
`2a4af81303a523a26f6ed3e9df2ac1a7aeb600a1b2c129158d3a6dc79231b93e` /
`d5a472a4f4f90145e2882adf997b75912f205f82fc51d718ede704c329c3929d`.
Its two-joint ownership, six local vertices, two exact primitives, and decoded
samples `[10,20,30,40,50,60,70,80,90]` /
`[11,21,31,41,51,61,71,81,91]` remain pinned.

Historical shared-encoder artifacts remain byte-for-byte exact:

```text
mario.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
task3-refactor.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
mario-actor-bank.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
task3-refactor.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
```

### Files, reuse, self-review, and open gates

Behavior commit `b21bb197`
(`fix(saturn): verify actor model binding sources`) changes only:

- `tools/saturn/actor_variant_bank.py` and its exact fixture tests;
- `tools/saturn/actor_source.py` and its selected generic parser tests;
- `CHANGELOG.md`, the active plan, and tracked Task 16 ledger.

Evidence/status commit `034f3c05`
(`docs(saturn): record actor binding repair evidence`) pins `b21bb197` in the
active plan and tracked Task 16 ledger. This ignored report records both SHAs
without being force-added.

Reuse remains same-repository direct/close-port at base `891a77a43`: the new
binding scanner follows the balanced-call and argument shapes already used by
`collect_scene_closure.py`, while source identity, S64B validation/packing,
rigid groups, Mesh IR, and historical Mario parsing remain their existing
in-tree implementations. No external source or license input was used.

Scoped self-review verified that metadata cannot redirect a selected numeric
model away from its source command, every binding source is attested and enters
the canonical identity, comment fallback is exact, duplicate/conflicting calls
cannot collapse, empty animation positions cannot normalize, and historical
paths are untouched. Only explicit Task-3 paths were staged; unrelated dirt
and generated files were neither staged nor altered.

Independent Task 3 rereview remains open. So do real BOB/whole-game
compilation/capacity, S64F orchestration, registry/cart/workspace, Task 16 Tasks
2-5, feature-off identity, target/P2/Ymir/map, transition, Task 9
rebuild/reseal, Task 10 smoke/visual/desktop/owner-manual, release, and
total-game acceptance. No source-only result closes those gates.

## Real-source repair round 3 (2026-08-11)

### Trigger, boundary, and design correction

Before any Task 4 edit, the real BOB supported key `(family ordinal 3, model
ID 0x007c)` / `bhvMessagePanel` reached the valid command
`LOAD_MODEL_FROM_GEO(MODEL_WOODEN_SIGNPOST, wooden_signpost_geo)` in
`levels/scripts.c`, followed by the valid three-argument command
`LOAD_MODEL_FROM_DL(MODEL_WHITE_PARTICLE_SMALL, white_particle_small_dl,
LAYER_ALPHA)`. Task 3's scanner globally required two arguments and rejected
the selected GEO binding as malformed.

The correction leaves the approved closure schema unchanged. The attested
binding-source bytes are authoritative because that path/hash already enters
the variant source identity. The scanner coverage-validates exact two-argument
GEO and three-argument DL forms and retains the DL layer internally. A selected
direct-DL binding must use one S64B-v1-representable layer and is compiled
through a synthetic one-node GeoLayout rooted at its strict declared Gfx
source. A valid unselected direct-DL layer is parsed without changing a
selected GEO. No S64F orchestration, production sourceboot wiring, runtime
format, heap, pointer, or closure-layer field was added.

### TDD RED

The first production-free RED added all three required real-shaped and
direct-DL tests:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_real_shaped_adjacent_geo_and_direct_dl_bindings_select_geo \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_selected_direct_dl_layer_drives_exact_typed_and_binary_semantics \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_direct_dl_binding_arity_layer_and_conflicts_fail_closed -v
test_real_shaped_adjacent_geo_and_direct_dl_bindings_select_geo ... ERROR
test_selected_direct_dl_layer_drives_exact_typed_and_binary_semantics ... ERROR
test_direct_dl_binding_arity_layer_and_conflicts_fail_closed ... ERROR
Ran 3 tests in 0.241s
FAILED (errors=3)
```

Each error was the pre-repair named boundary:

```text
actor_variant_bank.ActorSourceSelectionError: malformed model binding command in levels/test/script.c
```

After distinguishing GEO/DL arity, the real closure probe exposed a second
boundary: the parser incorrectly rejected a valid, unselected game layer. The
test was amended before the production correction:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank.ActorVariantBankTest.test_real_shaped_adjacent_geo_and_direct_dl_bindings_select_geo -v
test_real_shaped_adjacent_geo_and_direct_dl_bindings_select_geo ... ERROR

actor_variant_bank.UnsupportedActorSourceError: unsupported direct-DL material layer in levels/test/script.c: LAYER_TRANSPARENT_DECAL

Ran 1 test in 0.035s
FAILED (errors=1)
```

The repair moved S64B-v1 layer representability from global source parsing to
the exactly selected direct-DL binding.

### Exact fixtures and mutations

The adjacent fixture contains, in order, selected
`LOAD_MODEL_FROM_GEO(MODEL_TEST, test_geo)`, valid neighboring
`LOAD_MODEL_FROM_DL(MODEL_WHITE_PARTICLE_SMALL, white_particle_small_dl,
LAYER_ALPHA)`, and valid unselected
`LOAD_MODEL_FROM_DL(MODEL_UNSELECTED_DECAL, unselected_decal_dl,
LAYER_TRANSPARENT_DECAL)`. It asserts exact selected model/root, binding kind
`geo` and layer `None`, exact quad `[0,1,2,3]`, and no selected layer metadata.

The selected direct-DL fixture points provenance exactly at
`actors/test/model.inc.c:test_root_dl`. `LAYER_ALPHA` asserts:

- exactly 358 bytes, lane 104, scratch 211;
- payload/source hashes
  `89f2f521e98deda81c4af9be1b8867d23e9245421da8554ba720119234a7fb30` /
  `eaf9fcbffaa587ff5cb46a45f621e5f5294eddcd14cb640e56f1bb245d0d88d1`;
- exact four source paths (model, behavior, model IDs, level script);
- binding kind/layer `display_list` / `LAYER_ALPHA`, exact typed alpha layer,
  exact `[31,16,8]` material, exact quad, and meshlet opacity `1`.

Changing only the attested source command to `LAYER_OPAQUE` asserts payload
and source hashes
`3472a30bf9c4be9e8f3f1b7c86a18e71fcffa3e5382f0c85d756317740811058` /
`57234f661d1cb7dec5a2c15999bc649f247f89c1a37f33e064033c6b53e45909`,
exact binding/material layer `LAYER_OPAQUE`, meshlet opacity `0`, and different
source and payload identities.

Named failure mutations cover GEO with three arguments; DL with two or four
arguments; empty model, root, or layer; duplicate selected binding; conflicting
selected layers; a trailing `BROKEN` token; an unterminated call; and selected
unrepresentable `LAYER_FORCE`. Syntax/selection failures raise
`ActorSourceSelectionError`; selected unrepresentable layer raises
`UnsupportedActorSourceError`.

### GREEN and real-source proof

Targeted GREEN:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest [three tests above] -v
Ran 3 tests in 0.250s
OK
```

Focused source boundary:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 28 tests in 4.218s
OK
```

Historical rigid-group boundary:

```text
> .venv-saturn-tools\Scripts\python.exe -m unittest \
    tools.saturn.test_tools.DisplayListRigidGroupTests \
    tools.saturn.test_tools.GeoLayoutRigidGroupTests -v
Ran 25 tests in 0.002s
OK
```

The corrected real BOB closure selection reports:

```text
records 1
('MODEL_WOODEN_SIGNPOST', 'wooden_signpost_geo',
 'actors/wooden_signpost/geo.inc.c',
 [{'model': 'MODEL_WOODEN_SIGNPOST', 'geo_root': 'wooden_signpost_geo'}],
 _ModelBinding(model='MODEL_WOODEN_SIGNPOST', root='wooden_signpost_geo',
               kind='geo', layer=None))
```

Required combined Make GREEN used the exact native forward-slash worktree
root:

```text
> powershell -ExecutionPolicy Bypass -File \
    tools/saturn/with-msys-toolchain.ps1 mingw32-make \
    -f Makefile.saturn.mk -j1 \
    SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
    verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
actor pose bank fixture: PASS
actor meshlet fixture: PASS
actor meshlet invalid-span mutation caught by fixture
Ran 28 tests in 4.212s
OK
legacy Mario output/object bytes changed
Exit code: 0
```

The final changed-bytes line is the deliberately mutated negative meshlet
executable and the combined Make command exited zero. Python compileall over
`actor_variant_bank.py`, `actor_source.py`, `compile_actor_bank.py`, and both
focused test modules also exited zero. Scoped `git diff --check` passed with
only Windows line-ending warnings.

Historical shared-encoder artifacts remain byte-for-byte exact:

```text
mario-actor-bank.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
mario.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
task3-refactor.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
task3-refactor.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
```

### Files, commits, reuse, self-review, and open gates

Behavior commit `16e61b63`
(`fix(saturn): preserve direct actor binding layers`) changes exactly:

- `tools/saturn/actor_variant_bank.py`;
- `tools/saturn/test_actor_variant_bank.py`;
- `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, the active implementation plan,
  and the tracked Task 16 ledger.

Evidence commit `e72d7127`
(`docs(saturn): record direct binding repair evidence`) pins the behavior
commit in the plan and ledger. This ignored report records both commits without
being staged.

Reference reuse remains same-repository at base
`891a77a43d3587f62e21631acd23d6b584a78b58`: direct reuse of
`compile_actor_bank.py`'s historical `pack_actor_bank`/validate contract and
`actor_source.py` parsing; direct dependency on `dl_rigid_groups.py` and
`saturn_mesh_ir.py`; close-port of reached block/Vtx/Fast3D shapes from
`extract_mario_actor.py`. No external source or new license input was used.

Scoped self-review confirmed that model kind/layer comes only from attested
source bytes; the closure schema has no fabricated layer; the binding source
path/hash remains canonical identity input; direct roots use exact declared
Gfx provenance; GEO still derives layer only from its nodes; selected unknown
layers fail; valid unselected layers cannot alter selected geometry; and the
historical Mario entry point and bytes are unchanged. Only explicit Task 3
paths were staged; no Task 4 file or unrelated dirt was staged, reset, cleaned,
checked out, or stashed.

Scoped independent rereview remains open. Full real BOB variant compilation
and Task 4 S64F orchestration remain unclaimed; this round proves the exact
real-source selection that blocked Task 4, not every downstream actor support
boundary. Whole-game/capacity, registry, cart/workspace, Task 16 Tasks 2-5,
feature-off, target/P2/Ymir/map, transition, Task 9 rebuild/reseal, Task 10
smoke/visual/desktop/owner-manual, release, and total-game gates remain open.

### Independent real-source repair rereview

The scoped independent rereview of `c4c0973e..e72d7127` passed with C0/I0/M0.
It repeated 28/28 focused variant/source tests, 25/25 rigid-group tests, the
combined variant/pose/meshlet Make wave, a fresh BOB area-1 closure selection,
and exact historical Mario artifact hashes. It additionally compared selected
direct-DL alpha and opaque outputs: after excluding the intentionally different
embedded source digest, the canonical S64B payload differed at the expected
opacity byte and the typed report material/layer metadata differed correctly.
No closure schema field was added, no new breakage was found, and Task 4 is
unblocked. Full real BOB compilation and every target/release/manual gate remain
open.

### Real-source provenance repair round 4 — active

After the round-3 independent PASS, Task 4 replayed the exact BOB
`bhvMessagePanel` / model `0x007c` probe. Binding selection reached the correct
`wooden_signpost_geo`, then the strict source index rejected
`wooden_signpost_seg3_dl_0302DA48` because its defining model/display-list file
is absent from the closure record. Task 4 made zero edits. Task 3 is reopened
only to extend the authoritative hash-bound provenance chain; consuming an
unsealed repository source is explicitly forbidden. RED/GREEN, exact Mario
preservation, and independent scoped rereview remain open.

### Real-source provenance repair round 4 — source complete

#### Diagnosis and schema-compatible correction

The authoritative source chain failed upstream, not in the strict variant
compiler. `collect_scene_closure.py::_asset_root_source` found only the file
that defines each selected model root, and the per-record collector added only
that `geo_source`. Consequently the closure sealed
`actors/wooden_signpost/geo.inc.c` but not the reached
`actors/wooden_signpost/model.inc.c`. `scene_package_schema.py` already permits
additional entries in `record.sources`; its fixed
`root_provenance.models[*].geo_source` field must remain the singular source
that defines the selected root symbol. No schema extension or downstream
repository search was needed.

Behavior commit `1274c08b` (`fix(saturn): seal reached actor closure sources`)
adds a bounded, deterministic sealing-side definition index and a strict
coverage walk from the declared GeoLayout or direct-Gfx root through Geo
branches, display-list nodes, nested/tail lists, Vtx, and `Lights1`. Every
uniquely reached path enters the existing sorted `record.sources` array and
top-level `source_hashes`; missing, ambiguous, recursive, malformed, and
computed references raise `ClosureError`. Downstream `_SourceIndex` remains
closure-only and unchanged. Evidence commit `420d0223`
(`docs(saturn): record actor provenance repair evidence`) pins the behavior
commit in the active plan and tracked Task 16 ledger. Task 4 files remain
untouched.

Reference-code-first work stayed entirely in-tree at the Task 3 base. The
reachability resolution mirrors the strict closure-only selection in
`tools/saturn/actor_variant_bank.py`; initializer/token coverage is a close
port of selected-source parsing in `tools/saturn/actor_source.py`; the reached
Geo/Gfx/Vtx/light command shapes were checked against
`tools/saturn/dl_rigid_groups.py`, `tools/saturn/extract_mario_actor.py`, and
`tools/saturn/saturn_mesh_ir.py`. The historical
`tools/saturn/compile_actor_bank.py` pack/validate behavior is reused directly
and was not changed. Reuse mode is direct dependency plus close-port; no
external code or license input was used.

#### RED evidence and exact mutations

The first correctly rooted real BOB test was added before production edits:

```text
> .venv-saturn-tools\Scripts\python.exe tools/saturn/test_bob_scene_closure.py BobSceneClosureTest.test_real_bob_actor_sources_include_reached_model_data -v
ERROR: test_real_bob_actor_sources_include_reached_model_data
KeyError: 'actors/wooden_signpost/model.inc.c'
Ran 1 test in 54.517s
FAILED (errors=1)
```

An earlier module-style invocation failed during import and exercised no
closure code; it is deliberately not counted as the RED. The focused synthetic
RED then ran these five exact tests: reached exact-source/hash collection,
missing reached Gfx, ambiguous duplicate reached Gfx, computed nested Gfx, and
reached-source hash drift.

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
    test_scene_closure.SceneClosureTest.test_collects_reached_actor_asset_sources_and_hashes \
    test_scene_closure.SceneClosureTest.test_missing_reached_actor_source_fails_closed \
    test_scene_closure.SceneClosureTest.test_ambiguous_reached_actor_source_fails_closed \
    test_scene_closure.SceneClosureTest.test_computed_reached_actor_source_fails_closed \
    test_scene_closure.SceneClosureTest.test_reached_actor_source_hash_drift_fails_closed -v
Ran 5 tests in 0.391s
FAILED (failures=5)
```

The exact synthetic graph asserts the root Geo source plus parent Gfx,
branched Geo, nested Gfx, tail-branch Gfx, and shared Vtx/light source paths and
their file SHA-256 values in both the record and closure hash map. Mutations
replace the nested list with `missing_dl`, add a second
`parent_child_dl` definition, replace a target with
`select_parent_dl(1)`, and append a byte to the reached Vtx/light source before
schema validation. A separate mutation temporarily removed Geo-branch and
`gsSPBranchList` traversal from production while leaving those exact source
assertions in place:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
    test_scene_closure.SceneClosureTest.test_collects_reached_actor_asset_sources_and_hashes -v
Ran 1 test in 0.116s
FAILED (failures=1)
```

The production mutation was immediately restored. The first implementation
pass then failed closed on real BOB at `missing reached Gfx dl_billboard_num_0`.
That symbol is authoritatively defined in `bin/segment2.c`, proving the bounded
game-source index also had to include original `bin/**/*.c` and non-port
`src/**/*.c`; no downstream fallback was added.

#### GREEN and regression evidence

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest [the five focused SceneClosureTest cases above] -v
Ran 5 tests in 0.305s
OK

> .venv-saturn-tools\Scripts\python.exe tools/saturn/test_bob_scene_closure.py BobSceneClosureTest.test_real_bob_actor_sources_include_reached_model_data -v
Ran 1 test in 58.529s
OK

> .venv-saturn-tools\Scripts\python.exe tools/saturn/test_scene_closure.py -v
Ran 24 tests in 2.440s
OK

> .venv-saturn-tools\Scripts\python.exe tools/saturn/test_bob_scene_closure.py -v
Ran 2 tests in 64.144s
OK

> .venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_actor_variant_bank tools.saturn.test_actor_source -v
Ran 28 tests in 4.330s
OK

> .venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_tools.DisplayListRigidGroupTests tools.saturn.test_tools.GeoLayoutRigidGroupTests -v
Ran 25 tests in 0.002s
OK

> .venv-saturn-tools\Scripts\python.exe tools/saturn/test_generic_actor_bank.py -v
Ran 4 tests in 60.526s
OK
```

A post-commit package-style selector from the repository root repeated the
known local-import failure and exercised no implementation. The valid owning-
directory rerun was immediately green:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest [the five focused SceneClosureTest cases above] -v
Ran 5 tests in 0.299s
OK
```

The required Make wave used the exact absolute native forward-slash root:

```text
> powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
actor pose bank fixture: PASS
actor meshlet fixture: PASS
actor meshlet invalid-span mutation caught by fixture
Ran 28 tests
OK
legacy Mario output/object bytes changed
Exit code: 0
```

The final line is the intentional negative meshlet mutation. Final compileall
exited zero:

```text
> .venv-saturn-tools\Scripts\python.exe -m compileall -q tools/saturn/collect_scene_closure.py tools/saturn/test_scene_closure.py tools/saturn/test_bob_scene_closure.py tools/saturn/actor_variant_bank.py tools/saturn/test_actor_variant_bank.py
Exit code: 0
```

The repaired real `bhvMessagePanel` record now seals exactly these sorted
paths for this selected variant:

```text
['actors/wooden_signpost/geo.inc.c',
 'actors/wooden_signpost/model.inc.c',
 'data/behavior_data.c',
 'include/model_ids.h',
 'levels/scripts.c']
```

Strict compilation no longer raises missing Gfx. It advances to the next
honest named boundary:

```text
UnsupportedActorSourceError: unsupported GeoLayout node: GEO_SHADOW
```

Historical encoder identity remains exact:

```text
mario-actor-bank.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
mario.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
task3-refactor.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
task3-refactor.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
```

#### Changed files, self-review, and open gates

Behavior commit `1274c08b` changes exactly
`tools/saturn/collect_scene_closure.py`,
`tools/saturn/test_scene_closure.py`,
`tools/saturn/test_bob_scene_closure.py`, `CHANGELOG.md`, `STATE.md`,
`ROADMAP.md`, the active implementation plan, and the tracked Task 16 ledger.
Evidence commit `420d0223` changes only the plan and ledger. This ignored
report is not staged.

Scoped self-review confirmed root provenance remains strict and singular,
resolution prefers the referring source then requires a globally unique
definition, every returned path is hash-bound before downstream use,
unterminated/malformed commands and non-literal reachability fail named,
recursion is rejected, output ordering is deterministic, Task 4 files are
untouched, and no unrelated dirt was staged, reset, cleaned, checked out, or
stashed.

Independent scoped rereview remains open. `GEO_SHADOW` is the next real BOB
unsupported-semantics boundary and is not silently omitted or broadened here.
Task 4 S64F work remains paused with zero edits. Target/P2/Ymir/map,
whole-game/capacity, registry, cart/workspace, Task 16 Tasks 2-5, feature-off,
transition, Task 9 rebuild/reseal, Task 10 smoke/visual/desktop/owner-manual,
release, and total-game gates remain open and unclaimed.

### Source-provenance repair round 5 — C0/I3/M0 review repair

The scoped round-4 rereview returned NEEDS FIXES with C0/I3/M0. Inspection
confirmed all three Important findings against behavior commit `1274c08b`:

1. `_reached_actor_sources` handled only direct/branch display lists, Vtx, and
   light forms; every other coverage-tokenized Gfx command fell through. A
   repository-valid `gsSPBranchLessZraw(child_dl, 0, 0)` therefore omitted its
   reached child source.
2. `_actor_asset_definition_index` was `lru_cache`-keyed only by `root`, so a
   same-process source edit or new duplicate definition did not enter the
   selection snapshot.
3. recursive `visit` had cycle detection but no acyclic depth bound; a
   1,200-list chain exhausted the Python stack before a domain error.

Four tests were added before production edits. Their expectations are literal
and consumer-visible: the exact branch target path must be in the record, an
unmodeled command carrying a real indexed Gfx symbol must raise the named
domain error, a duplicate written after a priming collection must be observed,
and depth 1,200 must raise bounded `ClosureError` rather than
`RecursionError`.

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
    test_scene_closure.SceneClosureTest.test_branch_less_zraw_reached_gfx_source_is_sealed \
    test_scene_closure.SceneClosureTest.test_unmodeled_reference_bearing_gfx_macro_fails_closed \
    test_scene_closure.SceneClosureTest.test_actor_definition_index_refreshes_after_same_process_change \
    test_scene_closure.SceneClosureTest.test_deep_acyclic_display_list_chain_fails_bounded -v
branch-less source: FAIL (actors/branch_z/model.inc.c absent)
unmodeled reference-bearing command: FAIL (ClosureError not raised)
same-process duplicate: FAIL (ClosureError not raised)
deep acyclic chain: ERROR (RecursionError; previous line repeated 968 times)
Ran 4 tests in 4.701s
FAILED (failures=3, errors=1)
```

The minimal fixes are confined to authoritative closure generation. Exact
three-argument `gsSPBranchLessZraw` targets now walk as Gfx dependencies; an
otherwise unmodeled Gfx command that carries any indexed GeoLayout/Gfx/Vtx/
`Lights1` symbol raises `ClosureError` rather than omitting it. The unsafe
process cache was removed: one fresh deterministic definition index is built
per top-level collection and shared across its records. Recursive traversal
now has a fixed depth-256 bound and raises
`reached actor asset traversal depth limit exceeded: 256`. Closure schema,
public ABI, singular root `geo_source`, downstream closure-only lookup, and
Task 4 files are unchanged.

Focused GREEN after each isolated fix and then together:

```text
cache refresh: Ran 1 test in 0.095s, OK
branch/raw + unknown reference: Ran 2 tests in 0.137s, OK
depth bound: Ran 1 test in 1.188s, OK
all four: Ran 4 tests in 1.426s, OK
```

Complete pre-commit regression evidence:

```text
tools/saturn/test_scene_closure.py: Ran 28 tests in 3.693s, OK
tools/saturn/test_bob_scene_closure.py: Ran 2 tests in 66.449s, OK
actor variant/source: Ran 28 tests in 4.418s, OK
rigid groups: Ran 25 tests in 0.002s, OK
historical generic-family report: Ran 4 tests in 62.336s, OK
native-root variant/pose/meshlet Make wave: exit 0
compileall: exit 0
```

The real BOB signpost record still seals
`actors/wooden_signpost/model.inc.c`; full generic-family compilation retains
explicit unsupported records. Exact historical artifacts remain:

```text
mario-actor-bank.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
mario.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
task3-refactor.json size=562096 sha=3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0
task3-refactor.s64b size=596896 sha=242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539
```

Behavior commit `416c3a34` (`fix(saturn): bound actor source reachability`)
changes exactly `tools/saturn/collect_scene_closure.py`,
`tools/saturn/test_scene_closure.py`, `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`,
the active plan, and the tracked Task 16 ledger. The evidence commit is
`1f4dccd5` (`docs(saturn): record bounded provenance repair`) and changes only
the plan and ledger. Final-code reruns were 28/28 closure in 3.615s, 2/2 real BOB in
64.508s, 28/28 variant/source in 4.164s, 25/25 rigid-group in 0.002s, 4/4
generic-family in 61.632s, plus successful Make, compileall, exact hashes, and
scoped diff check. Scoped rereview remains open;
real BOB `GEO_SHADOW`, Task 4, Task 16 Tasks 2-5, target/P2/Ymir/map,
whole-game/capacity, release/reseal, smoke/visual/desktop/owner-manual, and
total-game gates remain open and unclaimed.

#### Same round-5 residual rereview repair

The same reviewer kept round 5 open with I2 after `416c3a34`/`1f4dccd5`.
The first residual was structural: the fallback classified a Gfx reference
only when an argument token already existed in the current definition index.
Thus valid `gsSPBranchLessZ`/`gsSPBranchLessZrg` commands were rejected only
when their target happened to exist, while missing or computed targets were
silently ignored. The second residual was the older
`_asset_root_source(root, symbol)` `lru_cache`, which hid a duplicate
`parent_geo` added after a priming collection.

Tests preceded production edits. Exact RED:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
    test_scene_closure.SceneClosureTest.test_branch_less_z_and_zrg_reached_gfx_sources_are_sealed \
    test_scene_closure.SceneClosureTest.test_branch_less_z_missing_computed_and_arity_fail_closed \
    test_scene_closure.SceneClosureTest.test_scalar_and_state_identifiers_are_not_source_references \
    test_scene_closure.SceneClosureTest.test_asset_root_lookup_refreshes_after_same_process_change -v
valid Z/Zrg sealing: ERROR (unsupported reference-bearing command because target existed)
missing Z target: FAIL (ClosureError not raised)
computed Z target: FAIL (ClosureError not raised)
wrong Z arity: FAIL (ClosureError not raised)
scalar/state identifiers: ok
same-process duplicate parent_geo: FAIL (ClosureError not raised)
Ran 4 tests in 0.477s
FAILED (failures=4, errors=1)
```

A fifth RED used repository-valid `gsSPMatrix(missing_mtx, flags)` to prove a
known but unsupported source-address form must fail from command semantics,
even when its target is absent:

```text
Ran 1 test in 0.086s
FAILED (failures=1; ClosureError not raised)
```

The correction replaces indexed-token guessing with explicit command specs.
The table gives every supported Geo/Gfx/Vtx/`Lights1` Fast3D source-bearing
form its exact target type, argument position, arity, and accepted expression.
All raw/scaled/region branch-Z variants now resolve and seal their Gfx target;
missing targets reach normal named resolution, and computed or malformed forms
fail expression validation. A separate explicit standard-command table names
source-address forms outside Task 3's approved source types (`gsSPMatrix`,
viewport/look-at/ucode/DMA/unsupported light-count forms, and related RSP
forms), which fail named without inspecting argument spelling. Scalar/state
identifiers are never treated as references by heuristic.

The root-symbol process cache is removed. A fresh bounded root/animation
definition inventory is built once per top-level collection and shared only
inside that collection, matching the already-fresh actor definition snapshot.
A direct no-cache implementation was semantically green but made the full BOB
test exceed its 180-second command timeout after the first test passed; the
per-collection inventory restores bounded performance without weakening
freshness.

Focused GREEN:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest [five residual cases] -v
Ran 5 tests in 0.423s
OK
```

Complete continuation GREEN:

```text
tools/saturn/test_scene_closure.py: Ran 32 tests in 3.828s, OK
tools/saturn/test_bob_scene_closure.py: Ran 2 tests in 25.501s, OK
actor variant/source: Ran 28 tests in 4.105s, OK
rigid groups: Ran 25 tests in 0.002s, OK
historical generic-family report: Ran 4 tests in 19.255s, OK
native-root variant/pose/meshlet Make wave: exit 0
compileall: exit 0
```

Mario remains exactly 562,096-byte JSON at
`3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`
and 596,896-byte S64B at
`242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`;
the refactor copies match exactly. The real wooden-signpost model source stays
sealed, the depth-256 bound remains, schema/public ABI/downstream closure-only
selection are unchanged, and Task 4 still has zero edits.

Continuation behavior commit `56155516`
(`fix(saturn): classify actor source references`) changes exactly
`tools/saturn/collect_scene_closure.py`, `tools/saturn/test_scene_closure.py`,
`CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, the active plan, and the tracked Task
16 ledger. Evidence commit `4bbed7c7`
(`docs(saturn): record semantic provenance repair`) changes only the plan and
ledger. Same-reviewer scoped
rereview, real BOB `GEO_SHADOW`, Task 4, Task 16 Tasks 2-5, all target/release/
manual gates, and total-game completion remain open.

#### Same round-5 exhaustive-command continuation

The same reviewer retained one Important after `56155516`/`4bbed7c7`:
commands absent from both explicit reference tables still reached the end of
the Gfx loop without a decision. The prior indexed-symbol heuristic was gone,
but the replacement was not exhaustive.

TDD RED used the same entirely unmodeled macro with three independent target
shapes. A known scalar/state fixture used only compiler-modeled
`gsSPSetGeometryMode` and `gsDPSetCombineMode` and remained green, constraining
the repair against token guessing:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
    test_scene_closure.SceneClosureTest.test_entirely_unmodeled_gfx_commands_fail_closed \
    test_scene_closure.SceneClosureTest.test_scalar_and_state_identifiers_are_not_source_references -v
indexed gsSPUnhandledReference(parent_child_dl): FAIL (ClosureError not raised)
missing gsSPUnhandledReference(missing_dl): FAIL (ClosureError not raised)
computed gsSPUnhandledReference(select_parent_dl(1)): FAIL (ClosureError not raised)
scalar/state fixture: ok
Ran 2 tests in 0.271s
FAILED (failures=3)
```

The final boundary adds `_ACTOR_GFX_NON_REFERENCE_COMMANDS`, close-ported from
the exact command vocabulary handled by
`actor_variant_bank._Fast3DCompiler`: triangles, pop-matrix, pipe/end, and its
typed S64B-v1-unrepresentable render states. An instrumented closure-only BOB
walk then identified six additional reached scalar states (`gsDPSetCycleType`,
`gsDPSetDepthSource`, `gsDPSetFogColor`, `gsDPSetRenderMode`,
`gsSPFogPosition`, and `gsSPNumLights`); they are explicitly source-address-
free and were added individually. No macro name or argument is inferred from
tokens. Commands absent from the supported-reference table, unsupported-
reference table, and non-reference/state allowlist now raise
`unknown reached Gfx command <macro>` unconditionally.

Focused GREEN:

```text
Ran 2 tests in 0.196s
OK
```

The first real BOB run correctly exposed missing `gsDPSetCycleType` in the
initial narrow allowlist (2 errors in 14.365s). The full reached-command trace
produced the six exact scalar additions above; final GREEN is:

```text
tools/saturn/test_scene_closure.py: Ran 33 tests in 3.905s, OK
tools/saturn/test_bob_scene_closure.py: Ran 2 tests in 24.903s, OK
actor variant/source: Ran 28 tests in 4.475s, OK
rigid groups: Ran 25 tests in 0.002s, OK
historical generic-family report: Ran 4 tests in 19.195s, OK
native-root variant/pose/meshlet Make wave: exit 0
compileall: exit 0
```

Mario JSON/S64B and refactor artifacts retain the exact sizes and SHA-256
values recorded above. The wooden-signpost model source, fresh inventories,
semantic branch/reference handling, depth-256 bound, singular `geo_source`,
schema/public ABI, and downstream closure-only selection are unchanged. Task 4
still has zero edits.

Final continuation behavior commit `2d8c4479`
(`fix(saturn): reject unknown actor source commands`) changes exactly the two
closure source/test files, `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, the active
plan, and tracked Task 16 ledger. Evidence commit `d902438e`
(`docs(saturn): record exhaustive actor command evidence`) changes only the
plan and ledger. Same-reviewer
rereview, real BOB `GEO_SHADOW`, Task 4, Task 16 Tasks 2-5, target/release/
manual, and total-game gates remain open.

### Final same-reviewer provenance verdict

The complete round-5 repair passed its final same-reviewer rereview with
C0/I0/M0. The reviewer independently confirmed unconditional named failure for
indexed, missing, and computed unknown Gfx commands; exact Z/Zraw/Zrg child
sealing; missing/computed/wrong-arity rejection; fresh same-process root and
child definition inventories; the depth-256 bound; singular signpost
`geo_source` plus sealed `model.inc.c`; closure-only downstream lookup; exact
Mario artifacts; and all 33/2/28/25/4 focused suites plus Make, compileall, and
diff checks. No schema, public ABI, actor compiler, Makefile, or Task 4 drift
was found. Task 4 is unblocked; `GEO_SHADOW` remains explicit unsupported
evidence rather than a repaired or discarded semantic.

### Real-source tail-branch repair round 6 — active

After the provenance PASS, Task 4 correctly retained the signpost
`GEO_SHADOW` as an unsupported row and continued. The next supported real BOB
key, `bhvExplosion` / model `0x00cd`, failed because
`explosion_seg3_dl_03004298` ends in a valid unconditional
`gsSPBranchList(explosion_seg3_dl_03004208)` with no
`gsSPEndDisplayList`. Task 4 made zero edits. Task 3 is reopened only for exact
terminal tail-transfer semantics with strict suffix, source, cycle, and depth
checks; RED/GREEN and scoped rereview remain open.

### Real-source tail-branch repair round 6 — source-complete

Base is docs-only operational stop `0e2f03b9`. Reference reuse remains wholly
in-tree: `actor_variant_bank.py` retains its close-port of the selected-source
Fast3D walk and `dl_rigid_groups.py` remains the directly reused structural
contract. The narrow correction adds no external source, schema field, public
ABI, sourceboot wiring, or S64B material representation.

Pre-production focused RED:

```text
> cd tools/saturn
> ..\..\.venv-saturn-tools\Scripts\python.exe -m unittest [four tail-branch ActorVariantBankTest methods] [two tail-branch DisplayListRigidGroupTests methods] -v
Ran 6 tests in 0.231s
FAILED (failures=3, errors=5)
```

The valid real-shaped list was rejected for lacking a final End; structural
walking returned no downstream triangle. Two suffix variants escaped the
malformed contract into unknown-state errors, while the rigid walker accepted
suffix/computed forms. Missing, duplicate, cycle, and the 300-link chain could
not reach their named assertions because the outer tail list failed first.

The repair recognizes `gsSPBranchList(target)` only as the sole final
terminator. It coverage-validates exact arity and a bare C identifier, resolves
the target through `_SourceIndex`, and recursively collects it under the same
cycle and fixed depth-256 limits. Fast3D execution and rigid-group analysis
walk the child using inherited state and return immediately from the parent;
`gsSPDisplayList` retains call/return semantics. Ordinary lists still require
one final zero-argument `gsSPEndDisplayList()`.

Focused GREEN:

```text
Ran 6 tests in 1.600s
OK
```

The exact tail fixture pins 358 payload bytes, lane/scratch 104/211, payload
SHA-256 `216112f7f8b59aeeefe15b86845f3aecfd4caf267f9a63a8d3d01663cbd944e2`,
source SHA-256 `5336a502acfb2f85072367ae5d0f5cef58f8958b91c54d58d6f31eebe47aa7bd`,
four exact vertices, RGB `[31,16,8]`, one `[0,1,2,3]` primitive, and neutral
pose channels. Mutations cover End/state suffixes, wrong/empty/computed target,
ordinary missing End, missing and duplicate selected definitions, a cycle,
and a 300-list acyclic chain.

Complete pre-commit GREEN:

```text
actor variant/source: Ran 32 tests in 6.580s, OK
rigid groups: Ran 27 tests in 0.003s, OK
tools/saturn/test_scene_closure.py: Ran 33 tests in 4.488s, OK
tools/saturn/test_bob_scene_closure.py: Ran 2 tests in 26.587s, OK
historical generic-family report: Ran 4 tests in 20.593s, OK
native-root variant/pose/meshlet Make wave: exit 0 (32 Python tests)
compileall: exit 0
```

The real closure-selected `(family 4, model 0x00cd)` record no longer raises a
false terminator error and advances to the deliberately unchanged boundary:

```text
UnsupportedActorSourceError: unsupported rigid-group source: textured
```

Historical outputs remain exact: 562,096-byte Mario JSON and its refactor copy
hash to `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`;
596,896-byte Mario S64B and its refactor copy hash to
`242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
Task 4 files remain unmodified. Behavior commit and final committed-HEAD
evidence are recorded in the following transition; scoped same-reviewer
rereview, Task 4, Task 16 Tasks 2-5, target/release/manual, and total-game gates
remain open.
