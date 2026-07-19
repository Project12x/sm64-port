#!/usr/bin/env python3
"""Deterministic offline BSP construction for VDP1 static-world painters.

The implementation is intentionally host-side and source-format agnostic.  It
keeps exact rational positions and arbitrary per-vertex attributes while it
splits convex polygons, so a caller can interpolate Fast3D UVs without a
camera-specific mesh rewrite.  Runtime code only needs the resulting planes,
nodes, and convex fragments.

Reference audit (2026-07-18): Sonic Z-Treme documents an effective offline BSP
compiler, but does not publish it; JoEngine and Yaul expose depth sorting only.
This implementation is therefore original rather than copied or closely
ported from those projects.
"""
from __future__ import annotations

from dataclasses import dataclass
from fractions import Fraction
from functools import reduce
import hashlib
import json
from math import gcd
from typing import Iterable, Sequence


Scalar = Fraction


def _fraction(value: int | Fraction) -> Fraction:
    return value if isinstance(value, Fraction) else Fraction(value)


@dataclass(frozen=True)
class Vertex:
    position: tuple[Scalar, Scalar, Scalar]
    attributes: tuple[Scalar, ...] = ()

    @classmethod
    def make(cls, position: Sequence[int | Fraction],
             attributes: Sequence[int | Fraction] = ()) -> "Vertex":
        return cls(tuple(_fraction(value) for value in position),
                   tuple(_fraction(value) for value in attributes))

    def between(self, other: "Vertex", amount: Fraction) -> "Vertex":
        if len(self.attributes) != len(other.attributes):
            raise ValueError("BSP edge attributes do not have equal arity")
        inverse = 1 - amount
        return Vertex(
            tuple(inverse * left + amount * right
                  for left, right in zip(self.position, other.position)),
            tuple(inverse * left + amount * right
                  for left, right in zip(self.attributes, other.attributes)),
        )


@dataclass(frozen=True)
class Polygon:
    vertices: tuple[Vertex, ...]
    source: int
    root: int = 0
    layer: int = 0
    texture: int = -1

    def __post_init__(self) -> None:
        if len(self.vertices) < 3:
            raise ValueError("a BSP polygon needs at least three vertices")


Plane = tuple[int, int, int, int]


def _lcm(a: int, b: int) -> int:
    return abs(a * b) // gcd(a, b) if a and b else 0


