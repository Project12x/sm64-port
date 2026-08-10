# Task 7 source-complete report

Date: 2026-08-10

## Status

Task 7 is `source-complete`. Behavior is committed as `7195fc48`
(`feat(saturn): seal and stage exact releases`), with source status in
`ec83cc4e` (`docs(saturn): record exact release source status`).
Controller-owned independent specification and code-quality reviews remain
open. No real SH-2 build, emulator capture, or target evidence was run or
claimed.

## Implementation

- Added canonical, host-root-neutral release-manifest write/verify/compare and
  profile-neutral verify-before-copy staging.
- Bound throughput, object-pool occupancy, HUD capture, and desktop launch to
  the exact verified manifest before emulator or SH-tool activity. Reports bind
  the manifest digest; v2 occupancy consumes manifest-owned effective config,
  and explicit v1 compatibility must reproduce the ELF identity bytes.
- Added Make sealing after post-link sealed-input verification and release
  verification to the outer sourceboot verification path.
- Updated BUILDING and CHANGELOG with the six-stage release/staging contract,
  prerequisites, failure policy, and compatibility behavior.

## Reference reuse

Reuse mode was in-tree close-port/pattern-only from the hermetic manifest,
identity, bootstrap, capture, and Make implementations at base `48f5a61a`.
Post-build ordering was checked against `yaul-org/libyaul` commit
`6012f79f237773378c8014e70d8998ad95a38d98`, MIT license, files
`libyaul/build/build.post.iso-cue.mk` and `build.post.bin.mk`. No external code
was copied.

## TDD and verification

- Initial release/staging RED: 12 discovered cases, two missing-module errors,
  ten expected skips.
- Capture/Make RED: throughput 2 failures; occupancy 1; HUD 1; desktop 1; Make
  2.
- Self-review RED/GREEN: malformed canonical closure rows now fail schema
  validation, and a v1 compatibility spec must equal the ELF identity bytes.
- Fresh post-commit exact suites: 9 + 4 + 40 + 10 + 3 + 7 + 7 = **80 tests**,
  zero failures/errors/skips.
- Fresh post-commit adjacent suites: 21 identity + 15 bootstrap + 16 boot trace
  + 1 route views = **53 tests**, zero failures/errors/skips.
- Six changed production scripts pass `py_compile`.
- The staged 18-path behavior diff passed `git diff --cached --check` before
  commit. `git show --check` passes for both commits;
  `git diff --check 48f5a61a..ec83cc4e` passes; that range contains exactly the
  18 Task 7 tracked paths; and the index is empty. All unrelated dirty and
  untracked paths remain preserved.

One initial adjacent command named two test files incorrectly and PowerShell
continued to later commands. That result was discarded; the totals above come
from a corrected fail-fast rerun.

## Design correction and open gates

The public `root` parameter is the repository root for Git provenance. Release
paths are relative to the CUE/manifest directory, matching Yaul's `obj/` ELF
and `SOURCE.DAT` layout without path escapes.

Open gates: both independent Task 7 reviews, real target build, reproducibility
comparison, audit v4, complete-package inventory, 20,100-frame smoke, visual,
and manual-play evidence. Host-only tests close none of these gates.

## Repair round 1

Status remains `source-complete`; behavior repair commit `bb5a840d`
(`fix(saturn): harden exact release snapshots`) addresses all five Important
findings from the first `Needs fixes` review. Controller-owned scoped rereview
remains open, so Task 7 is not yet `complete`.
Source status is committed as `eccb815b`
(`docs(saturn): record exact release snapshot repair`).

- Verification retains captured manifest bytes and a private four-output byte
  snapshot. CLI equality uses original manifest-resolved paths, while every
  emulator, SH-tool, capture, and stage read uses the snapshot. Desktop launch
  transfers cleanup ownership to a non-daemon watcher when Ymir remains alive
  after the bounded monitor, and reports snapshot root/PID/cleanup state.
- Staging is manifest-last and transactional for missing or preexisting-empty
  destinations. Copy/final-reverify failure rolls back every still-owned path;
  inode/device checks preserve foreign replacements and annotate the original
  exception when complete cleanup is unsafe. Retry succeeds after ordinary
  rollback.
