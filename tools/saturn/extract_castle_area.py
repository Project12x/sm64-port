#!/usr/bin/env python3
"""Flatten Castle Interior Area 1's source-selected Fast3D display lists.

The output is the static-geometry half of the Saturn world IR.  SM64's actual
GeoLayout still chooses the roots at runtime; this pass retains each root's
identity and layer while resolving vertex cache, texture state, nested lists,
and triangle macros ahead of time.
"""
from __future__ import annotations

import argparse
import ast
import json
import re
from collections import Counter
from pathlib import Path

from extract_mario_actor import blocks, ints, vertex_groups
from inspect_castle_area import root_display_lists


FAST3D_CONSTANTS = {
    "G_TX_RENDERTILE": 0,
    "G_TX_LOADTILE": 7,
    "G_TX_NOMASK": 0,
    "G_TX_NOLOD": 0,
    "G_TX_NOMIRROR": 0,
    "G_TX_MIRROR": 1,
    "G_TX_WRAP": 0,
    "G_TX_CLAMP": 2,
    "G_TEXTURE_IMAGE_FRAC": 2,
}


def split_args(args: str) -> list[str]:
    """Split a C macro argument list while retaining nested expressions."""
    values: list[str] = []
    start = 0
    depth = 0
    for index, character in enumerate(args):
        if character == "(":
            depth += 1
        elif character == ")":
            depth -= 1
        elif character == "," and depth == 0:
            values.append(args[start:index].strip())
            start = index + 1
    values.append(args[start:].strip())
    return values


def expression_int(expression: str) -> int:
    """Evaluate the integer-only Fast3D expressions used by SM64 assets."""
    rewritten = re.sub(
        r"\b[A-Za-z_]\w*\b",
        lambda match: str(FAST3D_CONSTANTS[match.group(0)])
        if match.group(0) in FAST3D_CONSTANTS else match.group(0),
        expression,
    )
    node = ast.parse(rewritten, mode="eval")

    def evaluate(value: ast.AST) -> int:
        if isinstance(value, ast.Expression):
            return evaluate(value.body)
        if isinstance(value, ast.Constant) and isinstance(value.value, int):
            return int(value.value)
        if isinstance(value, ast.UnaryOp) and isinstance(value.op, (ast.UAdd, ast.USub, ast.Invert)):
            operand = evaluate(value.operand)
            if isinstance(value.op, ast.UAdd):
                return operand
            if isinstance(value.op, ast.USub):
                return -operand
            return ~operand
        if isinstance(value, ast.BinOp) and isinstance(
            value.op,
            (ast.Add, ast.Sub, ast.Mult, ast.FloorDiv, ast.LShift, ast.RShift, ast.BitOr, ast.BitAnd),
        ):
            left, right = evaluate(value.left), evaluate(value.right)
            operations = {
                ast.Add: lambda: left + right,
                ast.Sub: lambda: left - right,
                ast.Mult: lambda: left * right,
                ast.FloorDiv: lambda: left // right,
                ast.LShift: lambda: left << right,
                ast.RShift: lambda: left >> right,
                ast.BitOr: lambda: left | right,
                ast.BitAnd: lambda: left & right,
            }
            return operations[type(value.op)]()
        raise ValueError(f"unsupported Fast3D integer expression: {expression}")

    return evaluate(node)


def texture_image(args: str) -> dict[str, object]:
    values = split_args(args)
    if len(values) != 4 or not re.fullmatch(r"[A-Za-z_]\w*", values[3]):
        raise ValueError(f"unsupported gsDPSetTextureImage: {args}")
    return {
        "format": values[0],
        "size": values[1],
        "width": expression_int(values[2]),
        "texture": values[3],
    }


def set_tile(args: str) -> dict[str, object]:
    values = split_args(args)
    if len(values) != 12:
        raise ValueError(f"unsupported gsDPSetTile: {args}")
    cmt, cms = expression_int(values[6]), expression_int(values[9])
    return {
        "format": values[0],
        "size": values[1],
        "line": expression_int(values[2]),
        "tmem": expression_int(values[3]),
        "tile": expression_int(values[4]),
        "palette": expression_int(values[5]),
        "clamp_t": bool(cmt & FAST3D_CONSTANTS["G_TX_CLAMP"]),
        "mirror_t": bool(cmt & FAST3D_CONSTANTS["G_TX_MIRROR"]),
        "mask_t": expression_int(values[7]),
        "shift_t": expression_int(values[8]),
        "clamp_s": bool(cms & FAST3D_CONSTANTS["G_TX_CLAMP"]),
        "mirror_s": bool(cms & FAST3D_CONSTANTS["G_TX_MIRROR"]),
        "mask_s": expression_int(values[10]),
        "shift_s": expression_int(values[11]),
    }


