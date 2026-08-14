# Hermetic Full-Game Release Identity Design

> **Historical design record (superseded 2026-08-13).** It may explain existing code, but it does not authorize new work or define product progress. Current authority is `docs/saturn/PRODUCT_GOAL.md` and the active recovery plan.

**Date:** 2026-08-10

**Status:** Approved architecture; implementation not started

**Scope:** Saturn build identity, release sealing, target profiles, package aggregation, and audit binding

**Supersedes:** No historical contract. Identity v1 and native-math audit v2/v3 remain immutable historical formats.

## 1. Purpose

The current sourceboot identity is intentionally conservative: it recursively hashes broad repository roots, including `tools/saturn`. That made early target proofs easy to bind, but it also makes the executable identity depend on verifier tests, capture tooling, evidence files, and other inputs that cannot affect the linked Saturn program. The resulting identity is not hermetic with respect to the actual build target.

This design replaces that broad repository hash for new builds with a layered, target-specific release identity. It supports both immediate goals:

1. produce a newly sealed, fully integrated BOB demo target that can proceed through exact native-math audit, packaging, smoke, visual, and manual-play gates; and
2. use the same identity and packaging architecture for the eventual complete Saturn port, where all shipped levels, actors, animations, audio, textures, and shared data are aggregated into one full-game target profile.

The design changes build and proof infrastructure only. It does not claim that the full game's content, renderer, gameplay systems, or platform integrations are complete.

## 2. Problem statement

The current identity bootstrap computes `source_hash` from a recursive union of broad roots. Because `tools/saturn` is among those roots, changes to audit scripts, tests, capture programs, or ignored local evidence can change the identity even when all compiler inputs and content packages are byte-identical. A fresh build therefore cannot reproduce the previously sealed identity after proof tooling evolves.

The immediate blocked target demonstrates the failure mode:

- all scalar configuration values and feature bits match the sealed target;
- all nine package/artifact hashes other than `source_hash` match;
- only `source_hash` differs;
- the changed bytes are in proof tooling and local capture material, not the target runtime.

Repinning the existing v3 audit to a newly noisy identity would preserve the defect and would destroy the value of v3 as an immutable record. Shipping the stale artifact would bind later smoke evidence to a target that cannot be reproduced from the current tree. Neither is acceptable.

## 3. Design principles

### 3.1 Identity follows the target closure

An executable build identity must be derived from the exact runtime inputs that can affect that executable: compiled sources, included headers, linker and build recipes, code/data generators, generated inputs, effective compiler configuration, and selected content packages.

Repository files that cannot affect the target—tests, capture tools, audit scripts, documentation, evidence, and unrelated untracked files—must not alter the executable identity.

### 3.2 Provenance is separate from executable identity

The release must still record where and how it was built. Git revision, dirty state, compiler and binutils versions, Yaul provenance, and build-mode facts belong in a provenance attestation and release manifest. They must not be folded indiscriminately into the runtime source-closure digest.

This separation preserves traceability without making non-input repository noise part of the executable identity.

### 3.3 Historical proof contracts are immutable

Identity v1 and native-math audit contracts v2 and v3 remain readable and valid for the artifacts they originally sealed. New behavior is introduced with identity v2 and audit v4. Existing expected identities, totals, and caller roots are not repinned.

### 3.4 The demo and full game use the same architecture

The BOB demo is a target profile, not a special-case build path. The eventual full game is another target profile selecting a larger package set. Both flow through the same source-closure discovery, identity generation, build verification, artifact sealing, audit, and deployment gates.

## 4. Chosen architecture

The release identity has four layers:

1. **Target profile** — declares the build's identity, effective boot/compiler configuration, and selected content-package manifests.
2. **Build-input identity** — hashes the canonical runtime source closure plus effective configuration and package roots.
3. **Toolchain attestation** — records the external compiler, binutils, SDK/Yaul, and external dependency provenance used to produce the target.
4. **Post-link release manifest** — binds the input identity to the exact ELF, `SOURCE.DAT`, ISO, and CUE bytes that may be tested or deployed.

Content packages remain independently content-addressed. A deterministic package-set root aggregates the selected level, actor, animation, audio, texture, and shared-data manifests for the target profile.

This is preferred over two alternatives:

- A hand-maintained file allowlist is initially faster but is a continuing correctness hazard when a new compiler input is omitted.
- Hashing every tracked repository file is mechanically simple but repeats the current failure by coupling executable identity to unrelated proofs and documentation.

The chosen design derives the closure from the real compiler configuration, verifies it against actual build dependencies, and uses explicit manifests only for the target profile and content packages.

## 5. Target profiles and content aggregation

### 5.1 `saturn-target-profile-v1.json`

A versioned target-profile manifest is the entry point for every releasable build. Its canonical representation contains:

