# Task 5b primitive-classification split — 2026-07-28

The dual-worker path now parallelizes the post-transform primitive visibility
and depth-bucket classification pass. Each worker owns a disjoint primitive
range and writes only its indexed visibility/bucket byte; the master performs
the final bucket scatter in source order before command emission, so draw order
and command ownership are unchanged.

Fresh dual-worker capture (`-r2048/-poly0`, 3,600 capture frames, DRAM cart,
BIOS handoff-yield) produced:

- `frame_serial=248`, `sim_tick_count=995`, `fault_flags=0`;
- `slave_jobs_completed=496`, `slave_busy_ticks=3,395,680`,
  `master_wait_ticks=24,703`, `slave_timeouts=0`;
- `render_frt_ticks_accum=15,029,161` (22.6% measured slave share);
- `triangles_vdp1_emitted=140,498`, with the expected BOB/Mario image.

The change is a measurable but insufficient utilization lever: share rises
from the prior ~20% baseline to ~22.6%, still below the owner's ≥50% gate.
The remaining dominant cost is command emission and texture/VRAM setup, which
still runs on the master.

The paired route comparator passes with identical checkpoint hash
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`,
`replay_ticks=600`, `global_timer=601`, zero faults, and zero capacity rejects.

Evidence: [report](reports/task5b-classify-2026-07-28.json),
[screenshot](screenshots/task5b-classify-2026-07-28.png),
[comparator](reports/task5b-classify-compare-2026-07-28.json).
