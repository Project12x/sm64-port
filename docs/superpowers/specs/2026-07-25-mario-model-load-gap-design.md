# Restoring the Model-Load Stage in Sourceboot — Design

Date: 2026-07-25. Follows the Gouraud shading cycle (tagged `v0.2.0`).

This is **sub-project 1 of 2**. It makes Mario — and the other 46 models the
retail boot chain registers — renderable through the real engine path. The
quad-optimized Mario IR substitution is sub-project 2 and gets its own spec;
it is deliberately not designed here, because it should be sized against
measurements this work produces.

## The defect

Mario's geometry is never submitted to the Fast3D frontend. He is not culled,
not rejected, not mis-lit, not offscreen. No Mario vertex, triangle, or matrix
has ever reached `saturn_fast3d_frontend.c`.

Traversal stops at one line:

```
src/game/rendering_graph_node.c:1127
    if (node->header.gfx.sharedChild != NULL) {
```

`sharedChild` is NULL because `gLoadedGraphNodes[MODEL_MARIO]` is never
written. `LOAD_MODEL_FROM_GEO(MODEL_MARIO, mario_geo)` exists at exactly one
place in the tree — `levels/scripts.c:68`, inside `level_main_scripts_entry[]`
(declared `levels/scripts.c:61`) — and sourceboot never executes that script.

Sourceboot substitutes its own entry at `src/port/saturn/sourceboot/source_entry.c:44`,
which runs `INIT_LEVEL` → act/level registers → three `CALL`s →
`EXECUTE(level_bob_entry)` → `JUMP(self)`. The file already documents this at
`source_entry.c:76`: *"This entry never reaches that call naturally, because it
never executes level_main_scripts_entry."* That comment was written about
`lvl_init_from_save_file`, which was fixed. The model-load half was never
revisited.

### The chain, verified hop by hop

| Step | Location | Result |
| --- | --- | --- |
| Backing array is plain BSS | `src/game/area.c:27` | all 256 slots zero at boot |
| Only writers of `gLoadedGraphNodes[]` | `src/engine/level_script.c:415, :427, :448` | never reached with index 1 |
| `MODEL_MARIO == 0x01` | `include/model_ids.h:26` | no aliasing |
| BOB reads the slot | `levels/bob/script.c:68` → `src/engine/level_script.c:463` | **NULL** |
| Propagates to spawn | `src/game/object_list_processor.c:505` → `src/engine/graph_node.c:726` | **NULL** |
| Traversal stops | `src/game/rendering_graph_node.c:1127` | zero geometry |

Nothing repairs it at runtime. `bhvMario` (`data/behavior_data.c:3506-3517`)
contains no `SET_MODEL`. `cur_obj_set_model` (`src/game/object_helpers.c:1124`)
has no caller in `src/game/mario*.c`. No Saturn-side injection exists.

### The cannon is the control

`MODEL_CANNON_BASE` is spawned at `levels/bob/script.c:40`, a few hundred units
from Mario's spawn at `levels/bob/script.c:101`. It travels the **identical**
path through the **identical** NULL branch, and renders — because its load sits
at `levels/scripts.c:191`, inside `script_func_global_1`, which
`levels/bob/script.c:69` does `JUMP_LINK`. Same frame, same function, same
branch; the single discriminating variable is which script performed the load.

This proves the object pipeline, `gLoadedGraphNodes`, `geo_process_object`, the
Q16.16 matrix path, Gouraud lighting, and VDP1 emission are all working.

### Link-level confirmation

With `-ffunction-sections -fdata-sections` + `-Wl,--gc-sections`, everything
reachable only from the unexecuted script was garbage-collected. In the map
file behind the shipped capture, `.rodata.mario_geo` and
`.rodata.level_main_scripts_entry` appear in the *discarded* sections block,
while `.rodata.cannon_base_geo` and `.rodata.script_func_global_1` are
retained. A machine check found **47/47 master-script models absent, 38/38
executed-script models present**, no exceptions.

Mario's geometry is not merely unregistered at runtime — it is absent from the
ELF and the cart image.

**`--gc-sections` is the detector, not the cause.** Disabling it would restore
`mario_geo` to `.rodata` and change nothing on screen, because
`gLoadedGraphNodes[1]` would still be NULL. Do not pursue a linker fix.

## Scope

Restore the model-registration stage the retail boot chain performs before any
level script runs, so that all 47 models in `levels/scripts.c:68-114` are
registered. Mario is the motivating case; stars, coins, and effect models come
with it and unblock later actor work.

## Approach

Mirror retail's sequence inside sourceboot's **own** entry script. Engine files
stay unmodified — this port's standing discipline — so the model loads are
declared in `source_entry.c`, referencing the same geo symbols
`levels/scripts.c` references.

Retail's pattern, which this reproduces:

1. `ALLOC_LEVEL_POOL()` (`levels/scripts.c:67`)
2. 47 × `LOAD_MODEL_FROM_GEO` / `LOAD_MODEL_FROM_DL` (`:68-114`)
3. `FREE_LEVEL_POOL()` (`:115`)

`FREE_LEVEL_POOL` is **shrink-to-fit, not destroy**: `level_cmd_free_level_pool`
(`src/engine/level_script.c:363-369`) calls
`alloc_only_pool_resize(sLevelPool, sLevelPool->usedSpace)` and nulls
`sLevelPool`. The allocated graph nodes persist for the session. This is why
the registered models survive into BOB's own `ALLOC_LEVEL_POOL` at
`levels/bob/script.c:67`.

