import importlib.util
import os
import pathlib
import tempfile
import unittest


MODULE_PATH = pathlib.Path(__file__).with_name("verify_audio68k_modules.py")
SPEC = importlib.util.spec_from_file_location("verify_audio68k_modules", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
VERIFY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(VERIFY)


class VerifyAudio68kModulesTest(unittest.TestCase):
    def test_sha256_file_is_byte_exact(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / "tool.exe"
            path.write_bytes(b"pinned-tool")
            self.assertEqual(
                VERIFY.sha256_file(path),
                "4abdad978ee29b2ce6cc391ab501d4a250a871fc9f917527ba0016581f1e4189",
            )

    def test_undefined_symbol_parser_rejects_any_symbol(self):
        self.assertEqual(VERIFY.undefined_symbols(""), [])
        self.assertEqual(
            VERIFY.undefined_symbols("         U memcpy\n         U __udivsi3\n"),
            ["memcpy", "__udivsi3"],
        )

    def test_object_format_requires_m68k_elf(self):
        VERIFY.require_m68k_elf(
            "task17.o:     file format elf32-m68k\narchitecture: m68k:68000"
        )
        with self.assertRaisesRegex(ValueError, "elf32-m68k"):
            VERIFY.require_m68k_elf(
                "task17.o: file format pe-x86-64\narchitecture: i386:x86-64"
            )

    def test_all_inputs_must_not_be_newer_than_artifact(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            artifact = root / "artifact.o"
            source = root / "source.o"
            artifact.write_bytes(b"artifact")
            source.write_bytes(b"source")
            artifact.touch()
            VERIFY.require_artifact_fresh(artifact, [source])
            source.touch()
            source_stat = source.stat()
            artifact_stat = artifact.stat()
            if source_stat.st_mtime_ns <= artifact_stat.st_mtime_ns:
                os.utime(source, ns=(artifact_stat.st_atime_ns,
                                     artifact_stat.st_mtime_ns + 1_000_000_000))
            with self.assertRaisesRegex(ValueError, "newer"):
                VERIFY.require_artifact_fresh(artifact, [source])


if __name__ == "__main__":
    unittest.main()
