#!/usr/bin/env python3
"""Reject a sourceboot image that serializes or loses the Task 1 kernels."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def function_body(disassembly: str, symbol: str) -> str:
    match = re.search(
        rf"(?ms)^\S+ <_?{re.escape(symbol)}>:\n(.*?)(?=^\S+ <|\Z)",
        disassembly,
    )
    if match is None:
        raise AssertionError(f"missing linked symbol: {symbol}")
    return match.group(1)


def require_order(body: str, *patterns: str) -> None:
    cursor = 0
    for pattern in patterns:
        match = re.search(pattern, body[cursor:])
        if match is None:
            raise AssertionError(f"missing or reordered instruction pattern: {pattern}")
        cursor += match.end()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asm", type=Path)
    args = parser.parse_args()
    disassembly = args.asm.read_text(encoding="utf-8", errors="replace")

    probe = function_body(disassembly, "sm64_saturn_sourceboot_q16_kernel_probe_run")
    launch = function_body(disassembly, "sm64_saturn_slavedriver_divu_q16_start_asm")

    # The probe must call launch, fill its latency shadow with both Jo-derived
    # multiplies, then read the quotient. An immediate DVDNTL read cannot pass.
    require_order(probe, r"jsr\s+@r1", r"dmuls\.l", r"xtrct", r"dmuls\.l", r"xtrct", r"mov\.l\s+@r1,r1")
    # The close-port must retain SlaveDriver's 64/32 DIVU start order.
    require_order(launch, r"mov\.l\s+r5,@\(0,r0\)", r"mov\.l\s+r1,@\(16,r0\)", r"mov\.l\s+r2,@\(20,r0\)")


if __name__ == "__main__":
    main()
