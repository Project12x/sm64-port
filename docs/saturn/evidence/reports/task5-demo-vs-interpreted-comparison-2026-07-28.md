# Task 5 demo-vs-interpreted route comparison — 2026-07-28

Status: **failed authority gate**.

The paired comparator consumed the route block from each capture's
`extra_probe_window` and rejected the demo build:

- source checkpoint signatures differ;
- demo `global_timer=602` vs interpreted `611`;
- Mario action/position signature differs;
- demo `triangles_transformed=3393`, `triangles_vdp1_emitted=1118` vs
  interpreted `2074/531`.

Both captures reached `replay_ticks=600` with zero fault and capacity rejects.
The JSON result is retained as the authoritative Task 5 blocker; no gallery
entry is warranted.
