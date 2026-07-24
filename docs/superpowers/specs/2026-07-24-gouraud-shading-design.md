# Gouraud Shading for the Sourceboot Fast3D-to-VDP1 Pipeline — Design

Date: 2026-07-24. Follows the Q16.16 render-matrix sprint
(`docs/superpowers/plans/2026-07-23-q16-render-matrix-sprint.md`, tagged
`v0.1.0`). This cycle is Gouraud only; texture work (offline-bake direction
already decided) is a separate later cycle.

## Why this is a correctness fix, not just polish

BOB's content — terrain and actors alike — runs with `G_LIGHTING` enabled
globally (`src/game/game_init.c:131`). Under `G_LIGHTING`, the trailing four
bytes of every vertex are a packed **normal** (`Vtx_tn`, `include/PR/gbi.h:1134`),
not a color. The frontend currently reads them unconditionally as RGBA
(`src/port/saturn/gfx/saturn_fast3d_frontend.c:667-670`, cast at `:638`) and
flat-shades each triangle with vertex 0's "color" (`:433-436`). The colors in
the v0.1.0 capture are therefore normal vectors misinterpreted as paint.
Implementing lighting is what makes the frame's colors correct at all.

No light data is decoded today: `G_MOVEMEM` handles only the viewport
(`saturn_fast3d_frontend.c:595-627`; `G_MV_LIGHT` falls through), and
`G_MOVEWORD` (numLights) has no case at all.

## Governing principle (user directive, this cycle and beyond)

**Degrade first; function first.** Ship the minimum tier that produces
correct-looking shaded frames on Saturn. Every dropped or simplified feature
gets a profile counter, so the cost of each degradation stays visible in
captures. A written fidelity ladder (below) defines what gets pulled back in,
in what order, if the base proves stable and cycles remain. Do not build
upgrade machinery speculatively — the ladder is a to-do list, not scaffolding
in this cycle's code.

Precedent: the castle demo already made this trade deliberately — dynamic
Gouraud only for the animated actor (`castleviewer/main.c`: `mario_gouraud`
bank, per-pose rebuild, dirty-flag upload, with a comment that per-frame
re-upload "was a measurable Saturn bandwidth tax"), scalar-intensity math,
and no light modulation at all on the CLUT-textured world (VDP1 cannot
Gouraud-modulate palette-mode textures — `docs/saturn/SGL_REFERENCE_NOTES.md:224-225`).

## Semantic anchor

`src/pc/gfx/gfx_pc.c` — the PC port's lowering of these exact display lists —
is the reference for what each vertex's lit color *should* be, the same way
`src/engine/math_util.c` anchored the matrix sprint. The Q16 evaluator mirrors
its lighting semantics (including its key efficiency property: light
*directions* are re-transformed once per matrix change; vertex normals stay in
model space and are dotted raw). Output fidelity is deliberately Saturn-class
per the degradation contract; *semantics* (which surface is bright, which is
dark) follow gfx_pc.c. The implementation plan must read gfx_pc.c's actual
lighting code before writing the evaluator — this spec cites its role, not its
line numbers.

## Architecture / data flow

```
G_MOVEWORD(numLights) ─┐
G_MOVEMEM(G_MV_LIGHT) ─┴─> frontend light state (1 ambient + 1 directional)
G_MTX ────────────────────> re-transform light dir into model space (once)
G_VTX ── G_LIGHTING on ──> per-vertex Q16 eval: amb + light·max(0, N·L)
      └─ G_LIGHTING off ─> vertex bytes used directly as RGBA
                    │
                    v
resolved triangle: 3 corner colors (RGB555)
                    │
                    v
frame-local Gouraud bank: one 8-byte vdp1_gouraud_table_t per emitted
triangle (A/B/C/C corner duplication), used-prefix upload once per frame
via the SlaveDriver-derived DMA queue (src/port/saturn/gpl/)
                    │
                    v
VDP1 command: CM_RGB_32768 + CC_GOURAUD, neutral base color 0xC210
(existing src/port/saturn/gfx/saturn_gouraud.h), per-command gouraud_base
```

SM64's lighting model is small and fixed: `Lights1` = 1 ambient + 1
directional (`gbi.h:1438-1441`), sent via `gSPLight` → `G_MOVEMEM`/`G_MV_LIGHT`
(`gbi.h:2554-2558`) and `gSPNumLights` → `G_MOVEWORD`/`G_MW_NUMLIGHT`
(`gbi.h:2533-2534`). BOB's terrain and actors both use exactly this pattern
(`levels/bob/areas/1/1/model.inc.c:2,433-434`; `actors/small_key/model.inc.c:7-25`).

## Components

1. **`src/port/saturn/gfx/saturn_light_q16.h`** (new; host-testable,
   Yaul-free, same design as `saturn_matrix_kernels.h`): light-state struct
   (ambient RGB, directional RGB, model-space direction in Q16) and the
   per-vertex evaluator. Uses `sm64_saturn_q16_mul` from the existing kernels.
   Differential-tested on host against a float reference implementing
   gfx_pc.c's math.

