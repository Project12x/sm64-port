#!/usr/bin/env python3
"""Run one executable and require its nonzero exit status.

Kept shell-free so GNU Make's Windows-native recipe path can verify mutation
fixtures without POSIX `if`/`then` syntax.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("--label", required=True)
    args = parser.parse_args()
    completed = subprocess.run([str(args.executable)], check=False)
    if completed.returncode == 0:
        print(f"{args.label} escaped fixture", file=sys.stderr)
        return 1
    print(f"{args.label} caught by fixture")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
