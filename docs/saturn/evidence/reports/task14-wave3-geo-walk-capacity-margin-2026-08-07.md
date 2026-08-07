# Task 14 wave 3 — geo-walk capacity margin and reentrancy-guard evidence — 2026-08-07

Scope: converting `geo_process_object`, `geo_process_object_parent`, and
`geo_process_held_object` from real recursion to the bounded
`saturn_geo_walk_runtime` (following the two-subtree extension in
`e92122c1`). This report captures the real capacity-margin verification
for that conversion, requested because `tools/saturn/geo_depth_manifest.py`
has no model of the iterative runtime's own frame cost and its "PASS"
alone proves nothing about capacity safety at real nesting depths.

## Summary

- **A real, previously-undocumented reentrancy hazard was found and
  fixed**, not just a frame-cost question. Converting `geo_process_held_
  object` to call `saturn_geo_walk_process_children()` made it reachable,
  in every real actor, via a real-recursion detour through still-
  unconverted "skeleton" types (`GEO_ANIMATED_PART`, `GEO_SCALE`,
  `GEO_SWITCH_CASE`) nested beneath a now-converted `Object`'s own
  `sharedChild` — a detour that, absent a guard, would silently
  reinitialize the shared `sourceboot_geo_walk_frames` array mid-drain of
  an already-active outer walk, corrupting its still-pending frames. See
  "Reentrancy hazard" below for the concrete chain, and `rendering_graph_
  node.c`'s `sSaturnGeoWalkActive` for the fix (a non-reentrancy guard
  that falls back to real recursion when reentered, exactly matching this
  subtree's pre-conversion behavior).
- **That guard has a direct, provable capacity consequence**: it confines
  every not-yet-converted type to real C recursion, entirely off
  `sourceboot_geo_walk_frames`, no matter how deep that subtree goes. So
  this wave's real, bounded-array frame cost is a small, fixed constant —
  not scene/actor-complexity-dependent at all.
- **Real number**: empirically measured (not hand-derived) peak additional
  frames from this wave, by driving the real `saturn_geo_walk_runtime.c`
  through the exact push shapes `rendering_graph_node.c`'s
  `saturn_geo_walk_enter` now produces —
  - **5 frames**, for the shape this codebase's real, verified behavior
    actually produces (`OBJECT_PARENT.node.children` and every live
    `Object.node.children` are provably always `NULL` — see "node.children
    is always NULL" below — so both types always take the runtime's
    single-subtree/combined-leave path).
  - **8 frames**, a theoretical pad assuming (contrary to the verified
    real behavior) `node.children` were non-`NULL` for both types,
    forcing the true two-subtree path.
- **Margin**: the manifest's current real numbers (`capacity=256`,
  `safety_margin=16`, `max_proven_depth=172`, from
  `build/saturn/sourceboot/generated/saturn_geo_depth_manifest.h`, which
  is itself sized as a forward-looking budget for once ALL node types are
  eventually converted) leave `256 - 16 - 172 = 68` frames of existing
  slack. Wave 3's real addition (5, or 8 padded) fits inside that slack
  with **60-63 frames to spare** — the conversion is capacity-safe today
  by a wide margin.
