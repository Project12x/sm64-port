#!/usr/bin/env python3
"""Source contract for the pre-cart sourceboot build identity."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
IDENTITY_C = (ROOT / "src/port/saturn/platform/saturn_build_identity.c").read_text()
LINKER = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text()


class SourcebootIdentityResidencyTests(unittest.TestCase):
    def test_identity_is_in_bootdata_before_cart_rodata(self):
        declaration = "const sm64_saturn_build_identity_t saturn_build_identity"
        declaration_start = IDENTITY_C.index(declaration)
        declaration_end = IDENTITY_C.index(";", declaration_start)
        declaration_text = IDENTITY_C[declaration_start:declaration_end]
        self.assertIn('section(".bootdata")', declaration_text)

        bootdata = LINKER.index(".bootdata")
        cart_rodata = LINKER.index(".cart_rodata")
        self.assertLess(bootdata, cart_rodata)


if __name__ == "__main__":
    unittest.main()
