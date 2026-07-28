# Task 5b Gouraud emission split — replay capture

Date: 2026-07-28  
Builds: `SATURN_SOURCEBOOT_ROUTE_REPLAY=1`, BOB `r6000`, BSP fragments,
Gouraud terrain emission split; serial and dual-SH2 variants.

## Results

| variant | emulated frames | wall seconds | emulator speed ratio | frame serial | sim ticks | render FRT ticks | slave jobs | master wait | slave busy | faults | Gouraud overflow |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| serial | 5,340 | 62.46 | 1.425 | 74 | 296 | 1,900,370 | 0 | 0 | 0 | 0 | 0 |
| dual | 5,340 | 63.11 | 1.410 | 58 | 233 | 2,120,470 | 232 | 282,521 | 5,895,847 | 0 | 0 |

The capture plumbing is healthy: fresh `_sourceboot_fast3d` symbols were
probed (`0x060C6188` serial, `0x060CD188` dual), the CUE mtimes are newer
than their ELFs, and both profiles decode completely with no renderer fault
or Gouraud-bank overflow.

This is **not** an FPS comparison. The replay runs did not reach the same
simulation endpoint (296 vs 233 ticks), so `frame_serial` cannot be
differenced as a matched A/B rate. The dual profile does prove that the
worker path is active (`slave_jobs_completed=232`) and exposes nonzero
master/slave scheduling counters. Re-establishing an identical replay
checkpoint is the next Task 5b gate before attributing a performance delta.
