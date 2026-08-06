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


def assert_source_tick_generation_contract(tick: str) -> None:
    declaration = re.search(
        r"const\s+uint32_t\s+source_tick_generation\s*=\s*"
        r"sm64_saturn_frame_pipeline_next_generation\(sourceboot_sim_tick_count\)\s*;",
        tick,
    )
    assert declaration, "source tick must declare exactly one canonical successor"
    assert len(re.findall(
        r"sm64_saturn_frame_pipeline_next_generation\(sourceboot_sim_tick_count\)",
        tick,
    )) == 1
    observer = re.search(
        r"sm64_saturn_geo_state_observer_begin_frame\s*\(\s*"
        r"&sourceboot_actor_observer\s*,\s*source_tick_generation\s*\)",
        tick,
    )
    assert observer, "observer must open with the named successor"
    game_loop = tick.index("game_loop_one_iteration()")
    assignment = re.search(
        r"sourceboot_sim_tick_count\s*=\s*source_tick_generation\s*;", tick
    )
    assert assignment, "source tick must publish the named successor"
    for consumer in (
        "sourceboot_fast3d.profile.sim_tick_count = source_tick_generation;",
        "sourceboot_capture_render_snapshot(source_tick_generation);",
        "sm64_saturn_camera_bypass_arm(source_tick_generation);",
    ):
        assert consumer in tick, f"same-tick consumer escaped named successor: {consumer}"
    idle_probe = re.search(
        r"sm64_saturn_sourceboot_camera_idle_probe_record\s*\(\s*"
        r"sm64_saturn_source_runtime_state\s*\(\s*\)\s*,\s*"
        r"source_tick_generation\s*\)",
        tick,
        re.S,
    )
    assert idle_probe, "idle probe must use the named successor"
    assert declaration.start() < observer.start() < game_loop < assignment.start()
    assert assignment.start() < tick.index("sourceboot_capture_render_snapshot(")
    assert "sourceboot_sim_tick_count + 1U" not in tick
    assert "sourceboot_sim_tick_count++" not in tick


