# Task 14 Task 3 — actor identity registry and observer seam

## Status

`DONE` / complete as the independently reviewed source prerequisite. Final
scoped rereview verdict: `PASS C0/I0/M0`, covering behavior `8a9ff531`, initial
status `1a5b8b37`, repair `d0a9868b`, and repair status `0e400b15`. The
generator, generated-header wiring, source-owned typed observer seams,
executable admission fixture, complete S64F validation, and MSYS host-runner
repair are implemented. The prescribed exact combined Make gate and Task 11
actor-bank regressions pass.

No Task 16 Task 2, target build, link, Ymir, reseal, smoke, manual, or FPS work
was started.

## Commits

- `8a9ff531bebbfdaf8fb84a82f3c86b5dfa356b3e` —
  `feat(saturn): wire generated actor identity registry into observer seam`
- `1a5b8b37` — `docs(saturn): record actor registry source transition`.
  The `.superpowers/sdd` ledger/report are intentionally ignored operational
  records and remain durable in the named workspace paths.
- `d0a9868b` — `fix(saturn): close actor registry review gaps`.
- `0e400b15` — `docs(saturn): record actor registry review fixes`.

## Recovery and reference-code-first record

Base was `1559e6a0` on `sh2/native-math-purge`. Every Git invocation used the
command-local safe-directory value
`D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`.
The unrelated dirty and untracked work present at start was neither staged nor
modified.

Both complete preserved draft inventories and diffs were inspected against
their base/current context:

- `stash@{0}`: `Makefile.saturn.mk`,
  `src/port/saturn/sourceboot/Makefile`,
  `tools/saturn/actor_identity_registry_test.c`, and
  `tools/saturn/test_actor_snapshot_source.py`.
- `stash@{1}`: `src/game/rendering_graph_node.c`,
  `tools/saturn/gen_actor_identity_registry.py`, and
  `tools/saturn/test_gen_actor_identity_registry.py`.

The two stash objects were not popped, applied, dropped, or rewritten. Final
verification still showed both entries present. Reuse mode was a close-port of
verified same-repository patterns, corrected against current inputs and the
reviewed observer seam; there was no external/upstream source reuse or copied
third-party code.

Current authoritative references inspected before production edits:

- `tools/saturn/compile_actor_bank.py` and
  `tools/saturn/collect_scene_closure.py` (Task 11 writers and schema rules).
- `Makefile.saturn.mk`'s `compile-actor-banks` and family-bank verification
  rules.
- Current `closure.json`, `actor-families.json`, and S64F payload.
- `src/port/saturn/gfx/saturn_actor_instance.h/.c` and
  `saturn_geo_state_observer.c` (pointer-free ABI, four identity admission
  gates, existing authoritative switch recorder).
- Current `src/game/rendering_graph_node.c`, including `obj_is_in_view()`,
  `gLoadedGraphNodes[]` model resolution, and the reviewed Task 19
  `NO_PARENT` seam.
- The generated HUD header/order-only prerequisite patterns in
  `src/port/saturn/sourceboot/Makefile`.

## Initial generated inputs at `8a9ff531` (superseded below)

