#!/usr/bin/env python3
"""Decode a captured ``sm64_saturn_fast3d_profile_t`` into named counters.

This is NOT the same block as ``tools/saturn/telemetry_decode.py``.  The two
answer different questions and read different memory:

* ``telemetry_decode.py`` decodes the *hwtest* telemetry block -- the "SAT0"
  structure that ``src/port/saturn/hwtest/main.c`` writes to the fixed address
  ``0x06030000``.  Only the hwtest disc ever writes it.
* This module decodes the *renderer* profile,
  ``sm64_saturn_fast3d_profile_t`` (``src/port/saturn/gfx/
  saturn_fast3d_frontend.h``), which is the first member of the sourceboot
  build's ``_sourceboot_fast3d`` frontend object.  It lives at whatever
  address that object was linked to, has no magic word, and is captured by
  pointing ``capture_hwtest.py --probe-address`` at it -- landing in the
  report's ``probe_window`` block, not its ``telemetry`` block.

Running a sourceboot capture through the hwtest decoder therefore reports
"unexpected telemetry magic 0x...": correct behaviour on the wrong data, since
``0x06030000`` in a sourceboot image is ordinary HWRAM that nothing stamps.

Layout derivation
-----------------
Field offsets are NOT written down here.  They are derived by parsing the real
header at decode time, so a counter appended to the struct is picked up
automatically and a counter *inserted* cannot silently shift every later field
under a stale hardcoded offset.  The parse is strict: any member this ABI model
does not cover (arrays, nested aggregates, bitfields, unknown types) is a loud
parse error rather than a silently dropped field.

The ABI model is the one sh-elf-gcc uses for this struct's member types:
big-endian SH-2, every scalar naturally aligned to its own size, struct
alignment equal to the widest member, total size rounded up to that alignment.
``test_tools.py`` cross-checks every derived offset and the total ``sizeof``
against a compiled ``offsetof()`` probe, and against two committed captures
whose real cross-compiled ``sizeof`` values (228 and 248 bytes) are recorded in
their ``probe_window`` byte counts.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import sys
from pathlib import Path
from typing import Any, NamedTuple

REPO_ROOT = Path(__file__).resolve().parents[2]
PROFILE_HEADER = REPO_ROOT / "src" / "port" / "saturn" / "gfx" / "saturn_fast3d_frontend.h"
STRUCT_TAG = "sm64_saturn_fast3d_profile"
STRUCT_TYPEDEF = "sm64_saturn_fast3d_profile_t"

# The decoder derives all fields from the profile header, including these
# append-only dual-pipeline diagnostics.  Keeping their public spellings here
# makes capture consumers able to identify the ownership evidence without
# carrying any offsets of their own.
DUAL_PIPELINE_COUNTERS = (
    "master_worker_started",
    "slave_worker_started",
    "vdp1_commands",
    "vdp2_active_layers",
    "pipeline_faults",
)

GOURAUD_SAVINGS_COUNTERS = (
    "gouraud_tables_saved",
    "gouraud_bytes_saved",
)

VDP2_FRAME_COUNTERS = (
    "master_transform_count",
    "slave_transform_count",
    "ordering_count",
    "dma_wait_ticks_last",
    "dma_wait_ticks_accum",
    "vdp1_wait_ticks_last",
    "vdp1_wait_ticks_accum",
)

# ctype -> (size in bytes, big-endian struct format).  Alignment equals size
# for every one of these on sh-elf, which is what makes the layout model below
# a two-line calculation rather than a target-description table.
C_TYPES: dict[str, tuple[int, str]] = {
    "uint8_t": (1, ">B"),
    "int8_t": (1, ">b"),
    "uint16_t": (2, ">H"),
    "int16_t": (2, ">h"),
    "uint32_t": (4, ">I"),
    "int32_t": (4, ">i"),
    "float": (4, ">f"),
}

_MEMBER_RE = re.compile(r"^(?P<ctype>[A-Za-z_][A-Za-z0-9_]*)\s+(?P<name>[A-Za-z_][A-Za-z0-9_]*)$")


class ProfileField(NamedTuple):
    name: str
    ctype: str
    offset: int
    size: int

    @property
    def end(self) -> int:
        return self.offset + self.size


class ProfileLayout(NamedTuple):
    fields: tuple[ProfileField, ...]
    size: int
    alignment: int
    header: Path

    def field(self, name: str) -> ProfileField:
        for candidate in self.fields:
            if candidate.name == name:
                return candidate
        raise KeyError(f"{STRUCT_TYPEDEF} has no field named {name!r}")


def strip_c_comments(text: str) -> str:
    """Remove /* */ and // comments, preserving nothing but the code."""
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def extract_struct_body(header_text: str, tag: str = STRUCT_TAG) -> str:
    """Return the member declarations of ``struct <tag>``.

    Deliberately naive about nesting: this struct is a flat list of scalars by
    design (see the header's own "appended at the very end" notes), and a
    member this function cannot handle is caught by parse_members' strict
    matching rather than mis-parsed here.
    """
    opener = f"typedef struct {tag} {{"
    start = header_text.find(opener)
    if start < 0:
        raise ValueError(f"{tag} definition not found (looked for {opener!r})")
    body_start = start + len(opener)
    closer = header_text.find("}", body_start)
    if closer < 0:
        raise ValueError(f"{tag} definition is not terminated")
    return header_text[body_start:closer]


