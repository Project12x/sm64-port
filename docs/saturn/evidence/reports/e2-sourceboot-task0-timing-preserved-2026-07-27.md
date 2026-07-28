# Sourceboot Task 0 timing evidence — 2026-07-27

Build: route-replay sourceboot (`SATURN_SOURCEBOOT_ROUTE_REPLAY=1`), rebuilt
after the timing-preservation change. The CUE was regenerated from the
resulting ISO and passed the stale-image preflight.

Capture: [`e2-sourceboot-task0-timing-preserved-2026-07-27.json`](e2-sourceboot-task0-timing-preserved-2026-07-27.json)

- ELF symbol: `_sourceboot_fast3d = 0x060C0FAC`
- Profile probe: 264 bytes
- FRT conversion: `CPU_FRT_NTSC_320_8_COUNT_1MS = 0x0D1F = 3359` ticks/ms
- `sim_tick_count`: 122
- `sim_frt_ticks_accum`: 3,658,971
- Average simulation phase: **8.93 ms/tick**
- Last simulation phase: **4.57 ms**
- Last render phase: **15.08 ms**
- `fault_flags`: 0

The capture stopped at a frame boundary (`frame_serial=123`), so the
last-frame renderer counters are not used as a rate. The timing fields are
explicitly preserved across the frontend's normal per-submit profile clear;
the accumulated simulation counter is therefore valid for the completed
ticks observed in this capture.

## Decision gate

The measured simulation average is below the plan's ~15 ms threshold. Proceed
with the demo-path renderer work unchanged; sim-side soft-float replacement is
not a prerequisite. Revisit sim optimization only if later renderer integration
changes the measured phase or the 15 FPS exit gate remains unreachable.

## Verification

- Route-replay cross-build: `make SATURN_SOURCEBOOT_ROUTE_REPLAY=1 -j2 verify`
- Host runtime contracts: direct MinGW compile and executable pass
- Profile decode: complete 264-byte layout, no missing fields
