#!/usr/bin/env python3
"""Extract Castle entry gameplay/camera constants from the SM64 source.

This keeps the first M4 placement and camera derived from LevelScript,
collision, and camera code rather than from target-specific staging values.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from pathlib import Path


def source_spawn(script: str) -> tuple[int, int, int, int, int]:
    matches = re.findall(
        r"MARIO_POS\([^,]+,\s*[^,]+\*/\s*(-?\d+),\s*[^,]+,\s*[^,]+\*/\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\)",
        script,
    )
    if not matches:
        # Comment-independent form used if source formatting changes.
        matches = re.findall(r"MARIO_POS\(\s*(\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)", script)
    if not matches:
        # Current source carries comments between every argument.
        body = re.search(r"MARIO_POS\((.*?)\)", script, re.DOTALL)
        if body is None:
            raise ValueError("Castle LevelScript has no MARIO_POS")
        numbers = [int(value) for value in re.findall(r"(?<![A-Za-z_])-?\d+", re.sub(r"/\*.*?\*/", "", body.group(1)))]
        if len(numbers) != 5:
            raise ValueError("unexpected Castle MARIO_POS arguments")
        return tuple(numbers)
    values = tuple(map(int, matches[-1]))
    return values if len(values) == 5 else (1, *values)


def collision_floor(collision: str, x: int, y: int, z: int) -> tuple[int, int]:
    vertices = [tuple(map(int, row)) for row in re.findall(r"COL_VERTEX\(\s*(-?\d+),\s*(-?\d+),\s*(-?\d+)\s*\)", collision)]
    triangles = [tuple(map(int, row)) for row in re.findall(r"COL_TRI\(\s*(\d+),\s*(\d+),\s*(\d+)\s*\)", collision)]

    def edge(ax: int, az: int, bx: int, bz: int) -> int:
        return (x - ax) * (bz - az) - (z - az) * (bx - ax)

    candidates: list[tuple[int, int]] = []
    for index, triangle in enumerate(triangles):
        a, b, c = (vertices[vertex] for vertex in triangle)
        signs = (edge(a[0], a[2], b[0], b[2]), edge(b[0], b[2], c[0], c[2]), edge(c[0], c[2], a[0], a[2]))
        if not (all(value >= 0 for value in signs) or all(value <= 0 for value in signs)):
            continue
        ux, uy, uz = b[0] - a[0], b[1] - a[1], b[2] - a[2]
        vx, vy, vz = c[0] - a[0], c[1] - a[1], c[2] - a[2]
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        if ny <= 0 or abs(ny) < abs(nx) + abs(nz):
            continue
        height = round(a[1] - (nx * (x - a[0]) + nz * (z - a[2])) / ny)
        if height <= y + 100:
            candidates.append((height, index))
    if not candidates:
        raise ValueError("no upward Castle collision floor beneath MARIO_POS")
    return max(candidates)


def camera_config(
    camera: str, area: int, spawn: tuple[int, int, int]
) -> dict[str, object]:
    lobby_axis = re.search(
        r"void\s+set_fixed_cam_axis_sa_lobby\([^)]*\)\s*\{.*?"
        r"case AREA_CASTLE_LOBBY:.*?"
        r"vec3f_set\(sFixedModeBasePosition,\s*(-?\d+)\.f,\s*"
        r"(-?\d+)\.f,\s*(-?\d+)\.f\)",
        camera,
        re.DOTALL,
    )
    if lobby_axis is None:
        raise ValueError("missing Castle lobby fixed-camera base")
    entrance = re.search(r"cam_castle_lobby_entrance.*?vec3f_set\(sCastleEntranceOffset,\s*(-?\d+)\.f\s*-.*?,\s*(-?\d+)\.f\s*-.*?,\s*(-?\d+)\.f\s*-", camera, re.DOTALL)
    if entrance is None:
        raise ValueError("missing Castle lobby entrance camera target")
    trigger = re.search(
        r"\{\s*(\d+)\s*,\s*cam_castle_lobby_entrance\s*,\s*"
        r"(-?\d+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*,\s*"
        r"(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,",
        camera,
    )
    if trigger is None:
        raise ValueError("missing Castle lobby entrance camera trigger")
    scale = re.search(r"case AREA_CASTLE_LOBBY:\s*scaleToMario\s*=\s*([0-9.]+)f", camera)
    focus = re.search(r"focus\[1\]\s*\+=\s*focusFloorOff\s*\+\s*([0-9.]+)f", camera)
    if scale is None or focus is None:
        raise ValueError("missing Castle fixed-camera scale/focus constants")
    fixed_base = tuple(map(int, lobby_axis.groups()))
    entrance_base = tuple(map(int, entrance.groups()))
    trigger_values = tuple(map(int, trigger.groups()))
    trigger_area, tx, ty, tz, sx, sy, sz = trigger_values
    trigger_active = trigger_area == area and all(
        abs(value - center) <= extent
        for value, center, extent in zip(spawn, (tx, ty, tz), (sx, sy, sz))
    )
    return {
        "base": entrance_base if trigger_active else fixed_base,
        "fixed_base": fixed_base,
        "entrance_base": entrance_base,
        "entrance_trigger": {
            "area": trigger_area,
            "center": (tx, ty, tz),
            "extent": (sx, sy, sz),
            "active_at_spawn": trigger_active,
        },
        "follow_q16": round(float(scale.group(1)) * 65536),
        "focus_height": round(float(focus.group(1))),
        # update_fixed_camera() passes focMul=0.9f to calc_y_to_curr_floor().
        "focus_floor_scale_q16": round(0.9 * 65536),
    }


def extract(script_path: Path, collision_path: Path, camera_path: Path) -> dict[str, object]:
    script = script_path.read_text(encoding="utf-8")
    collision = collision_path.read_text(encoding="utf-8")
    camera = camera_path.read_text(encoding="utf-8")
    area, yaw_degrees, x, y, z = source_spawn(script)
    floor_height, floor_triangle = collision_floor(collision, x, y, z)
    camera_values = camera_config(camera, area, (x, y, z))
    return {
        "spawn": {"area": area, "yaw_degrees": yaw_degrees, "yaw_angle": round(yaw_degrees * 65536 / 360), "position": [x, y, z]},
        "collision": {"floor_height": floor_height, "floor_triangle": floor_triangle},
        "camera": {
            "base": list(camera_values["base"]),
            "fixed_base": list(camera_values["fixed_base"]),
            "entrance_base": list(camera_values["entrance_base"]),
            "entrance_trigger": {
                "area": camera_values["entrance_trigger"]["area"],
                "center": list(camera_values["entrance_trigger"]["center"]),
                "extent": list(camera_values["entrance_trigger"]["extent"]),
                "active_at_spawn": camera_values["entrance_trigger"]["active_at_spawn"],
            },
            "follow_q16": camera_values["follow_q16"],
            "focus_height": camera_values["focus_height"],
            "focus_floor_scale_q16": camera_values["focus_floor_scale_q16"],
            "fov_degrees": 45,
            "focal_length_320": round(160 / math.tan(math.radians(45 / 2))),
        },
        "source": {
            "script": {"path": str(script_path).replace("\\", "/"), "sha256": hashlib.sha256(script.encode()).hexdigest()},
            "collision": {"path": str(collision_path).replace("\\", "/"), "sha256": hashlib.sha256(collision.encode()).hexdigest()},
            "camera": {"path": str(camera_path).replace("\\", "/"), "sha256": hashlib.sha256(camera.encode()).hexdigest()},
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", type=Path, required=True)
    parser.add_argument("--collision", type=Path, required=True)
    parser.add_argument("--camera", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    result = extract(args.script, args.collision, args.camera)
    spawn, collision, camera = result["spawn"], result["collision"], result["camera"]
    lines = [
        "/* Generated from SM64 LevelScript, collision, and camera source. */",
        "#pragma once",
        f"#define SM64_CASTLE_SPAWN_AREA {spawn['area']}",
        f"#define SM64_CASTLE_SPAWN_YAW {spawn['yaw_angle']}",
        f"#define SM64_CASTLE_SPAWN_X {spawn['position'][0]}",
        f"#define SM64_CASTLE_SPAWN_Y {spawn['position'][1]}",
        f"#define SM64_CASTLE_SPAWN_Z {spawn['position'][2]}",
        f"#define SM64_CASTLE_SPAWN_FLOOR_Y {collision['floor_height']}",
        f"#define SM64_CASTLE_CAMERA_BASE_X {camera['base'][0]}",
        f"#define SM64_CASTLE_CAMERA_BASE_Y {camera['base'][1]}",
        f"#define SM64_CASTLE_CAMERA_BASE_Z {camera['base'][2]}",
        f"#define SM64_CASTLE_CAMERA_FOLLOW_Q16 {camera['follow_q16']}",
        f"#define SM64_CASTLE_CAMERA_FOCUS_Y {camera['focus_height']}",
        f"#define SM64_CASTLE_CAMERA_FOCUS_FLOOR_SCALE_Q16 {camera['focus_floor_scale_q16']}",
        f"#define SM64_CASTLE_CAMERA_FOCAL_LENGTH {camera['focal_length_320']}",
    ]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines) + "\n", encoding="utf-8")
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
