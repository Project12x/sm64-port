from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class Vdp1FrameBankSourceTests(unittest.TestCase):
    def test_stale_publication_is_quarantined_before_assignment(self) -> None:
        source = (ROOT / "src/port/saturn/gfx/saturn_vdp1_frame_bank.c").read_text()
        publish = source[source.index("bool sm64_saturn_vdp1_frame_bank_publish("):]
        stale = publish.index("!generation_follows(")
        quarantine = publish.index("SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED")
        assign = publish.index("banks->published = bank;")
        self.assertLess(stale, quarantine)
        self.assertLess(quarantine, assign)

    def test_both_emitters_fail_closed_and_main_propagates_normal_result(self) -> None:
        demo = (ROOT / "src/port/saturn/gfx/saturn_demo_render.c").read_text()
        normal = (ROOT / "src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c").read_text()
        header = (ROOT / "src/port/saturn/gfx/saturn_fast3d_vdp1_emit.h").read_text()
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text()
        for source in (demo, normal):
            self.assertIn("!sm64_saturn_gouraud_transfer_submit(", source)
            failure = source.index("!sm64_saturn_gouraud_transfer_submit(")
            upload = source.index("sm64_saturn_vdp1_backend_upload", failure)
            self.assertIn("return false;", source[failure:upload])
        self.assertIn("bool sm64_saturn_fast3d_vdp1_emit(", header)
        self.assertIn(
            "render_complete = sm64_saturn_fast3d_vdp1_emit(", main
        )


if __name__ == "__main__":
    unittest.main()
