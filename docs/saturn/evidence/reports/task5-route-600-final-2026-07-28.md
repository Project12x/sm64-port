# Task 5 600-tick demo-route capture — 2026-07-28

Status: **checkpoint reached; parity gate failed; not gallery-accepted**.

The capture uses the current demo/replay ELF, `--dram-cart`, and fresh symbols
`_sourceboot_fast3d=0x060d2078` and
`_sourceboot_route_checkpoint=0x060d2010`. It reaches the exact route endpoint
(`SBR1`, `replay_ticks=600`, `fault_flags=0`, `command_capacity_rejects=0`).

The demo checkpoint does not match the retained interpreted baseline
`f0687607…`: the demo reports `global_timer=602` (baseline 744), a different
Mario action/position signature, and `triangles_transformed=3393 /
triangles_vdp1_emitted=1118` (baseline 2068/542). This is a real Task 5
authority failure, not a visual acceptance. The endpoint screenshot is kept
for diagnosis only and must not be promoted to the gallery or timeline.
