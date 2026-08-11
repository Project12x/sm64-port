# Hermetic sourceboot release evidence — 2026-08-11

## Status

Task 9 repair round 2 is source-complete at candidate source
df7894acd9cbce03eb099df68f8bc17ea71735b6, with the v4 pin committed in
537dd7210c2846e283c21a12a32770d601218c0c. Both controller-owned
same-reviewer rereviews remain open, so Task 9 is not complete. Task 10 is the
next acceptance lane and remains paused; no Ymir, 20,100-frame smoke, visual,
desktop-launch, or owner manual-play gate ran.

## Reproducible release identity

- Candidate A and B source: df7894acd9cbce03eb099df68f8bc17ea71735b6.
- Identity version/tag: 2 / id-a40f992c085da2f0.
- Identity SHA-256: 67c826f42010694fb317b1a3c47bb9e7826911f6be490207357f4081cd4fc8e2.
- Effective-config SHA-256: a40f992c085da2f04ccf47d9a00754b1831baba95060418d0900cc5368e54cf4.
- Source-closure SHA-256: 4f6a2febfc7c446424b9be79569a6937490804d5857b148d6ebce075bb0c4da3.
- Target-profile SHA-256: fe090885efa5245d08a5d09a4e03ba923b5b61d083b0c4763dc4745c9e211dd2.
- Package-set SHA-256: 85a5a1903c0ab04fab3f1fa2009e36e84a7537f2a975fea996cb9b20b0f4c266.
- Toolchain-attestation SHA-256: e74c5bad3adcf9f5207c99776ce13292ed3c4a97889aee0f315a4794bfda48db.
- Release-manifest SHA-256: 9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99.

The two detached, owned candidates used independent copies of the same
prerequisites: baserom SHA-256
17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91
and a 1,977-file, 42,663,369-byte build/us_pc inventory with SHA-256
ce37312358ecfd1fd537b36442b8905343e5f9e82f50e8839b7656e9d05ae7e7.
Both prerequisite trees were non-reparse. Exact release builds used the plan's
full profile/flag tuple and -j1 and exited 0 in 924.0 and 937.2 seconds.
Independent verification passed for both. Comparison reports identical:true
with no differing fields; its canonical report SHA-256 is
8edf96639c5c8244bab7bb171c9bf488b7b0df878417c0e24025cd7f0264eabc.

The toolchain seal includes the directly invoked binutils backends:
sh-elf-ar.exe SHA-256
6ea97810d5e686c029d5c279b437b6f08d2d729fa3220a2dd4a798739771024a
and sh-elf-nm.exe SHA-256
d6ed58af94b350368ce4e064ce5fd63ade56839300d9ad5d6188587e11dd7bb2.
The delegating gcc-ar/gcc-nm wrappers are not invoked by this boundary.

## Artifact and audit facts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| ELF | 9,445,220 | dcf4123f66ffff5e5d4efd4ac2cd5efdc4298a2c8c62c2a73af69ce936013010 |
| SOURCE.DAT | 3,565,696 | f0d3781cfce7ea1f58813de9b7b9d6e2076438fe772d4bcf2868b9918f0fde6c |
| ISO | 4,968,448 | af194c7574ec3934e1a9c319ccbf15fd0a184e49d878199cb44b99e801dce398 |
| CUE | 88 | cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7 |

Unsealed measurement against candidate B passed at root
_game_loop_one_iteration with total 700 and neither _atan2_lookup nor _atan2s.
The canonical measurement report SHA-256 is
148bec4b3616c190f5b384e85fbb8a7ade1cad0551d2f7d492cb0e5cdf15bcf1.
Only after that result was the 606-byte v4 contract generated and pinned at
SHA-256 14db6bfb5ab01239dd63aa4e11f767245c1f026f25836da73cf26fbc0d977b8b.
The exact v4 run exited 0 in 383.3 seconds and published status passed, total
700, and both forbidden callers verified absent. Acceptance-report SHA-256 is
2e37d74b806c44f65594a73018312ffde338ceb0b4887b0e65667a73a7efc286.
Historical v2/v3 contract hashes remain
87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2 and
80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5.

## Capacity and package facts

- ___end is 0x060fca38: 13,768 bytes of physical HWRAM margin and 6,856
  bytes after the 0x1B00 reserve.
- Cart span 0x22400000..0x22766880 is 3,565,696 bytes and leaves 628,608
  bytes under the 32-Mbit limit.
- obj/SOURCE.DAT, cd/SOURCE.DAT, and xorriso-extracted /SOURCE.DAT are
  byte-identical at the hash above. The ISO lists it at LBA 534 for 1,742
  blocks.
- Object-pool capacity is sealed at 208. The prior artifact-bound idle-boot
  probe peaked at 138 with zero failures, leaving 70 slots. This is only a
  floor: pickup/hold and action-particle pressure remain uncovered, and Task 9
  did not run a new route or occupancy capture.
- Package root 85a5a190...c266 contains exactly actor, animation, audio,
  camera, cart, input, level, route, shared-data, and texture. This is a BOB
  package set, not a complete-game inventory.

## Guarded staging

The prior five-file manifest-5e04 stage was verified as an owned non-reparse
directory and moved recoverably to
sourceboot-bob-demo-v2-manual-candidate.superseded-5e04e252. Candidate B was
then staged into the canonical manual-candidate directory. The result contains
exactly five files and 17,982,718 bytes and directly verifies to manifest
9110b40d...b99. A second stage exited 1 because the destination was nonempty;
the before/after inventory SHA-256 remained
910ac359f8f54e41ecac25a9eb6c1ad30b2715706b9c571b2042557360992004 and
the manifest reverified.

## Verification and limits

Fresh post-pin host verification passed: extractor 7/7 (one link-capability
skip), filtered checkout 1/1, attestation 17/17, hermetic Make 19/19, identity
bootstrap 15/15, closure 27/27 (one capability skip), release manifest 28/28,
staging 20/20, and v4 sealer 12/12. The full native-math suite ran 244 tests
with exactly the approved pre-existing null-camera-trigger proof failure
(243/244) and no new failure.

The integrated staged artifact is a BOB demo candidate. The same
profile-neutral identity, closure, audit, manifest, capture binding, and
staging architecture is intended for the total game, but the
sm64-saturn-full profile remains non-releasable until its complete content and
system inventory plus game-wide target evidence are implemented and reviewed.
