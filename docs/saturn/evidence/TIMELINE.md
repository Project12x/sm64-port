# Saturn port visual timeline

This is the visual portfolio for the port. Each entry distinguishes a
boot/protocol diagnostic from an actual renderer result, and distinguishes
source-derived content from an original stand-in. Screenshots are evidence of
the named state only; emulator timing is not retail hardware evidence.

| Stage | What it proves | Visual evidence |
|---|---|---|
| Hello disc | The first Yaul SH-2 disc reached a display through Yabause HLE. | [hello screen](screenshots/2026-07-17/hello-yabause-hle.png) |
| Hardware-test geometry | VDP1 can draw the test primitives, including textured/repeated-vertex triangles. | [textured triangle](screenshots/2026-07-17/hwtest-kronos-textured-triangle.png), [repeat-vertex triangle](screenshots/2026-07-17/hwtest-kronos-textured-triangle-repeat.png) |
| Hardware-test gate | Unsupported cartridge configurations fail visibly. | [size gate](screenshots/2026-07-17/hwtest-kronos-size-gate.png) |
| Ymir controller automation | The agent debugger selected English and advanced through the USA BIOS UI. | [English selection](screenshots/ymir-input-pulse-right-2026-07-17.png), [confirmation](screenshots/ymir-input-pulse-a-2026-07-17.png) |
| BIOS-backed hwtest | The fixed Ymir headless path authenticated the disc, loaded A.BIN, and completed the VDP1 hwtest. | [hwtest frame](screenshots/ymir-bios-hwtest-2026-07-17.png), [raw/report](ymir-bios-hwtest-2026-07-17.md) |
| Mario intro-face stand-in | Original VDP1 material-region study: cap, skin, hair, eyes, moustache, mouth, and collar. It is **not** a render of the original SM64 Mario mesh. | [intro-face study](screenshots/ymir-introface-2026-07-17.png) |

## Next visual gates

1. Render converted triangles from `src/goddard/dynlists/dynlist_mario_face.c`.
2. Capture a flat-shaded source-mesh silhouette and record its command count.
3. Add queued per-surface Gouraud tables, then capture the same camera angle.
4. Add source texture conversion and capture a textured face region.

The current intro-face stand-in is useful only as a VDP1 composition and
palette milestone. The first source-derived Mario rendering will be recorded
as a separate timeline row and must name the exact source file and selected
mesh range.