The authoritative inputs were rebuilt first with:

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-actor-banks
```

Result: PASS. Measurements after that refresh:

| Artifact | Schema/count | Bytes | SHA-256 |
|---|---:|---:|---|
| `build/saturn/packages/bob/1/closure.json` | `sm64-saturn-scene-closure-v1`; 86 records | 270,100 | `03cc66deae092544cce7a6ec246670c1f46ce7456ea45ac4633229cb56a0f28f` |
| `build/saturn/packages/bob/1/actors/actor-families.json` | `sm64-saturn-actor-family-bank-v2`; 47 families (34 supported / 13 unsupported) | 159,303 | `80b5f0a9783497884f7612dfd81b2c0dbdd5c414f8654bc1dc04858b05d33e6c` |
| `families-3e86389b330f3803.s64f` | 47 canonical records | 104,840 | `3e86389b330f3803dcd51bd6f05f8c86b0be6f5e71f012ce5735904742554dea` |
| generated `actor_identity_registry.h` | 54 supported drawable pairs | 19,673 | `77b9bd99be65981c9eca20d2a94d2c129017f0edbf6f8098c20a9d3be85dec47` |

The brief's historical 99,105-byte / `97dc231b...` S64F measurement is stale
and was not embedded. The current closure has 72 supported records; after
removing non-drawable `MODEL_NONE` controller records and deduplicating exact
runtime pairs, 54 supported drawable `(model, behavior)` rows remain.

## TDD evidence

### Generator RED

Test file was created before the generator. Command:

```powershell
cd tools\saturn
..\..\.venv-saturn-tools\Scripts\python.exe -m unittest test_gen_actor_identity_registry -v
```

RED result: exit 1, six failures because
`tools/saturn/gen_actor_identity_registry.py` did not exist. The six cases
already required byte determinism, equal-geo behavior disambiguation,
unsupported absence/no zero row, real SHA split into eight big-endian words,
stale-payload and zero-generation rejection, and the current-input count.

### Observer-seam RED

The observer source assertions were added before its production edit. Command:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_snapshot_source.py
```

RED result: exit 1 at the missing generated-header include and exact
behavior-pair registry lookup; the prior seam left identity fields zero and
hard-coded `render_active`.

### GREEN

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-identity-registry
```

PASS, including refreshed Task 11 products and 6/6 generator tests in 0.889 s.

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_snapshot_source.py
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_render_snapshot_source.py
.\build\saturn\host-tests\actor-instance-snapshot-test.exe
.\build\saturn\host-tests\render-snapshot-bank-test.exe
```

All four commands PASS, exit 0. The two executables are the fresh artifacts
compiled by the prescribed Make targets.

The sourceboot `identity-assets` dry run also exits 0 with the complete Yaul
environment. Its recipe first invokes the top-level `compile-actor-banks`, then
invokes `gen_actor_identity_registry.py` with the current report, closure,
`include/model_ids.h`, generation 1, and the generated output path. All SH
objects carry the generated header as an order-only prerequisite.

