# Hermetic Full-Game Release Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build, independently review, and target-prove a hermetic identity-v2 Saturn release pipeline that produces a fully integrated playable BOB demo now and scales unchanged to the eventual complete-game profile.

**Architecture:** A checked-in target profile selects deterministic content-package descriptors; generated manifests aggregate those packages while a compiler-derived source closure records only inputs that can affect the target. Identity v2 embeds the closure, profile, package-set, effective configuration, and toolchain roots. A post-link release manifest then binds that input identity to the exact ELF, `SOURCE.DAT`, ISO, and CUE used by audit, smoke, manual testing, and deployment staging.

**Tech Stack:** Python 3.11 standard library and `unittest`, GNU Make/MSYS2, SH-2 GCC/binutils, Yaul, Git, headless/desktop Ymir, canonical JSON, SHA-256, Markdown/JSON/PNG evidence.

## Global Constraints

- Starting design commit: `69c83a3f` (`docs(saturn): design hermetic full-game release identity`).
- Preserve identity v1, native-math audit v2, and native-math audit v3 as immutable historical contracts; do not repin their bytes, expected digests, totals, or exact ELF.
- New target builds use identity version 2. The verified v2 layout is the 404-byte v1 prefix plus three 32-byte hashes, for 500 bytes total; Task 4 must prove both sizes with struct and C static assertions before changing the target ABI.
- New exact-target native-math evidence uses audit contract v4. Measure the new total from the newly sealed ELF; never copy the historical total of 700 into v4 without measurement.
- Canonical manifests use UTF-8, sorted keys, separators `(',', ':')`, `ensure_ascii=True`, and exactly one trailing newline. They contain no timestamps, process IDs, absolute host paths, or filesystem enumeration order.
- Canonical repository paths use forward slashes, are relative to the resolved repository root, reject `..`/absolute escapes, and reject case-fold collisions.
- Release builds normalize repository paths in debug/macro metadata with `-ffile-prefix-map=$(ROOT)=.`, `-fdebug-prefix-map=$(ROOT)=.`, and `-fmacro-prefix-map=$(ROOT)=.` so identical commits in different worktree locations can produce identical ELF bytes.
- Derived identity headers/blobs/manifests and final artifacts are excluded from their own closure. Their generators, templates, schemas, Make recipes, linker scripts, and generated inputs remain covered.
- Development mode hashes the actual bytes of dirty closure inputs. Release mode rejects dirty or untracked checked-in closure inputs; deterministic `generated-input` records may live under ignored build directories. Dirty files outside the closure neither reseal nor invalidate the build.
- Missing, duplicate, escaping, case-colliding, stale, unclassified, or time-of-check/time-of-use inputs fail closed. No tool may silently reuse an earlier manifest after validation failure.
- `sourceboot-bob-demo` and future `sm64-saturn-full` go through the same profile, closure, identity, release-manifest, verification, and deployment-staging code paths. The incomplete full-game profile must remain non-releasable until its package inventory is populated and reviewed.
- Reference-code-first mode is in-tree pattern/extension: adapt `bootstrap_sourceboot_identity_spec.py`, `gen_build_identity.py`, existing package manifests, Yaul depfiles, sourceboot Make rules, capture binding, and audit-contract parsing. Record any newly inspected external source with repo, commit, license, files, and reuse mode before adapting it.
- Use test-driven development for every behavior change. Observe the named focused test fail before implementation, then pass after the smallest implementation.
- Each behavior-changing commit updates `CHANGELOG.md` under `[Unreleased]` with the reason, compatibility impact, and failure policy.
- During every task transition update this plan and `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md` with status, commit, tests actually run, independent spec/code-quality verdicts, and open gates. Update `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`, `STATE.md`, and `ROADMAP.md` whenever target or campaign status changes.
- Before starting each task reconcile this plan, the SDD ledger, the memory campaign, and Git HEAD. Preserve every unrelated modified or untracked path and stage only files named by the active task.
- Every task needs independent specification-compliance review followed by independent code-quality review. A task is not `complete` until both verdicts clear or their findings are fixed and rereviewed.
- Host tests, dry-run Make tests, and cross-links are not target evidence. Keep build, reproducibility, audit, package, 20,100-frame smoke, visual, and manual-play gates unchecked until each exact release-manifest-bound gate runs.
- The known unrelated full-verifier failure `test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites` may remain documented; no new or related failure is acceptable.

## File and interface map

- `tools/saturn/hermetic_manifest.py`: canonical JSON, hashing, strict key checks, safe path normalization, case-collision detection, and atomic write-if-changed primitives shared by every new manifest.
- `tools/saturn/target_profile.py`: package-descriptor resolution, per-class aggregate manifests, package-set roots, effective target-profile resolution, release-enabled checks, and CLI emission.
- `tools/saturn/gen_source_closure.py`: Make depfile parsing, repository/external dependency classification, canonical source-closure emission, post-compile equality/rehash verification, and release cleanliness checks.
- `tools/saturn/gen_toolchain_attestation.py`: canonical toolchain component and external-dependency attestation plus verification.
- `tools/saturn/gen_build_identity.py`: backward-compatible identity v1/v2 build, parse, validation, labels, JSON, binary, and C include generation.
- `tools/saturn/bootstrap_sourceboot_identity_spec.py`: sourceboot-specific composition of resolved profile, package roots, source closure, toolchain attestation, effective flags, and identity-v2 spec.
- `tools/saturn/release_manifest.py`: exact output sealing and verification for ELF, `SOURCE.DAT`, ISO, and CUE.
- `tools/saturn/stage_saturn_release.py`: profile-agnostic, verify-before-copy deployment staging into a new empty directory.
- `src/port/saturn/sourceboot/Makefile` and `Makefile.saturn.mk`: assets → dependency discovery → identity seal → compile/link → closure verify → artifact seal orchestration.
- `tools/saturn/capture_sourceboot_throughput.py` and `tools/saturn/capture_object_pool_occupancy.py`: backward identity parsing plus mandatory release-manifest binding for new target evidence.
- `tools/saturn/verify_sh2_native_math.py`: immutable audit-v4 parsing, release/ELF/identity preflight, measurement report, and exact-target verification.

---

### Task 1: Canonical manifests, target profiles, and package-set aggregation

**Execution status (2026-08-10):** `complete` from Task 1 dispatch base
`da15bd4857691fde83d0808400aca2c6ea715144`; source commit `0f01c14d` and repair
commits `b8f6f5e8` / `b345dd15` passed the focused host contract with
`.\\.venv-saturn-tools\\Scripts\\python.exe tools\\saturn\\test_target_profile.py`
(11 tests after repair) and scoped `git diff --check`. The canonical profile binds only
descriptor-selected measured payloads; the BOB profile carries the accepted
Task 5 flag tuple, while `sm64-saturn-full` declares every class but is
intentionally non-release-enabled with no invented game-inventory assertion.
The first independent combined specification/code-quality review returned
`Needs fixes`; the repairs record closure-wide exact duplicate and
case-fold-colliding payload rejection, pre-publication descriptor/payload
remeasurement, and relative output-name escape rejection with focused tests.
Scoped rereview found every finding addressed with no new Critical/Important
breakage, clearing both Task 1 review gates. Target build, reproducibility,
audit, complete package, 20,100-frame smoke, visual, and manual-play gates
remain open.

**Files:**
- Create: `tools/saturn/hermetic_manifest.py`
- Create: `tools/saturn/target_profile.py`
- Create: `tools/saturn/test_target_profile.py`
- Create: `tools/saturn/profiles/sourceboot-bob-demo-v1.json`
- Create: `tools/saturn/profiles/sm64-saturn-full-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/route-bob-parity-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/input-sourceboot-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/camera-sourceboot-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/cart-sourceboot-32mbit-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/level-bob-area1-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/shared-bob-dependencies-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/actor-mario-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/animation-mario-source-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/audio-stub-v1.json`
- Create: `tools/saturn/manifests/sourceboot-bob-demo/texture-bob-v1.json`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Create/modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Produces: `canonical_json_bytes(document: Mapping[str, Any]) -> bytes`, `sha256_file(path: Path) -> str`, `normalize_repo_path(root: Path, value: str | Path) -> str`, `write_if_changed(path: Path, data: bytes) -> None`.
- Produces: `ResolvedTargetProfile(document: dict[str, Any], canonical: bytes, sha256: str, package_class_manifests: dict[str, Path], package_class_hashes: dict[str, str], package_set_document: dict[str, Any], package_set_canonical: bytes, package_set_sha256: str)`.
- Produces: `resolve_target_profile(root: Path, profile_path: Path, effective_config: Mapping[str, int], output_dir: Path, mode: Literal['development', 'release']) -> ResolvedTargetProfile`.
- Package classes are `route`, `input`, `camera`, `cart`, `level`, `shared-data`, `actor`, `animation`, `audio`, and `texture`. The first nine map to the legacy identity fields; `texture` is covered by the v2 package-set root because v1 has no texture-specific field.

- [x] **Step 1: Create the SDD ledger and record the reconciled starting point**

Record design commit `69c83a3f`, implementation-plan commit `15289cd6`, the actual Task 1 implementation base at dispatch, the pre-existing blocked Task 3 plan annotations, the unrelated dirty paths, Task 1 status `active`, and every open target gate. Do not stage another campaign's SDD files.

- [x] **Step 2: Write failing canonicalization, path, profile, and package tests**

Add tests with these behaviors:

```python
def test_canonical_json_has_fixed_bytes(self) -> None:
    self.assertEqual(
        canonical_json_bytes({"z": 1, "a": "é"}),
        b'{"a":"\\u00e9","z":1}\n',
    )

def test_normalize_repo_path_rejects_escape_and_case_collision(self) -> None:
    with self.assertRaisesRegex(ValueError, "escapes repository"):
        normalize_repo_path(self.root, "../outside")
    with self.assertRaisesRegex(ValueError, "case-colliding"):
        reject_case_collisions(["levels/bob/data.bin", "LEVELS/BOB/data.bin"])

def test_unrelated_tool_change_does_not_reseal_demo_profile(self) -> None:
    first = self.resolve_demo()
    (self.root / "tools/saturn/unselected_capture.py").write_text("changed\n")
    second = self.resolve_demo()
    self.assertEqual(first.canonical, second.canonical)
    self.assertEqual(first.package_set_sha256, second.package_set_sha256)

def test_synthetic_full_profile_aggregates_multiple_packages_deterministically(self) -> None:
    first = resolve_target_profile(
        self.root, self.synthetic_full_profile(order="reverse"),
        self.config, self.output / "first", mode="development",
    )
    second = resolve_target_profile(
        self.root, self.synthetic_full_profile(order="forward"),
        self.config, self.output / "second", mode="development",
    )
    self.assertEqual(first.package_set_canonical, second.package_set_canonical)
    self.assertEqual(len(first.package_set_document["packages"]), 16)

def test_incomplete_full_game_profile_cannot_release(self) -> None:
    with self.assertRaisesRegex(ValueError, "sm64-saturn-full.*release-enabled"):
        resolve_target_profile(
            ROOT, ROOT / "tools/saturn/profiles/sm64-saturn-full-v1.json",
            self.config, self.output, mode="release",
        )
```

The synthetic profile contains two packages in each shipped-content class (`level`, `shared-data`, `actor`, `animation`, `audio`, `texture`) and one package in each control class (`route`, `input`, `camera`, `cart`), for 16 packages. Mutate one package in each class and assert only its class aggregate plus the overall package-set root changes. Reject missing payloads, duplicate class/id tuples, duplicate manifest paths, unknown keys, invalid schema, non-integer config values, absolute paths, escaping paths, and case-fold collisions.

- [x] **Step 3: Run the focused tests and observe RED**

Run:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_target_profile.py
```

Expected: import failure for absent `hermetic_manifest` or `target_profile`.

- [x] **Step 4: Implement the canonical manifest primitives**

Use these exact serialization and path rules:

```python
def canonical_json_bytes(document: Mapping[str, Any]) -> bytes:
    return (
        json.dumps(document, sort_keys=True, separators=(",", ":"),
                   ensure_ascii=True) + "\n"
    ).encode("ascii")

def normalize_repo_path(root: Path, value: str | Path) -> str:
    root = root.resolve()
    candidate = (root / value).resolve() if not Path(value).is_absolute() else Path(value).resolve()
    try:
        relative = candidate.relative_to(root)
    except ValueError as error:
        raise ValueError(f"path escapes repository: {value}") from error
    rendered = relative.as_posix()
    if not rendered or rendered == ".":
        raise ValueError("repository input path is empty")
    return rendered
```

`write_if_changed()` must write a sibling `.tmp`, flush/close it, and replace the destination only after complete bytes exist. Remove the temporary file on failure without touching a valid previous destination.

- [x] **Step 5: Implement package descriptor and profile resolution**

Package descriptors use this checked-in shape:

```json
{"inputs":[{"path":"build/saturn/sourceboot/generated/bob_area1_compiled.json"}],"package_class":"level","package_id":"bob-area1","schema":"sm64-saturn-package-descriptor-v1"}
```

Resolved package manifests are constructed from measured bytes, never typed
hashes:

```python
resolved_manifest = {
    "schema": "sm64-saturn-package-manifest-v1",
    "package_class": descriptor["package_class"],
    "package_id": descriptor["package_id"],
    "source_descriptor_sha256": sha256_file(descriptor_path),
    "inputs": [{"path": relative, "sha256": sha256_file(root / relative)}],
}
```

Implement deterministic tuple sorting:

```python
package_rows.sort(key=lambda row: (
    row["package_class"].encode("utf-8"),
    row["package_id"].encode("utf-8"),
    row["manifest_sha256"].encode("ascii"),
))
```

The resolved profile contains schema, profile id, release-enabled flag, exact effective config, sorted package-manifest records, output names `elf`, `source_dat`, `iso`, `cue`, and no source-machine paths. Release mode requires `release_enabled: true` and exact equality with the profile's `release_config`.

- [x] **Step 6: Add the real BOB descriptors and both checked-in profiles**

The BOB profile locks the accepted Task 5 flag tuple from the previous plan: demo/replay/live input enabled, 600 bootstrap ticks, level 9 area 1 route 0, camera route 0 variant 3, 32-Mbit cart, 8 staging sectors, pipe 4, hot/near/BSP enabled, polygon tier 2, complete Mario animation and dynamic actor closure enabled, semantic audio disabled, and object-pool capacity 208.

```json
{"atan2_variant":2,"bootstrap_ticks":600,"bsp_fragment_flat":0,"bsp_order":1,"camera_idle_discovery":0,"camera_idle_start_tick":0,"camera_range_capture":0,"camera_route":0,"camera_variant":3,"cart_mbit":32,"cart_stage_sectors":8,"demo_path":1,"demo_view_radius":6000,"diagnostic_mode":0,"experimental_skip_geo_walk":0,"fast3d_q16_trace":0,"features.complete_mario_animation":1,"features.dynamic_actor_closure":1,"features.semantic_audio":0,"fragment_mode":0,"hot_promotion":1,"level_id":9,"live_input_mode":1,"near_clip":1,"object_pool_capacity":208,"polygon_tier":2,"renderer_pipeline":4,"route_id":0,"route_replay_mode":1,"slave_render":1,"area_id":1}
```

The full-game profile has `profile_id: "sm64-saturn-full"`, `release_enabled: false`, all ten package classes declared, and no invented assertion that the missing game inventory is complete. Its release rejection is part of the contract.

- [x] **Step 7: Run focused tests and inspect deterministic outputs**

Run:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_target_profile.py
git diff --check -- tools/saturn/hermetic_manifest.py tools/saturn/target_profile.py tools/saturn/test_target_profile.py tools/saturn/profiles tools/saturn/manifests/sourceboot-bob-demo
```

Expected: all tests pass; no whitespace errors.

- [x] **Step 8: Update changelog and ledgers, commit, and obtain both reviews**

Mark Task 1 `source-complete`, list the exact test command, and keep all target gates open. Commit only Task 1 files:

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/hermetic_manifest.py tools/saturn/target_profile.py tools/saturn/test_target_profile.py tools/saturn/profiles tools/saturn/manifests/sourceboot-bob-demo
git commit -m "feat(saturn): add deterministic target profiles"
```

Run independent specification-compliance review, then code-quality review. Fix findings with focused RED/GREEN evidence, rereview, record verdicts and commits in the plan/SDD ledger, then mark Task 1 `complete`.

---

### Task 2: Compiler-derived source closure and post-build verification

**Live status (2026-08-10):** `complete` at source commit `11cdef08` plus
release-hardening repair `b43169b4`. Focused TDD RED observed the expected
absent-module import failure before implementation; final focused GREEN passes
14 closure contracts with one legitimate host-capability skip, and the existing
identity-bootstrap regression passes 7 tests. The implementation seals compiler-derived repository inputs,
keeps attestation-bound external paths out of the document, and verifies exact
post-build closure/class ownership plus byte and release-cleanliness drift.
The first independent combined specification/code-quality review returned
`Needs fixes`: release cleanliness must Git-check generated inputs outside
`build/` and must reject ignored/untracked checked-in closure inputs. The scoped
rereview found those and both minor findings addressed with no new breakage,
clearing Task 2's specification and code-quality gates. All target/
reproducibility/audit/package/smoke/visual/manual evidence gates remain open.

**Repair status (2026-08-10):** source-complete after focused RED/GREEN repair
of the first review findings. Release verification now proves every
Git-checked path is tracked before the required scoped porcelain check, and
only generated-input records beneath `build/` bypass Git. The focused suite
passes 14 tests (one case-spelling test is host-capability skipped) and the
bootstrap regression passes 7. Both independent review gates remain open
pending rereview; no target evidence gate is closed.

**Files:**
- Create: `tools/saturn/gen_source_closure.py`
- Create: `tools/saturn/test_gen_source_closure.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: Task 1 canonical manifest and safe-path helpers.
- Produces: `parse_make_depfile(text: str) -> tuple[str, ...]`.
- Produces: `ClosureBuild(document: dict[str, Any], canonical: bytes, sha256: str, external_dependencies: tuple[Path, ...])`.
- Produces: `build_source_closure(root: Path, compiled_sources: Sequence[Path], depfiles: Sequence[Path], recipe_inputs: Sequence[Path], generator_inputs: Sequence[Path], generated_inputs: Sequence[Path], derived_outputs: Sequence[Path], external_roots: Sequence[Path]) -> ClosureBuild`.
- Produces: `verify_source_closure(root: Path, sealed_path: Path, actual_depfiles: Sequence[Path], assembly_scan_depfiles: Sequence[Path], derived_outputs: Sequence[Path], external_roots: Sequence[Path], expected_external_dependencies: Sequence[Path], release_mode: bool) -> tuple[Path, ...]` returning the verified external dependencies.

