# Task 5 textured BOB diagnostic — 2026-07-27

Status: **diagnostic partial; not gallery-accepted**.

The fresh demo-variant capture uses the ELF/CUE pair recorded in the JSON report, `--dram-cart`, BIOS automation, `--handoff-yield`, `--post-poke-frames 3600`, and a freshly resolved `_sourceboot_fast3d` probe at `0x060D05BC`. The screenshot visibly contains the baked BOB CLUT16 terrain bank.

Probe decode: `frame_serial=52`, `triangles_transformed=3938`, `triangles_emitted=1706`, `triangles_vdp1_emitted=775`, `reject_vdp1_arena_capacity=0`, `gouraud_bank_overflow=0`, `render_frt_ticks_last=38683`.

This does not satisfy the plan's `task5-textured-bob` acceptance gate: animated Mario is not present and the camera/geometry presentation is malformed. Keep the PNG and paired report as failed/diagnostic evidence; do not promote it to `evidence/index.html` or `TIMELINE.md` until a clean terrain-plus-Mario frame is captured and visually confirmed.
