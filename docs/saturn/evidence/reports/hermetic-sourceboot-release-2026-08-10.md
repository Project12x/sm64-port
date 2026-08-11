# Hermetic sourceboot release evidence — 2026-08-10

## Status

Task 9 repair round 1 is `source-complete` pending both controller-owned
independent rereviews. Two clean, owned candidates reproduced, the repaired
tool topology and final provenance are sealed, exact audit v4 passed, capacity
and package facts passed, and the canonical manual candidate was restaged and
verified. Task 9 is not `complete`. Task 10's 20,100-frame smoke, visual
capture, desktop launch, and owner manual play have not run.

## Immutable candidate identity

- Candidate source commit: `04d6a2a3ff08577e64497132a4a77726fb33cc24`
- Identity version/tag: `2` / `id-264ab4203c268487`
- Identity SHA-256: `db522ebda6942e017f61316f478b4ac3d3513d9e402f2644db73439cac10c172`
- Effective-config SHA-256: `264ab4203c268487dabf3b573c48b80c5a434ad2c69fc3d486b735acf64eb2b2`
- Source-closure SHA-256: `3a5756b523ccdf5a055b630eb58b462de0f8fc92f4992f0f35257426b415fb8f`
- Target-profile SHA-256: `fe090885efa5245d08a5d09a4e03ba923b5b61d083b0c4763dc4745c9e211dd2`
- Package-set SHA-256: `85a5a1903c0ab04fab3f1fa2009e36e84a7537f2a975fea996cb9b20b0f4c266`
- Toolchain-attestation SHA-256: `e74c5bad3adcf9f5207c99776ce13292ed3c4a97889aee0f315a4794bfda48db`
- Release-manifest SHA-256: `5e04e2527e2373f30521bfcaaa63b56b061b74291cf1a4a3fd6e427aa311c1df`

Candidates A and B were detached owned worktrees at the same source commit,
with independent prerequisite copies. Each prerequisite inventory contains
1,977 `build/us_pc` files and 42,663,369 bytes, hashes to
`ce37312358ecfd1fd537b36442b8905343e5f9e82f50e8839b7656e9d05ae7e7`,
contains no reparse point, and uses baserom SHA-256
`17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`.
Their exact serial `-j1` release builds took 911.7 and 938.6 seconds. Direct
manifest verification passed for each; both manifests and every compared
field are identical. Reproducibility report SHA-256 is
`505004566b6dd842a43119427be9df53d2c2fe325c1c79e9616e4bb00cf11f8a`.

The toolchain attestation contains the directly invoked
`bin/sh-elf-ar.exe` (`6ea97810d5e686c029d5c279b437b6f08d2d729fa3220a2dd4a798739771024a`)
and `bin/sh-elf-nm.exe` (`d6ed58af94b350368ce4e064ce5fd63ade56839300d9ad5d6188587e11dd7bb2`).
No `gcc-ar` or `gcc-nm` wrapper row remains because sourceboot bypasses those
delegating wrappers and invokes the attested backends directly.

## Artifact and audit facts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| ELF | 9,445,220 | `21570fce4d68138e7d8d2089e0bb71d809ce964b0d59013dec792c2a7f923b6c` |
| `SOURCE.DAT` | 3,565,696 | `f0d3781cfce7ea1f58813de9b7b9d6e2076438fe772d4bcf2868b9918f0fde6c` |
| ISO | 4,968,448 | `93825c9c8ceb826f5c044a859364f9fa4104d2d9714b9cbe51243e915016635a` |
| CUE | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |

The linked cart span is `0x22400000..0x22766880`, exactly 3,565,696
bytes. `obj/SOURCE.DAT`, `cd/SOURCE.DAT`, and xorriso-extracted ISO member
`/SOURCE.DAT` are byte-identical at the hash above. The ISO directory lists it
at LBA 534 for 1,742 blocks. Against the 32-Mbit/4,194,304-byte cart limit,
payload headroom is 628,608 bytes.

Unsealed measurement against candidate B exited 0 in 330.3 seconds and
published report SHA-256
`d875fdc8b5d8fbcbbf4e5cddfda04d61c98f8c7d7f3c3ff6a1c2fb79e3d7680a`.
Only after it established root `_game_loop_one_iteration`, total 700, and both
forbidden callers absent was the canonical 606-byte v4 contract generated and
pinned. Contract SHA-256 is
`d52ecdb1d2a4f4847143af3d5e13629760ae8141f3fcecb413ad739b61ed7072`.
The exact v4 rerun exited 0 in 327.7 seconds and published acceptance report
SHA-256 `f4b90798bede749144ca92a6dba318e70dde09c90145423524b9abe93a7787aa`.
It binds the contract, manifest, ELF, identity, config, and profile hashes,
passes at total 700, and verifies `_atan2_lookup` and `_atan2s` absent.
Historical contracts remain unchanged: v2
`87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`
and v3 `80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`.

## Capacity and package facts

- `___end=0x060fca38`; physical HWRAM margin is 13,768 bytes and usable
  margin after `0x1B00` is 6,856 bytes.
