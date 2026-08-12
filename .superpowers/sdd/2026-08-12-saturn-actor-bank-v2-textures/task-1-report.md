# Task 1 implementation report — version-owned host S64B parsing

## Status and commits

- Status: `source-complete-pending-review`.
- Base: `3f50bb111bfa5297cd736523594ed9982be02d9c`.
- Behavior head: `68ceec9c` (`refactor(saturn): centralize actor bank version parsing`).
- Scope: host parsing only. No target/runtime, Make feature, heap/pointer, or
  relaxed-span change was made; version 2 stops at the explicitly named host
  boundary and has no acceptance path.

## Exact behavior file list

- `CHANGELOG.md`
- `docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`
- `tools/saturn/actor_bank_format.py`
- `tools/saturn/actor_family_bundle.py`
- `tools/saturn/actor_variant_bank.py`
- `tools/saturn/test_actor_bank_format.py`
- `tools/saturn/test_actor_family_bundle.py`
- `tools/saturn/test_actor_variant_bank.py`

This report is the only follow-up documentation path. The controller retains
the plan-scoped progress-ledger completion and independent-review entries.

## RED evidence

Before production changes, ran:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_format \
  tools.saturn.test_actor_family_bundle \
  tools.saturn.test_actor_variant_bank -v
```

Result: expected RED, `Ran 11 tests`; the new format and variant suites failed
with `ModuleNotFoundError: No module named 'actor_bank_format'`, and the bundle
ownership test failed because `_S64B_HEADER` remained in
`actor_family_bundle`.

## GREEN evidence

Fresh final focused command:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_format \
  tools.saturn.test_actor_family_bundle \
  tools.saturn.test_actor_variant_bank -v
```

Result: `Ran 37 tests ... OK` (3.508 s). It covers historical v1 Mario,
unknown version 0/3 dispatch, the exact `S64B v2 contract is not implemented`
boundary, source binding, both migrated callers, and existing malformed v1
coverage. `py_compile` passed for the three implementation modules; scoped
`git diff --check` passed.

Required host gates ran successfully after the sandbox blocked Make's fixture
write and the same command was re-run with approved elevated filesystem access:

```text
make -f Makefile.saturn.mk verify-actor-family-bundle \
  verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
```

- actor-family-bundle: `PASS (53 mutations)`;
- actor-variant-bank plus actor-source: `Ran 37 tests ... OK`;
- actor-pose-bank: `PASS`;
- actor-meshlets: `PASS`.

## Historical byte/hash proof

The committed format test rebuilds the historical Mario v1 bank and pins its
payload SHA-256 to
`242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`.
The rebuilt document's `payload_sha256` equals that exact committed value and
the parser accepts those unchanged 596,896 bytes.

The existing Mario artifact and its pre-existing `task3-refactor` reference
were rehashed after the gates:

| Artifact | SHA-256 | Result |
| --- | --- | --- |
| `mario.s64b` / `task3-refactor.s64b` | `242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539` | exact |
| `mario-actor-bank.json` / `task3-refactor.json` | `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0` | exact |
| JSON payload size | `596896` / `596896` | exact |
| JSON source SHA-256 | `60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1` / same | exact |

