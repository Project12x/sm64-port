# Task 5 evidence report: bounded textured BOB actor bundle

**Status (2026-08-12):** source-complete after fix round 1/5, pending scoped
independent rereview. The initial `65a3fdb9` review found C0/I3/M0. This is
host-only evidence. Target residency, renderer cutover, Ymir, manual visual,
release, and whole-game gates remain open.

## Outcome

- The real BOB closure emits one deterministic S64F-v3 containing 47 family
  records and exactly 14 supported S64B-v2 banks. The complete drawable
  unsupported inventory is retained as 20 rows: 18 `GEO_SHADOW`, one
  `GEO_SCALE`, and one `GEO_ASM`; two additional family variants are
  `MODEL_NONE` sentinels.
- At canonical package generation 1 the S64F is 160,928 bytes with SHA-256
  `5703b8485fc68c2ddfd8cd91cecdf54813f71e2532742430a7f4dd09c82bc97d`.
  Its embedded S64B bytes total 40,920, texture bytes total 16,640, CLUT bytes
  total 2,816, maximum lane stride is 544, and maximum workspace is 1,091.
  The design-owned 256-byte rounding emits a 1,280-byte fixed workspace
  capacity, leaving a positive 189-byte margin.
- Cannon remains an exact witness: family 29, model `0x0080`, 2,952-byte
  S64B-v2, source identity
  `bb972afe2022977f4b7290d11e28e081f869c7cc7b62b378e28bac17bf76dc4f`,
  payload SHA-256
  `2c8eee36768f0a42063949298eca8351bacb74838165a8188520a4b180d63c6d`,
  30 draws, eight texture-command credits, and 30 Gouraud credits.
- One canonical `ACTOR_DEPENDENCIES` record owns the S64F dependency. The real
  generic bundle is v2-only; the historical mixed-v1/v2 fixture remains a
  separate parser/format proof rather than pretending Mario belongs to this
  generic dependency.

## Resource proof

| Resource | Actor share/cap | Maximum supported per instance | Witness | Conservative floor |
| --- | ---: | ---: | --- | ---: |
| Observer/live count | 64 | 1 | every bank | 64 |
| Dedicated actor output records | 2,718 | 46 | family 24 / model `0x007f` | 59 |
| Post-Mario texture-command credits | 1,351 | 24 | family 40 / model `0x0095` | 56 |
| Post-Mario Gouraud credits | 892 | 46 | family 24 / model `0x007f` | 19 |

Every supported bank has a positive individual margin in all applicable
shares. Explicit zero-cost handling maps a zero maximum cost to the 64-observer
cap. The guaranteed-any-mix count is therefore exactly `min(64, 59, 56, 19) =
19`. Generic actors join Mario as essential work; Task 9 still owns checked
runtime dry-summing of the complete observed set and all-or-nothing
publication, while optional terrain receives only the remainder.

The 5,288 preserved source ceilings produce the exact unconstrained
85,512-output / 65,788-command / 29,352-Gouraud envelope. This is named and
reported only as a non-acceptance diagnostic because recurrent families share
source pools and those ceilings are not a simultaneous scene allocation.

Actor texture and CLUT requirements remain separately reported at 16,640 and
2,816 bytes. The report preserves the complete ordered equation: the 446,432-
byte post-command/Gouraud region minus 333,696 terrain texture, 25,600 Mario
texture, and 34,464 terrain CLUT bytes leaves Yaul's existing 52,672-byte
remaining region. Actors consume 19,456 bytes of that region, leaving a
positive 33,216-byte future repartition margin.
Task 7 owns that repartition; Task 5 does not claim the current binders can
address the remaining region. The provisional S64P is 2,340,436 bytes; with
the 160,928-byte actor dependency, the 32-Mbit cart total is 2,501,364 and the
margin is 1,692,940 bytes.

## Publication and fail-closed evidence

- Compilation occurs in a private sibling staging directory. Payload,
  dependency, and capacity header publish before the JSON report by exclusive
  hard links. Existing outputs reject before compilation, and injected
  mid-publication failure removes every newly published path.
