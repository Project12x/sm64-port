#!/usr/bin/env python3
# tools/saturn/extract_hud_glyphs.py
"""Extract the real SM64 gameplay-HUD glyphs into Saturn RGB1555 words.

The input is a user-supplied US SM64 ROM/archive and assets.json's pinned
offset map. Nintendo pixels are emitted only beneath build/, never tracked.
Mirrors the local-ROM-derived, never-committed pattern already established
by extract_title_prompt_font.py and extract_mario_textures.py.

GLYPH_MANIFEST keys are the JP-canonical `segment2.XXXXX` / `actors/...`
filenames used by assets.json (matching bin/segment2.c's texture symbol
declarations), not literal US ROM byte offsets: once a glyph exists in JP
but not US (see the VERSION_JP/VERSION_SH guards in bin/segment2.c, e.g.
texture_hud_char_J, _Q, _V, _X, _Z and the JP-only punctuation block before
texture_hud_char_multiply), the US MIO0 blob is shorter and every later
glyph's *actual* byte offset drifts from its JP-canonical filename number.
build_header() always resolves the real byte offset from assets.json's
`"us"` region for the given key, so this drift is harmless as long as the
key itself is one that genuinely exists with a "us" region -- confirmed
against the real assets.json and against bin/segment2.c's main_hud_lut /
main_hud_camera_lut arrays, not computed from the JP filename pattern.

Notable case: main_hud_camera_lut in bin/segment2.c reuses the exact same
texture_hud_char_mario_head pointer for the camera-status "Mario head" icon
that main_hud_lut uses for the lives-counter Mario head glyph -- there is no
separate camera-specific texture. cam_mario_head below therefore aliases the
same manifest_key as glyph_mario_head on purpose.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import dataclass
from pathlib import Path

from extract_mario_textures import mio0_decode, rom_bytes, saturn_rgb1555


@dataclass(frozen=True)
class GlyphAsset:
    asset_name: str
    manifest_key: str


GLYPH_MANIFEST = tuple(
    GlyphAsset(f"digit_{digit}", f"textures/segment2/segment2.{offset:05X}.rgba16.png")
    for digit, offset in enumerate(range(0x0000, 0x1400, 0x200))
) + (
    GlyphAsset("glyph_multiply", "textures/segment2/segment2.05600.rgba16.png"),
    GlyphAsset("glyph_coin", "textures/segment2/segment2.05800.rgba16.png"),
    GlyphAsset("glyph_mario_head", "textures/segment2/segment2.05A00.rgba16.png"),
    GlyphAsset("glyph_star", "textures/segment2/segment2.05C00.rgba16.png"),
    GlyphAsset("glyph_apostrophe", "textures/segment2/segment2.04800.rgba16.png"),
    GlyphAsset("glyph_double_quote", "textures/segment2/segment2.04A00.rgba16.png"),
    GlyphAsset("cam_camera", "textures/segment2/segment2.07B50.rgba16.png"),
    # Alias, not a bug: see module docstring. Same source pixels as glyph_mario_head.
    GlyphAsset("cam_mario_head", "textures/segment2/segment2.05A00.rgba16.png"),
    GlyphAsset("cam_lakitu_head", "textures/segment2/segment2.07D50.rgba16.png"),
    GlyphAsset("cam_fixed", "textures/segment2/segment2.07F50.rgba16.png"),
    GlyphAsset("cam_arrow_up", "textures/segment2/segment2.08150.rgba16.png"),
    GlyphAsset("cam_arrow_down", "textures/segment2/segment2.081D0.rgba16.png"),
) + (
    GlyphAsset("power_meter_1", "actors/power_meter/power_meter_one_segment.rgba16.png"),
    GlyphAsset("power_meter_2", "actors/power_meter/power_meter_two_segments.rgba16.png"),
    GlyphAsset("power_meter_3", "actors/power_meter/power_meter_three_segments.rgba16.png"),
    GlyphAsset("power_meter_4", "actors/power_meter/power_meter_four_segments.rgba16.png"),
    GlyphAsset("power_meter_5", "actors/power_meter/power_meter_five_segments.rgba16.png"),
    GlyphAsset("power_meter_6", "actors/power_meter/power_meter_six_segments.rgba16.png"),
    GlyphAsset("power_meter_7", "actors/power_meter/power_meter_seven_segments.rgba16.png"),
    # assets.json has no "eight_segments" entry -- the 8-wedge (full) state is "full".
    GlyphAsset("power_meter_8", "actors/power_meter/power_meter_full.rgba16.png"),
)


def build_header(assets: dict, rom: bytes | None = None, rom_sha256: str | None = None) -> tuple[str, dict]:
    if rom_sha256 is None:
        if rom is None:
            raise ValueError("either rom or rom_sha256 is required")
        rom_sha256 = hashlib.sha256(rom).hexdigest()
    lines = [
        "/* Local ROM-derived output: do not commit. */",
        "#pragma once",
        "#include <stdint.h>",
    ]
    manifest = {"rom_sha256": rom_sha256, "glyphs": {}}
    decoded_bases: dict[int, bytes] = {}
    for glyph in GLYPH_MANIFEST:
        entry = assets[glyph.manifest_key]
        width, height, size, regions = entry
        base, offset = regions["us"]
        if rom is not None:
            image = decoded_bases.setdefault(base, mio0_decode(rom, base))
            data = image[offset : offset + size]
            if len(data) != size:
                raise ValueError(f"{glyph.asset_name}: range outside decoded segment")
            words = [
                saturn_rgb1555(int.from_bytes(data[i : i + 2], "big"))
                for i in range(0, size, 2)
            ]
            manifest["glyphs"][glyph.asset_name] = {
                "width": width,
                "height": height,
                "offset": offset,
                "bytes": size,
                "sha256": hashlib.sha256(data).hexdigest(),
            }
            lines += [
                f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_WIDTH {width}U",
                f"#define SM64_SATURN_HUD_{glyph.asset_name.upper()}_HEIGHT {height}U",
                f"static const uint16_t sm64_saturn_hud_{glyph.asset_name}[{len(words)}] = {{",
            ]
            lines += [
                "    " + ", ".join(f"0x{word:04X}" for word in words[i : i + 8]) + ","
                for i in range(0, len(words), 8)
            ]
            lines += ["};"]
        else:
            manifest["glyphs"][glyph.asset_name] = {"width": width, "height": height}
    return "\n".join(lines) + "\n", manifest


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()

    rom = rom_bytes(args.rom)
    assets = json.loads(args.assets.read_text(encoding="utf-8"))
    header, manifest = build_header(assets, rom=rom)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(header, encoding="utf-8")
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
