#!/usr/bin/env python3
"""Classify SM64 triangle pairs for Saturn quad/UV conversion.

The source scan is intentionally conservative: C display-list macros provide
triangle topology, while an optional JSON primitive file supplies decoded UVs
and material identity. No Nintendo assets are required or read by default.
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path
from typing import Any


TRIANGLE_RE = re.compile(r"gsSP1Triangle\s*\(\s*([^)]*)\)|gsSP2Triangles\s*\(([^)]*)\)")


def source_scan(root: Path) -> dict[str, Any]:
    one = two = quadrangle = 0
    static_triangles = 0
    files = 0
    for path in root.rglob("*"):
        if path.suffix not in {".c", ".h", ".inc.c"} or not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8", errors="ignore")
        except OSError:
            continue
        matches = list(TRIANGLE_RE.finditer(text))
        quadrangles = re.findall(r"gsSP1Quadrangle\s*\(", text)
        if not matches and not quadrangles:
            continue
        files += 1
        for match in matches:
            if match.group(1) is not None:
                one += 1
                static_triangles += 1
            else:
                two += 1
                static_triangles += 2
        quadrangle += len(quadrangles)
    return {
        "source_root": str(root),
        "files_with_triangle_macros": files,
        "gsSP1Triangle": one,
        "gsSP2Triangles": two,
        "gsSP1Quadrangle": quadrangle,
        "static_triangles": static_triangles,
    }


def uv_is_rectangle(uvs: list[list[float]]) -> bool:
    unique = {(round(float(uv[0]), 6), round(float(uv[1]), 6)) for uv in uvs}
    if len(unique) != 4:
        return False
    us = sorted({uv[0] for uv in unique})
    vs = sorted({uv[1] for uv in unique})
    return len(us) == 2 and len(vs) == 2


def shared_uvs_match(first: dict[str, Any], second: dict[str, Any]) -> bool:
    """Require the two triangles to agree on UVs at shared vertices."""
    first_indices = [int(value) for value in first.get("indices", [])]
    second_indices = [int(value) for value in second.get("indices", [])]
    first_uvs = first.get("uvs", [])
    second_uvs = second.get("uvs", [])
    if len(first_indices) != len(first_uvs) or len(second_indices) != len(second_uvs):
        return False
    first_by_index = dict(zip(first_indices, first_uvs))
    second_by_index = dict(zip(second_indices, second_uvs))
    for index in set(first_by_index) & set(second_by_index):
        left = tuple(round(float(value), 6) for value in first_by_index[index])
        right = tuple(round(float(value), 6) for value in second_by_index[index])
        if left != right:
            return False
    return True


def classify_primitives(primitives: list[dict[str, Any]]) -> dict[str, Any]:
    reasons: Counter[str] = Counter()
    candidates = 0
    direct_quads = 0
    for index in range(0, len(primitives) - 1, 2):
        first = primitives[index]
        second = primitives[index + 1]
        candidates += 1
        if first.get("material") != second.get("material"):
            reasons["material_mismatch"] += 1
            continue
        first_indices = [int(value) for value in first.get("indices", [])]
        second_indices = [int(value) for value in second.get("indices", [])]
        shared = set(first_indices) & set(second_indices)
        if len(shared) != 2 or len(set(first_indices + second_indices)) != 4:
            reasons["topology_not_quad"] += 1
            continue
        if "uvs" not in first or "uvs" not in second:
            reasons["uv_missing"] += 1
            continue
        if not shared_uvs_match(first, second):
            reasons["shared_uv_mismatch"] += 1
            continue
        uvs = [list(map(float, uv)) for uv in first["uvs"] + second["uvs"]]
        if not uv_is_rectangle(uvs):
            reasons["uv_domain_not_rectangle"] += 1
            continue
        width = max(uv[0] for uv in uvs) - min(uv[0] for uv in uvs)
        height = max(uv[1] for uv in uvs) - min(uv[1] for uv in uvs)
        if round(width) % 8 or round(height) % 8:
            reasons["texture_extent_not_multiple_of_8"] += 1
            continue
        direct_quads += 1
    return {
        "triangle_pair_candidates": candidates,
        "direct_textured_quad_candidates": direct_quads,
        "rejection_reasons": dict(sorted(reasons.items())),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True, help="SM64 source/data root to scan")
    parser.add_argument("--primitives", type=Path, help="optional decoded triangle JSON array")
    parser.add_argument("--report", type=Path, help="write JSON report to this path")
    args = parser.parse_args()

    report: dict[str, Any] = {"schema": 1, "source": source_scan(args.root)}
    if args.primitives:
        primitives = json.loads(args.primitives.read_text(encoding="utf-8"))
        if not isinstance(primitives, list):
            raise SystemExit("--primitives must contain a JSON array")
        report["decoded_primitives"] = classify_primitives(primitives)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    else:
        print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
