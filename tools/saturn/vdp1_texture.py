#!/usr/bin/env python3
"""Shared offline mapping rules for source textures on Saturn VDP1."""
from __future__ import annotations


def repeated_vertex_weights(x: int, y: int, width: int, height: int) -> tuple[float, float, float]:
    """Return Fast3D A/B/C weights for a VDP1 (A,B,C,C) texture texel.

    The BIOS-backed corner probe measured source corners as C/B/A/C.  VDP1
    maps the complete rectangular source image onto the repeated-vertex
    distorted sprite, so the fourth corner contributes C; it is not an
    invalid half-image that should be made transparent.
    """
    s = (x + 0.5) / width
    t = (y + 0.5) / height
    a = (1.0 - s) * t
    b = s * (1.0 - t)
    c = (1.0 - s) * (1.0 - t) + s * t
    return a, b, c
