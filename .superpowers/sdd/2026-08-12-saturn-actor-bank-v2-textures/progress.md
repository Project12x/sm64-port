# SDD ledger — plan: docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md

## Execution setup — 2026-08-12

- Design: `62f16de8`; design status: `f1c5679a`.
- Owner written-spec approval: 2026-08-12.
- Implementation plan: `3338de20`; execution status: `3f50bb11`.
- Workflow: subagent-driven development; fresh implementer and two-stage task
  review per task, serial execution, five-round breaker.
- Preflight: existing isolated linked worktree verified; scoped plan/design/
  status paths clean; index empty; both plan commits and range whitespace
  checks pass.
- Plan scan: no unresolved conflict. Self-review corrections are recorded in
  the plan: real BOB S64F may be v2-only while the scene mixes historical v1
  Mario and v2 generic actors; S64P alignment stays 4; texture/CLUT upload
  regions are separate; global lane stride and active texture generation are
  explicit.
- Current: Tasks 1 and 2 complete and independently approved; Task 3 is
  source-complete pending independent parser/ABI review. Tasks 4-13 and all
  material, real-BOB, runtime, demo, release, reseal, smoke, visual, desktop,
  manual, retail, and total-game gates remain open.

## Task 1 review loop

- Base `3f50bb11`; implementation `68ceec9c`; evidence `89fa92da`.
- Implementer GREEN: 37 focused unittests, four actor host gates, exact Mario
  JSON/S64B hashes; status `source-complete-pending-review`.
- Review: spec ❌ / quality Needs fixes; C0/I1/M0. Important: the new
  version-owned `_S64B_HEADER` described only 102 of the binding 104 bytes and
  did not own/reject mutations in the final two reserved/padding bytes.
- Task 1: fix round 1/5 complete (1 addressed, 0 open; behavior `9d5fc03c`,
  evidence `1e517bac`). The complete 104-byte header owns a final zero-reserved
  `H`; independent mutations at offsets 102 and 103 now reject.
- Scoped rereview of `89fa92da..1e517bac`: original finding ADDRESSED; no new
  regression or out-of-scope issue; 38 affected tests pass; C0/I0/M0.
- Task 1: complete (commits `3f50bb11..1e517bac`, review clean). Historical
  Mario S64B/JSON/source hashes and the S64F-v3 fixture remain exact. Task 2 is
  ready for its missing-module/parser RED. No target evidence is claimed.

## Task 2 review loop

- Base `c6b6885f`; behavior `83cfc1ad`; evidence `671ad31d`.
- Implementer GREEN: 14 focused v2/v1 tests, 60 broader actor/bundle tests,
  all four required actor Make gates, exact historical Mario and S64F bytes;
  status `source-complete-pending-review`.
- Independent review: Spec FAIL / Quality Needs fixes; C0/I2/M0. Verified
  findings: v2 parser/packer accept a structurally valid zero-meshlet/
  zero-primitive core, and output aggregate bounds are enforced only after
  growing bytearrays, permitting uncontrolled large allocation/MemoryError
  before the named checked-overflow boundary.
- Task 2: fix round 1/5 active. The original implementer must capture focused
  REDs, reject zero required v2 counts without changing v1, preflight every
  aggregate/alignment/final span before allocation using reduced-limit tests,
  rerun all host/historical gates, update CHANGELOG/plan/report, and commit
  explicit Task 2 paths. Task 3 remains closed.
- Task 2 fix round 1/5 is source-complete at behavior `95de6457`, evidence
  `d5914059`, pending scoped rereview. Both
  Important findings have focused RED/GREEN coverage: a structurally valid
  zero-draw v1 fixture remains accepted as v1 but is rejected on v2 promotion/
  parsing, and injected total/texture/CLUT limits prove named rejection before
  any bytearray allocation. Sixteen focused and 62 broader tests pass; all four
  Make gates and historical/canonical hashes remain exact.
