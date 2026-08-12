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
- Current: Tasks 1-6 are complete and independently approved for their stated
  scopes. Task 6 has host plus freestanding target-module approval only. Task 7
  remains closed until the Task 6 status commit; all residency, runtime, demo,
  release, reseal, smoke, visual, desktop, manual, retail, and total-game gates
  remain open.

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

## Task 5 preflight and resource-admission design correction (2026-08-12)

- Task 5 stopped before production/test edits under the plan's explicit
  nonpositive-margin rule. Applying the original equation to the real 14-bank
  set produced 5,288 live contributions, 85,512 output records, 65,788 texture
  commands, and 29,352 Gouraud tables against fixed caps 64 / 2,718 / 2,048 /
  1,536.
- Root cause: `collect_scene_closure.py` assigns recurrent behaviors the same
  conservative 240-object source-pool ceiling, and family grouping sums those
  behavior ceilings for shared geometry. The fields are valid independent
  safety bounds but not a joint scene allocation; summing them counts the same
  global pool repeatedly. They remain byte/report exact and are not reduced.
- A global 64-only envelope is also insufficient: the largest supported bank
  costs 46 output records and 46 Gouraud tables per instance, so 64 identical
  instances would require 2,944 of each. Before terrain/Mario/profile
  reservations, the hard-cap diagnostic guarantees only 33 instances for the
  worst per-instance Gouraud cost; Task 5 must recompute the final floor from
  exact post-reservation actor shares rather than pinning 33 as an acceptance
  constant.
- Normative correction: static planning proves all-resident texture/CLUT/cart/
  workspace fit and every bank's individual admissibility. Live rendering uses
  checked per-variant output/command/Gouraud credits for the complete observed
  set (at most 64). Task 9 publishes the whole set only when every sum fits;
  otherwise it quarantines the whole actor generation with no partial
  descriptor/output/VDP1 mutation. No subset selection, count rewrite, or
  hidden working set is permitted.
- Task 5 status: active after docs-only adjudication commit `acef808d`.
  Required REDs now
  distinguish the named unconstrained source-ceiling diagnostic from
  all-resident/individual-bank acceptance, inject each one-credit overflow,
  and prove a positive computed service floor. Task 11 remains responsible for
  measured positive margins on the normally spawned BOB Cannon route; all
  target/runtime/Ymir/release/manual gates remain open.
- Reservation audit found no fixed generic-actor subpartition, and correctly
  stopped after the missing-module RED rather than assigning world capacity by
  guess. The authoritative policy is dynamic: maximum Mario is already an
  essential 694-command / 644-Gouraud obligation, terrain is explicitly
  optional, and generic actors join the essential set. This provides 1,351
  command and 892 Gouraud conservative planning credits plus the dedicated
  2,718-record actor arena; current measured maxima guarantee 19 actors of any
  supported mixture. Task 9 must use actual Mario and generic costs each frame,
  leaving only the remainder to terrain.
- The current Yaul VDP1 layout has physically distinct texture/CLUT partitions
  followed by a 52,672-byte `remaining` region. Generic actors require separate
  16,640-byte texture and 2,816-byte CLUT additions; Task 7 must repartition the
  region, with 33,216 bytes left. Task 5 proves the future partition equation
  but does not claim current binder ownership of `remaining`.
- The source-ceiling design correction is `acef808d`; the authoritative
  frame-policy/partition correction is `833c9bfb`. The prescribed Task 5 RED
  remains exactly the two missing compiler/inventory modules; implementation may
  resume from these committed contracts without changing its host-only scope.

## Task 5 source-complete implementation evidence (2026-08-12)

- Status: source-complete at behavior commit `65a3fdb9`, pending independent
  review. The
  prescribed RED failed only on the two missing compiler/inventory modules;
  the focused GREEN is 8/8 including real-artifact C Make wiring, relocation
  neutrality, no-clobber,
  report-last rollback, exact unsupported inventory, checked arithmetic,
  explicit zero-cost floors, and all seven one-byte/one-credit failures.
- Real BOB result: 47 families, 14 supported S64B-v2 banks, 20 drawable
  unsupported rows (18 shadow, one scale, one asm), two `MODEL_NONE`
  sentinels, 160,928-byte S64F, SHA-256
  `5703b8485fc68c2ddfd8cd91cecdf54813f71e2532742430a7f4dd09c82bc97d`,
  40,920 embedded-bank bytes, 16,640 texture bytes, 2,816 CLUT bytes,
  544-byte lane stride, and 1,091-byte workspace. The authoritative 256-byte
  rounding yields a 1,280-byte fixed capacity and positive 189-byte margin.