- The writer maps the resolved profile's flat effective config to the canonical
  identity schema, binds all four output basenames, supports real historical
  v1 ELF/identity JSON when the derived config hash matches, and retains v2
  root semantics.
- Closure, package, toolchain, profile-output, and manifest-output schemas
  reject empty, exact duplicate, case-fold-colliding, noncanonical,
  Windows-reserved, escaping, symlink, and filesystem-alias inputs with
  forward-slash host-neutral path semantics. Snapshot copying rechecks source
  identities to close a post-preflight hardlink race.
- Comparison verifies both releases and compares canonical identity inputs plus
  output size/SHA-256, while ignoring output paths, manifest digests for the
  equality decision, host layout, provenance, mode, and reproducibility state.

Observed repair RED comprised 10 intended release failures/errors, 3 staging
failures/errors, and one each in throughput, occupancy, HUD, and desktop (17
total), followed by two focused self-review RED cases for cross-component
toolchain path collision and an injected hardlink alias race. GREEN exact
suites pass 24 + 9 + 41 + 12 + 4 + 9 + 7 = **106 host tests** with no
failures/errors/skips. Adjacent suites pass 21 identity + 15 bootstrap + 16
boot trace + 1 route views = **53 host tests**. All six changed production
scripts pass `py_compile`; the 15-path behavior index passed
`git diff --cached --check` before commit. The same 106 exact and 53 adjacent
tests plus `py_compile` passed again after `bb5a840d`.

No real SH-2 build, emulator capture, reproducibility run, audit-v4 seal,
20,100-frame smoke, visual inspection, or manual-play evidence was run or
claimed. Those gates, complete-package inventory, and controller rereview all
remain open.

## Repair round 2

Status remains `source-complete`. Behavior repair commit `00736856`
(`fix(saturn): close release namespace races`) addresses the namespace-race
portion left open by the round-1 rereview. This report, the ignored execution
ledger, and the active plan are updated in the follow-up source-status commit;
controller-owned independent rereview remains open.

- Manifest acquisition traverses and validates the full ancestor chain, pins
  the active Windows directory namespace, opens the leaf without following it
  where supported, and compares path/opened-object identity before and after
  reading. Canonical validation and hashing consume only those opened bytes.
- Staging creates a unique private sibling, writes outputs from immutable
  verified snapshots, writes the manifest last, verifies exact inventory, then
  atomically publishes without replacement and exactly verifies again. Missing
  and preexisting-empty destinations retain their required semantics.
- Rollback performs no path unlink or recursive delete. A proven Windows empty
  backup is deleted by its verified handle; any ambiguous private/published
  tree, replacement, or concurrent extra is atomically retained under a named
  sibling quarantine, included in exception diagnostics, and the requested
  missing/empty destination state is restored so retry succeeds. Desktop
  snapshot lifetime and every round-1-approved behavior remain unchanged.
- RED comprised seven intended focused failures/errors across 39 cases:
  manifest replacement, exact inventory, deep ancestor swap, Windows junction,
  contamination after initial emptiness, post-publication extra, and foreign
  owned-file replacement. GREEN focused release/stage is 26 + 14 = **40**.
- All seven exact suites pass 26 + 14 + 41 + 12 + 4 + 9 + 7 = **113 host
  tests**. Adjacent identity/bootstrap/boot-trace/route-view suites pass
  21 + 15 + 16 + 1 = **53 host tests**. All six production scripts pass
  `py_compile`; there are no failures, errors, or skips. One incorrect
  module-form adjacent invocation was discarded before the correct direct
  script rerun.
- The behavior index contained exactly five Task 7 paths and passed
  `git diff --cached --check`; `git show --check 00736856` passes. Unrelated
  dirty and untracked work remains preserved.

Reference reuse remains in-tree pattern-only/close-port from the Task 7 base
`48f5a61a` and the already-recorded pinned Yaul commit
`6012f79f237773378c8014e70d8998ad95a38d98` (MIT) for post-build ordering. No
external code was copied. The namespace guard is a clean-room platform
adaptation because the in-tree helpers do not provide the required
opened-object/Windows namespace boundary.

No real SH-2 build, emulator run, target capture, reproducibility comparison,
audit-v4 seal, complete-package inventory, 20,100-frame smoke, visual, or
manual-play evidence was run or claimed. All those gates and controller
rereview remain open.
