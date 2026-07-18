# Source-face rendering provenance

The `introface` disc's first non-stand-in renderer consumes original SM64
Goddard face topology from
`src/goddard/dynlists/dynlist_mario_face.c`, present at repository commit
`8bab12daa6b376cdba960126a8c8a1087842fa0c`.

`tools/saturn/extract_introface_mesh.py` directly converts the two initializers
without changing their values:

| Input | Output | Reuse mode |
|---|---|---|
| `mario_Face_VtxData` (440 XYZ vertices) | `sm64_face_vertices` | direct data conversion |
| `mario_Face_FaceData` (877 material/triangle rows) | `sm64_face_triangles` | direct data conversion |

The generated header records the SHA-256 of the complete input source file:
`654541706fc131dc9d81107c2e2323ffae754965ab896bc9cd09a343062b1f6d`.
Its vertex coordinates, material IDs, triangle order, and source indices are
unchanged. `main.c` then uses an orthographic `X/Y` projection and paints the
triangles far-to-near by their original `Z` coordinate, because VDP1 has no
Z buffer.

The extractor also converts the eight `SetAmbient` RGB values in the same
source file to RGB555. The renderer uses those converted colors as the bright
endpoint of each VDP1 Gouraud table; its dark endpoint is a 50% intensity
variant. This establishes source-derived flat material colors and first-pass
lighting, but not pixel-identical original Mario materials: the N64 shine
texture remains later work.

The 2026-07-17 BIOS-backed Ymir capture is
`docs/saturn/evidence/screenshots/ymir-source-face-2026-07-17.png` (SHA-256
`c576acd491459bf810c638f5fb54d108e66558d7fbd6f29e915ecdee8f193c66`, frame
3300). Its adjacent JSON is emulator evidence only, not retail hardware proof.

The source-material follow-up is
`docs/saturn/evidence/screenshots/ymir-source-material-face-2026-07-17.png`
(SHA-256 `9331b87d14feae3fadec4d41f7c36578ad0bcd22d9c3fc7b18c1f6fac04670d6`,
frame 3300). It is likewise BIOS-backed Ymir emulator evidence only.

The eye follow-up directly converts `verts_mario_eye_right`,
`facedata_mario_eye_right`, `verts_mario_eye_left`, and
`facedata_mario_eye_left` from `src/goddard/dynlists/dynlists_mario_eyes.c`
(source SHA-256 `7db76365e55bfdc4a38649a53088fe55eac9bdcec6263491118a95f7378bffbb`).
Each 48-vertex / 82-triangle eye retains the source iris, pupil, and highlight
material IDs. For this fixed front camera, the extractor evaluates the static
eye joint/net rotations and offsets specified in `dynlist_mario_master.c` as
XYZ Euler transforms; dynamic eye tracking is intentionally out of scope.

Its capture is `docs/saturn/evidence/screenshots/ymir-source-eyes-face-2026-07-17.png`
(SHA-256 `cffaf736bcab95fa6e1af03a91d5fd4874a0f5185adfd15debfaf21eff6c5199`,
frame 3300). It is BIOS-backed Ymir emulator evidence only.

The initial static-eye transform was measured against the source face's two
material-4 eye-surface bounds. It placed the right eye 5 screen pixels inward,
the left eye 6 pixels inward, and both eyes 4 pixels low. The fixed-camera
Saturn renderer therefore applies right `(+5, -4)` and left `(-6, -4)`
projection offsets after source transformation. The uncalibrated result is
preserved at `ymir-source-eyes-face-2026-07-17.png`; the calibrated capture is
`docs/saturn/evidence/screenshots/ymir-source-eyes-calibrated-2026-07-17.png`
(SHA-256 `0cd261ca7e9d1a95644caaaff7dbb587e4e5d0c6965892f6dea2592f4f2545b8`,
frame 3300). Both are BIOS-backed Ymir emulator evidence only.

