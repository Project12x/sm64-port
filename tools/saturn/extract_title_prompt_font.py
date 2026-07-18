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

def mio0_decode(blob: bytes) -> bytes:
    if blob[:4] != b"MIO0":
        raise SystemExit("expected MIO0-compressed US Segment 2 at the recorded ROM offset")
    output_size = int.from_bytes(blob[4:8], "big")
    compressed_offset = int.from_bytes(blob[8:12], "big")
    raw_offset = int.from_bytes(blob[12:16], "big")
    layout_offset = 16
    output = bytearray()
    bits_remaining = 0
    layout = 0
    while len(output) < output_size:
        if bits_remaining == 0:
            layout = int.from_bytes(blob[layout_offset:layout_offset + 4], "big")
            layout_offset += 4
            bits_remaining = 32
        if layout & 0x80000000:
            output.append(blob[raw_offset])
            raw_offset += 1
        else:
            pair = int.from_bytes(blob[compressed_offset:compressed_offset + 2], "big")
            compressed_offset += 2
            length = (pair >> 12) + 3
            distance = (pair & 0x0FFF) + 1
            for _ in range(length):
                output.append(output[-distance])
        layout = (layout << 1) & 0xFFFFFFFF
        bits_remaining -= 1
    return bytes(output)

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
    segment2 = mio0_decode(rom[ROM_BASE:])
    glyphs = {}
    for letter, offset in ROM_OFFSETS.items():
        raw = segment2[offset:offset + 512]
        if len(raw) != 512:
            raise SystemExit(f"Segment 2 ends before glyph {letter}")
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
