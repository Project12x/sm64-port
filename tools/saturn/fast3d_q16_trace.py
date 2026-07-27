#!/usr/bin/env python3
"""Decode a ``SM64_SATURN_FAST3D_Q16_TRACE`` probe window from Ymir.

The trace is evidence plumbing, not a performance feature.  It converts the
SH-2 big-endian fixed layout into JSON that can be reviewed and then promoted
deliberately into the host Q16 differential corpus.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

MAGIC = 0x51363454  # Q64T
VERSION = 1
HEADER_SIZE = 16
# The C payload is 161 bytes followed by SH-2's three-byte tail pad, because
# the record has 32-bit members and the target aligns the next array element
# to four bytes.  Keep this explicit rather than guessing from the member
# list; a trace build's DWARF confirms sizeof(record)==164.
RECORD_SIZE = 164


def decode(data: bytes) -> dict[str, object]:
    if len(data) < HEADER_SIZE:
        raise ValueError(f"trace is {len(data)} bytes; need at least {HEADER_SIZE}")
    magic, version, write_count, dropped_count = struct.unpack_from(">4I", data)
    if magic != MAGIC:
        raise ValueError(f"unexpected trace magic 0x{magic:08X}; expected 0x{MAGIC:08X}")
    if version != VERSION:
        raise ValueError(f"unsupported trace version {version}")
    available = min(write_count, 16, (len(data) - HEADER_SIZE) // RECORD_SIZE)
    records: list[dict[str, object]] = []
    for index in range(available):
        offset = HEADER_SIZE + index * RECORD_SIZE
        values = struct.unpack_from(">16i4hI18f3B3B3b4B3x", data, offset)
        mp = [list(values[row * 4 : row * 4 + 4]) for row in range(4)]
        viewport = list(values[16:20])
        geometry_mode = values[20]
        floats = values[21:39]
        records.append({
            "slot": index,
            "mp_q16": mp,
            "viewport": viewport,
            "geometry_mode": geometry_mode,
            "source_xyz": [list(floats[i * 3 : i * 3 + 3]) for i in range(3)],
            "float_clip_xyw": [list(floats[9 + i * 3 : 12 + i * 3]) for i in range(3)],
            "dir_col": list(values[39:42]),
            "amb_col": list(values[42:45]),
            "dir_dir": list(values[45:48]),
            "num_lights": values[48],
            "lighting_enabled": bool(values[49]),
        })
    return {
        "magic": "Q64T",
        "version": version,
        "write_count": write_count,
        "dropped_count": dropped_count,
        "records": records,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path, help="capture_hwtest.py JSON report")
    parser.add_argument("--output", type=Path, help="write decoded JSON here")
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    window = report.get("probe_window")
    if not isinstance(window, dict) or not isinstance(window.get("data"), list):
        raise ValueError("report has no probe_window.data byte array")
    result = decode(bytes(window["data"]))
    rendered = json.dumps(result, indent=2) + "\n"
    if args.output:
        args.output.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