def _canonical_plane(values: Sequence[Fraction]) -> Plane:
    denominator = reduce(_lcm, (value.denominator for value in values), 1)
    integers = [value.numerator * (denominator // value.denominator)
                for value in values]
    divisor = reduce(gcd, (abs(value) for value in integers if value), 0) or 1
    integers = [value // divisor for value in integers]
    for value in integers:
        if value:
            if value < 0:
                integers = [-item for item in integers]
            break
    return tuple(integers)  # type: ignore[return-value]


def plane_for_polygon(polygon: Polygon) -> Plane:
    """Return a reduced, canonical exact plane for a non-degenerate polygon."""
    origin = polygon.vertices[0].position
    for middle_index in range(1, len(polygon.vertices) - 1):
        middle = polygon.vertices[middle_index].position
        for final_index in range(middle_index + 1, len(polygon.vertices)):
            final = polygon.vertices[final_index].position
            a = tuple(middle[axis] - origin[axis] for axis in range(3))
            b = tuple(final[axis] - origin[axis] for axis in range(3))
            normal = (
                a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0],
            )
            if any(normal):
                distance = -sum(normal[axis] * origin[axis]
                                for axis in range(3))
                return _canonical_plane((*normal, distance))
    raise ValueError(f"source polygon {polygon.source} is degenerate")


def plane_distance(plane: Plane, vertex: Vertex | Sequence[int | Fraction]) -> Fraction:
    position = vertex.position if isinstance(vertex, Vertex) else vertex
    return sum(Fraction(plane[axis]) * position[axis] for axis in range(3)) + plane[3]


def classify_polygon(polygon: Polygon, plane: Plane) -> str:
    signs = {(distance > 0) - (distance < 0)
             for distance in (plane_distance(plane, vertex)
                              for vertex in polygon.vertices)}
    signs.discard(0)
    if not signs:
        return "coplanar"
    if signs == {1}:
        return "front"
    if signs == {-1}:
        return "back"
    return "spanning"


def _deduplicate(vertices: list[Vertex]) -> tuple[Vertex, ...]:
    output: list[Vertex] = []
    for vertex in vertices:
        if not output or vertex != output[-1]:
            output.append(vertex)
    if len(output) > 1 and output[0] == output[-1]:
        output.pop()
    return tuple(output)


def split_polygon(polygon: Polygon, plane: Plane) -> tuple[Polygon, Polygon]:
    """Split a convex polygon, interpolating every vertex attribute exactly."""
    if classify_polygon(polygon, plane) != "spanning":
        raise ValueError("split_polygon requires a spanning polygon")
    front: list[Vertex] = []
    back: list[Vertex] = []
    vertices = polygon.vertices
    for index, current in enumerate(vertices):
        following = vertices[(index + 1) % len(vertices)]
        current_distance = plane_distance(plane, current)
        following_distance = plane_distance(plane, following)
        if current_distance >= 0:
            front.append(current)
        if current_distance <= 0:
            back.append(current)
        if ((current_distance > 0 and following_distance < 0) or
                (current_distance < 0 and following_distance > 0)):
            amount = current_distance / (current_distance - following_distance)
            intersection = current.between(following, amount)
            front.append(intersection)
            back.append(intersection)
    front_vertices, back_vertices = _deduplicate(front), _deduplicate(back)
    metadata = dict(source=polygon.source, root=polygon.root,
                    layer=polygon.layer, texture=polygon.texture)
    return Polygon(front_vertices, **metadata), Polygon(back_vertices, **metadata)


@dataclass
class Node:
    plane: Plane
    coplanar: list[Polygon]
    front: "Node | None" = None
    back: "Node | None" = None


@dataclass(frozen=True)
class BuildStats:
    input_polygons: int
    output_polygons: int
    split_events: int
    node_count: int
    max_depth: int
    max_vertices: int
    digest: str


def _candidate_planes(polygons: Sequence[Polygon], limit: int) -> list[Plane]:
    planes = sorted({plane_for_polygon(polygon) for polygon in polygons})
    if len(planes) <= limit:
        return planes
    # Even sampling keeps the choice deterministic while bounding O(n^2)
    # scoring on large source levels.
    return [planes[index * (len(planes) - 1) // (limit - 1)]
            for index in range(limit)]


def _choose_plane(polygons: Sequence[Polygon], candidate_limit: int,
                  split_weight: int) -> Plane:
    best: tuple[tuple[int, int, int, Plane], Plane] | None = None
    for plane in _candidate_planes(polygons, candidate_limit):
        counts = {name: 0 for name in ("front", "back", "coplanar", "spanning")}
        for polygon in polygons:
            counts[classify_polygon(polygon, plane)] += 1
        key = (counts["spanning"] * split_weight +
               abs(counts["front"] - counts["back"]),
               counts["spanning"], -counts["coplanar"], plane)
        if best is None or key < best[0]:
            best = key, plane
    assert best is not None
    return best[1]


def build(polygons: Iterable[Polygon], *, candidate_limit: int = 32,
          split_weight: int = 8) -> tuple[Node, BuildStats]:
    """Build a deterministic exact BSP from convex source polygons."""
    source = list(polygons)
    if not source:
        raise ValueError("cannot build an empty BSP")
    if candidate_limit < 2:
        raise ValueError("candidate_limit must be at least two")
    split_events = 0
    node_count = 0
    max_depth = 0

    def recurse(items: list[Polygon], depth: int) -> Node:
        nonlocal split_events, node_count, max_depth
        node_count += 1
        max_depth = max(max_depth, depth)
        plane = _choose_plane(items, candidate_limit, split_weight)
        groups: dict[str, list[Polygon]] = {
            "front": [], "back": [], "coplanar": [],
        }
        for polygon in items:
            classification = classify_polygon(polygon, plane)
            if classification == "spanning":
                front, back = split_polygon(polygon, plane)
                groups["front"].append(front)
                groups["back"].append(back)
                split_events += 1
            else:
                groups[classification].append(polygon)
        node = Node(plane=plane, coplanar=groups["coplanar"])
        if groups["front"]:
            node.front = recurse(groups["front"], depth + 1)
        if groups["back"]:
            node.back = recurse(groups["back"], depth + 1)
        return node

    root = recurse(source, 1)
    output = list(iter_polygons(root))
    digest = hashlib.sha256(json.dumps(serialize(root), sort_keys=True,
                                       separators=(",", ":")).encode()).hexdigest()
    return root, BuildStats(
        input_polygons=len(source), output_polygons=len(output),
        split_events=split_events, node_count=node_count, max_depth=max_depth,
        max_vertices=max(len(polygon.vertices) for polygon in output),
        digest=digest,
    )


def iter_polygons(node: Node | None) -> Iterable[Polygon]:
    if node is None:
        return
    yield from node.coplanar
    yield from iter_polygons(node.front)
    yield from iter_polygons(node.back)


def painter_order(node: Node | None, camera: Sequence[int | Fraction]) -> list[Polygon]:
    """Return far-to-near static geometry for a camera position."""
    if node is None:
        return []
    camera_front = plane_distance(node.plane, camera) >= 0
    far = node.back if camera_front else node.front
    near = node.front if camera_front else node.back
    return painter_order(far, camera) + node.coplanar + painter_order(near, camera)


def serialize(node: Node | None) -> object:
    if node is None:
        return None

    def scalar(value: Fraction) -> list[int]:
        return [value.numerator, value.denominator]

    return {
        "plane": list(node.plane),
        "polygons": [{
            "source": polygon.source,
            "root": polygon.root,
            "layer": polygon.layer,
            "texture": polygon.texture,
            "vertices": [{
                "position": [scalar(value) for value in vertex.position],
                "attributes": [scalar(value) for value in vertex.attributes],
            } for vertex in polygon.vertices],
        } for polygon in node.coplanar],
        "front": serialize(node.front),
        "back": serialize(node.back),
    }