- The exact 5,288 / 85,512 / 65,788 / 29,352 source-ceiling envelope is
  retained as a named non-acceptance diagnostic. Individual-bank proof uses
  2,718 output, 1,351 command, and 892 Gouraud actor shares with witness maxima
  46/24/46 and floors 59/56/19; the observer floor is 64 and the conservative
  guaranteed-any-mix count is exactly 19. Every individual margin is positive.
- The ordered VDP1 equation reports the 446,432-byte post-command/Gouraud
  region and existing 333,696 terrain-texture, 25,600 Mario-texture, and 34,464
  terrain-CLUT reservations. Future actor texture/CLUT repartition consumes
  19,456 of the resulting 52,672-byte remaining region and leaves 33,216 bytes;
  current binders are explicitly not claimed. The 2,340,436-byte provisional
  S64P plus the actor S64F uses
  2,501,364 of the 4,194,304-byte cart, leaving 1,692,940 bytes. Package
  ownership remains exactly ten canonical classes.
- Publication is private-stage, atomic exclusive-link, payload/dependency/
  header first and report last; existing outputs reject before compilation and
  injected mid-publish failure rolls back all published paths. Relocated builds
  make all four outputs byte-identical and path-neutral.
- Provenance: same-project GPL-2.0-only close-port/direct extension of
  `actor_family_bundle.py` (`68ceec9c`), `actor_variant_bank.py` (`86de51cc`),
  `compile_actor_bank.py` (`83cfc1ad`), `compile_scene_package.py`
  (`a3e22842`), and `target_profile.py` (`b345dd15`); no external source copied.
- Combined Make closure regenerated the provisional package and passed the
  existing scene-package suites (12 + 3), Task 5 focused suite (8), mixed-S64F
  C validator (54 mutations), real 47-family/14-variant C validation, S64B-v2
  C validator (86 mutations), and the
  articulated capability target. Historical/parser/closure is 87/87;
  compileall and scoped diff/hash checks pass. The opaque capability fixture's
  stale `56e9a35c...` expected-output digest first RED-exited at its trust
  comparison; two regenerations were identical at header `60c329ab...` and
  payload `db611af6...`, and the authorized test-only reseal makes the complete
  capability target GREEN without production changes. The full prescribed
  wave passes at fresh package generation 5; S64F SHA is `d83789cb...` and the
  1,280-byte-capacity header SHA is `c0faab4a...`. Independent review remains
  open. All target residency, runtime admission, renderer, Ymir, manual,
  release, and whole-game gates remain unchecked.

## Task 5 review and fix round 1/5 (2026-08-12)

- Independent review of `d2a14c63..65a3fdb9`: Spec FAIL / Quality Needs
  fixes, C0/I3/M0. Verified findings were unauthenticated family-report input,
  a stale-generation Make graph, and manual target-profile/package ownership
  parsing. Task 6 remained closed.
- Fix-round RED proved that altered Cannon ceiling, package generation,
  capabilities, source hash/count, and absolute/escaping/case-colliding source
  paths were accepted; that changed inputs silently reused generation-1
  outputs; and that absolute/outside/incomplete package descriptors bypassed
  the intended profile boundary.
- The compiler now regenerates family semantics through the owning
  `compile_actor_family_banks` implementation and exact-compares every field
  except its generated payload pathname, then consumes only regenerated
  values. Package inventory now uses `resolve_target_profile`; the shared
  read-only dependency API returns the fully validated canonical profile,
  descriptor, and payload input set for Make.
- The bundle report has real source/tool/profile/descriptor/payload
  prerequisites. An unchanged generation is verify-only; a changed input for
  an existing generation invokes publication and fails at the no-clobber
  boundary. Verification requires all four sidecars, checks their mutual
  semantics, and byte-compares them with a private deterministic rebuild from
  current closure/family/profile/model/generation inputs.
- Focused GREEN is 11/11 plus the direct target-profile API test. The final fresh
  generation-9 combined closure/provisional/family/bundle build passed, an
  unchanged repeat invoked only verification, and the prior generation-1
  stale-input probe failed closed with `publication target exists`. Missing or
  corrupt mutation of each sidecar is rejected. The complete Make wave passed:
  54 mixed-S64F mutations, real 47-family/14-bank S64F, 86 S64B-v2 mutations,
  and both capability gates. Broader historical/parser/closure/profile coverage
  passed 112/112; compileall, scoped diff, and four-sidecar hash checks pass.
  Repair behavior is `2cc767c0`. Task 5 remains
  source-complete-pending-rereview and all
  Task 6+/runtime/residency/renderer/Ymir gates remain open.

## Task 5 review and fix round 2/5 (2026-08-12)

