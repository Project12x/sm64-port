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
