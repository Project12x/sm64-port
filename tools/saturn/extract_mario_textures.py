#!/usr/bin/env python3
"""Extract local-only Mario RGBA16 textures into Saturn RGB1555 words.

The input is a user-supplied US SM64 ROM/archive and assets.json's pinned
offset map. Nintendo pixels are emitted only beneath build/, never tracked.
"""
from __future__ import annotations

import argparse, hashlib, json, zipfile
from pathlib import Path

NAMES = ("mario_eyes_center", "mario_logo", "mario_sideburn", "mario_mustache")

def rom_bytes(path: Path) -> bytes:
    if path.suffix.lower() != ".zip":
        return path.read_bytes()
    with zipfile.ZipFile(path) as archive:
        candidates = [item for item in archive.infolist() if item.filename.lower().endswith((".z64", ".n64", ".v64"))]
        if not candidates:
            raise ValueError("archive has no N64 ROM")
        return archive.read(max(candidates, key=lambda item: item.file_size))

def saturn_rgb1555(n64: int) -> int:
    return ((n64 & 1) << 15) | ((n64 >> 1) & 0x7FFF)

def mio0_decode(image: bytes, offset: int) -> bytes:
    if image[offset:offset + 4] != b"MIO0":
        raise ValueError(f"expected MIO0 at 0x{offset:X}")
    size = int.from_bytes(image[offset + 4:offset + 8], "big")
    comp = offset + int.from_bytes(image[offset + 8:offset + 12], "big")
    raw = offset + int.from_bytes(image[offset + 12:offset + 16], "big")
    mask, bits, out = offset + 16, 0, bytearray()
    while len(out) < size:
        if bits == 0: flags, mask, bits = image[mask], mask + 1, 8
        if flags & 0x80:
            out.append(image[raw]); raw += 1
        else:
            pair = int.from_bytes(image[comp:comp + 2], "big"); comp += 2
            count, distance = (pair >> 12) + 3, (pair & 0xFFF) + 1
            for _ in range(count): out.append(out[-distance])
        flags = (flags << 1) & 0xFF; bits -= 1
    return bytes(out[:size])

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--rom", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    args = parser.parse_args()
    rom, assets = rom_bytes(args.rom), json.loads(args.assets.read_text(encoding="utf-8"))
    lines = ["/* Local ROM-derived output: do not commit. */", "#pragma once"]
    manifest = {"rom_sha256": hashlib.sha256(rom).hexdigest(), "textures": {}}
    for name in NAMES:
        entry = assets[f"actors/mario/{name}.rgba16.png"]
        width, height, size, regions = entry
        location = regions["us"]
        if len(location) == 2:
            base, relative = location
            image, start = mio0_decode(rom, base), relative
        else:
            image, start = rom, location[0]
        data = image[start:start + size]
        if len(data) != size: raise ValueError(f"{name}: range outside ROM")
        words = [saturn_rgb1555(int.from_bytes(data[i:i + 2], "big")) for i in range(0, size, 2)]
        lines += [f"#define SM64_{name.upper()}_WIDTH {width}U", f"#define SM64_{name.upper()}_HEIGHT {height}U", f"static const uint16_t sm64_{name}_rgb1555[{len(words)}] = {{"]
        lines += ["    " + ", ".join(f"0x{word:04X}" for word in words[i:i + 8]) + "," for i in range(0, len(words), 8)]
        lines += ["};"]
        manifest["textures"][name] = {"offset": start, "bytes": size, "sha256": hashlib.sha256(data).hexdigest()}
    args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.manifest.parent.mkdir(parents=True, exist_ok=True); args.manifest.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
if __name__ == "__main__": main()