- Scoped rereview of `671ad31d..d5914059`: both Important findings
  ADDRESSED; no new regression or out-of-scope change; Spec PASS / Quality
  PASS, C0/I0/M0. Focused 16 tests and direct zero-count probes pass.
- Task 2: complete (commits `c6b6885f..d5914059`, review clean). Task 3 may
  open for target S64B-v2 and mixed-S64F validation. Every target execution,
  material/compiler, real BOB, Ymir, release, smoke, visual, desktop, manual,
  retail, and total-game gate remains open.

## Task 2 implementation

- Starting HEAD reconciled at `c6b6885f`; unrelated dirt preserved.
- RED: missing `actor_bank_v2` module plus the still-unimplemented v2 dispatch
  branch under the established `.venv-saturn-tools` runtime. The inaccessible
  WindowsApps launcher attempt stopped before discovery and is discarded.
- GREEN: 14 focused v2/v1 tests and 60 broader actor/bundle tests pass. Make
  gates pass: family bundle 53 mutations; variant/source 37 tests; pose and
  meshlet fixtures including invalid-span mutation.
- Historical Mario remains exact at 596,896 bytes / S64B SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`,
  JSON SHA-256 `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`,
  source identity
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- Deterministic v2 fixtures: untextured 504 bytes / SHA-256
  `e25fecda219459956bb39d11bd2d39f3775b7a310c5fbf3cbc791d285b406156`;
  CLUT16 textured 584 bytes / SHA-256
  `c58c8eaeebe8930a418812838f6090e4bc23822486e1d4f3dc66eca2a1ea876a`.
- Task 2: source-complete-pending-review at behavior `83cfc1ad`, evidence
  `671ad31d`. No target evidence is claimed; Task 3
  and every runtime/demo/release/manual gate remain blocked on independent
  Task 2 approval.

## Task 3 implementation

- Starting HEAD reconciled at `efd70710`; unrelated dirt was preserved and no
  reset, clean, stash, or broad staging was used.
- RED: the new C fixture failed on the missing v2 constants, view fields,
  stable record types, and three accessors. Mixed S64F remained unable to
  resolve its v2 variant. The first sandboxed Make attempt only hit the known
  MSYS `\\d\\...` path rewrite; the approved explicit-root rerun is the
  recorded feature RED.
- GREEN: one target version dispatch validates the shared S64B prefix/GEO1 and
  exact v2 tail. V1 exposes zero v2-only fields. V2 exposes bounded binding,
  material, tile, hot/cold payload, resident, command, Gouraud, draw, and bake
  fields only after canonical validation. S64F validate/resolve delegates the
  exact opaque bank span to `sm64_saturn_actor_bank_validate_expected`.
- TDD regression: `verify-actor-pose-bank` caught an attempted change to the
  historical no-write-on-failure bank-view contract; that behavior was
  restored and the gate passed. A focused `0x80000000` animation value-word
  mutation then proved unchecked byte-count multiplication wrapped to zero;
  checked multiplication produced the required RED-to-GREEN repair. Final
  pointer review also moved the aligned tile-padding bound ahead of its byte
  scan so malformed multi-tile spans cannot be inspected before rejection.
- Verification: S64B-v2 C gate PASS (86 mutations), S64F-v3 gate PASS (54
  mutations, including a resealed malformed v2 extension), actor pose PASS,
  meshlets PASS plus invalid-span mutation, feature-off wrapper 6 tests PASS,
  63 broader Python actor/bundle tests PASS, and
  `sh-elf-gcc -ffreestanding -fsyntax-only` PASS for `saturn_actor_bank.c`.
- Historical identity remains exact: Mario S64B 596,896 bytes / SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`,
  source identity
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`,
  and historical S64F-v3 1,688 bytes / SHA-256
  `4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.
- Status: source-complete-pending-review at behavior commit `87be53b6`. Task 4
  and every material, real-BOB, runtime, Ymir, release, smoke, visual,
  desktop, manual, retail, and total-game gate remain closed.
