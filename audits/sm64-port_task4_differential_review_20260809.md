# Differential Review: Task 4 Object-Pool Capacity Cut

**Range:** `16ff8b4b..worktree`
**Date:** 2026-08-09
**Verdict:** APPROVE

## Executive summary

The change makes the owner-approved 208-slot capacity a sourceboot-only,
identity-sealed compile-time configuration. It reduces the actual object pool
by 19,456 B, binds the occupancy report to the matching compiled artifact,
and preserves the no-override 240-slot target binary. No security, functional,
or build-integrity defect was found in the reviewed diff.

| Severity | Count |
|---|---:|
| Critical | 0 |
| High | 0 |
| Medium | 0 |
| Low | 0 |

## What changed

- `OBJECT_POOL_CAPACITY` now accepts only the sourceboot compile definition
  `SATURN_OBJECT_POOL_CAPACITY_OVERRIDE`, retaining 240 as its portable
  fallback.
- The sourceboot Makefile derives an effective capacity, passes it to the
  compiler only when requested, and seals it in identity bootstrap and
  expectation checks.
- The identity generator validates `object_pool_capacity` as 1 through 240.
- The capture harness requires a supplied identity spec and the exact sealed
  tuple in the selected ELF before it returns that spec's capacity.
- Tests cover the macro expansion, identity mutation and validation, bootstrap
  resealing, hook placement, and rejection of a mismatched capture ELF.
- Evidence documents fresh target builds, map delta, full-ELF passthrough
  comparison, and the 208-slot idle capture.

## Critical findings

None.

The Make variable is developer-controlled build input. Its typed identity
bootstrap validation rejects values outside the permitted range before a valid
sealed sourceboot build can complete. It is not an untrusted runtime or
network-input boundary, so its use in the existing Make bootstrap command is
not an externally reachable injection finding.

## Test coverage analysis

The following checks passed after the review comment correction:

- `test_object_pool_probe_contract.py`: 12 tests, including real host
  preprocessor values 240 and 208 and named-function hook checks.
- `test_gen_build_identity.py`: 9 tests, including capacity hashing and bounds.
- `test_sourceboot_identity_spec_bootstrap.py`: 7 tests.
- `test_capture_object_pool_occupancy.py`: 2 tests, including stale-ELF
  rejection.
- `test_actor_snapshot_source.py`: passed, confirming the separate bounded
  actor-observation domain remains compatible.
- Focused `git diff --check`: passed.

Target evidence previously completed for this same reviewed tree:

- Flags-on 208 sourceboot `verify-sourceboot`: passed.
- Fresh map: `gObjectPool` is 126,464 B versus 145,920 B at 240, a 19,456 B
  reduction.
- 20,100 requested post-BIOS frames: peak 138 and zero allocation failures.
- Unset and explicit-240 rebuilt target ELFs share SHA-256
  `eb1fc628ef2cf523c59d3789645561d044463109cde41176f4348b52a7a582d7`.

The target occupancy test is correctly limited: it is idle boot, not evidence
for pickup/hold, macro-object-dense traversal, or action-particle bursts.

## Blast radius analysis

The macro affects six direct source consumers in five files: the `gObjectPool`
array and clear loop, free-list initialization, pointer-to-slot traversal, and
particle-pressure thresholds. This is a medium-sized compile-time blast
radius, mitigated by the fresh cross build and target remeasurement.

The separate actor-observation storage keeps a fixed 240-entry upper bound;
it does not allocate `struct Object` entries and does not change the measured
pool-residency delta. Its comments now distinguish that upper bound from the
effective object-pool capacity.

`collect_scene_closure.py` intentionally source-attests the literal 240
fallback for recurrent analysis. Sourceboot Task 4 does not invoke that
closure path, and 240 is a conservative static bound, so this is not a
regression. A future capacity-aware closure workflow should accept the sealed
effective value rather than infer the header fallback.

## Historical context

`git log -S "OBJECT_POOL_CAPACITY 240" -- src/game/object_list_processor.h`
identifies `89e86908` (`init2`) as the origin of the literal. No earlier
capacity-override implementation exists in this history. The change preserves
the original default and scopes the new behavior to sourceboot.

## Recommendations

1. Commit this review-cleared Task 4 change and evidence.
2. Do not advance 208 beyond its current idle-boot claim until Task 5 exercises
   the planned interaction and particle-pressure paths with the allocation
   failure latch checked.
3. When scene closure becomes a capacity-sensitive gate, feed it the sealed
   effective capacity instead of its intentional 240 fallback.

## Analysis methodology

Reviewed the uncommitted diff against `16ff8b4b`, inspected all direct macro
consumers and identity/capture call paths, checked the relevant git history,
examined the target evidence report and JSON-binding contract, and ran the
focused host contracts and diff-whitespace validation listed above.
