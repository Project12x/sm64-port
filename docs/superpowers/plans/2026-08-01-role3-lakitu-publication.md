# Role-3 Lakitu Publication Repair

## Goal

Repair the fixed-camera compatibility publication seam exposed by the
role-3 endpoint capture: pipeline 2 consumes `gLakituState`, while role 3
returns before the original camera path synchronizes it. The repair must keep
the fixed candidate free of runtime fallback into the source camera update.

## Task 1: publish compatible Lakitu state for role 3

- Write a focused host contract test first and observe it fail.
- Adapt only the required original camera synchronization semantics so a role-3
  fixed `Camera` publication also updates the `gLakituState` position, focus,
  mode, yaw/pitch, and distance fields consumed by the Saturn actor bridge.
- Do not invoke the source camera update body, add a float fallback, or add BOB
  special cases.
- Keep source and bypass roles behaviorally unchanged.
- Run focused role/fixed-camera contracts and commit.

## Task 2: visual confirmation

- Build the same role-3 stage-8/pipeline-2 artifact serially only if source
  changes require it, then capture the exact BOB route tick 2000 with the
  identity-aware visual tool.
- The PNG must contain non-sky scene geometry before it is described as a
  visual improvement; retain the all-blue capture as the rejected predecessor.
- Report this as emulator visual evidence only; do not claim FPS, feel, or
  retail behavior.
