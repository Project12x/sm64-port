#!/usr/bin/env python3
"""Inventory the SM64 Castle Interior Area 1 source bank for the Saturn IR.

This is deliberately an intake/audit step, not a Fast3D interpreter.  It
keeps M3 tied to the existing decompilation files before a world renderer
exists, and records enough pressure data to choose its first fixed cameras.
No ROM-derived texture bytes are produced or committed.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path


def macro_count(source: str, name: str) -> int:
    return len(re.findall(rf"\b{name}\s*\(", source))


def texture_definitions(source: str) -> list[dict[str, str]]:
    pattern = re.compile(
        r"(?:ALIGNED8\s+)?static\s+const\s+Texture\s+(\w+)\[\]\s*=\s*\{\s*"
        r"#include\s+\"([^\"]+)\"", re.DOTALL
    )
    return [{"symbol": symbol, "include": include} for symbol, include in pattern.findall(source)]


def root_display_lists(source: str) -> list[dict[str, str]]:
    """Return the Area 1 root's explicitly layered display-list entry points."""
    root = re.search(r"const\s+GeoLayout\s+castle_geo_000F30\[\]\s*=\s*\{(.*?)GEO_RETURN\(\)", source, re.DOTALL)
    if root is None:
        raise ValueError("Castle Area 1 root castle_geo_000F30 is missing")
    return [
        {"layer": layer, "display_list": display_list}
        for layer, display_list in re.findall(r"GEO_DISPLAY_LIST\(\s*(\w+)\s*,\s*(\w+)\s*\)", root.group(1))
    ]


def collision_summary(source: str) -> dict[str, int]:
    declared = re.search(r"COL_VERTEX_INIT\(\s*(0x[0-9A-Fa-f]+|\d+)\s*\)", source)
    if declared is None:
        raise ValueError("Castle Area 1 collision has no COL_VERTEX_INIT")
    return {
        "declared_vertices": int(declared.group(1), 0),
        "vertices": macro_count(source, "COL_VERTEX"),
        "triangles": macro_count(source, "COL_TRI") + macro_count(source, "COL_TRI_SPECIAL"),
        "surface_groups": macro_count(source, "COL_TRI_INIT"),
    }


def inventory(area: Path, texture_file: Path) -> dict[str, object]:
    models = sorted(area.glob("*/model.inc.c"), key=lambda path: int(path.parent.name))
    if not models:
        raise ValueError(f"no model files below {area}")
    model_source = "\n".join(path.read_text(encoding="utf-8") for path in models)
    geo_source = (area / "geo.inc.c").read_text(encoding="utf-8")
    collision_source = (area / "collision.inc.c").read_text(encoding="utf-8")
    macro_names = ("gsSPVertex", "gsSP1Triangle", "gsSP2Triangles", "gsSP1Quadrangle", "gsDPSetTextureImage", "gsSPDisplayList")
    macros = {name: macro_count(model_source, name) for name in macro_names}
    static_triangles = macros["gsSP1Triangle"] + 2 * macros["gsSP2Triangles"] + 2 * macros["gsSP1Quadrangle"]
    roots = root_display_lists(geo_source)
    return {
        "schema": "sm64-saturn-castle-area-inventory",
        "version": 1,
        "source": {
            "area": "levels/castle_inside/areas/1",
            "models": [path.relative_to(area.parents[3]).as_posix() for path in models],
            "geo": "levels/castle_inside/areas/1/geo.inc.c",
            "collision": "levels/castle_inside/areas/1/collision.inc.c",
            "textures": "levels/castle_inside/texture.inc.c",
        },
        "model_file_count": len(models),
        "fast3d": {"macros": macros, "static_triangle_upper_bound": static_triangles},
        "root_display_lists": roots,
        "root_layers": dict(Counter(item["layer"] for item in roots)),
        "texture_definitions": texture_definitions(texture_file.read_text(encoding="utf-8")),
        "collision": collision_summary(collision_source),
        "next_gate": "Compile the listed root display lists through the shared Saturn IR, then capture fixed source-camera checkpoints before collision/player work.",
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--area", type=Path, required=True)
    parser.add_argument("--textures", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = inventory(args.area, args.textures)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