### Initial prescribed combined Make gate RED (closed in round 1 below)

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-identity-registry verify-actor-instance-snapshot verify-render-snapshot-bank
```

The registry target passes 6/6 and both host test programs compile. The command
then fails in each pre-existing runner recipe with native Python
`FileNotFoundError: [WinError 2]` because it receives an MSYS executable path:

- `/d/.../build/saturn/host-tests/actor-instance-snapshot-test.exe`
- `/d/.../build/saturn/host-tests/render-snapshot-bank-test.exe`

The direct native-path PASS commands above prove those same fresh binaries,
but are recorded as substitutions, not as a green combined Make gate. The
runner-path repair was deliberately not folded into this actor-registry task.

Scoped `git diff --check` and staged `git diff --cached --check` passed. The
repository-wide diff check continues to report an unrelated pre-existing
blank-line-at-EOF defect in
`.superpowers/sdd/2026-08-03-saturn-overlapped-render-pipeline/task-9-brief.md`.

## Implementation and design corrections

- The preserved draft's model-only key was rejected. Current inputs contain
  14 drawable model/geos used by multiple behaviors, so a model-only match can
  silently assign the wrong family. The generated table is sorted by numeric
  model ID and behavior symbol; runtime binary-searches the numeric model ID,
  then checks exact `BehaviorScript *` equality only inside that equal-ID run.
  Pointer ordering is never used.
- `sharedChild` is resolved through `gLoadedGraphNodes[]`, preserving the
  governing plan's `(sharedChild geo layout, behavior)` semantics without
  publishing source pointers.
- The S64F stable family ID is 32-bit while the reviewed observation ABI's
  `family_id` is 16-bit. Truncating the stable hash would be ambiguous and
  could produce zero. The registry therefore emits the nonzero, 1-based
  canonical S64F record ordinal. This is deterministic and names the exact
  record the later family-bank view must resolve.
- `actor_bank_id` is the first big-endian word of the current payload SHA-256;
  all eight hash words carry the complete SHA. Payload size and actual digest
  are revalidated against the report on every generation, and generation zero
  is rejected.
- Unsupported, unknown, stale, and `MODEL_NONE`/no-geo cases have no row. A
  lookup miss returns `NULL`; all four identity fields remain zero from the
  observation `memset`. There is no Mario or other fallback identity.
- Visibility comes from `GRAPH_RENDER_INVISIBLE`; draw distance mirrors
  `obj_is_in_view()`'s root `GraphNodeCullingRadius` or authoritative 300-unit
  default. Render-range nodes and switch cases are descendant decisions, so
  object-begin retains zero typed range values and explicitly zero switch
  count before the existing authoritative switch recorder runs. Opacity is
  fully opaque (255), because unconditional `oOpacity` access would reinterpret
  per-behavior aliased raw data for geo trees without an opacity callback.
  Generic `feature_state` is never substituted. Held/parent remains explicit
  `NO_PARENT` under the Task 19 design correction.

## Exact scoped files and self-review

Behavior commit `8a9ff531` contains exactly:

- `CHANGELOG.md`
- `Makefile.saturn.mk`
- `src/game/rendering_graph_node.c`
- `src/port/saturn/sourceboot/Makefile`
- `tools/saturn/gen_actor_identity_registry.py`
- `tools/saturn/test_actor_snapshot_source.py`
- `tools/saturn/test_gen_actor_identity_registry.py`

Tracked documentation transition contains exactly:

- `docs/superpowers/plans/2026-08-07-task14-completion.md`
- `docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`

Ignored operational records updated in place (not force-added):

- `.superpowers/sdd/2026-08-07-task14-completion/progress.md`
- `.superpowers/sdd/2026-08-07-task14-completion/task-3-report.md`

Self-review confirmed: no generated build artifact was staged; the top-level
`.PHONY` edit adds only the new target; all sourceboot generated-input and
order-only edges point at current authoritative products; lookup misses cannot
populate partial identity; hash words are copied only inside the hit branch;
the table is `static const`; behavior pointers are used only within the master
source observer and never cross the snapshot ABI; both stashes remain
recoverable; unrelated work remains unstaged.

## Remaining gates

- Independent two-stage review is closed by final scoped verdict
  `PASS C0/I0/M0`; the later section records its exact accepted scope.
- The initial MSYS/native Python executable path transport gap recorded above
  is historical and is closed by review fix round 1 below.
- The governing Task 14 broad checkbox at lines 794-803 remains open: it also
  requires comprehensive Mario/controller/held-parent/billboard/shadow/effect
  fixtures and mutation coverage beyond this registry prerequisite.
- The exact runtime integration fixture described by the older plan wording
  remains represented as two focused proofs (generated registered-family rows
  are nonzero, and snapshot admission accepts nonzero source identity), not a
  target-backed object-to-snapshot proof.
- Task 16 Task 2 production handoff population and generic renderer cutover.
- Any target cross-compile/link, package/reseal, Ymir/headless, smoke, manual
  visual, concurrent-SH2, hardware, or FPS evidence.
- Task 9 reseal and Task 10 smoke remain untouched by explicit instruction.

## Independent review round 1 corrections — 2026-08-11

The controller-owned independent review returned `FAIL C0/I3/M0` plus one
load-bearing cannot-verify gate. The correction reused the same two preserved
drafts and current Task 11/observer references listed above; both stashes were
again verified present and were not popped, applied, dropped, or rewritten.
Reuse remains a same-repository close-port, with the current reviewed seams
and generated formats authoritative over draft assumptions.

### I3 — executable generated identity admission

RED command:

```powershell
.\.venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_gen_actor_identity_registry.TestActorIdentityRegistryGenerator.test_generated_identity_drives_real_observer_admission_and_miss_rejection -v
```

RED: exit 1; the generated fixture failed host C compilation with an implicit
declaration of `saturn_actor_identity_registry_apply`. GREEN: the same command
passed 1/1 in 0.429 s after the generated header gained one fail-closed apply
function. It zeros all four identity domains before lookup and copies family
ID, actor-bank ID, all eight bank-hash words, and scene generation only on an
exact hit. The executable fixture then drives generated lookup through the
real observer and real `sm64_saturn_actor_instances_capture`: a registered
shared-geo behavior admits with all identity and typed fields intact, while an
unsupported/missing behavior retains zero identity and publishes zero records
with `unknown_family_count == 1`.

### I1 — authoritative typed render state

RED command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-instance-snapshot
```

