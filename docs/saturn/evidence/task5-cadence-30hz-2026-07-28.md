# Task 5 cadence gate — 2026-07-28

The demo scheduler now advances the authoritative source simulation from the
VBlank-out counter, with a two-VBlank credit per source tick. Catch-up ticks
suppress only display-list submission and the presentation wait; simulation
state and `gGlobalTimer` remain source-owned.

Fresh dual-worker capture (`-r2048/-poly0`, 3,600 capture frames, 3,600
post-poke frames, DRAM cart, BIOS handoff-yield) produced:

- `frame_serial=249`, `sim_tick_count=996`, `fault_flags=0`;
- `slave_jobs_completed=249`, `slave_timeouts=0`;
- `sim_frt_ticks_accum=35,289,691`, `render_frt_ticks_accum=14,797,786`;
- actor snapshot valid with 424 pose vertices.

The paired frozen BOB route comparator against the prior cadence capture passes
with identical checkpoint hash
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`,
`replay_ticks=600`, `global_timer=601`, zero faults, and zero capacity rejects.

Evidence: [report](reports/task5-cadence-30hz-2026-07-28.json),
[screenshot](screenshots/task5-cadence-30hz-2026-07-28.png),
[comparator](reports/task5-cadence-compare-2026-07-28.json).

This closes the scheduler ownership/catch-up gate; the measured sim counter is
reported alongside the render counter because the emulator's capture-frame
clock is not the source VBlank clock.
