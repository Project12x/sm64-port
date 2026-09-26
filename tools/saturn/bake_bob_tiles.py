#!/usr/bin/env python3
"""Bake BOB's Fast3D texture triangles into deterministic VDP1 CLUT16 tiles.

The source is the checked-in BOB intake stream, not a second geometry parser.
Each eligible source triangle gets one fixed-size tile; the manifest keeps the
source identity and tile state beside the packed bank so a stale bank cannot be
silently interpreted as textured geometry.
"""
from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import re
import struct
import zlib
from pathlib import Path

from bake_castle_uv import quantize_clut16, pack_clut16, sample_triangle, sample_quad
from vdp1_texture import downsample_rgb1555
from write_if_changed import (write_bytes_if_changed,
                             write_text_if_changed)

# VDP1 layout invariant: this value IS the Mario texture base
# (SATURN_MARIO_TEXTURE_BASE_OFFSET, src/port/saturn/sourceboot/Makefile:761).
# The terrain tile bank ends exactly where Mario's textures begin, so these two
# numbers move together or the bank overwrites Mario. Raised from 333,696 to
# make room for faces that Task 1's UV-repeat refinement made bakeable -- faces
# that previously consumed zero texture because the baker gave up on them and
# the runtime drew a white quad instead.
MAX_TILE_BYTES = 345_472
MAX_VDP1_TEXTURE_BYTES = 446_432
# The terrain CLUT partition main.c DMAs to VRAM: SOURCEBOOT_BOB_CLUT_COUNT
# (1077) palettes of 32 bytes (src/port/saturn/sourceboot/main.c:785, 2219).
# Another hand-kept copy of a VDP1 boundary, like MAX_TILE_BYTES; the upload
# length is fixed, so a bank larger than this is silently truncated on the
# console and a smaller one is read past its end.
MAX_CLUT_BYTES = 1077 * 32
REPO_ROOT = Path(__file__).resolve().parents[2]
_TEXTURE_DEFINITION = re.compile(
    r'const\s+Texture\s+([A-Za-z_][A-Za-z0-9_]*)\s*\[\s*\]\s*=\s*\{\s*'
    r'#include\s+"([^"]+)\.inc\.c"')


def texture_sources(root: Path = REPO_ROOT) -> dict[str, str]:
    """Map every Fast3D texture symbol the decomp defines to its PNG.

    Read from the decomp's own definitions -- `const Texture NAME[] = {
    #include "PATH.inc.c"` in the texture banks (bin/*.c) and each level's
    texture.inc.c -- rather than a hand-kept table. The previous table named
    only BOB's generic bank and five BOB textures, so any other level failed
    with "no exported BOB texture". The PNG is the .inc.c's source image.
    """
    table: dict[str, str] = {}
    for path in (*sorted(root.glob("bin/*.c")),
                 *sorted(root.glob("levels/*/texture.inc.c"))):
        for name, stem in _TEXTURE_DEFINITION.findall(
                path.read_text(encoding="utf-8", errors="ignore")):
            table[name] = f"{stem}.png"
    return table


# Kept under its historical name: bake_bob_bsp_fragments.py imports it.
BOB_TEXTURES = texture_sources()


