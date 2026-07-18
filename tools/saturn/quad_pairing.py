"""Deterministic triangle pairing for Saturn VDP1 render primitives.

The source mesh remains triangulated.  This module only builds a render-time
index table that replaces compatible pairs with true four-corner primitives.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Iterable

try:
    import networkx as nx
except ModuleNotFoundError as error:  # pragma: no cover - exercised by bootstrap failure
    raise ModuleNotFoundError(
        "Saturn mesh tools require NetworkX; run tools/saturn/bootstrap-host-tools.ps1 "
        "or tools/saturn/bootstrap-host-tools.sh"
    ) from error


Vertex = tuple[int, int, int]
Face = tuple[int, int, int, int]


@dataclass(frozen=True)
class QuadCandidate:
    first: int
    second: int
    vertices: tuple[int, int, int, int]
    alignment: float
    shared_length_squared: int


@dataclass(frozen=True)
class RenderPrimitive:
    material: int
    vertices: tuple[int, int, int, int]
    first_triangle: int
    second_triangle: int | None


def _sub(left: Vertex, right: Vertex) -> Vertex:
    return tuple(a - b for a, b in zip(left, right))  # type: ignore[return-value]


def _cross(left: Vertex, right: Vertex) -> Vertex:
    return (
        left[1] * right[2] - left[2] * right[1],
        left[2] * right[0] - left[0] * right[2],
        left[0] * right[1] - left[1] * right[0],
    )


def _dot(left: Vertex, right: Vertex) -> int:
    return sum(a * b for a, b in zip(left, right))


def _normal(vertices: list[Vertex], triangle: tuple[int, int, int]) -> Vertex:
    a, b, c = (vertices[index] for index in triangle)
    return _cross(_sub(b, a), _sub(c, a))


def _directed_edges(triangle: tuple[int, int, int]) -> list[tuple[int, int]]:
    a, b, c = triangle
    return [(a, b), (b, c), (c, a)]


def _boundary_cycle(
    first: tuple[int, int, int], second: tuple[int, int, int]
) -> tuple[tuple[int, int, int, int], tuple[int, int]] | None:
    shared = set(first) & set(second)
    if len(shared) != 2 or len(set(first + second)) != 4:
        return None
    shared_edge = tuple(sorted(shared))
    first_edges = _directed_edges(first)
    second_edges = _directed_edges(second)
    first_shared = next(edge for edge in first_edges if tuple(sorted(edge)) == shared_edge)
    second_shared = next(edge for edge in second_edges if tuple(sorted(edge)) == shared_edge)
    if first_shared != tuple(reversed(second_shared)):
        return None

    boundary = [
        edge
        for edge in first_edges + second_edges
        if tuple(sorted(edge)) != shared_edge
    ]
    outgoing = {start: end for start, end in boundary}
    incoming = {end: start for start, end in boundary}
    if len(outgoing) != 4 or len(incoming) != 4:
        return None
    start = min(outgoing)
    cycle = [start]
    for _ in range(3):
        cycle.append(outgoing[cycle[-1]])
    if outgoing[cycle[-1]] != start or len(set(cycle)) != 4:
        return None
    return tuple(cycle), shared_edge  # type: ignore[return-value]


def _project(vertex: Vertex, yaw_degrees: int, pitch_degrees: int) -> tuple[float, float]:
    yaw = math.radians(yaw_degrees)
    pitch = math.radians(pitch_degrees)
    sin_yaw, cos_yaw = math.sin(yaw), math.cos(yaw)
    sin_pitch, cos_pitch = math.sin(pitch), math.cos(pitch)
    x = vertex[0] * cos_yaw + vertex[2] * sin_yaw
    z = -vertex[0] * sin_yaw + vertex[2] * cos_yaw
    y = vertex[1] * cos_pitch - z * sin_pitch
    return x, y


def _is_strictly_convex(points: list[tuple[float, float]]) -> bool:
    signs = []
    for index in range(4):
        a, b, c = points[index], points[(index + 1) % 4], points[(index + 2) % 4]
        cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if abs(cross) < 1.0e-6:
            return False
        signs.append(cross > 0)
    return all(signs) or not any(signs)


def _projection_is_stable(vertices: list[Vertex], cycle: tuple[int, int, int, int]) -> bool:
    for yaw in (-45, -22, 0, 22, 45):
        for pitch in (-30, 0, 30):
            if not _is_strictly_convex([_project(vertices[index], yaw, pitch) for index in cycle]):
                return False
    return True


def candidates(
    vertices: list[Vertex], faces: list[Face], minimum_normal_alignment: float = 0.80
) -> tuple[list[QuadCandidate], dict[str, int]]:
    """Return Saturn-safe shared-edge candidates and rejection counts."""
    edge_faces: dict[tuple[int, int], list[int]] = {}
    for face_index, face in enumerate(faces):
        triangle = face[1:]
        for edge in _directed_edges(triangle):
            edge_faces.setdefault(tuple(sorted(edge)), []).append(face_index)

    accepted: list[QuadCandidate] = []
    rejected: dict[str, int] = {}

    def reject(reason: str) -> None:
        rejected[reason] = rejected.get(reason, 0) + 1

    for shared_edge, linked in sorted(edge_faces.items()):
        if len(linked) != 2:
            continue
        first_index, second_index = sorted(linked)
        first, second = faces[first_index], faces[second_index]
        if first[0] != second[0]:
            reject("material_mismatch")
            continue
        boundary = _boundary_cycle(first[1:], second[1:])
        if boundary is None:
            reject("winding_or_topology")
            continue
        cycle, _ = boundary
        first_normal = _normal(vertices, first[1:])
        second_normal = _normal(vertices, second[1:])
        lengths = math.sqrt(_dot(first_normal, first_normal) * _dot(second_normal, second_normal))
        if lengths == 0:
            reject("degenerate_triangle")
            continue
        alignment = _dot(first_normal, second_normal) / lengths
        if alignment < minimum_normal_alignment:
            reject("normal_divergence")
            continue
        if not _projection_is_stable(vertices, cycle):
            reject("projection_not_convex")
            continue
        edge_vector = _sub(vertices[shared_edge[0]], vertices[shared_edge[1]])
        accepted.append(
            QuadCandidate(
                first_index,
                second_index,
                cycle,
                alignment,
                _dot(edge_vector, edge_vector),
            )
        )
    accepted.sort(
        key=lambda item: (
            -round(item.alignment, 9),
            -item.shared_length_squared,
            item.first,
            item.second,
        )
    )
    return accepted, dict(sorted(rejected.items()))


def maximum_weight_matching(options: list[QuadCandidate]) -> dict[int, QuadCandidate]:
    """Return an exact maximum-cardinality, maximum-quality matching.

    NetworkX's blossom implementation uses exact arithmetic for these integer
    weights. Cardinality is the primary objective; normal alignment, shared
    edge length, and stable source order break ties in that order.
    """
    graph = nx.Graph()
    maximum_length = max((option.shared_length_squared for option in options), default=0)
    maximum_pairs = max(1, len({index for option in options for index in (option.first, option.second)}) // 2)
    length_scale = (maximum_length * maximum_pairs) + 1
    tie_scale = (len(options) * len(options)) + 1
    source_order = {
        option: rank
        for rank, option in enumerate(sorted(options, key=lambda item: (item.first, item.second)))
    }
    for option in options:
        alignment_units = round(option.alignment * 1_000_000)
        quality = (alignment_units * length_scale) + option.shared_length_squared
        stable_tie = len(options) - source_order[option]
        graph.add_edge(
            option.first,
            option.second,
            weight=(quality * tie_scale) + stable_tie,
            candidate=option,
        )

    selected = nx.max_weight_matching(graph, maxcardinality=True, weight="weight")
    matched: dict[int, QuadCandidate] = {}
    for first, second in sorted(tuple(sorted(edge)) for edge in selected):
        option = graph.edges[first, second]["candidate"]
        matched[first] = option
        matched[second] = option
    return matched


def pair_triangles(
    vertices: Iterable[Vertex], faces: Iterable[Face]
) -> tuple[list[RenderPrimitive], dict[str, object]]:
    vertex_list, face_list = list(vertices), list(faces)
    options, rejected = candidates(vertex_list, face_list)
    matched = maximum_weight_matching(options)
    primitives: list[RenderPrimitive] = []
    emitted: set[int] = set()
    for index, face in enumerate(face_list):
        if index in emitted:
            continue
        option = matched.get(index)
        if option is None:
            primitives.append(RenderPrimitive(face[0], (face[1], face[2], face[3], face[3]), index, None))
            emitted.add(index)
            continue
        primitives.append(
            RenderPrimitive(face[0], option.vertices, option.first, option.second)
        )
        emitted.update((option.first, option.second))
    quad_count = sum(primitive.second_triangle is not None for primitive in primitives)
    return primitives, {
        "matcher": "networkx.max_weight_matching",
        "matcher_version": nx.__version__,
        "matching_policy": "maximum cardinality, then maximum integer quality",
        "source_triangle_count": len(face_list),
        "candidate_count": len(options),
        "quad_count": quad_count,
        "standalone_triangle_count": len(primitives) - quad_count,
        "render_primitive_count": len(primitives),
        "commands_saved": len(face_list) - len(primitives),
        "rejection_reasons": rejected,
    }
