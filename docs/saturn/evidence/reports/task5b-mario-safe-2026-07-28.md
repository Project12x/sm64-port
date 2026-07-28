# Task 5b Mario command-emission split — 2026-07-28

This is a fresh dual-worker sourceboot capture after moving only the
post-transform Mario command encoding behind the existing dual-worker
boundary. The proven Mario transform/visibility loop remains on the master,
and the original emission loop remains the fallback if reservation or worker
completion fails.

- Build: `e2-bob-demo-replay-r2048-slave1-poly0-hot0`
- Capture: 3,600 frames, fresh CUE, `--dram-cart`, fresh
  `_sourceboot_fast3d=0x060d308c`
- Screenshot: `../screenshots/task5b-mario-safe-2026-07-28.png`
- Route comparator: `task5b-mario-safe-compare-2026-07-28.json`
- Visual description: the frame contains a small red/blue/green humanoid
  figure near the center, pale tiled terrain at left, a green terrain band at
  right, and a bright orange/white background band.

Decoded profile counters at the retained stop:

| Counter | Value |
| --- | ---: |
| `frame_serial` | 248 |
| `sim_tick_count` | 995 |
| `triangles_vdp1_emitted` | 140,498 |
| `demo_actor_vertices_valid` | 105,152 |
| `demo_actor_primitives_emitted` | 136,428 |
| `slave_jobs_completed` | 992 |
| `slave_busy_ticks` | 4,646,864 |
| `master_wait_ticks` | 31,871 |
| `slave_timeouts` | 0 |
| `fault_flags` | 0 |
| `render_frt_ticks_accum` | 14,868,977 |

The prior direct-emission capture emitted the same triangle and Mario counts;
the new capture adds the actor worker jobs and raises measured slave work.
The route comparator remains deterministic: checkpoint SHA-256 is
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`, with
`replay_ticks=600`, `global_timer=601`, zero faults, rejects, and renderer
counter deltas.

This report is evidence only. The screenshot is not gallery-promoted until
the owner accepts the visual milestone.
