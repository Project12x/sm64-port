# Task 6 view-radius sweep — 2026-07-28

Status: **measured degradation sweep; no cadence win; not gallery-accepted**.

All three captures use the demo/replay profile, fresh probes, `--dram-cart`,
and the same 3,600-frame post-poke budget:

| Radius | Frame serial | Sim FRT last | Render FRT last | Transformed | Emitted |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 6,000 | 53 | 25,829 | 11,618 | 368 | 105 |
| 4,096 | 53 | 25,829 | 8,133 | 368 | 105 |
| 2,048 | 53 | 25,829 | 4,218 | 368 | 105 |

The radius reduces render work but does not improve observed frame cadence;
simulation is the measured limiter for this phase. The 2,048 frame is retained
as a diagnostic visual, not a gallery candidate. Further performance work must
target the simulation/dual-SH2 path rather than claiming a view-distance FPS
win.
