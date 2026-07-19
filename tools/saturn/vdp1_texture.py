#!/usr/bin/env python3
"""Shared offline mapping rules for source textures on Saturn VDP1."""
from __future__ import annotations


def downsample_rgb1555(
    pixels: list[int], width: int, height: int, scale: int
) -> tuple[int, int, list[int]]:
    """Box-filter an RGB1555 image by an integer power-of-two scale.

    Filtering happens in five-bit channel space so the result can be emitted
    directly as a Saturn texture.  Alpha uses majority coverage, which keeps
    cutout edges deterministic without inventing intermediate alpha values.
    """
    if scale not in (1, 2, 4):
        raise ValueError("texture scale must be 1, 2, or 4")
    if len(pixels) != width * height:
        raise ValueError("pixel count does not match texture dimensions")
    if width % scale or height % scale:
        raise ValueError("texture dimensions must be divisible by scale")
    if scale == 1:
        return width, height, list(pixels)

    output: list[int] = []
    sample_count = scale * scale
    for output_y in range(height // scale):
        for output_x in range(width // scale):
            red = green = blue = alpha = 0
            for y in range(output_y * scale, (output_y + 1) * scale):
                for x in range(output_x * scale, (output_x + 1) * scale):
                    value = pixels[y * width + x]
                    red += value & 0x1F
                    green += (value >> 5) & 0x1F
                    blue += (value >> 10) & 0x1F
                    alpha += (value >> 15) & 1
            average = lambda total: (total + sample_count // 2) // sample_count
            output.append(
                (0x8000 if alpha * 2 >= sample_count else 0)
                | (average(blue) << 10)
                | (average(green) << 5)
                | average(red)
            )
    return width // scale, height // scale, output


def repeated_vertex_weights(x: int, y: int, width: int, height: int) -> tuple[float, float, float]:
    """Return Fast3D A/B/C weights for a VDP1 (A,B,C,C) texture texel.

    The BIOS-backed corner probe measured source corners as C/B/A/C.  VDP1
    maps the complete rectangular source image onto the repeated-vertex
    distorted sprite, so the fourth corner contributes C; it is not an
    invalid half-image that should be made transparent.
    """
    a, b, c, d = distorted_sprite_weights(x, y, width, height)
    c += d
    return a, b, c


def distorted_sprite_weights(
    x: int, y: int, width: int, height: int
) -> tuple[float, float, float, float]:
    """Return measured VDP1 A/B/C/D weights for a source-image texel.

    The BIOS-backed valid-quad probe established source-image corner order D/B/A/C
    for the vertex order passed to ``vdp1_cmdt_vtx_set``.  Keeping this rule in
    one host helper lets native quads and repeated-vertex triangle fallbacks use
    the same proven orientation rather than guessing at character flips.
    """
    s = (x + 0.5) / width
    t = (y + 0.5) / height
    return (
        (1.0 - s) * t,
        s * (1.0 - t),
        s * t,
        (1.0 - s) * (1.0 - t),
    )
