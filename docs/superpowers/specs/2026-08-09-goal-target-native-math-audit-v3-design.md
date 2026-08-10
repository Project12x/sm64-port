# Goal-target native-math audit v3 design

Date: 2026-08-09

Status: owner-approved design; implementation not started

## Purpose

Task 5's fully integrated sourceboot BOB target links successfully, but its
corrected linked-ELF native-math census is 700 while the existing v2 contract
pins 582 from a narrower historical artifact. The corrected census has no
unlisted unresolved transfer or unresolved register effect. The remaining
problem is contract scope, not target behavior.

The goal is to admit exactly the sealed Task 5 artifact to its measured census
without changing or weakening the historical v2 contract.

## Decision

Add audit-contract version 3 as an exact-artifact contract. It will bind:

- root `_game_loop_one_iteration`;
- total native-math census 700;
- the existing forbidden converted callers `_atan2_lookup` and `_atan2s`; and
- ELF SHA-256
  `562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a`.

The target's embedded build identity has effective configuration hash
`735756402029c2f43b4b9792077ca7f4393de9330a37ad12914c4c9ffb68ed59`
(`id-735756402029c2f4`). Binding the complete ELF hash also binds those embedded
identity bytes: a different configuration, source identity, link layout, or
artifact cannot satisfy the contract.

V2 remains byte-for-byte unchanged and remains the default sourceboot replay
audit contract. Task 5 selects v3 explicitly for the exact goal build; there
is no global switch of unrelated replay builds to the goal total.

## Contract and verifier interface

Create `tools/saturn/sh2_native_math_goal_audit_contract_v3.txt` with this
closed directive set:

```text
AUDIT_CONTRACT_VERSION 3
EXPECTED_ROOT _game_loop_one_iteration
EXPECTED_TOTAL 700
EXPECTED_ELF_SHA256 562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a
FORBIDDEN_CALLER _atan2_lookup
FORBIDDEN_CALLER _atan2s
```

`AuditContract` gains an optional expected-ELF digest. Version 2 forbids that
directive; version 3 requires exactly one lowercase 64-hex digest. Unknown,
duplicate, malformed, or version-inappropriate directives fail closed.

`verify_audit_contract_integrity` selects the immutable checked-in digest for
the parsed version. It continues to pin v2 with
`SIM_AUDIT_CONTRACT_V2_SHA256` and pins v3 separately with a new constant.
There is no fallback digest and no `PENDING` acceptance.

After parsing the contract and confirming the ELF exists, the verifier hashes
the ELF before invoking objdump/readelf. A v3 mismatch is an invocation error
and exits 2. It must not spend the full census time on an artifact that the
contract does not authorize. V2 performs no artifact check and retains its
existing behavior.

The corrected simulation route oracle remains shared. V3 does not create a
second callback allowlist; the exact artifact binding and the independently
pinned route-oracle digest cover different responsibilities.

## Build and verification flow

Task 5's exact 18-flag goal build supplies
`SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT` on the Make command line, pointing
to the v3 contract. GNU Make command-line variables propagate to the
sourceboot sub-make, so no Makefile default or target behavior changes.

The acceptance flow is:

1. Run the focused parser, digest, artifact-mismatch, and callback-oracle tests.
2. Rebuild the exact goal tuple. Its ELF SHA-256 must reproduce the v3 digest;
   otherwise stop and investigate identity drift rather than editing v3.
3. Run ordinary sourceboot verification with the explicit v3 contract. The
   renderer baseline, total 700, forbidden callers, indirect-edge closure,
   and unresolved-effect checks must all pass.
4. Verify ISO completeness and map margins from that same identity directory.
5. Run the required 20,000-frame post-handoff smoke/capture against its CUE.
6. Only after those gates pass, hand the CUE to the owner for Task 6 manual
   pickup/hold, sustained-camera, visual, stability, and FPS checks.

## Tests

TDD adds failures before implementation for:

- a valid v3 contract with the exact digest;
- missing, duplicate, uppercase, short, or non-hex v3 ELF digests;
- `EXPECTED_ELF_SHA256` appearing in v2;
- v3 text mutation failing its pinned contract digest;
- a matching file digest passing the target check;
- a one-byte-different artifact failing before disassembly;
- v2 retaining its current no-artifact-binding behavior; and
- the checked-in v2 and v3 fixtures both matching their separate pins.

The existing source-derived callback tests remain mandatory. The final target
audit is the integration proof; a green unit suite cannot substitute for it.

## Failure and maintenance policy

V3 is deliberately narrow. Any target source/configuration change, link-layout
change, or nondeterministic rebuild invalidates it. A future artifact requires
a new reviewed contract version or a separately approved identity-bound
contract; it does not justify editing v2 or silently changing v3's total/hash.

No runtime source, renderer behavior, object-pool capacity, target identity
schema, route oracle version, or historical v2 evidence changes in this work.
