# Dual-SH2 work-storage ownership evidence — 2026-08-06

## Scope

This report records a permanent RAM-ownership correction for the Saturn sourceboot
renderer. It does not claim a manual launch, texture/audio closure, target/P2
proof, or FPS improvement. A single-SH2 build is not an accepted production
configuration.

## Source contract

- `tools/saturn/test_dual_sh2_work_storage_contract.py`: 2/2 after the RED/GREEN
  transition.
- CPU-only renderer traversal/classification/merge arrays use `.lwram_bss` through
  `DEMO_CPU_WORK_CACHE`.
- Scene admission uses the explicit
  `sm64_saturn_scene_admit_with_scratch()` API. The sourceboot caller supplies the
  phase-owned terrain master-command bank; the host compatibility wrapper is not
  available to silently allocate a sourceboot arena.
- CD source-cart staging and the file-list table borrow the first
  `SOURCE_CART_STAGE_BYTES` bytes of `sourceboot_main_pool` while the pool is not
  initialized. `main_pool_init()` then resets the complete pool. The null check is
  performed before deriving either borrowed pointer.
- VDP1 command banks, Gouraud data, and SCU-visible/uncached transport storage are
  unchanged and retain their HWRAM ownership.

## Build

Command (from the sh2-native-math-purge worktree):

```text
C:\msys64\usr\bin\bash.exe .tmp-current-link-demo-stage4.sh
```

The script sources `.yaul.env`, preflights the MSYS DLL set, and invokes the
serialized route with:

```text
SATURN_DEMO_PATH=1
SATURN_DEMO_VIEW_RADIUS=6000
SATURN_SLAVE_RENDER=1
SATURN_DEMO_POLY_TIER=0
SATURN_DEMO_HOT_PROMOTION=0
SATURN_DEMO_NEAR_CLIP=0
SATURN_DEMO_BSP_ORDER=1
SATURN_DEMO_BSP_FRAGMENTS=0
SATURN_RENDERER_PIPELINE=4
SATURN_SOURCE_CART_STAGE_SECTORS=4
```

Result: exit code 0, identity `e2-bob-identity-id-bd57c0a81635606c`.
The linked ELF SHA-256 is
`eb076d2785ebcea936871ef22351955313de2307c21dcc4deeb013b172b6f0c3`.

Artifacts:

- ELF: `build/saturn/sourceboot/e2-bob-identity-id-bd57c0a81635606c/obj/sm64-saturn-sourceboot-e2.elf`
- CUE: `build/saturn/sourceboot/e2-bob-identity-id-bd57c0a81635606c/sm64-saturn-sourceboot-e2.cue`
- map: `build/saturn/sourceboot/e2-bob-identity-id-bd57c0a81635606c/obj/sm64-saturn-sourceboot-e2.map`

Map/link evidence:

- HWRAM `___end = 0x060FE438`; margin to `0x06100000` is `0x1BC8` (required
  floor `0x1B00`).
- `.lwram_bss` ends at `0x002EB878`; the fixed `.lwram_actor_runtime` owner ends
  at `0x002FB880`, so the full-section margin to `0x00300000` is `0x4780`
  (required floor `0x4000`).
- `nm` shows `_sourceboot_main_pool` at `0x00280A30`, size `0x60000`.
- `nm` shows no `s_source_cart_stage`; there is no permanent HWRAM CD-stage owner.
- The link command contains `-DSATURN_SLAVE_RENDER=1`.

## Verifier result

`verify_sourceboot_memory_map.validate_layout()` now binds the short
identity-directory path to `generated/saturn_build_identity_spec.json`, accepts
the exported HWRAM `sourceboot_vdp1_cmdts` owner, and validates this exact ELF.
The command returns HWRAM margin `0x1BC8` and full-section LWRAM margin `0x4780`.

## Remaining gates

Still open: P2/cache and real concurrent-SH2 target evidence, repository-profile
Ymir/manual inspection with DRAM/CUE, texture and sound closure, production actor
cutover, and measured performance/FPS. Do not substitute a single-SH2 image for
any of these gates.