- schema identifier and version;
- stable profile identifier;
- boot and runtime feature configuration;
- compiler and linker configuration inputs;
- ordered package classes and the manifests selected within each class;
- release-mode requirements; and
- output-product declarations.

The first profiles are:

- `sourceboot-bob-demo`: the current integrated BOB slice and its required shared packages;
- `sm64-saturn-full`: the eventual complete shipped content set.

The full-game profile is not considered complete merely because the manifest exists. It becomes deployable only when every required package is present and all full-game acceptance gates pass.

### 5.2 Package-set root

Each content package keeps its own deterministic manifest and digest. The profile aggregates selected packages by canonical tuple:

`(package class, package id, manifest schema, manifest digest)`

Tuples are normalized and sorted by bytewise UTF-8 order before hashing. Duplicate package identifiers within a class, duplicate manifest paths, missing manifests, paths escaping the repository, and case-colliding paths fail closed.

The aggregate package-set root covers every package selected by the profile. Per-class aggregate roots are also retained so diagnostics and existing tooling can identify which content class changed.

The profile must not silently discover additional packages from a directory. Adding content to the full game requires declaring it in the full-game profile or in a manifest transitively selected by that profile.

## 6. Canonical source closure

### 6.1 `saturn-source-closure-v2.json`

After generated assets exist, the build discovers dependencies using the same SH compiler executable, source list, defines, include paths, language mode, and generated-input paths as the real build. The canonical closure manifest records each repository-owned input as:

- repository-relative normalized path;
- SHA-256 of the exact file bytes;
- input class; and
- the build unit or generator that caused the input to enter the closure.

Allowed input classes are:

- `compiled-source`;
- `header`;
- `linker/build-recipe`;
- `generator`;
- `generated-input`.

Records are sorted first by normalized path and then by class. Serialization uses UTF-8, fixed JSON separators, fixed key ordering, and a final newline. Absolute host paths, timestamps, process IDs, and filesystem enumeration order are forbidden.

### 6.2 Discovery and verification

Dependency closure is established in two checks:

1. **Pre-build discovery:** the compiler emits dependency metadata from the exact target flags. Repository-owned dependencies are canonicalized and hashed. Generators, linker scripts, make fragments, and other build recipes that affect output are added explicitly and deterministically.
2. **Post-build verification:** the real compile emits dependency records. The verifier compares the actual set with the sealed set and rehashes every closure input after link.

An actual compiler dependency absent from the sealed closure fails the build. A sealed dependency that is no longer reachable also fails in release mode because it indicates a stale or overbroad closure. Any byte change between discovery and post-link verification fails as a time-of-check/time-of-use violation.

External compiler, SDK, Yaul, system header, and library paths are not serialized as host paths in the source closure. They must be classified under the toolchain attestation. An external dependency that cannot be mapped to a declared toolchain component fails closed.

### 6.3 Derived identity outputs

Generated identity headers, identity blobs, release manifests, and final artifacts are derived outputs and are excluded from their own input closure. The generator source, bootstrap logic, target profile, build recipes, and any template or schema used to produce those outputs are included. This breaks the identity-generation cycle without hiding code that can change the generated identity or target bytes.

## 7. Toolchain attestation

### 7.1 `saturn-toolchain-attestation-v1.json`

The canonical toolchain attestation records:

- SH compiler identity, version, and binary digest;
- assembler, linker, objcopy, and related binutils identities and binary digests;
- Yaul/SDK version or pinned commit and applicable library/header roots;
- digests for external libraries and classified external headers that can affect the output;
- generator runtime identities when they execute outside the repository tool closure; and
- target ABI and relevant deterministic-build settings.

Host-specific install paths are diagnostic metadata outside the canonical digest. Canonical records use component identifiers and content digests.

Toolchain attestation is separate from the runtime source-closure hash, but its digest is embedded in identity v2. This means a toolchain change reseals the build for a legitimate reason while a test-script change does not.

## 8. Embedded identity v2

Identity v2 extends the existing fixed binary identity rather than replacing its concepts. Parsers branch on the version and size fields so historical v1 artifacts remain readable.

### 8.1 Compatibility and fields

The v2 record retains the existing prefix and v1 fields. For v2:

- `source_hash` is the digest of `saturn-source-closure-v2.json`;
- the existing per-class content hash fields contain deterministic per-class aggregate roots;
- `effective_config_hash` is computed from the canonical v2 effective configuration described below; and
- three 32-byte fields are appended:
  - target-profile hash;
  - package-set root hash;
  - toolchain-attestation hash.

With the current 404-byte v1 record, this produces a 500-byte v2 record. The implementation plan must verify the current layout and static assertions before treating that size as final.

### 8.2 Effective configuration v2

