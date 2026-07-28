# Task 5 SBR2 one-dispatch checkpoint — 2026-07-28

This is a diagnostic performance report, not a pass. Both images were built
from the same working tree and feature profile (`view_radius=6000`,
`poly_tier=0`, `near_clip=1`, BSP order on, fragments off), with fresh symbol
resolution and the standard `--dram-cart`, `--bios-input`,
`--handoff-yield`, and 1,700-second capture budget.

| Metric | Serial | Dual |
| --- | ---: | ---: |
| SBR2 replay ticks | 600 | 600 |
| Frame serial | 150 | 150 |
| Render FRT accumulator | 3,887,383 | 5,548,511 |
| Render FRT last | 19,357 | 5,998 |
| Worker jobs | 0 | 150 |
| Master wait FRT | 0 | 12,986 |
| Slave busy FRT | 0 | 1,059,980 |
| Compact results (master / slave) | — | 20,585 / 15,494 |
| Faults / timeouts / command rejects | 0 / 0 / 0 | 0 / 0 / 0 |

The route comparator is deterministic and the dual producer is useful, but the
dual render accumulator is **42.7% slower** than serial. The current Task 5
slice still performs transform before dispatch, so Task 5A is a hard stop
before VDP1 banking, hot promotion, or LOD work.

The paired route comparator is
[`task5-sbr2-current-compare-2026-07-28.json`](task5-sbr2-current-compare-2026-07-28.json).
The final-frame screenshots were retained as diagnostic captures only; they
are not gallery milestones or owner-accepted visual evidence.
