# Task 9 execution report — authoritative repair-round-2 closeout

Date: 2026-08-11

## Status and scope

Task 9 is source-complete pending the two controller-owned same-reviewer
rereviews. Candidate source is
df7894acd9cbce03eb099df68f8bc17ea71735b6 and the one-shot v4 pin is commit
537dd7210c2846e283c21a12a32770d601218c0c. Authoritative release evidence and
public status are commit 6685084aa258238a312dd6228f649e4b30328792. Task 10
is the next acceptance lane and remains paused. This task did not run Ymir,
the 20,100-frame smoke, visual capture, desktop launch, or owner manual play.

All release, audit, capacity, package, and stage evidence from earlier Task 9
rounds is superseded. The only authoritative candidate identity is
id-a40f992c085da2f0 and the only authoritative release-manifest SHA-256 is
9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99.

## Review finding and repair

The final rereview found a TOCTOU boundary in extracted-asset cleanup:
_clean_asset_path validated a mutable ancestor chain, returned, and later
Path.unlink/Path.rmdir resolved the path again. A deterministic junction swap
after validation redirected the old deletion to an outside file.

Reference-first inspection covered the active plan's Task 7 repair rounds 2
and 3, the full in-tree DirectoryNamespaceGuard implementation in
tools/saturn/release_manifest.py, and the exact-opened-object patterns in
tools/saturn/path_identity.py. Reuse mode is same-repository close-port:

- cleanup holds Task 7's full ancestor namespace across validation and
  mutation;
- POSIX deletes relative to the held directory descriptor;
- Windows opens the child without following reparse points, verifies exact
  identity/type/non-reparse state, and applies delete disposition to that
  exact handle;
- unavailable directory-relative capability fails closed; and
- batch cleanup removes files before pruning deepest-first, never removing the
  output root or any ancestor.

Behavior and required CHANGELOG/status updates are commit
df7894acd9cbce03eb099df68f8bc17ea71735b6
(fix(saturn): pin asset cleanup namespaces).

Focused TDD RED ran seven extractor tests with exactly two intended failures:
the deterministic ancestor replacement deleted the outside file, and missing
namespace capability still allowed deletion. Authoritative Windows GREEN is
7/7 with one link-capability skip. The test proves traversal, absolute, NUL,
symlink/reparse, and swapped-ancestor cases fail closed; the outside file,
displaced generated file, output parent, and output root survive.

## Host verification

Fresh post-pin commands and results:

- python tools/saturn/test_extract_assets_output_root.py — 7/7, one
  link-capability skip.
- python tools/saturn/test_audit_checkout_identity.py — 1/1.
- python tools/saturn/test_gen_toolchain_attestation.py — 17/17.
- python tools/saturn/test_sourceboot_hermetic_build_make.py — 19/19.
- python tools/saturn/test_sourceboot_identity_spec_bootstrap.py — 15/15.
- python tools/saturn/test_gen_source_closure.py — 27/27, one capability skip.
- python tools/saturn/test_release_manifest.py — 28/28.
- python tools/saturn/test_stage_saturn_release.py — 20/20.
- python tools/saturn/test_seal_sh2_native_math_audit_v4.py — 12/12.
- python tools/saturn/test_verify_sh2_native_math.py — 244 tests, exactly the
  approved pre-existing
  test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites
  failure; 243/244 and no new failure.

Release-manifest and staging tests used the established unsandboxed Windows
environment required for ancestor handle access. Python compilation, canonical
JSON parsing, direct manifest verification, git show --check, and scoped
diff checks are closeout gates, not substitutes for the target runs below.

## Reproducible candidates

Candidates:

- A: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/hermetic-release-repro-a
- B: D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/hermetic-release-repro-b

Both were detached and tracked-clean at exact source
df7894acd9cbce03eb099df68f8bc17ea71735b6. Their independently copied,
non-reparse prerequisites matched:

- baserom.us.z64 SHA-256
  17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91;
- build/us_pc: 1,977 files, 42,663,369 bytes, inventory SHA-256
  ce37312358ecfd1fd537b36442b8905343e5f9e82f50e8839b7656e9d05ae7e7.

Only each verified owned build/saturn tree was reset. Both builds used MSYS
Make 4.4.1, -f Makefile.saturn.mk, -j1, verify-sourceboot, the
sourceboot-bob-demo-v1 profile, release mode, object capacity 208, and the
active plan's exact accepted feature/runtime flag tuple. A exited 0 in 924.0
seconds and B exited 0 in 937.2 seconds.

Direct release-manifest verification passed for A and B. Both published:

