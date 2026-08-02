#!/usr/bin/env python3
"""Reject native floating conversion/arithmetic in Q16 boundary probes."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def function_assembly(text: str, name: str) -> str:
    match = re.search(
        rf"(?ms)^{re.escape(name)}:\s*(.*?)(?:^\s*\.size\s+{re.escape(name)}\b|^\s*\.seh_endproc\b)",
        text,
    )
    if match is None:
        raise AssertionError(f"missing assembly body for {name}")
    return match.group(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("assembly", type=Path)
    args = parser.parse_args()
    assembly = args.assembly.read_text(encoding="utf-8")
    forbidden = re.compile(
        r"\b(?:cvt(?:t)?(?:ss|sd)2si\w*|cvtsi2(?:ss|sd)\w*|"
        r"addss|subss|mulss|divss|sqrtss|"
        r"___?(?:fixsfsi|floatsisf|addsf3|subsf3|mulsf3|divsf3))\b",
        re.IGNORECASE,
    )
    for name in ("probe_float_to_q16", "probe_q16_to_float"):
        body = function_assembly(assembly, name)
        hit = forbidden.search(body)
        if hit is not None:
            raise AssertionError(f"{name}: floating helper/instruction remains: {hit.group(0)}")
    print("Q16 conversion assembly: integer/bit operations only")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
