# Task 6 brief — stable S64B-v2 material to VDP1 binding

Plan: `docs/superpowers/plans/2026-08-12-saturn-actor-bank-v2-textures.md`

Base: `a553b500bbac30f98da96b065edebcdebded8ee2`

## Scope

Create `saturn_actor_material.h/.c`, `actor_material_test.c`, and
`ir_texture_test.c`; modify `saturn_ir_texture.h/.c`, `Makefile.saturn.mk`,
`CHANGELOG.md`, the active plan, ledger, and this task report. Stop before
texture residency, generation publication, runtime activation, renderer/Ymir,
or Task 7 work. Do not expand the file list without controller authorization.

## Exact interface

```c
typedef struct sm64_saturn_actor_texture_mapping {
    uint32_t bank_id;
    uint32_t texture_base_offset;
    uint16_t clut_base_index;
    uint16_t tile_count;
    uint32_t generation;
} sm64_saturn_actor_texture_mapping_t;

bool sm64_saturn_actor_material_bind(
    vdp1_cmdt_t *cmdt, const vdp1_vram_partitions_t *partitions,
    const sm64_saturn_actor_bank_view_t *bank, uint16_t primitive_id,
    const sm64_saturn_actor_texture_mapping_t *mapping,
    uint32_t active_generation,
    const int16_vec2_t vertices[4]);
```

## TDD and behavior

- First create RED host fixtures and run `verify-ir-texture
  verify-actor-material`; expected failures are uint8 truncation at 256/504 and
  the missing actor-material API.
- Widen both IR texture binder width inputs to `uint16_t`. Preserve exact 8 and
  248 behavior. Accept only widths 8..504 inclusive that are multiples of 8;
  reject 0, 505, and every non-multiple-of-8. Preserve height range 1..255.
- Check every width/height/address/partition arithmetic before command
  mutation, including `cmd_size = ((width / 8U) << 8) | height`.
- Bind only validated S64B-v2 render binding, material, and tile records.
  Reject stale/nonzero-generation mismatch, wrong bank identity, mapping tile
  count mismatch, invalid primitive/tile/material, incompatible format/recipe,
  null vertices, and any out-of-range source/CLUT span. Every failure leaves the
  entire command byte-exact.
- Translate S64B-owned stable recipes to Yaul enums only at this master-owned
  boundary. Cover CLUT16/RGB1555 replace, Gouraud, and half-transparent
  recipes; flat Gouraud is an untextured polygon. End-code interpretation is
  disabled for every fixed-size textured pattern. Verify exact source address,
  size, CLUT address, color mode, blend mode, and four input vertices.
- Keep package enums separate from Yaul enums. Add no pointer or residency
  state to serialized/cross-SH-2 data.

## Verification and completion contract

Run focused IR/material gates, existing bank/family gates, and exact
freestanding SH-2 syntax checks for both changed modules using the current
sourceboot/libyaul target flags. Self-review atomic command mutation,
big-endian validated accessors, width arithmetic, bank/mapping bounds, recipe
coverage, Yaul partition bounds, master-only ownership, feature-off/history
compatibility, and absence of Task 7 leakage. The behavior commit must include
`CHANGELOG.md`; update plan, ledger, and `task-6-report.md` with exact commands,
results, commits, and open gates. Finish as source-complete-pending-review only.
