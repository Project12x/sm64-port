# Role-3 Visual Capture Plan

## Goal

Produce one truthful, bounded Ymir PNG and JSON sidecar for the already
validated fixed-camera role-3 BOB artifact. This evidence establishes visual
output only; it must not imply FPS, camera-feel acceptance, or retail
validation.

## Task 1: make route-view capture identity-aware

- Add host coverage first for role-3/pipeline-2 report identity.
- Extend the existing route-view tool only as needed to accept and record the
  selected renderer pipeline and camera role, and to reject a mismatched ELF
  variant marker.
- Preserve legacy pipeline-8 behavior and filenames.
- Use the existing symbol resolver conventions; the role-3 capture must bind
  to camera variant `3` and route marker `1`.
- Commit the tool and tests. Do not launch Ymir in this task.

## Task 2: one bounded visual capture

- Use the pinned role-3 stage-8/pipeline-2 CUE and its sibling ELF.
- Use the `bob-default-camera-v1` route, not `bob-parity-v1`; capture exactly
  route tick 2000 and one PNG with a same-basename JSON identity sidecar.
- Validate CUE/ISO/ELF hashes against the recorded role-3 capture evidence
  before launch. Do not rebuild.
- Report Ymir as emulator-only visual evidence. Do not report FPS, feel, or
  retail behavior.
