# A9 Step 6: VDP2 generation coherence

Date: 2026-08-05

Scope: host/source verification only. No target build or Ymir launch was run.

## Contract

`sourceboot_present_generation()` builds one VDP2 metadata record at the
terminal presentation boundary. Its displayed and rendered values are the
published VDP1 bank generation, its camera snapshot carries that same bank
generation, and its simulation value is copied from the frame scheduler at the
same boundary. The VDP2 frame API rejects a zero or mismatched
camera/displayed/rendered record before any sky, HUD, layer, or VBlank callback.
It labels accepted HUD text as `GEN D <displayed> R <rendered> S <simulation>`.
Changing that record forces a HUD write even before the normal metric refresh
interval, so the new sky cannot be shown beside stale generation labels.

The simulation number is intentionally not constrained to a fixed delta from
the displayed bank: a missed deadline can reuse the previous completed bank
after the scheduler has consumed its bounded recovery work. Naming the actual
scheduler generation makes that lead explicit without giving VDP2 live game or
geometry access.

## Evidence

- RED: the added VDP2 contract fixture did not compile because camera
  generation metadata, the VDP2 generation state, and the extended begin API
  did not yet exist.
- GREEN: direct host GCC compilation and execution of
  `tools/saturn/vdp2_frame_contract_test.c` succeeds. It verifies the HUD
  tuple, immediate tuple-refresh alongside a changed immutable-camera sky
  scroll, and that a stale render generation performs no VDP2 callbacks.
- GREEN: direct host GCC compilation and execution of
  `tools/saturn/runtime_contract_test.c` succeeds with the production VDP2
  module.
- GREEN: `.venv-saturn-tools\\Scripts\\python.exe
  tools/saturn/test_sourceboot_presentation_boundary.py` passes 7/7,
  including mutations that detach VDP2 simulation/render/camera generation
  ownership.

## Remaining gates

Independent rereview, target compile, target capture, manual Ymir observation,
broad native-math census, and broader Task 9 gates are not claimed by this
source-only evidence.
