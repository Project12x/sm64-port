#!/usr/bin/env python3
"""Require the captured-route atan2 mutation test to fail."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--python", required=True)
    parser.add_argument("--test", type=Path, required=True)
    parser.add_argument(
        "--deliberately-escape",
        action="store_true",
        help="test-only path: omit the mutation and require this verifier to fail",
    )
    args = parser.parse_args(argv)

    environment = os.environ.copy()
    environment["SM64_SATURN_EXPECT_ATAN2_Q16_MUTATION"] = "1"
    if not args.deliberately_escape:
        environment["SM64_SATURN_TEST_MUTATE_ATAN2_Q16"] = "1"
    else:
        environment.pop("SM64_SATURN_TEST_MUTATE_ATAN2_Q16", None)
    completed = subprocess.run(
        [args.python, str(args.test)],
        env=environment,
        check=False,
    )
    if completed.returncode != 0:
        print(
            "engine atan2 Q16 mutation escaped or mutation verification failed",
            file=sys.stderr,
        )
        return 1
    print("engine atan2 Q16 mutation caught by the captured-route differential")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
