#!/usr/bin/env python3
"""Emit the historical Mario animation object with shared strict parsing."""

from __future__ import annotations

import sys
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent / "saturn"))

from actor_source import render_legacy_mario_anims  # noqa: E402


try:
    sys.stdout.buffer.write(render_legacy_mario_anims(Path("assets/anims")))
except Exception:
    print(
        "NOTE! The mario animation C files are not processed by a normal C compiler, "
        "but by the script in tools/mario_anims_converter.py. The format is much more "
        "strict than normal C, so please follow the syntax of existing files.\n",
        file=sys.stderr,
    )
    traceback.print_exc()
    sys.exit(1)
