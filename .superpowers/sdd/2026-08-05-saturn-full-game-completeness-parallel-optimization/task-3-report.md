# Task 3 — transitive scene closure report

Status: source-complete (host tooling).  Implementation base was `ce9f7ef3`;
the worktree head was `beaa9e77` before this task, preserving its controller
plan-metadata correction.  No plan or ledger file was staged or edited here.

## Design and provenance

`collect_scene_closure.py` is a clean-room Python parser/collector for the
repository's LevelScript and behavior-source formats.  It reads BOB's
`levels/bob/script.c`, macro list, macro preset table, `model_ids.h`,
`behavior_data.c`, model geo files, and native behavior sources.  It follows
static `SPAWN_CHILD`/`SPAWN_OBJ` edges, preserves model-less controllers,
cuts cycles per traversal path, and uses `behavior_spawn_rules.json` only for
computed native edges.  Every manual rule identifies its exact in-tree source
and why syntax alone cannot resolve it.  Unknown behavior symbols and missing
model/geo bindings fail closed.

No external source was copied or adapted, so no external upstream repository,
commit, or license attribution applies.  The in-tree source provenance is
captured in each closure's path/SHA-256 set; the BOB generation used the source
tree at this task's recorded base plus preserved metadata head.

The schema is `sm64-saturn-scene-closure-v1`.  Its closed fields reject
hand-authored BOB-only additions, stale hashes, duplicate stable IDs, and
incomplete hash coverage.  Records separately classify normal children,
rewards, projectiles, and effects while retaining all direct children for
dependency traversal.  The former Goomba-only assertion is now a generated
closure fact: 2 direct macros + 3 triplet spawners × 3 = 11.

## Test and build evidence

RED, before production modules existed:

```text
.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
ModuleNotFoundError: No module named 'collect_scene_closure'

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
ModuleNotFoundError: No module named 'collect_scene_closure'
```

GREEN (serial):

```text
.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
Ran 3 tests ... OK

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
Ran 1 test ... OK

powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure SCENE_LEVEL=bob SCENE_AREA=1
PASS: generated build/saturn/packages/bob/1/closure.json
```

The direct determinism check generated BOB twice: byte-identical `True`,
SHA-256 `ab22061527cdfbe175fa2b089aff1377c750b696bd2c6a3fb036797c188999a2`,
with 53 records and 59 complete source hashes.  The compatibility check
`tools/saturn/test_actor_generalization_inventory.py` also passes and reads
the generated closure rather than asserting source text.

## Review and gates

