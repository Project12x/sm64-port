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
lighting, but not pixel-identical original Mario materials: the separate eye,
eyebrow, and moustache objects and the N64 shine texture are later work.

The 2026-07-17 BIOS-backed Ymir capture is
`docs/saturn/evidence/screenshots/ymir-source-face-2026-07-17.png` (SHA-256
`c576acd491459bf810c638f5fb54d108e66558d7fbd6f29e915ecdee8f193c66`, frame
3300). Its adjacent JSON is emulator evidence only, not retail hardware proof.

The source-material follow-up is
`docs/saturn/evidence/screenshots/ymir-source-material-face-2026-07-17.png`
(SHA-256 `9331b87d14feae3fadec4d41f7c36578ad0bcd22d9c3fc7b18c1f6fac04670d6`,
frame 3300). It is likewise BIOS-backed Ymir emulator evidence only.
