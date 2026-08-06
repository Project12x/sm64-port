#!/usr/bin/env python3
"""Emit a deterministic C byte array for a validated S64B actor bank."""

from __future__ import annotations

import argparse
from pathlib import Path


def emit(source: Path, output: Path) -> None:
    data = source.read_bytes()
    lines = [
        "/* Generated; source identity is carried by the S64B header. */",
        "#include <stdint.h>",
        "const uint8_t sm64_saturn_mario_actor_bank_data[] __attribute__((section(\".cart_rodata\"), aligned(4))) = {",
    ]
    for offset in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{value:02X}" for value in data[offset:offset + 16]) + ",")
    lines.extend([
        "};",
        "const uint32_t sm64_saturn_mario_actor_bank_size =",
        f"    {len(data)}U;",
        "",
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    emit(args.input, args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
