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
| Goddard-profiled shine | The exact 32x32 IA8 shine map was extracted from a hash-validated user-owned US ROM and measured without committing the asset. Its narrow lobe is close-ported as a fixed-point specular term in the existing VDP1 Gouraud endpoints. | [shine frame](screenshots/ymir-face-shine-2026-07-17.png), [capture report](ymir-face-shine-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Interactive face loop | The static proof now runs every VBlank with fixed-point yaw/pitch/zoom projection, camera-aware painter sorting, persistent VDP1 command storage, cached per-vertex lighting, an A-button shine switch, B-button auto-orbit, and a live timing/pad HUD. | [interactive frame](screenshots/ymir-interactive-face-2026-07-17.png), [capture report](ymir-interactive-face-2026-07-17.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Renderer cost split | Source inspection of SlaveDriver, Jo Engine, and Sonic Z-Treme led to an explicit four-way HUD measurement: painter sort, transform/command build, conditional Gouraud upload, and render wait. The BIOS-backed Ymir capture shows the counters over the actual source-derived face; emulator timing remains comparative evidence only. | [telemetry frame](screenshots/ymir-renderer-telemetry-2026-07-17.png), [capture report](ymir-renderer-telemetry-2026-07-17.json), [prior-art decision](../RENDERER_PRIOR_ART.md) |
| Controller polling repair | The demo now initializes SMPC before registering the libyaul-style VBlank INTBACK callback and processes a sample every loop. A bounded Ymir Right hold reaches the Saturn program as `DOWN 8000` and changes the rendered framebuffer from the neutral baseline. | [neutral frame](screenshots/ymir-input-neutral-2026-07-18.png), [Right-held frame](screenshots/ymir-input-right-held-2026-07-18.png), [capture report](ymir-input-right-held-2026-07-18.json), [provenance](../INTROFACE_PROVENANCE.md) |
| Remote release follow-up | Preserved automation limitation: after Ymir removes its persistent pad override, the most recent Saturn-side sample remains latched as `DOWN 8000` in this run. Manual SDL input is ready for retest, but deterministic remote release is not yet claimed. | [latched-release frame](screenshots/ymir-input-right-2026-07-18.png), [capture report](ymir-input-right-2026-07-18.json) |
| Painter/vertex cache pass | The first performance pass retains the preceding frame's nearly sorted order, caches one depth per surface, and transforms/projects each of the 440 shared face vertices once per frame. This removes repeated fixed-point work without changing source topology or the Gouraud result. The remaining major risk is VDP1 command cost from 1,049 repeated-vertex triangle commands. | [optimized frame](screenshots/ymir-sort-cache-2026-07-18.png), [capture report](ymir-sort-cache-2026-07-18.json), [renderer decision](../RENDERER_PRIOR_ART.md) |

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

### Goddard-profiled VDP1 shine

![Mario source face with fixed-point Gouraud specular shine](screenshots/ymir-face-shine-2026-07-17.png)

### Interactive camera and lighting benchmark

![Interactive VBlank renderer with controls and timing HUD](screenshots/ymir-interactive-face-2026-07-17.png)

## Next visual gates

1. Evaluate the animated Goddard skin-joint pose for facial and moustache
   deformation; retain the source-scale static pose as the neutral baseline.
2. Finish deterministic remote release after the duration-aware Ymir hold;
   press detection is proven, while the final Saturn sample can remain latched.
3. Compare VDP1 command, Gouraud-table, CPU transform, and painter-sort budgets
   with the intended game-frame budget.
4. Replace compatible adjacent triangle pairs with true four-vertex VDP1
   quads, keeping explicit fallbacks for non-pairable or unsafe topology.