RED: exit 2 during host C compilation because the executable test called the
missing render-range, selected-switch, and opacity observer APIs. GREEN direct
fresh-binary proof was:

```powershell
.\build\saturn\host-tests\actor-instance-snapshot-test.exe
```

PASS, exit 0. The final combined Make gate below also executes that binary and
passes its source guard. The source observer now starts with typed neutral
state, then the actual owner of each decision updates the currently bound
observation: object frustum evaluation writes `render_active`; selected LOD
writes Q16 min/max plus selected state; the post-callback switch selection is
appended in traversal order; and `geo_update_layer_transparency` records the
actual clamped opacity after reading `oOpacity`. Generic `feature_state` never
substitutes. The reviewed held/parent rule stays `NO_PARENT`.

### I2 — complete S64F and generation validation

RED command:

```powershell
.\.venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_gen_actor_identity_registry.TestActorIdentityRegistryGenerator.test_s64f_identity_layout_digest_and_record_spans_fail_closed tools.saturn.test_gen_actor_identity_registry.TestActorIdentityRegistryGenerator.test_payload_records_and_scene_generation_must_match_report -v
```

RED: exit 1, 2/2 failures; arbitrary 64 bytes with a matching outer
size/SHA, an internally corrupted digest, an escaped record span, report
record drift, and a stale requested generation were accepted. GREEN: the same
command passed 2/2 in 1.767 s. `gen_actor_identity_registry.py` now directly
reuses Task 11's `validate_family_bank_payload` and its authoritative struct
definitions, validating S64F magic/version/layout/internal digest/record
spans. It additionally cross-checks the internal digest, every packed scalar
and byte span against the JSON report, and requires the report generation to
equal the nonzero build-owned generation supplied to both the Task 11 family
compiler and registry generator. The production CLIs no longer invent a
generation independently: top-level `SCENE_PACKAGE_GENERATION` is passed into
the S64F report and registry, and sourceboot forwards its single corresponding
build input to both steps. A stale mismatch fails before header emission.

### G1 — prescribed combined Make gate

RED command:

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-identity-registry verify-actor-instance-snapshot verify-render-snapshot-bank
```

Initial RED: exit 2 after compilation; native Windows Python received an MSYS
`/d/.../actor-instance-snapshot-test.exe` path and raised `FileNotFoundError:
[WinError 2]`. The same boundary affected the render-snapshot recipe. The
established adjacent Makefile pattern is direct quoted execution by the MSYS
shell. Only those two affected runner lines were changed. An intermediate run
then correctly reached a stale source guard and failed because it still
expected the superseded manual lookup; that supplemental guard was updated to
the generated apply API and actual render seams.

Final GREEN: the exact command above passed, exit 0 in 59.5 s. It ran 9/9
registry generator tests, compiled and executed the actor snapshot C suite,
passed its source guard, compiled and executed the render snapshot C suite,
and passed its source guard. No direct-binary substitution is used for this
acceptance result.

### Current authoritative generated inputs after correction

The exact combined gate rebuilt the inputs. Current measurements are:

| Artifact | Schema/count | Bytes | SHA-256 |
|---|---:|---:|---|
| `closure.json` | `sm64-saturn-scene-closure-v1`; 86 records | 270,100 | `d162665a330d80cf1cf5e9d4d87b4da551906a078b77548b3a6a8b0986a03756` |
| `actor-families.json` | `sm64-saturn-actor-family-bank-v2`; 47 families (34 supported / 13 unsupported); generation 1 | 159,337 | `dbe721eec7a9aa617d981451691ece840c6048ba62cb9e0215420d63cd050ad8` |
| `families-00e5754c80762a15.s64f` | 47 canonical records | 104,840 | `00e5754c80762a15b5482fb1f2e88f4bc1fc7ab847f3463944e2e6689d412ee8` |
| S64F internal content digest | header-bound | 32 | `830fe0de909c9e6e49d4b03d32aa0f0c6ce64329bd59bb7b23864da8a2350d51` |
| generated `actor_identity_registry.h` | 54 supported drawable pairs | 20,663 | `dbfd686f1eece20422890c3ef909717a8f1677123e18064f6326274db3ff239d` |

These supersede the earlier measurements in this report because the current
authoritative source closure changed under preserved unrelated branch work;
the generator consumed the refreshed inputs instead of embedding historical
hashes.

### Additional regression and scoped file record

```powershell
.\.venv-saturn-tools\Scripts\python.exe -m unittest tools.saturn.test_gen_actor_identity_registry -v
# PASS: 9/9 in 5.276 s

