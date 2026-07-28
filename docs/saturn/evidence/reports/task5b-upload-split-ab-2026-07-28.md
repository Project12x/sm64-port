# Task 5b upload-split A/B — 2026-07-28

This is the same-commit route-parity pair for the VDP1 command-table upload
split. Both images use `-r2048/-poly0/-hot0`, fresh CUEs, `--dram-cart`, and
the same 3,600-frame capture window.

| Profile | `frame_serial` | `sim_tick_count` | render FRT/frame | slave busy | faults |
| --- | ---: | ---: | ---: | ---: | ---: |
| dual (`slave1`) | 249 | 996 | ~58,946 | 5,952,214 total | 0 |
| serial (`slave0`) | 246 | 985 | ~7,527 | 0 | 0 |

The route comparator (`task5b-upload-split-ab-compare-2026-07-28.json`)
reports identical checkpoint SHA-256
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`,
`replay_ticks=600`, `global_timer=601`, zero faults/rejects, and zero
renderer-counter deltas. The paired screenshots are
`task5b-upload-split-2026-07-28.png` and
`task5b-upload-split-serial-2026-07-28.png`.

The large dual render interval is consistent with the existing worker and
shared-bus cost; this pair is parity/utilization evidence, not a claim that
the 15 FPS gate has been met.