def parse_members(body: str) -> tuple[tuple[str, str], ...]:
    """Parse a comment-stripped struct body into ordered (ctype, name) pairs.

    Strict on purpose.  A member that does not match "<known scalar type>
    <identifier>" -- an array, a nested struct, a bitfield, a pointer, a
    preprocessor conditional, a type this module has no size for -- raises.
    Skipping it instead would shift every following offset and produce a
    decode that looks fine and is wrong, which is exactly the failure mode
    this module exists to prevent.
    """
    members: list[tuple[str, str]] = []
    for statement in body.split(";"):
        statement = " ".join(statement.split())
        if not statement:
            continue
        match = _MEMBER_RE.match(statement)
        if match is None:
            raise ValueError(
                f"cannot derive layout: unsupported member declaration {statement!r} in "
                f"struct {STRUCT_TAG}. This decoder models only naturally-aligned scalar "
                f"members ({', '.join(sorted(C_TYPES))}); extend C_TYPES or the parser "
                f"rather than letting the offset model drift from the header."
            )
        ctype = match.group("ctype")
        if ctype not in C_TYPES:
            raise ValueError(
                f"cannot derive layout: member {match.group('name')!r} has unmodelled type "
                f"{ctype!r} in struct {STRUCT_TAG}"
            )
        members.append((ctype, match.group("name")))
    if not members:
        raise ValueError(f"struct {STRUCT_TAG} parsed to zero members")
    return tuple(members)


def layout_members(members: tuple[tuple[str, str], ...]) -> ProfileLayout:
    """Apply the sh-elf scalar layout rules to an ordered member list."""
    fields: list[ProfileField] = []
    offset = 0
    alignment = 1
    for ctype, name in members:
        size = C_TYPES[ctype][0]
        align = size  # every modelled type is naturally aligned
        alignment = max(alignment, align)
        offset = (offset + align - 1) // align * align
        fields.append(ProfileField(name, ctype, offset, size))
        offset += size
    total = (offset + alignment - 1) // alignment * alignment
    return ProfileLayout(tuple(fields), total, alignment, PROFILE_HEADER)


_LAYOUT_CACHE: dict[Path, ProfileLayout] = {}


def profile_layout(header: Path | None = None) -> ProfileLayout:
    """Derive the current layout from the real header (cached per path)."""
    path = (header or PROFILE_HEADER).resolve()
    cached = _LAYOUT_CACHE.get(path)
    if cached is None:
        body = strip_c_comments(extract_struct_body(path.read_text(encoding="utf-8")))
        cached = layout_members(parse_members(body))._replace(header=path)
        _LAYOUT_CACHE[path] = cached
    return cached


class ProfileSizeMismatch(ValueError):
    """A capture's probe byte count disagrees with the header's sizeof.

    Raised rather than decoding anyway: captures are archived evidence, and a
    capture taken against an older struct must be *detectably* older instead of
    quietly reinterpreted under today's offsets.
    """

    def __init__(self, captured_bytes: int, layout: ProfileLayout) -> None:
        self.captured_bytes = captured_bytes
        self.layout = layout
        detail = ""
        if captured_bytes < layout.size:
            missing = [field.name for field in layout.fields if field.end > captured_bytes]
            detail = (
                f"; the capture stops inside/before field {missing[0]!r}, so the "
                f"{len(missing)} field(s) from there on ({', '.join(missing)}) did not exist "
                f"in the build that produced it"
            )
        else:
            detail = (
                f"; {captured_bytes - layout.size} trailing byte(s) beyond the profile "
                f"(the probe window may cover more of sm64_saturn_fast3d_frontend_t than "
                f"the profile alone)"
            )
        super().__init__(
            f"profile size mismatch: capture holds {captured_bytes} byte(s) but "
            f"{STRUCT_TYPEDEF} is {layout.size} byte(s) in {layout.header}{detail}. "
            f"Re-run with --partial to decode only the fields the capture fully covers."
        )


