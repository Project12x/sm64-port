# Hermetic sourceboot release evidence — 2026-08-10

## Status

Task 9 is source-complete: release construction, two-candidate reproducibility,
exact audit v4, memory margin, cart payload, package inventory, and
transactional staging pass. Independent evidence/code-quality reviews remain
open, so Task 9 is not yet complete. Task 10's 20,100-frame
smoke, visual capture, desktop launch, and owner manual play have not run.

## Immutable candidate identity

- Candidate source commit: `44b786276f73c3dbd7dc91d9f39c332b2b51bf65`
- Identity version/tag: `2` / `id-9a051d30880c78f0`
- Identity SHA-256: `f5fd613bb06036b57ec4da18bd88112cd413fbbb0e7e6a044f6d700ede048da9`
- Effective-config SHA-256: `9a051d30880c78f08394fc3d3b08c3daa9b5224957b8c455b0180108dde71c5b`
- Source-closure SHA-256: `8bc5261a132694c55f3c96edb33e9c4f349807ea6a6eefc5f5244ad6ea79853d`
- Target-profile SHA-256: `fe090885efa5245d08a5d09a4e03ba923b5b61d083b0c4763dc4745c9e211dd2`
- Package-set SHA-256: `85a5a1903c0ab04fab3f1fa2009e36e84a7537f2a975fea996cb9b20b0f4c266`
- Toolchain-attestation SHA-256: `e1360ab552a218f351965ca380afc494f96c87682f365536e4882aa56e60fa76`
- Release-manifest SHA-256: `b75ba5f073d8c7d03b64db47e6eed3e34d7fa8c70392ffe340a8ed208aca2ddf`

Candidates A and B were independent owned worktrees with independently copied
and inventoried ignored prerequisites. Their exact serial release builds took
968.4 and 963.1 seconds. Both manifests and every normalized input/output field
are identical; comparison report SHA-256 is
`9c3b179f4ef699c69c4a0727d2046757a68824805a94f277bb63a2b604b9b231`.

## Artifact and audit facts

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| ELF | 9,445,220 | `f3e01ff2ccd2dde46f30a47b2a7201e70ea429a4332701612de4aaef4a881c1b` |
| `SOURCE.DAT` | 3,565,696 | `f0d3781cfce7ea1f58813de9b7b9d6e2076438fe772d4bcf2868b9918f0fde6c` |
| ISO | 4,968,448 | `b0589b7860020701226bc9ae9df9a6c65945590bc14cde7197fc11708e76b989` |
| CUE | 88 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |

The linked cart span is `0x22400000..0x22766880`, exactly 3,565,696
bytes. `obj/SOURCE.DAT`, staged `cd/SOURCE.DAT`, and xorriso-extracted ISO
member `/SOURCE.DAT` are byte-identical at the hash above. The ISO directory
places it at LBA 534 for 1,742 blocks. Against the 32-Mbit/4,194,304-byte cart
limit, payload headroom is 628,608 bytes.

Audit-v4 contract SHA-256 is
`2c23ce448c6495e552461bf2e5b75e596d57a5a292a9c8bef8f71d1edf7f7265`.
The exact acceptance rerun took 274.0 seconds and passed with root
`_game_loop_one_iteration`, total 700, and both `_atan2_lookup` and `_atan2s`
verified absent. Acceptance-report SHA-256 is
`1fe9d585bf5385b33a038081ffe37b9cfc054f2ebcab174858f0e0b27ff4ddbf`.
The earlier measurement report remains explicitly `measured-unsealed` and is
not acceptance.

## Memory and object-pool capacity

- `___end`: `0x060fca38`
- Physical HWRAM margin to `0x06100000`: 13,768 bytes
- Reserved runtime allowance: `0x1B00` / 6,912 bytes
- Usable HWRAM margin after allowance: 6,856 bytes
- Sealed object-pool capacity: 208 slots
- Prior artifact-bound 20,100-post-BIOS-frame peak: 138 slots
- Prior allocation failures: 0
- Capacity margin over measured peak: 70 slots

Coverage remains limited: the 138-slot result is an idle-boot measurement and
is a floor, not a worst-case ceiling. It did not cover pickup/hold interaction
or action-particle pressure. This Task 9 target enables route replay and live
input in its identity, but Task 9 did not execute a new occupancy route or idle
capture; build/audit evidence is not substitute occupancy evidence. Task 10's
artifact-bound combined smoke remains required.

## Exact ten-class package inventory

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

This is the complete reviewed BOB demo package set, not the complete SM64 game
inventory. The `sm64-saturn-full` profile remains intentionally non-releasable
until its full content/system packages are populated and independently proved.

## Remaining gates

- Obtain both controller-owned Task 9 independent reviews; fix and rereview any
  finding before marking Task 9 complete.
- Run Task 10's exact staged-release smoke, visual, desktop, and owner manual
  gates. No Ymir or manual gate was run in Task 9.

## Transactional staging

The staged manual candidate is
`build/saturn/releases/sourceboot-bob-demo-v2-manual-candidate`. A clean
implementation worktree lacked the publisher's required existing parent
namespace, so the first invocation failed before copying. After verifying
`build` and `build/saturn` were owned real directories, execution created only
the missing real `build/saturn/releases` parent and retained an absent final
destination. This records the Task 7 publisher boundary: it atomically
publishes a missing destination inside an existing locked parent namespace.

Staging then copied exactly five files totaling 17,982,718 bytes and direct
manifest verification returned the reviewed SHA
`b75ba5f073d8c7d03b64db47e6eed3e34d7fa8c70392ffe340a8ed208aca2ddf`.
A second identical staging command exited 1 with `release destination is not
empty`; all five relative paths, sizes, and SHA-256 values remained unchanged,
and the staged manifest verified again.
