# Task 9 implementation report — reproducible identity-v2 release

## Verdict

Task 9 is `source-complete` through Step 10. Two isolated, owned candidates
reproduced the same release manifest and output bytes; audit v4, capacity,
package, manifest verification, transactional staging, and overwrite refusal
passed. Step 11's controller-owned independent evidence and code-quality
reviews remain open. Task 10/Ymir/smoke/visual/manual execution did not run.

## Exact candidate boundary

- Common detached source: `44b786276f73c3dbd7dc91d9f39c332b2b51bf65`.
- Candidate A: `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/hermetic-release-repro-a`.
- Candidate B: `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/hermetic-release-repro-b`.
- Each candidate had an independently copied 1,977-file `build/us_pc` tree and
  `baserom.us.z64` SHA-256
  `17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
- Both used pinned clean libyaul commit
  `6012f79f...` and the attested Yaul toolchain under
  `D:/Code/RetroDev/sm64-saturn-port/work/yaul-install`.
- The implementation worktree's existing `build/us_pc` junction was never
  removed, replaced, or admitted to a candidate closure.

Both candidates ran `C:/msys64/usr/bin/make.exe -f Makefile.saturn.mk -j1
verify-sourceboot` through the repository's Windows/MSYS wrapper with
`SOURCEBOOT_TARGET_PROFILE=tools/saturn/profiles/sourceboot-bob-demo-v1.json`,
`SOURCEBOOT_RELEASE_MODE=release`, and this exact accepted tuple:

```text
SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1
SATURN_SOURCEBOOT_LIVE_INPUT=1 SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600
SATURN_SOURCEBOOT_LEVEL_ID=9 SATURN_SOURCEBOOT_AREA_ID=1
SATURN_SOURCEBOOT_ROUTE_ID=0 SATURN_SOURCEBOOT_CAMERA_ROUTE=0
SATURN_CAMERA_VARIANT=3 SATURN_CAMERA_IDLE_START_TICK=0
SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0
SATURN_CART_MBIT=32 SATURN_SOURCE_CART_STAGE_SECTORS=8
SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1
SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_POLY_TIER=2
SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_DEMO_FRAGMENT_MODE=0
SATURN_DEMO_BSP_FRAGMENT_FLAT=0 SATURN_RENDERER_PIPELINE=4
SATURN_SLAVE_RENDER=1 SATURN_ATAN2_VARIANT=2
SATURN_DEMO_VIEW_RADIUS=6000 SATURN_DIAGNOSTIC_MODE=0
SATURN_FAST3D_Q16_TRACE=0 SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0
SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1
SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 SATURN_FEATURE_SEMANTIC_AUDIO=0
SATURN_OBJECT_POOL_CAPACITY=208
```

The command also bound forward-slashed `SOURCEBOOT_PYTHON`,
`YAUL_INSTALL_ROOT`, SH-2/M68K prefixes, and scoped Git safe-directory entries.
No flag or `-j1` requirement was relaxed.

## Reproducibility and audit evidence

- Candidate A/B durations: 968.4/963.1 seconds; both exact commands exited 0.
- Manifest SHA-256:
  `b75ba5f073d8c7d03b64db47e6eed3e34d7fa8c70392ffe340a8ed208aca2ddf`.
- Comparison: `identical:true`, zero differing fields; report SHA-256
  `9c3b179f4ef699c69c4a0727d2046757a68824805a94f277bb63a2b604b9b231`.
- Identity tag `id-9a051d30880c78f0`; identity/config/closure/profile/package/toolchain
  hashes are recorded in `hermetic-sourceboot-release-2026-08-10.md`.
- ELF SHA-256 `f3e01ff2...81c1b`; `SOURCE.DAT` `f0d3781c...fde6c`;
  ISO `b0589b78...b989`; CUE `cdbf0bfa...dba7`.
- Unsealed measurement command was the plan's Step 5
  `verify_sh2_native_math.py` invocation with candidate B ELF/manifest,
  baseline, both route oracles, exact SH tools, and exclusive
  `--measure-audit-report`. It exited 0 in 273 seconds and published
  `ad79a992...fa8075`, status `measured-unsealed`, total 700, with both
  forbidden callers absent.
- Seal command was the plan's Step 6
  `seal_sh2_native_math_audit_v4.py --measurement ... --release-manifest ...
  --output tools/saturn/sh2_native_math_goal_audit_contract_v4.txt`.
  Contract bytes are exactly 606 bytes at
  `2c23ce448c6495e552461bf2e5b75e596d57a5a292a9c8bef8f71d1edf7f7265`.
- Exact-v4 command was the plan's Step 7 verifier invocation with the same
  candidate B ELF/manifest, baseline/oracles, contract, exact SH tools, and
  exclusive `--json-output`. It exited 0 in 274.0 seconds. Acceptance report
  `1fe9d585...f4ddbf` is `passed`, total 700, and binds contract, manifest,
  ELF, identity, config, and profile hashes.
- Historical contract digests remain v2 `87dabb51...6127e2` and v3
  `80f66286...9cba5`.

## Capacity, package, and staging evidence

- `___end=0x060fca38`; physical HWRAM margin 13,768 bytes; usable margin
  after `0x1B00` is 6,856 bytes.
- Cart span `0x22400000..0x22766880` is 3,565,696 bytes; 32-Mbit headroom is
  628,608 bytes.
- Object, CD, and ISO-extracted `/SOURCE.DAT` are byte-identical. ISO listing:
  LBA 534, 1,742 blocks.
- Package root `85a5a190...c266` contains exactly the required ten BOB classes:
  actor, animation, audio, camera, cart, input, level, route, shared-data, and
  texture. It is not a complete-game package inventory.
- Capacity is sealed at 208. Prior artifact-bound idle-boot evidence peaked at
  138 with zero failures, leaving 70 slots. That measurement is a floor and
  does not cover pickup/hold/action-particle pressure.
- Staging command:
  `.\.venv-saturn-tools\Scripts\python.exe tools\saturn\stage_saturn_release.py
  --manifest <candidate-B-manifest> --destination
  build\saturn\releases\sourceboot-bob-demo-v2-manual-candidate`.
  It published exactly five files/17,982,718 bytes. Direct
  `release_manifest.py verify --manifest <staged-manifest>` returned the same
  manifest digest. A second staging command exited 1 with `release destination
  is not empty`; inventory and hashes were unchanged and reverified.

## Corrections and discarded executions

Every failed run stopped before the affected gate and was not promoted:

- Qt GNU Make 4.2.1 was selected by the public spelling; the wrapper now binds
  compatible MSYS Make 4.4.1.
- MSYS shell escaping broke Python and Yaul Windows paths; explicit boundary
  paths now use forward slashes.
- dependency arguments exceeded the Windows command-line limit; strict sorted
  LF path-list transport replaced repeated argv.
- the implementation `build/us_pc` junction resolved outside the guarded
  root; candidates moved to two owned worktrees with independent prerequisites.
- required PNG assets were absent; a generic candidate-local baserom extraction
  prerequisite now derives them and keeps generated bytes in the closure.
- subsequent failures exposed generated-input classification, canonical profile
  bytes, tool-helper attestation, copied-asset ownership, bounded Git status,
  closure-owner schema, final provenance revalidation, and release-mode
  historical-audit sequencing defects; each received focused RED/GREEN tests
  and a separate behavior commit with CHANGELOG.
- first A/B comparison exposed absolute generated `.incbin` paths; commit
  `342c173d` made them repository-relative and both outputs were rebuilt empty.
- second comparison differed only in unstripped ELF debug paths from soft-fp;
  commit `44b78627` applied the canonical prefix maps, and both outputs were
  rebuilt empty for the final passing pair.
- first stage attempt found its required parent namespace absent and copied
  nothing. After verifying real owned parent directories, only the missing
  `build/saturn/releases` namespace was created and staging reran.
- xorriso's first Windows extraction spelling was rejected without output;
  the MSYS spelling succeeded and the temporary extracted file was removed
  after equality hashing.

Reference reuse throughout was same-repository pattern-only/close-port from the
named profile, identity, Make, manifest, audit, asset, and publication code in
the active plan. No external source bytes were copied and no license/NOTICE
obligation changed.

## Commits and open gates

Final full-verifier RED ran 244 tests with two failures: the documented
unrelated null-camera proof and a stale pre-pin v4 integrity test. Task 9
updated only the stale test so its negative case now rejects synthetic
noncanonical bytes against the real pin; no runtime, candidate, contract, or
release bytes changed. Focused correction/integrity is GREEN 3/3; the final
full suite is 243/244 with only the documented null-camera baseline failure.

Final adjacent verification also passed release staging 20/20, release
manifest 28/28, hermetic Make 18/18, and the focused v4/measurement slice 5/5.
All three evidence JSON files parse, relevant Python modules compile, direct
staged-manifest verification returns `b75ba5f0...a2ddf`, and scoped whitespace
checks pass. The initial sandboxed staging-suite attempt produced 16 Windows
ancestor-handle setup errors at `C:\Users`; the required unsandboxed rerun is
the authoritative 20/20 result.

Task 9 execution range begins at controller transition `8588d391`. The final
candidate behavior identity is `44b78627`; later evidence/contract work is
`c99563e5`, `283d701d`, `162945d6`, `438f926d`, `43c82145`, `29f5fd62`,
`16a31d54`, and `e02f785e`. The complete intervening source-fix list is
preserved by `git log --reverse 038391a4..HEAD` and the active plan/ledger.

The required legacy campaign/v3 plan files already contained user-owned
uncommitted historical blocked-reproduction hunks before closeout. Task 9 adds
self-contained supersession notes, but its commit must stage only those new
notes and leave the older hunks uncommitted.

Open gates are the two independent Task 9 reviews, then Task 10's exact staged
20,100-frame smoke, visual inspection, desktop launch, and owner manual play.
The `sm64-saturn-full` profile remains intentionally non-releasable until the
complete-game content/system inventory and game-wide target gates are finished.