2. **Frontend decode extensions** (`saturn_fast3d_frontend.c/.h`):
   - `G_MOVEWORD` case: capture numLights (values ≠ 1 directional: counted,
     first light used).
   - `G_MOVEMEM` `G_MV_LIGHT` case: capture ambient + directional `Light`
     payloads (strict-aliasing-safe reads, per this codebase's established
     discipline).
   - `G_MTX` handler: mark light-direction dirty; re-transform lazily on next
     lit `G_VTX` (mirrors gfx_pc.c's lights_changed pattern).
   - `G_VTX`: interpretation gated on `G_LIGHTING` (the geometry-mode word is
     already stored, `:685-686` — it is simply never consulted today). Vertex
     struct gains a computed RGB; resolved triangle
     (`saturn_fast3d_frontend.h:233-238`) gains 3 corner colors, replacing the
     single `color_rgb1555`.

3. **`src/port/saturn/gfx/saturn_gouraud_bank.h`** (new; shared component so
   the texture cycle reuses it): bounded frame-local table bank over Yaul's
   `gouraud_base` partition. API shape: begin-frame reset / allocate-table
   (returns VRAM address for the command, writes 4 RGB555 entries to a CPU
   staging array) / end-frame used-prefix upload through
   `saturn_dma_queue_*` (the GPL adapter already carries Gouraud uploads in
   hwtest — `docs/saturn/SLAVEDRIVER_ADAPTATION.md:43`). Capacity =
   `SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES` (1536) tables = 12,288 bytes
   worst case; verify against Yaul's actual partition size at init, clamp and
   count if smaller.

4. **Emit path** (`saturn_fast3d_vdp1_emit.c`): replace flat
   `CC_REPLACE`/`color_set` (`:44-52`) with `CC_GOURAUD` + neutral base +
   `vdp1_cmdt_gouraud_base_set`. Triangles duplicate corner C into D
   (A/B/C/C), matching the demos' corrected corner mapping.

## Degradation contract (this cycle's shipped tier)

| Dropped/simplified | Behavior | Counter |
|---|---|---|
| Fog (`G_FOG`, used by BOB terrain at `levels/bob/areas/1/1/model.inc.c:584`) | Ignored entirely | fog-encountered counter |
| >1 directional light | First directional used | unsupported-light-count counter |
| Perspective-correct interpolation | VDP1 screen-linear (hardware) | — (inherent) |
| 8-bit color precision | RGB555 table quantization (hardware) | — (inherent) |
| Gouraud bank exhaustion | Remaining triangles flat-lit with their vertex-0 computed color (graceful, bounded) | bank-overflow counter |
| Table caching / dirty tracking | None — full per-frame rebuild, used-prefix upload (user decision 2026-07-24) | — |
| Palette-mode texture modulation (future) | Forward note only: CLUT textures cannot take Gouraud; expected endgame mirrors the castle demo (RGB-mode lit Mario, CLUT unlit terrain) — decided in the texture cycle, not here | — |

## Fidelity ladder (pull in later, in this order, only if stable + cycles remain)

1. Dirty/cached tables for static world geometry (the demos' conditional-upload
   pattern) — biggest bandwidth win, needs stable triangle identity.
2. Fog approximation: per-vertex blend toward fog color by depth, through the
   same tables — restores BOB's atmospheric look.
3. Multi-light support (SM64 rarely needs it; counter data will say).
4. Per-vertex evaluation cost reductions (LUTs, scalar-intensity tier) — only
   if profiling shows the evaluator on the critical path.

## Error handling

Everything bounded and counted, nothing crashes — the frontend's existing
profile-counter discipline. New counters: fog-encountered,
unsupported-light-count, gouraud-bank-overflow, plus a lit-vertex/unlit-vertex
pair so captures show which path dominated. All reads of wire data
strict-aliasing-safe.

## Testing

1. Host differential test: Q16 evaluator vs a float reference implementing
   gfx_pc.c's lighting math, swept over real BOB `Lights1` values, the full
   s8 normal range, and light directions from real captures. Tolerances
   asserted, not printed — matrix-sprint pattern.
2. Runtime-contract tests: new decode cases (numLights, G_MV_LIGHT payload,
   lit/unlit G_VTX gating, corner-color propagation, bank
   allocation/overflow/used-prefix accounting).
3. Mutation pass per project standard (drop the clamp, swap ambient/directional,
   flip a corner index — tests must fail).
4. Full regression: all four existing host suites + SH-2 cross-compile +
   `make verify`.
5. Live capture at free-roam depth; screenshot presented to the user — the
   user's eyes are the acceptance gate. Expected: terrain in real colors with
   smooth shading; Mario recognizable. Both honest outcomes (visible
   improvement, or counters-improved-but-visuals-wrong with attribution) are
   acceptable completions.

## Out of scope

- Texture decode/bake (separate cycle; offline-bake direction already decided).
- Fog rendering (ladder item 2).
- Table caching (ladder item 1).
- Any change to gameplay float math or engine logic — the only engine-adjacent
  edit is inside the frontend/port layer; `rendering_graph_node.c` is not
  touched this cycle.
- Audio (separate equal-priority milestone per standing instruction).

## Fact base

All code citations above were verified against the working tree on 2026-07-24
(post-`v0.1.0`). Key files: `saturn_fast3d_frontend.c/.h`,
`saturn_fast3d_vdp1_emit.c`, `saturn_gouraud.h`, `saturn_matrix_kernels.h`,
`src/port/saturn/gpl/` (DMA queue), `include/PR/gbi.h`, `src/pc/gfx/gfx_pc.c`,
demo implementations in `castleviewer/main.c`, `marioturntable/main.c`,
`introface/main.c`, and Yaul's `vdp1_gouraud_table_t`
(`third_party/libyaul/.../vdp1/vram.h:32-34`).