- Scoped rereview of `65a3fdb9..8f1cacbe`: Spec FAIL / Quality Needs Fixes,
  C0/I1/M0. Round 1 cleared family reconciliation, publication freshness,
  four-sidecar validation, and canonical package ownership. The sole remaining
  finding was eleven output-affecting repository-local Python imports missing
  from `ACTOR_FAMILY_BUNDLE_TOOL_INPUTS`; current-input verification rejected
  semantic drift, but Make did not invoke publisher/no-clobber for those tools.
- TDD RED's deterministic recursive AST closure named exactly the eleven
  omitted modules. Repair `cf8bef5c` adds those normal prerequisites and makes
  the regression require closure coverage plus existence of every declared
  tool path. No compiler, parser, target, runtime, residency, renderer, or Ymir
  behavior changed.
- Generation-11 `make -W` on formerly omitted `vdp1_texture.py` now selects
  publisher and verifier; the actual same-generation call fails closed with
  `publication target exists`. The unchanged generation-11 repeat is
  verifier-only and GREEN. The full fresh Make/C wave is GREEN (scene-package
  12+3, focused 12, mixed S64F 54, real BOB 47/14, S64B-v2 86, and both
  capability families). Post-hardening focused is 12/12, broader historical/
  parser/closure/profile is 112/112, and compileall/diff/hash gates pass.
- Same-reviewer round-2 verdict at `13854e17`: Spec PASS / Quality PASS,
  C0/I0/M0. Independent closure is 21 local modules, all present in the 23
  declared inputs with zero missing/nonexistent paths and only the intentional
  `collect_scene_closure.py` / `scene_package_schema.py` extras. The reviewer
  independently reconfirmed baseline verifier-only, forced publisher+verifier,
  no-clobber failure, focused/profile/C/capability/source-policy/diff/AST/hash,
  docs, and scope gates.
- Task 5 is complete for host scope at `cf8bef5c` plus evidence `13854e17` and
  this verdict record. Task 6 and every target/runtime/residency/renderer/Ymir
  gate remain closed.

## Task 6 active transition (2026-08-12)

- Reconciled plan, ledger, and HEAD `a553b500`; the pre-existing dirty/untracked
  set belongs to other work and remains untouched. Task 6 is active for the
  target material binder and IR width contract only; Task 7 residency,
  publication, runtime activation, renderer integration, Ymir, release, and
  whole-game gates remain closed.
- Reference-code-first inspection pinned libyaul gitlink
  `6012f79f237773378c8014e70d8998ad95a38d98` (MIT):
  `libyaul/scu/bus/b/vdp/vdp1/cmdt.h` and `vdp1/vram.h`. Same-project shared
  sources inspected at `a553b500` are `saturn_ir_texture.*`,
  `saturn_actor_bank.*`, `actor_bank_format.py`, `actor_bank_v2.py`, and
  `actor_material_v2.py`. Reuse mode is dependency/API use and in-repository
  shared-core extension; no external source is copied.
- TDD gate is open: write the Task 6 IR/material fixtures first and capture the
  prescribed uint8 truncation/missing-API RED before production edits. No test,
  host GREEN, freestanding SH-2 compile, behavior commit, or independent review
  is claimed yet.

## Task 6 source-complete evidence (2026-08-12)

- Status: source-complete-pending-review from base `a553b500`; behavior commit
  `863b4646`. Task 7 and every
  residency/publication/runtime/renderer/Ymir/release gate remain closed.
- TDD RED: `make -k -f Makefile.saturn.mk verify-ir-texture
  verify-actor-material` exited 1. The IR test compiled then aborted at the
  first 256-width boundary because the old public `uint8_t` parameter
  truncated it to zero; the material test failed to compile because
  `saturn_actor_material.c` did not exist. Production files were untouched at
  that point.
- GREEN: `make -f Makefile.saturn.mk verify-ir-texture
  verify-actor-material` passes. It proves exact 8/248/256/504 size encoding,
  the `504x255 -> 0x3FFF` maximum, all non-multiple/0/505 failures, complete
  CLUT16/RGB1555 span checks, blend-mode validation, aligned/nonoverflowing
  texture/CLUT addresses, all four vertices, and byte-exact no-command-mutation
  failure. The material fixture covers stable recipes 1..7 independently of
  Yaul enum values, exact source/CLUT/size/mode/end-code fields, stale/zero
  generation, wrong/zero bank, tile-count mismatch, invalid primitive/
  material/tile/format, null inputs, partition shortage, and address overflow.