cd tools\saturn
..\..\.venv-saturn-tools\Scripts\python.exe -m unittest test_actor_bank -v
# PASS: 8/8 in 4.116 s
..\..\.venv-saturn-tools\Scripts\python.exe -m unittest test_generic_actor_bank -v
# PASS: 4/4 in 52.835 s
```

Review-fix production/test scope is exactly:

- `CHANGELOG.md`
- `Makefile.saturn.mk`
- `src/game/object_helpers.c`
- `src/game/rendering_graph_node.c`
- `src/port/saturn/gfx/saturn_actor_instance.h`
- `src/port/saturn/gfx/saturn_geo_state_observer.c`
- `src/port/saturn/sourceboot/Makefile`
- `tools/saturn/actor_instance_snapshot_test.c`
- `tools/saturn/compile_actor_bank.py`
- `tools/saturn/gen_actor_identity_registry.py`
- `tools/saturn/test_actor_snapshot_source.py`
- `tools/saturn/test_gen_actor_identity_registry.py`

Behavior correction commit: `d0a9868b`. Tracked plan/status commit:
`0e400b15`.

Tracked status documents updated during the same transition are
`docs/superpowers/plans/2026-08-07-task14-completion.md` and
`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`.
This ignored report and its sibling progress ledger were updated in place.
Scoped diff checks pass; unrelated tracked/untracked work remains unstaged.

Remaining gates at the round-1 checkpoint included scoped rereview, which is
now closed below. Still open are the governing broad Task 14 fixture/mutation
checkbox (Mario/controller, held/parent, billboard, shadow, effects, complete
budget coverage); Task 16 Task 2; and every target/Ymir, package/reseal, smoke,
manual, concurrent-SH2, hardware, and FPS gate. The exact combined Make gate
is no longer open.

## Final scoped rereview — 2026-08-11

Independent verdict: `PASS C0/I0/M0`. Task 3 is complete as the reviewed
source prerequisite. The accepted source transition is:

- behavior `8a9ff531bebbfdaf8fb84a82f3c86b5dfa356b3e`;
- initial tracked status `1a5b8b3798f1b13c1c6c2265d81bda87e4904064`;
- review repair `d0a9868b090827b485e2fc1e49f10c455535c3e2`;
- repair tracked status `0e400b15cb6c4956f2f5770050e2c0a4df803468`.

Accepted executable evidence remains the exact prescribed combined Make
command: PASS, registry 9/9 plus actor and render executable/source suites.
Task 11 regression evidence remains `test_actor_bank` 8/8 PASS and
`test_generic_actor_bank` 4/4 PASS. Both preserved references remain
recoverable and untouched: `stash@{0}` (Makefiles/tests draft) and
`stash@{1}` (observer/generator draft).

The review closes only the source prerequisite. Target cart placement and
HWRAM evidence for the integrated result remain open, as do the governing
broad Task 14 fixtures, all Task 16 production/cutover gates, Ymir,
package/reseal, smoke, manual, concurrent-SH2, hardware, and FPS evidence. No
downstream gate is inferred green from this source review.