- [x] **Step 1: Write failing depfile, classification, mutation, and cleanliness tests**

```python
def test_depfile_parser_handles_continuations_and_escaped_spaces(self) -> None:
    self.assertEqual(
        parse_make_depfile("obj.o: src/main.c include/a.h \\\n include/with\\ space.h\n"),
        ("src/main.c", "include/a.h", "include/with space.h"),
    )

def test_closure_excludes_derived_identity_outputs_but_includes_generators(self) -> None:
    built = self.build_closure()
    records = {row["path"]: row["class"] for row in built.document["inputs"]}
    self.assertNotIn("build/generated/saturn_build_identity_values.inc", records)
    self.assertEqual(records["tools/saturn/gen_build_identity.py"], "generator")
    self.assertEqual(records["src/main.c"], "compiled-source")
    self.assertEqual(records["include/main.h"], "header")

def test_post_build_rejects_missing_extra_stale_and_toc_tou_inputs(self) -> None:
    sealed = self.write_sealed_closure()
    for mutation, message in self.actual_dependency_mutations():
        with self.subTest(mutation=mutation):
            mutation()
            with self.assertRaisesRegex(ValueError, message):
                verify_source_closure(
                    self.root, sealed, self.depfiles, self.asm_depfiles,
                    self.derived, self.external_roots, self.expected_external,
                    release_mode=False,
                )

def test_release_mode_ignores_dirty_file_outside_closure(self) -> None:
    self.git_init_with_tracked_closure()
    (self.root / "docs/unrelated.md").write_text("dirty\n")
    verify_source_closure(
        self.root, self.sealed, self.depfiles, (), self.derived,
        self.external_roots, self.expected_external, release_mode=True,
    )
```

Also test duplicate/case-colliding records, missing depfiles, malformed multiple-target depfiles, repo escapes, generated-input precedence, unclassified external paths, dirty tracked closure inputs, untracked checked-in closure inputs, and clean ignored `generated-input` records.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_source_closure.py
```

Expected: import failure for absent `gen_source_closure`.

- [x] **Step 3: Implement strict depfile parsing and classification**

Use one record per canonical path and this precedence:

```python
CLASS_PRECEDENCE = {
    "header": 0,
    "compiled-source": 1,
    "linker/build-recipe": 2,
    "generator": 3,
    "generated-input": 4,
}
```

Derived outputs are removed before classification. A path supplied under two explicit classes is an error rather than a precedence decision. Precedence is used only when compiler discovery also sees a path already declared as a generated input, generator, recipe, or compiled source.

The canonical document is:

```python
document = {
    "schema": "sm64-saturn-source-closure-v2",
    "inputs": sorted(records, key=lambda row: (
        row["path"].encode("utf-8"), row["class"].encode("ascii")
    )),
}
```

Each record contains only `path`, `sha256`, `class`, and sorted `owners`. Absolute external paths are returned separately for Task 3 and never serialized here.

- [x] **Step 4: Implement verification and release cleanliness**

Post-build verification must:

1. parse real C/C++ `.d` files and post-rescanned `.sx` dependency files;
2. rebuild the classified dependency set;
3. require exact equality with the sealed record set after derived-output exclusion;
4. rehash every sealed input and reject byte drift;
5. require the actual external dependency set to be exactly the set passed to toolchain attestation; and
6. in release mode, call `subprocess.run(["git", "status", "--porcelain=v1", "--untracked-files=all", "--", *sorted_checked_in_paths], cwd=root, check=False, capture_output=True, text=True)` for checked-in classes and reject a nonzero return code or any output line.

Do not invoke Git for `generated-input` records under `build/`.

```python
sealed_rows = {(row["path"], row["class"]): row for row in sealed["inputs"]}
actual_rows, actual_external = _classify_dependencies(
    root, actual_depfiles, assembly_scan_depfiles, derived_outputs, external_roots
)
if set(actual_rows) != set(sealed_rows):
    raise ValueError(render_closure_difference(sealed_rows, actual_rows))
if tuple(sorted(actual_external)) != tuple(sorted(expected_external_dependencies)):
    raise ValueError("actual external dependency set differs from sealed discovery")
for key, row in sealed_rows.items():
    actual = sha256_file(root / row["path"])
    if actual != row["sha256"]:
        raise ValueError(f"source closure input changed after discovery: {row['path']}")
```

- [x] **Step 5: Run tests and the broader identity bootstrap regression tests**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_source_closure.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py
```

Expected: new focused tests pass; existing bootstrap tests retain their pre-Task-5 behavior.

- [x] **Step 6: Update changelog/ledgers, commit, and clear both reviews**

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/gen_source_closure.py tools/saturn/test_gen_source_closure.py
git commit -m "feat(saturn): derive hermetic source closure"
```

Record tests and open gates, run specification review then code-quality review, fix/rereview findings, and mark Task 2 complete only after both clear.

---

### Task 3: Toolchain attestation and external dependency classification

**Files:**
- Create: `tools/saturn/gen_toolchain_attestation.py`
- Create: `tools/saturn/test_gen_toolchain_attestation.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: Task 1 manifest primitives and Task 2 external dependency paths.
- Produces: `ToolchainComponent(component_id: str, version: str, root: Path, binaries: tuple[Path, ...])`.
- Produces: `AttestationBuild(document: dict[str, Any], canonical: bytes, sha256: str, external_dependency_keys: frozenset[tuple[str, str]])`.
- Produces: `build_toolchain_attestation(components: Sequence[ToolchainComponent], external_dependencies: Sequence[Path]) -> AttestationBuild` and `verify_toolchain_attestation(path: Path, components: Sequence[ToolchainComponent], external_dependencies: Sequence[Path]) -> dict[str, Any]`.

Status: `complete`; the first independent combined specification/code-
quality review returned `Needs fixes` because an unchecked compiler-version
string could serialize an absolute host path. The focused repair rejects
absolute POSIX/Windows paths from programmatic component versions and GCC
stdout before publication, but round 1 rereview found `file:///...` and
`//host/path` bypasses. Repair commits `0b39b46d` and `f63e78d9` reject all
reviewed forms while preserving ordinary relative version metadata; round 2
rereview found the remaining finding addressed with no new breakage, clearing
both review gates. `external_dependency_keys` use
`(component_id, component-relative path)`,
and the `yaul-sh-sdk` version string carries Yaul `0.3.1`, pinned commit
`6012f79f237773378c8014e70d8998ad95a38d98`, and exact GCC `--version` stdout.
This keeps component install roots diagnostic-only and out of canonical bytes.
Source implementation commit: `1ab25c845b1a0c782db9e2aec0d27324763e6017`.
Final focused verification: `test_gen_toolchain_attestation.py` passed 13 tests and
the Task 2 closure regression passed 14 tests with one existing host-only skip.

- [x] **Step 1: Write failing toolchain tests**

```python
def test_install_root_does_not_enter_canonical_attestation(self) -> None:
    first = build_toolchain_attestation(
        [self.component(self.root / "install-a")], self.dependencies("install-a")
    )
    second = build_toolchain_attestation(
        [self.component(self.root / "elsewhere/install-b")], self.dependencies("elsewhere/install-b")
    )
    self.assertEqual(first.canonical, second.canonical)

def test_binary_or_external_header_change_reseals(self) -> None:
    first = self.build()
    for relative in ("bin/sh-elf-gcc.exe", "sh-elf/include/stdint.h"):
        with self.subTest(relative=relative):
            self.mutate(relative)
            second = self.build()
            self.assertNotEqual(first.sha256, second.sha256)
            self.restore(relative)

def test_unclassified_or_ambiguous_external_dependency_fails(self) -> None:
    with self.assertRaisesRegex(ValueError, "unclassified external dependency"):
        build_toolchain_attestation([self.component()], [self.root / "outside/header.h"])
    with self.assertRaisesRegex(ValueError, "matches multiple toolchain components"):
        build_toolchain_attestation(self.overlapping_components(), [self.shared_header])
```

Also reject duplicate component ids, duplicate binary records, missing binaries, stale attestation bytes, case collisions, unknown keys, and invalid lowercase hashes.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_toolchain_attestation.py
```

- [x] **Step 3: Implement canonical component records**

Use the canonical form:

```python
document = {
    "schema": "sm64-saturn-toolchain-attestation-v1",
    "target_abi": "sh2eb-none-elf",
    "components": [{
        "id": component.component_id,
        "version": component.version,
        "binaries": binary_records,
        "dependencies": dependency_records,
    } for component in sorted(components, key=lambda item: item.component_id.encode("utf-8"))],
}
```

Binary and dependency record paths are relative to their component root. Component roots are diagnostics returned to the caller but absent from canonical bytes. For the sourceboot build use one non-overlapping `yaul-sh-sdk` component rooted at `YAUL_INSTALL_ROOT`, versioned with Yaul `0.3.1`, commit `6012f79f237773378c8014e70d8998ad95a38d98`, GCC `--version`, and the SHA-256 of the invoked compiler, assembler, linker driver, `nm`, `objcopy`, `objdump`, `readelf`, and `addr2line` binaries.

- [x] **Step 4: Implement verification and CLI emission**

The CLI accepts repeated `--external-dependency`, explicit tool binary paths, `--yaul-root`, `--yaul-version`, `--yaul-commit`, and `--output`. It validates all input bytes before atomically replacing the prior output. Verification rebuilds canonical bytes from the current components and dependencies and requires byte equality with the sealed file.

```python
built = build_toolchain_attestation(components, args.external_dependency)
if args.verify:
    if args.verify.read_bytes() != built.canonical:
        raise ValueError("toolchain attestation differs from current inputs")
else:
    write_if_changed(args.output, built.canonical)
```

- [x] **Step 5: Run focused tests**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_toolchain_attestation.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_source_closure.py
```

- [x] **Step 6: Update docs, commit, and clear both reviews**

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/gen_toolchain_attestation.py tools/saturn/test_gen_toolchain_attestation.py
git commit -m "feat(saturn): attest release toolchain inputs"
```

Record exact tests, review verdicts, findings/fixes, and remaining target gates before marking Task 3 complete.

---

### Task 4: Backward-compatible embedded identity v2

**Files:**
- Modify: `tools/saturn/gen_build_identity.py`
- Modify: `tools/saturn/test_gen_build_identity.py`
- Modify: `tools/saturn/capture_sourceboot_throughput.py`
- Modify: `tools/saturn/test_sourceboot_feature_identity.py`
- Modify: `tools/saturn/test_capture_sourceboot_throughput.py`
- Modify: `src/port/saturn/platform/saturn_build_identity.h`
- Modify: `src/port/saturn/platform/saturn_build_identity.c`
- Modify: `tools/saturn/test_sourceboot_identity_residency.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: existing v1 spec plus v2 descriptors `target_profile`, `package_set`, and `toolchain_attestation`.
- Produces: `IDENTITY_V1_SIZE = 404`, `IDENTITY_V2_SIZE = 500`, `SUPPORTED_IDENTITY_SIZES = (404, 500)`, and new-build alias `IDENTITY_SIZE = IDENTITY_V2_SIZE`.
- Produces: v2 hash fields `target_profile_hash`, `package_set_root_hash`, `toolchain_attestation_hash` appended after the v1 prefix.
- Produces: identity JSON containing the validated `effective_config` object whose canonical hash equals the embedded `effective_config_hash`, so staged v2 captures do not depend on a build-tree spec path.
- Preserves: `build_identity(spec)`, `parse_identity(raw)`, `validate_identity(raw, expected=None)`, `identity_label(raw)`, `identity_directory_tag(raw)`, and historical v1 parsing.

**Live status (2026-08-10): `complete`.** The first independent combined
specification/code-quality review returned `Needs fixes`: snapshot ELF bytes
once for symbol resolution/extraction, reject ambiguous/unknown v2 descriptor
JSON, and emit canonical CLI manifest bytes. Repair commits `7aef88c4` and
`1582467c` addressed every finding; scoped rereview found no new breakage and
cleared both review gates. TDD preserves the exact
404-byte v1 fixture (`faa7288b4c9fdf90ae14f01ab3af3752649b8e1ca78d77c47033425c9d68b23f`),
proves the v2 size and root offsets at 500 bytes, compiles the C ABI contract,
and exercises 404/500-byte ELF extraction and exact target reads. The final
four focused suites pass 67 tests (21 + 6 + 38 + 2); adjacent bootstrap and
object-pool capture regressions pass 13 more (7 + 6). Independent specification
and code-quality reviews are approved. Target build,
reproducibility, audit v4, complete-package, 20,100-frame smoke, visual, and
manual-play gates remain open; host tests do not close them.
Implementation commit: `09c30c23` (`feat(saturn): add build identity v2`).

Repair round 1 is review-cleared. The
repair snapshots the ELF once for both symbol and PT_LOAD parsing, requires
exact v2 root descriptor keys while retaining v1 programmatic compatibility,
rejects duplicate/case-fold-colliding keys recursively at the CLI JSON
boundary, and emits compact canonical manifest bytes. The focused suites now
pass 67 tests (21 + 6 + 38 + 2), with 13 adjacent regressions (7 + 6) also
passing. Repair commit: `7aef88c4` (`fix(saturn): harden identity v2 inputs`).
All target evidence gates remain open.

Design decision: the legacy `IDENTITY_STRUCT` and `HASH_FIELDS` aliases remain
v1-compatible, while explicit v1/v2 structs select parsing from the immutable
`>IHH` version/size prefix and the new-build `IDENTITY_SIZE` alias is 500. V2
JSON is self-contained only after canonical effective-config rehash succeeds.

- [x] **Step 1: Write failing v1/v2 layout and mutation tests**

```python
def test_v1_fixture_remains_404_bytes_and_parses(self) -> None:
    built = identity.build_identity(self.v1_spec)
    self.assertEqual(len(built.raw), 404)
    self.assertEqual(identity.parse_identity(built.raw)["version"], 1)

def test_v2_extends_v1_prefix_to_exactly_500_bytes(self) -> None:
    built = identity.build_identity(self.v2_spec)
    parsed = identity.parse_identity(built.raw)
    self.assertEqual(len(built.raw), 500)
    self.assertEqual(parsed["version"], 2)
    self.assertEqual(parsed["size"], 500)
    self.assertEqual(parsed["target_profile_hash"], self.profile_sha256)
    self.assertEqual(parsed["package_set_root_hash"], self.package_set_sha256)
    self.assertEqual(parsed["toolchain_attestation_hash"], self.toolchain_sha256)

def test_v2_effective_config_covers_all_new_roots(self) -> None:
    baseline = identity.build_identity(self.v2_spec)
    for field in identity.V2_ROOT_DESCRIPTOR_FIELDS:
        changed = self.mutate_descriptor(self.v2_spec, field)
        with self.subTest(field=field):
            self.assertNotEqual(
                baseline.values["effective_config_hash"],
                identity.build_identity(changed).values["effective_config_hash"],
            )

def test_v2_json_exposes_hash_verified_effective_config(self) -> None:
    manifest = identity.output_manifest(identity.build_identity(self.v2_spec))
    canonical = identity.canonical_effective_config(manifest["effective_config"])
    self.assertEqual(
        hashlib.sha256(canonical).hexdigest(),
        manifest["identity"]["effective_config_hash"],
    )
```

Add capture tests that accept symbol sizes 404 and 500, read exactly the symbol's declared size, reject 403/499/501, and validate the correct version. Add C contract tests for version 2, size 500, and the three appended 32-byte arrays.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_residency.py
```

Expected: new v2 constants/fields are absent.

- [x] **Step 3: Split the binary layouts by version**

```python
IDENTITY_V1_STRUCT = struct.Struct(V1_FORMAT)
IDENTITY_V2_STRUCT = struct.Struct(V1_FORMAT + "32s" * 3)
IDENTITY_V1_SIZE = IDENTITY_V1_STRUCT.size
IDENTITY_V2_SIZE = IDENTITY_V2_STRUCT.size
SUPPORTED_IDENTITY_SIZES = (IDENTITY_V1_SIZE, IDENTITY_V2_SIZE)
IDENTITY_SIZE = IDENTITY_V2_SIZE
```

Read the common `>IHH` prefix first, require `(version, size)` to be `(1, 404)` or `(2, 500)`, then unpack with the matching struct. A spec without `identity_version` remains v1 for historical tests and tooling. A spec with `identity_version: 2` requires all three new descriptors and uses schema `sm64-saturn-effective-config-v2`. Factor `canonical_effective_config(document: Mapping[str, Any]) -> bytes` and `output_manifest(built: BuiltIdentity) -> dict[str, Any]`; the CLI JSON output uses the latter and includes the effective-config object only after rehashing it against the embedded digest.

- [x] **Step 4: Extend target C ABI and generated initializer**

Append exactly:

```c
uint8_t target_profile_hash[32];
uint8_t package_set_root_hash[32];
uint8_t toolchain_attestation_hash[32];
```

Set target constants to version 2 and size 500. Preserve magic `SBI1`, field order, endianness, and the 404-byte prefix. Keep the static assert and add an offset assertion that `offsetof(sm64_saturn_build_identity_t, target_profile_hash) == 404U`.

- [x] **Step 5: Make capture probe size version-aware**

Resolve `saturn_build_identity` with `{BUILD_IDENTITY_SYMBOL: build_identity.SUPPORTED_IDENTITY_SIZES}`, use `symbol['size']` for ELF extraction and target reads, then call `validate_identity(raw)`. Never truncate a v2 symbol to 404 bytes.

```python
symbols = _resolve_symbols(
    elf, {BUILD_IDENTITY_SYMBOL: build_identity.SUPPORTED_IDENTITY_SIZES}
)
symbol = resolve_build_identity_symbol(symbols)
raw = _elf_symbol_bytes(elf, symbol)
parsed = build_identity.validate_identity(raw)
return {"address": symbol["address"], "size": symbol["size"],
        "expected_bytes": list(raw), "identity": parsed}
