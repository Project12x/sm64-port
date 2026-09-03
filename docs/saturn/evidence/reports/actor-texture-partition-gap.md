# The actor texture budget is paper: VDP1 is never partitioned for it

**Date:** 2026-09-01 · Found by bisecting BOB's actor roster with a build and a
headless capture per actor, then correlating residency against pass/fail.

## The symptom

Admitting MODEL_WOODEN_SIGNPOST, MODEL_KOOPA_SHELL or MODEL_GOOMBA produces a
build that compiles, boots, matches its sealed identity, reports **zero faults
from either CPU**, and then presents **0 frames across 3,600 vblanks**. Every
check the bake has passes. MODEL_GOOMBA was retested in isolation and still
failed, so it is not cumulative load.

## The boundary, measured

| generation | set | verdict | actor texture bytes |
|---|---|---|---:|
| g26 | star only | works | 21,504 |
| g92 | star + 4 | works | 22,912 |
| g85 | + bob-omb buddy | works | 24,320 |
| **g84** | **star + 4 (variant)** | **works** | **25,600** |
| **g90** | **+ goomba** | **BLACK** | **25,984** |
| g76 | all 8 | BLACK | 33,664 |

The largest residency that presents frames is **25,600 bytes exactly**.

## Why that number

`src/port/saturn/sourceboot/main.c`:

```c
#define SOURCEBOOT_BOB_TEXTURE_BYTES 333696U
#define SOURCEBOOT_MARIO_TEXTURE_BYTES \
    (SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT * \
     SM64_MARIO_TEXTURE_UV_TILE_WIDTH * SM64_MARIO_TEXTURE_UV_TILE_WIDTH * \
     sizeof(uint16_t))
#define SOURCEBOOT_TEXTURE_BYTES \
    (SOURCEBOOT_BOB_TEXTURE_BYTES + SOURCEBOOT_MARIO_TEXTURE_BYTES)
```

with `SM64_MARIO_TEXTURE_UV_TRIANGLE_COUNT = 50` and
`SM64_MARIO_TEXTURE_UV_TILE_WIDTH = 16`:

**50 x 16 x 16 x 2 = 25,600 bytes.**

That is the observed boundary to the byte, and it is not a coincidence:
`vdp1_vram_partitions_set()` is handed `SOURCEBOOT_TEXTURE_BYTES`, which
contains a **terrain term and a Mario term and nothing else**. There is no actor
texture term in the VDP1 partition at all. The actor residency writer
(`saturn_actor_texture_residency.c:553`) is then initialised with
`partitions->texture_base` / `partitions->texture_size` and lays banks out from
a cursor starting at zero.

## The real defect

**The bake budgets actor textures out of VDP1 space the runtime never reserves.**

`inventory_actor_family_bundles.py` carries `actor_texture_bytes` and
`actor_clut_bytes`, justified in its own comment as "drawn from
`existing_vdp1_remaining_bytes` (52,672 B of unallocated VDP1 that no current
binder owns)". That 52,672 B is a **paper allocation**. No binder owns it
because nothing partitions it -- `SOURCEBOOT_TEXTURE_BYTES` does not include it,
so the space the bake is spending is not space the runtime has handed out.

Actor residency therefore fits only while it happens to stay inside the region
the partition does contain, and past 25,600 B it runs off the end.

This is the same class of defect as the workspace constant fixed the same day: a
capacity the bake reasons about and the runtime does not implement, with nothing
comparing the two. It is worth assuming there are more.

## What a fix looks like

1. Add an actor term to `SOURCEBOOT_TEXTURE_BYTES` and the CLUT count, so VDP1
   is actually partitioned for what the bake spends. VDP1 has the room on
   paper -- 333,696 terrain + 25,600 Mario + 34,464 terrain CLUT = 393,760 of
   524,288 -- but the command region and gouraud tables also live there, so the
   real headroom must be measured rather than assumed from that subtraction.
2. Tie the bake's `actor_texture_bytes` / `actor_clut_bytes` to the runtime
   constants the way `compile_actor_family_bundle.py` now ties the workspace,
   so a budget the runtime cannot honour fails the build instead of shipping a
   black screen.
3. Only then retry MODEL_WOODEN_SIGNPOST, MODEL_KOOPA_SHELL and MODEL_GOOMBA.
   Their capture digests are already recorded beside their keys in
   `actor_material_v2.py`, so re-admitting them is mechanical.

## Method note

This was found by **building and running each actor**, not by baking it. Bake
success proves nothing about this failure mode, because the bake's model of VDP1
is the thing that is wrong. Three hypotheses were tested and discarded first --
the workspace constant (a real bug, but not this one), Goomba's gouraud service
floor, and flat shading regressing existing actors -- at roughly a build cycle
each. The per-actor bisect found it in one pass and should have been the first
move.
