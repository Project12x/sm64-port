# Task 7 brief — master-owned actor texture residency

## Scope and base

- Reconciled base: `ce28a7d9e90e24302f808e1e28f086f3d02216b5`.
- Implement only master-owned all-resident actor texture/CLUT upload and scalar
  generation-last publication. Task 8 streaming/runtime integration, renderer
  activation, sourceboot cutover, Ymir, release, and reseal remain closed.
- Preserve every unrelated dirty/untracked path and stage only explicit Task 7
  paths.

## Required interfaces and ownership

Create `saturn_actor_texture_residency.h/.c` with capacity 128 and the approved
publication/activation/lookup API from the active plan. The publication owns
only fixed scalar mappings, byte counts, a generation, count, and committed
flag; no worker-visible pointer, VDP1 address, queue state, or allocator state
is permitted.

Scene residency owns the publication storage and exposes bounded staging and
active accessors. This task must not change existing begin/commit/unload
success criteria or activate a renderer. A staging accessor accepts only the
current nonzero staging generation; an active accessor accepts only the
current active generation and an exact committed actor publication generation.

## Authorized design corrections

1. The legacy residency implementation is in `saturn_ir_texture.c`, not a
   standalone `saturn_texture_residency.c`. Add that existing file to Task 7
   scope solely to implement `sm64_saturn_texture_residency_init_region` and
   make the old `init(partitions)` a thin delegate. Do not change Task 6 IR
   binders.
2. Pinned libyaul `scu_dma_transfer` and `scu_dma_transfer_wait` return `void`.
   Actor activation therefore uses the existing checked master-owned
   `saturn_dma_queue_submit(..., SATURN_DMA_QUEUE_SCU)` and
   `saturn_dma_queue_wait(sequence)` boundary. The queue is already initialized
   and owned at scene transition; activation must not initialize or edit it.
   Invalid sequence and false wait are activation failures. Prevalidate the
   complete plan before the first submit. If any post-submit failure occurs,
   bytes may be dirty but remain unreachable because no generation commits;
   there is no rollback or old-generation claim.
3. Task 6 originally owned the scalar mapping typedef in
   `saturn_actor_material.h`, whose IR/Yaul dependency is unsuitable for the
   public scene owner. Move the unchanged 16-byte scalar mapping ABI into the
   lightweight actor-residency header, have material include it, and
   forward-declare Yaul's exact `struct vdp1_vram_partitions` tag. The scene
   header must compile without Yaul; material behavior is unchanged.

## Exact behavior

- Invalidate any prior publication before evaluating an activation attempt so
  every failure leaves `committed == 0` and no lookup can reuse the prior
  generation.
- Require nonzero/new residency generation, `gameplay_suspended`, and
  `vdp1_idle`. Residency generation and immutable S64F package generation are
  deliberately distinct identities; Task 8 carries and reconciles both.
- Revalidate the complete bundle from its bytes and require the supplied view's
  scalar/hash/count fields to agree exactly with that canonical view. Resolve
  and revalidate every embedded bank and full source/payload identity before
  DMA. Select every S64B-v2 bank in canonical S64F variant order; v1 remains
  opaque and unmapped.
- The S64F validator's full-hash checks and explicit unique nonzero 32-bit bank
  ID rule are authoritative. Do not deduplicate at residency: every selected
  v2 variant produces exactly one canonical mapping, and a scalar collision
  fails S64F validation even when the full hashes differ.
- Plan texture offsets and CLUT indices independently with checked arithmetic,
  fixed capacity 128, 8-byte texture alignment, 32-byte CLUT alignment, exact
  region bounds, nonoverlapping/nonwrapping address spans, `uint32_t` texture
  offsets, and `uint16_t` CLUT indices. Prove every source and destination span
  through its inclusive last byte before DMA and form transfer pointers only
  after checked `uintptr_t` addition; a valid nonempty span ending exactly at
  `UINTPTR_MAX` is representable. The passed Yaul partitions must already
  represent the Task 7 repartitioned 16,640-byte texture and 2,816-byte CLUT
  additions; do not claim `remaining` is already part of either region.
- Use bounded revalidation passes instead of a heap or a large pointer-bearing
  plan array: validate/measure all banks, upload all selected spans through the
  checked queue, then rebuild scalar mappings. Write mappings/counts, fence,
  write nonzero generation, fence, and write committed last.
- Lookup zeroes output first and succeeds only for exact nonzero generation,
  exact bank ID, committed publication, and matching mapping generation.

## RED/GREEN and evidence

Write `actor_texture_residency_test.c` first. The RED command is
`make -k -f Makefile.saturn.mk verify-actor-texture-residency
verify-scene-residency` and must fail because the new API/target is absent.
Tests cover the real BOB bundle (14 v2 mappings, 16,640 texture bytes, 2,816
CLUT bytes), mixed v1/v2 fixture, canonical offsets, exact copied payloads,
separate short regions, capacity/aggregate/view/hash/bank mutations, collisions,
null/alignment/address overflow, false lifecycle flags, zero/stale/reused
generation, malformed prior publication, submit failure, wait failure, and
mid-transfer observation that no state is published.

GREEN must include actor texture residency, scene residency, IR texture,
S64B-v2, S64F-v3, actor material, VDP1 frame bank, transfer pipeline, Gouraud
transfer, feature-off/history, plus exact SH-2 freestanding syntax and object
compiles for amended/new production modules. Host-only and module-compile
evidence is not target runtime evidence.

## Reference/reuse record

- libyaul gitlink `6012f79f237773378c8014e70d8998ad95a38d98` (MIT):
  `libyaul/scu/bus/b/vdp/vdp1/vram.h` and `libyaul/scu/scu/dma.h`; dependency/API
  use and partition/alignment contract.
- In-tree `saturn_ir_texture.c`, `saturn_actor_bundle.*`, `saturn_actor_bank.*`,
  `saturn_actor_material.*`, `saturn_scene_residency.*`, frame-bank/Gouraud
  transfer modules: close-port/shared-core validation and generation-last
  fail-closed patterns.
- In-tree `slavedriver_dma_queue.*`: GPL-3.0-or-later close-port of SlaveDriver
  Engine `a8986591557b6e680550d3c23970284d3b38ff8f`, adapted to libyaul. Task 7
  depends on its checked submit/wait API and makes no queue-source change.

## Completion contract

Commit behavior as `feat(saturn): publish actor texture residency generations`
with CHANGELOG, active plan, ledger, and this task report in the same transition.
Mark only `source-complete-pending-review`; independent review is mandatory
before Task 8 opens.