The canonical v2 effective configuration contains:

- all existing scalar and feature-bit values that alter runtime behavior;
- source-closure schema and digest;
- target-profile schema and digest;
- package-set schema and root digest;
- toolchain-attestation schema and digest; and
- identity schema/version.

It contains no output artifact hashes, because those do not exist until after link and are bound by the release manifest.

### 8.3 Development and release modes

Development builds may include modified repository-owned closure inputs. Their actual bytes are hashed, producing a distinct and truthful identity.

Formal release mode requires every repository-owned closure input to be tracked and clean relative to the recorded Git revision. Dirty or untracked files inside the closure fail. Dirty or untracked files outside the closure are recorded in noncanonical provenance diagnostics when desired but do not alter or invalidate the executable identity.

## 9. Two-phase build and sealing flow

Every releasable target follows this sequence:

1. Resolve and validate the target profile.
2. Build deterministic content packages and generated assets selected by the profile.
3. Compute per-package manifests, per-class roots, and the aggregate package-set root.
4. Discover the exact compiler dependency closure using the target's real flags.
5. Emit the canonical source-closure and toolchain-attestation manifests.
6. Generate embedded identity v2 from the closure, effective configuration, target profile, package roots, and toolchain attestation.
7. Compile and link the target while emitting actual dependency records.
8. Verify actual dependencies against the sealed closure and rehash all closure inputs.
9. Produce `SOURCE.DAT`, ISO, CUE, and other declared release outputs.
10. Emit the post-link release manifest containing the exact output hashes.
11. Verify artifacts against the release manifest before audit, smoke, visual, manual-play, packaging, or deployment evidence is accepted.

Steps 1–8 form the input-identity phase. Steps 9–11 form the artifact-sealing phase. A failure in either phase produces no releasable target.

## 10. Post-link release manifest

### 10.1 `saturn-release-manifest-v1.json`

The release manifest canonically records:

- schema identifier and version;
- target-profile id and digest;
- embedded identity version and digest;
- effective-configuration digest;
- source-closure, package-set, and toolchain-attestation digests;
- each declared output's normalized release-relative path, byte size, and SHA-256;
- release/development mode;
- reproducibility status; and
- Git revision and closure cleanliness facts as provenance.

Declared outputs include, where produced, the exact ELF, `SOURCE.DAT`, ISO, and CUE used by downstream gates. Timestamps are excluded from the canonical manifest. Human-readable build times may be stored in a separate noncanonical report.

The CUE digest alone does not transitively authenticate the referenced image; every material output is listed and hashed independently.

No downstream test may infer the target from a mutable “latest” path. It must be given a release manifest and verify the selected file bytes against that manifest before executing or inspecting them.

## 11. Native-math audit v4

Native-math audit v4 is a new immutable contract for the new hermetic BOB target. It requires identity v2 and binds:

- expected target-profile id and digest;
- expected identity-v2 digest and effective-configuration digest;
- expected release-manifest digest;
- exact audited ELF size and SHA-256;
- measured native-math total and root count; and
- forbidden caller/callee rules.

The v4 verifier validates the release manifest, exact ELF, embedded identity, and effective configuration before disassembly. It then performs the existing root/caller proof against measured v4 facts. The current total must be measured from the new sealed target; the historical total of 700 is not assumed.

Audit v2 and v3 files, expected constants, and artifacts remain unchanged and continue to validate historically.

Passing v4 proves only the native-math contract for the exact sealed artifact. It does not substitute for the 20,100-frame combined smoke, visual verification, manual play, or broader full-game gates.

## 12. Failure policy

The new path fails closed when any of the following occurs:

- a declared profile, package, source, generated input, or output is missing;
- a path escapes an allowed repository or release root;
- normalized paths collide by case or canonical representation;
- package identifiers or selected manifests are duplicated;
- a package manifest is stale or undeclared content enters the output;
- compiler dependency discovery observes an unclassified external dependency;
- the real compile uses a dependency absent from the sealed closure;
- the release closure contains stale unreachable dependencies;
- any closure byte changes between discovery and post-link verification;
- release mode observes a dirty or untracked file inside the closure;
- identity generation produces an unsupported version or size;
- an output is absent from, or does not match, the release manifest; or
- a downstream audit/test is given bytes that do not match its pinned release manifest.

An unrelated file outside the closure must not change identity or fail a build solely because it is modified or untracked.

## 13. Verification strategy

### 13.1 Unit and mutation tests

Tests mutate every identity field and every manifest class. They demonstrate that:

