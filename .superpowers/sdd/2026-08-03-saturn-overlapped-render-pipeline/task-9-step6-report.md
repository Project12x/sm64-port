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
  `tools/saturn/vdp2_frame_contract_test.c`.
- GREEN: `tools/saturn/test_sourceboot_presentation_boundary.py`, 7/7 tests,
  including mutations that detach the VDP2 simulation/render/camera ownership.
- GREEN: direct host GCC compilation/execution of
  `tools/saturn/runtime_contract_test.c` with the production VDP2 module.
- GREEN: `git diff --check`.

## Documentation and gates

Updated `CHANGELOG.md`, the A9 design specification, engine architecture,
Task 9 brief, and a source-evidence report. The mutable execution ledger will
receive the commit and test record after the commit and remains deliberately
unstaged because it contains mixed work.

Independent rereview has not been run. No target build, target capture, Ymir
launch, manual observation, or native-math gate was run or claimed.
