"""Reject pointer fields in the published, slave-facing snapshot types."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.h"
ACTOR = ROOT / "src/port/saturn/gfx/saturn_actor_bridge.h"


def typedef_body(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"typedef struct {name} \{{(?P<body>.*?)\}} {name}_t;",
        text,
        flags=re.DOTALL,
    )
    assert match is not None, f"missing {name}"
    return match.group("body")


def test_snapshot_types_have_no_pointer_fields() -> None:
    names = (
        (SNAPSHOT, "sm64_saturn_render_view"),
        (SNAPSHOT, "sm64_saturn_render_snapshot"),
        (SNAPSHOT, "sm64_saturn_render_snapshot_release"),
        (ACTOR, "sm64_saturn_mario_actor_snapshot"),
        (ACTOR, "sm64_saturn_mario_pose_selector"),
    )
    for path, name in names:
        assert "*" not in typedef_body(path, name), (
            f"{name} must not carry live game, graph, VDP1, or VRAM pointers"
        )


if __name__ == "__main__":
    test_snapshot_types_have_no_pointer_fields()
