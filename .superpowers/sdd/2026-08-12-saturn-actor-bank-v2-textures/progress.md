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
