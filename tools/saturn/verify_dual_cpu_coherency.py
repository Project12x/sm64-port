#!/usr/bin/env python3
"""Cross-SH2 cache-coherency gate.

The target SH-2s have separate, non-coherent caches.  This gate has two
deliberately separate modes:

* ``--source`` is a host-only structural gate for the frame-bank protocol.
  It rejects cached completion records, direct peer reads, release ordering
  inversions, and a whole-cache purge in the accepted transform frame path.
* the historical ``<sym-file>`` / ``<nm> <elf>`` mode checks link addresses at
  the target-wave gate.  Target inspection is intentionally deferred while
  the developer's MSYS runtime is unavailable.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path


REQUIRED_UNCACHED_SYMBOLS = (
    "_s_transform_frame_bank",
    "_s_transform_phase_failed",
    "_s_terrain_spans_shared",
)
UNCACHED_BASE = 0x20000000


def source_failures(source: str, header: str) -> list[str]:
    failures: list[str] = []
    if not re.search(
        r"static\s+sm64_saturn_dual_frame_bank_t\s+"
        r"s_transform_frame_bank\s*\n\s*DEMO_CROSS_CPU_SHARED", source
    ):
        failures.append("transform completion bank is not explicitly uncached")
    if "s_transform_phase_ready" in source:
        failures.append("legacy cached transform ready flag remains")
    if "sm64_saturn_dual_frame_publish(" not in source:
        failures.append("transform producer does not publish an uncached frame record")
    required_consumer_helpers = (
        r"demo_position_valid_read\(lane, index\)",
        r"demo_view_read\(lane, primitive->indices\[corner\]\)",
        r"demo_projected_read\(lane, primitive->indices\[corner\]\)",
        r"demo_projected_read\(\s*lane,\s*primitive->indices\[source_corner\]\)",
    )
    for helper in required_consumer_helpers:
        if re.search(helper, source) is None:
            failures.append(f"consumer bypasses owner-sensitive alias: {helper}")
    if source.count("sm64_saturn_dual_frame_read_range(") < 4:
        failures.append("owner-sensitive helpers do not select all peer aliases")
    permitted_direct_writes = {
        "s_view": "&s_view[position]",
        "s_projected": "&s_projected[position]",
        "s_position_valid": "s_position_valid[position] =",
    }
    for line_number, line in enumerate(source.splitlines(), start=1):
        for bank, permitted_write in permitted_direct_writes.items():
            if f"{bank}[" not in line:
                continue
            # The multi-line declarations are not consumer accesses.  The
            # only accepted indexed runtime references are transform writes;
            # all renderer consumers must use the named helpers above.
            if "static " in line or permitted_write in line:
                continue
            failures.append(
                f"line {line_number}: direct {bank} consumer read bypasses "
                "the owner-sensitive alias")
    if "cpu_cache_purge(" in source:
        failures.append("whole-cache purge remains in accepted frame source")
    if "LWRAM_UNCACHED(physical)" not in header:
        failures.append("cache-through peer alias is missing")
    if "sm64_saturn_dual_frame_peer_ready(" not in source:
        failures.append("consumer does not wait on the uncached peer record")

    match = re.search(
        r"static inline void sm64_saturn_dual_frame_publish\(.*?\n}\n",
        header, re.DOTALL)
    if match is None:
        failures.append("frame publish helper is missing")
    else:
        publish = match.group(0)
        sequence = publish.find(".sequence = sequence;")
        count = publish.find(".count = count;")
        ready = publish.find(".ready = 1U;")
        if min(sequence, count, ready) < 0:
            failures.append("publish must write sequence, count, and ready")
        elif not sequence < count < ready:
            failures.append("ready must publish after sequence/count")
        elif publish.find("sm64_saturn_dual_frame_compiler_fence()") > sequence:
            failures.append("publish is missing the pre-header compiler fence")
    return failures


def check_source(source_path: Path, header_path: Path) -> int:
    failures = source_failures(source_path.read_text(encoding="utf-8"),
                               header_path.read_text(encoding="utf-8"))
    if failures:
        print("dual-CPU coherency source gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("dual-CPU coherency source gate OK: uncached release and "
          "cache-through peer reads")
    return 0


def self_test(source_path: Path, header_path: Path) -> int:
    """Mutation fixtures must fail, otherwise this source gate is decorative."""
    source = source_path.read_text(encoding="utf-8")
    header = header_path.read_text(encoding="utf-8")
    mutants = (
        (source.replace("DEMO_CROSS_CPU_SHARED;", ";", 1), header,
         "cached completion"),
        (source.replace("sm64_saturn_dual_frame_read_range(", "/* absent */("),
         header, "missing peer alias"),
        (source.replace(
            "demo_view_read(lane, primitive->indices[corner])->z",
            "s_view[primitive->indices[corner]].z", 1), header,
         "direct consumer bypass"),
        (source + "\nvoid rejected_path(void) { cpu_cache_purge(); }\n",
         header, "whole-cache purge"),
        (source, header.replace("bank->lane[lane].count = count;\n"
                                "    sm64_saturn_dual_frame_compiler_fence();\n"
                                "    bank->lane[lane].ready = 1U;",
                                "bank->lane[lane].ready = 1U;\n"
                                "    bank->lane[lane].count = count;"),
         "ready before count"),
    )
    for mutant_source, mutant_header, name in mutants:
        if not source_failures(mutant_source, mutant_header):
            print(f"dual-CPU coherency mutation unexpectedly passed: {name}",
                  file=sys.stderr)
            return 1
    print("dual-CPU coherency mutation gate OK: five invalid handoffs rejected")
    return 0


def check_symbols(arguments: list[str]) -> int:
    if len(arguments) == 1:
        out = Path(arguments[0]).read_text(encoding="utf-8", errors="replace")
    elif len(arguments) == 2:
        out = subprocess.run(arguments, capture_output=True, text=True,
                             check=True).stdout
    else:
        raise ValueError("expected <sym-file> or <nm> <elf>")
    addresses: dict[str, int] = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addresses[parts[2]] = int(parts[0], 16)
    failures = []
    for symbol in REQUIRED_UNCACHED_SYMBOLS:
        if symbol not in addresses:
            failures.append(f"{symbol}: MISSING from image")
        elif addresses[symbol] < UNCACHED_BASE:
            failures.append(f"{symbol}: 0x{addresses[symbol]:08X} is CACHED")
    if failures:
        print("dual-CPU coherency symbol gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("dual-CPU coherency symbol gate OK: "
          f"{len(REQUIRED_UNCACHED_SYMBOLS)} shared symbols uncached")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("symbols", nargs="*")
    args = parser.parse_args()
    if args.source is not None:
        if args.header is None:
            parser.error("--source requires --header")
        status = check_source(args.source, args.header)
        if status == 0 and args.self_test:
            status = self_test(args.source, args.header)
        return status
    try:
        return check_symbols(args.symbols)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"dual-CPU coherency gate error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
