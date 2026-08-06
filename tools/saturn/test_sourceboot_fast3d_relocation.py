#!/usr/bin/env python3
"""Source contract for the CPU-only sourceboot Fast3D owner."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/port/saturn/sourceboot/main.c").read_text()


class SourcebootFast3dRelocationTests(unittest.TestCase):
    def test_fast3d_state_is_lwram_owned_and_initialized_before_bootstrap_read(self):
        declaration = "static sm64_saturn_fast3d_frontend_t sourceboot_fast3d"
        self.assertIn(declaration, MAIN)
        declaration_start = MAIN.index(declaration)
        declaration_end = MAIN.index(";", declaration_start)
        declaration_text = MAIN[declaration_start:declaration_end]
        self.assertIn('section(".lwram_bss")', declaration_text)
        self.assertNotIn(".lwram_actor_runtime", declaration_text)
        self.assertNotIn("__uncached", declaration_text)

        init = "sm64_saturn_fast3d_frontend_init(&sourceboot_fast3d);"
        bootstrap_read = (
            "sm64_saturn_vdp2_frame_begin(&sourceboot_vdp2_frame, NULL,"
        )
        self.assertLess(MAIN.index(init), MAIN.index(bootstrap_read))


if __name__ == "__main__":
    unittest.main()