- Relocated source trees produce byte-identical S64F, dependency, header, and
  report outputs; none contains a host path.
- Named pre-publication failures cover one-byte/one-credit reductions for
  texture, CLUT, cart, workspace, output, command, and Gouraud capacity.
- Package ownership is exactly the ten canonical classes: route, input,
  camera, cart, level, shared-data, actor, animation, audio, and texture.
- Family input is not trusted: the owning family compiler deterministically
  recomputes the closure-derived report, exact-compares every semantic field
  except the generated payload pathname, and downstream compilation consumes
  only the recomputed values. Mutated ceiling, generation, capability, source
  hash/count, and absolute/escaping/case-colliding source paths all reject.
- Package bytes are derived through canonical `target_profile` containment,
  collision, descriptor, payload, and exact one-per-class ownership machinery.
  Make's read-only dependency discovery uses the same validator.
- Make tracks closure, family report, provisional S64P report, model IDs,
  owning host tools, profile, all descriptors, and all descriptor payloads as
  real prerequisites. Repeating unchanged generation 6 is verify-only; a
  changed input for an already-published generation invokes the publisher and
  fails closed at no-clobber. The always-run verifier requires all four
  sidecars and compares them to a private current-input rebuild.

## Provenance

No external source was copied. The orchestration is a same-project close-port
and direct extension of the GPL-2.0-only host formats at pinned owning commits:
`actor_family_bundle.py` (`68ceec9c`), `actor_variant_bank.py` (`86de51cc`),
`compile_actor_bank.py` (`83cfc1ad`), `compile_scene_package.py` (`a3e22842`),
and `target_profile.py` (`b345dd15`). Exact paths, commits, license, and reuse
mode are serialized in the build report.

## Verification

- Prescribed RED: both new suites failed only because
  `compile_actor_family_bundle` and `inventory_actor_family_bundles` did not
  exist.
- Focused GREEN: eight tests pass, covering real BOB, relocation,
  no-clobber/report-last rollback, exact inventory, individual margins,
  zero-cost floor handling, and all seven injected budget failures.
- The combined Make build regenerated closure and the provisional S64P and ran
  the existing scene-package suites: 12 plus three tests pass.
- Historical/parser/closure GREEN is 87 tests. Mixed-S64F C validation passes
  54 mutations, the same C parser accepts the real 47-family/14-variant BOB
  artifact, S64B-v2 C validation passes 86 mutations, and articulated
  capability validation passes. `compileall`, scoped diff, and artifact hash
  checks pass.
- The opaque capability fixture first RED-exited at its stale pre-Task-4
  `56e9a35c...` trust anchor. Two fresh family-bank regenerations were identical
  at header-content SHA `60c329ab3e8bcd8bc7d13869c123706af5517f5bd7efd3b3a539b70212e28b73`
  and payload SHA `db611af699337f38a2284abf58cb287c70f9ab6e5df5b07555cda48aba7bf313`.
  A narrow test-only reseal makes opaque/surface/collectible validation GREEN;
  production parser/runtime code and the separate effect oracle are unchanged.
- The complete prescribed Make wave passes at fresh package generation 5. Its
  160,928-byte S64F SHA is
  `d83789cbf6c33dc287d6c8326b220a6a6e482744e6ca2bb99063921a557b3012`
  (generation is serialized), and its capacity header SHA is
  `c0faab4ae9b0d0141474ac4ce716cd830ab703d5df6178cd4ca9878550459b94`.
  Independent review found C0/I3/M0; fix round 1 is source-complete and scoped
  rereview is still required before Task 5 is complete. Generation-9 build,
  unchanged-repeat verification, each-sidecar missing/corrupt mutations, and
  11 focused tests plus the direct target-profile dependency API test pass.
  The complete C/Make wave passes (54 mixed-S64F mutations, real 47/14 S64F,
  86 S64B-v2 mutations, both capability gates); broader historical/parser/
  closure/profile coverage is 112/112. Generation-9 S64F SHA-256 is
  `ee786f5c907ac7f88dbec007170fe1bd6db778f39df81e7d71e8813f9b602b56`;
  compileall, scoped diff, and sidecar hash checks pass.
