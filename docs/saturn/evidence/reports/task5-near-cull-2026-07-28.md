# Task 5 near-plane cull visual milestone — 2026-07-28

Status: **first identifiable Mario diagnostic; not gallery-accepted**.

The shared IR transform now rejects vertices on or behind the near plane
instead of clamping their reciprocal. On the current demo/replay image (fresh
symbols `_sourceboot_fast3d=0x060d1fd0` and
`_sourceboot_route_checkpoint=0x060d1f68`), the paired probe reports
`frame_serial=51`, `demo_actor_snapshot_valid=1`, `demo_actor_pose_vertices=424`,
`demo_actor_vertices_valid=10600`, `demo_actor_primitives_emitted=13921`, and
`fault_flags=0`. The screenshot now contains an identifiable Mario actor and
textured BOB terrain; the remaining black/fragmented terrain is an open
renderer-composition issue. The route is live but only at `replay_ticks=25/600`.

This image is retained as a visual milestone for the implementation log. It
must not be promoted to `docs/saturn/evidence/index.html` or `TIMELINE.md`
until the owner confirms the frame and the route gate is satisfied.
