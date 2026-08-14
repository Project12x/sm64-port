"""Reject pointer fields in the published, slave-facing snapshot types."""
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.h"
ACTOR = ROOT / "src/port/saturn/gfx/saturn_actor_bridge.h"
HUD = ROOT / "src/port/saturn/gfx/saturn_hud.h"
IMPLEMENTATION = ROOT / "src/port/saturn/gfx/saturn_render_snapshot.c"
BRIDGE = ROOT / "src/port/saturn/gfx/saturn_actor_bridge.c"
MESHLETS = ROOT / "src/port/saturn/gfx/saturn_actor_meshlets.c"
SOURCEBOOT_MAIN = ROOT / "src/port/saturn/sourceboot/main.c"
SOURCE_SCENE_BUNDLE = ROOT / "src/port/saturn/sourceboot/source_scene_bundle.c"
DEMO_RENDER = ROOT / "src/port/saturn/gfx/saturn_demo_render.c"
VDP1_BACKEND = ROOT / "src/port/saturn/gfx/saturn_vdp1_backend.h"


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


def test_complete_mario_uses_precomputed_vertex_gouraud() -> None:
    """The complete-pose bridge must not publish pose evaluator's flat 255s."""
    body = function_body(BRIDGE.read_text(encoding="utf-8"),
                         "complete_actor_evaluate")
    assert "sm64_mario_animation_light_intensity" in body
    assert "memcpy(slot->lights" in body


def test_opaque_actor_meshlets_use_depth_bins() -> None:
    """Opaque output is VDP1 painter-order input, never source-order input."""
    body = function_body(MESHLETS.read_text(encoding="utf-8"),
                         "actor_meshlet_core")
    assert "opaque_bins[SM64_SATURN_ACTOR_DEPTH_BIN_COUNT]" in body
    assert "opaque_output[opaque_cursor[bin]++] = ref" in body


def test_normal_bob_visual_repair_preserves_the_hardware_memory_floor() -> None:
    """The generic visual route may not consume the reserved slave stack."""
    main = SOURCEBOOT_MAIN.read_text(encoding="utf-8")
    scene = SOURCE_SCENE_BUNDLE.read_text(encoding="utf-8")
    assert "sourceboot_hud_publish_state" not in main
    assert "SOURCE_SCENE_LWRAM_STATE" not in scene
    assert "source_scene_actor_texture_partitions_t" in scene
    assert "vdp1_vram_partitions_t texture_partitions;" not in scene


def test_vdp1_painter_chain_uses_all_existing_master_depth_tags() -> None:
    """Terrain, Mario, and generic actors must enter one VDP1 painter list."""
    text = DEMO_RENDER.read_text(encoding="utf-8")
    terrain = function_body(text, "demo_emit_terrain_result")
    assert "sm64_saturn_terrain_depth_bin(result->painter_key)" in terrain
    assert "cmdt->cmd_link = painter_bin" in terrain
    assert "cmdt->cmd_link = (uint16_t)(ref->sort_key >> 16);" in text
    assert "detail->cmd_link = (uint16_t)(ref->sort_key >> 16);" in text
    assert "records[local].sort_key >> 16" in text
    assert "sm64_saturn_vdp1_backend_link_depth_bins(" in text
    assert "SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT" in text


def test_mario_textured_path_uses_fixed_gouraud_tables() -> None:
    """Direct RGB1555 texture texels use the neutral existing light ramp."""
    text = DEMO_RENDER.read_text(encoding="utf-8")
    start = text.index("static void __attribute__((unused)) demo_emit_mario_range")
    body = text[start:text.index("#if SATURN_SLAVE_RENDER", start)]
    assert "s_mario_transform_context.light_intensity[corners[corner]]" in body
    assert "s_actor_light_intensity" not in body
    assert "for (uint8_t corner = 0; corner < 4U; corner++)" in body
    assert "table->colors[corner]" in body
    assert "SM64_SATURN_MARIO_TEXTURE_GOURAUD_MATERIAL" in text
    assert "sm64_saturn_mario_light_scale" not in body
    assert "/ 31U" not in body
    # RGB1555 texture texels are themselves the command base color.  VDP1
    # interprets a Gouraud entry as a signed correction around neutral gray,
    # so reusing a material-final-color entry here shifts the texture hue.
    # Base material RGB plus the already-present neutral grayscale material
    # row gives direct-color overlays the same fixed light path without a
    # second HWRAM-resident table or per-texture table allocation.
    assert "VDP1_CMDT_CC_GOURAUD" in body
    assert "VDP1_CMDT_CC_GOURAUD, texture_vertices" in body
    assert "table != NULL &&" in body
    assert "vdp1_cmdt_gouraud_base_set(" in body
    assert "sm64_saturn_mario_texture_gouraud_color" not in text
    assert "SM64_SATURN_MARIO_TEXTURE_GOURAUD_MATERIAL][intensity]" in body
    assert "SM64_SATURN_GOURAUD_NEUTRAL" in body
    assert "texture_indices" in body
    assert "texture_ordinal" not in body
    assert "detail, (vdp1_vram_t)s_actor_gouraud_addresses[ordinal]" in body


