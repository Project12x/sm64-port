# Task 4 live-Mario bridge fallback diagnostic — 2026-07-28

Status: **bridge seam diagnostic; not gallery-accepted**.

This build permits the read-only bridge to remain live when `gMarioState`
exists before its graph object: the fallback selects the neutral generated
pose until the animation object returns. The paired capture still reports
`demo_actor_snapshot_valid=0`, `demo_actor_pose_vertices=0`, and
`demo_actor_primitives_emitted=0` at `frame_serial=51`; the route probe is
`SBR1` at `replay_ticks=25` (not the 600-tick endpoint). This indicates that
the captured phase has not yet published authoritative Mario state, rather
than proving a projection or handoff defect.

The screenshot is retained as diagnostic evidence only and must not be
promoted to `docs/saturn/evidence/index.html` or `TIMELINE.md`.