def _png_pixels(path: Path) -> tuple[int, int, list[int]]:
    """Read the small exporter PNGs without adding a runtime dependency."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    offset, idat, width = 8, bytearray(), 0
    height = color_type = bit_depth = 0
    while offset < len(data):
        size = struct.unpack(">I", data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        offset += 12 + size
        if kind == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", payload[:10])
        elif kind == b"IDAT":
            idat.extend(payload)
    if bit_depth != 8 or color_type not in (4, 6):
        raise ValueError(f"{path}: unsupported PNG format")
    channels = 2 if color_type == 4 else 4
    row_bytes = width * channels
    raw = zlib.decompress(bytes(idat))
    rows: list[bytes] = []
    cursor = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filt = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor:cursor + row_bytes])
        cursor += row_bytes
        for i in range(row_bytes):
            left = row[i - channels] if i >= channels else 0
            up = previous[i]
            up_left = previous[i - channels] if i >= channels else 0
            if filt == 1:
                row[i] = (row[i] + left) & 255
            elif filt == 2:
                row[i] = (row[i] + up) & 255
            elif filt == 3:
                row[i] = (row[i] + ((left + up) // 2)) & 255
            elif filt == 4:
                p = left + up - up_left
                pa, pb, pc = abs(p - left), abs(p - up), abs(p - up_left)
                row[i] = (row[i] + (left if pa <= pb and pa <= pc else up if pb <= pc else up_left)) & 255
            elif filt != 0:
                raise ValueError(f"{path}: unsupported PNG filter {filt}")
        rows.append(bytes(row))
        previous = row
    pixels: list[int] = []
    for row in rows:
        for x in range(width):
            if channels == 2:
                red = green = blue = row[x * 2]
                alpha = row[x * 2 + 1]
            else:
                red, green, blue, alpha = row[x * 4: x * 4 + 4]
            # exporter PNGs use 8-bit-expanded RGB1555 channels.
            value = ((red * 31 + 127) // 255) | (((green * 31 + 127) // 255) << 5) | (((blue * 31 + 127) // 255) << 10)
            pixels.append(value | (0x8000 if alpha >= 128 else 0))
    return width, height, pixels


def texture_coordinate_key(state: dict[str, object]) -> str:
    return json.dumps(state, sort_keys=True, separators=(",", ":"))


def triangle_k(triangle: dict[str, object]) -> float:
    state = triangle["tile"]
    uv = triangle["uv"]
    span_s = max(point[0] for point in uv) - min(point[0] for point in uv)
    span_t = max(point[1] for point in uv) - min(point[1] for point in uv)
    tile_s = int(state["lrs"]) - int(state["uls"]) + 4
    tile_t = int(state["lrt"]) - int(state["ult"]) + 4
    # Fast3D UVs are s10.5 values while the tile extents above are in
    # quarter-texel units.  Eight of these extent ratios is one source-tile
    # period (the same normalization used by the texture specification).
    return max(span_s / tile_s, span_t / tile_t) / 8.0


def bake_bob(intake: dict[str, object], asset_root: Path,
             compiled_mesh: dict[str, object] | None = None,
             allow_tile_class_drift: bool = False) -> tuple[bytes, bytes, dict[str, object]]:
    # The runtime binds source0's tile to the compiled A/B/C/D quad.
    # Preserve bank layout, but bake that tile in the same four-corner domain.
    # Unpaired source triangles retain the existing A/B/C/C sampler.
    quad_uvs = {}
    if compiled_mesh is not None:
        attributes = compiled_mesh['vertex_attributes']['uv']
        for primitive in compiled_mesh['primitives']:
            sources = primitive['source_triangles']
            if len(sources) == 1:
                continue
            indices = primitive['indices']
            if len(sources) != 2 or len(indices) != 4 or len(set(indices)) != 4:
                raise ValueError('paired terrain primitive must have four distinct corners')
            source0 = int(sources[0])
            if source0 in quad_uvs or not 0 <= source0 < len(intake['triangles']):
                raise ValueError('paired terrain source identity is invalid or duplicated')
            quad_uvs[source0] = tuple(tuple(int(v) for v in attributes[index])
                                     for index in indices)
    textures: dict[str, tuple[int, int, list[int], str, int]] = {}
    for name in sorted({str(t["texture"]) for t in intake["triangles"]
                        if t.get("texture") is not None}):
        relative = BOB_TEXTURES.get(name)
        if relative is None:
            raise ValueError(f"no exported BOB texture for {name}")
        width, height, pixels = _png_pixels(asset_root / relative)
        pixels = downsample_rgb1555(pixels, width, height, 1)[2]
        source = (asset_root / relative).read_bytes()
        textures[name] = (width, height, pixels, hashlib.sha256(source).hexdigest(), len(source))

    bank = bytearray()
    clut = bytearray()
    entries: list[dict[str, object]] = []
    class_counts = {"16x16": 0, "32x32": 0, "gouraud": 0}
    untextured = 0
    for source_index, triangle in enumerate(intake["triangles"]):
        if triangle.get("texture") is None:
            # A vertex-coloured face: nothing to tile. Like the gouraud class
            # it gets no manifest entry, so the scene draws it untextured
            # with its own colour. BOB had none; WF does.
            untextured += 1
            continue
        k = triangle_k(triangle)
        if k > 16:
            class_counts["gouraud"] += 1
            continue
        tile_size = 16 if k <= 2 else 32
        # A subdivided face covers 1/(4**depth) of the original surface, so the
        # same texel density needs half the tile dimension per level. Without
        # this a child keeps its parent's full tile and subdivision costs 4x the
        # texture for identical coverage; with it, four 16x16 children occupy
        # exactly the 1024 bytes their 32x32 parent did. 16 is the floor.
        depth = int(triangle.get("subdivision_depth", 0))
        if depth > 0:
            tile_size = max(16, tile_size >> depth)
        class_name = f"{tile_size}x{tile_size}"
        class_counts[class_name] += 1
        state = dict(triangle["tile"])
        texture = textures[str(triangle["texture"])]
        uv = tuple(tuple(int(value) for value in point) for point in triangle["uv"])
        sampler = sample_quad if source_index in quad_uvs else sample_triangle
        uv = quad_uvs.get(source_index, uv)
        pixels = [sampler(texture, uv, state, x, y, tile_size, 1)
                  for y in range(tile_size) for x in range(tile_size)]
        palette, mapping = quantize_clut16(pixels)
        packed = pack_clut16([mapping[value] for value in pixels])
        tile_offset = len(bank)
        clut_offset = len(clut)
        bank.extend(packed)
        clut.extend(struct.pack(">16H", *palette))
        entries.append({
            "source_triangle": source_index,
            "source_display_list": triangle["source_display_list"],
            "display_list_ordinal": source_index,
            "texture": triangle["texture"],
            "tile_state_sha256": hashlib.sha256(texture_coordinate_key(state).encode()).hexdigest(),
            "k": k,
            "tile_size": tile_size,
            "bank_offset": tile_offset,
            "bank_bytes": len(packed),
            "clut_offset": clut_offset,
            "clut_bytes": 32,
        })
    # Pinned bake composition, as a drift detector for the default pipeline.
    # It is an exact snapshot, so any DELIBERATE change to the mesh -- terrain
    # subdivision in particular, which makes polygons smaller and therefore
    # shifts tiles from 32x32 toward 16x16 -- trips it by construction. Callers
    # making such a change pass --allow-tile-class-drift, which reports the new
    # composition instead of failing. The guard still protects every build that
    # is not intentionally reshaping the mesh.
    expected_classes = {"16x16": 567, "32x32": 510, "gouraud": 24}
    if class_counts != expected_classes:
        if not allow_tile_class_drift:
            raise ValueError(
                f"unexpected BOB tile classes: {class_counts} "
                f"(expected {expected_classes}); pass "
                f"--allow-tile-class-drift if the mesh change is intended")
        print(f"bob tiles: class composition {expected_classes} -> "
              f"{class_counts} (drift allowed)")
    if len(bank) > MAX_TILE_BYTES:
        raise ValueError(f"tile bank exceeds budget: {len(bank)} > {MAX_TILE_BYTES}")
    clut_overflow_palettes = max(0, (len(clut) - MAX_CLUT_BYTES) // 32)
    if clut_overflow_palettes:
        # main.c DMAs exactly MAX_CLUT_BYTES, so every palette past the
        # partition never reaches VRAM and its faces draw with whatever
        # follows. BOB's UV-refined bake is in this state (1175 palettes vs
        # 1077) -- including the build the owner approved. Reported loudly and
        # recorded in the manifest rather than failing the default build: the
        # shared-texture bake replaces per-face palettes and removes the cause.
        print(f"bob tiles: WARNING CLUT bank exceeds its VDP1 partition by "
              f"{clut_overflow_palettes} palettes ({len(clut)} > "
              f"{MAX_CLUT_BYTES} bytes); those faces sample past the "
              f"uploaded palettes")
    if len(bank) + len(clut) > MAX_VDP1_TEXTURE_BYTES:
        raise ValueError("tile plus CLUT bank exceeds free VDP1 texture VRAM")
    manifest = {
        "schema": "sm64-saturn-bob-texture-bank",
        "version": 1,
        "source_schema": intake["schema"],
        "source_version": intake["version"],
        "tile_format": "VDP1_CLUT16_4BPP",
        "coverage": class_counts,
        "untextured_triangles": untextured,
        "clut_overflow_palettes": clut_overflow_palettes,
        "source_triangles": len(intake["triangles"]),
        "texture_bytes": len(bank),
        "clut_bytes": len(clut),
        "resident_bytes": len(bank) + len(clut),
        "max_texture_bytes": MAX_TILE_BYTES,
        "max_resident_bytes": MAX_VDP1_TEXTURE_BYTES,
        "entries": entries,
        "source_texture_sha256": {name: value[3] for name, value in sorted(textures.items())},
    }
    # The banks are VDP1 partition images, not content blobs. main.c uploads a
    # fixed length from each (SOURCEBOOT_BOB_TEXTURE_BYTES, SOURCEBOOT_BOB_CLUT_
    # COUNT * 32) and places Mario's textures at the fixed end of the terrain
    # partition, so a level whose content is smaller than the partition must
    # still supply the whole partition or the upload reads past the end of the
    # symbol. Zero fill is inert: no face indexes past its own content. The
    # manifest above keeps the true content sizes. A no-op for a bank that
    # already fills its partition, as BOB's tile bank does.
    bank.extend(bytes(MAX_TILE_BYTES - len(bank)))
    clut.extend(bytes(max(0, MAX_CLUT_BYTES - len(clut))))
    return bytes(bank), bytes(clut), manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--mesh", type=Path, required=True,
                        help="Compiled primitive corner order used by the renderer")
    parser.add_argument("--asset-root", type=Path, default=Path("."))
    parser.add_argument("--bank", type=Path, required=True)
    parser.add_argument("--clut", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--allow-tile-class-drift", action="store_true",
                        help="report rather than reject a tile-class "
                             "composition that differs from the pinned "
                             "baseline; for deliberate mesh changes such as "
                             "terrain subdivision")
    args = parser.parse_args()
    intake = json.loads(args.intake.read_text(encoding="utf-8"))
    mesh = json.loads(args.mesh.read_text(encoding="utf-8"))
    bank, clut, manifest = bake_bob(intake, args.asset_root, mesh,
                                    args.allow_tile_class_drift)
    for path, payload in ((args.bank, bank), (args.clut, clut)):
        write_bytes_if_changed(path, payload)
    manifest["bank_sha256"] = hashlib.sha256(bank).hexdigest()
    manifest["clut_sha256"] = hashlib.sha256(clut).hexdigest()
    write_text_if_changed(
        args.manifest,
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
