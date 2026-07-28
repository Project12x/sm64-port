# Task 5b route-parity gate — 2026-07-28

The serial and dual-SH2 replay captures now use the frozen route-checkpoint
probe, rather than comparing their moving final profiles.

| field at replay tick 600 | serial | dual |
|---|---:|---:|
| route magic/version | `SBR1` / 1 | `SBR1` / 1 |
| replay ticks | 600 | 600 |
| `global_timer` | 601 | 601 |
| Mario position bits | `0x28000000, 0xC5A7B8B0, 0x80000000` | identical |
| camera mode | 1 | 1 |
| triangles transformed | 854,580 | 854,580 |
| triangles VDP1 emitted | 110,958 | 110,958 |
| fault flags / capacity rejects | 0 / 0 | 0 / 0 |

The route identity gate therefore passes exactly. The serial capture used
8,700 emulator frames; dual used 9,300 because it needed one additional
post-route chunk to reach tick 600. Both captures used fresh frontend and
route-probe symbols, `--dram-cart`, and CUE images newer than their ELFs.

This closes the prior “different endpoint” blocker. The profile block is
still sampled after the route (and therefore has different final
`frame_serial`/dispatch totals); the next performance capture should append
the relevant timing and worker counters to the route checkpoint itself before
claiming a serial/dual FPS delta.
