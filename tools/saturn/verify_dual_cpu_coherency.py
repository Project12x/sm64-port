#!/usr/bin/env python3
"""Build gate: cross-CPU shared state must live in uncached address space.

The SH7604 has no inter-CPU cache coherency. Any datum written by one CPU
and read by the other must be accessed through the uncached partition
(addresses >= 0x20000000), or the reader consumes stale cache lines.
This gate failed on the Pipe 5 image: the transform-phase fence flags and
the terrain result-span headers resolved into cached LWRAM/stack space,
which is the demonstrated root cause of the disappearing Mario/terrain
(see docs/superpowers/plans/2026-07-28-saturn-throughput-recovery-sprint.md,
Pipe 5 root cause). SlaveDriver's own discipline is a full cache flush per
slave pass (WALLS.C:1806-1810) plus cache-through result writes
(WALLS.C:1272); this gate pins our equivalent structurally.

Usage: verify_dual_cpu_coherency.py <sh-elf-nm> <elf>
"""
import subprocess
import sys

REQUIRED_UNCACHED_SYMBOLS = (
    "_s_transform_phase_ready",
    "_s_transform_phase_failed",
    "_s_terrain_spans_shared",
)

UNCACHED_BASE = 0x20000000


def main() -> int:
    if len(sys.argv) == 2:
        with open(sys.argv[1], encoding="utf-8", errors="replace") as handle:
            out = handle.read()
    elif len(sys.argv) == 3:
        out = subprocess.run([sys.argv[1], sys.argv[2]], capture_output=True,
                             text=True, check=True).stdout
    else:
        print("usage: verify_dual_cpu_coherency.py <sym-file> | <nm> <elf>",
              file=sys.stderr)
        return 2
    addresses = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            addresses[parts[2]] = int(parts[0], 16)
    failures = []
    for symbol in REQUIRED_UNCACHED_SYMBOLS:
        if symbol not in addresses:
            failures.append(f"{symbol}: MISSING from image "
                            f"(fence/spans must be link-visible statics)")
        elif addresses[symbol] < UNCACHED_BASE:
            failures.append(
                f"{symbol}: 0x{addresses[symbol]:08X} is in CACHED space; "
                f"cross-CPU reads will consume stale lines")
    if failures:
        print("dual-CPU coherency gate FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("dual-CPU coherency gate OK: "
          f"{len(REQUIRED_UNCACHED_SYMBOLS)} shared symbols uncached")
    return 0


if __name__ == "__main__":
    sys.exit(main())
