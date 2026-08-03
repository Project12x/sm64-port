# Task 8 — hysteretic terrain LOD before material work

## Scope and provenance

- Worktree/starting HEAD: `sm64-port/.worktrees/sh2-native-math-purge`,
  `79923cea2cf40f8903c9dd214216477a25a91974`.
- Prior art: [Maxime-XL2/SONIC-Z-TREME](https://github.com/Maxime-XL2/SONIC-Z-TREME),
  `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0.
- Inspected reference record/files: `LICENSE`, `README.md`,
  `Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c:439-480`, and
  `ZT_LOADING.c:148-172`; the project ledger additionally records the related
  `ZT_FRUSTUM.c`, `workarea.c`, and `ZTE_DEF.H` study.
- Reuse mode for this task: **pattern-only**. The new small policy API is
  original project code. The upstream SGL model/package structures do not fit
  the generated BOB primitive, source-identity, route-prefix, and VDP1
  ownership contracts. Existing GPL attribution in
  `src/port/saturn/gpl/ztreme_hot_promotion.{c,h}` is retained.

## Implementation

- Added a host-testable NEAR/MID/FAR controller with both depth and projected
  screen-span enter/exit windows. A primitive cannot become distant solely
  because of depth when its visible span remains large.
- The renderer computes the tier before clipping and material/template work.
  Role `0` is the visual reference, role `1` can reduce only expensive
  textured material work, and role `2` additionally permits FAR suppression.
  Sourceboot validates the build role is exactly `0`, `1`, or `2`, and its
  existing output tag retains `-poly0`, `-poly1`, or `-poly2`.
- FAR suppression now goes through one policy predicate that requires all of:
  role 2, FAR, a bake-approved optional primitive, and an ID outside the
  mandatory route prefix. MID never suppresses geometry. Counters remain
  diagnostic only; no counter value or performance percentage is a promotion
  condition.
- `saturn_lod_reset()` clears all tier state deterministically. Sourceboot
  now observes the authoritative `gCurrentArea`, `gCurrLevelNum`, and
  `gCurrAreaIndex` inside every `game_loop_one_iteration()` wrapper, before a
  catch-up batch advances to its next tick. An area unload and a subsequent
  same-ID re-entry both reset tier history before the next render.

## Host validation

The test was written first and was observed to fail at compile time because
the controller interface did not yet exist. After implementation, native host
GCC (`C:\\Qt\\Tools\\mingw1310_64\\bin\\gcc.exe`) built and ran:

```text
hot promotion contract: PASS
```

It proves threshold hysteresis, camera-jitter stability, projected-size
rejection of a depth-only downgrade, FAR→MID→NEAR transition behavior,
deterministic multi-tick scene exit/re-entry and active-scene-change resets,
normal same-scene no-reset behavior, mandatory-prefix preservation, bake
approval, and role-limited material degradation (including invalid roles).
`git diff --check` also passed. A source-order check confirms tier selection
precedes construction of the clipping input, and the renderer rejects invalid
roles at compile time. `tools/saturn/test_task8_lod_source.py` permanently
extracts the complete Sourceboot tick helper by balanced braces, checks that
the observer follows the game tick with only the required comments and
preprocessor guard between them, and rejects both an inserted statement and a
nested matching preprocessor block. It also forbids a batch-level observer
that could mask an exit/re-entry.

No MSYS, Bash, `sh-elf-*`, target build, Ymir, or target replay was invoked.
Target replay and visual verification of the three tagged builds remain
pending until the DLL/toolchain environment is repaired.
