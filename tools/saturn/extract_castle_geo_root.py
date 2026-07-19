#!/usr/bin/env python3
"""Extract an exact SM64 GeoLayout root for the Saturn graph bridge."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def extract(source: str, symbol: str) -> tuple[str, list[tuple[str, str]]]:
    match = re.search(
        rf"const\s+GeoLayout\s+{re.escape(symbol)}\[\]\s*=\s*\{{(.*?)\n\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"GeoLayout {symbol} not found")
    body = match.group(1).strip()
    displays = re.findall(r"GEO_DISPLAY_LIST\(\s*(\w+)\s*,\s*(\w+)\s*\)", body)
    if not displays:
        raise ValueError(f"GeoLayout {symbol} has no display-list nodes")
    return body, displays


def write_header(body: str, displays: list[tuple[str, str]], output: Path) -> None:
    lines = [
        "/* Generated from levels/castle_inside/areas/1/geo.inc.c; do not edit. */",
        "#pragma once",
        f"#define SM64_SATURN_CASTLE_GRAPH_DISPLAY_LIST_COUNT {len(displays)}U",
        "static const GeoLayout sm64_saturn_castle_geo_root[] = {",
    ]
    lines.extend(f"    {line.strip()}" for line in body.splitlines())
    lines.append("};")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--symbol", default="castle_geo_000F30")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    body, displays = extract(args.input.read_text(encoding="utf-8"), args.symbol)
    write_header(body, displays, args.output)


if __name__ == "__main__":
    main()