- Broader fresh host wave passed: S64B-v2 `86` mutations, mixed S64F-v3 `54`
  mutations, historical pose, meshlet plus invalid-span mutation, feature-off
  wrapper `6/6`, and variant/source `40/40`. Historical Mario remains exactly
  596,896 bytes with SHA-256
  `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
- Both production modules passed the installed libyaul SH-2 compiler with
  sourceboot-equivalent `-std=c11 -m2 -mb -Os -g -ffreestanding -fno-lto
  -Wall -Wextra -Werror -pedantic` flags, first `-fsyntax-only` and then real
  object emission. Objects were 20,904 bytes for `saturn_ir_texture.o` and
  24,544 bytes for `saturn_actor_material.o`; this is compiler evidence, not a
  target link or execution claim.
- At initial source completion, the unrelated adjacent gate
  `verify-actor-family-bank` reached its C
  validator only with an explicit Windows-root override, then returns 1 because
  `actor_family_bank_test.c` hardcodes historical payload SHA-256
  `00e5754c80762a15b5482fb1f2e88f4bc1fc7ab847f3463944e2e6689d412ee8`.
  The current Task-5-attested payload and report agree on
  `db611af699337f38a2284abf58cb287c70f9ab6e5df5b07555cda48aba7bf313`.
  That test-only trust-anchor file was then outside Task 6's strict file list,
  so it remained explicitly unchecked at that transition; fix round 1 below
  records its later authorized reseal and GREEN.
- Self-review found no production edit outside the approved module/Make/docs
  list. The stable S64B enums stay in `saturn_actor_bank.h`; Yaul values appear
  only in the new master-owned translation C file. Workers receive no pointer,
  command, partition, or mapping state. All fallible parsing, arithmetic, and
  range work precedes command mutation; textured final writes delegate to the
  atomic IR binders. Task 7 code and sourceboot runtime integration are absent.

## Task 6 review and fix round 1/5 (2026-08-12)

- Independent review of `a553b500..5ceb251c` returned Spec FAIL / Quality needs
  fixes, C0/I3/M0. Verified findings were start-only IR/aggregate hardware
  address validation, missing bank-local CLUT ordinal validation, and the
  already-recorded stale family-bank test anchor. Task 7 remained closed.
- Serial TDD RED first caught IR CLUT16 full-span wrap and actor aggregate
  texture wrap with byte-exact command preservation; after span repair, the
  one-palette `clut_id 0 -> 1` mutation was accepted and supplied the second
  production RED. `verify-actor-family-bank` separately exited 1 at the old
  `00e5754c...` payload trust comparison after its report gate passed.
- Repair validates every nonempty integer address span through its last byte,
  casts only final checked IR addresses, and treats zero aggregate bytes as
  touching no address. Actor binding retains scalar partition and per-tile IR
  checks while adding complete aggregate texture/CLUT checks and the local
  dense palette bound before global mapping addition.
- Family payload generation run twice produced identical payload SHA-256
  `db611af699337f38a2284abf58cb287c70f9ab6e5df5b07555cda48aba7bf313`,
  header content SHA-256
  `60c329ab3e8bcd8bc7d13869c123706af5517f5bd7efd3b3a539b70212e28b73`,
  and 128,917 bytes. Only `actor_family_bank_test.c` is resealed; the parser,
  compiler, and effect oracle are unchanged.
- Full host wave passes IR/material, S64B-v2 86 mutations, family-bank 47/13/14,
  mixed-S64F 54 mutations, pose, meshlet and invalid-span mutation, feature-off
  6/6, and variant/source 40/40. Both production modules pass exact SH-2
  freestanding syntax and object compilation; fix-round objects are 22,504 and
  25,216 bytes. Repair behavior is `661e54a4`; at that transition same-reviewer
  rereview remained mandatory and every Task 7/runtime/Ymir gate stayed closed.
  The closure below records the later verdict.

## Task 6 same-reviewer closure (2026-08-12)

- Same-reviewer rereview of exact repair range `5ceb251c..d0fbc5aa` passed
  Spec PASS / Quality PASS, C0/I0/M0. All three original Important findings are
  addressed with no new or out-of-scope finding.
- Fresh approval evidence reconfirmed IR/material PASS, S64B-v2 86 mutations,
  family bank 47 families / 13 unsupported representatives / 14 records,
  mixed-S64F 54 mutations, pose, meshlet plus invalid-span mutation,
  feature-off 6/6, variant/source 40/40, both exact SH-2 freestanding syntax
  and object compiles, and scoped diff/cleanliness gates. Fix-round object
  sizes remain 22,504 and 25,216 bytes.
- Task 6 is complete for host and freestanding target-module scope. This does
  not claim a target link/run, residency, publication, runtime activation,
  renderer, Ymir, release, or manual result. Task 7 remains closed until this
  status record is committed.
- Nonblocking adjacent evidence: fresh `verify-actor-effects` reaches its host
  effect tests, then exits 1 at `actor effect family source identity drift`
  because the pre-existing effect oracle still pins header/payload SHA-256
  `56e9a35c2ee24c92e0cf3f7f80f5a10720e87183c9cd0f5a18e135b463a71bad` /
  `861be66d903460f9e54689a2dccbd611374a9123e3db75c6bc291c0d7ca7f298`
  instead of the current `60c329ab...` / `db611af6...`. This belongs to the
  later effects/runtime gate, does not cover Task 6 material binding, and its
  oracle is intentionally unchanged.

## Task 7 active transition (2026-08-12)

- Base `ce28a7d9`; Task 6 is independently approved and Task 7 is now active.
  Scope is fixed all-resident actor texture/CLUT planning, checked master queue
  upload, scalar generation-last publication, and scene-residency ownership.
  Task 8/runtime/renderer/Ymir/release and every target evidence gate stay
  closed.
- Authorized corrections: add existing `saturn_ir_texture.c` solely for
  `sm64_saturn_texture_residency_init_region` because it owns the current
  implementation; use the existing checked GPL-3.0-or-later
  `saturn_dma_queue_submit`/`wait` boundary because pinned MIT libyaul's raw
  SCU-DMA calls return void. No queue source or Task 6 binder edit is allowed.
- Reference record and exact requirements are in `task-7-brief.md`; report is
  `task-7-report.md`. RED/GREEN, SH-2 compilation, behavior commit, and
  independent review are pending.

## Task 7 source-complete evidence (2026-08-12)

- Status: source-complete-pending-review from base `ce28a7d9`; behavior commit
  `7cbd06ed` is `feat(saturn): publish actor texture residency generations`.
  Task 8/runtime activation/renderer/Ymir/release remain closed.
- Authorized file/design corrections are complete: `saturn_ir_texture.c` owns
  the new bounded-region initializer while its old initializer delegates;
  checked queue submit/wait replaces uncheckable raw libyaul DMA; the unchanged
  16-byte scalar mapping ABI moved into a lightweight Yaul-free publication
  header for the scene owner. Production asserts fix mapping size/alignment/
  offsets, 128-entry capacity, generation/commit offsets, and the 2,064-byte
  HWRAM footprint.
- Residency generation is deliberately independent from S64F package
  generation. S64F's validated unique nonzero scalar bank IDs make one mapping
  per canonical selected v2 variant the exact rule; distinct hashes never
  deduplicate on a 32-bit alias. Real BOB is exactly 14 mappings / 16,640
  texture bytes / 2,816 CLUT bytes and includes Cannon.
- Complete S64F/S64B/hash/aggregate/source/destination planning precedes the
  first DMA. Inclusive-last checked `uintptr_t` arithmetic covers legal spans
  ending at `UINTPTR_MAX`; no typed pointer addition precedes the proof. Queue
  submit/wait failures and every input/lifecycle failure leave mappings/counts/
  generation/committed zero, while transferred bytes may be dirty but are
  unreachable. Publication writes generation and committed last with compiler
  fences.
- Scene-owned lifecycle is fail-closed without changing existing return
  semantics: reset clears publication; failed staging clears the matching
  staged generation; commit retains only an exact matching committed actor
  generation; matching inactive unload clears stale state. The scene header
  remains consumable without Yaul and no renderer path is activated.
- TDD RED: the exact actor target exited 1 at the absent implementation, and
  the combined `-k` actor/scene wave also failed at the absent publication
  type/accessors before production edits. Focused actor residency, scene
  residency, IR texture, and actor material GREEN passes after implementation.
- Broader fresh host evidence passes: historical bank/bundle Python 26/26;
  S64B-v2 86 mutations; S64F 54 mutations; actor-family 47/13/14; VDP1 frame
  bank; checked DMA queue; Gouraud; pose; meshlet and invalid-span mutation;
  feature-off 6/6; variant/source 40/40. Exact GCC 14.3.0 SH-2 freestanding
  syntax and object compiles pass for all four amended/new production modules;
  final objects are 35,092 bytes actor residency, 23,144 IR texture, 25,184
  actor material, and 64,528 scene residency. Scoped and staged diff checks pass;
  explicit inventory contains only the 16 Task 7 paths and leaves unrelated
  dirty/untracked work unstaged.
- The untouched adjacent A8 deferred-transfer runtime source contract remains
  5/7 at its known destination-poison and VDP2-camera/frame-bank requirements.
  It is not Task 7 evidence and sourceboot is not edited. Independent Task 7
  spec/quality review, target link/run, runtime, Task 8+, renderer, Ymir,
  release, visual/manual, and total-game gates stay unchecked.

## Task 7 review and fix round 1/5 active (2026-08-12)

- Frozen reviewed head is `97dae9b2`. Independent review returned C0/I4/M1;
  all findings are accepted for serial TDD repair. Task 7 is
  source-complete-pending-fix-and-rereview, and Task 8 remains closed.
- Repair scope adds only the existing DMA queue header/source/test, the active
  design spec, and the existing Task 7 files/docs. No raw Yaul, queue scheduling,
  sourceboot, runtime bundle, renderer, or Ymir edit is authorized.
- RED order: isolate scene staging access from an older active publication;
  centralize complete publication self-consistency for activation and lookup;
  introduce one bounded caller-owned HWRAM cold-span stage plus a read-only
  checked-queue preflight; make the residency verifier build/verify its real
  Task 5 dependency; replace implementation-defined signed generation ordering
  with explicit nonzero half-range unsigned serial arithmetic.
- The approved architecture correction keeps the complete S64F in CART, CPU-
  copies one validated cold span at a time into caller-owned HWRAM, and submits
  only `(VDP1 destination, HWRAM stage)` to SCU DMA. Real BOB's measured maximum
  staged span is 2,560 bytes, but the API remains capacity-driven; final HWRAM
  budget/link evidence belongs to later target gates.

## Owner convergence correction (2026-08-12)

- The milestone is now a working normal BOB scene through the canonical package,
  registry, generic queue, generic bank/material lookup, and production renderer
  by 2026-08-14. Cannon remains a regression witness, not the acceptance slice.
- All 34 drawable BOB selections must be generically admitted. Common,
  telemetry-visible Saturn reductions are allowed; injected models, forced
  records, object-specific renderer branches, and whole-scene quarantine for a
  drawable are not.
- The persistent memory-debt rule is recorded in project `AGENTS.md` and
  `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`. The active plan now binds CART,
  HWRAM staging, VDP1, publication, LWRAM actor arena, frame-credit ownership,
  transport, lifetime, and first consumer before Task 8 begins.
- The requested two-week behavior audit is preserved at
  `audits/audit-20260812-0849.md`, SHA-256
  `8d979ee55aa9c9d2467656a94ed32e39e07ea0381df1b117583d5c90f8f54ca5`.
  It records 888 commits, six failed first reviews in seven current-sprint
  tasks, proof-before-live-use, and repeated late ownership redesign as the
  process defects this convergence reset is intended to stop.
- The first short identity-bound Ymir smoke moves directly after the minimum
  Task 8 package plus Task 9 cutover/build path. Exhaustive capacity, release
  reproduction, reseal, and final manual evidence remain later gates.
- The Task 7 clean-output integration gate rebuilt and C-validated a fresh real
  47-family/14-variant bundle, then passed actor residency. The canonical stale
  generation correctly refused overwrite. The temporary generated output was
  removed after the passing run.

## Task 7 fix-round memory-owner gate (2026-08-12)

- Focused RED isolated the absent physical-stage authority: exact/P2 HWRAM,
  CART/LWRAM, one-byte HWRAM overflow, and stage overlap were not centrally
  decidable. A separate Make RED proved the actor residency target did not own
  the real Task 5 producer/validator chain.
- GREEN now permits only a nonempty caller-owned HWRAM span ending at or before
  `0x06100000`, normalizes only P0 cached/P2 cache-through aliases, and rejects
  P1/P3/P4 cache-control shapes plus overlap with the S64F
  bundle, scalar publication, or either complete VDP1 destination region before
  any queue preflight, CPU copy, or DMA. Generation replacement uses explicit
  unsigned half-range serial arithmetic; the half-range ambiguity rejects.
- Fresh absent-output integration passed in 209.3 seconds: real BOB bundle C
  validation 47 families / 14 variants, actor texture residency PASS, and 13/13
  publisher/inventory tests. Exact SH-2 `-m2 -mb -ffreestanding -Werror`
  syntax compilation passes. The task-created four-file output directory was
  verified beneath `build/saturn` and removed; no canonical artifact changed.
- Repair behavior commit is `4c4c24a9` (`fix(saturn): bind actor residency
  memory ownership`). Status is source-complete-pending-rereview. Task 8,
  runtime activation, renderer, Ymir, release, visual/manual, and total-game
  gates remain open and are not replaced by these host/module results.
- Rereview found one new Important memory debt: the empty-publication helper
  placed a 2,064-byte zero object on the SH-2 stack (`2076` bytes exact stack
  usage). RED was captured with installed GCC 14.3 `-fstack-usage`. The
  follow-up scans scalar fields/128 mappings in place, including a last-unused-
  row mutation regression; no publication-sized local object remains.
- Pre-Task-8 memory reconciliation found two previously omitted bound owners:
  the generated actor workspace is exactly 1,280 bytes LWRAM (1,091 used, 189
  margin, two lanes), and existing scene begin/commit validation still measures
  3,920/3,420-byte SH-2 stack frames from package-view/identity locals. Task 8
  must reuse the state-owned staging view/slot and prove ≤256 bytes per call by
  exact `-fstack-usage` before any target wiring. The runtime will alias the
  S64P/S64F already placed by `source_cart`; no redundant streamer is allowed.
- Same-reviewer rereview then found the HWRAM check normalized every SH-2 area
  before classification, allowing cache-control shapes such as `0x460FF600`
  to masquerade as HWRAM. Focused RED reproduced that acceptance. GREEN now
  requires P0 cached or P2 cache-through shape before physical masking;
  `0x460FF600`, `0x660FF600`, and `0xC60FF600` reject, while the exact P0/P2
  2,560-byte top fit remains accepted. The checked-queue host double exempts
  >32-bit fixture pointers from Saturn area decoding to remove an ASLR-only
  false failure. Native host actor residency and exact SH-2 freestanding
  syntax compilation pass; rereview was still open at this checkpoint and is
  closed by the final verdict below.

## Task 7 approved; Task 8 active (2026-08-12)

- Same-reviewer final rereview of `97dae9b2..d1408e00` passed Spec PASS,
  Quality PASS, C0/I0/M0. Fresh evidence includes the isolated absent-output
  real bundle (47 families / 14 variants), 13/13 publisher/inventory tests,
  actor/scene/DMA/IR/material/frame/Gouraud/bank/bundle/pose/meshlet/feature-off
  and 40/40 variant-source gates, plus installed SH-2 GCC compilation.
- Actor activation measures 476 bytes of target stack and the scene texture-
  staging accessor 0 bytes. Same-generation publication still refuses
  overwrite. All Task 7 review findings are closed and Step 5 is complete.
- Task 8 opened from approved status `dfa8b286` under the owner convergence
  rule. Its first RED was the existing 3,920/3,420-byte scene-validation stack;
  acceptance was <=256 bytes per call using state-owned slots/views. It had to
  reuse the package already in CART, bind the 2,560-byte cold-stage lifetime
  and fixed 1,280-byte LWRAM workspace, and avoid a redundant streamer. The
  source-complete record below replaces the provisional persistent-stage
  assumption with the verified phase-borrowed command-bank owner.

## Task 8 source-complete package/runtime owner (2026-08-12)

- Status: `source-complete-pending-review` from approved Task 7 HEAD
  `dfa8b286`; behavior commit `b84103cd`. Evidence/status commit and the
  independent verdict remain open;
  Task 9, target link, Ymir, release, and manual gates remain unchecked.
- RED established the missing canonical payload-root contract, bundle runtime,
  source owner, v1/v2 workspace resolution, and the existing exact SH-2 scene
  validation stack debt of begin 3,920 B / commit 3,420 B.
- GREEN generation 14 binds one 740-byte S64P
  (`9b0a0a4a5ce1f059417175a7ad76e8ec6141a96c26af61065d48475f4a40d101`)
  to one 160,928-byte S64F-v3
  (`3eee00fd9a7440ba669c694d947696b8989192d54b0084decd832a95def6a523`).
  The deterministic CART assembly is
  `bde84bbbf71e201d3ff6a62f5f3ee1182b2583a75c84ece45285a72ef24f15fb`
  and assembles to a `0x277a4`-byte `.rodata` section with exact root/bundle
  symbols. The root duplicates no terrain, collision, or sky payload.
- The package contains 47 families, 14 v2 variants, 20 named unsupported
  drawable selections, and two `MODEL_NONE` objects. Common runtime lookup
  resolves and prepares a real v2 bank with nonzero bounded draw output; new
  production code contains no Cannon/model/behavior/family special case.
  Cannon is only one test/result. Task 9 still owns the remaining 20 drawable
  compiler states and the all-34 production generic job/emitter cutover.
- The corrected memory owner is Saturn-shaped: the persistent LWRAM source
  owner is 5,556 bytes (2,208-byte owner plus a 3,348-byte union). Boot root
  validation and the fixed 1,280-byte/two-lane runtime workspace have disjoint
  lifetimes and share that union. Scene residency adds 320 bytes of scalar
  dependency metadata. Exact GCC 14.3 SH-2 stack is source init 220 B, source
  resolve 44 B, scene begin 92 B, commit 88 B, and section load 112 B.
- The original fixed-HWRAM-stage wording was corrected before completion.
  Sourceboot phase-borrows 2,560 bytes from the idle VDP1 command bank during
  boot, waits after every checked SCU DMA, and returns the bank before its first
  frame use. This adds zero persistent HWRAM. Actor VDP1 texture/CLUT regions
  remain 16,640/2,816 bytes and leave 33,216 bytes of Yaul remaining capacity.
- Package tools now require one explicit payload root and reject absolute or
  escaping manifest/payload paths before validation, hashing, or generated
  header emission. Deterministic source uses only repository-relative
  `.incbin` operands.
- Final host GREEN: package schema 16/16 and determinism 3/3; actor texture
  residency, bundle runtime, source owner, scene residency, S64B-v2 86
  mutations, mixed S64F 54 mutations, pose, meshlets plus invalid-span,
  instance queue, batches/neutrality 2/2, and feature-off 6/6. Python
  compileall and scoped whitespace checks pass. Exact installed SH-2 GCC
  `-m2 -mb -ffreestanding -Wall -Wextra -Werror` syntax/object/stack gates
  pass for source owner, bundle runtime, meshlets, texture residency, and scene
  residency.
- The full hermetic sourceboot candidate is intentionally not claimed. The
  development worktree's pre-existing `build/us_pc` fixture resolves outside
  its root, and identity-assets correctly fails closed on that fixture before
  link. The repository MSYS wrapper and GNU Make 4.4.1 run normally; no MSYS
  DLL loader failure occurred. A proper clean candidate is required after
  independent Task 8 review and Task 9 cutover.

## Task 8 review and repair round 1/5 (2026-08-12)

- Independent review of `dfa8b286..0a180e39` returned Spec FAIL / Quality
  needs fixes C0/I3/M0. Findings were: command bank 0's boot-stage borrow
  overwrote the already-initialized system/local/END prefix; activation omitted
  exact stable-ID and scene-lifetime binding; root/sidecar/assembly generation
  overwrote existing bytes despite the no-clobber contract. Task 9 remained
  closed.
- Focused RED reproduced all three defects. GREEN repair `dc81808b` moves both
  backend initializers after cold-stage retirement, binds exact
  `bob-area1-actors-v3` plus scene lifetime, and close-ports the reviewed Task 5
  exclusive private-link publisher for all final S64P outputs. Assembly is now
  compiler-generated; Make has no overwrite redirection.
- Fresh evidence: package schema 19/19; determinism 3/3; exact repository MSYS
  wrapper `verify-source-scene-bundle` PASS twice; second-run hash+mtime
  inventory unchanged 7/7; focused actor/package/scene Make wave PASS; Python
  compileall PASS; exact installed GCC 14.3 ELF32 big-endian SuperH object PASS.
  S64P/S64F/assembly identities remain `9b0a0a4a...d101`,
  `3eee00fd...a523`, and `bde84bb...15fb`.
- Two pre-repair JSON sidecars differed only by CRLF (160 and 137 line endings)
  and were moved recoverably to `.superseded-crlf-*` before canonical LF
  publication. No MSYS DLL loader failure occurred. Status is
  `source-complete-pending-rereview`; evidence commit and same-reviewer verdict
  remain open, and Task 9/link/Ymir/release/visual/manual remain unchecked.

## Task 8 repair round 2/5 (2026-08-12)

- Same-reviewer rereview of `0a180e39..c8a2fdf8` returned Spec FAIL / Quality
  needs fixes C0/I1/M2. Runtime prefix restoration and dependency identity
  passed. Remaining Important: individually exclusive links could leave a
  partial generation after a late conflict. Minors corrected source init-from
  stack 92→96 B and narrowed DLL evidence to the tested wrapper route.
- RED: divergent static target at every seven-file position; injected late
  conflict at each link position; header published before a divergent ABI;
  identical and mixed divergent concurrent publishers. GREEN behavior
  `3b81456b` precomputes/preflights/stages the whole set, publishes report/ready
  last, and ownership-rolls back only links still matching the transaction's
  exact file identity. Concurrent identical publishers converge and divergent
  producers cannot mix a generation.
- Fresh: schema 22/22; determinism 3/3; full focused actor/package/scene Make
  wave PASS through the repository MSYS wrapper; generation-set hash+mtime
  inventory unchanged 7/7; Python compileall PASS; installed GCC 14.3 emits
  ELF32 big-endian SuperH with exact source stack init/init-from/resolve
  220/96/44 B. Evidence/status commit and same-reviewer round-2 verdict remain
  open. Task 9 and all linked/Ymir/release/visual/manual gates remain closed.

- Pre-rereview self-audit found and closed one identical-publisher race: a
  publisher could observe another transaction's identical early link, then the
  owner could roll it back. Follow-up `b2f66f5a` serializes the entire set with
  an OS-held generation lock outside build outputs and rejects an existing
  ready marker whose siblings are incomplete. Schema is now 23/23; the locked
  exact-wrapper gate passes and preserves hash+mtime 7/7. The same reviewer was
  interrupted before this edit and must rereview the new exact range.
