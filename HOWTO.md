# How to

Read [the product goal](docs/saturn/PRODUCT_GOAL.md) and [current
state](STATE.md) before changing Saturn behavior. Use
[`docs/saturn/BUILDING.md`](docs/saturn/BUILDING.md) for toolchain details.

## Before a build

1. Record `git rev-parse HEAD` and `git status --short`.
2. Name the one visible or audible hypothesis being tested.
3. Record the exact profile path and hash.
4. Confirm the previous accepted artifact remains untouched.

The immutable historical A9A rollback is
`build/saturn/baselines/a9a-2026-08-05/`. Its CUE SHA-256 is
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`.
Never regenerate or overwrite it.

## Build and launch discipline

- Use the repository MSYS wrapper and serial `-j1` make route documented in the
  build guide. Do not invoke a different Make, Python, or profile implicitly.
- After building, record the CUE/ELF/profile hashes and filesystem build time.
- Before opening Ymir, print those values and verify the launched path is the
  new artifact—not a baseline, superseded release, or another worktree.
- Let gameplay visibly render before capturing or making a visual claim.
- Capture audio output or a waveform in addition to SCSP command/heartbeat
  telemetry. A started MC68000 is not audible success.
- Record presentation cadence, exception/allocation/generation failures, and
  the manual verdict for the same CUE.

## Keep or revert

Keep a behavior change only when the new CUE improves its stated hypothesis
and preserves boot, Mario, controls, camera, collision, accepted actors, audio,
and the current FPS floor. Otherwise revert or bypass it before another causal
change. Two failed implementation attempts or two hours without a new live
observation is a mandatory stop; do not open another architecture or format
task.