- Independent Task 3 review of `efd70710..ca3318cd`: Spec PASS / Quality PASS,
  C0/I0/M0, no out-of-scope change. Fresh 86-mutation S64B, 54-mutation mixed
  S64F, pose, meshlet, feature-off, 51-test Python, freestanding SH-2 compile,
  and diff checks pass.
- Task 3: complete (commits `efd70710..ca3318cd`, review clean). Task 4 may
  open for exact measured BOB material capture/lowering. Every actual target
  execution, real BOB bundle, residency/runtime, Ymir, release, smoke, visual,
  desktop, manual, retail, and total-game gate remains open.

## Task 4 review loop

- Base `05b77e64`; behavior `52c9c1af`; evidence `20e5484b`.
- Implementer GREEN: exact 14 direct-textured BOB keys, all 47-family outcomes,
  real Cannon 29/0x0080/bhvCannon with 30 draws and eight unpaired textured
  inputs, 46 focused tests, historical pose/meshlet/mixed-S64F/variant gates,
  exact Mario bytes; host-only source-complete-pending-review.
- Independent review: Spec FAIL / Quality Needs fixes; C0/I2/M1. Verified
  findings: recognized Fast3D state after the final triangle is not included
  in exact capture admission; decoded PNG bytes are read outside the closure's
  attested source set and only synthesized into identity afterward; scalar
  shifts execute before a bounded shift-count/operand check.
- Material design correction: closure collection must uniquely resolve every
  reached texture declaration, derive the checked-in PNG it names, add that
  PNG to the owning record/source hashes before downstream compilation, and
  make the material compiler require that exact attested digest. The complete
  recognized relevant command/final-state trace becomes an exact whitelist
  input, not merely material-at-triangle snapshots.
- Task 4: fix round 1/5 active. The original implementer must capture each RED,
  apply the narrow upstream closure/test correction plus Task 4 material fixes,
  rerun real BOB closure and all host/historical gates, update CHANGELOG/plan/
  ledgers/report, and commit explicit paths. Task 5 remains closed.

## Task 4 implementer evidence (2026-08-12)

- Baseline reconciled at `05b77e6472a09facd3d4faf01100ad09b2d9882e`;
  unrelated dirty work was inventoried and preserved. RED ran the prescribed
  three suites: 37 historical tests passed and the new suite failed solely on
  missing `actor_material_v2` before production edits.
- Source-complete-pending-review at behavior commit `52c9c1af`: the one
  existing Fast3D compiler now captures
  complete image, load-tile/load-block, render-tile, mask/shift, wrap/clamp,
  combiner plus env/alpha, geometry, layer, opacity, call/tail, UV, and exact
  PNG path/hash state. Canonical category hashes admit only the 14 measured
  direct-textured BOB keys; partial, computed, ambiguous, unknown, and all
  later states reject. The 34 drawable and all 47-family outcomes are frozen,
  including 18 `GEO_SHADOW`, one `GEO_SCALE`, one `GEO_ASM`, 13 capability
  families, and two `MODEL_NONE` entries.
- Demo-key truth: real family 29 / `MODEL_CANNON_BASE` `0x0080` / `bhvCannon`
  deterministically packs as S64B v2 with 30 draws, eight unpaired textured
  tile inputs, 1,024 resident texture bytes, 256 CLUT bytes, eight texture
  commands, and 30 Gouraud tables per instance. This is host packing evidence,
  not target runtime/residency/renderer/Ymir evidence.
- Provenance: same-project direct adaptation/close-port at pinned project HEAD
  `05b77e64`: `actor_variant_bank.py` (`68ceec9c`),
  `dl_rigid_groups.py` (`a78db8c`), `vdp1_texture.py` (`2e5b41e6`),
  `bake_castle_uv.py` (`b295928e`), `bake_bob_tiles.py` (`4c60f4fe`), and
  Task 2 `actor_bank_v2.py` (`95de6457`). The inherited repository has no
  blanket root license; no external source was copied and no new obligation
  was introduced.
