#!/usr/bin/env python3
"""Create a local-only Saturn header from the US SM64 HUD glyphs.

The output contains Nintendo-derived pixels and is intentionally generated
under build/, never committed.  Source offsets are taken from assets.json and
the main_hud_lut ordering in bin/segment2.c.
"""
import argparse
import hashlib
import json
import pathlib
import zipfile

ROM_BASE = 1083968
ROM_OFFSETS = {"A": 5120, "E": 7168, "P": 12288, "R": 12800,
               "S": 13312, "T": 13824}

def read_rom(path: pathlib.Path) -> bytes:
    if path.suffix.lower() != ".zip":
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        candidates = [info for info in archive.infolist()
                      if info.filename.lower().endswith((".z64", ".n64", ".v64"))]
        if len(candidates) != 1:
            raise SystemExit("archive must contain exactly one N64 ROM image")
        return archive.read(candidates[0])

def n64_to_saturn(word: int) -> int:
    # N64 RGBA16: RRRRRGGGGGBBBBBA; Saturn RGB1555: ARRRRRGGGGGBBBBB.
    red = (word >> 11) & 0x1F
    green = (word >> 6) & 0x1F
    blue = (word >> 1) & 0x1F
    return ((word & 1) << 15) | (red << 10) | (green << 5) | blue

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--manifest", required=True, type=pathlib.Path)
    args = parser.parse_args()
    rom = read_rom(args.rom)
    glyphs = {}
    for letter, offset in ROM_OFFSETS.items():
        raw = rom[ROM_BASE + offset:ROM_BASE + offset + 512]
        if len(raw) != 512:
            raise SystemExit(f"ROM ends before glyph {letter}")
        glyphs[letter] = [n64_to_saturn(int.from_bytes(raw[i:i + 2], "big"))
                          for i in range(0, 512, 2)]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        "/* Local ROM-derived data. Do not commit. */\n"
        "#define SM64_TITLE_FONT_GENERATED 1\n"
        "static const uint16_t sm64_title_font[6][256] = {\n" +
        "\n".join("{" + ",".join(f"0x{pixel:04X}" for pixel in glyphs[ch]) + "},"
                    for ch in "AEP RST".replace(" ", "")) + "\n};\n")
    args.manifest.write_text(json.dumps({
        "input_sha256": hashlib.sha256(rom).hexdigest(),
        "source": "US main_hud_lut 16x16 RGBA16 glyphs",
        "letters": list("A E P R S T".replace(" ", "")),
        "output": str(args.output),
        "derived_asset_committed": False,
    }, indent=2) + "\n")

if __name__ == "__main__":
    main()
