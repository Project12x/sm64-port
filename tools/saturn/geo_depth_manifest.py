#!/usr/bin/env python3
"""Generate a deterministic, fail-closed Saturn geo-walk capacity manifest.

The source path currently has no production iterative dispatcher.  This tool
therefore owns the capacity proof first: it scans every supplied GeoLayout
source (or accepts a checked descriptor from a generated/link-time producer),
accounts for structural nesting plus shared/held/callback edges, and emits a
16-frame-aligned frame capacity with a recorded identity.  A later dispatcher
may consume the generated header only after the linker/map gate proves
placement.

Capacity rounding policy (owner-approved 2026-08-09): the requirement
(max proven depth + safety margin) is rounded up to the next 16-frame
boundary.  The original next-power-of-two rounding over-allocated at real
full-game scale (requirement 188 -> 256 frames = 4,096 B), pushing the
LWRAM traversal arena past the reserved slave-stack floor; alignment
rounding keeps the deterministic aligned bound without the exponential
blow-up (188 -> 192 frames = 3,072 B).  Capacity never drops below the
requirement, and the runtime independently latches
SM64_SATURN_GEO_WALK_RUNTIME_OVERFLOW fail-closed if the static bound is
ever exceeded.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Iterable, Mapping

from write_if_changed import write_text_if_changed


SCHEMA = "sm64-saturn-geo-depth-manifest-v1"
INPUT_SCHEMA = "sm64-saturn-geo-depth-input-v1"
FRAME_BYTES = 16
DEFAULT_SAFETY_MARGIN = 16


class ManifestError(ValueError):
    """Raised when a depth input cannot prove a safe capacity."""


def _comment_free(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)


def _nonnegative_int(value: object, field: str, identity: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or value < 0:
        raise ManifestError(f"{identity}: {field} must be a non-negative integer")
    return value


def _identity(path: Path, root: Path | None) -> str:
    resolved = path.resolve()
    if root is not None:
        try:
            return resolved.relative_to(root.resolve()).as_posix()
        except ValueError as exc:
            raise ManifestError(f"source is outside manifest root: {path}") from exc
    return resolved.as_posix()


def scan_source(path: Path, identity: str) -> dict[str, object]:
    if not path.is_file():
        raise ManifestError(f"missing geo-depth input: {path}")
    clean = _comment_free(path.read_text(encoding="utf-8"))
    structural_depth = 0
    token_re = re.compile(r"\bGEO_(?:OPEN_NODE|CLOSE_NODE)\s*\(\s*\)")
    layout_starts = list(re.finditer(
        r"\b(?:static\s+)?const\s+GeoLayout\s+[A-Za-z0-9_]+\s*\[\]\s*=\s*\{",
        clean,
    ))
    blocks: list[str] = []
    if not layout_starts:
        blocks = [clean]
    else:
        for index, start in enumerate(layout_starts):
            end = layout_starts[index + 1].start() if index + 1 < len(layout_starts) else len(clean)
            blocks.append(clean[start.end():end])
    for block in blocks:
        depth = 0
        unmatched_closes = 0
        for token in token_re.finditer(block):
            if token.group(0).lstrip().startswith("GEO_OPEN_NODE"):
                depth += 1
                structural_depth = max(structural_depth, depth)
            elif depth:
                depth -= 1
            else:
                # A few upstream layouts close the implicit root sentinel
                # once more than their explicit GEO_OPEN_NODE count.
                unmatched_closes += 1
        if unmatched_closes > 1:
            raise ManifestError(f"{identity}: more than one implicit-root close")

    shared_child_edges = len(re.findall(r"\bGEO_BRANCH(?:_AND_LINK)?\s*\(", clean))
    held_object_edges = len(re.findall(r"\bGEO_HELD_OBJECT\s*\(", clean))
    callback_edges = len(re.findall(r"\bGEO_ASM\s*\(", clean))
    max_depth = structural_depth + shared_child_edges + held_object_edges + callback_edges
    return {
        "identity": identity,
        "kind": "source",
        "structural_depth": structural_depth,
        "shared_child_edges": shared_child_edges,
        "held_object_edges": held_object_edges,
        "callback_edges": callback_edges,
        "max_depth": max_depth,
    }


def load_descriptor(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise ManifestError(f"missing geo-depth input: {path}")
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise ManifestError(f"invalid geo-depth JSON {path}: {exc}") from exc
    if not isinstance(payload, dict) or payload.get("schema") != INPUT_SCHEMA:
        raise ManifestError(f"{path}: unsupported geo-depth input schema")
    identity = payload.get("identity")
    if not isinstance(identity, str) or not identity:
        raise ManifestError(f"{path}: identity is required")
    fields = {
        name: _nonnegative_int(payload.get(name), name, identity)
        for name in (
            "structural_depth",
            "shared_child_edges",
            "held_object_edges",
            "callback_edges",
            "max_depth",
        )
    }
    expected = (
        fields["structural_depth"]
        + fields["shared_child_edges"]
        + fields["held_object_edges"]
        + fields["callback_edges"]
    )
    if fields["max_depth"] < expected:
        raise ManifestError(f"{identity}: claimed max_depth is an undercount")
    return {"identity": identity, "kind": "descriptor", **fields}


CAPACITY_ALIGNMENT_FRAMES = 16


def _aligned_capacity(required: int) -> int:
    """Round the required frame count up to the 16-frame capacity policy."""
    if required < 1:
        required = 1
    remainder = required % CAPACITY_ALIGNMENT_FRAMES
    if remainder:
        required += CAPACITY_ALIGNMENT_FRAMES - remainder
    return required


def _canonical_records(records: Iterable[Mapping[str, object]]) -> list[dict[str, object]]:
    return [dict(record) for record in sorted(records, key=lambda item: str(item["identity"]))]


def build_manifest(
    *,
    sources: Iterable[tuple[Path, str]] = (),
    descriptors: Iterable[Path] = (),
    safety_margin: int = DEFAULT_SAFETY_MARGIN,
) -> dict[str, object]:
    if safety_margin < 0:
        raise ManifestError("safety margin must be non-negative")
    records: list[dict[str, object]] = [scan_source(path, identity) for path, identity in sources]
    records.extend(load_descriptor(path) for path in descriptors)
    if not records:
        raise ManifestError("at least one geo-depth input is required")
    records = _canonical_records(records)
    identities = [str(record["identity"]) for record in records]
    duplicates = sorted({identity for identity in identities if identities.count(identity) > 1})
    if duplicates:
        raise ManifestError("duplicate geo-depth identity: " + ", ".join(duplicates))
    max_proven_depth = max(int(record["max_depth"]) for record in records)
    required = max_proven_depth + safety_margin
    capacity = _aligned_capacity(required)
    canonical = {
        "schema": SCHEMA,
        "frame_bytes": FRAME_BYTES,
        "safety_margin": safety_margin,
        "max_proven_depth": max_proven_depth,
        "capacity": capacity,
        "inputs": records,
    }
    encoded = json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode("utf-8")
    canonical["input_sha256"] = hashlib.sha256(encoded).hexdigest()
    return canonical


def verify_report(path: Path) -> None:
    try:
        report = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ManifestError(f"cannot read manifest report {path}: {exc}") from exc
    if not isinstance(report, dict) or report.get("schema") != SCHEMA:
        raise ManifestError("unsupported geo-depth manifest schema")
    max_depth = _nonnegative_int(report.get("max_proven_depth"), "max_proven_depth", "manifest")
    margin = _nonnegative_int(report.get("safety_margin"), "safety_margin", "manifest")
    capacity = _nonnegative_int(report.get("capacity"), "capacity", "manifest")
    if capacity < _aligned_capacity(max_depth + margin):
        raise ManifestError(
            "manifest capacity is below the 16-frame-aligned proven depth plus safety margin")
    inputs = report.get("inputs")
    if not isinstance(inputs, list) or not inputs:
        raise ManifestError("manifest inputs are missing")
    records = _canonical_records(inputs)
    if records != inputs:
        raise ManifestError("manifest inputs are not deterministically ordered")
    identities = [str(record.get("identity", "")) for record in records]
    if any(not identity for identity in identities):
        raise ManifestError("manifest input identity is missing")
    if len(set(identities)) != len(identities):
        raise ManifestError("manifest contains duplicate input identities")
    observed_max = 0
    for record in records:
        identity = str(record["identity"])
        values = {
            name: _nonnegative_int(record.get(name), name, identity)
            for name in (
                "structural_depth",
                "shared_child_edges",
                "held_object_edges",
                "callback_edges",
                "max_depth",
            )
        }
        expected = sum(values[name] for name in (
            "structural_depth", "shared_child_edges", "held_object_edges", "callback_edges"))
        if values["max_depth"] < expected:
            raise ManifestError(f"{identity}: manifest input max_depth is an undercount")
        observed_max = max(observed_max, values["max_depth"])
    if observed_max != max_depth:
        raise ManifestError("manifest max_proven_depth disagrees with its inputs")
    canonical = {
        "schema": SCHEMA,
        "frame_bytes": FRAME_BYTES,
        "safety_margin": margin,
        "max_proven_depth": max_depth,
        "capacity": capacity,
        "inputs": records,
    }
    expected_hash = hashlib.sha256(
        json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode("utf-8")
    ).hexdigest()
    if report.get("input_sha256") != expected_hash:
        raise ManifestError("manifest input identity digest is stale or forged")


def emit_header(path: Path, report: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    text = (
        "#ifndef SM64_SATURN_GEO_DEPTH_MANIFEST_H\n"
        "#define SM64_SATURN_GEO_DEPTH_MANIFEST_H\n\n"
        f"#define SM64_SATURN_GEO_TRAVERSAL_CAPACITY {int(report['capacity'])}U\n"
        f"#define SM64_SATURN_GEO_TRAVERSAL_MAX_PROVEN_DEPTH {int(report['max_proven_depth'])}U\n"
        f"#define SM64_SATURN_GEO_TRAVERSAL_SAFETY_MARGIN {int(report['safety_margin'])}U\n"
        f"#define SM64_SATURN_GEO_TRAVERSAL_FRAME_BYTES {FRAME_BYTES}U\n"
        f"#define SM64_SATURN_GEO_TRAVERSAL_INPUT_SHA256 \"{report['input_sha256']}\"\n\n"
        "#endif\n"
    )
    write_text_if_changed(path, text, encoding="utf-8", newline="\n")


def emit_linker_fragment(path: Path, report: Mapping[str, object]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    size = int(report["capacity"]) * FRAME_BYTES
    text = (
        "/* Generated by geo_depth_manifest.py; do not edit. */\n"
        f"PROVIDE(__sourceboot_geo_traversal_expected_capacity = {int(report['capacity'])});\n"
        f"PROVIDE(__sourceboot_geo_traversal_expected_size = 0x{size:X});\n"
    )
    write_text_if_changed(path, text, encoding="utf-8", newline="\n")


def _parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", action="append", type=Path, default=[])
    parser.add_argument("--source-dir", action="append", type=Path, default=[])
    parser.add_argument("--input", action="append", type=Path, default=[])
    parser.add_argument("--root", type=Path)
    parser.add_argument("--safety-margin", type=int, default=DEFAULT_SAFETY_MARGIN)
    parser.add_argument("--output-header", type=Path)
    parser.add_argument("--output-linker", type=Path)
    parser.add_argument("--output-json", type=Path)
    parser.add_argument("--verify-json", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    args = _parser().parse_args(argv)
    try:
        if args.verify_json is not None:
            verify_report(args.verify_json)
            print("geo depth manifest verification: PASS")
            return 0
        root = args.root.resolve() if args.root is not None else None
        source_paths = list(args.source)
        for directory in args.source_dir:
            if not directory.is_dir():
                raise ManifestError(f"missing geo-depth source directory: {directory}")
            source_paths.extend(
                path for path in directory.rglob("*.c")
                if "geo" in path.name.lower()
            )
        sources = [(_path, _identity(_path, root)) for _path in source_paths]
        report = build_manifest(
            sources=sources,
            descriptors=args.input,
            safety_margin=args.safety_margin,
        )
        if args.output_header is not None:
            emit_header(args.output_header, report)
        if args.output_linker is not None:
            emit_linker_fragment(args.output_linker, report)
        if args.output_json is not None:
            write_text_if_changed(
                args.output_json,
                json.dumps(report, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )
        print(
            f"geo depth manifest: PASS (inputs={len(report['inputs'])} "
            f"max={report['max_proven_depth']} capacity={report['capacity']})"
        )
        return 0
    except (ManifestError, OSError) as exc:
        print(f"geo depth manifest: FAIL: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