```

- [x] **Step 6: Run all focused identity/capture tests**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_residency.py
```

- [x] **Step 7: Update docs, commit, and obtain both reviews**

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/gen_build_identity.py tools/saturn/test_gen_build_identity.py tools/saturn/capture_sourceboot_throughput.py tools/saturn/test_sourceboot_feature_identity.py tools/saturn/test_capture_sourceboot_throughput.py src/port/saturn/platform/saturn_build_identity.h src/port/saturn/platform/saturn_build_identity.c tools/saturn/test_sourceboot_identity_residency.py
git commit -m "feat(saturn): add build identity v2"
```

Independent reviews must explicitly verify v1 byte compatibility, v2 offsets/size, dynamic capture size, all-root mutation coverage, and no accidental v2/v3 audit edits.

---

### Task 5: Compose the sourceboot identity-v2 spec from sealed inputs

**Execution status (2026-08-10):** `complete` from dispatch base
`b0c7fa03437e54019803581f5652d099e85029b4`; behavior commit `992bfa7d`
records the implementation below. The first independent combined review
returned `Needs fixes`: consume validated source/profile/toolchain snapshots,
publish the sibling manifest set transactionally, emit repository-relative
canonical descriptors/spec bytes, and isolate mutation tests. Focused repair/
rereview is active; round 1 cleared portability and mutation isolation but kept
staged-profile revalidation plus exception-preserving, `.tmp`-clean rollback
open. Repair round 2 in commits `c1612fd8` / `727c4075` passed 14
focused tests plus 21 identity and 11 target-profile regressions (46
total); scoped rereview found both remaining findings addressed with no new
breakage, clearing both review gates. The review's request to
remeasure live compiler/header bytes here was withdrawn: Task 5 validates and
rehashes the exact attestation document snapshot, while Task 6 owns the Task 3
live verifier after link. Repair round 1 is recorded in `ec546de2`. TDD replaced the recursive repository-root
contract with focused identity-v2 composition tests. The bootstrap now rehashes
validated source closure, resolved-profile, package-class/package-set, and toolchain descriptors;
requires Make configuration to equal the selected profile; maps the nine legacy
package classes one-to-one while leaving texture aggregate-only; and validates
all fixed sibling outputs before rollback-safe transactional replacement. The
spec uses canonical bytes and repository-relative paths, and the isolated
mutation contract checks the full root vector. Target build, reproducibility, audit v4,
complete-package, 20,100-frame smoke, visual, and manual-play gates remain open.

**Files:**
- Modify: `tools/saturn/bootstrap_sourceboot_identity_spec.py`
- Modify: `tools/saturn/test_sourceboot_identity_spec_bootstrap.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: `resolve_target_profile()`, source closure v2, toolchain attestation v1, and identity-v2 generation.
- Produces: `write_spec(root: Path, output: Path, config: Mapping[str, int], profile_path: Path, source_closure_path: Path, toolchain_attestation_path: Path, mode: Literal['development', 'release']) -> None`.
- Produces: v2 spec descriptors for all existing artifact fields plus `target_profile`, `package_set`, and `toolchain_attestation`.

- [x] **Step 1: Replace broad-root expectations with failing hermetic tests**

```python
def test_capture_tool_and_evidence_changes_do_not_reseal_v2_spec(self) -> None:
    first = self.write_v2_spec()
    for relative in (
        "tools/saturn/capture_object_pool_occupancy.py",
        "tools/saturn/test_gen_build_identity.py",
        "docs/saturn/evidence/local.json",
    ):
        self.write_unselected(relative, "changed\n")
    second = self.write_v2_spec()
    self.assertEqual(identity.build_identity(first).raw, identity.build_identity(second).raw)

def test_closure_profile_package_and_toolchain_mutations_reseal_separately(self) -> None:
    baseline = identity.parse_identity(identity.build_identity(self.write_v2_spec()).raw)
    for fixture, expected_field in (
        ("closure", "source_hash"),
        ("profile", "target_profile_hash"),
        ("package-set", "package_set_root_hash"),
        ("toolchain", "toolchain_attestation_hash"),
    ):
        with self.subTest(fixture=fixture):
            changed = identity.parse_identity(
                identity.build_identity(self.write_v2_spec(mutate=fixture)).raw
            )
            self.assertNotEqual(baseline[expected_field], changed[expected_field])

def test_bootstrap_rejects_stale_declared_manifest_without_overwriting_spec(self) -> None:
    self.write_v2_spec()
    preserved = self.output.read_bytes()
    self.source_closure.write_bytes(self.source_closure.read_bytes() + b"drift")
    with self.assertRaisesRegex(ValueError, "source closure.*stale"):
        self.write_v2_spec(reuse_declared_hash=True)
    self.assertEqual(self.output.read_bytes(), preserved)
```

Also assert `SOURCE_CLOSURE_ROOTS` is absent, identity version is 2, profile config matches Make-provided config, release-disabled full profile fails, the nine legacy-mapped package classes map one-to-one to the existing artifact fields, and the texture class remains covered by the aggregate package-set root.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py
```

Expected: the old broad-root bootstrap changes identity for unselected tooling and lacks v2 arguments.

- [x] **Step 3: Implement v2 composition and remove recursive repository hashing**

Construct the spec only from validated manifest descriptors:

```python
spec = {
    "identity_version": 2,
    "features": {name: values[f"features.{name}"] for name in identity.FEATURE_BITS},
    **{field: values[field] for field in identity.SCALAR_FIELDS},
    **{field: values[field] for field in identity.COMPILER_CONFIG_FIELDS},
    "artifacts": {
        "source_hash": descriptor(source_closure_path),
        **package_identity_descriptors(resolved.package_class_manifests),
    },
    "target_profile": descriptor(resolved_profile_path),
    "package_set": descriptor(package_set_path),
    "toolchain_attestation": descriptor(toolchain_attestation_path),
}
```

Delete `SOURCE_CLOSURE_ROOTS`, `_source_closure_inputs()`, and any test that requires audit/capture/test files merely because they live under `tools/saturn`. Retain sourceboot-specific configuration validation and atomic stale-output protection.

`write_spec()` emits the resolved profile as sibling
`saturn-target-profile-v1.json`, the package-set manifest as sibling
`saturn-package-set-v1.json`, and per-class manifests below sibling directory
`saturn-package-manifests/`. It writes the identity spec only after every one
of those outputs validates and its declared digest matches current bytes.

- [x] **Step 4: Add CLI profile, closure, toolchain, and mode arguments**

Required arguments are `--profile`, `--source-closure`, `--toolchain-attestation`, `--mode development|release`, existing repeated `--set`, `--root`, and `--output`. No default may fall back to a stale broad-root spec.

```python
parser.add_argument("--profile", type=Path, required=True)
parser.add_argument("--source-closure", type=Path, required=True)
parser.add_argument("--toolchain-attestation", type=Path, required=True)
parser.add_argument("--mode", choices=("development", "release"), required=True)
write_spec(
    args.root, args.output, _parse_settings(args.set), args.profile,
    args.source_closure, args.toolchain_attestation, args.mode,
)
```

- [x] **Step 5: Run bootstrap and identity regressions**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_target_profile.py
```

- [x] **Step 6: Update docs, commit, and clear both reviews**

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/bootstrap_sourceboot_identity_spec.py tools/saturn/test_sourceboot_identity_spec_bootstrap.py
git commit -m "feat(saturn): compose hermetic sourceboot identity"
```

Reviews must confirm the old closure is gone for v2, every consumed manifest is rehashed, config drift fails, unselected tool/evidence changes do not reseal, and v1 parsing remains untouched.

---

### Task 6: Two-phase sourceboot build, dependency verification, and release cleanliness

**Files:**
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`
- Modify: `tools/saturn/gen_source_closure.py`
- Modify: `tools/saturn/test_gen_source_closure.py`
- Modify: `tools/saturn/gen_toolchain_attestation.py`
- Modify: `tools/saturn/test_gen_toolchain_attestation.py`
- Create: `tools/saturn/test_sourceboot_hermetic_build_make.py`
- Modify: `tools/saturn/test_sourceboot_identity_spec_bootstrap.py`
- Modify: `docs/saturn/BUILDING.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: Task 2 closure CLI, Task 3 attestation CLI, Task 5 bootstrap CLI.
- Produces Make stages `assets`, `discover`, and `seal`; targets `identity-assets`, `identity-discovery`, `print-identity-tag`, and `verify-sealed-inputs`.
- Produces generated paths `saturn-source-closure-v2.json`, `saturn-toolchain-attestation-v1.json`, resolved profile/package manifests, and identity-v2 spec.
- Produces Task 2 CLI subcommands `build` and `verify`, plus diagnostic handoff schema `sm64-saturn-external-dependencies-v1` containing sorted absolute dependency paths outside identity.
- Extends Task 3's existing flag-style CLI with `--external-dependencies <handoff>` while preserving `--output`, `--verify`, and repeated `--external-dependency` compatibility.

**Reconciled interface correction (2026-08-10):** Task 2's review-cleared
library lacked the CLI that this integration task was written to consume, and
Task 3 already exposed a reviewed flag-style CLI rather than subcommands. Task
6 owns the missing Task 2 CLI/handoff adapter and adds a handoff-file reader to
Task 3; Make must invoke Task 3's actual `--output` / `--verify` form instead
of inventing a second interface.

**Live status (2026-08-10):** Task 6 is `complete`; repair round 1 landed in commit
`b2ea338c` (`fix(saturn): match hermetic scans to Yaul`) after the original
behavior commit `b1d75772`, and scoped independent rereview cleared every
finding with no new Critical or Important breakage. Host/dry-run
TDD covers stage isolation, exact C/`.sx` scan expansion, flag/spec parity, profile/mode
propagation, explicit closure classification and identity-cycle breaking,
post-link dependency/external equality, release cleanliness dispatch, live
toolchain verification, and stale closure/attestation failure. Generated `.sx`
sources now live in the stable generated-input directory so discovery and build
name identical inputs rather than requiring an identity to discover itself. No
real SH-2 build was run; every target/release evidence
gate remain open.

**Review repair round 1 (2026-08-10):** the reviewer confirmed the staged
architecture and failure ordering, but found one Critical command-parity defect:
`.sx` discovery/post-link scans added `SH_SPECS` even though Yaul's real `.sx`
rule does not. Important findings require discovery scans to be fresh when
flags/specs change, full C++ discovery/prefix-map support for the eventual full
game, fail-closed rejection of aliased closure/handoff output paths before I/O,
and dry-run tests that prove exact expanded inventory/argv parity rather than
source-token presence. The repair now uses distinct C/C++/`.sx` scanners,
forces discovery depfiles fresh on every discovery stage, applies all three
prefix maps to C++, and rejects resolved closure/handoff output aliases before
any publication. The Make regression extracts its real compile models from the
checked-in pinned Yaul rules and proves exact 223 C + 4 `.sx` default coverage,
four post-link `.sx` scans, and an injected one-C++ 228-source inventory. Scoped
rereview marked all five findings `ADDRESSED` and found no new Critical or
Important breakage. Task 6 is `complete`; no target or release-evidence gate is
closed by the repair.

Repair TDD RED comprised four focused failures: aliased outputs were accepted;
`.sx` discovery retained an extra `-specs=sourceboot.specs`; a cached `main.c`
depfile suppressed the second scan after flag drift; and the injected C++ source
was absent from discovery. Final exact GREEN is 7 Make + 15 bootstrap + 17
closure + 15 attestation = 54 host tests, with one legitimate Windows
case-spelling skip. Rereview is approved; every real-target/release gate remains
open.

The initial implementation's post-commit verification passed 5 Make + 15
bootstrap + 16 closure + 15 attestation = 51 host tests, with one legitimate
case-spelling skip on this Windows filesystem. Its no-findings self-review was
superseded by the independent `Needs fixes` verdict and repair above. The
source/documentation and independent-review portions of Step 8 are complete.

- [x] **Step 1: Write failing Make-contract tests**

```python
def test_outer_build_orders_assets_discovery_seal_build_and_verify(self) -> None:
    makefile = (ROOT / "Makefile.saturn.mk").read_text()
    positions = [makefile.index(token) for token in (
        "SOURCEBOOT_BUILD_IDENTITY_STAGE=assets identity-assets",
        "SOURCEBOOT_BUILD_IDENTITY_STAGE=discover identity-discovery",
        "print-identity-tag",
        "verify-sealed-inputs",
    )]
    self.assertEqual(positions, sorted(positions))

def test_discovery_uses_real_flags_and_seal_consumes_manifests(self) -> None:
    makefile = self.sourceboot_makefile()
    self.assertIn("$(filter-out -save-temps=obj,$(SH_CFLAGS))", makefile)
    self.assertIn("$(foreach specs,$(SH_SPECS),-specs=$(specs))", makefile)
    self.assertIn("-ffile-prefix-map=$(ROOT)=.", makefile)
    self.assertIn("-fdebug-prefix-map=$(ROOT)=.", makefile)
    self.assertIn("-fmacro-prefix-map=$(ROOT)=.", makefile)
    self.assertIn("--source-closure", makefile)
    self.assertIn("--toolchain-attestation", makefile)
    self.assertIn("verify-sealed-inputs", makefile)

def test_discovery_stage_cannot_build_or_reuse_identity(self) -> None:
    self.assert_stage_rejected("discover", "all")
    self.assert_stage_rejected("assets", "identity-discovery")
```

Add a dry-run fixture that provides a fake Yaul include and asserts development/release mode propagation, BOB profile selection, derived identity output exclusions, linker/build-recipe inputs, generator inputs, and one dependency-scan command per C and `.sx` source.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_hermetic_build_make.py
```

- [x] **Step 3: Add discovery variables and stage guards**

Define:

```make
SOURCEBOOT_TARGET_PROFILE ?= $(ROOT)/tools/saturn/profiles/sourceboot-bob-demo-v1.json
SOURCEBOOT_RELEASE_MODE ?= development
SOURCEBOOT_RESOLVED_PROFILE := $(SOURCEBOOT_GENERATED)/saturn-target-profile-v1.json
SOURCEBOOT_PACKAGE_SET := $(SOURCEBOOT_GENERATED)/saturn-package-set-v1.json
SOURCEBOOT_SOURCE_CLOSURE := $(SOURCEBOOT_GENERATED)/saturn-source-closure-v2.json
SOURCEBOOT_TOOLCHAIN_ATTESTATION := $(SOURCEBOOT_GENERATED)/saturn-toolchain-attestation-v1.json
SOURCEBOOT_EXTERNAL_DEPENDENCIES := $(SOURCEBOOT_GENERATED)/saturn-external-dependencies-v1.json
SOURCEBOOT_DISCOVERY_DEPS := $(SOURCEBOOT_GENERATED)/closure-discovery
```

Allow only `assets`, `discover`, or `seal`. `assets` may run only `identity-assets`; `discover` may run only `identity-discovery`; neither stage may parse an identity label or choose an identity output directory.

Append the three root-normalization flags to `SH_CFLAGS` before both discovery
and real compile commands are expanded. Do not map `YAUL_INSTALL_ROOT` to the
repository; external SDK paths belong to the toolchain attestation.

- [x] **Step 4: Generate pre-seal dependency metadata with the real flags**

For every C input invoke SH GCC with `-MM -MG`, the exact `SH_CFLAGS` minus `-save-temps=obj`, and the same `-specs=` arguments as Yaul's real compile. Scan `.sx` inputs with the same preprocessor configuration. Permit `-MG` to discover the one absent derived identity include, then have `gen_source_closure.py` reject any other missing dependency.

```make
define sourceboot-discover-dependency
	$(SH_CC) -MM -MG $(filter-out -save-temps=obj,$(SH_CFLAGS)) \
	  $(foreach specs,$(SH_SPECS),-specs=$(specs)) \
	  -MT "$(1)" -MF "$(SOURCEBOOT_DISCOVERY_DEPS)/$(2).d" "$(1)"
endef

identity-discovery:
	@mkdir -p "$(SOURCEBOOT_DISCOVERY_DEPS)"
	$(foreach src,$(SH_SRCS_C),$(call sourceboot-discover-dependency,$(src),$(call sourceboot-dep-key,$(src))))
	$(foreach src,$(SH_SRCS_S),$(call sourceboot-discover-dependency,$(src),$(call sourceboot-dep-key,$(src))))
```

Pass these explicit inputs to the closure generator:

