"""Host tests for terrain refinement invariants.

These assert GEOMETRY and BAKEABILITY, not return codes. A refinement that
returned the right triangle count while leaving a hanging node would still
crack the floor, and one that left a face above the UV-repeat limit would
still produce a white quad at runtime.

Runnable two ways:
  * `python -m pytest tools/saturn/test_subdivide_terrain_ir.py -v`, if pytest
    is installed for the interpreter running it.
  * `python tools/saturn/test_subdivide_terrain_ir.py` as a plain script --
    the `__main__` block below calls each test directly and prints a pass
    line. This project's SATURN_TOOLS_PYTHON venv does not have pytest
    installed, so the Makefile's verify-terrain-subdivision target uses this
    form.
"""
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import subdivide_terrain_ir as sub

TILE = {
    "uls": 0, "ult": 0, "lrs": 124, "lrt": 124,
    "width": 32, "height": 32,
}


def _doc(positions, uvs, triangles):
    return {
        "positions": [list(p) for p in positions],
        "vertex_attributes": {"uv": [list(uv) for uv in uvs]},
        "triangles": triangles,
    }


def test_uv_repeat_splits_until_bakeable():
    """A face whose UV span repeats 27x must be refined until every
    surviving face is at or under the baker's limit of 16."""
    positions = [[0, 0, 0], [4000, 0, 0], [0, 0, 4000]]
    # 27x the tile period: 27 * 8 * (124 - 0 + 4) = 27648
    uvs = [[0, 0], [27648, 0], [0, 27648]]
    tri = {"source": 0, "material": 0, "indices": [0, 1, 2],
           "texture_tile": {"state": TILE}}
    document, _ = sub.subdivide(_doc(positions, uvs, [tri]),
                               threshold=0.0, max_passes=6,
                               uv_repeat_limit=16.0)
    repeats = [sub.triangle_uv_repeat(t, document["vertex_attributes"]["uv"])
               for t in document["triangles"]]
    assert repeats, "refinement produced no triangles"
    assert max(repeats) <= 16.0, f"still unbakeable: max k={max(repeats)}"


def test_no_t_junctions_across_a_shared_edge():
    """Two triangles sharing an edge must receive the SAME midpoint index
    when that edge is split, or the floor cracks along the seam."""
    positions = [[0, 0, 0], [4000, 0, 0], [0, 0, 4000], [4000, 0, 4000]]
    uvs = [[0, 0], [27648, 0], [0, 27648], [27648, 27648]]
    triangles = [
        {"source": 0, "material": 0, "indices": [0, 1, 2],
         "texture_tile": {"state": TILE}},
        {"source": 1, "material": 0, "indices": [1, 3, 2],
         "texture_tile": {"state": TILE}},
    ]
    document, _ = sub.subdivide(_doc(positions, uvs, triangles),
                               threshold=0.0, max_passes=4,
                               uv_repeat_limit=16.0)
    # Every vertex position must be referenced by every triangle that
    # geometrically touches it: collect positions used as corners, then
    # assert no triangle edge passes through a corner it does not own.
    pos = document["positions"]
    corners = {tuple(pos[i]) for t in document["triangles"]
               for i in t["indices"]}
    for tri in document["triangles"]:
        a, b, c = (pos[i] for i in tri["indices"])
        for (p, q) in ((a, b), (b, c), (c, a)):
            mid = tuple((p[k] + q[k]) // 2 for k in range(3))
            if mid in (tuple(p), tuple(q)):
                continue
            assert mid not in corners, (
                f"hanging node at {mid}: an edge of triangle "
                f"{tri['indices']} passes through another face's corner")


def test_untouched_triangle_is_byte_identical():
    """A face already under both limits must pass through unchanged,
    including its source id, so no cost is paid where none is needed."""
    positions = [[0, 0, 0], [10, 0, 0], [0, 0, 10]]
    uvs = [[0, 0], [64, 0], [0, 64]]
    tri = {"source": 7, "material": 3, "indices": [0, 1, 2],
           "texture_tile": {"state": TILE}}
    document, provenance = sub.subdivide(_doc(positions, uvs, [tri]),
                                         threshold=0.0, max_passes=4,
                                         uv_repeat_limit=16.0)
    assert len(document["triangles"]) == 1
    assert document["triangles"][0]["source"] == 7
    assert document["positions"] == positions
    assert provenance == {}


def test_idempotent_when_nothing_exceeds_the_limits():
    """Running twice must not mint ids or grow the mesh. make can run a
    recipe more than once per build."""
    positions = [[0, 0, 0], [10, 0, 0], [0, 0, 10]]
    uvs = [[0, 0], [64, 0], [0, 64]]
    tri = {"source": 0, "material": 0, "indices": [0, 1, 2],
           "texture_tile": {"state": TILE}}
    first, _ = sub.subdivide(_doc(positions, uvs, [tri]), 0.0, 4, 16.0)
    second, _ = sub.subdivide(first, 0.0, 4, 16.0)
    assert first == second


if __name__ == "__main__":
    tests = [
        test_uv_repeat_splits_until_bakeable,
        test_no_t_junctions_across_a_shared_edge,
        test_untouched_triangle_is_byte_identical,
        test_idempotent_when_nothing_exceeds_the_limits,
    ]
    for test in tests:
        test()
        print(f"PASS: {test.__name__}")
    print(f"{len(tests)} passed")