- changing a compiled source, included header, linker/build recipe, generator, generated input, effective scalar, feature bit, toolchain component, target profile, or selected content package changes its corresponding root and final identity;
- changing audit scripts, capture programs, tests, documentation, evidence, or unrelated untracked files outside the closure does not change identity;
- missing, duplicate, escaping, case-colliding, unclassified, or stale records fail closed;
- v1 and v2 identities parse through explicit version branches; and
- historical v1/v3 fixtures retain their existing meaning.

### 13.2 Reproducibility tests

Two clean builds of the same target profile and toolchain must emit byte-identical:

- target-profile canonical bytes;
- source-closure manifest;
- package manifests and aggregate roots;
- toolchain attestation;
- embedded identity; and
- release manifest structure and identity fields.

If the pinned toolchain is deterministic end to end, exact ELF, `SOURCE.DAT`, ISO, and CUE hashes must also match. If an artifact is not deterministic, the implementation is blocked from release until the nondeterministic field is removed, normalized, or explicitly redesigned; the manifest must not mask it.

### 13.3 Profile-scale tests

The suite covers both:

- the real `sourceboot-bob-demo` profile; and
- a synthetic multi-level full-game profile with multiple packages in every relevant class.

The synthetic full-game fixture proves deterministic ordering, aggregate roots, duplicate rejection, missing-package rejection, and that changing one package changes its class root and the overall package-set root without perturbing unrelated class roots.

### 13.4 Target gates

After host tests and independent review pass:

1. build the new BOB identity-v2 target twice;
2. compare canonical manifests and artifacts;
3. measure and seal audit v4;
4. run the exact-target native-math audit;
5. package the release-manifest-bound target;
6. run the 20,100-frame combined smoke;
7. perform visual verification; and
8. perform manual play on the fully integrated target.

Host-only or narrow tests do not complete target gates.

## 14. Migration sequence

Implementation proceeds in independently reviewed stages:

1. Add target-profile, source-closure, package-set, toolchain-attestation, and release-manifest schemas plus canonicalization/failure tests.
2. Add identity v2 generation and backward-compatible parsing/capture support.
3. Integrate the two-phase build and dependency verification for `sourceboot-bob-demo`.
4. Produce two reproducible target builds and independently review their evidence.
5. Measure the new native-math facts and create immutable audit v4.
6. Resume the blocked packaging/margin task and combined smoke task using the exact release manifest.
7. Complete visual and manual-play gates for the integrated demo.
8. Add and progressively populate `sm64-saturn-full`, using the same package and release contracts for the eventual complete port.

The current v3 target and its evidence remain historical. Migration creates a new target; it does not rewrite the old one.

## 15. Reference-code-first decision

The implementation extends the repository's existing identity bootstrap, build-identity generator, feature-identity parser, package manifests, capture tooling, and Make-based Saturn build. Reuse mode is **in-tree pattern/extension**.

No external reference implementation is required for this design because the relevant binary contract and packaging architecture are project-specific and already exist in-tree. If implementation research identifies a license-compatible upstream dependency-scanning or reproducible-image implementation that fits the target toolchain, its repository, pinned commit, license, files inspected, and reuse mode must be recorded before adaptation.

## 16. Non-goals

This design does not:

- implement or complete full-game content;
- change renderer, gameplay, object-pool, memory-residency, or decimation behavior;
- overhaul package binary formats beyond deterministic manifest aggregation;
- delete or reinterpret identity v1 or audit v2/v3;
- repin a historical proof contract;
- claim manual playability from host tests;
- make the BOB demo a separate architecture from the full game; or
- authorize deployment before the release manifest and all required target gates pass.

## 17. Acceptance criteria

The design is implemented only when all of the following are true:

- tooling, capture, test, documentation, evidence, and unrelated untracked changes outside the target closure do not change the build identity;
- every relevant runtime, header, linker/build-recipe, generator, generated-input, compiler-config, toolchain, profile, or package change reseals its corresponding root;
- historical identity-v1 and audit-v3 artifacts still validate with unchanged contracts;
- the BOB target emits reproducible identity-v2 closure, profile, package-set, toolchain, and release manifests across two clean builds;
- the release manifest matches the exact ELF, `SOURCE.DAT`, ISO, and CUE accepted by downstream gates;
- native-math audit v4 passes with newly measured, independently reviewed facts;
- the actual 20,100-frame combined smoke passes against the same sealed target;
- visual and manual-play evidence are recorded separately and remain unchecked until performed;
- the synthetic multi-level full-game profile proves deterministic multi-package aggregation and fail-closed validation; and
- the architecture can populate `sm64-saturn-full` without introducing a second build-identity or release-sealing path.

## 18. Expected outcome

The immediate result is a target that can be rebuilt, sealed, audited, packaged, smoke-tested, and manually played without its identity drifting because proof tooling changed. The long-term result is a single deployment contract for the entire port: the full game differs from the demo by its declared profile and package set, not by a bespoke proof or packaging pipeline.