S64F-v3 wire proof remains the existing exact deterministic fixture: focused
test `test_deterministic_bytes_layout_metadata_alias_and_zero_padding` passed
with 1,688 bytes and SHA-256
`4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.

## Reference/reuse record

- Upstream/source: in-tree reviewed source at base
  `3f50bb111bfa5297cd736523594ed9982be02d9c`, no external repository.
- Files/functions inspected: `tools/saturn/actor_family_bundle.py`,
  `_S64B_HEADER`, `_S64B_ANIMATION`, `_GEO_HEADER`, `_MESHLET`, `_workspace`,
  and `_validate_s64b`.
- Reuse mode: shared-core move/close-port. The exact v1 validation semantics
  now reside in `tools/saturn/actor_bank_format.py`; both former owners import
  `validate_actor_bank`, and bundle source binding uses
  `validate_actor_bank_expected`.
- License/attribution: same repository source; no external license or NOTICE
  obligation was introduced.

## Self-review and open gates

Self-review PASS: the v1 header and validator no longer exist in
`actor_family_bundle`; both callers use the version-owned authority; the v1
canonical ordering, bounds, GEO1, workspace, source hash, and output bytes
remain strict; version 2 rejects only with the exact named unimplemented
contract error. The v1 view uses zero texture/CLUT spans and `hot_end` equal to
the complete v1 payload; Task 2 owns all v2 interpretation.

Open gates: independent spec-compliance review and code-quality review are
required before Task 2; every Task 2-13 host/compiler/target/runtime/build,
texture/CLUT, scene, emulator, release, visual, desktop, manual, retail, and
total-game gate remains open. No host-only result is claimed as target evidence.

## Fix round 1 — complete v1 header padding ownership

Reviewer finding: the original version-owned `_S64B_HEADER` format was 102
bytes even though v1 declares a 104-byte header, leaving bytes 102 and 103
structurally unowned. The fix commit is
`9d5fc03ce1f4112ba750aaad7f648e5b158113dd`
(`fix(saturn): validate actor bank header padding`). It changes the authority
format to `>4s9HI32sHH10IH`, makes the final `H` an explicit zero-reserved
field, and rejects nonzero padding with `S64B header padding`. No v2 behavior
or target/runtime code changed.

RED command:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_format.ActorBankFormatTest.test_v1_header_padding_bytes_must_be_zero -v
```

RED result: one test ran and failed at both independent subtests, offsets 102
and 103, with `AssertionError: ValueError not raised`. That proves both bytes
were formerly accepted rather than merely testing source text.

GREEN commands/results:

```text
.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_format.ActorBankFormatTest.test_v1_header_padding_bytes_must_be_zero -v
# Ran 1 test ... OK

.venv-saturn-tools\Scripts\python.exe -m unittest \
  tools.saturn.test_actor_bank_format \
  tools.saturn.test_actor_family_bundle \
  tools.saturn.test_actor_variant_bank -v
# Ran 38 tests ... OK

make -f Makefile.saturn.mk verify-actor-family-bundle \
  verify-actor-variant-bank verify-actor-pose-bank verify-actor-meshlets
```

The re-run host gates passed: family bundle `PASS (53 mutations)`, variant plus
actor-source `Ran 37 tests ... OK`, actor pose `PASS`, and actor meshlets
`PASS`. The historical Mario rehash is unchanged: S64B payload
`242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539`,
JSON `3f0f2dd965e7fbe9e73d9b791053478d9b3fe73199087bb827b76912e4206bf0`,
payload size `596896`, and source SHA-256
`60f942e6f30d4a153393a47ac53626ee53d90ebeb750ee5d244d5ef2a16925c1`.
Both matched the pre-existing `task3-refactor` references exactly. The existing
S64F-v3 deterministic fixture remains 1,688 bytes with SHA-256
`4b3334a61f8ce7c8b2c4548a112b0c7354c444b42659ec7943941de5529e4dbc`.

## Independent scoped rereview

The same independent reviewer inspected `89fa92da..1e517bac` and reran the
focused padding test plus all three affected suites (38 tests, all passing).
The original Important finding is **ADDRESSED**: `_S64B_HEADER` is exactly 104
bytes, the parser consumes the explicit final `H`, and either reserved-byte
mutation rejects. No regression or out-of-scope issue was found; final counts
are C0/I0/M0 and Task 1 is approved.

Task 1 is complete at evidence head `1e517bac`. Tasks 2-13, target build,
Ymir Cannon demo, release reproduction/reseal, smoke, visual, desktop, manual,
retail, and total-game gates remain open.