- `SH_SRCS` as compiled/generated sources;
- `src/port/saturn/sourceboot/Makefile`, `Makefile.saturn.mk`, `sourceboot.specs`, `sourceboot-cart.x`, and the pinned Yaul build fragments as linker/build recipes;
- identity/profile/package/asset generator Python files as generators;
- generated headers, generated C/assembly sources, incbin payloads, and linker fragments as generated inputs; and
- identity values/include/blob/spec, resolved manifests, closure/attestation outputs, ELF, map, sym, asm, `SOURCE.DAT`, ISO, CUE, and release manifest as derived outputs.

- [x] **Step 5: Generate toolchain attestation and identity spec during seal**

`identity-discovery` emits the canonical source closure, a noncanonical absolute-path external-dependency handoff, and the canonical toolchain attestation atomically. The handoff is a derived diagnostic input excluded from identity; the attestation canonicalizes the dependencies under component-relative paths. The seal-stage bootstrap receives the closure/attestation exact paths, target profile, and `SOURCEBOOT_RELEASE_MODE`; only then may `print-identity-tag` generate identity v2 and select the identity-tagged output directory.

Task 2 `build` writes the canonical closure and diagnostic handoff with atomic
file replacement. Task 3 consumes the handoff through
`--external-dependencies "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"` in addition to
its existing repeated `--external-dependency` option, and writes the canonical
attestation through its existing `--output` flag.

```make
SOURCEBOOT_BUILD_IDENTITY_BOOTSTRAP_ARGS += \
  --profile "$(SOURCEBOOT_TARGET_PROFILE)" \
  --source-closure "$(SOURCEBOOT_SOURCE_CLOSURE)" \
  --toolchain-attestation "$(SOURCEBOOT_TOOLCHAIN_ATTESTATION)" \
  --mode "$(SOURCEBOOT_RELEASE_MODE)"
```

- [x] **Step 6: Verify real compile dependencies and rehash after link**

`verify-sealed-inputs` depends on the linked ELF. It passes Yaul's real `$(SH_DEPS)` for C/C++ and freshly rescanned `.sx` depfiles to `gen_source_closure.py verify`, requires the actual external paths to equal the discovery handoff, then calls `gen_toolchain_attestation.py verify` with that exact set. The outer `sourceboot` target invokes it immediately after the sealed build. A mismatch exits nonzero before artifact release sealing.

```make
.PHONY: verify-sealed-inputs
verify-sealed-inputs: $(SH_BUILD_PATH)/$(SH_PROGRAM).elf
	"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/gen_source_closure.py" verify \
	  --root "$(ROOT)" --sealed "$(SOURCEBOOT_SOURCE_CLOSURE)" \
	  $(foreach dep,$(SH_DEPS),--actual-depfile "$(dep)") \
	  --expected-external "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)" \
	  --mode "$(SOURCEBOOT_RELEASE_MODE)"
	"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/gen_toolchain_attestation.py" \
	  $(SOURCEBOOT_TOOLCHAIN_ARGS) \
	  --verify "$(SOURCEBOOT_TOOLCHAIN_ATTESTATION)" \
	  --external-dependencies "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"
```

- [x] **Step 7: Run host/dry-run Make tests**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_hermetic_build_make.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_identity_spec_bootstrap.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_source_closure.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_toolchain_attestation.py
```

Do not claim a real SH build in this task.

- [x] **Step 8: Update build guide, changelog, ledgers, commit, and review**

Document `SOURCEBOOT_TARGET_PROFILE`, development versus release mode, generated manifest locations, and the five-stage failure boundary in `docs/saturn/BUILDING.md`.

```powershell
git add CHANGELOG.md Makefile.saturn.mk src/port/saturn/sourceboot/Makefile tools/saturn/gen_source_closure.py tools/saturn/test_gen_source_closure.py tools/saturn/gen_toolchain_attestation.py tools/saturn/test_gen_toolchain_attestation.py tools/saturn/test_sourceboot_hermetic_build_make.py tools/saturn/test_sourceboot_identity_spec_bootstrap.py docs/saturn/BUILDING.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md
git commit -m "feat(saturn): integrate hermetic sourceboot sealing"
```

Independent reviewers must inspect exact flag parity, stage isolation, `.sx` coverage, derived-output cycle breaking, post-link equality, release cleanliness, and stale-spec failure.

---

### Task 7: Post-link release manifest, capture binding, and deployment staging

**Files:**
- Create: `tools/saturn/release_manifest.py`
- Create: `tools/saturn/test_release_manifest.py`
- Create: `tools/saturn/stage_saturn_release.py`
- Create: `tools/saturn/test_stage_saturn_release.py`
- Modify: `tools/saturn/capture_sourceboot_throughput.py`
- Modify: `tools/saturn/capture_object_pool_occupancy.py`
- Modify: `tools/saturn/capture_sourceboot_hud_state.py`
- Modify: `tools/saturn/launch_ymir_desktop.py`
- Modify: `tools/saturn/test_capture_sourceboot_throughput.py`
- Modify: `tools/saturn/test_capture_object_pool_occupancy.py`
- Create: `tools/saturn/test_capture_sourceboot_hud_state.py`
- Modify: `tools/saturn/test_launch_ymir_desktop.py`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `Makefile.saturn.mk`
- Modify: `docs/saturn/BUILDING.md`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Produces: `ReleaseManifestVerification(document: dict[str, Any], manifest_sha256: str, outputs: dict[str, Path])`.
- Produces: `build_release_manifest(root: Path, profile_path: Path, identity_json: Path, source_closure: Path, package_set: Path, toolchain_attestation: Path, outputs: Mapping[str, Path], mode: Literal['development', 'release']) -> bytes`.
- Produces: `verify_release_manifest(path: Path, *, required_profile: str | None = None, exact_inventory: bool = False) -> ReleaseManifestVerification`; staging uses `exact_inventory=True` before and after publication while capture inputs retain sealed-sibling tolerance.
- Produces: `compare_release_manifests(first: Path, second: Path) -> dict[str, Any]`, which verifies both manifests and requires identical canonical identity inputs and output bytes while ignoring their host locations.
- Produces: `stage_release(manifest: Path, destination: Path) -> Path`, which requires a missing or empty destination and copies only verified outputs plus the manifest.

**Live status (2026-08-10):** Task 7 is `complete` after behavior commits
`7195fc48`, `bb5a840d`, `00736856`, and `740a08bc`. Three scoped repair rounds
cleared every independent-review finding with no new Critical or Important
breakage. Fresh post-commit host
verification passes all seven exact suites at
24 + 9 + 41 + 12 + 4 + 9 + 7 = 106 tests and adjacent identity/bootstrap/boot-
trace/route-view suites at 21 + 15 + 16 + 1 = 53 tests, with zero skips or
failures; all six changed production scripts also compile. Focused TDD covers
canonical root-neutral artifact sealing, strict input/output/ELF/CUE
verification, relocated comparison, profile-neutral verify-before-copy staging,
v1/v2 capture identity binding, v2 manifest-owned pool capacity, report
digests, and exact Make ordering. The public `root` argument is the repository
root for Git provenance, while release-relative output paths are derived from
the CUE/output-manifest directory; the current Yaul layout places ELF and
`SOURCE.DAT` under its `obj/` child, so no escaping path is needed. No real
SH-2 build was run. Target build, reproducibility, audit-v4, complete-package,
20,100-frame smoke, visual, and manual-play gates remain open.

Reference-code-first record: in-tree close-port/pattern-only reuse at base
`48f5a61a` from `hermetic_manifest.py`, `gen_build_identity.py`,
`bootstrap_sourceboot_identity_spec.py`, the throughput/boot-trace capture
preflights, both sourceboot Makefiles, and their focused tests. Make ordering
was checked against pinned `yaul-org/libyaul` commit
`6012f79f237773378c8014e70d8998ad95a38d98`, MIT license,
`libyaul/build/build.post.iso-cue.mk` and `build.post.bin.mk`. No external
source was copied.

First independent review verdict: `Needs fixes`, with five Important findings.
Repair round 1 addresses all five: verification owns immutable manifest/output
snapshots through consumers; staging has ownership-checked transactional
rollback and manifest-last publication; profile effective config/output names
are semantically bound; closure/package/toolchain/output schemas reject empty,
exact/case-fold/path/alias collisions using host-neutral rules; the writer and
occupancy resolver exercise a real historical v1 ELF/identity; and comparison
uses canonical identity inputs plus semantic output bytes while ignoring host
layout/provenance. Desktop launch retains the private CUE/ISO snapshot through
child exit when Ymir outlives the bounded monitor. Task 7 is `complete`; every
target and release-evidence gate remains open.

Repair-round-1 rereview marked profile binding, host-neutral schema validation,
real v1 compatibility, and semantic comparison `ADDRESSED`. Immutable consumer
snapshots also cleared, but staging/manifest acquisition still use path checks
separately from open/unlink operations: a swapped ancestor can redirect writes,
rollback can unlink a foreign replacement, manifest selection can be swapped
between check/read, and final verification does not reject an extra staged file.
Round 2 must use race-resistant opened-file/directory identity or private-tree
atomic publication, exact final inventory, and rollback that never deletes an
unproven path. Host evidence cannot close any target gate.

Repair round 2 is `source-complete` in behavior commit `00736856`. Canonical
documents are now acquired through no-follow opened-file snapshots with
opened-object and full-ancestor identity checks. Staging builds a complete
manifest-last private sibling, verifies its exact inventory, publishes with an
atomic no-replace rename, and re-verifies the exact published tree. Windows
directory handles pin active namespaces; a proven preexisting empty backup is
removed by handle, while any ambiguous replacement, extra, partial tree, or
failed publication is retained as a uniquely named quarantine and the requested
missing/empty state is restored for retry without path deletion. Focused
release/stage GREEN is 26 + 14 = 40 tests; all seven exact suites pass
26 + 14 + 41 + 12 + 4 + 9 + 7 = 113 host tests, adjacent suites pass
21 + 15 + 16 + 1 = 53, and all six production scripts compile. Controller-owned
rereview remains open, as do every target and release-evidence gate.

Round-2 rereview confirmed the opened-snapshot and atomic/quarantine namespace
protections with no regression to the already-cleared findings. Its new
Important finding is platform scope: `_rename_noreplace()` treats every POSIX
host as Linux `renameat2`, so macOS/BSD fail rather than publish atomically,
while safe identity deletion of a proven preexisting-empty backup is
Windows-only. Round 3 must provide an exclusive atomic rename adapter for
supported non-Linux POSIX hosts and make retained empty-quarantine behavior
explicit and tested wherever deletion by opened identity is unavailable. It
must not reintroduce check-then-unlink cleanup.

Repair round 3 is `source-complete` in behavior commit `740a08bc`. Atomic
publication resolves its platform capability before creating any staging
namespace: Windows uses its exclusive rename, Linux requires directory-relative
`renameat2(RENAME_NOREPLACE)`, and Darwin/BSD-family platforms require libc's
directory-relative `renameatx_np(RENAME_EXCL)`. A missing symbol or unsupported
host fails closed; path-only `renamex_np` and check-then-rename are not
fallbacks. Windows still deletes a proven empty backup by retained handle;
platforms without identity-conditional opened-object deletion retain the empty
`.sm64-saturn-quarantine-<destination>-<unique-id>` sibling and emit its path.
Injected tests characterize POSIX API dispatch without claiming OS execution.
All seven exact suites pass 26 + 20 + 41 + 12 + 4 + 9 + 7 = 119 host tests;
adjacent suites pass 21 + 15 + 16 + 1 = 53, and six production scripts compile.
Round-3 scoped rereview marked the POSIX portability finding `ADDRESSED` and
found no new Critical or Important breakage. No Linux/macOS/BSD execution or
target evidence is claimed; those gates remain open in their owning tasks.

Final self-review found and TDD-corrected two fail-closed gaps before commit:
malformed canonical source-closure rows now fail schema validation rather than
surfacing only as a downstream digest mismatch, and the explicit v1 occupancy
compatibility spec must reproduce the exact ELF identity bytes. No further
source finding remained before independent review. One initial adjacent command named two tests
incorrectly and was discarded; the corrected fail-fast rerun is the 53-test
result above. Step 8 is complete; all target-only evidence remains open.

- [x] **Step 1: Write failing release and staging tests**

```python
def test_release_manifest_binds_exact_outputs_without_timestamps(self) -> None:
    raw = self.build_manifest()
    document = json.loads(raw)
    self.assertNotIn("created_at", document)
    self.assertEqual(set(document["outputs"]), {"elf", "source_dat", "iso", "cue"})
    for record in document["outputs"].values():
        self.assertEqual(len(record["sha256"]), 64)
        self.assertGreater(record["size"], 0)

def test_verifier_rejects_each_mutated_output_before_capture_or_stage(self) -> None:
    manifest = self.write_manifest()
    for output in ("elf", "source_dat", "iso", "cue"):
        with self.subTest(output=output):
            self.mutate_output(output)
            with self.assertRaisesRegex(ValueError, f"{output}.*SHA-256 mismatch"):
                verify_release_manifest(manifest)
            self.restore_output(output)

def test_stage_is_profile_agnostic_and_refuses_nonempty_destination(self) -> None:
    staged = stage_release(self.full_profile_fixture(), self.destination)
    self.assertTrue((staged / "saturn-release-manifest-v1.json").is_file())
    with self.assertRaisesRegex(ValueError, "destination.*not empty"):
        stage_release(self.demo_manifest, self.destination)

def test_compare_accepts_same_bytes_at_different_host_roots(self) -> None:
    comparison = compare_release_manifests(
        self.release_under("worktree-a"), self.release_under("worktree-b")
    )
    self.assertTrue(comparison["identical"])
    self.assertEqual(comparison["differing_fields"], [])
```

Add throughput, occupancy, HUD, and desktop-launch tests that require `--release-manifest` for new evidence, reject a manifest whose ELF/CUE differs from CLI paths, accept v1 and v2 embedded identities where historical parsing is permitted, and record the release-manifest SHA-256 in every report.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_release_manifest.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_stage_saturn_release.py
```

- [x] **Step 3: Implement exact artifact sealing and verification**

Canonical output records contain `path`, `size`, and lowercase `sha256`. Paths are relative to the release manifest's directory. Extract `saturn_build_identity` from the exact ELF, validate its version/size/bytes, require its digest and effective configuration to equal the identity JSON, and rehash the embedded `effective_config` object before writing or accepting the release manifest. Validate that the CUE has exactly one `FILE` directive, its referenced ISO basename equals the manifest's ISO path, and both files match their hashes independently. The CLI provides `write`, `verify`, and `compare` subcommands; `compare --output` writes the verified two-build comparison report.

The manifest document includes:

```python
document = {
    "schema": "sm64-saturn-release-manifest-v1",
    "profile_id": profile["profile_id"],
    "target_profile_sha256": profile_sha256,
    "identity_version": identity_values["version"],
    "identity_sha256": identity_binary_sha256,
    "effective_config_sha256": identity_values["effective_config_hash"],
    "effective_config": identity_json["effective_config"],
    "source_closure_sha256": source_closure_sha256,
    "package_set_sha256": package_set_sha256,
    "toolchain_attestation_sha256": toolchain_sha256,
    "mode": mode,
    "reproducibility": "uncompared",
    "outputs": output_records,
}
```

Git revision and closure cleanliness facts are included under `provenance`; no dirty file listing or absolute path enters canonical bytes.

- [x] **Step 4: Implement safe profile-agnostic deployment staging**

Verify all source bytes before creating destination files. Require the destination to be absent or empty; never delete or overwrite an existing release. Copy each output under its release-relative path, copy the manifest last, then re-run `verify_release_manifest()` inside the destination.

```python
verified = verify_release_manifest(manifest)
if destination.exists() and any(destination.iterdir()):
    raise ValueError(f"release destination is not empty: {destination}")
destination.mkdir(parents=True, exist_ok=True)
for name, source in verified.outputs.items():
    relative = Path(verified.document["outputs"][name]["path"])
    target = destination / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
shutil.copyfile(manifest, destination / "saturn-release-manifest-v1.json")
verify_release_manifest(destination / "saturn-release-manifest-v1.json")
```

- [x] **Step 5: Bind capture and manual-launch entry points to the release manifest**

Throughput and occupancy require the manifest's resolved `elf` and `cue` paths to equal `--elf` and `--game`. HUD capture requires its `--game` CUE to match and reads the ELF from the verified manifest. Desktop launch accepts `--release-manifest`, resolves its CUE, and refuses a separate mismatching CUE. Before opening Ymir or invoking SH tools, every entry point verifies the manifest and stores `release_manifest_sha256` in its report. Throughput/occupancy additionally require identity bytes from the ELF to match manifest identity SHA-256/version/effective config. For identity v2, occupancy reads `object_pool_capacity` from the manifest's hash-verified `effective_config`; `--identity-spec` remains only as an explicit v1 compatibility input and is not required for the staged v2 target.

```python
verified = verify_release_manifest(args.release_manifest)
if args.game.resolve() != verified.outputs["cue"]:
    raise ValueError("game CUE differs from verified release manifest")
elf = verified.outputs["elf"]
report["release_manifest_sha256"] = verified.manifest_sha256
```

- [x] **Step 6: Wire post-build sealing into Make**

Add `seal-release` after `verify-sealed-inputs`; it writes `saturn-release-manifest-v1.json` beside the CUE. Add `verify-release` to `verify-sourceboot`. The outer `sourceboot` target is successful only after closure verification and release-manifest creation.