def test_sourceboot_draws_reject_backfacing_mario_and_generic_actor_quads() -> None:
    """VDP1 has no depth buffer: sourceboot must not emit hidden back faces."""
    text = DEMO_RENDER.read_text(encoding="utf-8")
    mario_classify = function_body(text, "demo_classify_mario_range")
    mario_queue_lower = function_body(text, "demo_actor_queue_classify")
    generic_start = text.rindex("static bool demo_generic_actor_emit(")
    generic_emit = text[generic_start:text.index("#endif", generic_start)]
    for body in (mario_classify, mario_queue_lower):
        assert "if (cross <= 0)" in body
    assert "if (cross <= 0)" in generic_emit


def test_non_diagnostic_bob_build_omits_optional_job_telemetry() -> None:
    """A normal live build may not spend HWRAM code on dashboard counters."""
    body = function_body(DEMO_RENDER.read_text(encoding="utf-8"),
                         "demo_render_finalize")
    assert "#if SATURN_DIAGNOSTIC_MODE" in body
    assert "sm64_saturn_render_job_runtime_telemetry_snapshot" in body


def test_generic_actor_lowerer_reuses_the_queue_owned_meshlet_records() -> None:
    """Normal BOB must not re-admit the same generic meshlets before emit.

    Queue admission already owns a generation-bound record span in the fixed
    actor arena.  Re-running bank meshlet preparation for the dry Gouraud
    count and once again for VDP1 emission turns one normal actor observation
    into three complete meshlet walks.  The final lowerer may re-evaluate the
    source-selected pose, but it must consume that immutable queue span.
    """
    text = DEMO_RENDER.read_text(encoding="utf-8")
    process = function_body(text, "demo_generic_actor_process")
    prepare = function_body(text, "demo_generic_actor_prepare")
    emit_start = text.rindex("static bool demo_generic_actor_emit(")
    emit = text[emit_start:text.index("#endif", emit_start)]

    assert "demo_generic_actor_gouraud_upper_bound" not in text
    assert "generic_actor_gouraud_count" in process
    assert "sm64_saturn_actor_meshlets_prepare_bank(" in process
    # The S64B-v2 header seals this per-instance capacity.  Counting a
    # transient bit from every emitted primitive duplicates material decoding
    # in the HWRAM hot path solely to recreate a value the selected bank
    # already owns.
    assert "resolution.bank.gouraud_tables_per_instance" in process
    assert "SM64_SATURN_ACTOR_DRAW_FLAG_GOURAUD" not in process
    assert "resolution.bank.render_bindings_offset" not in process
    assert "demo_generic_actor_emit(transaction, false)" not in prepare
    assert "sm64_saturn_actor_meshlets_prepare_bank(" not in emit
    assert "sm64_saturn_actor_pose_evaluate(" in emit
    assert "actor_runtime->outputs[descriptor->output_offset]" in emit
    assert "result->output_count" in emit


if __name__ == "__main__":
    test_snapshot_types_have_no_pointer_fields()
    test_release_and_peer_payload_use_cache_through_accessors()
    test_ready_claim_uses_sh2_atomic_test_and_set()
    test_producer_writes_payload_through_cache_through_alias()
    test_terminal_transitions_share_the_claim_lock()
    test_complete_mario_uses_precomputed_vertex_gouraud()
    test_opaque_actor_meshlets_use_depth_bins()
    test_normal_bob_visual_repair_preserves_the_hardware_memory_floor()
    test_vdp1_painter_chain_uses_all_existing_master_depth_tags()
    test_mario_textured_path_uses_fixed_gouraud_tables()
    test_sourceboot_draws_reject_backfacing_mario_and_generic_actor_quads()
    test_non_diagnostic_bob_build_omits_optional_job_telemetry()
    test_generic_actor_lowerer_reuses_the_queue_owned_meshlet_records()
