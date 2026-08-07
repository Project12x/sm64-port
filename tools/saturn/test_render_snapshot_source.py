"""Reject pointer fields in the published, slave-facing snapshot types."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.h"
ACTOR = ROOT / "src/port/saturn/gfx/saturn_actor_bridge.h"
HUD = ROOT / "src/port/saturn/gfx/saturn_hud.h"
IMPLEMENTATION = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.c"


def typedef_body(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(
        rf"typedef struct {name} \{{(?P<body>.*?)\}} {name}_t;",
        text,
        flags=re.DOTALL,
    )
    assert match is not None, f"missing {name}"
    return match.group("body")


def function_body(text: str, name: str) -> str:
    start = text.index(f"{name}(")
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated {name}")


def test_snapshot_types_have_no_pointer_fields() -> None:
    names = (
        (SNAPSHOT, "sm64_saturn_render_view"),
        (SNAPSHOT, "sm64_saturn_render_snapshot"),
        (SNAPSHOT, "sm64_saturn_render_snapshot_release"),
        (ACTOR, "sm64_saturn_mario_actor_snapshot"),
        (ACTOR, "sm64_saturn_mario_pose_selector"),
        (HUD, "sm64_saturn_hud_snapshot"),
    )
    for path, name in names:
        assert "*" not in typedef_body(path, name), (
            f"{name} must not carry live game, graph, VDP1, or VRAM pointers"
        )


def test_release_and_peer_payload_use_cache_through_accessors() -> None:
    header = SNAPSHOT.read_text(encoding="utf-8")
    implementation = IMPLEMENTATION.read_text(encoding="utf-8")
    assert "#if defined(__sh__)" in header
    assert "CPU_CACHE_THROUGH" in header
    for symbol in (
        "sm64_saturn_render_snapshot_cache_through",
        "sm64_saturn_render_snapshot_release_uncached",
        "sm64_saturn_render_snapshot_peer_payload",
    ):
        assert symbol in header
    for symbol in (
        "sm64_saturn_render_snapshot_release_uncached",
        "sm64_saturn_render_snapshot_peer_payload",
    ):
        assert symbol in implementation
    assert "slot->release.state" not in implementation
    assert "slot->release.generation" not in implementation


def test_ready_claim_uses_sh2_atomic_test_and_set() -> None:
    header = SNAPSHOT.read_text(encoding="utf-8")
    implementation = IMPLEMENTATION.read_text(encoding="utf-8")
    assert "tas.b" in header
    assert "sm64_saturn_render_snapshot_release_claim_try" in implementation
    assert "sm64_saturn_render_snapshot_release_claim_release" in implementation


def test_producer_writes_payload_through_cache_through_alias() -> None:
    header = SNAPSHOT.read_text(encoding="utf-8")
    implementation = IMPLEMENTATION.read_text(encoding="utf-8")
    assert "sm64_saturn_render_snapshot_owner_payload" in header
    for name in (
        "sm64_saturn_render_snapshot_reset",
        "sm64_saturn_render_snapshot_begin_write",
        "sm64_saturn_render_snapshot_retire",
    ):
        body = function_body(implementation, name)
        assert "sm64_saturn_render_snapshot_owner_payload" in body
        assert "memset(&slot->snapshot" not in body
    assert "*out = sm64_saturn_render_snapshot_owner_payload(slot);" in implementation


def test_terminal_transitions_share_the_claim_lock() -> None:
    implementation = IMPLEMENTATION.read_text(encoding="utf-8")
    for name in (
        "sm64_saturn_render_snapshot_quarantine",
        "sm64_saturn_render_snapshot_complete",
        "sm64_saturn_render_snapshot_retire",
    ):
        body = function_body(implementation, name)
        assert "sm64_saturn_render_snapshot_release_claim_try" in body
        assert "sm64_saturn_render_snapshot_release_claim_release" in body


if __name__ == "__main__":
    test_snapshot_types_have_no_pointer_fields()
    test_release_and_peer_payload_use_cache_through_accessors()
    test_ready_claim_uses_sh2_atomic_test_and_set()
    test_producer_writes_payload_through_cache_through_alias()
    test_terminal_transitions_share_the_claim_lock()