The first feature experiment directly converts `verts_mario_eyebrow_right`,
`facedata_mario_eyebrow_right`, `verts_mario_eyebrow_left`,
`facedata_mario_eyebrow_left`, `verts_mario_mustache`, and
`facedata_mario_mustache` from
`src/goddard/dynlists/dynlists_mario_eyebrows_mustache.c` (source SHA-256
`534876bb73e5b3b428f6b0cc515769479f3608c6ed72a3a6d4305e748acf2770`).
The two eyebrows retain 26 vertices / 36 triangles each; the moustache retains
56 vertices / 100 triangles. The initial renderer projected those raw vertices
as if they already occupied final face space and used their black ambient
material directly. The capture disproves the draw-order assumption: the
moustache sits in front of the nose, which makes its correct black material
and source scale read as a large cutout. `dynlist_mario_master.c` defines
additional net attachment, skin-joint, and animation transforms that a later
animated pose must evaluate.

Its capture is
`docs/saturn/evidence/screenshots/ymir-source-features-2026-07-17.png`
(SHA-256 `f2a056166614cb022540faafc2b585930d1d0dd650bbc1d05265507aec5ec9ae`,
frame hash `d9abbdcdafa238c405689f2c0c6d5e89`, frame 3300). It is retained as a
rejected visual experiment and BIOS-backed Ymir emulator evidence only.

The intermediate static front-camera renderer keeps the same converted feature
topology but scales the moustache projection to `3/4` around screen center
`(160, 169)` and adds a `(0, +3)` pixel offset. It also sorts the eyebrow and
moustache triangles together with all 877 face triangles by source Z, so the
nose is painted after and occludes the moustache. The original material is
pure black; on the Saturn's untextured VDP1 polygon path that read as a flat
cutout against the dark backdrop. The calibrated study therefore uses RGB555
`(7, 3, 1)`, a deliberately documented dark-brown approximation matched to
the rendered hair. This is a static presentation calibration, not a claim that
the full Goddard skin/animation system has been reproduced.

The intermediate capture is
`docs/saturn/evidence/screenshots/ymir-features-calibrated-2026-07-17.png`
(SHA-256 `a0f26f60efe32e97617ac4763b6b7898bc963038598eff6c887d64e0476b6cd1`,
frame hash `41055614fe317a229fc770a28cb3ae97`, frame 3300). It is BIOS-backed Ymir
emulator evidence only.

The accepted follow-up restores the moustache's full source projection and
pure-black source ambient material. It retains the unified painter ordering of
all 877 face and 172 feature triangles, which is the change that actually puts
the nose in front. Its capture is
`docs/saturn/evidence/screenshots/ymir-mustache-source-scale-2026-07-17.png`
(SHA-256 `564a9de72a008f4d10563e05c30cc134ca26b41628a3cb95ad5b213c0ebcbb33`,
frame hash `2a94f17eb58a1625b6f8fd6bc3536ed2`, frame 3300). It is BIOS-backed Ymir
emulator evidence only.

## Shine behavior port

The shine implementation was researched from the existing Goddard renderer at
repository baseline `8bab12daa6b376cdba960126a8c8a1087842fa0c`:

| Reference file | Behavior inspected | Reuse mode |
|---|---|---|
| `src/goddard/objects.c` | Face materials default to type 16 (`GD_MTL_SHINE_DL`). | behavior/pattern only |
| `src/goddard/renderer.c` | A 32x32 IA8 lobe is sampled with normal-generated coordinates and a camera/light-dependent highlight center. | clean-room behavior port |
| `src/goddard/draw_objects.c` | The flagged white star supplies the moving Phong light. | behavior/pattern only |

The repository intentionally omits ROM assets. The user supplied a US ROM ZIP;
the contained ROM validated against the repository's expected SHA-1
`9bef1128717f958171a4afac3ed78ee2bb4e86ce` (ROM SHA-256
`17ce077343c6133f8c9f2d6d6d9a4ab62c8cd2aa57c40aea1f490b4c8bb21d91`).
The repository's own `n64graphics` tool extracted only manifest entry
`textures/intro_raw/mario_face_shine.ia8.png` at US ROM offset `2511696` for
local analysis. That extracted Nintendo asset remains ignored and is not
committed.

The exact map is black except for a narrow 7x7 intensity lobe centered at
`(15,16)`. The Saturn renderer close-ports that measured profile without
copying its pixels: it evaluates normal alignment using fixed-point integer
math, applies a narrow quadratic threshold, and blends the result toward white
inside each existing VDP1 Gouraud table. Runtime floating point, extra polygons,
and extra VDP1 texture commands are all avoided.

