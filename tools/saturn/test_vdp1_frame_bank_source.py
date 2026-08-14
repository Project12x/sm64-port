from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class Vdp1FrameBankSourceTests(unittest.TestCase):
    def test_mario_textured_detail_retains_a9a_direct_color_contract(self) -> None:
        """Mario keeps the accepted per-material Gouraud and direct tile path."""
        source = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text()
        start = source.index("static void __attribute__((unused)) demo_emit_mario_range(")
        end = source.index("#if SATURN_SLAVE_RENDER", start)
        mario_emit = source[start:end]

        # A9A shaded the solid material command from its own source RGB and
        # emitted the RGB1555 tile as direct colour.  Letting the generic
        # neutral actor table modulate the tile changes Mario's source colours.
        self.assertIn(
            "const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];",
            mario_emit,
        )
        self.assertIn(
            "const uint8_t r = (uint8_t)((rgb[0] * intensity) / 31U);",
            mario_emit,
        )
        self.assertIn(
            "const uint8_t g = (uint8_t)((rgb[1] * intensity) / 31U);",
            mario_emit,
        )
        self.assertIn(
            "const uint8_t b = (uint8_t)((rgb[2] * intensity) / 31U);",
            mario_emit,
        )
        self.assertIn("VDP1_CMDT_CC_REPLACE, texture_vertices", mario_emit)
        self.assertNotIn("SM64_SATURN_MARIO_TEXTURE_GOURAUD_MATERIAL", mario_emit)
        self.assertIn(
            "if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE &&\n"
            "            context->partitions != NULL)",
            mario_emit,
        )

    def test_command_source_contract_matches_cpu_dmac_hardware_path(self) -> None:
        source = (ROOT / "src/port/saturn/gfx/saturn_vdp1_frame_bank.c").read_text()
        self.assertIn(
            "bool sm64_saturn_vdp1_frame_bank_command_source_is_cpu_dmac(",
            source,
        )
        set_init = source[source.index("bool sm64_saturn_vdp1_frame_bank_set_init("):]
        submit = source[source.index("bool sm64_saturn_vdp1_frame_bank_submit_transfers("):]
        self.assertIn(
            "sm64_saturn_vdp1_frame_bank_command_source_is_cpu_dmac(",
            set_init,
        )
        self.assertIn(
            "sm64_saturn_vdp1_frame_bank_command_source_is_cpu_dmac(",
            submit,
        )

    def test_stale_publication_is_quarantined_before_assignment(self) -> None:
        source = (ROOT / "src/port/saturn/gfx/saturn_vdp1_frame_bank.c").read_text()
        publish = source[source.index("bool sm64_saturn_vdp1_frame_bank_publish("):]
        stale = publish.index("!generation_follows(")
        quarantine = publish.index("SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED")
        assign = publish.index("banks->published = bank;")
        self.assertLess(stale, quarantine)
        self.assertLess(quarantine, assign)

    def test_both_emitters_are_construction_only_and_main_owns_transport(self) -> None:
        demo = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text()
        normal = (ROOT / "src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c").read_text()
        header = (ROOT / "src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h").read_text()
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text()
        for source in (demo, normal):
            self.assertNotIn("sm64_saturn_gouraud_transfer_submit(", source)
            self.assertNotIn("sm64_saturn_vdp1_backend_upload", source)
            self.assertIn("sm64_saturn_vdp1_backend_finish(backend);", source)
        self.assertIn("bool sm64_saturn_fast3d_vdp1_emit(", header)
        self.assertIn(
            "render_complete = sm64_saturn_fast3d_vdp1_emit(", main
        )
        self.assertEqual(
            main.count("sm64_saturn_vdp1_frame_bank_submit_transfers("), 1
        )
        self.assertEqual(main.count("vdp1_sync_force_put();"), 1)


if __name__ == "__main__":
    unittest.main()
