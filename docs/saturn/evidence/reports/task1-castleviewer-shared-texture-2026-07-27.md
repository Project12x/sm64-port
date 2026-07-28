# Task 1 castleviewer shared-texture capture

The post-harvest castleviewer image was built from commit `780d25e` and
captured from the freshly regenerated CUE with `--dram-cart`, BIOS input, and
the documented handoff-yield sequence. The fresh ELF symbol
`_saturn_scene_profile` resolved to `0x06096AD8`; the 42-byte probe decoded as
the expected `SC` profile (`magic=0x5343`, `version=1`, `size=42`). The capture
also reached the textured castle scene and returned `fault_flags`-independent
scene telemetry (the legacy SAT0 block is intentionally absent from this
castleviewer target).

Probe values (big-endian 16-bit fields):

| Field | Value |
| --- | ---: |
| sort_mario_ticks | 4150 |
| static_rebuild_ticks | 1 |
| emit_ticks | 466 |
| static_stream_count | 537 |
| mario_visible | 294 |
| mario_rejected | 648 |
| mario_culled | 499 |
| static_rejected | 648 |
| static_culled | 149 |
| static_valid | 1 |

The retained comparison image is
`ymir-e1-shared-vdp1-castle-2026-07-19.png`. The new image has SHA-256
`0dae0dfe14d4b97f2f4f994d2ad64db36d591633867d6fff16142a69178aa789`; its Ymir
frame hash is `8f8b1b3c6786eb3b65cfaf2643e05690`. A direct pixel comparison
against the retained 320x224 image changed 68,465 pixels, with diff bounding
box `(0,0)-(319,223)`. This is recorded as a benign comparison limitation:
the captures use different post-boot camera/animation states, so the entire
frame is eligible to differ; no pixel-identical regression claim is made.

Artifacts:

- `task1-castleviewer-shared-texture-2026-07-27.json`
- `../screenshots/task1-castleviewer-shared-texture-2026-07-27.png`
