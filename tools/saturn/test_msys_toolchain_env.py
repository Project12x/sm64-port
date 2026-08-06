import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


class MsysToolchainEnvironmentTests(unittest.TestCase):
    def test_wrapper_preflights_transitive_compiler_runtime_and_path_order(self) -> None:
        source = (ROOT / "tools/saturn/with-msys-toolchain.ps1").read_text()
        for dll in (
            "msys-2.0.dll",
            "msys-gcc_s-seh-1.dll",
            "libgmp-10.dll",
            "libmpfr-6.dll",
            "libisl-23.dll",
            "libwinpthread-1.dll",
            "libgcc_s_seh-1.dll",
            "zlib1.dll",
            "libiconv-2.dll",
            "libintl-8.dll",
        ):
            self.assertIn(dll, source)
        self.assertLess(source.index("$mingwBin"), source.index("$usrBin"))
        self.assertIn("Select-Object -Unique", source)

    def test_host_compiler_gates_cannot_bypass_environment_prefix(self) -> None:
        makefile = (ROOT / "Makefile.saturn.mk").read_text()
        direct = re.findall(r"^\s*\$\(HOST_CC\)\s", makefile, re.MULTILINE)
        self.assertEqual(direct, [])


if __name__ == "__main__":
    unittest.main()
