"""Deterministic triangle pairing for Saturn VDP1 render primitives.

The source mesh remains triangulated.  This module only builds a render-time
index table that replaces compatible pairs with true four-corner primitives.
"""

from __future__ import annotations

from dataclasses import dataclass
import math
from typing import Any, Iterable

try:
    import networkx as nx
except ModuleNotFoundError as error:  # pragma: no cover - exercised by bootstrap failure
    raise ModuleNotFoundError(
        "Saturn mesh tools require NetworkX; run tools/saturn/bootstrap-host-tools.ps1 "
        "or tools/saturn/bootstrap-host-tools.sh"
    ) from error


Vertex = tuple[int, int, int]
Face = tuple[int, int, int, int]
DEFAULT_TEXTURED_AFFINE_ERROR_LIMIT = 256


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


def _is_planar_convex(vertices: list[Vertex], cycle: tuple[int, int, int, int]) -> bool:
    """Prove a static 3D quad remains convex under valid camera projection.

    A convex polygon on one plane stays convex under a projective transform as
    long as it does not cross the camera plane. Runtime near clipping enforces
    the latter. This dominant-axis proof avoids rejecting Castle floors and
    walls merely because an arbitrary orthographic pose sample sees them
    exactly edge-on; the sampled policy remains the default for deforming
    actors.
    """
    points = [vertices[index] for index in cycle]
    normal = _cross(_sub(points[1], points[0]), _sub(points[2], points[0]))
    normal_length_squared = _dot(normal, normal)
    if normal_length_squared == 0:
        return False
    plane_error = abs(_dot(normal, _sub(points[3], points[0])))
    # Castle source vertices are integral and intended coplanar. Permit at
    # most one source-space unit of quantization distance from the plane.
    if plane_error > math.sqrt(normal_length_squared):
        return False
    dominant = max(range(3), key=lambda axis: abs(normal[axis]))
    axes = tuple(axis for axis in range(3) if axis != dominant)
    projected = [(float(point[axes[0]]), float(point[axes[1]])) for point in points]
    return _is_strictly_convex(projected)


def _texture_state_key(state: Any) -> str:
    """Return a deterministic key for the tile state carried by one source face."""
    if state is None:
        return "<none>"
    if not isinstance(state, dict):
        return repr(state)
    return repr(sorted((str(key), repr(value)) for key, value in state.items()))


def _textured_pair_is_safe(
    cycle: tuple[int, int, int, int],
    texture_tiles: list[dict[str, Any] | None],
    vertex_uvs: list[tuple[int, int]],
    first_index: int,
    second_index: int,
    affine_error_limit: int,
) -> tuple[bool, str]:
    """Check the v2 textured-quad contract without weakening geometry gates.

    The attribute-exact weld makes UVs at a shared edge identical by
    construction. We require identical tile state and bound the opposite-
    corner affine residual. The latter is conservative: VDP1's
    rectangle mapper can represent a parallelogram exactly, while a small
    integer residual is tolerated for source quantisation.
    """
    left = texture_tiles[first_index]
    right = texture_tiles[second_index]
    if left is None or right is None:
        return False, "textured_state_missing"
    if left.get("texture") != right.get("texture"):
        return False, "texture_identity_mismatch"
    if _texture_state_key(left.get("state")) != _texture_state_key(right.get("state")):
        return False, "texture_tile_state_mismatch"
    uvs = [vertex_uvs[index] for index in cycle]
    if len(set(uvs)) != 4:
        return False, "uv_cycle_degenerate"
    residual = (uvs[0][0] + uvs[2][0] - uvs[1][0] - uvs[3][0]) ** 2 + (
        uvs[0][1] + uvs[2][1] - uvs[1][1] - uvs[3][1]
    ) ** 2
    if residual > affine_error_limit * affine_error_limit:
        return False, "uv_affine_error"
    return True, ""