- The object-pool capacity remains 208. Prior artifact-bound idle-boot evidence
  peaked at 138 with zero allocation failures, leaving 70 slots; that is a
  floor and does not cover pickup/hold or action-particle pressure.
- The package set contains exactly the ten BOB rows below. It is not a
  complete-game inventory.

| Class | Package | Manifest SHA-256 | Class root SHA-256 |
|---|---|---|---|
| actor | `mario` | `17f590d2a9596a555b327a7e917641b35949c63270aa825f5df3fb7272d22f65` | `a4e4355903cb44588fb151b74936beb7c3dfe4274601871dfff2b7dab188a390` |
| animation | `mario-source` | `9b727d6a21ac15199482459d555335891dcc92affacf1db2af77c7db398b3ff2` | `d367795485312e76f4eb4576003da82a11b5f2991e3b746260ae7b9c4a1caf13` |
| audio | `stub` | `c22479147ab8aab90a7c341b760bf288d59c128683a2cf73ef049344eda5bbab` | `5407070348b20600101913f8b50a6d36e56afc50e58995da15228ae09fd9603e` |
| camera | `sourceboot` | `e3a9f1487cf61abd2b4441e6ed61bbf583a8dd2f7e3bfeca27ca8fc39598c037` | `a8132fc385fea49a4fee506ee2a3ecfc3084d9d5061c0ca93669705eb84d7f46` |
| cart | `sourceboot-32mbit` | `4c60960f84eafbc285c52ccfcff2f80256124251562e309addc376fe7982389d` | `555f9a0ed2f5446aa8766da0e5e3664772571ad6bb14c2281a7e64abf787a3be` |
| input | `sourceboot` | `97e878957f61f49f3d540658e6ab1c1682ae6dd9eb493c78cd300f82f467a829` | `4cd1d3037de45760d33c73368952e107f71b46f31b6829f8fb629dc6c4381f47` |
| level | `bob-area1` | `38e803f0f0168e0cc379f69995d12691f2efaed29590f4054c3d0ed09d319596` | `0d6e2211cecc9c6501c71e30b83173e224f9905d534a30d460ce3b9d6d0b607c` |
| route | `bob-parity` | `e55cbde696e1e2216adac3382a0fde5774dd20405a1de53ad8ff65ab8999878a` | `c7729f75e5b9b2e81bb8864370d0a1c6ed5cb946bfc3f84dae513639afd10283` |
| shared-data | `bob-dependencies` | `90e02577c18c69449b03d9ce98bea0ee73436826a335ea81b66f1425ddc01f45` | `326fe783fb20b1fa503c7f9b410c75bed76d17628dee3811bafb453e8cc4fe54` |
| texture | `bob` | `89bc006d6ad2718c67bf47e5e14b72e63fac2d3ad9c2caf054e4ebbc61d16c44` | `2f8b159c6e31f51c81755b5a5d6052c85ec3917c58625802a9ffd986ecb94c2e` |

## Staging evidence

The previous invalid candidate directory was verified as an owned,
non-reparse generated directory containing five files and was moved
recoverably to `sourceboot-bob-demo-v2-manual-candidate.superseded-b75ba5f0`.
The repaired candidate was then staged to
`build/saturn/releases/sourceboot-bob-demo-v2-manual-candidate`.

Direct `release_manifest.py verify` returned manifest
`5e04e252...11c1df`. The staged inventory has exactly five files and
17,982,718 bytes: the manifest plus the four artifact rows above. A second
identical staging attempt exited 1 with `release destination is not empty`;
the canonical before/after file-size/hash inventory was unchanged, and direct
manifest verification again returned the same digest.

## Repair verification and open gates

Review-repair behavior commits are `ac037740` (cleanup containment),
`6e3291e9` (LF checkout identity), `3d37349b` (direct ar/nm topology),
`0959af40` (final provenance and one-index scaling), and `04d6a2a3` (isolated
gitlink fixture). Documentation reconciliation is `9d097b0b`; v4 pin/integrity
is `18bd3941`. Every behavior commit includes its `CHANGELOG.md` update.

Focused repair results before closeout were extractor 5/5 with one capability
skip, filtered-checkout 1/1, attestation 17/17, hermetic Make 19/19, bootstrap
15/15, closure 27/27 with one capability skip, release manifest 28/28,
staging 20/20, and v4 sealer 12/12. The full native-math verifier retained
exactly its approved baseline: 243/244, with only
`test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites`
failing. The release-manifest and staging suites required the established
unsandboxed Windows ancestor-handle environment; sandboxed attempts failed in
setup at `C:\Users` and did not exercise product behavior. Scoped syntax,
JSON, direct-manifest, whitespace, and commit checks complete the evidence
closeout.

Task 9 remains incomplete until independent evidence and code-quality rereviews
both clear. Task 10 is the next active acceptance lane, but remains paused; no
Ymir, 20,100-frame smoke, visual, desktop, or manual-play claim is made here.
The integrated BOB slice is not the total game. `sm64-saturn-full` remains
non-releasable until its full package inventory and game-wide target evidence
exist.