def set_tile_size(args: str) -> tuple[int, dict[str, int]]:
    values = split_args(args)
    if len(values) != 5:
        raise ValueError(f"unsupported gsDPSetTileSize: {args}")
    tile, uls, ult, lrs, lrt = map(expression_int, values)
    return tile, {
        "uls": uls,
        "ult": ult,
        "lrs": lrs,
        "lrt": lrt,
        "width": (lrs - uls) // (1 << FAST3D_CONSTANTS["G_TEXTURE_IMAGE_FRAC"]) + 1,
        "height": (lrt - ult) // (1 << FAST3D_CONSTANTS["G_TEXTURE_IMAGE_FRAC"]) + 1,
    }


def initial_state() -> dict[str, object]:
    return {
        "cache": [None] * 32,
        "image": None,
        "tiles": {},
        "tmem": {},
        "texture_on": False,
        "texture_scale_s": 0xFFFF,
        "texture_scale_t": 0xFFFF,
        "texture_level": 0,
        "texture_tile": 0,
        "combine": None,
        "cycle_type": None,
        "render_mode": None,
        # SM64 enables G_CULL_BACK in the global display-list setup before
        # traversing level geometry. Individual model lists may temporarily
        # clear it, so preserve that state across nested display lists.
        "cull_back": True,
        "cull_front": False,
    }


def render_texture_state(state: dict[str, object]) -> tuple[str | None, list[dict[str, object]], dict[str, object] | None]:
    if not state["texture_on"]:
        return None, [], None
    tiles: dict[int, dict[str, object]] = state["tiles"]  # type: ignore[assignment]
    tmem: dict[int, dict[str, object]] = state["tmem"]  # type: ignore[assignment]
    first = int(state["texture_tile"])
    level = int(state["texture_level"])
    bindings: list[dict[str, object]] = []
    primary_tile: dict[str, object] | None = None
    for tile_index in range(first, first + level + 1):
        tile = tiles.get(tile_index)
        if tile is None:
            continue
        if primary_tile is None:
            primary_tile = dict(tile)
        binding = tmem.get(int(tile["tmem"]))
        if binding is not None:
            bindings.append({**binding, "render_tile": tile_index})
    if primary_tile is None:
        return None, bindings, None
    primary_tile.update({
        "sp_scale_s": int(state["texture_scale_s"]),
        "sp_scale_t": int(state["texture_scale_t"]),
        "sp_level": level,
        "sp_tile": first,
        "combine": state["combine"],
        "cycle_type": state["cycle_type"],
        "render_mode": state["render_mode"],
        "bindings": bindings,
    })
    return (str(bindings[0]["texture"]) if bindings else None), bindings, primary_tile


def display_list_macros(body: str):
    """Yield macro arguments without truncating nested C expressions."""
    cursor = 0
    while (match := re.search(r"(gs\w+)\(", body[cursor:])) is not None:
        macro = match.group(1)
        start = cursor + match.end()
        depth, end = 1, start
        while depth and end < len(body):
            if body[end] == "(":
                depth += 1
            elif body[end] == ")":
                depth -= 1
            end += 1
        if depth:
            raise ValueError(f"unclosed {macro} macro")
        yield macro, body[start:end - 1]
        cursor = end


