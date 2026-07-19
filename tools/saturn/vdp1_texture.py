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
    VDP1's transparent direct-color code is exactly 0x0000; an RGB1555 word
    with bit 15 clear but nonzero RGB channels is still visible character
    data.  Canonicalize every transparent result to zero, including scale 1.
    """
    if scale not in (1, 2, 4):
        raise ValueError("texture scale must be 1, 2, or 4")
    if len(pixels) != width * height:
        raise ValueError("pixel count does not match texture dimensions")
    if width % scale or height % scale:
        raise ValueError("texture dimensions must be divisible by scale")
    if scale == 1:
        return width, height, [value if value & 0x8000 else 0 for value in pixels]

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
            if alpha * 2 < sample_count:
                output.append(0)
            else:
                output.append(
                    0x8000
                    | (average(blue) << 10)
                    | (average(green) << 5)
                    | average(red)
                )
    return width // scale, height // scale, output


def repeated_vertex_weights(x: int, y: int, width: int, height: int) -> tuple[float, float, float]:
    """Return Fast3D A/B/C weights for a VDP1 (A,B,C,C) texture texel.

    The BIOS-backed corner probe, decoded with Saturn's actual RGB1555 lane
    order, maps source corners to A/B/C/D.  Repeating destination D at C
    collapses the lower-left character corner onto C, so its weight contributes
    to C without introducing a crossed or masked half-image.
    """
    a, b, c, d = distorted_sprite_weights(x, y, width, height)
    c += d
    return a, b, c


def distorted_sprite_weights(
    x: int, y: int, width: int, height: int
) -> tuple[float, float, float, float]:
    """Return measured VDP1 A/B/C/D weights for a source-image texel.

    The BIOS-backed valid-quad probe establishes ordinary source-image corner
    order A/B/C/D for the vertex order passed to ``vdp1_cmdt_vtx_set``.  The
    earlier D/B/A/C interpretation had labelled raw 0xFC00 as red and 0x801F as
    blue; on Saturn those values are blue and red respectively.  Keeping this
    rule in one host helper prevents that channel-label error from becoming a
    geometric texture fold.
    """
    s = (x + 0.5) / width
    t = (y + 0.5) / height
    return (
        (1.0 - s) * (1.0 - t),
        s * (1.0 - t),
        s * t,
        (1.0 - s) * t,
    )