Its capture is `docs/saturn/evidence/screenshots/ymir-face-shine-2026-07-17.png`
(SHA-256 `c27aef050b56ab7a7d88fb470acf6e27698652fa91ec67dca3d6a3d671177f0a`,
frame hash `8b1d4bf028e441b1a37890cb7456f5f6`, frame 3300). It is BIOS-backed Ymir
emulator evidence only.

## Interactive presentation loop

The implementation inspected pinned permissive reference code before editing:

| Upstream | Commit / license | Files inspected | Reuse mode |
|---|---|---|---|
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `gamemath/fix16/fix16_trig.c`, `scu/bus/cpu/smpc/smpc_peripheral.c`, `smpc_peripherals.c`, `scu/bus/b/vdp/vdp_sync.c` and their public headers | API use and pattern-only lifecycle adaptation |
| `yaul-org/libyaul-examples` | `66b648eb059bb8bb7392eac70821605a68205b85` / no root license found | `vdp1-mesh/vdp1-mesh.c`, `vdp1-drawing/vdp1-drawing.c` | Behavior study of one INTBACK issue per VBlank and main-loop processing; no source copied |

The next renderer revision replaces the one-shot draw with a VBlank-paced
presentation loop. It uses libyaul 0.3.1 fixed-point `fix16_sincos` projection
for yaw and pitch, re-sorts face and feature depth after camera changes, and
retains the calibrated eye/feature projection. Saturn controls are D-pad
yaw/pitch, L/R zoom, A shine, B auto-orbit, and Start camera reset. This is
whole-head presentation motion; Goddard skin-joint facial deformation is not
yet implemented.

The shine path remains VDP1 Gouraud rather than half transparency. Diffuse and
specular alignment are calculated once for each of the 440 unique face
vertices, expanded into the generated face primitives' Gouraud tables only when the shine
state changes, and uploaded with SCU DMA. The VDP1 command-list allocation is
also retained across frames. The HUD exposes uncapped render ticks, estimated
render throughput, one-time shade rebuild ticks, and the most recently
consumed pad state; presentation itself is capped to VBlank.

The accepted interactive capture is
`docs/saturn/evidence/screenshots/ymir-interactive-face-2026-07-17.png`
(SHA-256 `5b575fcdfc25d55165689161859f65055da346650c3667461786a3ead0bb734c`,
frame hash `0a4022b318768232e53d13864ad91ff8`, frame 3300). The accompanying
`ymir-interactive-face-2026-07-17.json` is BIOS-backed Ymir emulator evidence.
The original interactive proof issued an SMPC collection only once every four
frames, so short manual taps and Ymir's one-frame `input.pulse` could be missed
entirely. The corrected path follows the pinned libyaul API lifecycle: a
VBlank-out callback issues one asynchronous INTBACK collection per video frame,
and the main loop processes the completed sample. The HUD now shows controller
connection, current-down, and new-press edge masks plus Ymir's default keyboard
bindings. Duration-aware input injection remains useful for long deterministic
holds, but is no longer required for a single-frame edge. A post-fix automated
camera/toggle capture is required before the control milestone is closed.

## Conservative true-quad render IR

The source extractor still emits all 877 original triangles unchanged. It now
also calls `tools/saturn/quad_pairing.py` to generate a Saturn-only primitive
table. The compiler requires common material, consistent shared-edge winding,
triangle-normal alignment of at least 0.80, and a strictly convex four-vertex
projection across yaw `-45/-22/0/22/45` and pitch `-30/0/30` degrees. Original
triangle indices remain attached to every primitive, and an unpaired triangle
retains the documented repeated-final-vertex fallback.

The generated result contains 156 true quads and 565 fallback triangles, or
721 face commands instead of 877. Including the original eye and feature
objects, draw commands fall from 1,213 to 1,057. The compiler report is
`docs/saturn/evidence/reports/introface-quad-pairing.json`. The implementation
uses the candidate-graph/matching pattern from the Apache-2.0
`Rulesobeyer/Optimized-Tris-to-Quads-Converter` at commit
`1e1cdb1aaf55bb3e222cd8ecf7233f9065af392c`; no upstream code is copied, and
Blender/PuLP are not build dependencies. Exact selection uses hash-pinned
NetworkX 3.6.1 (`7530809bfa1ea7ed6fdf918a4d1431488953cb1f`, BSD-3-Clause)
with maximum cardinality followed by integer-weighted pairing quality. This
proves that 156 is the maximum under the current safety gates rather than an
artifact of greedy ordering.