- **What this does NOT prove**: it does not prove the original
  ~228-frame master-stack-overrun problem is fixed for the "Mario holding
  something" scenario specifically. That scenario's `GEO_HELD_OBJECT`
  subtree still runs via real recursion today (via the guard's fallback),
  identical to its pre-wave-3 depth/behavior — this wave makes that path
  *safe* (no corruption) and *no worse*, but the real stack-depth fix for
  that specific path is deferred to a future wave that converts the
  skeleton types, at which point this guard's fallback stops firing for
  that path automatically, with no further code changes needed in
  `geo_process_object`/`_parent`/`geo_process_held_object`.

## Reentrancy hazard: the concrete chain

1. `CAMERA` is converted (wave 1). Its children are walked via
   `saturn_geo_walk_process_children()`, i.e. through the bounded array.
2. `OBJECT_PARENT` is always `CAMERA`'s child in the real scene graph
   (`rendering_graph_node.c`'s own top-of-file structural comment). Now
   converted (wave 3): reached as `CAMERA`'s child, it extends the SAME
   active walk (`saturn_geo_walk_enter`'s new
   `GRAPH_NODE_TYPE_OBJECT_PARENT` case), never starting a fresh one.
3. Live `Object` nodes are `OBJECT_PARENT`'s dynamically-linked children.
   Now converted (wave 3): same story, extends the same active walk.
4. An `Object`'s `sharedChild` is its model's skeleton root — verified
   against the real shipped `actors/mario/geo.inc.c`, `GEO_HELD_OBJECT` is
   reached ONLY via `GEO_SWITCH_CASE`/`GEO_ANIMATED_PART`/`GEO_SCALE`
   ancestors, 13 levels deep from the layout root, ALL still-unconverted
   "skeleton" types.
5. Those skeleton types dispatch through `saturn_geo_walk_dispatch_
   legacy()`'s `default:` bridge into their real, unmodified handler
   functions — genuine C recursion, entirely independent of the active
   walk's own frame bookkeeping.
6. If that real recursion reaches a `GEO_HELD_OBJECT` node,
   `geo_process_node_and_siblings`'s own switch calls
   `geo_process_held_object()` directly — which, once converted (this
   wave), would otherwise call `saturn_geo_walk_process_children()` again,
   **while the outer walk from step 1-3 is still active many real C stack
   frames up**, with real pending frame data still sitting in
   `sourceboot_geo_walk_frames[0, outer_depth)`.
   `sm64_saturn_geo_walk_runtime_init()` unconditionally resets depth to 0
   and starts pushing at index 0 of that SAME shared array — the ONLY
   production owner of these frames (`saturn_geo_walk_storage.h`) —
   silently overwriting the outer walk's still-pending frames.

This was independently confirmed via a second-opinion consult (Codex,
`codex-cli` 0.146.0, this session) before implementing the fix: "Yes, the
reentrancy analysis is correct... That is genuine memory corruption, not
merely a capacity issue," and "The guard is a reasonable minimal
transitional fix."

Fix: `sSaturnGeoWalkActive` (`rendering_graph_node.c`, just above
`saturn_geo_walk_process_children`) is set for the duration of the
outermost call only. Any call arriving while it is already `true` is, by
construction, reached via exactly this detour, and falls back to plain
`geo_process_node_and_siblings()` recursion instead — this subtree's
unconditionally-correct pre-conversion behavior, safe because it never
touches the shared array. A plain `bool` (not a counter) is sufficient:
this walk runs only on the single master SH-2 core, synchronously, with
no interrupt-driven or concurrent entry.

## node.children is always NULL (the fact the 5-frame number depends on)

Verified by tracing `struct GraphNodeObject.node.children`'s only writer:

- `init_scene_graph_node_links()` (`src/engine/graph_node.c:31`)
  unconditionally zeroes `.children`; called from `init_graph_node_object()`
  (`graph_node.c:311`), itself called only from `geo_reset_object_node`
  (`graph_node.c:688`), `try_allocate_object` (`spawn_object.c:111`), and
  one bare (non-`struct Object`) case in `mario_misc.c:594`.
- The only function anywhere that writes non-`NULL` into `.children` is
  `geo_add_child(parent, childNode)` (`graph_node.c:529-551`, write at
  line 538: `parent->children = childNode`) — a repo-wide grep for
  `.children =` / `->children =` returns exactly those two hits (line 31's
  zero-init and line 538's write), in this one file only.
- Every real call site that passes a `GraphNodeObject`'s `.node` to
  `geo_add_child` uses it as the **child** argument, never the **parent**
  argument (`graph_node.c:690`, `spawn_object.c:115`, `spawn_object.c:229`,
  `mario_misc.c:597`), so line 538 never fires for one.
- `GraphNodeObject` never enters `gCurGraphNodeList` via
  `register_scene_graph_node` either (`geo_layout.c` never calls
  `init_graph_node_object`), closing the only other path to a `geo_add_
  child(...parent...)` call with a `GraphNodeObject` as parent.

This matches the pre-conversion `geo_process_object_parent`'s own comment
("in practice they are null though") for the `OBJECT_PARENT` side, and
extends the same conclusion, independently verified, to `OBJECT`.

## Empirical probe (not just hand-derived)

Two throwaway host-side C harnesses (not part of the shipped test suite —
not committed) drove the real `src/port/saturn/runtime/saturn_geo_walk_
runtime.c` through the exact push shapes `saturn_geo_walk_enter` produces
for `OBJECT_PARENT` → 3 live `Object`s (proving list WIDTH doesn't add to
peak depth, only to total sequential work) → each `Object`'s `sharedChild`
reported `admitted=false` (modeling the reentrancy-guard-confined skeleton
subtree, which never pushes here regardless of its real depth), reading
`walk.high_water` directly:

```
$ gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/runtime \
    wave3_capacity_probe.c src/port/saturn/runtime/saturn_geo_walk_runtime.c \
    -o wave3_capacity_probe && ./wave3_capacity_probe
high_water (peak simultaneous frames) = 5
final depth = 0 (should be 0)
```

Second scenario, hypothesizing `node.children` non-`NULL` for both types
(contrary to the verified real behavior above), forcing the true
two-subtree path:

```
high_water (peak simultaneous frames) = 8
final depth = 0 (should be 0)
```

Both probes returned `depth == 0` at completion (no stranded frames) and
matched their hand-derived predictions once OBJECT_PARENT's own sibling
under `CAMERA` (a real scene-graph fact) was included in the first probe
— an earlier hand-trace omitted it and predicted 5 where the first,
sibling-less probe run actually measured 4; re-adding the real sibling
reconciled hand-trace and probe at 5. This discrepancy is exactly why the
task's own standard ("must do real verification, not just note the risk")
matters in practice, not just in principle.

These two constants (`WAVE3_REALISTIC_PEAK_FRAMES = 5`,
`WAVE3_PADDED_PEAK_FRAMES = 8`) are now encoded, with this same derivation,
in `tools/saturn/test_geo_depth_manifest.py`'s
`test_wave3_two_subtree_object_chain_has_real_capacity_margin`, which
asserts `capacity - safety_margin - max_proven_depth >= 8` against the
manifest freshly computed from this repository's real `actors/`/`levels/`
content (not hardcoded numbers), so the check stays meaningful if that
content changes.

## Corrected estimate for a future wave

The originating review estimated "up to 3x the frames per level" for
`child`-direction two-subtree nesting versus the old single-child path.
Measured directly from `saturn_geo_walk_runtime.c`'s `run()`: the real
worst-case factor is **up to 2x**, not 3x — a two-subtree node's true
two-subtree path costs `second_child + boundary + child` = 3 frames (the
old single-child path's typical cost was `leave + child` = 2 frames), a
1.5x-2x factor depending on whether a final leave is also required, not
3x. This matters for whoever eventually converts the skeleton types: that
wave should re-run this same empirical-probe method (not re-guess the
multiplier) once the guard's fallback stops confining the held-object
path, since at that point the "5 frames, scene-independent" conclusion
above no longer holds and the real cost WILL start scaling with actor
skeleton depth.
