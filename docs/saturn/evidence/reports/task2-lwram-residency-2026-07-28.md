# Task 2 LWRAM BOB-bank residency milestone — 2026-07-28

Status: **residency implementation verified; visual diagnostic; not gallery-accepted**.

The generated BOB positions (`19,500 B`) and primitives (`24,276 B`) are now
copied from cart-linked source data into `.lwram_bss` at boot before the demo
frame loop. Fresh ELF symbols place the resident arrays at `0x00215ed4` and
`0x00210000`; the frame renderer reads those resident arrays, while the baked
texture bank is uploaded once to VDP1 VRAM through the shared residency API.

The paired capture remains live (`frame_serial=51`, actor bridge valid,
`fault_flags=0`, route `SBR1` at `replay_ticks=25`). The screenshot retains the
identifiable Mario diagnostic but still has incomplete black terrain, so it is
not promoted to `docs/saturn/evidence/index.html` or `TIMELINE.md`.
