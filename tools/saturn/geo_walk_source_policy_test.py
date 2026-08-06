#!/usr/bin/env python3
"""Reject direct recursive scene-graph calls in the Saturn production source."""
from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "src" / "game" / "rendering_graph_node.c"


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    # Exclude the public definition itself; every remaining call is a handler
    # recursion edge that must become an explicit continuation frame.
    body = re.sub(
        r"void\s+geo_process_node_and_siblings\s*\([^)]*\)\s*\{",
        "",
        text,
        count=1,
    )
    direct_calls = list(re.finditer(r"\bgeo_process_node_and_siblings\s*\(", body))
    assert not direct_calls, (
        f"production Saturn geo path still has {len(direct_calls)} direct "
        "recursive dispatcher calls"
    )
    assert "saturn_geo_walk_runtime_frame_t" in text
    assert ".lwram_geo_traversal" in (ROOT / "src/port/saturn/runtime/saturn_geo_walk_storage.c").read_text(encoding="utf-8")
    print("geo walk source policy: PASS")


if __name__ == "__main__":
    main()
