# Task 5 frozen 600-tick demo endpoint — 2026-07-28

Status: **authority pass; visual diagnostic; not gallery-accepted**.

The route checkpoint is frozen on the first complete replay publication, so
fixed wall-frame tails cannot overwrite it. The demo and interpreted builds
now share the exact endpoint: `replay_ticks=600`, `global_timer=601`,
`mario_action=0x04000440`, identical Mario position bits/camera mode, and
zero fault/capacity rejects. The endpoint screenshot remains a renderer
diagnostic; owner visual confirmation is still required before gallery entry.