def candidates(
    vertices: list[Vertex],
    faces: list[Face],
    minimum_normal_alignment: float = 0.80,
    deformation_poses: Iterable[tuple[str, list[Vertex]]] | None = None,
    pairing_forbidden_triangles: set[int] | None = None,
    projection_policy: str = "sampled",
    texture_tiles: list[dict[str, Any] | None] | None = None,
    vertex_uvs: list[tuple[int, int]] | None = None,
    textured_affine_error_limit: int = DEFAULT_TEXTURED_AFFINE_ERROR_LIMIT,
) -> tuple[list[QuadCandidate], dict[str, int]]:
    """Return Saturn-safe shared-edge candidates and rejection counts.

    A candidate must remain aligned and project to a strictly convex VDP1
    quadrilateral in the neutral mesh and every supplied deformation pose.
    Pose samples are an offline safety contract; they are not emitted to the
    Saturn binary by this module.
    """
    if projection_policy not in {"sampled", "planar"}:
        raise ValueError("projection_policy must be 'sampled' or 'planar'")
    pose_list = list(deformation_poses or [])
    forbidden = pairing_forbidden_triangles or set()
    if texture_tiles is not None and len(texture_tiles) != len(faces):
        raise ValueError("texture_tiles length must match faces")
    if texture_tiles is not None and vertex_uvs is None:
        raise ValueError("textured pairing requires vertex_uvs")
    if vertex_uvs is not None and len(vertex_uvs) != len(vertices):
        raise ValueError("vertex_uvs length must match vertices")
    for name, pose_vertices in pose_list:
        if len(pose_vertices) != len(vertices):
            raise ValueError(
                f"deformation pose {name!r} has {len(pose_vertices)} vertices; "
                f"expected {len(vertices)}"
            )
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
        if first_index in forbidden or second_index in forbidden:
            reject("textured_pairing_not_implemented")
            continue
        if first[0] != second[0]:
            reject("material_mismatch")
            continue
        boundary = _boundary_cycle(first[1:], second[1:])
        if boundary is None:
            reject("winding_or_topology")
            continue
        cycle, _ = boundary
        if texture_tiles is not None and (
            texture_tiles[first_index] is not None or texture_tiles[second_index] is not None
        ):
            textured_ok, textured_reason = _textured_pair_is_safe(
                cycle,
                texture_tiles,
                vertex_uvs or [],
                first_index,
                second_index,
                textured_affine_error_limit,
            )
            if not textured_ok:
                reject(textured_reason)
                continue
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
        projection_safe = (
            _is_planar_convex(vertices, cycle)
            if projection_policy == "planar"
            else _projection_is_stable(vertices, cycle)
        )
        if not projection_safe:
            reject("planar_quad_not_convex" if projection_policy == "planar"
                   else "projection_not_convex")
            continue
        pose_rejection = None
        for _pose_name, pose_vertices in pose_list:
            first_pose_normal = _normal(pose_vertices, first[1:])
            second_pose_normal = _normal(pose_vertices, second[1:])
            pose_lengths = math.sqrt(
                _dot(first_pose_normal, first_pose_normal)
                * _dot(second_pose_normal, second_pose_normal)
            )
            if pose_lengths == 0:
                pose_rejection = "pose_degenerate_triangle"
                break
            pose_alignment = _dot(first_pose_normal, second_pose_normal) / pose_lengths
            if pose_alignment < minimum_normal_alignment:
                pose_rejection = "pose_normal_divergence"
                break
            pose_projection_safe = (
                _is_planar_convex(pose_vertices, cycle)
                if projection_policy == "planar"
                else _projection_is_stable(pose_vertices, cycle)
            )
            if not pose_projection_safe:
                pose_rejection = "pose_projection_not_convex"
                break
        if pose_rejection is not None:
            reject(pose_rejection)
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
    vertices: Iterable[Vertex],
    faces: Iterable[Face],
    deformation_poses: Iterable[tuple[str, Iterable[Vertex]]] | None = None,
    pairing_forbidden_triangles: set[int] | None = None,
    texture_tiles: Iterable[dict[str, Any] | None] | None = None,
    vertex_uvs: Iterable[tuple[int, int]] | None = None,
    projection_policy: str = "sampled",
    textured_affine_error_limit: int = DEFAULT_TEXTURED_AFFINE_ERROR_LIMIT,
) -> tuple[list[RenderPrimitive], dict[str, object]]:
    vertex_list, face_list = list(vertices), list(faces)
    pose_list = [(name, list(pose)) for name, pose in (deformation_poses or [])]
    texture_tile_list = list(texture_tiles) if texture_tiles is not None else None
    uv_list = list(vertex_uvs) if vertex_uvs is not None else None
    options, rejected = candidates(
        vertex_list,
        face_list,
        deformation_poses=pose_list,
        pairing_forbidden_triangles=pairing_forbidden_triangles,
        projection_policy=projection_policy,
        texture_tiles=texture_tile_list,
        vertex_uvs=uv_list,
        textured_affine_error_limit=textured_affine_error_limit,
    )
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
        "deformation_pose_count": len(pose_list),
        "deformation_pose_names": [name for name, _vertices in pose_list],
        "source_triangle_count": len(face_list),
        "candidate_count": len(options),
        "quad_count": quad_count,
        "standalone_triangle_count": len(primitives) - quad_count,
        "render_primitive_count": len(primitives),
        "commands_saved": len(face_list) - len(primitives),
        "rejection_reasons": rejected,
        "textured_pairing": texture_tile_list is not None,
        "textured_affine_error_limit": textured_affine_error_limit,
    }
