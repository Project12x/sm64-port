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

- Use the repository MSYS wrapper and the full 27-variable make route documented
  in the build guide. Do not invoke a different Make, Python, or profile
  implicitly.
- Parallel make is permitted; `-j1` is not required. T2.18 built the same tuple
  `-j12`, `-j12` and `-j1` into three fresh trees and got byte-identical ELF,
  ISO, linker map and all 280 objects, all sealing identity
  `id-0fade22f26a95c0c`, with `release_manifest.py compare` clean
  (`identical: true`, zero differing fields) on all three pairs. `-j1` 1039 s
  vs `-j12` 713 s / 708 s. See
  `docs/saturn/evidence/reports/sprint2-t2_18-parallel-build-identity.md`.
  Prefer a `-j` at or below the host core count; keep `-j1` when the machine is
  busy.
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
