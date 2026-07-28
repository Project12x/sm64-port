# Task 5 interpreted 600-tick route oracle — 2026-07-28

Status: **same-commit authority oracle; visual diagnostic; not gallery-accepted**.

This is the `SATURN_DEMO_PATH=0` replay build from the same commit as the
demo capture, using fresh symbols `_sourceboot_fast3d=0x060c1478` and
`_sourceboot_route_checkpoint=0x060c1410`. It reaches `replay_ticks=600` with
`fault_flags=0` and `command_capacity_rejects=0`; its checkpoint is the
authority comparison target for the paired demo capture, not the older
`f0687607…` report.

The paired comparator reports source-checkpoint drift and different renderer
counters in the demo build. This screenshot is retained to document the
same-commit baseline and is not promoted to the gallery or timeline.