```make
.PHONY: seal-release verify-release
seal-release: verify-sealed-inputs $(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue
	"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/release_manifest.py" write \
	  --profile "$(SOURCEBOOT_RESOLVED_PROFILE)" \
	  --source-closure "$(SOURCEBOOT_SOURCE_CLOSURE)" \
	  --package-set "$(SOURCEBOOT_PACKAGE_SET)" \
	  --toolchain-attestation "$(SOURCEBOOT_TOOLCHAIN_ATTESTATION)" \
	  --mode "$(SOURCEBOOT_RELEASE_MODE)" \
	  --identity-json "$(SOURCEBOOT_BUILD_IDENTITY_JSON)" \
	  --elf "$(SH_BUILD_PATH)/$(SH_PROGRAM).elf" \
	  --source-dat "$(SOURCEBOOT_CART_IMAGE)" \
	  --iso "$(SH_OUTPUT_PATH)/$(SH_PROGRAM).iso" \
	  --cue "$(SH_OUTPUT_PATH)/$(SH_PROGRAM).cue" \
	  --output "$(SH_OUTPUT_PATH)/saturn-release-manifest-v1.json"
verify-release: seal-release
	"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/release_manifest.py" verify \
	  --manifest "$(SH_OUTPUT_PATH)/saturn-release-manifest-v1.json"
```

- [x] **Step 7: Run focused release/capture tests**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_release_manifest.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_stage_saturn_release.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_object_pool_occupancy.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_hud_state.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_launch_ymir_desktop.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_hermetic_build_make.py
```

- [x] **Step 8: Update docs, commit, and clear both reviews**

```powershell
git add CHANGELOG.md Makefile.saturn.mk src/port/saturn/sourceboot/Makefile tools/saturn/release_manifest.py tools/saturn/test_release_manifest.py tools/saturn/stage_saturn_release.py tools/saturn/test_stage_saturn_release.py tools/saturn/capture_sourceboot_throughput.py tools/saturn/capture_object_pool_occupancy.py tools/saturn/capture_sourceboot_hud_state.py tools/saturn/launch_ymir_desktop.py tools/saturn/test_capture_sourceboot_throughput.py tools/saturn/test_capture_object_pool_occupancy.py tools/saturn/test_capture_sourceboot_hud_state.py tools/saturn/test_launch_ymir_desktop.py docs/saturn/BUILDING.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md
git commit -m "feat(saturn): seal and stage exact releases"
```

Reviews must cover verify-before-I/O, CUE/ISO binding, no overwrite/delete behavior, profile neutrality, new capture preflight, and exact Make ordering.

---

### Task 8: Native-math audit v4 measurement and immutable release binding

**Files:**
- Create: `tools/saturn/seal_sh2_native_math_audit_v4.py`
- Create: `tools/saturn/test_seal_sh2_native_math_audit_v4.py`
- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Extends `AuditContract` with optional v4 fields `expected_release_manifest_sha256`, `expected_identity_sha256`, `expected_effective_config_sha256`, and `expected_target_profile_sha256`.
- Produces `verify_audit_contract_target(contract: AuditContract, elf: Path, release_manifest: Path | None = None) -> None`.
- Adds required `--release-manifest PATH` whenever audit v4 or measurement mode is selected; v2/v3 historical invocations keep their existing behavior.
- Produces measurement CLI `--measure-audit-report PATH`, valid only with `--audit-route-oracle` and without `--audit-contract`.
- Produces `seal_v4_contract(measurement_path: Path, release_manifest_path: Path, output: Path) -> bytes`, refusing to overwrite an existing output.

**Live status (2026-08-10):** `complete` in behavior commits `db4c620d`
(`feat(saturn): add release-bound native math audit v4`), `9fcc9632`
(`fix(saturn): harden audit v4 publication`), and round-2 repair `2277c3e2`
(`fix(saturn): publish exact audit objects`). Parser/preflight,
explicitly unsealed measurement, one-shot sealing, focused tests, the full
verifier run, Task 7 release-manifest adjacency, and Python compilation are
implemented. Independent review initially returned `Needs fixes`; repairs
`9fcc9632` and `2277c3e2` closed all three findings. The final scoped rereview
marked exact-object publication and late measurement-output aliasing
`ADDRESSED`, confirmed release-manifest mode legality remained addressed, and
found no new Critical or Important breakage. No real measurement,
v4 contract, or pinned digest was created; Task 9 still owns those exact-target
steps. Historical v2/v3 bytes and every target/release-evidence gate remain
open and unchanged.

Reference-code-first record: implementation used pattern-only/close-port reuse
from the in-tree audit parser/integrity/preflight/main and tests at base
`15265084`, plus Task 7's context-managed `release_manifest.py` snapshot API and
`test_release_manifest.py` fixture at reviewed closeout `a07ffbe1`. The source
and destination are this same deliberately GPL-compatible project, whose root
has no repository-wide license declaration; no external source was copied or
adapted and no new notice obligation was introduced.

Execution evidence: the initial v4 parser test failed on the first unknown v4
directive and the sealer suite failed because its module did not exist. The
measurement tests then failed because both CLI flags were absent. A self-review
RED proved public v4 preflight rehashed the mutable original after manifest
verification; the correction now verifies the requested original path but
hashes and consumes only Task 7's immutable snapshot. Final sealer and release
adjacency suites pass 4 + 26 host tests; all four changed Python files compile.
The full verifier ran 238 tests with 237 passes and only the documented unrelated
`test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites`
failure. V2 remains 507 bytes at
`87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`;
v3 remains 416 bytes at
`80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`.

First independent review found one Critical and two Important safety gaps.
The sealer creates/writes the final contract pathname directly, exposing
partial bytes and using a raceable path unlink on failure; it must privately
stage/fsync and publish with an identity-safe exclusive primitive. Measurement
output must reject aliases with every input before verification or subprocess
execution. `--release-manifest` must be accepted only for v4/measurement and
rejected in ordinary/object-reference/v2/v3 modes so historical semantics stay
unchanged. Repair `9fcc9632` now writes/fsyncs a private same-directory file,
publishes it with Task 7's reviewed platform-dispatched atomic no-replace
rename, and retains ambiguous private state with diagnostics instead of
unlinking any pathname. Measurement rejects exact, normalized/casefold,
symlink, and hardlink aliases of every read input before release verification
or tools. Release manifests are legal only for parsed v4 or measurement.
Focused repair tests pass 11 sealer cases; the full verifier is 239/240 with
only the preserved null-camera failure; Task 7 manifest/staging adjacency is
46/46. Round-1 rereview cleared mode legality but retained the two race
findings; no target gate was affected.

Round-1 scoped rereview marked release-manifest mode legality `ADDRESSED`, but
kept exact-object publication and late alias safety open. Repair `2277c3e2`
now writes both contracts and measurement reports to a complete, fsynced
same-directory private object and atomically publishes the exact held object
without replacement. Windows renames the held handle under Task 7's pinned
directory guard; POSIX links the held descriptor through `/proc/self/fd` or
`/dev/fd` and fails closed when neither exact-object facility is available.
Post-publication identity/size checks reject ambiguity, and no final or foreign
path is unlinked. A preexisting or late symlink/hardlink output therefore makes
publication fail while all inputs remain byte-identical. Focused race REDs
proved both prior vulnerabilities; GREEN is 12/12 sealer, 241/242 full verifier
with only the preserved null-camera failure, and 46/46 Task 7 adjacency.
Final scoped rereview marked both residual findings `ADDRESSED`, confirmed the
mode matrix and historical v2/v3 contracts did not regress, and found no new
Critical or Important breakage. No real measurement, contract, pin, target, or
emulator gate is implied by this host-side repair.

- [x] **Step 1: Write failing v4 parser, preflight, and measurement tests**

```python
def test_v4_requires_all_release_identity_hashes(self) -> None:
    contract = parse_audit_contract(self.v4_contract_text())
    self.assertEqual(contract.version, 4)
    for directive in (
        "EXPECTED_RELEASE_MANIFEST_SHA256",
        "EXPECTED_IDENTITY_SHA256",
        "EXPECTED_EFFECTIVE_CONFIG_SHA256",
        "EXPECTED_TARGET_PROFILE_SHA256",
        "EXPECTED_ELF_SHA256",
    ):
        with self.subTest(directive=directive):
            with self.assertRaisesRegex(ValueError, "v4"):
                parse_audit_contract(self.remove_directive(directive))

def test_v4_rejects_manifest_or_identity_before_disassembly(self) -> None:
    with patch.object(verifier, "run_tool") as run_tool:
        with self.assertRaisesRegex(ValueError, "release manifest"):
            verify_audit_contract_target(
                self.contract, self.elf, self.wrong_release_manifest
            )
        run_tool.assert_not_called()

def test_measurement_report_is_explicitly_unsealed(self) -> None:
    report = self.run_measurement()
    self.assertEqual(report["schema"], "sm64-saturn-native-math-measurement-v1")
    self.assertEqual(report["status"], "measured-unsealed")
    self.assertEqual(report["root"], "_game_loop_one_iteration")
    self.assertIsInstance(report["total"], int)
```

Add tests that v2 rejects every new directive, v3 still requires only exact ELF, v4 rejects uppercase/wrong-length hashes and duplicate directives, sealer rejects mismatched measurement/manifest ELF hashes, sealer refuses overwrite, and checked-in v2/v3 contract bytes/digests remain unchanged.

- [x] **Step 2: Run focused tests and observe RED**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py NativeMathCensusTests.test_v4_requires_all_release_identity_hashes
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_seal_sh2_native_math_audit_v4.py
```

- [x] **Step 3: Extend parser and fail-fast target verification**

Audit v4 canonical directives are emitted directly from verified objects:

```python
contract_text = (
    "AUDIT_CONTRACT_VERSION 4\n"
    "EXPECTED_ROOT _game_loop_one_iteration\n"
    f"EXPECTED_TOTAL {measurement['total']}\n"
    f"EXPECTED_RELEASE_MANIFEST_SHA256 {manifest_sha256}\n"
    f"EXPECTED_IDENTITY_SHA256 {release['identity_sha256']}\n"
    f"EXPECTED_EFFECTIVE_CONFIG_SHA256 {release['effective_config_sha256']}\n"
    f"EXPECTED_TARGET_PROFILE_SHA256 {release['target_profile_sha256']}\n"
    f"EXPECTED_ELF_SHA256 {release['outputs']['elf']['sha256']}\n"
    "FORBIDDEN_CALLER _atan2_lookup\n"
    "FORBIDDEN_CALLER _atan2s\n"
)
```

The executor never types or estimates these values. V4 preflight verifies release manifest bytes, exact ELF, identity version 2, embedded identity digest/config/profile, then permits disassembly.

Task 8 defines `GOAL_AUDIT_CONTRACT_V4_SHA256: str | None = None` and makes
default v4 integrity verification fail with `v4 audit contract is not pinned`
while no real v4 file exists. Synthetic parser/preflight tests pass an explicit
fixture digest. Task 9 replaces `None` with the sealer's measured digest in the
same commit that adds the immutable contract; no sentinel digest is accepted.

- [x] **Step 4: Add unsealed measurement mode**

Measurement mode runs the existing source-derived audit route analysis and ordinary baseline/oracle integrity checks, writes root, measured total, forbidden-caller observations, exact ELF SHA-256, and release-manifest SHA-256, and labels the result `measured-unsealed`. It never prints PASS for an immutable audit contract.

```python
measurement = {
    "schema": "sm64-saturn-native-math-measurement-v1",
    "status": "measured-unsealed",
    "root": audit_root,
    "total": sum(row.count for row in audited_rows),
    "callers": sorted({row.caller for row in audited_rows}),
    "elf_sha256": file_digest(elf_path),
    "release_manifest_sha256": verified_release.manifest_sha256,
}
write_if_changed(args.measure_audit_report, canonical_json_bytes(measurement))
```

- [x] **Step 5: Implement one-shot contract sealing**

`seal_sh2_native_math_audit_v4.py` verifies the release manifest, measurement schema/status, matching ELF/manifest hashes, exact root `_game_loop_one_iteration`, and absence of both forbidden callers. It writes the canonical v4 text only when output does not exist. It prints the contract SHA-256 so Task 9 can pin that exact digest in `GOAL_AUDIT_CONTRACT_V4_SHA256` with a reviewed patch.

```python
if output.exists():
    raise ValueError(f"refusing to overwrite audit contract: {output}")
if measurement["elf_sha256"] != release["outputs"]["elf"]["sha256"]:
    raise ValueError("measurement ELF differs from release manifest")
for forbidden in ("_atan2_lookup", "_atan2s"):
    if forbidden in measurement["callers"]:
        raise ValueError(f"forbidden caller remains: {forbidden}")
raw = render_contract(measurement, release, manifest_sha256)
output.write_bytes(raw)
print(hashlib.sha256(raw).hexdigest())
```

- [x] **Step 6: Run focused and full verifier suites**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_seal_sh2_native_math_audit_v4.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
```

Expected: every new/v2/v3 compatibility test passes. If the known unrelated null-camera proof remains the only failure, record the exact 228/229-style count rather than calling the full suite green.

- [x] **Step 7: Update docs, commit support code, and clear both reviews**

```powershell
git add CHANGELOG.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/seal_sh2_native_math_audit_v4.py tools/saturn/test_seal_sh2_native_math_audit_v4.py tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py
git commit -m "feat(saturn): add release-bound native math audit v4"
```

Do not create or pin the real v4 contract in this task. Reviewers must verify measurement cannot masquerade as acceptance, v2/v3 immutability, fail-fast ordering, and one-shot sealing.

---

### Task 9: Reproducible identity-v2 BOB target, release seal, and audit-v4 contract

**Files:**
- Create: `tools/saturn/sh2_native_math_goal_audit_contract_v4.txt`
- Modify: `tools/saturn/verify_sh2_native_math.py` (pin only the generated v4 contract digest)
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Create: `docs/saturn/evidence/reports/hermetic-sourceboot-release-2026-08-10.md`
- Create: `docs/saturn/evidence/reports/hermetic-sourceboot-reproducibility-2026-08-10.json`
- Create: `docs/saturn/evidence/reports/sh2-native-math-goal-measurement-v4-2026-08-10.json`
- Create: `docs/saturn/evidence/reports/sh2-native-math-goal-audit-v4-2026-08-10.json`
- Modify: `CHANGELOG.md`
- Modify: `STATE.md`
- Modify: `ROADMAP.md`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`

**Interfaces:**
- Consumes: all Tasks 1–8 and the exact accepted BOB flag tuple.
- Produces: two clean release-mode builds with byte-identical canonical manifests and artifacts, one exact release manifest, one generated/pinned v4 contract, a passing v4 audit, measured low-RAM/cart/package facts, and a deployment-staged manual candidate.

**Live status (2026-08-10):** `active` from reviewed Task 8 closeout
`038391a4`. Required local prerequisites are present: the Saturn-tools Python
environment, `baserom.us.z64`, generated `build/us_pc` inputs, and pinned Yaul
SH-2 toolchain. The v4 contract, measurement report, reproducibility report,
and manual-candidate staging destination are absent, as required for exclusive
one-shot publication. Existing unrelated dirty and untracked work remains out
of scope and must be preserved. Candidate A may start only after checked-in
source-closure inputs are proven tracked and clean; any relevant dirt,
non-reproducible artifact, forbidden audit caller, capacity/package failure, or
release-verification failure stops sealing and staging rather than weakening a
gate.

The first exact candidate-A invocation was discarded before SH-2 compilation:
the Windows wrapper resolved the documented `mingw32-make` spelling to inherited
Qt GNU Make 4.2.1 because this MSYS2 installation names its installed Make
`usr/bin/make.exe`. That compiler driver cannot parse sourceboot's documented
GNU Make 4.3 grouped targets and misparsed Yaul dependency discovery. Task 9's
narrow TDD correction maps only that public spelling to MSYS2 Make, checks the
4.3 minimum, and otherwise preserves the exact command/flag tuple. Reference
reuse is pattern-only from the in-tree full-path MSYS Make invocations and
existing wrapper contract at controller base `8588d391`; no external source was
copied. Candidate A must restart from the exact command after this separately
reviewable behavior change, and all release gates remain open until then.

The first post-fix restart was also discarded before identity discovery: MSYS
Make passed the plan's backslash-form `SOURCEBOOT_PYTHON` assignment into
`/bin/sh`, where the backslashes became escapes and the interpreter path became
`D:Code...python.exe`. Direct wrapper execution proved that the same exact
interpreter launches as `D:/Code/.../python.exe`. The binding below therefore
normalizes only that explicit assignment to forward slashes; the profile,
release mode, toolchain, `-j1`, and all accepted BOB flags remain unchanged.

The next restart completed assets and reached identity discovery, then was
discarded before closure generation when the same POSIX-shell boundary consumed
the backslashes in `YAUL_INSTALL_ROOT` before the first `sh-elf-gcc -MM` call.
Direct wrapper execution proves the pinned GCC 14.3.0 launches from the
forward-slash spelling. The exact binding below now normalizes both explicit
Windows absolute inputs passed into MSYS Make. Candidate B must detach at the
final source commit that produces successful candidate A, not historical Task
8 head: these narrow Task 9 execution fixes are part of the candidate source
identity and remain subject to controller-owned independent review.

The following run completed all 228 dependency-only scans but was discarded at
closure assembly before cleanliness evaluation: expanding every discovered
path as repeated Python arguments exceeded the Windows/MSYS command-line limit
(more than 50 KiB of depfile paths alone). TDD now carries every large build
and post-link path family through strict `sm64-saturn-path-list-v1` transports.
The LF-only lists reject blank, padded, NUL, duplicate, or byte-unsorted rows;
their semantic paths derive the unchanged canonical source closure, while the
transport files themselves stay outside identity. Windows Python converts the
canonical MSYS drive spelling only at the transport boundary. This is a clean-room
adaptation of the existing in-tree canonical-handoff pattern at `bc6d9441`;
no external source was copied. Candidate A and closure cleanliness remain open
until the exact release command succeeds from the resulting source commit.

