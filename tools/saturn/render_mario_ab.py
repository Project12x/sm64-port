#!/usr/bin/env python3
"""Render side-by-side A/B contact sheets of the decimated Mario actor.

Host-side only, dependency-free.  ``.venv-saturn-tools`` has no PIL, numpy or
matplotlib and this task is not allowed to mutate a venv three other agents are
using, so the PNG encoder (zlib + CRC) and the z-buffered triangle rasteriser
are both in this file.  The precedent is
``docs/saturn/evidence/reports/memcamp-decimation-prototype-2026-08-09.md`` §5,
which wrote a dependency-free SVG renderer for the same reason.

Every level is drawn with the *same* camera and the *same* fitted bounds, so
the sheets are directly comparable: a silhouette difference on the page is a
silhouette difference in the mesh, not a framing artefact.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import zlib
from pathlib import Path

CELL_WIDTH = 208
CELL_HEIGHT = 264
MARGIN = 8
LABEL_HEIGHT = 12
BACKGROUND = (24, 26, 32)
PANEL = (14, 15, 19)
INK = (232, 234, 240)
WIRE = (120, 200, 255)

# 5x7 bitmap font, enough for the labels this tool writes.
FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11110", "10001", "10001", "10001", "10001", "10001", "11110"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "F": ["11111", "10000", "10000", "11110", "10000", "10000", "10000"],
    "G": ["01110", "10001", "10000", "10111", "10001", "10001", "01110"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "I": ["01110", "00100", "00100", "00100", "00100", "00100", "01110"],
    "J": ["00111", "00010", "00010", "00010", "00010", "10010", "01100"],
    "K": ["10001", "10010", "10100", "11000", "10100", "10010", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "M": ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "O": ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
    "P": ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
    "Q": ["01110", "10001", "10001", "10001", "10101", "10010", "01101"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "S": ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
    "T": ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "V": ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
    "W": ["10001", "10001", "10001", "10101", "10101", "11011", "10001"],
    "X": ["10001", "10001", "01010", "00100", "01010", "10001", "10001"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "Z": ["11111", "00001", "00010", "00100", "01000", "10000", "11111"],
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00110", "01000", "10000", "11111"],
    "3": ["11111", "00010", "00100", "00010", "00001", "10001", "01110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
    "%": ["11001", "11010", "00010", "00100", "01000", "01011", "10011"],
    "-": ["00000", "00000", "00000", "11111", "00000", "00000", "00000"],
    ".": ["00000", "00000", "00000", "00000", "00000", "01100", "01100"],
    ",": ["00000", "00000", "00000", "00000", "01100", "00100", "01000"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    "(": ["00010", "00100", "01000", "01000", "01000", "00100", "00010"],
    ")": ["01000", "00100", "00010", "00010", "00010", "00100", "01000"],
    ":": ["00000", "01100", "01100", "00000", "01100", "01100", "00000"],
    "=": ["00000", "00000", "11111", "00000", "11111", "00000", "00000"],
    "+": ["00000", "00100", "00100", "11111", "00100", "00100", "00000"],
    " ": ["00000", "00000", "00000", "00000", "00000", "00000", "00000"],
}


class Image:
    def __init__(self, width: int, height: int, colour: tuple[int, int, int]):
        self.width = width
        self.height = height
        self.pixels = bytearray(bytes(colour) * (width * height))

    def set(self, x: int, y: int, colour: tuple[int, int, int]) -> None:
        if 0 <= x < self.width and 0 <= y < self.height:
            offset = (y * self.width + x) * 3
            self.pixels[offset:offset + 3] = bytes(colour)

    def fill_rect(self, x: int, y: int, width: int, height: int,
                  colour: tuple[int, int, int]) -> None:
        row = bytes(colour) * width
        for line in range(y, min(y + height, self.height)):
            if line < 0:
                continue
            offset = (line * self.width + x) * 3
            self.pixels[offset:offset + len(row)] = row

    def blit(self, other: "Image", x: int, y: int) -> None:
        for line in range(other.height):
            source = line * other.width * 3
            offset = ((y + line) * self.width + x) * 3
            self.pixels[offset:offset + other.width * 3] = other.pixels[
                source:source + other.width * 3
            ]

    def text(self, x: int, y: int, message: str, colour: tuple[int, int, int],
             scale: int = 1) -> None:
        cursor = x
        for character in message.upper():
            glyph = FONT.get(character, FONT[" "])
            for row, bits in enumerate(glyph):
                for column, bit in enumerate(bits):
                    if bit == "1":
                        for dy in range(scale):
                            for dx in range(scale):
                                self.set(cursor + column * scale + dx,
                                         y + row * scale + dy, colour)
            cursor += 6 * scale

    def write_png(self, path: Path) -> None:
        raw = bytearray()
        stride = self.width * 3
        for line in range(self.height):
            raw.append(0)
            raw += self.pixels[line * stride:(line + 1) * stride]

        def chunk(tag: bytes, payload: bytes) -> bytes:
            return (
                struct.pack(">I", len(payload))
                + tag
                + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF)
            )

        header = struct.pack(">IIBBBBB", self.width, self.height, 8, 2, 0, 0, 0)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", header)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
            + chunk(b"IEND", b"")
        )


def text_width(message: str, scale: int = 1) -> int:
    return len(message) * 6 * scale


def rotate(point: list[int], yaw: float, pitch: float) -> tuple[float, float, float]:
    x, y, z = float(point[0]), float(point[1]), float(point[2])
    cy, sy = math.cos(yaw), math.sin(yaw)
    x, z = x * cy + z * sy, -x * sy + z * cy
    cp, sp = math.cos(pitch), math.sin(pitch)
    y, z = y * cp - z * sp, y * sp + z * cp
    return x, y, z


def fit(points: list[tuple[float, float, float]]) -> tuple[float, float, float]:
    xs = [point[0] for point in points]
    ys = [point[1] for point in points]
    span = max(max(xs) - min(xs), max(ys) - min(ys), 1.0)
    scale = (min(CELL_WIDTH, CELL_HEIGHT - LABEL_HEIGHT) - 24) / span
    return scale, (min(xs) + max(xs)) * 0.5, (min(ys) + max(ys)) * 0.5


def shade(rgb555: list[int], normal: tuple[float, float, float]) -> tuple[int, int, int]:
    length = math.sqrt(sum(value * value for value in normal)) or 1.0
    light = (-0.35, 0.55, -0.76)
    lambert = abs(sum(a * b for a, b in zip(normal, light)) / length)
    level = 0.32 + 0.68 * lambert
    return tuple(
        min(255, int(round(component * (255.0 / 31.0) * level))) for component in rgb555
    )


def draw_triangle(image: Image, depth: list[float], points, colour) -> None:
    (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = points
    min_x = max(0, int(math.floor(min(x0, x1, x2))))
    max_x = min(image.width - 1, int(math.ceil(max(x0, x1, x2))))
    min_y = max(0, int(math.floor(min(y0, y1, y2))))
    max_y = min(image.height - 1, int(math.ceil(max(y0, y1, y2))))
    area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)
    if abs(area) < 1e-9:
        return
    inverse = 1.0 / area
    for y in range(min_y, max_y + 1):
        py = y + 0.5
        for x in range(min_x, max_x + 1):
            px = x + 0.5
            w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) * inverse
            w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) * inverse
            w2 = 1.0 - w0 - w1
            if w0 < 0.0 or w1 < 0.0 or w2 < 0.0:
                continue
            z = w0 * z0 + w1 * z1 + w2 * z2
            index = y * image.width + x
            if z >= depth[index]:
                continue
            depth[index] = z
            image.set(x, y, colour)


def draw_line(image: Image, a, b, colour) -> None:
    x0, y0 = int(round(a[0])), int(round(a[1]))
    x1, y1 = int(round(b[0])), int(round(b[1]))
    dx, dy = abs(x1 - x0), -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    error = dx + dy
    while True:
        image.set(x0, y0, colour)
        if x0 == x1 and y0 == y1:
            return
        double = 2 * error
        if double >= dy:
            error += dy
            x0 += sx
        if double <= dx:
            error += dx
            y0 += sy


def render_cell(level: dict, positions: list[list[int]], material_rgb: list[list[int]],
                yaw_degrees: float, pitch_degrees: float, transform, wireframe: bool,
                highlight_textured: bool = False) -> Image:
    image = Image(CELL_WIDTH, CELL_HEIGHT - LABEL_HEIGHT, PANEL)
    depth = [float("inf")] * (image.width * image.height)
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    scale, centre_x, centre_y = transform
    origin_x = image.width * 0.5
    origin_y = image.height * 0.5

    view = []
    for point in positions:
        x, y, z = rotate(point, yaw, pitch)
        view.append(
            (
                origin_x + (x - centre_x) * scale,
                origin_y - (y - centre_y) * scale,
                z,
            )
        )

    order = []
    for primitive in level["primitives"]:
        indices = primitive["indices"]
        corners = indices if indices[3] != indices[2] else indices[:3]
        order.append((primitive, corners))

    for primitive, corners in order:
        a, b, c = (view[index] for index in corners[:3])
        normal = (
            (b[1] - a[1]) * (c[2] - a[2]) - (b[2] - a[2]) * (c[1] - a[1]),
            (b[2] - a[2]) * (c[0] - a[0]) - (b[0] - a[0]) * (c[2] - a[2]),
            (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]),
        )
        colour = shade(material_rgb[primitive["material"]], normal)
        if highlight_textured:
            if primitive["textured"]:
                colour = shade([31, 0, 26], normal)
            else:
                grey = sum(colour) // 5
                colour = (grey, grey, grey)
        elif primitive["textured"]:
            colour = tuple(min(255, int(value * 0.72 + 60)) for value in colour)
        fans = [(0, 1, 2)] if len(corners) == 3 else [(0, 1, 2), (0, 2, 3)]
        for fan in fans:
            draw_triangle(image, depth, [view[corners[i]] for i in fan], colour)

    if wireframe:
        for primitive, corners in order:
            for i in range(len(corners)):
                draw_line(image, view[corners[i]], view[corners[(i + 1) % len(corners)]], WIRE)
    return image


def build_sheet(document: dict, pose_positions: list[list[int]], pose_name: str,
                angles: list[float], pitch: float, wireframe: bool,
                stats: dict, output: Path, highlight_textured: bool = False) -> None:
    levels = document["levels"]
    material_rgb = document["material_rgb"]

    reference = []
    for angle in angles:
        reference.append(
            [rotate(point, math.radians(angle), math.radians(pitch)) for point in pose_positions]
        )
    transforms = [fit(points) for points in reference]

    columns = len(angles)
    rows = len(levels)
    header = 34
    width = MARGIN + columns * (CELL_WIDTH + MARGIN)
    height = header + rows * (CELL_HEIGHT + MARGIN) + MARGIN
    sheet = Image(width, height, BACKGROUND)
    title = f"MARIO ACTOR DECIMATION AB - POSE {pose_name}"
    if wireframe:
        title += " - WIREFRAME"
    if highlight_textured:
        title = f"THE 50 TEXTURED PRIMITIVES ARE FIXED COST - POSE {pose_name}"
    sheet.text(MARGIN, 8, title, INK, 2)

    for column, angle in enumerate(angles):
        label = f"YAW {int(angle)}"
        x = MARGIN + column * (CELL_WIDTH + MARGIN)
        sheet.text(x + (CELL_WIDTH - text_width(label)) // 2, 24, label, INK, 1)

    for row, level in enumerate(levels):
        y = header + row * (CELL_HEIGHT + MARGIN)
        for column, angle in enumerate(angles):
            x = MARGIN + column * (CELL_WIDTH + MARGIN)
            cell = render_cell(level, pose_positions, material_rgb, angle, pitch,
                               transforms[column], wireframe, highlight_textured)
            sheet.blit(cell, x, y)
        entry = stats[level["name"]]
        caption = (
            f"{level['name'].replace('_', ' ')}  "
            f"PRIM {entry['primitive_count']}  CMD {entry['vdp1_command_count']}  "
            f"VTX {entry['referenced_vertex_count']}  "
            f"{entry['bytes']['total'] // 1024}KB"
        )
        sheet.text(MARGIN + 2, y + CELL_HEIGHT - LABEL_HEIGHT + 2, caption, INK, 1)

    sheet.write_png(output)
    print(f"wrote {output}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--levels", type=Path, required=True)
    parser.add_argument("--stats", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()

    document = json.loads(args.levels.read_text())
    report = json.loads(args.stats.read_text())
    stats = {entry["name"]: entry for entry in report["levels"]}

    poses = [
        ("NEUTRAL", document["animation"][0]),
        ("WALK F20", document["walking"][20]),
        ("WALK F48", document["walking"][48]),
    ]
    angles = [0.0, 45.0, 90.0, 200.0]
    for pose_name, positions in poses:
        slug = pose_name.lower().replace(" ", "-")
        build_sheet(document, positions, pose_name, angles, 12.0, False, stats,
                    args.output_dir / f"mario-decimation-{slug}-solid.png")
    build_sheet(document, poses[0][1], poses[0][0], angles, 12.0, True, stats,
                args.output_dir / "mario-decimation-neutral-wireframe.png")
    build_sheet(document, poses[1][1], poses[1][0], angles, 12.0, True, stats,
                args.output_dir / "mario-decimation-walk-f20-wireframe.png")

    # The 50 textured primitives, highlighted: they are byte-identical at every
    # level because decimation is forbidden to merge, split or reorder them.
    build_sheet(document, poses[0][1], poses[0][0], angles, 12.0, False, stats,
                args.output_dir / "mario-decimation-textured-fixed-cost.png",
                highlight_textured=True)
    print("done")


if __name__ == "__main__":
    main()
