#!/usr/bin/env python3
"""One checked-in answer to "which ymir-headless should this capture use?".

Every capture tool used to take --ymir as a required, hand-passed path, and
two ymir-headless builds exist in the tree. The older `build-agent` one
predates DRAM-cart support, and Ymir's argument parser ignores unknown flags
SILENTLY: it accepts --dram-cart, boots the game, and reaches the same
boot-trace stage as a good binary while never mapping the cart. Because the
sourceboot identity probe binds to `.cart_rodata` at 0x22400000, that window
then reads 0xFF open bus forever and identity can never match at any VBlank
count -- a failure that presents as "the harness is broken" rather than
"wrong binary". Resolving the path here means a stale command line copied
from an old report cannot re-arm that trap.

The runtime check in capture_sourceboot_throughput.assert_dram_cart_mapped
remains the authority on whether a cart is actually mapped. This module only
decides which binary to reach for by default; it deliberately does NOT reject
a binary by path, because a rebuilt `build-agent` with cart support would be
perfectly valid and only the runtime probe can tell.

Note this is the HEADLESS capture binary. The visible GUI launcher
(launch_ymir_desktop.py) drives ymir-sdl3.exe with its own profile and is a
separate concern.
"""

from __future__ import annotations

import os
from pathlib import Path

YMIR_ENV_VAR = "SM64_SATURN_YMIR"
YMIR_BUILD_DIR = "build-agent2"
SUPERSEDED_BUILD_DIRS = ("build-agent",)
YMIR_SUFFIX = ("apps", "ymir-headless", "Release", "ymir-headless.exe")
YMIR_RELATIVE_PATH = Path("ymir-agent", YMIR_BUILD_DIR, *YMIR_SUFFIX)


def _search_roots(start: Path | None = None) -> list[Path]:
    """Walk upward so this resolves from a worktree or the main checkout."""
    origin = (start or Path(__file__)).resolve()
    if origin.is_file():
        origin = origin.parent
    return [origin, *origin.parents]


def find_ymir_headless(start: Path | None = None) -> Path | None:
    """Return the supported headless binary, or None if it is not on disk."""
    override = os.environ.get(YMIR_ENV_VAR)
    if override:
        candidate = Path(override).expanduser()
        return candidate if candidate.is_file() else None
    for root in _search_roots(start):
        candidate = root / YMIR_RELATIVE_PATH
        if candidate.is_file():
            return candidate
    return None


def default_ymir_headless(start: Path | None = None) -> Path | None:
    """Argparse-safe default: never raises, so --help always works."""
    return find_ymir_headless(start)


def _superseded_binaries(start: Path | None = None) -> list[Path]:
    return [
        root / "ymir-agent" / name / Path(*YMIR_SUFFIX)
        for root in _search_roots(start)
        for name in SUPERSEDED_BUILD_DIRS
        if (root / "ymir-agent" / name / Path(*YMIR_SUFFIX)).is_file()
    ]


def describe_missing_ymir(start: Path | None = None) -> str:
    """Explain what to do, and name a superseded build if that is what is here."""
    override = os.environ.get(YMIR_ENV_VAR)
    if override:
        return (
            f"{YMIR_ENV_VAR} is set to {override!r} but no file is there. "
            f"Unset it to fall back to the in-tree {YMIR_RELATIVE_PATH}, or "
            "point it at a ymir-headless built with DRAM-cart support."
        )
    message = (
        f"no supported ymir-headless found: expected {YMIR_RELATIVE_PATH} under "
        f"a parent of {Path(__file__).resolve().parent}. Pass --ymir explicitly "
        f"or set {YMIR_ENV_VAR}."
    )
    superseded = _superseded_binaries(start)
    if superseded:
        message += (
            f" NOTE: {superseded[0]} exists but is a SUPERSEDED build that "
            "predates DRAM-cart support. It accepts --dram-cart and boots "
            "normally without ever mapping the cart, so captures fail later at "
            f"target-identity instead of here. Use {YMIR_BUILD_DIR} instead."
        )
    return message


def resolve_ymir_headless(
    explicit: Path | None = None, *, start: Path | None = None
) -> Path:
    """Honour an explicit path, else the default; raise with the fix if neither."""
    if explicit is not None:
        return explicit
    found = find_ymir_headless(start)
    if found is None:
        raise ValueError(describe_missing_ymir(start))
    return found


def add_ymir_argument(parser, help_text: str = "ymir-headless executable") -> None:
    """Declare --ymir with the checked-in default, staying required if absent.

    Keeping required=True when nothing is found preserves the previous
    behaviour exactly on a machine where the binary is not in the expected
    place, rather than failing later with a confusing None.
    """
    default = default_ymir_headless()
    parser.add_argument(
        "--ymir",
        type=Path,
        default=default,
        required=default is None,
        help=f"{help_text} (default: {default or 'none found; --ymir required'})",
    )
