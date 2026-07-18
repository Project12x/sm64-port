#!/usr/bin/env python3
"""Rank Castle source materials by projected coverage in the M3 fixed camera."""
from __future__ import annotations

import argparse
import json
from collections import defaultdict
from pathlib import Path


def project(point: list[int]) -> tuple[float, float]:
    x, y, z = point[0] + 1050, point[1] - 720, point[2] + 4200
    z = max(128, z)
    return 160 + x * 300 / z, 112 - y * 300 / z


def area(points: list[list[int]]) -> float:
    (ax, ay), (bx, by), (cx, cy) = (project(point) for point in points)
    return abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) * 0.5


def plan(scene: dict[str, object]) -> dict[str, object]:
    totals: dict[str, dict[str, float | int]] = defaultdict(lambda: {"triangles": 0, "projected_pixels": 0.0})
    for triangle, texture_index in zip(scene["triangles"], scene["texture_indices"]):
        if texture_index == 0xFF:
            continue
        name = scene["textures"][texture_index]
        totals[name]["triangles"] += 1
        totals[name]["projected_pixels"] += area([scene["positions"][index] for index in triangle])
    ranked = [{"texture": name, **values} for name, values in totals.items()]
    ranked.sort(key=lambda item: float(item["projected_pixels"]), reverse=True)
    return {"schema": "sm64-saturn-castle-camera-coverage", "camera": "castleviewer fixed source-space transform", "ranked_materials": ranked}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--intake", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = plan(json.loads(args.intake.read_text(encoding="utf-8")))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
