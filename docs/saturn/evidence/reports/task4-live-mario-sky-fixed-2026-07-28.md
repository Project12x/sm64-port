# Task 4 live Mario + sky capture — 2026-07-28

Fresh visual evidence for the current `e2-bob-demo-replay-r2048-slave1-poly0-hot0`
build. The capture used the BIOS handoff-yield sequence, `--dram-cart`, a
freshly resolved `_sourceboot_fast3d` probe at `0x060D11E0`, and the route
checkpoint probe at `0x060D1158`.

The frame visibly contains an identifiable Mario actor over the baked BOB sky
and terrain. The decoded profile reports `frame_serial=802`,
`demo_actor_snapshot_valid=1`, `demo_actor_pose_vertices=424`, and
`fault_flags=0`; `render_frt_ticks_last=60421` and `vdp2_display_mask=10` are
retained for the performance and VDP2 ledgers. This is a fresh visual
milestone, not a gallery acceptance: owner visual confirmation is still
required before promotion to `TIMELINE.md` or `evidence/index.html`.

Paired files:

- Screenshot: `../screenshots/task4-live-mario-sky-fixed-2026-07-28.png`
- Machine report: `task4-live-mario-sky-fixed-2026-07-28.json`
