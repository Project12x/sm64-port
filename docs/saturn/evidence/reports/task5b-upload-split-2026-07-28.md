# Task 5b VDP1 upload split — 2026-07-28

The dual demo path now splits the LWRAM command-table copy into disjoint
master/slave word ranges. The master still performs the existing VDP1 sync
wait and `force_put`; a timeout falls back to a complete master copy before
the commit. Non-LWRAM and serial builds retain the existing backend upload.

- Build: `e2-bob-demo-replay-r2048-slave1-poly0-hot0`
- Capture: 3,600 frames, fresh CUE, `--dram-cart`, fresh
  `_sourceboot_fast3d=0x060d314c`
- Screenshot: `../screenshots/task5b-upload-split-2026-07-28.png`
- Comparator: `task5b-upload-split-compare-2026-07-28.json`
- Visual description: the frame contains a small red/blue/green humanoid
  figure near the center, pale tiled terrain at left, a green terrain band at
  right, and a bright orange/white background band.

Decoded profile counters:

| Counter | Value |
| --- | ---: |
| `frame_serial` | 249 |
| `sim_tick_count` | 996 |
| `triangles_vdp1_emitted` | 141,063 |
| `demo_actor_primitives_emitted` | 136,970 |
| `slave_jobs_completed` | 1,245 |
| `slave_busy_ticks` | 5,952,214 |
| `master_wait_ticks` | 33,504 |
| `slave_timeouts` | 0 |
| `fault_flags` | 0 |
| `render_frt_ticks_accum` | 14,677,538 |
| `vdp1_commands_last` | 568 |

The paired route comparator is deterministic: checkpoint SHA-256 remains
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`, with
`replay_ticks=600`, `global_timer=601`, zero faults, rejects, and renderer
counter deltas. The screenshot is evidence only; owner visual acceptance is
still required before gallery promotion.