The new block must run **before** `EXECUTE(level_bob_entry)`, and must not
disturb the existing, load-bearing call ordering documented at length in
`source_entry.c:63-124` (`lvl_init_from_save_file` before
`lvl_set_current_level`, both after the act-number store).

### Duplication, accepted deliberately

Listing 47 model loads in `source_entry.c` duplicates a block that exists in
`levels/scripts.c`. The alternative — extracting a shared script array — would
modify engine source for the benefit of one target. Duplication is chosen so
the engine tree stays pristine and the port's requirements are explicit and
greppable in port-owned code. The block carries a comment pointing at
`levels/scripts.c:68-114` as its origin so the two can be diffed.

## Constraints and where the risk actually is

**Cart image: comfortable.** Model `.rodata` routes to `.cart_rodata` (see
`sourceboot-cart.x:49-58`), i.e. the 4 MiB cart. `SOURCE.DAT` is currently
1,715,520 of 4,194,304 bytes (41%), leaving ~2.4 MiB. Restoring the models
grows this, and the existing `make verify` size check enforces the cap.

**HWRAM: unaffected.** Model data is cart-resident; graph nodes are pool-
resident. The 4 KiB TLSF floor added in `sourceboot-cart.x` is not implicated.

**LWRAM main pool: this is the real gate.** `process_geo_layout` allocates
GraphNode structs at runtime from `sLevelPool`, sized from
`main_pool_available()`. The pool is 384 KiB (`SOURCEBOOT_MAIN_POOL_BYTES =
0x60000`), and BOB's collision load alone needs roughly 166 KiB of it. Adding
47 models' node trees consumes more. This must be **measured, not estimated** —
this project has already been burned once by an under-sized pool causing a
silent allocation failure and out-of-bounds writes (see the
`SOURCEBOOT_MAIN_POOL_BYTES` history comment in `sourceboot/main.c`).

**Degradation path if the pool cannot hold all 47:** fall back to a named
subset — Mario plus the models BOB actually spawns — rather than silently
truncating. The subset must be explicit in source with a comment stating why
each entry is present. Failing loudly beats a half-populated model table.

## Open implementation question for the plan

Retail loads segments before registering models: `LOAD_MIO0(0x04, group0)` and
`LOAD_RAW(0x17, group0_geo)` at `levels/scripts.c:62-63`. Mario's geo lives in
group0 (`actors/group0_geo.c:18` includes `mario/geo.inc.c`), and BOB's script
loads only group3/group14/common0 — not group0.

This build defines `NO_SEGMENTED_MEMORY=1`. Whether the segment-load commands
are required, no-ops, or partially required under that define is **not
established** and must be determined by reading `level_cmd_load_mio0`
(`src/engine/level_script.c:282`), `load_segment_decompress`, and
`segmented_to_virtual` under `NO_SEGMENTED_MEMORY` before writing the block.
Do not assume either way.

## Success criteria

1. `gLoadedGraphNodes[MODEL_MARIO]` is non-NULL after the entry script runs.
2. Mario's `sharedChild` is non-NULL, verified by memory probe at the same
   address used for this cycle's diagnosis (`gMarioObject`, offset `0x14`).
3. Mario's geometry reaches the frontend: `triangles_transformed` rises
   materially above the terrain-only baseline of 1307 at the probed frame.
4. Mario is visible in a free-roam capture, animated and Gouraud-shaded.
5. The user confirms the screenshot. Per this project's standing rule, the
   user's eyes are the acceptance gate; no TIMELINE or gallery entry is written
   before that confirmation.
6. No regression: all four host suites, SH-2 cross-compile, and `make verify`
   exit 0. `fault_flags` remains 0.

Both honest outcomes are acceptable completions: Mario renders, or Mario still
does not render and the counters attribute the next cause. The second outcome
is a real result given no skinned actor has ever been traversed in this target.

## Measurements to capture (input to sub-project 2)

These are the reason to do this before the IR work, and must be recorded even
if Mario looks wrong:

- LWRAM pool headroom before and after registration.
- Cart image size delta.
- `triangles_transformed` / `triangles_emitted` / `triangles_vdp1_emitted`
  attributable to Mario, by differencing against the terrain-only baseline.
- Total VDP1 command count against the ~650-command 15 FPS floor and
  ~450-command 20 FPS stretch profile recorded in `docs/saturn/ROADMAP.md`.
- Whether the animated-part Q16 matrix path produces sane matrices — the
  `modelview_stack_overflow` and `reject_w_nonpositive_overflow_suspect`
  counters are the sensors.

## Out of scope

- **Quad-optimized Mario IR** — sub-project 2, sized from the measurements above.
- **Textures** — the offline-bake cycle remains deferred; Mario renders
  Gouraud-shaded like the terrain does today.
- **Performance work** — transform-once caching is the roadmap's named next
  lever and is not attempted here. This cycle may legitimately make the frame
  slower; that is expected and gets measured, not fixed.
- **Geometry LOD** — a real lever with a documented home in the mesh IR, but it
  belongs to sub-project 2 where it can be validated per pose.
- Any change to `levels/scripts.c` or other engine files.

## Fact base

All citations verified against the working tree on 2026-07-25 at tag `v0.2.0`
plus the post-tag review fixes. Live values (`sharedChild = 0x0`,
`flags = 0x21`, `MarioState.pos`, `ACT_IDLE`, camera focus) were read by direct
memory probe from a cart-enabled `ymir-headless` boot at 240 BIOS frames +
25,000 post-poke frames, the same depth used for this project's capture
evidence.
