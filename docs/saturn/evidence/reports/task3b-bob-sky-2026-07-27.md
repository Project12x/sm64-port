# Task 3b — BOB sky-gradient capture

This is the first fresh visual capture after replacing sourceboot's solid
black VDP2 back color with a 224-line RGB1555 back-screen table.

| Item | Value |
|---|---|
| Screenshot | `docs/saturn/evidence/screenshots/task3b-bob-sky-2026-07-27.png` |
| Screenshot SHA-256 | `c1de4d41d22b8f2e38f2d0b9fb0066eef473897333e1b9fcd58bc8654e81d3c0` |
| Frame hash | `fbab7fc1233f0b03f61ec0e5efd2501d` |
| Dimensions | 320×224 |
| ELF SHA-256 | `18cc4c97386f67a657d77774a1e0b3e71ffdcB0c496eac50f8a72cabe22c6cc8` |
| CUE SHA-256 | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| Probe | `_sourceboot_fast3d = 0x060C12A4`, 512 bytes |
| Capture | BIOS automation, handoff yield, `--dram-cart`, post-poke 3600 frames |

The profile decoder reports `frame_serial=28`, `fault_flags=0`,
`triangles_transformed=1439`, `triangles_emitted=492`, and
`triangles_vdp1_emitted=0`. The image reaches the sourceboot scene and shows
the non-black VDP2 sky field, but the scene geometry is still a diagnostic
sourceboot frame rather than the Task 5 textured-Bob acceptance image.

This report and image are evidence only until the owner confirms what is
visibly present. No `TIMELINE.md`, HTML gallery, or portfolio entry is added
by this capture.