The first run from `21b79b1c` proved the bounded handoff works, then failed
closed during semantic classification because implementation-worktree
`build/us_pc` is a junction to the parent checkout. That allowed generated
inputs to resolve outside the guarded repository root, so no cleanliness or
target gate was claimed and the junction remains untouched. Candidate A is now
moved to a fresh detached, fully owned sibling; candidate B is a second fresh
detached sibling at the same exact source commit. Each independently receives
only `baserom.us.z64` and an inventoried copy of `build/us_pc`, with relative
path, length, and SHA-256 equality required before build. This material
isolation correction preserves user state and strengthens reproducibility.

The two owned candidate roots are
`D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\hermetic-release-repro-a`
and
`D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\hermetic-release-repro-b`.
Each `build/us_pc` is an ordinary directory (not a reparse point), and each
matches the source prerequisite across 1,977 files by relative path, byte
length, and per-file SHA-256. The canonical inventory digest for the source and
both copies is
`43019209f5080af9a9d4a1c876176b5b9724c17913a4786a573f4ad2275fe7a1`.
The independently copied ROMs likewise match at SHA-256
`17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
Both detached roots were tracked-clean before those ignored prerequisites were
installed. Because the managed execution identity differs from the worktree
creator identity, the build function scopes Git trust to the active candidate
through inherited `GIT_CONFIG_*` variables; it does not change global Git
configuration. Candidate A has not restarted yet, and both candidates must
detach at the final common source commit before either build begins.

The first owned-candidate invocation stopped at `check-libyaul`, before source
discovery or SH-2 compilation, because `git worktree add` does not materialize
submodule contents. Both candidates now materialize the superproject's pinned
`third_party/libyaul` gitlink at
`6012f79f237773378c8014e70d8998ad95a38d98`, with clean tracked state. This is
tracked source-checkout completion, not an additional ignored prerequisite.
The clone used a one-shot local `file://` transport from the implementation
worktree's clean checkout after restricted networking rejected the upstream
URL; no repository URL or global Git configuration changed. Reference record:
`yaul-org/libyaul`, commit `6012f79f237773378c8014e70d8998ad95a38d98`,
MIT license, full pinned submodule checkout, dependency/materialization reuse.
Candidate A remains unbuilt after this preflight-only stop.

The next candidate-A restart entered `identity-assets` but stopped before
identity discovery when `bake_bob_tiles.py` could not find
`levels/bob/0.rgba16.png`. A fresh candidate intentionally has no copied
ignored extracted assets, exposing that sourceboot consumed root-extracted
PNGs without deriving them from its allowed baserom. The corrected general
boundary invokes the existing project extractor with an explicit
candidate-local output root under `build/saturn/sourceboot/generated`, orders
that prerequisite before BOB tile/fragment/sky consumers, and tells the
recursive root Make to retain the already-inventoried `build/us_pc` inputs via
`NOEXTRACT=1`. A strict second generated-input list seals 1,539 US assets plus
the extractor manifest (1,540 rows) and post-link verification rediscovers
their bytes. The default extractor output and positional language interface
remain compatible for root Make callers. TDD RED was the hermetic Make suite
at 8/9 and the isolated-output suite at 0/1; GREEN is 9/9 and 2/2, with source
closure 20/20 plus its existing case-filesystem skip and changed Python
compilation clean. Reference record: the in-tree `Makefile` asset-extraction
boundary at `fc56b14a`; this inherited SM64 fork has no root license file at
that revision, so existing project terms remain unchanged; files inspected were
`Makefile` lines 239-252 and `extract_assets.py`; same-repository close-port
reuse introduced no external source or new notice.
Candidate A and all release gates remain open pending the exact rerun from the
behavior commit.

The first run from that behavior commit successfully derived the candidate-local
asset set, then stopped before identity discovery because
`compile-bob-tiles` hard-coded `SATURN_REPO_ROOT` instead of honoring the
caller-supplied `BOB_ASSET_ROOT`; the sibling fragment recipe already used the
public override. A focused Make-contract assertion was RED at 8/9 with one
observed use versus two required consumers, then GREEN at 9/9 after the tile
recipe adopted the same override. This is a one-line consumer fix with no
profile, output-root, inventory, toolchain, or serial-execution change.
Candidate A and every downstream gate remain open pending a clean exact rerun.

The next restart got past the corrected initial tile bake but stopped before
discovery while generating `bob_scene.h`: the scene delegation re-invokes the
outer `compile-bob-scene` target, whose nested tile/BSP prerequisites require
the same `BOB_ASSET_ROOT`, but that delegation did not forward it. The focused
contract was RED at 8/9 with two of three sourceboot BOB delegations bound;
GREEN is 9/9 after scene propagation. This does not change the extracted bytes
or closure; it makes all nested consumers use the already-sealed root. Candidate
A and every downstream gate remain open pending another clean exact rerun.

That rerun derived the complete candidate-local asset inventory and completed
all BOB consumers, then stopped before identity discovery when `source-assets`
invoked root Make. Root Make unconditionally rebuilt every host tool, including
obsolete `armips` sources that fail under the current host compiler, although
the requested `build/us_pc` targets were already present in the independently
inventoried prerequisite tree. The narrow correction adds `NOTOOLS ?= 0` at
that existing boundary and passes `NOTOOLS=1` only from sourceboot's verified
asset-target submake. Default root Make behavior is therefore unchanged;
missing named generated targets still execute their normal recipes and fail
closed. Focused TDD was RED at 8/9 before the boundary existed and GREEN at
9/9 afterward. Reference record: same-repository pattern-only adaptation of
the root `NOEXTRACT` opt-out and sourceboot recursive Make call at `9949e655`;
this inherited fork has no root license file, no external source was copied,
and no notice obligation changed. Candidate A, closure, target, and release
gates remain open pending an exact clean rerun from this behavior commit.

The run from `fcfe8068` confirmed the broad all-tools pass was gone and that a
missing target still failed, but exposed a second distinction: fresh-worktree
source mtimes were newer than the independently copied, byte-identical
`build/us_pc` files. Root Make therefore treated
`build/us_pc/text/us/define_text.inc.c` as stale and attempted its recipe,
which stopped on absent `tools/textconv`. No discovery or downstream gate was
claimed. The corrected release boundary no longer interprets mtimes as input
identity: `prepare_sourceboot_assets.py --verify-existing` computes the same
bounded selected target set, adds the explicit text and water-sky inputs,
rejects missing or root-escaping files, and leaves exact byte hashing to source
closure construction and rediscovery. Development mode retains the unmodified
legacy root Make materialization path, including normal tool rebuilding.
Focused TDD was RED at 8/10 (missing mode and CLI) and GREEN at 10/10, including
a newer-source/older-generated fixture and missing-file rejection. Reference
record: close-port/pattern-only reuse of the same in-tree target collector and
release closure boundary at `fcfe8068`; no external source or new license/notice
obligation. Candidate A and all target/release gates remain open pending the
exact rerun from the superseding commit.

Candidate A from `6781cb5f` passed release prerequisite verification and all
228 compiler dependency scans, then failed closed during source-closure
assembly because Yaul's valid sourceboot-local `main.c` spelling was written
unchanged to a list whose consumer resolves paths from repository root. The
compiled-source handoff now applies Make's `abspath` to each `SH_SRCS_UNIQ`
entry before filtering generated inputs, so relative local entries identify
the exact compiled files while absolute repository/generated entries remain
unchanged. Focused TDD was RED at 10/11 and GREEN at 11/11. Reference record:
pattern-only reuse of Yaul's existing absolute build-path normalization in the
pinned MIT-licensed submodule at `6012f79f`, files inspected
`libyaul/build/build.pre.mk` and `build.post.iso-cue.mk`; no upstream bytes were
copied and no notice changed. Closure, compile, link, seal, and release gates
remain open pending an exact rerun from this source commit.

The next run passed explicit compiled-source classification but stopped at the
same discovery stage when parsing GCC depfile contents: dependency rows use
canonical MSYS `/d/...` spellings, while Windows Python's existing drive-path
conversion covered only path-list transport rows. The unconverted dependency
became `D:\d\...`, correctly rejected as an absent external path. Dependency
resolution and canonical repository classification now both apply the same
existing `_host_transport_path` conversion before filesystem semantics; output
closure paths remain host-independent repository-relative values. Windows TDD
reproduced the exact MSYS dependency spelling: RED 20/21 plus the existing
case-filesystem skip, GREEN 21/21 plus that same skip. Reference record:
same-file close-port reuse of `_host_transport_path` at `b7bdb347`; no external
source, license, or notice change. No closure or downstream target/release gate
is claimed pending the exact rerun.

The run from `2bf05467` progressed through MSYS dependency conversion, then
stopped before closure publication on the unresolved generated identity include
`saturn_build_identity_values.inc`. Real depfiles also contain valid
sourceboot-working-directory rows including `main.c`, `source_cart.h`, and
`../runtime/...`; repository-root interpretation was therefore wrong beyond
the single derived file. The closure CLI now accepts an explicit
`--dependency-base`, bounded inside the repository and supplied as sourceboot
`$(CURDIR)` for both discovery and post-link rediscovery. Relative existing
dependencies resolve from that base. A missing single-component dependency may
alias only one explicitly declared derived output with the same basename,
which models GCC `-MG` without materializing the pre-seal identity; zero matches
remain missing and multiple matches fail as ambiguous. TDD was RED in both
suites (closure 21/22 plus existing skip; Make 10/11), then GREEN at closure
22/22 plus the same skip and Make 11/11, including ambiguous-alias rejection.
Reference record: clean-room extension of the existing Task 2 explicit-derived
class and sourceboot Make working-directory semantics at `2bf05467`; no external
source or license/notice change. Closure and downstream gates remain open until
the exact rerun succeeds.

Candidate A from `f9b1345e` successfully constructed the full source closure
and reached toolchain attestation, but atomic discovery publication stopped
because Yaul supplies `sh-elf-gcc.exe` for both the compiler and linker-driver
roles. The v1 attestation schema records a canonical set of measured binary
paths rather than role labels, so the sourceboot CLI composer now deduplicates
only exact repeated path spellings after requiring every role argument. Direct
component duplicates remain rejected, and case-variant paths remain distinct
long enough for the existing collision check to reject them. TDD was RED at
15/16 and GREEN at 16/16 with the real compiler/linker-driver topology.
Reference record: clean-room correction against the in-tree Task 3 schema and
pinned Yaul Make variables at `f9b1345e`; no external source or license/notice
change. The closure bytes existed only as an uncommitted `.tmp`; the combined
closure/attestation publication, compile, link, and release gates remain open
pending the exact rerun.

The run from `a35c2a93` again built the closure `.tmp` and advanced through the
shared compiler/linker-driver topology, then attestation rejected the literal
extensionless Windows path `sh-elf-readelf` because the installed concrete file
is `sh-elf-readelf.exe`. MSYS command lookup hides that distinction during
execution, but a byte attestation must not. Sourceboot now defines a Windows-only
`.exe` suffix for its custom objdump/readelf/addr2line bindings; non-Windows
spelling is unchanged, and both attestation and native-math verification consume
the same exact files. Focused Make TDD was RED at 10/11 and GREEN at 11/11.
Reference record: close-port reuse of `HOST_EXEEXT` in the in-tree
`Makefile.saturn.mk` at `a35c2a93`; no external source or license/notice change.
Atomic discovery publication and downstream target/release gates remain open.

Candidate A from `1dea5fad` completed all 228 dependency scans and atomically
published the source closure, external-dependency handoff, and toolchain
attestation. Seal-stage bootstrap then rejected the checked-in target profile
before compilation because the candidate creator's Windows `core.autocrlf`
setting had checked its canonical LF Git blob out as CRLF. The byte-strict
rejection was correct; accepting either spelling would make release identity
host-dependent. Tracked JSON is now explicitly `eol=lf` in `.gitattributes`,
covering profiles, descriptors, routes, and every other JSON identity input.
Focused TDD was RED at 11/12 target-profile tests (`eol: unspecified`) and
GREEN at 12/12, with identity-bootstrap regression GREEN at 15/15. Candidate
checkouts must be refreshed at the resulting commit so the attribute takes
effect; closure, compile, link, seal, and release gates remain open until the
exact candidate rerun succeeds.

The rerun from `e07d7ece` proved all selected JSON files were LF and again
atomically published closure, external handoff, and attestation, but bootstrap
still rejected the profile before compilation. Byte comparison isolated the
remaining defect: `area_id`, added during Task 1 review repair, was appended at
the end of `release_config` instead of its canonical sorted position. The
profile's accepted values and meaning were correct; only its raw order was
noncanonical. A real-file contract now requires the release profile and all ten
selected descriptors to equal their canonical serialization. TDD was RED at
12/13 target-profile tests with the release profile as the only failing file,
then GREEN at 13/13; identity bootstrap remains GREEN at 15/15 and hermetic Make
at 11/11. Discovery publication is distinguished from the still-open identity,
compile, link, seal, and release gates pending the exact rerun from the resulting
source commit.

Candidate A from `a18f612e` passed canonical-profile validation and identity
bootstrap, then stopped before identity-label publication or target compilation.
The Task 5 spec correctly names sealed artifacts with repository-relative paths,
but all three generator invocations ran from sourceboot's nested working
directory and therefore looked for `src/port/saturn/sourceboot/build/...`.
Sourceboot now centralizes those invocations behind a root-bound command for
label lookup, directory-tag lookup, and generated identity outputs; the public
identity CLI retains its existing caller-relative behavior. Make TDD was RED at
11/12 and GREEN at 12/12; build-identity remains GREEN at 21/21 and bootstrap at
15/15. Reference record: same-repository close-port of bootstrap's explicit
`--root` invocation/base contract and Task 5's reviewed repository-relative
spec at `a18f612e`; no external source, license, or notice change. Identity,
compile, link, seal, and release gates remain open pending the exact rerun.

Candidate A from `7a8271b8` successfully published identity v2 with sealed tag
`id-d4082ee7fde66135`, passed geo-depth capacity at 172/192 across 518 inputs,
and entered real target compilation. The first object then failed because this
GCC driver searches `PATH` for its unprefixed `as` helper and the Windows
wrapper placed MSYS `mingw64/bin` ahead of Yaul; host `as.exe` rejected SH-2's
`-big` option. The wrapper now puts the selected `YAUL_INSTALL_ROOT/bin` first,
while keeping both MSYS DLL roots ahead of inherited PATH. TDD was RED at 2/3
wrapper tests and GREEN at 3/3; a disposable one-line SH-2 compile produced a
552-byte object under the corrected order, and that probe was removed before
resealing. Reference record: same-repository close-port of the existing
`test_camera_acceptance_route.py` Yaul helper-path setup at `7a8271b8`; no
external source, license, or notice change. Compile completion, link, seal, and
release gates remain open pending the exact rerun from the behavior commit.

The exact rerun from `7cee9a12` kept identity tag `id-d4082ee7fde66135` but
proved the wrapper-level order was insufficient: pinned Yaul
`share/build.pre.mk:66` prepends `/mingw64/bin` after the wrapper starts, so the
first object again found host `as`. GCC's own `-B` program prefix is now bound
to `$(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-` for C, C++, preprocessed
assembly, and link flags. A verbose compile under deliberately host-first PATH
showed the concrete invoked helper was `sh-elf-as.exe`; `-print-prog-name=ld`
resolved `sh-elf-ld.exe`. Toolchain attestation now measures those exact
assembler and linker files while GCC remains independently measured as the
link driver. TDD was RED at 12/13 hermetic Make tests and GREEN at 13/13, with
attestation 16/16 and wrapper 3/3. All disposable objects and the temporary
source fixture were removed before resealing. Reference record: close-port of
GCC's existing in-tree `-B` link-search pattern and pinned Yaul helper naming at
commit `7cee9a12`; no external source, license, or notice change. Compile, link,
seal, and release gates remain open pending the exact rerun.

Candidate A from `d1699ab7` preserved the new sealed identity tag
`id-99f6dfa38dcc64f8`, passed canonical bootstrap and geo-depth capacity, and
compiled all 75 main sourceboot objects with the exact attested assembler. It
then stopped in the separately compiled software-float runtime: `SOFTFP_CFLAGS`
is an independent flag set and did not inherit the main C flags, so
`softfp/addsf3.o` again launched MSYS host `as.exe` and rejected `-big`. No
link, post-link seal, release manifest, or reproducibility gate is claimed.
The software-float flag set now carries the same explicit GCC `-B` prefix.
Focused TDD was RED at hermetic Make 12/13 and GREEN at 13/13; toolchain
attestation remains GREEN at 16/16 and the Windows wrapper at 3/3. Reference
record: same-repository close-port of the already-reviewed main sourceboot
helper binding into the adjacent custom soft-float recipe, with the vendored
runtime boundary documented by `third_party/gcc-soft-fp/README.md`; no external
bytes, license, or notice changed. Candidate A must restart at the resulting
common source commit; compile completion, link, seal, release, and
reproducibility gates remain open.

