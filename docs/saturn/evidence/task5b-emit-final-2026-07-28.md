# Task 5b direct command-emission split — 2026-07-28

The dual-worker renderer now preassigns the source-order BOB command slots
after classification. The master and slave encode disjoint slot ranges in
parallel; the VDP1 arena cursor is reserved once by the master, and Mario is
still appended afterward. Textured CLUT setup is worker-safe; untextured worker
slots use flat RGB1555 so Gouraud-bank allocation remains master-owned.

Fresh dual-worker capture (`-r2048/-poly0`, 3,600 capture frames, DRAM cart,
BIOS handoff-yield) produced:

- `frame_serial=248`, `sim_tick_count=994`, `fault_flags=0`;
- `triangles_vdp1_emitted=140,498`, `texture_commands=4,070`;
- `slave_jobs_completed=744`, `slave_busy_ticks=3,754,302`,
  `master_wait_ticks=26,410`, `slave_timeouts=0`;
- `render_frt_ticks_accum=15,468,012`, or **24.3% measured slave share**.

This is a real but insufficient improvement over the prior ~22.6% split. The
remaining utilization gap is now specifically the master-owned Gouraud/Mario
path and the final VDP1/VDP2 submission boundary; the ≥50% owner gate remains
open.

The route comparator passes with identical checkpoint hash
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`,
`replay_ticks=600`, `global_timer=601`, zero faults, and zero capacity rejects.

Evidence: [report](reports/task5b-emit-final-2026-07-28.json),
[screenshot](screenshots/task5b-emit-final-2026-07-28.png),
[comparator](reports/task5b-emit-final-compare-2026-07-28.json).
