#!/usr/bin/env python3
"""Flatten Castle Interior Area 1's static Fast3D display-list subset.

The output is an intermediate source record for the forthcoming Saturn world
IR. It intentionally includes only the target's first fixed-camera root
lists: vertex cache loads, texture state, nested lists, and triangle macros.
It does not pretend to execute level scripts, behavior code, or effects.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

from extract_mario_actor import blocks, ints, vertex_groups
from inspect_castle_area import root_display_lists


def texture_name(args: str) -> str | None:
    identifiers = re.findall(r"\b[A-Za-z_]\w*\b", args)
    ignored = {"G_IM_FMT_RGBA", "G_IM_FMT_IA", "G_IM_FMT_CI", "G_IM_SIZ_4b", "G_IM_SIZ_8b", "G_IM_SIZ_16b"}
    values = [value for value in identifiers if value not in ignored]
    return values[-1] if values else None


def flatten(display_lists: dict[str, str], vertices: dict[str, list[tuple[int, int, int, int, int]]],
            name: str, layer: str, out: list[dict[str, object]], texture: str | None = None,
            stack: tuple[str, ...] = ()) -> str | None:
    if name in stack:
        raise ValueError(f"recursive display list: {' -> '.join(stack + (name,))}")
    body = display_lists.get(name)
    if body is None:
        raise ValueError(f"missing display list {name}")
    cache: list[tuple[int, int, int, int, int] | None] = [None] * 32
    current_texture = texture
    for macro, args in re.findall(r"(gs\w+)\(([^;]*?)\)", body, re.DOTALL):
        if macro == "gsDPSetTextureImage":
            current_texture = texture_name(args)
        elif macro == "gsSPTexture" and "G_OFF" in args:
            current_texture = None
        elif macro == "gsSPVertex":
            group = re.match(r"\s*(\w+)", args)
            if group is None or group.group(1) not in vertices:
                raise ValueError(f"unknown vertex group in {name}: {args}")
            values = ints(args[group.end():])
            if len(values) != 2:
                raise ValueError(f"unexpected gsSPVertex arguments in {name}: {args}")
            count, destination = values
            loaded = vertices[group.group(1)]
            if count != len(loaded) or destination + count > len(cache):
                raise ValueError(f"invalid vertex cache load in {name}: {args}")
            cache[destination:destination + count] = loaded
        elif macro in ("gsSP1Triangle", "gsSP2Triangles", "gsSP1Quadrangle"):
            values = ints(args)
            if macro == "gsSP1Triangle":
                triangles = (values[:3],)
            elif macro == "gsSP2Triangles":
                triangles = (values[:3], values[4:7])
            else:
                triangles = (values[:3], (values[0], values[2], values[3]))
            for triangle in triangles:
                if len(triangle) != 3 or any(index >= len(cache) or cache[index] is None for index in triangle):
                    raise ValueError(f"invalid triangle in {name}: {args}")
                out.append({
                    "source_display_list": name,
                    "layer": layer,
                    "texture": current_texture,
                    "positions": [list(cache[index][0:3]) for index in triangle],
                    "uv": [[cache[index][3], cache[index][4]] for index in triangle],
                })
        elif macro == "gsSPDisplayList":
            child = re.match(r"\s*(\w+)", args)
            if child:
                current_texture = flatten(display_lists, vertices, child.group(1), layer, out, current_texture, stack + (name,))
    return current_texture


def extract(area: Path) -> dict[str, object]:
    source = "\n".join(path.read_text(encoding="utf-8") for path in sorted(area.glob("*/model.inc.c"), key=lambda path: int(path.parent.name)))
    display_lists, vertices = blocks(source, "Gfx"), vertex_groups(source)
    roots = root_display_lists((area / "geo.inc.c").read_text(encoding="utf-8"))
    triangles: list[dict[str, object]] = []
    for root in roots:
        flatten(display_lists, vertices, root["display_list"], root["layer"], triangles)
    return {
        "schema": "sm64-saturn-static-scene-intake",
        "version": 1,
        "name": "castle_inside_area_1_root",
        "source": "levels/castle_inside/areas/1",
        "roots": roots,
        "triangle_count": len(triangles),
        "layers": dict(Counter(str(item["layer"]) for item in triangles)),
        "textured_triangle_count": sum(item["texture"] is not None for item in triangles),
        "texture_use": dict(Counter(str(item["texture"]) for item in triangles if item["texture"] is not None)),
        "triangles": triangles,
        "limits": ["Static Area 1 roots only", "No level-script interpreter", "No texture bytes", "No alpha/decal renderer yet"],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--area", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(extract(args.area), indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
