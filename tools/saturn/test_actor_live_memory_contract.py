import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class ActorLiveMemoryContractTests(unittest.TestCase):
    def test_frame_resolve_does_not_repeat_full_bank_validation(self) -> None:
        source = (ROOT / "src/port/saturn/gfx/saturn_actor_bundle.c").read_text(
            encoding="utf-8"
        )
        match = re.search(
            r"bool sm64_saturn_actor_bundle_resolve\(.*?\n\}", source, re.DOTALL
        )
        self.assertIsNotNone(match)
        body = match.group(0)
        self.assertNotIn("sm64_saturn_actor_bank_validate_expected", body)
        self.assertIn("actor_bank_open_validated", body)

    def test_boot_only_validation_code_is_cart_resident(self) -> None:
        linker = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text(
            encoding="utf-8"
        )
        self.assertIn("*(.cart_cold_text)", linker)
        self.assertLess(
            linker.index("*(.cart_cold_text)"),
            linker.index("___sourceboot_cart_rodata_end"),
        )
        self.assertIn("__sourceboot_required_hwram_margin = 0x1F00", linker)
        self.assertIn("1 KiB growth reserve", linker)

        required = {
            "src/port/saturn/gfx/saturn_actor_bank.c": (
                "sm64_saturn_actor_bank_validate_expected",
            ),
            "src/port/saturn/gfx/saturn_actor_bundle.c": (
                "sm64_saturn_actor_bundle_validate",
            ),
            "src/port/saturn/runtime/saturn_scene_package.c": (
                "sm64_saturn_scene_package_validate",
            ),
            "src/port/saturn/runtime/saturn_sha256.c": (
                "sm64_saturn_sha256_update",
                "sm64_saturn_sha256_finish",
            ),
        }
        for relative, functions in required.items():
            text = (ROOT / relative).read_text(encoding="utf-8")
            for function in functions:
                with self.subTest(function=function):
                    self.assertRegex(
                        text,
                        rf"SM64_SATURN_CART_COLD[\s\n]+(?:bool|void)[\s\n]+{function}\(",
                    )

    def test_cart_is_loaded_before_cold_scene_validation(self) -> None:
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(
            encoding="utf-8"
        )
        self.assertLess(
            main.index("sm64_saturn_source_cart_load()"),
            main.index("sm64_saturn_source_scene_bundle_init("),
        )

    def test_post_cart_bootstrap_executes_from_cart(self) -> None:
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(
            encoding="utf-8"
        )
        self.assertRegex(
            main,
            r"static SM64_SATURN_CART_COLD void\s+sourceboot_post_cart_init\(void\)",
        )
        load = main.index("sm64_saturn_source_cart_load()")
        call = main.index("sourceboot_post_cart_init();")
        self.assertLess(load, call)

        cold_initializers = {
            "src/port/saturn/gfx/saturn_demo_render.c": (
                "sm64_saturn_demo_render_init",
            ),
            "src/port/saturn/gfx/saturn_hud_atlas.c": (
                "sm64_saturn_hud_atlas_init",
            ),
            "src/port/saturn/gfx/saturn_vdp1_frame_bank.c": (
                "sm64_saturn_vdp1_frame_bank_set_init",
            ),
            "src/port/saturn/gfx/saturn_render_job_queue.c": (
                "sm64_saturn_render_job_queue_init",
            ),
            "src/port/saturn/gfx/saturn_render_job_graph.c": (
                "sm64_saturn_render_job_graph_init",
            ),
            "src/port/saturn/gfx/saturn_render_payload_bank.c": (
                "sm64_saturn_render_payload_bank_init",
            ),
            "src/port/saturn/gfx/saturn_lod_lifetime.c": (
                "sm64_saturn_lod_lifetime_init",
            ),
            "src/port/saturn/gpl/ztreme_hot_promotion.c": (
                "saturn_hot_promotion_init",
            ),
            "src/port/saturn/gfx/saturn_fast3d_frontend.c": (
                "sm64_saturn_fast3d_frontend_init",
            ),
            "src/port/saturn/gfx/saturn_render_snapshot.c": (
                "sm64_saturn_render_snapshot_reset",
            ),
            "src/port/saturn/gfx/saturn_geo_state_observer.c": (
                "sm64_saturn_geo_state_observer_init",
            ),
            "src/port/saturn/gfx/saturn_actor_instance.c": (
                "sm64_saturn_actor_instance_bank_init",
                "sm64_saturn_actor_instances_set_observer",
            ),
            "src/port/saturn/gfx/saturn_hud_publish.c": (
                "sm64_saturn_hud_publish_init",
            ),
            "src/port/saturn/gfx/saturn_vdp2_frame.c": (
                "sm64_saturn_vdp2_frame_init",
            ),
            "src/port/saturn/gpl/slavedriver_dma_queue.c": (
                "saturn_dma_queue_init",
            ),
            "src/port/saturn/runtime/saturn_frame_pipeline.c": (
                "sm64_saturn_frame_pipeline_init",
            ),
        }
        for relative, functions in cold_initializers.items():
            text = (ROOT / relative).read_text(encoding="utf-8")
            for function in functions:
                with self.subTest(function=function):
                    self.assertRegex(
                        text,
                        rf"SM64_SATURN_CART_COLD[\s\n]+(?:bool|void)[\s\n]+{function}\(",
                    )

    def test_level_transition_code_is_cart_resident(self) -> None:
        cold_transition_functions = {
            "src/game/game_init.c": (
                "init_controllers",
                "setup_game_memory",
                "thread5_game_loop",
            ),
            "src/game/camera.c": ("reset_camera", "init_camera"),
            "src/game/level_update.c": ("init_mario_after_warp", "init_level"),
            "src/game/mario.c": ("init_mario",),
            "src/game/save_file.c": ("save_file_load_all",),
            "src/engine/surface_load.c": ("load_area_terrain",),
            "src/game/area.c": (
                "get_mario_spawn_type",
                "area_get_warp_node",
                "area_get_warp_node_from_params",
                "load_obj_warp_nodes",
                "clear_areas",
                "clear_area_graph_nodes",
                "load_area",
                "unload_area",
                "load_mario_area",
                "unload_mario_area",
                "change_area",
            ),
            "src/game/object_list_processor.c": (
                "unload_objects_from_area",
                "spawn_objects_from_info",
                "clear_objects",
            ),
        }
        for relative, functions in cold_transition_functions.items():
            text = (ROOT / relative).read_text(encoding="utf-8")
            for function in functions:
                with self.subTest(function=function):
                    self.assertRegex(
                        text,
                        rf"SM64_SATURN_CART_COLD[\s\n]+(?:s32|u32|void|struct ObjectWarpNode \*)[\s\n]*{function}\(",
                    )

    def test_source_runtime_boot_configuration_is_cart_resident(self) -> None:
        source = (
            ROOT / "src/port/saturn/runtime/saturn_source_runtime.c"
        ).read_text(encoding="utf-8")
        for function in (
            "sm64_saturn_source_runtime_configure",
            "sm64_saturn_source_runtime_configure_input_replay",
            "sm64_saturn_source_runtime_init_controllers",
        ):
            with self.subTest(function=function):
                self.assertRegex(
                    source,
                    rf"SM64_SATURN_CART_COLD[\s\n]+void[\s\n]+{function}\(",
                )

    def test_actor_workspace_overlays_completed_mario_phase(self) -> None:
        renderer = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text(
            encoding="utf-8"
        )
        scene = (ROOT / "src/port/saturn/sourceboot/source_scene_bundle.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("union demo_actor_phase_workspace", renderer)
        self.assertIn("source_scene_workspace", renderer)
        self.assertIn("sm64_saturn_demo_render_actor_workspace", renderer)
        self.assertNotIn("source_scene_scratch SOURCE_SCENE_LWRAM", scene)
        self.assertIn("source_scene_workspace_get", scene)
        self.assertIn('section(".uncached")', scene)


if __name__ == "__main__":
    unittest.main()
