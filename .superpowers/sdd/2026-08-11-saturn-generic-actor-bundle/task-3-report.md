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
