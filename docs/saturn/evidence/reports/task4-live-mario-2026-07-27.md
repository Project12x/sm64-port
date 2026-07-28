# Task 4 live-Mario diagnostic — 2026-07-27

Status: **diagnostic partial; not gallery-accepted**.

The demo renderer consumed the authoritative `gMarioState`/`gMarioObject` bridge and emitted a live pose pass. The fresh capture used the demo ELF/CUE pair recorded in the JSON report, `--dram-cart`, BIOS automation, `--handoff-yield`, `--post-poke-frames 3600`, and a freshly resolved `_sourceboot_fast3d` probe.

The bridge snapshot was valid at the capture stop: Mario was approximately `(-6558, 0, 6464)`, while the authoritative Lakitu camera was approximately `(-6562, 125, 6459)` with focus `(-7208, 264, 7050)`. That puts the actor inside the renderer's 128-unit near clip during the intro camera, explaining the absence of visible Mario without justifying a camera hack.

The frame is retained as evidence of the live-pose wiring, but it does not satisfy the Task 4 visual sanity gate or the Task 5 `task5-textured-bob` gate. Do not promote it to `evidence/index.html` or `TIMELINE.md`; a replay-route capture at a later authoritative camera phase is still required.
