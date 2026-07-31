#!/usr/bin/env python3
"""Validation helpers for the isolated default-camera acceptance route."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Mapping


R_TRIG = 0x0010


def load_route_manifest(path: Path) -> dict[str, Any]:
    """Load a route manifest and validate its first Mario dispatch tick."""
    manifest = json.loads(path.read_text(encoding="utf-8"))
    first_mario_dispatch_tick(manifest)
    return manifest


def first_mario_dispatch_tick(route_manifest: Mapping[str, Any]) -> int:
    """Derive the one-based tick of the sole neutral R-trigger sample."""
    samples = route_manifest.get("samples")
    if not isinstance(samples, list):
        raise ValueError("route manifest samples must be a list")

    dispatch_tick: int | None = None
    ticks_before = 0
    for sample in samples:
        if not isinstance(sample, Mapping):
            raise ValueError("route sample must be an object")
        try:
            ticks = sample["ticks"]
            stick_x = sample["stick_x"]
            stick_y = sample["stick_y"]
            buttons = sample["buttons"]
        except KeyError as error:
            raise ValueError("route sample is incomplete") from error
        if not all(isinstance(value, int) for value in
                   (ticks, stick_x, stick_y, buttons)) or ticks <= 0:
            raise ValueError("route sample fields must be positive/integer")
        if stick_x != 0 or stick_y != 0:
            raise ValueError("camera route must keep both sticks neutral")
        if buttons == R_TRIG:
            if ticks != 1 or dispatch_tick is not None:
                raise ValueError("camera route requires one one-tick R_TRIG sample")
            dispatch_tick = ticks_before + 1
        elif buttons != 0:
            raise ValueError("camera route only permits the R_TRIG button")
        ticks_before += ticks

    if dispatch_tick is None:
        raise ValueError("camera route requires one R_TRIG sample")
    return dispatch_tick
