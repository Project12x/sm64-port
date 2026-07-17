#!/usr/bin/env python3
"""Decode the fixed Saturn hardware-test telemetry block.

The input is either a Ymir JSON-RPC response (``result.data``) or a raw JSON
array of byte values copied from ``mem.peek``.  Saturn words are big-endian.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any


BASE_ADDRESS = 0x06010000
WORD_COUNT = 16
BLOCK_BYTES = WORD_COUNT * 4
EXT_BASE_ADDRESS = BASE_ADDRESS + BLOCK_BYTES
EXT_WORD_COUNT = 14
EXT_BLOCK_BYTES = EXT_WORD_COUNT * 4
MAGIC = 0x53415430
EXT_MAGIC = 0x53415458
EXPECTED_VERSION = 1
EXPECTED_PHASE = 1
EXPECTED_CART_ID = 0x5C
EXPECTED_CART_BYTES = 0x00400000
STATUS_NAMES = {
    0: "cart_present",
    1: "cart_pass",
    2: "dma_pass",
    3: "vdp1_pass",
    31: "complete",
}
FIELD_NAMES = (
    "magic",
    "version",
    "phase",
    "status",
    "cart_id",
    "cart_bytes",
    "test_bytes",
    "first_bad_offset",
    "expected",
    "observed",
    "cpu_copy_ticks",
    "scu_cart_to_wram_ticks",
    "scu_wram_to_vdp1_ticks",
    "vdp1_draw_ticks",
    "vdp1_command_count",
    "vdp1_pixel_estimate",
)
EXT_FIELD_NAMES = (
    "magic",
    "version",
    "cpu_cached_ticks",
    "cpu_uncached_ticks",
    "cpu_dmac_ticks",
    "cpu_dmac_pass",
    "vdp1_modes_mask",
    "vdp1_quad_ticks",
    "vdp1_triangle_ticks",
    "vdp1_gouraud_ticks",
    "vdp1_transparency_ticks",
    "vdp1_concave_ticks",
    "vdp1_textured_ticks",
    "vdp1_textured_triangle_ticks",
)


def load_bytes(path: Path | None) -> list[int]:
    text = path.read_text(encoding="utf-8") if path else sys.stdin.read()
    try:
        value: Any = json.loads(text)
    except json.JSONDecodeError:
        value = None
        for line in text.splitlines():
            if not line.strip():
                continue
            candidate = json.loads(line)
            if isinstance(candidate, dict) and isinstance(candidate.get("result", {}).get("data"), list):
                value = candidate
                break
        if value is None:
            raise ValueError("input is neither JSON nor JSON-lines containing result.data")
    if isinstance(value, dict):
        value = value.get("result", {}).get("data")
    if not isinstance(value, list) or not all(isinstance(byte, int) for byte in value):
        raise ValueError("input must be a JSON byte array or a Ymir response with result.data")
    if any(byte < 0 or byte > 255 for byte in value):
        raise ValueError("byte values must be in range 0..255")
    return value


def decode(data: list[int], require_complete: bool) -> dict[str, Any]:
    if len(data) < BLOCK_BYTES:
        raise ValueError(f"telemetry block requires {BLOCK_BYTES} bytes, got {len(data)}")
    words = [int.from_bytes(bytes(data[offset : offset + 4]), "big") for offset in range(0, BLOCK_BYTES, 4)]
    decoded = dict(zip(FIELD_NAMES, words, strict=True))
    if decoded["magic"] != MAGIC:
        raise ValueError(f"unexpected telemetry magic 0x{decoded['magic']:08X}")
    if decoded["version"] != EXPECTED_VERSION:
        raise ValueError(f"unsupported telemetry version {decoded['version']}")
    if decoded["phase"] != EXPECTED_PHASE:
        raise ValueError(f"unsupported telemetry phase {decoded['phase']}")
    status = decoded["status"]
    flags = {name: bool(status & (1 << bit)) for bit, name in STATUS_NAMES.items()}
    if require_complete and not flags["complete"]:
        raise ValueError("telemetry block is not marked complete")
    decoded["status_flags"] = flags
    decoded["base_address"] = f"0x{BASE_ADDRESS:08X}"
    if len(data) >= BLOCK_BYTES + EXT_BLOCK_BYTES:
        ext_offset = BLOCK_BYTES
        ext_words = [
            int.from_bytes(bytes(data[ext_offset + offset : ext_offset + offset + 4]), "big")
            for offset in range(0, EXT_BLOCK_BYTES, 4)
        ]
        decoded["extended"] = dict(zip(EXT_FIELD_NAMES, ext_words, strict=True))
        if decoded["extended"]["magic"] != EXT_MAGIC:
            raise ValueError(
                f"unexpected extended telemetry magic 0x{decoded['extended']['magic']:08X}"
            )
        if decoded["extended"]["version"] != EXPECTED_VERSION:
            raise ValueError(
                f"unsupported extended telemetry version {decoded['extended']['version']}"
            )
        decoded["extended"]["base_address"] = f"0x{EXT_BASE_ADDRESS:08X}"
    decoded["ok"] = bool(
        flags["complete"]
        and flags["cart_pass"]
        and flags["dma_pass"]
        and flags["vdp1_pass"]
        and decoded["cart_id"] == EXPECTED_CART_ID
        and decoded["cart_bytes"] == EXPECTED_CART_BYTES
    )
    return decoded


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, help="JSON file; defaults to stdin")
    parser.add_argument("--require-complete", action="store_true", help="reject a block before the final status bit")
    args = parser.parse_args()
    try:
        report = decode(load_bytes(args.input), args.require_complete)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