The BIOS-backed Ymir visual regression is
`docs/saturn/evidence/screenshots/ymir-true-quads-2026-07-18.png` (SHA-256
`a4bcc8cdd5d71fd493f98b69bfd8ef6228b1573bcc2cd6ed589ffa3e2026506c`,
frame hash `5c39367b895770cbcc2c34c9810dea64`, frame 3300). It preserves the face
silhouette, feature occlusion, eyes, and shine. Emulator timing is comparative
evidence only; animated-deformation validation and retail hardware timing
remain future gates.

The exact-matching follow-up capture is
`docs/saturn/evidence/screenshots/ymir-exact-quads-2026-07-18.png` (SHA-256
`4deb096a9dac257661a2a6b2055002234b62a100be1c233faf772b5fcaa6ee7f`,
frame hash `915851840edf189fb39889476ad98055`, frame 3300). Against the preceding
heuristic pairing it changes 610 pixels in the nose-shading region and changes
zero foreground-mask pixels, preserving the complete rendered silhouette.

The face now enters quad pairing through the reusable Saturn mesh IR v1
compiler in `tools/saturn/saturn_mesh_ir.py`. Its checked compiled artifact is
`docs/saturn/evidence/reports/introface-mesh-ir.json` (SHA-256
`9f308444f68af3cc8d43406935708ec0d3ae43f02caf2aeeeedd5fc208d3b357`). It
records all 440 positions, 721 compiled primitives, and stable source triangle
IDs. It also preserves 41 ordered Goddard joints and all 506 explicit weight
records affecting 320 vertices. The source has up to eight influences per
vertex, and 44 vertices have explicit totals over 100%; the IR therefore uses
`goddard_weighted_accumulation` Q15 records instead of incorrectly normalizing
or truncating them to conventional four-weight skinning. The relevant behavior
was traced through `dynlist_mario_master.c`, `skin.c`, `skin_movement.c`, and
`joints.c`.

The generated C header remains byte-identical at SHA-256
`d390d7638a024a59fbb0431a309353895a643e0917e4333c8dd47f43c24be6ad`.
The intro face currently supplies zero deformation samples, so this is an IR
and neutral-regression milestone rather than a claim that its quads are already
safe under facial motion.

The BIOS-backed reusable-IR regression capture is
`docs/saturn/evidence/screenshots/ymir-mesh-ir-v1-2026-07-18.png` (SHA-256
`4deb096a9dac257661a2a6b2055002234b62a100be1c233faf772b5fcaa6ee7f`,
frame hash `915851840edf189fb39889476ad98055`, frame 3300). Both hashes exactly
match the preceding exact-matcher capture, as expected from the unchanged C
header. The separate capture and manifest establish that the generic compiler
path boots through the USA BIOS without a visual regression.

## Local title-prompt glyph conversion

`tools/saturn/extract_title_prompt_font.py` reads only the six US `main_hud_lut`
glyphs needed for `PRESS START` (`A`, `E`, `P`, `R`, `S`, `T`) from a user-owned
US ROM, converts N64 RGBA16 pixels to Saturn RGB1555, and writes a generated
header plus SHA-256 manifest beneath `build/saturn/introface/generated/`.
Neither the ROM, generated header, nor manifest is committed. Source offsets
are cross-checked against `assets.json` and `bin/segment2.c`; the title source
calls `print_intro_text`, whose text path uses `main_hud_lut` in
`src/game/print.c`. The BIOS-backed result is
`ymir-m1-source-press-start-2026-07-18.png`; it proves local source glyphs,
not a hand-drawn substitute, are composited into the VDP2 title bitmap.

The converter now follows the same MIO0 Segment 2 decompression boundary used
by `extract_assets.py` before reading the recorded US glyph offsets. The
corrected capture is
`ymir-m1-source-glyphs-eyelid-occlusion-2026-07-18.png`; the earlier noisy
prompt is retained in the gallery as a rejected compressed-data read.
