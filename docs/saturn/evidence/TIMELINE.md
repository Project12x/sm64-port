# Saturn port visual timeline

This is the visual portfolio for the port. Each entry distinguishes a
boot/protocol diagnostic from an actual renderer result, and distinguishes
source-derived content from an original stand-in. Screenshots are evidence of
the named state only; emulator timing is not retail hardware evidence.

For a screenshot-first viewer, open [the visual timeline gallery](index.html).

| Stage | What it proves | Visual evidence |
|---|---|---|
| Hello disc | The first Yaul SH-2 disc reached a display through Yabause HLE. | [hello screen](screenshots/2026-07-17/hello-yabause-hle.png) |
| Hardware-test geometry | VDP1 can draw the test primitives, including textured/repeated-vertex triangles. | [textured triangle](screenshots/2026-07-17/hwtest-kronos-textured-triangle.png), [repeat-vertex triangle](screenshots/2026-07-17/hwtest-kronos-textured-triangle-repeat.png) |
| Hardware-test gate | Unsupported cartridge configurations fail visibly. | [size gate](screenshots/2026-07-17/hwtest-kronos-size-gate.png) |
| Ymir controller automation | The agent debugger selected English and advanced through the USA BIOS UI. | [English selection](screenshots/ymir-input-pulse-right-2026-07-17.png), [confirmation](screenshots/ymir-input-pulse-a-2026-07-17.png) |
| BIOS-backed hwtest | The fixed Ymir headless path authenticated the disc, loaded A.BIN, and completed the VDP1 hwtest. | [hwtest frame](screenshots/ymir-bios-hwtest-2026-07-17.png), [raw/report](ymir-bios-hwtest-2026-07-17.md) |
| Mario intro-face stand-in | Original VDP1 material-region study: cap, skin, hair, eyes, moustache, mouth, and collar. It is **not** a render of the original SM64 Mario mesh. | [intro-face study](screenshots/ymir-introface-2026-07-17.png) |
| Mario source-face topology | The VDP1 renderer projected all 440 original vertices and submitted all 877 original `mario_Face_FaceData` triangles from `src/goddard/dynlists/dynlist_mario_face.c`, each with a Gouraud table. The colors remain temporary material-ID mapping; geometry is source-derived. | [source-face frame](screenshots/ymir-source-face-2026-07-17.png), [capture report](ymir-source-face-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Mario source-face materials | The mesh extractor converted the same file's eight original `SetAmbient` material colors to RGB555: teeth/emblem/eyes, skin, hat shadow, hair, mouth, and cap. VDP1 uses them as Gouraud endpoints. | [source-material frame](screenshots/ymir-source-material-face-2026-07-17.png), [capture report](ymir-source-material-face-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Mario source eyes and pupils | The static front-pose renderer adds both original 48-vertex / 82-triangle eye objects from `dynlists_mario_eyes.c`, including blue irises, black pupils, and white highlights. | [source-eye frame](screenshots/ymir-source-eyes-face-2026-07-17.png), [capture report](ymir-source-eyes-face-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Mario eye alignment calibration | The first static transform was retained, measured against the source face eye-surface bounds, then corrected by right `(+5,-4)` and left `(-6,-4)` screen pixels. | [initial eye frame](screenshots/ymir-source-eyes-face-2026-07-17.png), [calibrated eye frame](screenshots/ymir-source-eyes-calibrated-2026-07-17.png), [capture report](ymir-source-eyes-calibrated-2026-07-17.json) |
| Mario eye-size calibration | The separate source eye meshes were reduced to 2/3 around their matching source eye-surface centers so irises/pupils sit inside the white face geometry. | [scaled-eye frame](screenshots/ymir-source-eyes-scaled-2026-07-17.png), [capture report](ymir-source-eyes-scaled-2026-07-17.json) |
| Topology-derived Gouraud depth | Per-vertex normals are accumulated from the 877 source triangles and evaluated against an upper-left key light, producing continuous VDP1 Gouraud lighting on the face. | [Gouraud frame](screenshots/ymir-source-gouraud-2026-07-17.png), [capture report](ymir-source-gouraud-2026-07-17.json) |
| Rejected raw-feature placement | The first feature import drew every moustache triangle after the face, placing it over the nose and making its otherwise-correct source scale and black material read like an oversized cutout. Preserved as a failure, not accepted renderer progress. | [failure frame](screenshots/ymir-source-features-2026-07-17.png), [capture report](ymir-source-features-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Intermediate reduced moustache | A 3/4-scale dark-brown calibration proved that shared Z sorting fixes nose occlusion, but unnecessarily changed two source-authentic properties. Preserved as an intermediate correction. | [intermediate frame](screenshots/ymir-features-calibrated-2026-07-17.png), [capture report](ymir-features-calibrated-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Source-scale black moustache | The accepted correction restores the original scale, position, and black material while retaining the unified face/feature Z sort. The nose now occludes the full source silhouette correctly. | [accepted frame](screenshots/ymir-mustache-source-scale-2026-07-17.png), [capture report](ymir-mustache-source-scale-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |

## Source-face gallery

### Material conversion

![Source-derived face with original material colors](screenshots/ymir-source-material-face-2026-07-17.png)

### Eye placement iteration

![Initial source eye placement](screenshots/ymir-source-eyes-face-2026-07-17.png)

![Calibrated source eye placement](screenshots/ymir-source-eyes-calibrated-2026-07-17.png)

![Scaled source eye placement](screenshots/ymir-source-eyes-scaled-2026-07-17.png)

### Topology-derived Gouraud lighting

![Source face with Gouraud depth lighting](screenshots/ymir-source-gouraud-2026-07-17.png)

### Rejected: raw moustache and eyebrow placement

![Failed raw placement with oversized black moustache in front of the nose](screenshots/ymir-source-features-2026-07-17.png)

The source objects are valid, but this direct projection is not. The moustache
must follow its Goddard net/skin transform and correct depth/material behavior.

### Corrected static feature calibration

![Corrected dark-brown moustache behind the nose and aligned eyebrows](screenshots/ymir-features-calibrated-2026-07-17.png)

### Accepted source-scale black moustache

![Full-size source-black moustache correctly occluded by the nose](screenshots/ymir-mustache-source-scale-2026-07-17.png)

## Next visual gates

1. Extract and convert the original 32x32 IA8 Mario face shine map from the
   user-owned US ROM, then capture its Saturn rendering.
2. Evaluate the animated Goddard skin-joint pose for moustache deformation;
   retain the source-scale static pose as the front-camera baseline.
3. Add controllable camera yaw/pitch plus a deterministic two-angle screenshot
   pair.
4. Compare VDP1 command and Gouraud-table budgets with the intended game frame
   budget.

The current intro-face stand-in is useful only as a VDP1 composition and
palette milestone. The first source-derived Mario rendering will be recorded
as a separate timeline row and must name the exact source file and selected
mesh range.