def flatten(display_lists: dict[str, str], vertices: dict[str, list[tuple[int, int, int, int, int]]],
            name: str, layer: str, out: list[dict[str, object]], state: dict[str, object] | None = None,
            stack: tuple[str, ...] = (), root_display_list: str | None = None) -> None:
    if name in stack:
        raise ValueError(f"recursive display list: {' -> '.join(stack + (name,))}")
    body = display_lists.get(name)
    if body is None:
        raise ValueError(f"missing display list {name}")
    state = initial_state() if state is None else state
    root_display_list = name if root_display_list is None else root_display_list
    cache: list[tuple[int, int, int, int, int] | None] = state["cache"]  # type: ignore[assignment]
    for macro, args in display_list_macros(body):
        if macro == "gsDPSetTextureImage":
            state["image"] = texture_image(args)
        elif macro == "gsDPSetTile":
            descriptor = set_tile(args)
            tiles: dict[int, dict[str, object]] = state["tiles"]  # type: ignore[assignment]
            previous = tiles.get(int(descriptor["tile"]), {})
            tiles[int(descriptor["tile"])] = {**previous, **descriptor}
        elif macro == "gsDPSetTileSize":
            tile_index, extent = set_tile_size(args)
            tiles = state["tiles"]  # type: ignore[assignment]
            tiles[tile_index] = {**tiles.get(tile_index, {"tile": tile_index}), **extent}
        elif macro in ("gsDPLoadBlock", "gsDPLoadTile"):
            values = split_args(args)
            if len(values) < 5:
                raise ValueError(f"unsupported {macro}: {args}")
            load_tile = expression_int(values[0])
            tiles = state["tiles"]  # type: ignore[assignment]
            descriptor = tiles.get(load_tile)
            image = state["image"]
            if descriptor is None or image is None:
                raise ValueError(f"{macro} without image/load-tile state in {name}")
            binding = {
                **image,
                "load_tile": load_tile,
                "tmem": int(descriptor["tmem"]),
                "load_uls": expression_int(values[1]),
                "load_ult": expression_int(values[2]),
                "load_lrs": expression_int(values[3]),
                "load_kind": macro,
            }
            state["tmem"][int(descriptor["tmem"])] = binding  # type: ignore[index]
        elif macro == "gsSPTexture":
            values = split_args(args)
            if len(values) != 5:
                raise ValueError(f"unsupported gsSPTexture: {args}")
            state["texture_scale_s"] = expression_int(values[0])
            state["texture_scale_t"] = expression_int(values[1])
            state["texture_level"] = expression_int(values[2])
            state["texture_tile"] = expression_int(values[3])
            state["texture_on"] = "G_OFF" not in values[4]
        elif macro == "gsDPSetCombineMode":
            state["combine"] = split_args(args)
        elif macro == "gsDPSetCycleType":
            state["cycle_type"] = args.strip()
        elif macro == "gsDPSetRenderMode":
            state["render_mode"] = split_args(args)
        elif macro in ("gsSPClearGeometryMode", "gsSPSetGeometryMode"):
            enabled = macro == "gsSPSetGeometryMode"
            modes = {value.strip() for value in args.split("|")}
            if "G_CULL_BACK" in modes:
                state["cull_back"] = enabled
            if "G_CULL_FRONT" in modes:
                state["cull_front"] = enabled
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
                texture, bindings, tile = render_texture_state(state)
                out.append({
                    "root_display_list": root_display_list,
                    "source_display_list": name,
                    "layer": layer,
                    "texture": texture,
                    "textures": [binding["texture"] for binding in bindings],
                    "tile": tile,
                    "cull_back": bool(state["cull_back"]),
                    "cull_front": bool(state["cull_front"]),
                    "positions": [list(cache[index][0:3]) for index in triangle],
                    "uv": [[cache[index][3], cache[index][4]] for index in triangle],
                })
        elif macro == "gsSPDisplayList":
            child = re.match(r"\s*(\w+)", args)
            if child:
                flatten(display_lists, vertices, child.group(1), layer, out, state,
                        stack + (name,), root_display_list)


def extract(area: Path) -> dict[str, object]:
    source = "\n".join(path.read_text(encoding="utf-8") for path in sorted(area.glob("*/model.inc.c"), key=lambda path: int(path.parent.name)))
    display_lists, vertices = blocks(source, "Gfx"), vertex_groups(source)
    roots = root_display_lists((area / "geo.inc.c").read_text(encoding="utf-8"))
    triangles: list[dict[str, object]] = []
    for root in roots:
        flatten(display_lists, vertices, root["display_list"], root["layer"], triangles,
                root_display_list=root["display_list"])
    return {
        "schema": "sm64-saturn-static-scene-intake",
        "version": 3,
        "name": "castle_inside_area_1_root",
        "source": "levels/castle_inside/areas/1",
        "roots": roots,
        "triangle_count": len(triangles),
        "layers": dict(Counter(str(item["layer"]) for item in triangles)),
        "textured_triangle_count": sum(item["texture"] is not None for item in triangles),
        "texture_use": dict(Counter(str(item["texture"]) for item in triangles if item["texture"] is not None)),
        "triangles": triangles,
        "limits": ["Static geometry payloads only", "Runtime GeoLayout selects roots", "No texture bytes"],
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
