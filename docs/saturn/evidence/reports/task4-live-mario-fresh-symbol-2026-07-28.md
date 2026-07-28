# Task 4 live-Mario bridge fresh-symbol capture — 2026-07-28

Status: **bridge live; visual diagnostic; not gallery-accepted**.

This capture uses fresh symbols from the current ELF
(`_sourceboot_fast3d=0x060d1fd0`, `_sourceboot_route_checkpoint=0x060d1f68`).
The paired probe confirms the bridge is consuming authoritative state:
`frame_serial=51`, `demo_actor_snapshot_valid=1`, `demo_actor_pose_vertices=424`,
`demo_actor_vertices_valid=10600`, and
`demo_actor_primitives_emitted=13921` (the latter counters persist across the
frontend profile reset). The route block is live at `SBR1`, but this short run
only reaches `replay_ticks=25/600`.

The screenshot still does not provide a clean, identifiable Mario view; the
terrain composition remains diagnostic. This artifact is therefore not a
visual-gate pass and must not be promoted to `docs/saturn/evidence/index.html`
or `TIMELINE.md`.
