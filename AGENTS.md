# AGENTS.md — Saturn Product-Gate Constitution

## What this is

This is the durable Saturn product-gate constitution for the SM64 Saturn
port. It was referenced by
`docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md` (as "repository-root
`AGENTS.md` — durable Saturn product-gate constitution and two-attempt/
two-hour stop rule") but was missing from every tree. Restored 2026-08-14
from `HOWTO.md` and the handoff's "Operating contract for the next
maintainer" section. It binds all agents and maintainers working in this
repository.

## The product gate

An owner-observed CUE is the only milestone. `source-complete`, review
PASS, format validation, manifest equality, and clean host tests are not
milestones — they repeatedly became de facto milestones that demonstrated
nothing about the game. The next deliverable is not another report, review
PASS, source-complete task, format version, or infrastructure sprint. It is
a newly built CUE the owner can see and hear working.

## Before editing anything, write down

1. the owner-visible or audible defect;
2. the exact baseline and current ISO/ELF/identity;
3. one causal hypothesis;
4. the smallest files allowed to change;
5. the earliest Ymir observation; and
6. the stop time.

## The loop (follow exactly)

1. one focused regression test tied to the observed defect;
2. one bounded code change;
3. one unique build;
4. hash and name the build/profile before launch;
5. observe after the gameplay stage renders;
6. record image/audio/input/failure/FPS facts; and
7. keep or revert the change immediately.

## Two-attempt / two-hour stop rule

Stop after two implementation attempts or two hours without a new live
result. At that stop, the only allowed decisions are:

- revert;
- bypass; or
- a smaller donor transplant.

A new plan, abstraction, repair round, audit, or generalized format is
**not** an allowed response.

## Artifact identity

The CUE file is only 88 bytes and has the same SHA-256 in the A9A baseline
and many current builds, so a CUE hash alone does not identify the build.
Every launch must bind identity(-tag), ELF hash, ISO hash, CUE path, build
time, BIOS, cart, and profile before Ymir opens. Verify the launched path
is the new artifact — not a baseline, superseded release, or another
worktree.

## Testing budget before a live observation

Before the live observation, run only:

- one focused defect regression;
- target compile/link; and
- only the safety checks needed to avoid corrupting console state.

Broader suites and independent review occur after a useful live result and
before that result becomes the new accepted baseline. Do not spend another
night proving unused interfaces while the CUE is silent or visually wrong.

## Fail-open rules

- Audio failure must mute audio only; it may not stall simulation, blank
  the scene, or change Mario/terrain rendering.
- Unsupported content must skip at the smallest safe object — one bad
  actor never blanks a frame.

## Immutable baseline

`build/saturn/baselines/a9a-2026-08-05/` is the immutable historical A9A
rollback oracle. Do not rebuild, overwrite, or relabel it.

## Performance floor

The 4 FPS floor blocks any retained change; measure with
`capture_sourceboot_throughput.py`.