Candidate A from `47ab8135` published the new identity tag
`id-8a95684a4575e95c`, passed closure, attestation, canonical bootstrap, and
geo depth at 172/192, compiled the complete main and software-float object
sets, and linked the ELF. Packaging then stopped before `IP.BIN`: pinned
libyaul's `wrap-error` defaults `TMPDIR` to `/tmp`, which is not writable by
the managed execution identity, so its first `mktemp` failed. No IP.BIN,
ISO/CUE, post-link seal, release manifest, or reproducibility gate is claimed.
Sourceboot now exports an identity-tagged, candidate-local temporary directory
under `$(SH_BUILD_DIR)` and creates it as an order-only prerequisite of Yaul's
first wrapper consumer. Hook sub-makes and later `make-iso`/`make-cue` inherit
the same directory. Transient path and stderr bytes remain ignored and outside
canonical identity; the tracked recipe stays in source closure and semantic
package bytes remain release-manifest-bound. TDD was RED at Make 13/14 and
GREEN at 14/14, with attestation 16/16 and wrapper 3/3 (33/33 combined).
Reference record: pinned MIT libyaul commit
`6012f79f237773378c8014e70d8998ad95a38d98`, inspected
`libyaul/build/build.post.iso-cue.mk` and `libyaul/common/wrap-error`; dependency
integration plus pattern-only reuse of the repository's existing explicit
temporary-environment recipe, with no copied upstream bytes or notice change.
Candidate A must restart at the resulting common source commit; package,
post-link seal, release, and reproducibility gates remain open.

Candidate A from `d48e069e` published identity tag
`id-fb999abbcc42c7fa`, completed compilation/link, and proved the candidate
temporary boundary by producing `IP.BIN` (4,108 bytes), `SOURCE.DAT`
(3,565,696 bytes), ISO (4,968,448 bytes), and CUE (88 bytes). It then stopped
inside post-link source-closure verification: the recipe-level `foreach` for
fresh assembly scans expanded all GCC commands onto one shell line. Later GCC
paths became operands of the first invocation, only the final depfile was
written, and the verifier correctly rejected the missing generated
`bob_sky_bitmap.sx` depfile. No post-link closure pass or release manifest is
claimed. Each post-link assembly scan is now an explicit Make target whose
prerequisites include the linked ELF, its own source, and the force-scan gate;
`verify-sealed-inputs` requires the complete depfile set. This preserves the
required after-link freshness and makes command separation structural under
both serial and parallel Make. TDD was RED at Make 14/15 and GREEN at 15/15;
combined Make, source-closure, and attestation coverage is 53/53 with the one
existing case-filesystem skip. Reference record: clean-room correction using
the existing per-source discovery-rule pattern in the same sourceboot
Makefile; no external source, copied bytes, license, or notice change.
Candidate A must restart at the resulting common source commit; post-link
closure, release, and reproducibility gates remain open.

Candidate A from `0dc7ede0` published identity tag
`id-5125ad1b7f816a3b`, completed compile/link/package, and produced all four
independent fresh assembly depfiles. Closure verification then stopped because
the actual external dependency set differed from sealed discovery. Exact
diagnosis found 4 sealed paths versus 103 actual paths (99 added, 0 missing):
pre-seal discovery used GCC `-MM`, which excludes headers found through
`-isystem`, while pinned Yaul's real compile recipe uses `-MD` and records
those GCC and Yaul system headers. No post-link closure pass or release
manifest is claimed. All C, C++, and preprocessed-assembly discovery plus
post-link scans now use `-M -MG`, retaining missing-generated-header discovery
while matching `-MD` system-header coverage. The complete 103-path external
set will therefore be byte-attested before the next identity is formed rather
than accepted only after compilation. TDD was RED at Make 15/16 and GREEN at
16/16; combined Make, source-closure, and attestation coverage is 54/54 with
the one existing case-filesystem skip. Reference record: close-port of pinned
MIT libyaul commit `6012f79f...` dependency semantics, inspected
`libyaul/build/build.post.bin.mk` (`macro-generate-sh-build-object` and its
`-MD` flags); no copied upstream bytes or notice change. Candidate A must
restart at the resulting common source commit; closure, release, and
reproducibility gates remain open.

Candidate A from `361d27ad` published identity tag
`id-ed9d802a7e9609df`, passed sealed-versus-actual external closure equality,
compiled, linked, packaged, and reached release cleanliness. Cleanliness then
rejected `build/us_pc/actors/amp/amp_body.rgba16.inc.c` as untracked because
compiler discovery had classed the verified copied prerequisite as a generic
header: the exact `build/us_pc` target set selected by
`prepare_sourceboot_assets.py` was verified but never handed to source-closure
classification. No release manifest or reproducibility gate is claimed.
Source asset preparation now atomically emits the verified selected targets in
canonical `sm64-saturn-path-list-v1` form, and discovery consumes that bounded
list as explicit `generated-input` records. Only generated-input records under
`build/` retain the existing Git exception; dirty or untracked checked-in
inputs still fail, and every generated asset byte remains closure-hashed and
post-link-rehashed while the transport list itself stays outside identity.
Focused TDD was RED 0/2 and GREEN 2/2; combined hermetic Make and source-
closure coverage is GREEN 38/38 with one existing case-filesystem skip,
including explicit generated-input mutation rejection. Reference record:
same-repository close-port of the strict path-list boundary already used by
`extract_assets.py` and `gen_source_closure.py`; no external source, copied
bytes, license, or notice change. Candidate A must restart from the resulting
common source commit; release and reproducibility gates remain open.

The first candidate restart from `e943e2b0` emitted a canonical 1,021-row PC
asset inventory but stopped before closure publication on strict duplicate
explicit ownership. Exactly two required rows, `build/us_pc/bin/water_skybox.c`
and `build/us_pc/include/text_strings.h`, were present in both the new inventory
and the legacy static generated-input list; Windows/MSYS transport spellings
differed but canonical repository paths were equal. Duplicate rejection stays
fail-closed. The PC inventory is now the sole explicit owner of those two rows
by filtering them from only the static list; compiled-source exclusion remains
unchanged, and the same bytes retain generated-input classification through
the verified inventory. Focused TDD was RED 0/1 and GREEN 1/1; combined Make
and source-closure coverage remains GREEN 38/38 with one existing case-
filesystem skip. No identity, compile, release manifest, or reproducibility
gate is claimed from the failed run. Candidate A must restart at the resulting
common source commit.

Candidate A from `3df2ec4a` published identity tag
`id-c7c5e79df283e6a5`, completed compile/link/package and external closure
equality, then release cleanliness rejected the sole remaining generic
`build/us_pc` header: `include/text_menu_strings.h`. The 1,021-row direct
inventory included `text_strings.h`, whose quoted generated child was visible
to the compiler but not the inventory. No release manifest or reproducibility
gate is claimed. The verified inventory walk now follows quoted includes only
when their resolved paths stay within the bounded `build/us_pc` root, rejects
missing children there or escaping direct targets, and publishes the resulting
1,022 exact inputs
as generated-input rows. This restores the already independently reviewed
identity-v1 rule from commit `46162df5` at the source-closure-v2 boundary;
reuse mode is close-port from the same GPL-compatible repository and no
external source, copied notice, or license change is involved. Focused TDD was
RED 0/1 and GREEN 1/1; combined Make/source-closure coverage is GREEN 38/38
with one existing case-filesystem skip. Candidate A must restart from the
resulting common source commit; release and reproducibility remain open.

The first candidate restart from `085cdbc9` stopped during verified asset
inventory, before discovery or identity publication. The transitive walker
treated `water_skybox.c`'s quoted `types.h` as a missing local generated child,
but pinned root Make compiles that generated C source with repository include
paths; no `build/us_pc/bin/types.h` should exist. The reviewed historical walk
did not place water-skybox C through its recursive input set. The close-port now
requires missing local children only when walking generated headers, while
following any existing in-root child and directly inventorying/hash-sealing all
generated C/inc.c targets. Real compiler-resolved headers remain covered by
pre-seal and post-link depfiles. A water-style regression was RED 0/1 and GREEN
1/1; combined Make/source-closure coverage remains GREEN 38/38 with one
existing case-filesystem skip. Candidate A must restart from the resulting
common source commit; all release/reproducibility gates remain open.

Candidate A from `d816c3dc` published identity tag
`id-0fe6b6cd91ccfeb5`, completed compile/link/package and external closure
equality, and cleared the complete 1,022-row PC asset classification. Release
cleanliness then rejected
`third_party/libyaul/libyaul/build/build.post.bin.mk` because root
`git ls-files` tracks the pinned libyaul checkout as a mode-160000 gitlink, not
as individual superproject files. No release manifest or reproducibility gate
is claimed. Cleanliness now maps only otherwise-untracked closure paths to an
enclosing indexed gitlink, requires the initialized nested checkout HEAD to
equal that exact index object, and requires each relevant nested path tracked
and clean under an exact per-submodule `safe.directory`. The superproject
gitlink must also be clean; unrelated nested dirt remains outside the closure.
Focused TDD was RED 0/1 and GREEN 1/1 across clean, dirty relevant-file, and
unpinned-HEAD cases; combined source-closure, hermetic-Make, and attestation
coverage is GREEN 55/55 with one existing case-filesystem skip. Reference:
clean-room integration of Git's existing indexed gitlink/checkout contract;
no external source, copied bytes, license, or notice change. Candidate A must
restart at the resulting common source commit; release/reproducibility remain
open.

Candidate A from `8912faeb` published identity tag
`id-2644d689b9eb77c6`, completed compile/link/package and external equality,
then the new nested check reported the three relevant libyaul recipes dirty.
Immediate diagnosis proved exact HEAD `6012f79f`, an empty host-Windows-Git
status, and an MSYS-Git status showing all three modified; its diff was
line-ending-only and `--ignore-space-at-eol` clean. The target-build `PATH`
selects MSYS Git, whose system config lacks the Windows Git
`core.autocrlf=true` used to materialize the checkout. No release manifest or
reproducibility gate is claimed. On Windows, release-cleanliness Git commands
now bind `core.autocrlf=true` locally, including the submodule check; exact
working bytes remain closure-hashed before this status-only normalization, so
semantic changes and unpinned commits still fail. A CRLF checkout fixture with
system Git config disabled was RED 0/1 and GREEN 1/1; the semantic dirty-file
and unpinned-HEAD cases remain GREEN. Combined closure/Make/attestation
coverage remains 55/55 with one existing case-filesystem skip. Candidate A
must restart from the resulting common source commit; release/reproducibility
remain open.

Candidate A from `4dbf515f` published identity tag
`id-6d6ecbaa8345b28f`, completed compile/link/package and external equality,
and cleared nested libyaul cleanliness. Root cleanliness then raised Windows
`WinError 206` before manifest sealing because it expanded the complete
checked-in closure into one `git status` argv. No release manifest or
reproducibility gate is claimed. Root and nested status checks now partition
the exact ordered path set into deterministic commands whose rendered Windows
command lines are at most 16,000 characters; each path remains checked once,
and any dirty batch or individually overlong path fails closed. Focused TDD
was RED 0/1 and GREEN 1/1; combined source-closure, Make, and attestation
coverage is GREEN 56/56 with one existing case-filesystem skip. Reference:
clean-room use of Git's existing path-limited status interface; no external
source, copied bytes, license, or notice change. Candidate A must restart from
the resulting common source commit; release/reproducibility remain open.

Candidate A from `535cba13` published identity tag
`id-a0b8a3f2d9a15efe`, completed compile/link/package and external equality,
and passed root plus nested cleanliness, proving the Windows command-boundary
correction on the real 4,234-record closure. Manifest construction then
rejected all seven recipe rows because their canonical schema owner
`linker/build-recipe` contains a slash. No manifest or reproducibility gate is
claimed. Release validation now accepts exact `CLASS_PRECEDENCE` values as
schema-defined owners while retaining portable-identifier validation for all
other owner values, so malformed arbitrary owners and case collisions still
fail closed. Focused TDD was RED 0/1 and GREEN 1/1; full release-manifest
coverage is GREEN 27/27 and staging coverage is GREEN 20/20. Reference:
clean-room reconciliation of `release_manifest.py` with the same repository's
canonical `gen_source_closure.py` schema; no external source, copied bytes,
license, or notice change. Candidate A must restart from the resulting common
source commit; manifest/reproducibility remain open.

- [ ] **Step 1: Reconcile HEAD, ledgers, toolchain, and dirty closure state**

```powershell
git status --short
git log -12 --oneline
git diff --check
```

Confirm Tasks 1–8 and both reviews per task are recorded. Preserve unrelated dirt. Release mode may proceed only if every checked-in source-closure input is tracked and clean; if a relevant file is dirty, stop and reconcile ownership instead of hiding it.

- [ ] **Step 2: Build owned release-mode candidate A with exact profile and serial execution**

```powershell
$implementationRoot = (Get-Location).Path
$projectRoot = Split-Path (Split-Path (Split-Path $implementationRoot -Parent) -Parent) -Parent
$env:YAUL_INSTALL_ROOT = (Join-Path $projectRoot 'work\yaul-install').Replace('\', '/')
$env:YAUL_PROG_SH_PREFIX = 'sh-elf'
$env:YAUL_ARCH_SH_PREFIX = 'sh-elf'
$env:YAUL_ARCH_M68K_PREFIX = 'm68keb-elf'
if (-not (Test-Path -LiteralPath (Join-Path $env:YAUL_INSTALL_ROOT 'bin\sh-elf-gcc.exe'))) {
    throw "pinned Yaul toolchain is missing: $env:YAUL_INSTALL_ROOT"
}
$task9MakeArguments = @(
    '-f', 'Makefile.saturn.mk', '-j1', 'verify-sourceboot',
    'SOURCEBOOT_TARGET_PROFILE=tools/saturn/profiles/sourceboot-bob-demo-v1.json',
    'SOURCEBOOT_RELEASE_MODE=release',
    'SATURN_DEMO_PATH=1', 'SATURN_SOURCEBOOT_ROUTE_REPLAY=1',
    'SATURN_SOURCEBOOT_LIVE_INPUT=1', 'SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600',
    'SATURN_SOURCEBOOT_LEVEL_ID=9', 'SATURN_SOURCEBOOT_AREA_ID=1', 'SATURN_SOURCEBOOT_ROUTE_ID=0',
    'SATURN_SOURCEBOOT_CAMERA_ROUTE=0', 'SATURN_CAMERA_VARIANT=3',
    'SATURN_CAMERA_IDLE_START_TICK=0', 'SATURN_CAMERA_IDLE_DISCOVERY=0', 'SATURN_CAMERA_RANGE_CAPTURE=0',
    'SATURN_CART_MBIT=32', 'SATURN_SOURCE_CART_STAGE_SECTORS=8',
    'SATURN_DEMO_HOT_PROMOTION=1', 'SATURN_DEMO_NEAR_CLIP=1', 'SATURN_DEMO_BSP_ORDER=1',
    'SATURN_DEMO_POLY_TIER=2', 'SATURN_DEMO_BSP_FRAGMENTS=0', 'SATURN_DEMO_FRAGMENT_MODE=0',
    'SATURN_DEMO_BSP_FRAGMENT_FLAT=0', 'SATURN_RENDERER_PIPELINE=4', 'SATURN_SLAVE_RENDER=1',
    'SATURN_ATAN2_VARIANT=2', 'SATURN_DEMO_VIEW_RADIUS=6000', 'SATURN_DIAGNOSTIC_MODE=0',
    'SATURN_FAST3D_Q16_TRACE=0', 'SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0',
    'SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1', 'SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1',
    'SATURN_FEATURE_SEMANTIC_AUDIO=0', 'SATURN_OBJECT_POOL_CAPACITY=208'
)
function Invoke-HermeticBobBuild([string]$repoRoot) {
    Push-Location $repoRoot
    try {
        # Scope Git trust to this candidate; do not mutate global config.
        $env:GIT_CONFIG_COUNT = '1'
        $env:GIT_CONFIG_KEY_0 = 'safe.directory'
        $env:GIT_CONFIG_VALUE_0 = $repoRoot.Replace('\', '/')
        $python = (Join-Path $implementationRoot '.venv-saturn-tools\Scripts\python.exe').Replace('\', '/')
        $arguments = @($task9MakeArguments) + @("SOURCEBOOT_PYTHON=$python")
        powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 `
            mingw32-make @arguments
        if ($LASTEXITCODE -ne 0) { throw "sourceboot release build failed in $repoRoot" }
    } finally {
        Pop-Location
    }
}
$sourceCommit = (git rev-parse HEAD).Trim()
$candidateARoot = [IO.Path]::GetFullPath((Join-Path $implementationRoot '..\hermetic-release-repro-a'))
git worktree add --detach $candidateARoot $sourceCommit
Copy-Item -LiteralPath (Join-Path $implementationRoot 'baserom.us.z64') `
  -Destination (Join-Path $candidateARoot 'baserom.us.z64')
New-Item -ItemType Directory -Path (Join-Path $candidateARoot 'build') | Out-Null
Copy-Item -LiteralPath (Join-Path $implementationRoot 'build\us_pc') `
  -Destination (Join-Path $candidateARoot 'build\us_pc') -Recurse
# Before build, require source/destination prerequisite inventories to match by
# relative path, byte length, and SHA-256; the implementation junction itself
# is never admitted to either candidate root.
Invoke-HermeticBobBuild $candidateARoot
```

Expected: ordinary sourceboot gates, post-link closure verification, and release-manifest verification pass. Audit v4 is not yet selected.

- [ ] **Step 3: Verify and retain candidate A's immutable manifest**

Resolve the current identity-tagged directory from the generated identity JSON,
then verify its manifest:

```powershell
function Get-SealedCandidate([string]$repoRoot) {
    $identityPath = Join-Path $repoRoot 'build\saturn\sourceboot\generated\saturn_build_identity.json'
    $identity = Get-Content -Raw -LiteralPath $identityPath | ConvertFrom-Json
    $tag = 'id-' + $identity.identity.effective_config_hash.Substring(0, 16)
    $directory = Join-Path $repoRoot "build\saturn\sourceboot\e2-bob-identity-$tag"
    [pscustomobject]@{
        Root = $repoRoot
        Directory = $directory
        Elf = Join-Path $directory 'obj\sm64-saturn-sourceboot-e2.elf'
        Manifest = Join-Path $directory 'saturn-release-manifest-v1.json'
    }
}
$candidateA = Get-SealedCandidate $candidateARoot
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\release_manifest.py verify `
  --manifest $candidateA.Manifest
```