Self-review verdict: PASS.  Reviewed every manual rule against its cited
native behavior source; model-less roots (spawners, warps, hidden-star
controllers, and King Bob-omb's anchor) are retained.  `git diff --check`
found no Task 3 whitespace errors; an unrelated pre-existing Task 9 brief
newline is reported separately and was not touched.

No linked-target, Ymir, FPS, broad native-math, package-link/seal, or later
actor/animation/audio gates were run or claimed.  Those remain open for their
own tasks.  A separate independent review is not yet recorded; this task's
host-only source-complete state must not be treated as target evidence.

## Commit

`c75e9951 feat(saturn): derive transitive scene dependency closure`

## Review-fix round 1

The independent review rejected the initial 53-record / 59-source-hash result;
it is not promoted.  RED was observed for a missing mandatory schema record
field and for the real BOB omissions.  The new reachable-native detector first
reported unruled direct sites for checkerboard subobjects, grill halves, cannon
opening, pole 1-Up controllers, explosion descendants, sparkle descendants,
and water-bomb shadows; each is now a source-attested rule and the detector
returns an empty list before any closure can be returned.

Additional RED tests require explicit cycle rejection and the actual BOB
controller/effect records.  GREEN evidence:

```text
.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
Ran 4 tests ... OK

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
Ran 1 test ... OK

powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure SCENE_LEVEL=bob SCENE_AREA=1
PASS
```

The repaired BOB result contains 64 records and 66 hash-covered sources.
Live capacities are computed per compatible act rather than summing exclusive
act variants; reachable cycles now fail explicitly.  SFX bank names derive
from the `SOUND_*` source identifier family.  Self-review: PASS for this
host-only repair; independent rereview and all target/Ymir/FPS/package gates
remain open.  Serialization now emits LF bytes with a canonical `source_root`
and returns the SHA-256 of exactly the bytes written; the BOB test compares
that returned hash to the output file.

Review-fix commit: `796fc3bd fix(saturn): fail closed on native scene spawns`.

## Rereview repair round 2

RED added concrete BOB expectations for the Koopa-shell helper effects and
exclamation-box computed contents; the first run failed on the absent shell
effects.  Reviewed source-attested rules now cover wave trail, droplet,
flame, sparkle, rotating marker, wing cap, spawned star, and the droplet's
water-splash child.  The closure expands to 71 records / 71 source hashes.
`test_scene_closure.py` (4) and `test_bob_scene_closure.py` (1) pass after
the repair; target generation is rerun before commit.  Target/Ymir/FPS and
package gates remain open.

## Scanner slice

The scanner now performs a bounded (64-body) source-defined helper walk from
each `CALL_NATIVE` callback. It found concrete coin/star helper edges that the
direct-only scanner had missed. Unknown computed arguments are a permanent
RED regression and now fail before output; the BOB grill model table is a
recognized explicit table form. GREEN: scene suite (5), BOB suite (1), and
serial closure target generation pass. Rule-file and audio slices remain open.

## Closure hardening round 3

The remaining rereview slices are implemented.  RED fixtures first proved that
an external rule file could affect output without a hash, unrelated native
source could claim a rule owner, nonexistent edge text and an unsupported
capacity could pass, and file-wide audio scanning leaked an unrelated helper's
sound while accepting missing/ambiguous bank declarations.  GREEN now requires
the v2 rule inventory to bind each unique behavior/edge to repository-relative
owner source, exact expression/location, and a source-derived capacity.  The
bounded source-symbol route includes local helpers, referenced data/action
tables, function-pointer action arrays, and audited generic game dispatchers.

That validation corrected real stale claims rather than merely wrapping them:
the water-bomb shadow belongs only to the spawner; explosion bubbles are
bounded by the source loop at 40 and shell flames by its loop at 2; default-star
callers produce `bhvStarSpawnCoordinates`; moving/loot coin helpers use their
actual coin behaviors; the wooden post owns its five-coin reward; and the chain
pivot's zero-valued model symbol is separately source-resolved and hash-covered.
The merged hidden-red-star rule now contains both its direct star and
coordinate-star paths.  The final detector reports no unruled reachable native
spawn sites and BOB contains 76 records / 78 complete source hashes.

Audio collection now starts at each record's BehaviorScript `CALL_NATIVE`
entrypoints, follows only reachable helper/data regions, and maps every reached
`SOUND_*` use to exactly one `SOUND_ARG_LOAD(SOUND_BANK_*)` declaration.  Missing
or ambiguous mappings fail before output, `include/sounds.h` is included only
for records that use sound and is covered by the document hashes, and unrelated
functions in the same C file no longer leak IDs or banks.

Fresh serial GREEN evidence:

```text
.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
Ran 9 tests ... OK

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
Ran 1 test ... OK

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_generalization_inventory.py
Ran 1 test ... OK

powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure SCENE_LEVEL=bob SCENE_AREA=1
exit 0; generated build/saturn/packages/bob/1/closure.json
```

Self-review: PASS for the source-closure contract.  The controller-owned plan
and execution ledger remain preserved and unstaged.  Target/Ymir/FPS,
package-link/seal, broad native-math, and manual evidence gates were not run and
remain open; this remains host-tooling source completion, not target evidence.

Implementation commit: `15b502650c536c87abc0e4cb3858db0e4a4611a0`
(`fix(saturn): harden scene closure provenance`).  Independent rereview of this
round remains an open controller gate.

## Rereview repair round 3

The round-2 rereview's 76-record / 78-hash output is superseded.  RED first
reproduced all remaining contracts: a helper defined outside
`src/game/behaviors` was invisible, an unknown cross-file model/behavior pair
was accepted, nested area music leaked, animation definitions were optional,
recurrent callbacks had no maximum-live proof, schema validation accepted bad
child/type/scope/audio/provenance relationships, and real BOB omitted
`bhvRespawner` and the mist-created `bhvWhitePuffExplosion`.

The repair builds a bounded repository-wide native symbol graph rooted at each
reachable `CALL_NATIVE`.  It includes helper/data regions across `src/game`,
resolves concrete spawn APIs plus source-owned respawner, particle,
sound-spawner, triangle, and loot-coin wrappers, hashes every reached file, and
rejects ambiguous symbols or unresolved dynamic creation.  Context-sensitive
attack dispatch is constrained by the caller source's handler-table tokens, so
Goomba cannot inherit Koopa's shell-loss branch.  The same graph extends exact
behavior-local audio collection instead of reverting to file-wide token scans.

LevelScript evaluation now recursively expands local and `levels/scripts.c`
`JUMP_LINK` roots.  Only the requested area's expanded text contributes
objects and music; the entry prelude contributes global model loads.  Both
`LOAD_MODEL_FROM_GEO` and `LOAD_MODEL_FROM_DL` override stale model-comment
hints.  Every non-model-less geo/display-list root and every animation table
must resolve to exactly one definition and be hash-covered.  Each record emits
`root_provenance`, and schema validation enforces child existence, typed-list
agreement, record level/area, exact audio unions, sorted typed fields, and
behavior/model/geo/animation provenance membership.

Native rules still attest the burst syntax, but recurrent callbacks no longer
multiply that burst as though it were a lifetime total.  BOB's Goomba triplet
retains the source-proven three-member active-set bound.  One-shot callbacks
retain their attested burst.  Recurrent native or BehaviorScript creation with
no tighter cadence/lifetime proof uses the repository's literal
`OBJECT_POOL_CAPACITY` (240) as an explicit maximum-live ceiling and hashes
`src/game/object_list_processor.h`; generation fails if that cap is absent or
non-literal.  Respawner edges are represented as replacement cycles rather
than recursively multiplied, yielding 14 live respawners for BOB's 12 Bob-ombs
and two small breakable boxes.  No tighter lifetime rule was invented.

The real BOB closure is now 86 records / 127 complete source hashes, canonical
file SHA-256 `84fadd65f66a4c5a1aaa5057441eb4c93adf2000026c208c323529819a92d4d5`.
It contains `bhvRespawner` with contextual Bob-omb/breakable-box return edges,
`bhvWhitePuffExplosion` at the proven pool ceiling, generic water-death,
triangle, sound-spawner, orange-number, and coin effects, corrected
`chain_chomp_geo`, and hash-covered Chain Chomp animation provenance.  Area 1
music is exactly `SEQ_LEVEL_GRASS`.

Fresh serial host evidence before commit:

```text
.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
Ran 13 tests ... OK

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
Ran 1 test ... OK (46.806s)

.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_generalization_inventory.py
Ran 1 test ... OK (45.545s)

powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure SCENE_LEVEL=bob SCENE_AREA=1
exit 0; generated 86-record / 127-hash closure.json
```

Self-review verdict: PASS for this bounded host-source contract.  Independent
rereview is pending the controller; target/Ymir/FPS, package-link/seal, broad
native-math, and manual gates remain explicitly open and were not run.  The
controller-owned active plan and progress ledger were preserved and unstaged.
Implementation is recorded by this repair commit (`fix(saturn): close native
scene graph gaps`).
