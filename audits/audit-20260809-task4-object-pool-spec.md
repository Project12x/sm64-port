# Task 4 Specification Audit — 2026-08-09

## Context

Task 4 applies the G1-approved 208-slot Saturn object-pool capacity, makes
that compile-time value part of the sealed sourceboot identity, and binds the
occupancy-capture report to the matching ELF. The review scope is the
uncommitted Task 4 diff after base `16ff8b4b`, its target-build evidence, and
the active memory-residency plan.

## Findings

### Critical

None.

### Important

None.

### Minor

None outstanding. During review, two target-header comments still described
the portable 240-slot fallback as applying to every configuration. They were
corrected to describe the sealed override and the intentionally fixed
240-entry actor-observation storage ceiling. No behavior changed.

## Discrepancies between summary and code

None. The implementation matches the stated 208-slot decision and the
evidence:

- A nonempty `SATURN_OBJECT_POOL_CAPACITY` adds the sourceboot-only compiler
  definition; an unset value adds no definition and retains the 240 fallback.
- `object_pool_capacity` is required, typed as an integer from 1 through 240,
  and participates in the effective configuration identity.
- The capture tool rebuilds the sealed identity from the supplied spec and
  requires its raw tuple to occur in the chosen ELF before reporting the
  capacity.
- The target map reduction is exactly `32 * 608 = 19,456` B and the
  20,100-frame post-BIOS capture reports peak 138 with zero failures.

The actor-observation header deliberately retains a fixed 240-entry backing
array. It is an upper-bound observation domain, not the `gObjectPool` object
array whose residency changed; the implementation still restricts observed
slots to `OBJECT_POOL_CAPACITY`.

## What was done well

- The override has a real C-preprocessor contract, rather than a source-text
  assertion that could overlook a misspelled or inactive macro.
- The artifact binding closes the Task 2 review gap: an overridden ELF can no
  longer be labelled with the header's default capacity merely because the
  header is readable on the host.
- The 240 passthrough proof compares full target ELF hashes after forcing the
  capacity-sensitive object to recompile, which is stronger than comparing a
  map symbol or an identity label alone.
- The evidence explicitly preserves the idle-only coverage limitation; it
  does not promote the observed 138 peak to a general occupancy ceiling.

## Recommended next actions

1. Commit the reviewed Task 4 change and its evidence.
2. Run Task 5's goal configuration and combined smoke. That gate must still
   exercise and report pickup/hold and action-particle pressure before any
   claim that 208 is sufficient beyond idle boot.

## Verdict

**PASS.** Task 4 meets its written acceptance criteria. Task 5 remains the
next required gate; this review does not substitute the idle-boot run for the
broader interaction coverage.
