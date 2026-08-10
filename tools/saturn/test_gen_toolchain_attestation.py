#!/usr/bin/env python3
"""Behavioral contract for Saturn release toolchain attestation."""

from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from hermetic_manifest import canonical_json_bytes  # noqa: E402
from gen_toolchain_attestation import (  # noqa: E402
    ToolchainComponent,
    build_toolchain_attestation,
    verify_toolchain_attestation,
    write_toolchain_attestation,
)


class ToolchainAttestationTests(unittest.TestCase):
    """A byte or ownership change must never silently reuse an attestation."""

    binary_names = (
        "sh-elf-gcc.exe", "sh-elf-as.exe", "sh-elf-ld.exe", "sh-elf-nm.exe",
        "sh-elf-objcopy.exe", "sh-elf-objdump.exe", "sh-elf-readelf.exe",
        "sh-elf-addr2line.exe",
    )

    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.install = self.root / "install"
        self._populate(self.install)

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _populate(self, root: Path) -> None:
        for name in self.binary_names:
            self.write(root / "bin" / name, f"binary:{name}\n".encode("ascii"))
        self.write(root / "sh-elf/include/stdint.h", b"#define UINT8_MAX 255\n")

    @staticmethod
    def write(path: Path, data: bytes) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def component(self, root: Path | None = None, *, component_id: str = "yaul-sh-sdk",
                  binaries: tuple[Path, ...] | None = None) -> ToolchainComponent:
        root = root or self.install
        return ToolchainComponent(
            component_id=component_id,
            version="yaul-0.3.1 commit-6012f79f237773378c8014e70d8998ad95a38d98; gcc-version",
            root=root,
            binaries=binaries or tuple(root / "bin" / name for name in self.binary_names),
        )

    def dependencies(self, root: Path | None = None) -> tuple[Path, ...]:
        root = root or self.install
        return (root / "sh-elf/include/stdint.h",)

    def build(self):
        return build_toolchain_attestation([self.component()], self.dependencies())

    def test_install_root_does_not_enter_canonical_attestation(self) -> None:
        first_root = self.root / "install-a"
        second_root = self.root / "elsewhere/install-b"
        self._populate(first_root)
        self._populate(second_root)
        first = build_toolchain_attestation([self.component(first_root)], self.dependencies(first_root))
        second = build_toolchain_attestation([self.component(second_root)], self.dependencies(second_root))
        self.assertEqual(first.canonical, second.canonical)
        self.assertNotIn(str(first_root).encode(), first.canonical)

    def test_binary_or_external_header_change_reseals(self) -> None:
        first = self.build()
        for relative in ("bin/sh-elf-gcc.exe", "sh-elf/include/stdint.h"):
            with self.subTest(relative=relative):
                path = self.install / relative
                original = path.read_bytes()
                path.write_bytes(original + b"changed\n")
                self.assertNotEqual(first.sha256, self.build().sha256)
                path.write_bytes(original)

    def test_unclassified_or_ambiguous_external_dependency_fails(self) -> None:
        outside = self.root / "outside/header.h"
        self.write(outside, b"outside\n")
        with self.assertRaisesRegex(ValueError, "unclassified external dependency"):
            build_toolchain_attestation([self.component()], [outside])

        nested = self.install / "nested"
        self._populate(nested)
        shared_header = nested / "sh-elf/include/stdint.h"
        with self.assertRaisesRegex(ValueError, "matches multiple toolchain components"):
            build_toolchain_attestation(
                [self.component(), self.component(nested, component_id="nested-sdk")], [shared_header]
            )

    def test_rejects_duplicate_missing_and_case_colliding_component_inputs(self) -> None:
        good = self.install / "bin/sh-elf-gcc.exe"
        missing = self.install / "bin/missing.exe"
        cases = (
            ([self.component(), self.component(component_id="yaul-sh-sdk")], (), "duplicate component ids"),
            ([self.component(binaries=(good, good))], (), "duplicate binary records"),
            ([self.component(binaries=(missing,))], (), "toolchain binary is not a file"),
            ([self.component(binaries=(good, self.install / "BIN/SH-ELF-GCC.EXE"))], (),
             "case-colliding binary paths"),
        )
        for components, dependencies, message in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    build_toolchain_attestation(components, dependencies)

    def test_verification_rejects_stale_bytes_unknown_keys_and_invalid_hashes(self) -> None:
        sealed = self.root / "toolchain.json"
        built = self.build()
        sealed.write_bytes(built.canonical)
        self.assertEqual(verify_toolchain_attestation(sealed, [self.component()], self.dependencies()),
                         built.document)

        (self.install / "sh-elf/include/stdint.h").write_bytes(b"changed\n")
        with self.assertRaisesRegex(ValueError, "differs from current inputs"):
            verify_toolchain_attestation(sealed, [self.component()], self.dependencies())
        (self.install / "sh-elf/include/stdint.h").write_bytes(b"#define UINT8_MAX 255\n")

        sealed.write_bytes(canonical_json_bytes({"unknown": True}))
        with self.assertRaisesRegex(ValueError, "keys invalid"):
            verify_toolchain_attestation(sealed, [self.component()], self.dependencies())

        invalid = json.loads(built.canonical)
        invalid["components"][0]["binaries"][0]["sha256"] = "A" * 64
        sealed.write_bytes(canonical_json_bytes(invalid))
        with self.assertRaisesRegex(ValueError, "record is invalid"):
            verify_toolchain_attestation(sealed, [self.component()], self.dependencies())

    def test_invalid_inputs_do_not_replace_a_prior_attestation(self) -> None:
        output = self.root / "toolchain.json"
        output.write_bytes(b"known-valid-prior-output\n")
        outside = self.root / "outside/header.h"
        self.write(outside, b"outside\n")
        with self.assertRaisesRegex(ValueError, "unclassified external dependency"):
            write_toolchain_attestation(output, [self.component()], [outside])
        self.assertEqual(output.read_bytes(), b"known-valid-prior-output\n")


if __name__ == "__main__":
    unittest.main()
