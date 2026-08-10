#!/usr/bin/env python3
"""Source contract for the pre-cart sourceboot build identity."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
IDENTITY_C = (ROOT / "src/port/saturn/platform/saturn_build_identity.c").read_text()
IDENTITY_INCLUDE = ROOT / "src/port/saturn/platform"
LINKER = (ROOT / "src/port/saturn/sourceboot/sourceboot-cart.x").read_text()


class SourcebootIdentityResidencyTests(unittest.TestCase):
    def test_target_c_identity_v2_layout_is_exact(self):
        compiler = next(
            (path for name in ("cc", "gcc", "clang")
             if (path := shutil.which(name)) is not None),
            None,
        )
        self.assertIsNotNone(compiler, "a host C compiler is required for ABI checks")
        source = r'''#include <stddef.h>
#include "saturn_build_identity.h"
_Static_assert(SM64_SATURN_BUILD_IDENTITY_VERSION == 2U, "identity version");
_Static_assert(SM64_SATURN_BUILD_IDENTITY_SIZE == 500U, "identity size");
_Static_assert(sizeof(sm64_saturn_build_identity_t) == 500U, "struct size");
_Static_assert(offsetof(sm64_saturn_build_identity_t, target_profile_hash) == 404U, "profile offset");
_Static_assert(offsetof(sm64_saturn_build_identity_t, package_set_root_hash) == 436U, "package offset");
_Static_assert(offsetof(sm64_saturn_build_identity_t, toolchain_attestation_hash) == 468U, "toolchain offset");
'''
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            contract = root / "identity_layout.c"
            contract.write_text(source, encoding="ascii")
            result = subprocess.run(
                [str(compiler), "-std=c11", "-Werror", "-I", str(IDENTITY_INCLUDE),
                 "-c", str(contract), "-o", str(root / "identity_layout.o")],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

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