The report records canonical profile, closure, package-set, toolchain, identity, ELF, `SOURCE.DAT`, ISO, CUE, and release-manifest hashes.

- [ ] **Step 4: Rebuild candidate B from a fresh identity-tagged output directory**

Use `superpowers:using-git-worktrees` to create a clean detached sibling
worktree named `hermetic-release-repro-b` at the exact source commit used by
successful candidate A. That commit includes any separately reviewable Task 9
execution correction required to produce A and remains subject to Task 9's
independent reviews. Copy
only ignored prerequisite inputs (`baserom.us.z64` and the verified
`build/us_pc` generated-input tree) into the new worktree; do not copy any
identity, object, package, sourceboot-generated, or final artifact output.

```powershell
$reproRoot = [IO.Path]::GetFullPath((Join-Path $implementationRoot '..\hermetic-release-repro-b'))
git worktree add --detach $reproRoot $sourceCommit
Copy-Item -LiteralPath (Join-Path $implementationRoot 'baserom.us.z64') `
  -Destination (Join-Path $reproRoot 'baserom.us.z64')
New-Item -ItemType Directory -Path (Join-Path $reproRoot 'build') | Out-Null
Copy-Item -LiteralPath (Join-Path $implementationRoot 'build\us_pc') `
  -Destination (Join-Path $reproRoot 'build\us_pc') -Recurse
Invoke-HermeticBobBuild $reproRoot
$candidateB = Get-SealedCandidate $reproRoot
& (Join-Path $implementationRoot '.venv-saturn-tools\Scripts\python.exe') `
  (Join-Path $implementationRoot 'tools\saturn\release_manifest.py') verify `
  --manifest $candidateB.Manifest
```

Compare the two verified manifests:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\release_manifest.py compare `
  --first $candidateA.Manifest `
  --second $candidateB.Manifest `
  --output docs\saturn\evidence\reports\hermetic-sourceboot-reproducibility-2026-08-10.json
```

Expected: byte-identical target profile, source closure, package/class roots, toolchain attestation, embedded identity, ELF, `SOURCE.DAT`, ISO, CUE, and normalized release-manifest identity/output records. If any output differs, stop; do not seal v4 around nondeterminism.

- [ ] **Step 5: Measure native math from candidate B without claiming acceptance**

Run `verify_sh2_native_math.py` with the existing baseline, route oracle,
simulation audit route oracle, SH tools from the verified Task 3 toolchain,
`$candidateB.Elf`, `$candidateB.Manifest`, and
`--measure-audit-report docs/saturn/evidence/reports/sh2-native-math-goal-measurement-v4-2026-08-10.json`.

```powershell
$toolBin = Join-Path $env:YAUL_INSTALL_ROOT 'bin'
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\verify_sh2_native_math.py `
  $candidateB.Elf tools\saturn\sh2_native_math_baseline_v1.txt `
  --route-oracle tools\saturn\sh2_native_math_route_oracle_v1.txt `
  --audit-route-oracle tools\saturn\sh2_native_math_sim_route_oracle_v1.txt `
  --release-manifest $candidateB.Manifest `
  --measure-audit-report docs\saturn\evidence\reports\sh2-native-math-goal-measurement-v4-2026-08-10.json `
  --objdump (Join-Path $toolBin 'sh-elf-objdump.exe') `
  --readelf (Join-Path $toolBin 'sh-elf-readelf.exe') `
  --addr2line (Join-Path $toolBin 'sh-elf-addr2line.exe')
```

Expected: report schema `sm64-saturn-native-math-measurement-v1`, status `measured-unsealed`, root `_game_loop_one_iteration`, a measured integer total, and neither `_atan2_lookup` nor `_atan2s` among audited callers.

- [ ] **Step 6: Generate the immutable v4 contract and pin its printed digest**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\seal_sh2_native_math_audit_v4.py `
  --measurement docs\saturn\evidence\reports\sh2-native-math-goal-measurement-v4-2026-08-10.json `
  --release-manifest $candidateB.Manifest `
  --output tools\saturn\sh2_native_math_goal_audit_contract_v4.txt
```

Copy the script's printed lowercase digest into `GOAL_AUDIT_CONTRACT_V4_SHA256`, add a checked-in integrity test that reads the v4 file and calls `verify_audit_contract_integrity()`, and do not alter v2/v3 constants.

- [ ] **Step 7: Run exact v4 audit against the same release manifest**

Run the verifier with candidate-B ELF, candidate-B release manifest, existing baseline/oracles, and the new v4 contract:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\verify_sh2_native_math.py `
  $candidateB.Elf tools\saturn\sh2_native_math_baseline_v1.txt `
  --route-oracle tools\saturn\sh2_native_math_route_oracle_v1.txt `
  --audit-route-oracle tools\saturn\sh2_native_math_sim_route_oracle_v1.txt `
  --audit-contract tools\saturn\sh2_native_math_goal_audit_contract_v4.txt `
  --release-manifest $candidateB.Manifest `
  --json-output docs\saturn\evidence\reports\sh2-native-math-goal-audit-v4-2026-08-10.json `
  --objdump (Join-Path $toolBin 'sh-elf-objdump.exe') `
  --readelf (Join-Path $toolBin 'sh-elf-readelf.exe') `
  --addr2line (Join-Path $toolBin 'sh-elf-addr2line.exe')
```

Expected: exit 0; reported actual total equals the generated contract total; exact ELF/release/identity/profile preflight passes; forbidden callers are absent.

- [ ] **Step 8: Measure memory and package facts from the sealed artifact**

Record:

- `___end`, physical HWRAM margin to `0x06100000`, and usable margin after subtracting `0x1B00`;
- exact linked cart-rodata start/end span;
- exact `SOURCE.DAT` size/hash and equality between object, staged CD, ISO listing, and release manifest;
- identity version/id/effective config/profile/package/toolchain roots; and
- exact release-manifest SHA-256.

Do not reuse old expected addresses or 3,565,776-byte payload facts; measure candidate B.

```powershell
$sym = Join-Path $candidateB.Directory 'obj\sm64-saturn-sourceboot-e2.sym'
$endMatch = Select-String -LiteralPath $sym -Pattern '^([0-9a-fA-F]+)\s+B\s+___end$'
$cartStartMatch = Select-String -LiteralPath $sym -Pattern '^([0-9a-fA-F]+)\s+R\s+___sourceboot_cart_rodata_start$'
$cartEndMatch = Select-String -LiteralPath $sym -Pattern '^([0-9a-fA-F]+)\s+R\s+___sourceboot_cart_rodata_end$'
if ($endMatch.Count -ne 1 -or $cartStartMatch.Count -ne 1 -or $cartEndMatch.Count -ne 1) {
    throw 'expected one HWRAM end and one cart span in candidate B symbols'
}
$endAddress = [Convert]::ToUInt32($endMatch.Matches[0].Groups[1].Value, 16)
$cartStart = [Convert]::ToUInt32($cartStartMatch.Matches[0].Groups[1].Value, 16)
$cartEnd = [Convert]::ToUInt32($cartEndMatch.Matches[0].Groups[1].Value, 16)
$physicalMargin = 0x06100000 - $endAddress
$usableMargin = $physicalMargin - 0x1B00
$release = Get-Content -Raw -LiteralPath $candidateB.Manifest | ConvertFrom-Json
$manifestDir = Split-Path $candidateB.Manifest -Parent
$sourceDat = Join-Path $manifestDir $release.outputs.source_dat.path
$iso = Join-Path $manifestDir $release.outputs.iso.path
if ((Get-Item $sourceDat).Length -ne ($cartEnd - $cartStart)) {
    throw 'SOURCE.DAT size differs from linked cart span'
}
[pscustomobject]@{
    end_address = ('0x{0:x8}' -f $endAddress)
    physical_margin_bytes = $physicalMargin
    usable_margin_bytes = $usableMargin
    cart_bytes = $cartEnd - $cartStart
    source_dat_sha256 = (Get-FileHash -Algorithm SHA256 $sourceDat).Hash.ToLowerInvariant()
    iso_sha256 = (Get-FileHash -Algorithm SHA256 $iso).Hash.ToLowerInvariant()
    release_manifest_sha256 = (Get-FileHash -Algorithm SHA256 $candidateB.Manifest).Hash.ToLowerInvariant()
}
```

- [ ] **Step 9: Stage the verified candidate without overwriting prior releases**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\stage_saturn_release.py `
  --manifest $candidateB.Manifest `
  --destination build\saturn\releases\sourceboot-bob-demo-v2-manual-candidate
```

Expected: stage verifier passes and the staged manifest hashes its own copied ELF, `SOURCE.DAT`, ISO, and CUE.

- [ ] **Step 10: Update evidence and status docs, then commit exact contract/evidence**

Mark the old v3 Task 3 blocked path superseded by this new v2/v4 target without changing historical v3 facts. Mark reproducibility, release sealing, package, margin, and v4 gates with measured evidence. Keep 20,100-frame smoke, visual, and manual gates open.

```powershell
git add CHANGELOG.md STATE.md ROADMAP.md docs/saturn/evidence/reports/hermetic-sourceboot-release-2026-08-10.md docs/saturn/evidence/reports/hermetic-sourceboot-reproducibility-2026-08-10.json docs/saturn/evidence/reports/sh2-native-math-goal-measurement-v4-2026-08-10.json docs/saturn/evidence/reports/sh2-native-math-goal-audit-v4-2026-08-10.json docs/superpowers/plans/2026-08-09-memory-residency-campaign.md docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md tools/saturn/sh2_native_math_goal_audit_contract_v4.txt tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py
git commit -m "test(saturn): seal reproducible goal target v4"
```

- [ ] **Step 11: Obtain independent evidence and code-quality reviews**

Reviewers independently recompute the v4 contract digest, release-manifest/artifact hashes, identity-v2 fields, reproducibility comparison, total, forbidden callers, HWRAM/cart margins, and historical v2/v3 immutability. Fix and rereview any finding before Task 9 is complete.

---

### Task 10: Exact-target 20,100-frame smoke, visual proof, and manual-test handoff

**Files:**
- Create: `docs/saturn/evidence/reports/hermetic-sourceboot-combined-smoke-2026-08-10.json`
- Create: `docs/saturn/evidence/reports/hermetic-sourceboot-visual-2026-08-10.json`
- Create: `docs/saturn/evidence/screenshots/hermetic-sourceboot-visual-2026-08-10.png`
- Create: `docs/saturn/evidence/reports/hermetic-sourceboot-desktop-launch-2026-08-10.json`
- Create/modify after owner run: `docs/saturn/evidence/reports/hermetic-sourceboot-manual-play-2026-08-10.md`
- Modify: `docs/saturn/BUILDING.md`
- Modify: `STATE.md`
- Modify: `ROADMAP.md`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md`
- Modify: `.superpowers/sdd/2026-08-10-hermetic-full-game-release-identity/progress.md`
- Modify: `CHANGELOG.md` only if behavior or user-facing command semantics change during fixes.

**Interfaces:**
- Consumes: Task 9 staged release manifest and exact candidate artifacts.
- Produces: artifact-bound combined smoke, artifact-bound screenshot/visual report, a verified desktop launch command, owner manual-play checklist, and final campaign status.

- [ ] **Step 1: Re-verify the staged release before emulator execution**

```powershell
$manualRoot = (Resolve-Path 'build\saturn\releases\sourceboot-bob-demo-v2-manual-candidate').Path
$manualManifest = Join-Path $manualRoot 'saturn-release-manifest-v1.json'
$manualCue = Join-Path $manualRoot 'sm64-saturn-sourceboot-e2.cue'
$manualElf = Join-Path $manualRoot 'obj\sm64-saturn-sourceboot-e2.elf'
$projectRoot = Split-Path (Split-Path (Split-Path (Get-Location).Path -Parent) -Parent) -Parent
$ymirHeadless = Join-Path $projectRoot 'ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe'
$ymirDesktop = Join-Path $projectRoot 'ymir-agent\build-agent2\apps\ymir-sdl3\Release\ymir-sdl3.exe'
$ipl = (Resolve-Path '.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin').Path
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\release_manifest.py verify `
  --manifest $manualManifest
```

Require the same release-manifest digest reviewed in Task 9.

- [ ] **Step 2: Run the exact 20,100-frame combined smoke**

Invoke the combined capture against the staged v2 release:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_object_pool_occupancy.py `
  --ymir $ymirHeadless --ipl $ipl `
  --game $manualCue --elf $manualElf --release-manifest $manualManifest `
  --post-bios-frames 20100 --sample-interval 300 --timeout 1800 `
  --output docs\saturn\evidence\reports\hermetic-sourceboot-combined-smoke-2026-08-10.json
```

Acceptance requires:

- release-manifest/ELF/CUE/identity match;
- object-pool allocation failures remain zero;
- measured peak remains below sealed capacity 208;
- cart reaches ready/complete with copied size equal expected size;
- exception record remains clear;
- VDP presentation generations advance;
- area yaw changes in the prescribed 9,500–10,000-frame route window; and
- all 20,100 post-BIOS frames complete.

If the idle-boot coverage gap from the occupancy campaign remains, state it explicitly; do not convert route coverage into idle coverage.

- [ ] **Step 3: Run visual capture against the same staged release**

Use the existing HUD/visual capture path with mandatory release-manifest binding. Require a non-black gameplay frame, textured BOB terrain, Mario/actor visibility, HUD visibility, advancing generations, no exception, and exact artifact identity. Save the JSON and PNG at the paths above.

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_hud_state.py `
  --ymir $ymirHeadless --ipl $ipl --game $manualCue `
  --release-manifest $manualManifest --startup-frames 3600 `
  --expect-glyph-index 12 `
  --output docs\saturn\evidence\reports\hermetic-sourceboot-visual-2026-08-10.json `
  --screenshot-output docs\saturn\evidence\screenshots\hermetic-sourceboot-visual-2026-08-10.png
```

- [ ] **Step 4: Independently inspect the screenshot and report**

Open the PNG and verify the visible result matches the machine report. Record exact observed defects rather than accepting solely on nonzero pixels. If a visual defect appears, stop, diagnose under systematic debugging, fix through a new reviewed behavior commit, rebuild/reseal v4 if target bytes change, and rerun Tasks 9–10 gates against the replacement manifest.

- [ ] **Step 5: Prepare and verify the desktop manual launch**

Use `launch_ymir_desktop.py` with the staged CUE and a launch report that includes the staged release-manifest SHA-256. The manual checklist must cover boot to BOB, live controller response, movement/jump/camera, terrain/actor/HUD appearance, sustained play, transition/exit behavior available in the slice, audio expectation for semantic-audio-off, and absence of crash/hang/corruption.

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\launch_ymir_desktop.py `
  --ymir $ymirDesktop --profile .ymir-profile --cue $manualCue `
  --release-manifest $manualManifest --launch --monitor-seconds 20 `
  --output docs\saturn\evidence\reports\hermetic-sourceboot-desktop-launch-2026-08-10.json
```

- [ ] **Step 6: Present the exact manual candidate to the owner**

Provide the staged CUE path, release-manifest digest, identity id, v4 result, smoke result, screenshot, known limitations, and checklist. Manual acceptance remains unchecked until the owner actually plays and reports a verdict.

- [ ] **Step 7: Record the owner's manual result and reconcile the full-game boundary**

If accepted, mark the integrated BOB demo playable and release-manifest-bound. Keep `sm64-saturn-full` non-releasable and list its remaining content/system gates; do not describe the demo as the total game. If rejected, record the exact symptom and leave the task active while the issue follows systematic debugging and resealing rules.

- [ ] **Step 8: Final verification, documentation commit, and independent review**

Run focused release/capture tests plus manifest verification again. Update plan, SDD ledger, campaign, `STATE.md`, `ROADMAP.md`, build guide, and manual report with exact evidence and open full-game gates.

```powershell
git add STATE.md ROADMAP.md docs/saturn/BUILDING.md docs/saturn/evidence/reports/hermetic-sourceboot-combined-smoke-2026-08-10.json docs/saturn/evidence/reports/hermetic-sourceboot-visual-2026-08-10.json docs/saturn/evidence/screenshots/hermetic-sourceboot-visual-2026-08-10.png docs/saturn/evidence/reports/hermetic-sourceboot-desktop-launch-2026-08-10.json docs/saturn/evidence/reports/hermetic-sourceboot-manual-play-2026-08-10.md docs/superpowers/plans/2026-08-09-memory-residency-campaign.md docs/superpowers/plans/2026-08-10-hermetic-full-game-release-identity.md
git commit -m "docs(saturn): record integrated demo acceptance"
```

Independent evidence review must confirm every report binds the same release manifest and that full-game deployment readiness is described as an architecture/capability with remaining content gates, not as completed game status.

## Completion criteria

This plan is complete only when Tasks 1–10 and both reviews per task clear; the BOB identity-v2 target reproduces across two clean builds; v4 passes against the exact release manifest; release staging verifies; the 20,100-frame smoke passes; visual evidence is accepted; and the owner's manual result is recorded. The full-game profile remains explicitly incomplete until its full package inventory and game-wide target gates are implemented, but adding that inventory must require no second identity, audit-binding, release-manifest, capture-binding, or deployment-staging architecture.