def test_sourceboot_uses_one_skip_zero_generation_for_observer_and_consumers() -> None:
    sourceboot = SOURCEBOOT.read_text(encoding="utf-8")
    tick = body(SOURCEBOOT, "sourceboot_run_source_tick")
    assert_source_tick_generation_contract(tick)

    observer_mutation, changed = re.subn(
        r"(&sourceboot_actor_observer\s*,\s*)source_tick_generation",
        r"\1sourceboot_sim_tick_count + 1U",
        tick,
        count=1,
    )
    assert changed == 1
    try:
        assert_source_tick_generation_contract(observer_mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("raw observer increment mutation escaped source gate")

    capture_mutation = tick.replace(
        "sourceboot_capture_render_snapshot(source_tick_generation);",
        "sourceboot_capture_render_snapshot(sourceboot_sim_tick_count);",
        1,
    )
    assert capture_mutation != tick
    try:
        assert_source_tick_generation_contract(capture_mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("global capture mutation escaped source gate")

    camera_mutation = tick.replace(
        "sm64_saturn_camera_bypass_arm(source_tick_generation);",
        "sm64_saturn_camera_bypass_arm(sourceboot_sim_tick_count);",
        1,
    )
    assert camera_mutation != tick
    try:
        assert_source_tick_generation_contract(camera_mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("global camera mutation escaped source gate")

    idle_probe_mutation, changed = re.subn(
        r"(sm64_saturn_sourceboot_camera_idle_probe_record\s*\(\s*"
        r"sm64_saturn_source_runtime_state\s*\(\s*\)\s*,\s*)"
        r"source_tick_generation",
        r"\1sourceboot_sim_tick_count",
        tick,
        count=1,
        flags=re.S,
    )
    assert changed == 1
    try:
        assert_source_tick_generation_contract(idle_probe_mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("global idle-probe mutation escaped source gate")


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
    assert "source->pool_slot >= SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY" in capture
    assert "pool_slot_overflow_count" in capture
    assert "instance_key = ((uint32_t)observer->incarnation" in capture
    assert "sm64_saturn_actor_instance_bank_capture" in implementation
    assert "CPU_CACHE_THROUGH" in implementation
    assert "actor_bank_fence" in implementation
    assert "sm64_saturn_render_generation_next" in implementation


def test_source_pool_identity_bound_stays_separate_from_compact_capacity() -> None:
    header = (GFX / "saturn_actor_instance.h").read_text(encoding="utf-8")
    observer = body(GFX / "saturn_geo_state_observer.c",
                    "sm64_saturn_geo_state_observer_begin_object")
    capture = body(GFX / "saturn_actor_instance.c",
                   "sm64_saturn_actor_instances_capture")
    validation = body(GFX / "saturn_actor_instance.c", "valid_observation")
    assert "SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY 240U" in header
    assert "OBJECT_POOL_CAPACITY" in header
    assert "seen[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY]" in header
    assert "live[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY]" in header
    assert "incarnation[SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY]" in header
    assert "observation->pool_slot >= SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY" in observer
    assert "observer->count >= observer->capacity" in observer
    assert "source->pool_slot >= SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY" in capture
    assert "source->parent_index >= SM64_SATURN_ACTOR_SOURCE_POOL_CAPACITY" in validation


def assert_pre_acquire_recycle_contract(capture: str, source_capture: str) -> None:
    assert re.search(
        r"sm64_saturn_actor_instance_bank_recycle_pre_acquire\s*\(\s*"
        r"bank\s*,\s*selected\s*,\s*generation\s*,\s*"
        r"SM64_SATURN_ACTOR_INSTANCE_BANK_WRITING\s*\)", capture, re.S
    )
    assert "sm64_saturn_actor_instance_bank_quarantine(bank, generation)" not in capture
    assert re.search(
        r"sm64_saturn_actor_instance_bank_recycle_pre_acquire\s*\(\s*"
        r"&sourceboot_actor_instances\s*,\s*actor_bank\s*,\s*generation\s*,\s*"
        r"SM64_SATURN_ACTOR_INSTANCE_BANK_READY\s*\)", source_capture, re.S
    )


def test_pre_acquire_failures_use_exact_producer_recycle() -> None:
    implementation = (GFX / "saturn_actor_instance.c").read_text(encoding="utf-8")
    sourceboot = SOURCEBOOT.read_text(encoding="utf-8")
    capture = body_text(implementation, "sm64_saturn_actor_instance_bank_capture")
    source_capture = body_text(sourceboot, "sourceboot_capture_render_snapshot")
    assert_pre_acquire_recycle_contract(capture, source_capture)
    capture_mutation = capture.replace(
        "sm64_saturn_actor_instance_bank_recycle_pre_acquire",
        "sm64_saturn_actor_instance_bank_quarantine", 1)
    assert capture_mutation != capture
    try:
        assert_pre_acquire_recycle_contract(capture_mutation, source_capture)
    except AssertionError:
        pass
    else:
        raise AssertionError("capture quarantine mutation escaped source gate")
    source_mutation = source_capture.replace(
        "sm64_saturn_actor_instance_bank_recycle_pre_acquire",
        "sm64_saturn_actor_instance_bank_quarantine", 1)
    assert source_mutation != source_capture
    try:
        assert_pre_acquire_recycle_contract(capture, source_mutation)
    except AssertionError:
        pass
    else:
        raise AssertionError("sourceboot quarantine mutation escaped source gate")


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
        "sourceboot_capture_render_snapshot(source_tick_generation);"
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
    test_sourceboot_uses_one_skip_zero_generation_for_observer_and_consumers()
    test_snapshot_and_observation_are_pointer_free()
    test_capture_copies_source_values_and_rejects_bad_identity()
    test_source_pool_identity_bound_stays_separate_from_compact_capacity()
    test_pre_acquire_failures_use_exact_producer_recycle()
    test_bank_capture_payload_uses_cache_through_alias_and_rejects_cached_mutation()
    test_observer_only_records_geo_decisions_at_source_boundary()
    test_two_bank_lifecycle_is_explicit_and_sourceboot_orders_capture()
