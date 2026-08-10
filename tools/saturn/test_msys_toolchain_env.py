import os
import re
import subprocess
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
        self.assertIn(
            "$prefixParts = @($yaulBin, $mingwBin, $usrBin) | Where-Object { $_ }",
            source,
        )
        self.assertIn("Select-Object -Unique", source)

    def test_host_compiler_gates_cannot_bypass_environment_prefix(self) -> None:
        makefile = (ROOT / "Makefile.saturn.mk").read_text()
        direct = re.findall(r"^\s*\$\(HOST_CC\)\s", makefile, re.MULTILINE)
        self.assertEqual(direct, [])

    @unittest.skipUnless(os.name == "nt", "PowerShell wrapper is Windows-only")
    def test_mingw32_make_alias_selects_grouped_target_capable_msys_make(self) -> None:
        result = subprocess.run(
            [
                "powershell",
                "-ExecutionPolicy",
                "Bypass",
                "-File",
                str(ROOT / "tools/saturn/with-msys-toolchain.ps1"),
                "mingw32-make",
                "--version",
            ],
            cwd=ROOT,
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        match = re.search(r"GNU Make (\d+)\.(\d+)", result.stdout)
        self.assertIsNotNone(match, result.stdout)
        version = tuple(int(part) for part in match.groups())
        self.assertGreaterEqual(version, (4, 3), result.stdout)


if __name__ == "__main__":
    unittest.main()
