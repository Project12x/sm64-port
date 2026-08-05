# Task 9 Step 6 report — VDP2 generation coherence

Status: SOURCE COMPLETE

## Result

VDP2 now accepts only an immutable camera snapshot plus a terminal
`(displayed, rendered, simulation)` generation record. Sourceboot builds that
record after publish acknowledgement, using the published VDP1 bank for the
displayed/rendered camera ownership and the frame scheduler for the current
authoritative simulation generation. The HUD displays `GEN D <n> R <n> S <n>`.

The VDP2 module remains geometry-free. It rejects zero or mismatched
camera/displayed/rendered metadata before sky, HUD, layer, or VBlank callbacks.
When the generation record changes, HUD output refreshes immediately rather
than waiting for the regular metric rate limit, so the sky and labels switch
together. The metric-rate clock itself is not reset by that forced refresh.

The implementation is an internal ownership-contract repair. No external code
was adapted: the project's pinned scheduler and VDP1 bank contracts already
define the relevant generation ownership.

## TDD and focused evidence

- RED: the VDP2 fixture first failed to compile because camera generation,
  generation-state metadata, and the extended VDP2 begin API did not exist.
- RED: after the first implementation, the fixture caught the real stale-HUD
  defect: a changed sky composition at tick 15 did not refresh the HUD before
  its tick-30 metric interval.
- GREEN: direct host GCC compilation/execution of
  `tools/saturn/vdp2_frame_contract_test.c`, including Fix Round 1 checks that
  zero displayed or simulation generations make no backend callbacks.
- GREEN: `tools/saturn/test_sourceboot_presentation_boundary.py`, 7/7 tests,
  including mutations that detach the VDP2 simulation/render/camera ownership.
- GREEN: direct host GCC compilation/execution of
  `tools/saturn/runtime_contract_test.c` with the production VDP2 module.
- GREEN: `git diff --check`.

## Documentation and gates

Updated `CHANGELOG.md`, the A9 design specification, engine architecture,
Task 9 brief, source evidence, governing active plan, and `STATE.md`. The
execution ledger records this transition through a surgical cached hunk; mixed
native-math work remains unstaged.

Independent review of `2377bf8b` was specification **NO-GO** solely for the
missing plan/ledger transition and code **APPROVED WITH MINOR FOLLOW-UP**.
Fix Round 1 corrects those records and closes the direct zero-generation test
gap; focused rereview remains pending. No target build, target capture, Ymir
launch, manual observation, or native-math gate was run or claimed.
