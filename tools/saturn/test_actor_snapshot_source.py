"""Static source/ABI guard for the generic actor snapshot seam."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
GFX = ROOT / "src/port/saturn/gfx"
RENDERING = ROOT / "src/game/rendering_graph_node.c"
SOURCEBOOT = ROOT / "src/port/saturn/sourceboot/main.c"


def body(path: Path, symbol: str) -> str:
    return body_text(path.read_text(encoding="utf-8"), symbol)


def body_text(text: str, symbol: str) -> str:
    start = text.index(symbol)
    opening = text.index("{", start)
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1:index]
    raise AssertionError(f"unterminated {symbol}")


def struct_body(text: str, name: str) -> str:
    match = re.search(
        rf"typedef struct {name} \{{(?P<body>.*?)\}} {name}_t;",
        text,
        flags=re.DOTALL,
    )
    assert match, f"missing {name}"
    return match.group("body")


def test_snapshot_and_observation_are_pointer_free() -> None:
    header = (GFX / "saturn_actor_instance.h").read_text(encoding="utf-8")
    for name in (
        "sm64_saturn_actor_instance_snapshot",
        "sm64_saturn_actor_source_observation",
    ):
        assert "*" not in struct_body(header, name)
    assert "_Static_assert(sizeof(sm64_saturn_actor_instance_snapshot_t) == 188U" in header
    assert "_Alignof(sm64_saturn_actor_instance_snapshot_t) == 4U" in header


def test_capture_copies_source_values_and_rejects_bad_identity() -> None:
    implementation = (GFX / "saturn_actor_instance.c").read_text(encoding="utf-8")
    capture = body(GFX / "saturn_actor_instance.c", "sm64_saturn_actor_instances_capture")
    assert "source->source_generation != generation" in implementation
    assert "source->family_id == 0U" in implementation
    assert "source->actor_bank_id == 0U" in implementation
    for field in (
        "position_q16", "scale_q16", "held_offset_q16", "render_range_min_q16",
        "animation_accel", "effect_params_q16", "opacity", "billboard_state",
        "shadow_solidity", "effect_kind", "switch_state",
    ):
        assert field in capture, f"source field {field} is not copied"
    assert "capacity_overflow_count++" in capture
    assert "overflow_latched" in capture
    assert "source->pool_slot >= observer->capacity" in capture
    assert "pool_slot_overflow_count" in capture
    assert "instance_key = ((uint32_t)observer->incarnation" in capture
    assert "sm64_saturn_actor_instance_bank_capture" in implementation
    assert "CPU_CACHE_THROUGH" in implementation
    assert "actor_bank_fence" in implementation
    assert "sm64_saturn_render_generation_next" in implementation


def assert_capture_uses_cache_through_payload(text: str) -> None:
    capture = body_text(text, "sm64_saturn_actor_instance_bank_capture")
    assert "actor_bank_uncached(bank)" in capture
    assert "shared->snapshots[selected]" in capture
    assert "bank->snapshots[selected]" not in capture


def test_bank_capture_payload_uses_cache_through_alias_and_rejects_cached_mutation() -> None:
    source = GFX / "saturn_actor_instance.c"
    implementation = source.read_text(encoding="utf-8")
    assert_capture_uses_cache_through_payload(implementation)
    mutation = implementation.replace(
        "shared->snapshots[selected]", "bank->snapshots[selected]", 1
    )
    try:
        assert_capture_uses_cache_through_payload(mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("cached payload-write mutation escaped the source gate")


def test_observer_only_records_geo_decisions_at_source_boundary() -> None:
    observer = (GFX / "saturn_geo_state_observer.c").read_text(encoding="utf-8")
    rendering = RENDERING.read_text(encoding="utf-8")
    assert "sm64_saturn_geo_state_observer_record_geo_decision" in observer
    assert "sm64_saturn_geo_state_observer_begin_object" in observer
    assert "sm64_saturn_geo_state_observer_record_switch" in observer
    # No observer call may select a child, mutate an Object, or write a source
    # graph link.  The rendering hook is checked separately once target code is
    # compiled, but the observer module itself must remain scalar-only.
    assert "geo_process_node_and_siblings" not in observer
    assert "gLoadedGraphNodes" not in observer
    assert "struct Object" not in observer
    assert "obj_is_in_view" in rendering
    assert "saturn_source_observe_object_begin" in rendering
    assert "sm64_saturn_geo_state_observer_end_object" in rendering
    assert "saturn_source_model_id" in rendering
    capture = (GFX / "saturn_actor_instance.c").read_text(encoding="utf-8")
    assert "source->family_id == 0U" in capture
    assert "source->actor_bank_id == 0U" in capture
    assert "scene_package_generation == 0U" in capture


def test_two_bank_lifecycle_is_explicit_and_sourceboot_orders_capture() -> None:
    header = (GFX / "saturn_actor_instance.h").read_text(encoding="utf-8")
    sourceboot = SOURCEBOOT.read_text(encoding="utf-8")
    assert "snapshots[2][" in header
    assert "sm64_saturn_actor_instance_bank_capture" in header
    for state in (
        "SM64_SATURN_ACTOR_INSTANCE_BANK_FREE",
        "SM64_SATURN_ACTOR_INSTANCE_BANK_WRITING",
        "SM64_SATURN_ACTOR_INSTANCE_BANK_READY",
        "SM64_SATURN_ACTOR_INSTANCE_BANK_RENDERING",
    ):
        assert state in header
    # The actor capture is called from the existing render-publication seam,
    # after the source tick has advanced its generation and counters.
    tick = sourceboot[sourceboot.index("static void sourceboot_run_source_tick") :]
    assert tick.index("sourceboot_sim_tick_count =") < tick.index(
        "sourceboot_capture_render_snapshot(sourceboot_sim_tick_count);"
    )
    assert "sourceboot_actor_instances" in sourceboot
    assert "sm64_saturn_actor_instance_bank_capture" in sourceboot
    assert "sourceboot_actor_bank_generation[2]" in sourceboot
    assert "actor_instance_bank_valid" in sourceboot
    assert tick.index("sm64_saturn_geo_state_observer_begin_frame") < tick.index(
        "game_loop_one_iteration"
    )
    capture = sourceboot[
        sourceboot.index("static void sourceboot_capture_render_snapshot"):
        sourceboot.index("static void sourceboot_run_source_tick")
    ]
    assert "begin_frame" not in capture
    assert capture.index("sm64_saturn_geo_state_observer_end_frame") < capture.index(
        "sm64_saturn_actor_instance_bank_capture"
    )


if __name__ == "__main__":
    test_snapshot_and_observation_are_pointer_free()
    test_capture_copies_source_values_and_rejects_bad_identity()
    test_bank_capture_payload_uses_cache_through_alias_and_rejects_cached_mutation()
    test_observer_only_records_geo_decisions_at_source_boundary()
    test_two_bank_lifecycle_is_explicit_and_sourceboot_orders_capture()