- tag id-a40f992c085da2f0;
- manifest 9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99;
- identity 67c826f42010694fb317b1a3c47bb9e7826911f6be490207357f4081cd4fc8e2;
- effective config a40f992c085da2f04ccf47d9a00754b1831baba95060418d0900cc5368e54cf4;
- closure 4f6a2febfc7c446424b9be79569a6937490804d5857b148d6ebce075bb0c4da3;
- profile fe090885efa5245d08a5d09a4e03ba923b5b61d083b0c4763dc4745c9e211dd2;
- package set 85a5a1903c0ab04fab3f1fa2009e36e84a7537f2a975fea996cb9b20b0f4c266;
- toolchain e74c5bad3adcf9f5207c99776ce13292ed3c4a97889aee0f315a4794bfda48db;
- ELF dcf4123f66ffff5e5d4efd4ac2cd5efdc4298a2c8c62c2a73af69ce936013010;
- provenance df7894acd9cbce03eb099df68f8bc17ea71735b6 with closure_clean:true.

The comparison command passed identical:true with no differing fields.
Comparison report SHA-256 is
8edf96639c5c8244bab7bb171c9bf488b7b0df878417c0e24025cd7f0264eabc.
Attestation binds direct sh-elf-ar.exe
6ea97810d5e686c029d5c279b437b6f08d2d729fa3220a2dd4a798739771024a
and sh-elf-nm.exe
d6ed58af94b350368ce4e064ce5fd63ade56839300d9ad5d6188587e11dd7bb2.

## Measurement, seal, and exact v4

Unsealed measurement used candidate B's exact ELF and release manifest plus
the pinned baseline, route oracle, audit-route oracle, and exact attested
objdump/readelf/addr2line paths. It published only after passing:

- schema/status measured-unsealed;
- root _game_loop_one_iteration;
- total 700;
- _atan2_lookup and _atan2s absent;
- manifest 9110b40d...b99 and ELF dcf4123f...3010.

Measurement-report SHA-256 is
148bec4b3616c190f5b384e85fbb8a7ade1cad0551d2f7d492cb0e5cdf15bcf1.

The prior contract destination was removed before the one-shot sealer ran.
The resulting canonical contract is 606 bytes and SHA-256
14db6bfb5ab01239dd63aa4e11f767245c1f026f25836da73cf26fbc0d977b8b.
TDD RED was the prior digest pin rejecting these new bytes. Focused integrity
GREEN is 1/1 and sealer GREEN is 12/12. The pin and required CHANGELOG update
are commit 537dd7210c2846e283c21a12a32770d601218c0c. Historical v2/v3 contract
hashes remain 87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2
and 80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5.

The exact v4 run against the same B ELF/manifest exited 0 in 383.3 seconds.
Its canonical result is status passed, total 700, and both forbidden callers
verified absent. Result-report SHA-256 is
2e37d74b806c44f65594a73018312ffde338ceb0b4887b0e65667a73a7efc286.

## Capacity, package, and staging

- ___end=0x060fca38; physical margin 13,768; usable margin after 0x1B00
  reserve 6,856.
- Cart span 0x22400000..0x22766880 is 3,565,696 bytes; headroom under
  32 Mbit is 628,608 bytes.
- ELF is 9,445,220 bytes at dcf4123f...3010; ISO is 4,968,448 bytes at
  af194c75...e398; CUE is 88 bytes at cdbf0bfa...dba7.
- Object, staged-CD, and xorriso-extracted ISO /SOURCE.DAT are byte-identical:
  3,565,696 bytes at f0d3781c...fde6c. ISO listing is LBA 534, 1,742 blocks.
- Package root 85a5a190...c266 contains exactly the ten BOB classes: actor,
  animation, audio, camera, cart, input, level, route, shared-data, texture.
  It is not a total-game inventory.
- Capacity is sealed at 208. Prior artifact-bound idle-boot evidence peaked at
  138 with zero failures, leaving 70 slots. The 138 is a floor: pickup/hold
  and action-particle pressure are not covered, and Task 9 ran no new occupancy
  route.

The prior manifest-5e04 stage was verified as owned/non-reparse and moved
recoverably to the .superseded-5e04e252 sibling. The canonical stage now holds
exactly five files/17,982,718 bytes and directly verifies to
9110b40d...b99. A repeated stage exited 1 on the nonempty destination; its
before/after inventory SHA-256 stayed
910ac359f8f54e41ecac25a9eb6c1ad30b2715706b9c571b2042557360992004 and
the manifest reverified.

## Closeout gates

The evidence files are:

- docs/saturn/evidence/reports/hermetic-sourceboot-reproducibility-2026-08-10.json
- docs/saturn/evidence/reports/sh2-native-math-goal-measurement-v4-2026-08-10.json
- docs/saturn/evidence/reports/sh2-native-math-goal-audit-v4-2026-08-10.json
- docs/saturn/evidence/reports/hermetic-sourceboot-release-2026-08-10.md

Still open:

- same-reviewer specification/evidence rereview;
- same-reviewer code-quality rereview;
- Task 10 exact staged 20,100-frame smoke;
- Task 10 visual, desktop-launch, and owner manual-play acceptance;
- total-game content/system inventory and game-wide target evidence;
- pickup/hold/action-particle occupancy coverage beyond the idle 138 floor.

No demo smoke/manual or total-game completion claim is made by Task 9.