def decode_profile(
    data: bytes | list[int],
    layout: ProfileLayout | None = None,
    partial: bool = False,
) -> dict[str, Any]:
    """Decode a raw big-endian profile dump into named counters.

    ``partial=True`` decodes only the fields the capture fully covers and
    records which ones it could not, for reading archived captures taken
    against an older (or newer) struct.  It is opt-in: the default refuses a
    size mismatch outright.
    """
    if not isinstance(data, (bytes, bytearray)):
        if not all(isinstance(byte, int) and 0 <= byte <= 255 for byte in data):
            raise ValueError("profile bytes must all be integers in range 0..255")
        data = bytes(data)
    layout = layout or profile_layout()
    if len(data) != layout.size and not partial:
        raise ProfileSizeMismatch(len(data), layout)

    values: dict[str, Any] = {}
    missing: list[str] = []
    for field in layout.fields:
        if field.end > len(data):
            missing.append(field.name)
            continue
        (value,) = struct.unpack_from(C_TYPES[field.ctype][1], data, field.offset)
        values[field.name] = value
    return {
        "struct": STRUCT_TYPEDEF,
        "header": str(layout.header),
        "layout_bytes": layout.size,
        "captured_bytes": len(data),
        "complete": not missing and len(data) == layout.size,
        "fields_missing": missing,
        "fields": values,
    }


def load_probe(source: Any) -> tuple[list[int], dict[str, Any]]:
    """Pull profile bytes out of any of the shapes a capture can arrive in.

    Accepts a full ``capture_hwtest.py`` report (uses its ``probe_window``), a
    bare Ymir ``mem.peek`` response (``result.data`` or a top-level ``data``),
    or a raw JSON byte array.
    """
    meta: dict[str, Any] = {}
    if isinstance(source, dict):
        window = source.get("probe_window")
        if isinstance(window, dict):
            meta["probe_address"] = window.get("address")
            meta["probe_target"] = window.get("target")
            source = window.get("data")
        elif window is None and "probe_window" in source:
            raise ValueError(
                "capture report has probe_window = null: it was taken without "
                "--probe-address, so it contains no profile dump"
            )
        else:
            result = source.get("result")
            if isinstance(result, dict) and "data" in result:
                meta["probe_address"] = result.get("address")
                source = result.get("data")
            elif "data" in source:
                meta["probe_address"] = source.get("address")
                source = source.get("data")
    if not isinstance(source, list):
        raise ValueError(
            "input must be a capture report with probe_window, a Ymir mem.peek "
            "response, or a raw JSON byte array"
        )
    return source, meta


def _format_address(address: Any) -> Any:
    if isinstance(address, int):
        return f"0x{address:08X}"
    return address


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n", 1)[0])
    parser.add_argument("input", nargs="?", type=Path, help="capture JSON; defaults to stdin")
    parser.add_argument(
        "--partial",
        action="store_true",
        help="decode a capture whose byte count differs from the header's sizeof, "
        "reporting which fields are absent instead of failing",
    )
    parser.add_argument(
        "--header",
        type=Path,
        help=f"override the header the layout is derived from (default {PROFILE_HEADER})",
    )
    parser.add_argument(
        "--layout-only",
        action="store_true",
        help="print the derived field/offset table and exit without decoding",
    )
    args = parser.parse_args(argv)
    try:
        layout = profile_layout(args.header)
        if args.layout_only:
            print(
                json.dumps(
                    {
                        "struct": STRUCT_TYPEDEF,
                        "header": str(layout.header),
                        "sizeof": layout.size,
                        "alignment": layout.alignment,
                        "fields": [field._asdict() for field in layout.fields],
                    },
                    indent=2,
                )
            )
            return 0
        text = args.input.read_text(encoding="utf-8") if args.input else sys.stdin.read()
        data, meta = load_probe(json.loads(text))
        report = decode_profile(data, layout, partial=args.partial)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    if "probe_address" in meta:
        report["probe_address"] = _format_address(meta["probe_address"])
    if meta.get("probe_target"):
        report["probe_target"] = meta["probe_target"]
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
