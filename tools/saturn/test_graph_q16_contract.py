#!/usr/bin/env python3
"""Static branch contract for the host-preprocessed render graph.

This checks the actual selected C branch after preprocessing, rather than raw
source spelling: TARGET_SATURN must select direct Q16 graph constructors while
the source/PC build must retain the original float calls.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def function_body(source: str, name: str) -> str:
    marker = f"static void {name}("
    start = source.find(marker)
    if start < 0:
        raise AssertionError(f"missing preprocessed function {name}")
    opening = source.find("{", start)
    if opening < 0:
        raise AssertionError(f"missing body for {name}")
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated body for {name}")


def inline_function_body(source: str, name: str) -> str:
    match = re.search(rf"\b{re.escape(name)}\s*\([^;]*?\)\s*\{{", source)
    if match is None:
        raise AssertionError(f"missing preprocessed inline function {name}")
    opening = source.find("{", match.start())
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1:index]
    raise AssertionError(f"unterminated body for {name}")


def require(body: str, needle: str, context: str) -> None:
    if needle not in body:
        raise AssertionError(f"{context}: missing {needle}")


def forbid(body: str, needle: str, context: str) -> None:
    if needle in body:
        raise AssertionError(f"{context}: forbidden {needle}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--saturn", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    saturn = args.saturn.read_text(encoding="utf-8")
    source = args.source.read_text(encoding="utf-8")

    saturn_ortho = function_body(saturn, "geo_process_ortho_projection")
    require(saturn_ortho, "sm64_saturn_mtxq_ortho", "Saturn ortho")
    require(saturn_ortho, "saturn_mtxq_write_wire", "Saturn ortho")
    forbid(saturn_ortho, "guOrtho", "Saturn ortho")

    saturn_perspective = function_body(saturn, "geo_process_perspective")
    if re.search(r"\bu16\s+perspNorm\s*=\s*\(0xffff\)\s*;", saturn_perspective) is None:
        raise AssertionError("Saturn perspective: perspNorm lacks deterministic initialization")
    require(saturn_perspective, "sm64_saturn_mtxq_perspective", "Saturn perspective")
    require(saturn_perspective, "saturn_mtxq_write_wire", "Saturn perspective")
    forbid(saturn_perspective, "guPerspective", "Saturn perspective")

    saturn_camera = function_body(saturn, "geo_process_camera")
    require(saturn_camera, "sm64_saturn_mtxq_lookat", "Saturn camera")
    forbid(saturn_camera, "mtxf_lookat", "Saturn camera")

    lookat_ctor = inline_function_body(saturn, "sm64_saturn_mtxq_lookat")
    if lookat_ctor.count("sm64_saturn_div_s64_s32") != 5:
        raise AssertionError("Saturn look-at must use five explicit DIVU divisions")
    if re.search(r"/\s*mag\b", lookat_ctor):
        raise AssertionError("Saturn look-at retains generic 64-bit division")

    saturn_object = function_body(saturn, "geo_process_object")
    require(saturn_object, "sm64_saturn_mtxq_rotate_zxy_and_translate", "Saturn object")
    forbid(saturn_object, "mtxf_rotate_zxy_and_translate", "Saturn object")

    for name in (
        "geo_process_translation_rotation",
        "geo_process_translation",
        "geo_process_billboard",
    ):
        forbid(function_body(saturn, name), "vec3s_to_vec3f", f"Saturn {name}")
    forbid(function_body(saturn, "geo_process_scale"), "vec3f_set", "Saturn scale")

    source_ortho = function_body(source, "geo_process_ortho_projection")
    require(source_ortho, "guOrtho", "source ortho")
    forbid(source_ortho, "sm64_saturn_mtxq_ortho", "source ortho")

    source_perspective = function_body(source, "geo_process_perspective")
    require(source_perspective, "guPerspective", "source perspective")
    forbid(source_perspective, "sm64_saturn_mtxq_perspective", "source perspective")

    source_camera = function_body(source, "geo_process_camera")
    require(source_camera, "mtxf_lookat", "source camera")
    forbid(source_camera, "sm64_saturn_mtxq_lookat", "source camera")

    for name in (
        "geo_process_translation_rotation",
        "geo_process_translation",
        "geo_process_billboard",
    ):
        require(function_body(source, name), "vec3s_to_vec3f", f"source {name}")

    print("graph Q16 branch contract: Saturn direct constructors; source float path preserved")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
