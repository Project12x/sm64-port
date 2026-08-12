#!/usr/bin/env python3
"""Shared offline mapping rules for source textures on Saturn VDP1."""
from __future__ import annotations

import binascii
import struct
import zlib
from pathlib import Path


def read_png_rgb1555(path: Path) -> tuple[int, int, list[int]]:
    """Read and decode one checked-in RGBA/IA exporter PNG."""
    source = Path(path)
    return decode_png_rgb1555(source.read_bytes(), str(source))


def decode_png_rgb1555(data: bytes, label: str) -> tuple[int, int, list[int]]:
    """Decode attested RGBA/IA PNG bytes to canonical RGB1555.

    This is a close port of ``bake_bob_tiles._png_pixels``.  The shared form
    additionally owns chunk CRCs, terminal structure, scanline length, and
    non-interlaced 8-bit constraints so a malformed source image cannot enter
    a bank through permissive host image-library behavior.
    """
    path = label
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: texture source is not a PNG")
    offset = 8
    idat = bytearray()
    width = height = bit_depth = color_type = -1
    seen_ihdr = seen_iend = False
    while offset < len(data):
        if len(data) - offset < 12:
            raise ValueError(f"{path}: truncated texture PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        end = offset + 12 + size
        if end > len(data):
            raise ValueError(f"{path}: truncated texture PNG payload")
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        expected_crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if binascii.crc32(kind + payload) & 0xFFFFFFFF != expected_crc:
            raise ValueError(f"{path}: texture PNG CRC mismatch")
        offset = end
        if kind == b"IHDR":
            if seen_ihdr or size != 13:
                raise ValueError(f"{path}: invalid texture PNG IHDR")
            (width, height, bit_depth, color_type, compression, filtering,
             interlace) = struct.unpack(">IIBBBBB", payload)
            if (width <= 0 or height <= 0 or bit_depth != 8 or
                    color_type not in (4, 6) or compression or filtering or
                    interlace):
                raise ValueError(f"{path}: unsupported texture PNG format")
            seen_ihdr = True
        elif kind == b"IDAT":
            if not seen_ihdr or seen_iend:
                raise ValueError(f"{path}: invalid texture PNG IDAT order")
            idat.extend(payload)
        elif kind == b"IEND":
            if size or not seen_ihdr or seen_iend or offset != len(data):
                raise ValueError(f"{path}: invalid texture PNG IEND")
            seen_iend = True
        elif kind[0] & 0x20 == 0:
            raise ValueError(f"{path}: unsupported critical texture PNG chunk")
    if not seen_iend or not idat:
        raise ValueError(f"{path}: incomplete texture PNG")
    channels = 2 if color_type == 4 else 4
    row_bytes = width * channels
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as error:
        raise ValueError(f"{path}: invalid texture PNG deflate stream") from error
    expected_size = height * (row_bytes + 1)
    if len(raw) != expected_size:
        raise ValueError(f"{path}: texture PNG scanline size mismatch")
    pixels: list[int] = []
    cursor = 0
    previous = bytearray(row_bytes)
    for _ in range(height):
        filter_kind = raw[cursor]
        cursor += 1
        row = bytearray(raw[cursor:cursor + row_bytes])
        cursor += row_bytes
        for index in range(row_bytes):
            left = row[index - channels] if index >= channels else 0
            up = previous[index]
            up_left = previous[index - channels] if index >= channels else 0
            if filter_kind == 1:
                row[index] = (row[index] + left) & 0xFF
            elif filter_kind == 2:
                row[index] = (row[index] + up) & 0xFF
            elif filter_kind == 3:
                row[index] = (row[index] + ((left + up) // 2)) & 0xFF
            elif filter_kind == 4:
                prediction = left + up - up_left
                distances = (abs(prediction - left), abs(prediction - up),
                             abs(prediction - up_left))
                predictor = (left if distances[0] <= distances[1] and
                              distances[0] <= distances[2]
                              else up if distances[1] <= distances[2]
                              else up_left)
                row[index] = (row[index] + predictor) & 0xFF
            elif filter_kind != 0:
                raise ValueError(f"{path}: unsupported texture PNG filter {filter_kind}")
        for x in range(width):
            if channels == 2:
                red = green = blue = row[x * 2]
                alpha = row[x * 2 + 1]
            else:
                red, green, blue, alpha = row[x * 4:x * 4 + 4]
            value = ((red * 31 + 127) // 255)
            value |= ((green * 31 + 127) // 255) << 5
            value |= ((blue * 31 + 127) // 255) << 10
            # VDP1 CLUT index zero is the canonical transparent texel.  RGB
            # under a source alpha-zero pixel is unobservable and must not
            # create an unmapped pseudo-color in deterministic quantization.
            pixels.append((value | 0x8000) if alpha >= 128 else 0)
        previous = row
    return width, height, pixels


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
