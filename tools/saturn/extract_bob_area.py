#!/usr/bin/env python3
"""Extract Bob-omb Battlefield Area 1 into the shared Saturn Mesh IR.

This is deliberately a thin BOB adapter: Fast3D macro parsing, vertex-cache
semantics, texture-state tracking, and nested display-list walking are reused
from ``extract_castle_area``.  Only the GeoLayout root selection and the
intake-to-Mesh-IR projection are BOB-specific.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any

from extract_castle_area import flatten
from extract_mario_actor import blocks, vertex_groups


def root_display_lists(geo_source: str) -> list[dict[str, str]]:
    """Return every explicitly layered BOB terrain display-list root."""
    roots = [
        {"layer": layer, "display_list": display_list}
        for layer, display_list in re.findall(
            r"GEO_DISPLAY_LIST\(\s*(\w+)\s*,\s*(\w+)\s*\)", geo_source
        )
    ]
    if not roots:
        raise ValueError("BOB Area 1 GeoLayout has no display-list roots")
    return roots


def extract_area(area: Path, level_name: str, area_id: int) -> dict[str, Any]:
    """Extract one static area through the shared Fast3D intake path."""
    models = sorted(area.glob("*/model.inc.c"), key=lambda path: int(path.parent.name))
    if not models:
        raise ValueError(f"no BOB model files below {area}")
    source = "\n".join(path.read_text(encoding="utf-8") for path in models)
    display_lists, vertices = blocks(source, "Gfx"), vertex_groups(source)
    roots = root_display_lists((area / "geo.inc.c").read_text(encoding="utf-8"))
    triangles: list[dict[str, Any]] = []
    for root in roots:
        flatten(
            display_lists,
            vertices,
            root["display_list"],
            root["layer"],
            triangles,
            root_display_list=root["display_list"],
        )
    return {
        "schema": "sm64-saturn-static-scene-intake",
        "version": 3,
        "name": f"{level_name}_area_{area_id}_root",
        "source": f"levels/{level_name}/areas/{area_id}",
        "roots": roots,
        "model_files": [path.as_posix() for path in models],
        "triangle_count": len(triangles),
        "layers": dict(Counter(str(item["layer"]) for item in triangles)),
        "textured_triangle_count": sum(item["texture"] is not None for item in triangles),
        "texture_use": dict(
            Counter(str(item["texture"]) for item in triangles if item["texture"] is not None)
        ),
        "triangles": triangles,
        "limits": [
            "Static world-space terrain payload only",
            "Runtime GeoLayout and dynamic objects are out of scope",
            "Texture bytes are produced by Task 3",
        ],
    }


def intake(area: Path) -> dict[str, Any]:
    """Compatibility wrapper for existing BOB sourceboot consumers."""
    return extract_area(area, "bob", 1)


def mesh_ir(document: dict[str, Any]) -> dict[str, Any]:
    """Convert the shared static-scene intake to Mesh IR v2.

    Vertices are welded only when every raster attribute that can affect the
    primitive agrees: position, UV, texture identity, and tile state.  This is
    deliberately stricter than position-only welding, which is unsafe at hard
    edges and is prohibited by the sprint plan.
    """
    positions: list[list[int]] = []
    uvs: list[list[int]] = []
    triangles: list[dict[str, Any]] = []
    material_ids: dict[str, int] = {}
    materials: list[dict[str, Any]] = []
    vertex_ids: dict[str, int] = {}

    def vertex_for(
        position: list[int],
        uv: list[int],
        texture: str | None,
        tile: dict[str, Any] | None,
    ) -> int:
        key = json.dumps(
            {"position": position, "uv": uv, "texture": texture, "tile": tile},
            sort_keys=True,
            separators=(",", ":"),
        )
        existing = vertex_ids.get(key)
        if existing is not None:
            return existing
        index = len(positions)
        vertex_ids[key] = index
        positions.append(position)
        uvs.append(uv)
        return index

    def material_for(texture: str | None) -> int:
        key = texture or "__untextured__"
        if key not in material_ids:
            material_ids[key] = len(materials)
            materials.append({"id": material_ids[key], "rgb555": [31, 31, 31], "texture": texture})
        return material_ids[key]

    for source_id, item in enumerate(document["triangles"]):
        texture = item.get("texture")
        tile = item.get("tile")
        indices = [
            vertex_for(position, uv, texture, tile)
            for position, uv in zip(item["positions"], item["uv"])
        ]
        triangle: dict[str, Any] = {
            "source": source_id,
            "material": material_for(texture),
            "indices": indices,
        }
        if tile is not None:
            triangle["texture_tile"] = {
                "texture": texture,
                "state": tile,
            }
        triangles.append(triangle)

    return {
        "schema": "sm64-saturn-mesh-ir",
        "version": 2,
        "name": document["name"],
        "source": {
            "path": document["source"],
            "intake_schema": document["schema"],
            "intake_version": document["version"],
        },
        "static_world_space": True,
        "positions": positions,
        "materials": materials,
        "triangles": triangles,
        "vertex_attributes": {"uv": uvs},
        "pairing_forbidden_triangles": [],
        "validation_poses": [],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--area", type=Path, required=True)
    parser.add_argument("--intake-output", type=Path, required=True)
    parser.add_argument("--mesh-ir-output", type=Path, required=True)
    args = parser.parse_args()
    report = intake(args.area)
    mesh = mesh_ir(report)
    args.intake_output.parent.mkdir(parents=True, exist_ok=True)
    args.mesh_ir_output.parent.mkdir(parents=True, exist_ok=True)
    args.intake_output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    args.mesh_ir_output.write_text(json.dumps(mesh, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
