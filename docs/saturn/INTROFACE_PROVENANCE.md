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

This milestone does **not** yet convert the original face textures or its
runtime material setup. Its cap/skin/hair/eye colors are a temporary,
hand-authored mapping of the original material IDs, with per-vertex Gouraud
intensity supplied by VDP1. The screenshot therefore proves original geometry
on the Saturn renderer, not pixel-identical original Mario materials.

The 2026-07-17 BIOS-backed Ymir capture is
`docs/saturn/evidence/screenshots/ymir-source-face-2026-07-17.png` (SHA-256
`c576acd491459bf810c638f5fb54d108e66558d7fbd6f29e915ecdee8f193c66`, frame
3300). Its adjacent JSON is emulator evidence only, not retail hardware proof.
