#!/usr/bin/env python3
"""Lower Castle Area 1's original COL_* stream to a Saturn-local s16 bank.

The source collision command stream remains authoritative; this tool only
removes special-object records that the first Saturn room does not instantiate
and emits the original vertices/surface groups unchanged for the SM64 surface
loader.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


def surface_values(header: Path) -> dict[str, int]:
    text = header.read_text(encoding="utf-8")
    values: dict[str, int] = {}
    for name, value in re.findall(r"^#define\s+(SURFACE_[A-Z0-9_]+)\s+0x([0-9A-Fa-f]+)", text, re.MULTILINE):
        values[name] = int(value, 16)
    return values


def compile_stream(source: str, values: dict[str, int]) -> tuple[list[int], dict[str, int]]:
    vertices = [tuple(map(int, row)) for row in re.findall(
        r"COL_VERTEX\(\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", source)]
    groups: list[tuple[int, list[tuple[int, int, int]]]] = []
    starts = list(re.finditer(r"COL_TRI_INIT\(\s*([A-Z0-9_]+)\s*,\s*(\d+)\s*\)", source))
    for index, match in enumerate(starts):
        end = starts[index + 1].start() if index + 1 < len(starts) else source.find("COL_TRI_STOP", match.end())
        body = source[match.end():end]
        tris = [tuple(map(int, row)) for row in re.findall(
            r"COL_TRI\(\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)", body)]
        name, expected = match.group(1), int(match.group(2))
        if name not in values:
            raise ValueError(f"unknown surface type {name}")
        if len(tris) != expected:
            raise ValueError(f"{name}: expected {expected} triangles, found {len(tris)}")
        groups.append((values[name], tris))

    stream = [0x40, len(vertices)]
    for vertex in vertices:
        stream.extend(vertex)
    for surface_type, triangles in groups:
        stream.extend((surface_type, len(triangles)))
        for triangle in triangles:
            stream.extend(triangle)
    stream.append(0x41)
    stream.append(0x42)
    return stream, {"vertices": len(vertices), "surface_groups": len(groups), "triangles": sum(len(group[1]) for group in groups), "words": len(stream)}


def write_header(stream: list[int], stats: dict[str, int], output: Path) -> None:
    lines = [
        "/* Generated from levels/castle_inside/areas/1/collision.inc.c; do not edit. */",
        "#pragma once",
        "#include <stdint.h>",
        f"#define SM64_CASTLE_COLLISION_VERTEX_COUNT {stats['vertices']}U",
        f"#define SM64_CASTLE_COLLISION_TRIANGLE_COUNT {stats['triangles']}U",
        f"#define SM64_CASTLE_COLLISION_WORD_COUNT {stats['words']}U",
        "static const int16_t sm64_castle_collision_data[SM64_CASTLE_COLLISION_WORD_COUNT] = {",
    ]
    lines.extend("    " + ", ".join(str(value) for value in stream[offset:offset + 16]) + "," for offset in range(0, len(stream), 16))
    lines.extend(("};", ""))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--surface-header", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.read_text(encoding="utf-8")
    stream, stats = compile_stream(source, surface_values(args.surface_header))
    write_header(stream, stats, args.output)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps({"source": str(args.source).replace("\\", "/"), **stats, "special_objects": "omitted; collision loader only"}, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