- Verification: focused real-key/mutation/historical wave PASS (42 tests),
  pose PASS, meshlets PASS plus invalid-span mutation, S64F mixed bundle PASS
  (54 mutations), v1 variant/source Make gate PASS (38 tests), strict
  `compileall` PASS, historical Mario remains 596,896 bytes / SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
  Independent review and its verdict remain open; Task 5 and every target,
  real-bundle, residency, renderer, Ymir, release, and total-game gate remain
  unchecked.

## Task 4 fix round 1 implementer evidence (2026-08-12)

- Status: source-complete-pending-rereview at repair commit `86de51cc`; Task 5
  remains closed. Scope is still host-only Task 4:
  no target runtime, residency, renderer, bundle orchestration, Makefile,
  Ymir, or later-task production file changed.
- RED/GREEN finding 1: a real Cannon mutation inserted
  `gsDPSetEnvColor(1, 2, 3, 4)` after the final geometry use and initially
  compiled. Exact admission now hashes an evaluated command-local trace plus
  final state across the full reached call/tail graph, including ambient and
  diffuse light commands. A reduced equal-final-state geometry regression
  proves distinct canonical commands do not collapse.
- RED/GREEN finding 2: focused synthetic and real-Cannon tests initially
  proved PNG paths/hashes absent from the input closure. The authorized file
  list now includes `collect_scene_closure.py`, `test_scene_closure.py`, and
  `test_bob_scene_closure.py`: reached `gsDPSetTextureImage` and
  `gsDPLoadTextureBlock` symbols must resolve one declaration and canonical
  checked-in `.rgba16`/`.ia16` PNG before closure validation/publication.
  Missing, ambiguous, computed, unsupported, noncanonical, and missing-file
  cases reject. `_SourceIndex` requires the exact expected digest and decodes
  the already-hashed bytes; no post-hoc `SourceRecord` is accepted.
- RED/GREEN finding 3: million-bit shift counts and over-uint32 operands
  initially reached Python shift evaluation. Both domains are now checked
  before execution and fail by named `shift count` / `shift operand` errors.
- Fresh verification: focused 34-key/47-family/mutation/variant/source wave 45
  tests PASS; full synthetic/real BOB closure 38 tests PASS; focused final
  closure 3 PASS; Task 2 parser/packer 16 PASS and freestanding validator 86
  mutations PASS; mixed S64F 54 mutations PASS; pose PASS; meshlet plus
  invalid-span PASS; variant/source Make wave 40 PASS. Historical Mario is
  still 596,896 bytes with SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`,
  JSON SHA-256
  `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`,
  and source identity
  `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
- The approved Cannon output remains exact: 2,952 bytes, source identity
  `bb972afe2022977f4b7290d11e28e081f869c7cc7b62b378e28bac17bf76dc4f`,
  payload SHA-256
  `2c8eee36768f0a42063949298eca8351bacb74838165a8188520a4b180d63c6d`.
- Reference reuse remains the pinned same-project record at reconciled HEAD
  `05b77e64`: `actor_variant_bank.py` (`68ceec9c`), `dl_rigid_groups.py`
  (`a78db8c`), `vdp1_texture.py` (`2e5b41e6`), `bake_castle_uv.py`
  (`b295928e`), `bake_bob_tiles.py` (`4c60f4fe`), and Task 2
  `actor_bank_v2.py` (`95de6457`), by direct extension/close-port/dependency.
  No external source or target-side budget claim was added.
- Scoped rereview of `20e5484b..f5a03808`: all three findings ADDRESSED;
  83 focused/closure tests, real 34-key/47-family Cannon replay, compileall,
  and diff check pass. No target/runtime/Task 5 scope creep; Spec PASS /
  Quality PASS, C0/I0/M0.
- Task 4: complete (commits `05b77e64..f5a03808`, review clean). Task 5 may
  open for real bounded BOB S64F orchestration and aggregate budgets. Every
  actual target/runtime/residency/renderer/Ymir/release/manual gate remains
  open.
