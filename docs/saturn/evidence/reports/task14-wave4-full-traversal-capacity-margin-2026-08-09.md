# Task 14 wave 4 — full-traversal geo-walk capacity margin re-measurement — 2026-08-09

Scope: waves 1-4 (`5ba8d85c`, `958caf5f`, `e92122c1`, `169141f5`, `51c46bfa`,
`ea31881c`, `3cc5e84a`, `ac37a009`, `442597e7`) finished converting every
per-node-type handler `geo_process_node_and_siblings`'s switch dispatches to
in `rendering_graph_node.c` onto the bounded `saturn_geo_walk_runtime`. Wave
3's own capacity-margin report
(`task14-wave3-geo-walk-capacity-margin-2026-08-07.md`) was scoped only to
the 3 OBJECT-family types and explicitly flagged this re-measurement as
owed "once the guard's fallback stops confining the [skeleton] path." This
report does that re-measurement for real, against the actual runtime, now
that all node types are converted — and finds the guard's fallback has
**not** stopped confining that path for the one case that matters most.

## Summary

- **Real, measured peaks** (driving the actual
  `src/port/saturn/runtime/saturn_geo_walk_runtime.c` through the exact
  push shapes `rendering_graph_node.c`'s `saturn_geo_walk_enter` now
  produces for every real node type involved, see "Methodology" below):
  - **5 frames** — canonical infrastructure trunk (`PERSPECTIVE -> CAMERA ->
    OBJECT_PARENT -> OBJECT`, matching this file's own "scene graph
    typically looks like" comment) ending at an object whose own geo-layout
    root is `GEO_CULLING_RADIUS` — the shape of `actors/whomp/geo.inc.c`
    and 24 other actor `geo.inc.c` files found by grep this session.
  - **6 frames** — the same trunk continuing into **Mario's actual shipped
    in-game render path** (`mario_geo` → `GEO_BRANCH(mario_geo_render_body)`
    → `GEO_NODE_START()`, `actors/mario/geo.inc.c:1788`) before detouring.
  - **14 frames** — a **hypothetical** fully-bounded continuation past that
    detour, using `mario_geo_body`'s own real structural depth (8,
    independently measured by `tools/saturn/geo_depth_manifest.py`'s
    `scan_source` against the real file) as an `ANIMATED_PART` chain ending
    in a `GEO_HELD_OBJECT` edge. This is **not** today's shipped behavior
    for the common case — see "The headline finding" below.
  - **6 frames** — a 240-object live sibling ring (the real object-pool
    cap), each whomp-style. Confirms list *width* does not add to peak
    *depth* (matches and generalizes wave 3's smaller 3-object probe).
- **Margin against the manifest** (`capacity=256`, `safety_margin=16`,
  from `build/saturn/sourceboot/generated/saturn_geo_depth_manifest.h`):
  usable budget is `256 - 16 = 240` frames. All four measured/hypothetical
  peaks above (5, 6, 14, 6) fit with **226-235 frames to spare** — the
  bounded array remains capacity-safe by an enormous margin. This was
  already true after wave 3 and remains true; waves 1-4 did not
  meaningfully change the array's own peak usage, for the structural
  reason below.
- **The headline finding, and why it matters more than the margin number:**
  `GRAPH_NODE_TYPE_ROOT`/`START`/`CULLING_RADIUS` remain, by design,
  unconverted (task 2's guard-disposition decision, this session — see
  the companion write-up in `docs/superpowers/plans/2026-08-07-task14-completion.md`).
  In real, shipped content these three types are not rare edge cases: they
  gate off large fractions of real geometry into real, unbounded C-stack
  recursion via `sSaturnGeoWalkActive`'s deliberate fallback — most
  critically, **Mario's own live-render path**. `geo_switch_mario_stand_run`
  (`src/game/mario_misc.c:343-352`) selects `mario_geo_render_body` (the
  `GEO_NODE_START`-gated, LOD-based branch) whenever
  `ACT_FLAG_STATIONARY` is *not* set — i.e. whenever Mario is moving,
  which is the overwhelming majority of real play time — and selects
  `mario_geo_load_body` (fully bounded, no `START`/`CULLING_RADIUS`
  anywhere in its subtree) only while he stands perfectly still. **So
  during normal gameplay, Mario's entire body/limb/held-object chain runs
  via real C recursion today, exactly as before this whole effort**,
  because it detours off the bounded array one level below `OBJECT`,
  before ever reaching the `ANIMATED_PART`/`HELD_OBJECT` chain the
  original "Mario holding something" master-stack-overrun scenario (wave
  3's report, and the crash this task's boot-crash root-cause section
  documents) was about. This does not make the bounded array unsafe — it
  is proven safe with a huge margin for everything it actually handles —
  but it means the array's proven margin is not the load-bearing safety
  proof for that original scenario. The C-stack recursion depth for that
  detoured path is bounded by the pre-existing, unrelated
  `tools/saturn/geo_depth_manifest.py` mechanism
  (`max_proven_depth=172`, `capacity=256` frames of *native SH-2 stack*,
  sized against `actors/mario/geo.inc.c`'s real worst-case structural
  content — the same file this report also measures), unchanged by waves
  1-4.
- **This is consistent with, not a contradiction of, task 2's
  guard-keep decision**: the guard exists precisely because
  `ROOT`/`START`/`CULLING_RADIUS` remain unconverted, and this report is
  the first real measurement of how much real content that leaves outside
  the bounded array's reach.

## Methodology

Extends wave 3's own method (drive the real
`saturn_geo_walk_runtime.c` through synthetic node shapes matching
`saturn_geo_walk_enter`'s real per-type push logic, read `walk.high_water`)
to the full, now-converted switch, plus a `DETOUR` node type modeling the
hand-off to `saturn_geo_walk_dispatch_legacy()`'s real recursion (reported
as `admitted=false`, which is the exact and correct bounded-array
accounting for that hand-off: everything past it is off this array by
construction). Four scenarios (A-D above) were driven through the real
runtime; the C harness (not part of the shipped test suite, not committed
— same precedent as wave 3's own throwaway probes) is preserved at
`C:\Users\estee\AppData\Local\Temp\claude\D--Code-RetroDev-sm64-saturn-port\4cf09e19-94d4-4d00-a8d5-aa76996f837d\scratchpad\wave4_probe\wave4_capacity_probe.c`
for this session; reproduce with:

```
$ gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/runtime \
    wave4_capacity_probe.c src/port/saturn/runtime/saturn_geo_walk_runtime.c \
    -o wave4_capacity_probe && ./wave4_capacity_probe
A: trunk + whomp-style object (CULLING_RADIUS at object root)          high_water=5 final_depth=0 ok=1
B: trunk + Mario's real shipped render path (detours at START)         high_water=6 final_depth=0 ok=1
C: hypothetical fully-bounded Mario body+held-object chain             high_water=14 final_depth=0 ok=1
D: trunk + 240-wide live-object ring, whomp-style each                 high_water=6 final_depth=0 ok=1
```

All four scenarios finished at `final_depth=0` (no stranded frames). The
harness was independently caught and fixed once during this session's own
verification: an early draft used 0-indexed node tokens, which collided
with the runtime's `0` == "no child/no sibling" sentinel and silently
truncated the very first node added — caught by adding a step-by-step
trace mode (visible in the harness's `run_probe(..., trace=true)` path)
before any number was recorded, not after. This is exactly why this
report drives the real runtime instead of hand-deriving the numbers, per
the task's own standard.

**Real source facts this methodology depends on, each independently
verified against the current HEAD this session:**
- `saturn_geo_walk_enter`'s real switch (`rendering_graph_node.c:2265-2520`)
  has real cases for `MASTER_LIST, ORTHO_PROJECTION, PERSPECTIVE, CAMERA,
  BACKGROUND, LEVEL_OF_DETAIL, SWITCH_CASE, TRANSLATION_ROTATION,
  TRANSLATION, ROTATION, SCALE, BILLBOARD, ANIMATED_PART, DISPLAY_LIST,
  GENERATED_LIST, SHADOW, OBJECT_PARENT, OBJECT, HELD_OBJ` — 18 types.
  `default:` (everything else, including `ROOT`/`START`/`CULLING_RADIUS`)
  falls to `saturn_geo_walk_dispatch_legacy()` → `geo_try_process_children()`
  → real recursive `geo_process_node_and_siblings()` call
  (`rendering_graph_node.c:2221-2223`, `:1918-1920`).
- `saturn_geo_walk_process_children`'s reentrancy check
  (`rendering_graph_node.c:2666-2669`) is a plain, type-agnostic boolean:
  once `sSaturnGeoWalkActive` is true, **every** subsequent call — for
  *any* node type, converted or not — falls back to real recursion until
  the outermost call returns. This is why a single `START`/`CULLING_RADIUS`
  detour takes the *entire* remaining subtree off the bounded array, not
  just the detour node itself.
- `actors/whomp/geo.inc.c:3` — `GEO_CULLING_RADIUS(2000)` is the file's
  first command (object-local depth 0 before detour). 25 actor
  `geo.inc.c` files share this exact shape (`grep -rl GEO_CULLING_RADIUS
  actors/`, this session); 250 level+actor files use `GEO_CULLING_RADIUS`
  somewhere.
- `actors/mario/geo.inc.c:1809-1825` (`mario_geo`, "used to load all of
  Mario Geo in the Level Scripts") → `GEO_SWITCH_CASE(0,
  geo_switch_mario_stand_run)` selects between `GEO_BRANCH(1,
  mario_geo_load_body)` (case 0) and `GEO_BRANCH(1, mario_geo_render_body)`
  (case 1); `geo_switch_mario_stand_run`
  (`src/game/mario_misc.c:343-352`) sets `selectedCase = ((bodyState->action
  & ACT_FLAG_STATIONARY) == 0)` — case 1 (render_body, LOD-gated) whenever
  Mario is not stationary.
- `actors/mario/geo.inc.c:1787-1804` (`mario_geo_render_body`) opens with
  `GEO_NODE_START()` (line 1788) — confirmed to compile to a real,
  registered `GraphNodeStart` instance
  (`src/engine/geo_layout.c:280-288`, `geo_layout_cmd_node_start` →
  `init_graph_node_start` → `register_scene_graph_node`), not a
  compile-time-only marker.
- `actors/mario/geo.inc.c:1747-1756` (`mario_geo_load_body`, the
  stationary-case branch) and everything it reaches
  (`mario_geo_body`/`mario_vanish_geo_body`/`mario_metal_geo_body`/
  `mario_metal_vanish_geo_body`, all `ANIMATED_PART`/`SCALE`/`ASM`
  (`GENERATED_LIST`)/`HELD_OBJECT`/`BRANCH` — no `ROOT`/`START`/
  `CULLING_RADIUS` anywhere) is fully bounded today — scenario C's 14-frame
  number is this branch's *real*, not hypothetical, peak when Mario is
  standing still.
- `saturn_geo_enter_object_parent`/`saturn_geo_enter_object`
  (`rendering_graph_node.c:1510-1621`, `:1702-1710`) both report
  `node.children` as `own_children`, and wave 3's independently-verified
  proof ("node.children is always NULL") establishes this is always
  `NULL` in real content for both types — so both always take the
  runtime's single-subtree, combined/final-leave path, as modeled here.

## Interpretation

The manifest's `capacity=256`/`safety_margin=16` was sized (per its own
generator docstring) as "a forward-looking budget for once ALL node types
are eventually converted." That framing is not yet accurate: `ROOT`,
`START`, and `CULLING_RADIUS` are permanent-by-design exceptions (task 2's
decision), not a temporary gap a future wave will close. The bounded
array's real peak usage is therefore governed less by scene complexity and
more by **how early a `ROOT`/`START`/`CULLING_RADIUS` node appears on a
given path** — and in real content, these three types are typically placed
right at or near an object's own geo-layout root (culling and LOD gates
are naturally outermost concerns), so the array's real peak stays low
(5-6 frames measured) even though the *content* behind those gates can be
arbitrarily deep. The 226-235-frame margin is real and large, but it is
margin for a *shallow* trunk, not proof that deep scene content is bounded
— deep scene content mostly is not reaching the array at all.
