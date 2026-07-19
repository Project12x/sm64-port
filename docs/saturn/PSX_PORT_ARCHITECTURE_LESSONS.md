# PS1 port architecture lessons for the Saturn target

Last updated 2026-07-19.

## Decision

`malucard/sm64-psx` shows that the next Saturn gain is a compact, generated
runtime around the original SM64 game loop—not a bespoke Castle renderer or a
generic triangle replacement. The Saturn port will adopt that *architecture*
through its own Yaul, VDP1/VDP2, and 4 MiB cartridge implementation.

The PS1 project has no repository-wide license at the inspected pin, so this
is a **behavior study only**. No PS1 source may be copied or close-ported.
Pinned source paths, status, and the distinct GPL adaptation candidates are in
[`UPSTREAM_CODE_LEDGER.md`](UPSTREAM_CODE_LEDGER.md).

## What the PS1 port does that the Saturn target needs

| PS1-port lesson | Saturn decision | Concrete destination and acceptance proof |
| --- | --- | --- |
| A normal game frame reads input, advances the source level/object loop, then submits the source display work. | Retire harness-owned Mario motion, camera, animation, and room selection. | E2 introduces `saturn_game_loop`: `game_loop_one_iteration()` produces an `SPTask` consumed by Saturn `exec_display_list()`. A recorded route must agree with the PC-source state trace. |
| Display lists are preprocessed into a smaller target-native representation rather than interpreted as full N64 RSP streams every frame. | Keep generated IR as the permanent Fast3D boundary; static area packets must be templates, not Castle globals. | The compiler emits source-ID-preserving primitive/material records and per-area VDP1 command templates. Per frame, patch only matrices, visibility, order, and texture residency. |
| Fixed/low-precision math is used where frame cost justifies it. | Preserve source simulation semantics, but move hot Saturn boundaries to Q16/packed 16-bit records after measurement. | Transform, camera, clip, cull, projection, and ordering stages report before/after ticks and remain deterministic against a PC trace. |
| Textures and Mario animation data are prepared for target VRAM/residency rather than fetched ad hoc per primitive. | Treat the 4 MiB DRAM cartridge as an area-bank cache, not fast heap. | E3 area manifests batch/prefetch geometry, texture, CLUT, and animation banks to cartridge; a bounded internal-WRAM ring promotes the visible working set to VDP1. |
| Large polygon handling and painter ordering are explicit target policies. | Use one source-aware, scene-neutral VDP1 ordering/clipping path. | Generated topology, coarse hierarchy, near clipping, and adaptive subdivision are selected by material/camera budget, never by a Castle/Bob special case. |
| Controller, camera, HUD, profiler, and replay state belong to the runtime. | Make 3D Control Pad input camera-relative and expose deterministic diagnostics. | Input/camera modes, phase timing, command/texture memory, source action, and state replay are recorded by the shared platform layer. |

## Required implementation order

1. Close E1 and E2: make the shared renderer accept source-identified jobs,
   then boot the real source loop and `exec_display_list()` handoff.
2. Close E3: compile Castle Area 1 into an offset-based area package and prove
   deterministic load/unload, cartridge residency, and replay.
3. Reduce the shared hot path: static command templates, transform-once job
   records, fixed-point/packed boundaries, and measured texture/animation
   banks. The current stable frame probe is the baseline.
4. Generalize it through E4 by compiling a bounded Bob-omb Battlefield region
   and one dynamic object actor without scene-named runtime branches.
5. Add presentation systems—HUD, transitions, minimal audio—after the source
   loop and package lifecycle are proven, not before.

This ordering is deliberately different from polishing the Castle viewer. A
visual improvement counts only when it survives the source loop, common IR,
area package, and global VDP1 ordering path.

## Important non-lessons

The PS1 port is useful evidence, but not a backend to imitate mechanically:

- PS1 ordering tables, GTE transforms, GPU packet layout, and CLUT/VRAM policy
  do not map directly to VDP1 quads, VDP2 planes, or Saturn memory buses.
- Its README records unfinished camera/title/pause work, animation failures,
  texture errors and individual-texture loading stutters. These are failure
  cases to avoid, not compatibility targets.
- Its benchmark configuration requires 8 MiB and is not a retail target. The
  Saturn port must budget its mandatory 4 MiB cartridge plus hot internal WRAM
  explicitly.
- The face/Goddard work remains original SM64-source integration; it should
  not be replaced by the PS1 port's separate WIP subsystem.

## Sources inspected

At `malucard/sm64-psx` pin `3073845688ea273da78d539b20c45110d8a868c3`:

- `README.md` for declared architecture, limitations, and benchmark context;
- `src/game/game_init.c` for the source-frame ordering;
- `src/port/gfx/gfx_rsp_jit.c`, `src/port/psx/gfx_dl_exec_psx.c`, and
  `src/port/psx/gfx_tessellation_psx.c` for compact command processing and
  large-polygon policy;
- `src/port/psx/gfx_texture_psx.c` and `cd_psx.c` for texture/asset residency;
- `src/port/psx/controller_psx.c` and `src/game/hud.c` for platform input and
  runtime presentation boundaries; and
- `tools/preprocess_graphics.py`, `convert_image_psx.py`,
  `pack_textures.py`, and `compress_mario_anims.c` for offline asset steps.

The active Saturn source counterparts are the architecture contract, area
compiler, Yaul controller backend, and generated-IR tooling—not the PS1
backend. See [`ENGINE_PORT_ARCHITECTURE.md`](ENGINE_PORT_ARCHITECTURE.md) and
[`CARTRIDGE_ASSET_POLICY.md`](CARTRIDGE_ASSET_POLICY.md).
