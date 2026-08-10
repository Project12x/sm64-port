#!/usr/bin/env python3
"""Release-binding contracts for automated sourceboot HUD evidence."""

from __future__ import annotations

import sys
import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import capture_sourceboot_hud_state as capture


class HudReleaseBindingTests(unittest.TestCase):
    def test_cli_requires_release_manifest_for_new_evidence(self) -> None:
        with self.assertRaises(SystemExit) as caught:
            capture.main([])
        self.assertEqual(caught.exception.code, 2)

    def test_cli_report_records_verified_manifest_and_elf(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ymir, ipl, game, manifest = (
                root / "ymir.exe", root / "ipl.bin", root / "game.cue",
                root / "release.json",
            )
            for path in (ymir, ipl, game, manifest):
                path.write_bytes(b"fixture")
            output = root / "hud.json"

            class Client:
                def call(self, method: str, _params: dict[str, int] | None = None) -> dict[str, object]:
                    if method == "mem.peek":
                        expected = capture.expected_character_number(12)
                        return {"data": list(expected.to_bytes(2, "big"))}
                    if method == "video.capture":
                        return {}
                    return {}

                def shutdown(self) -> None:
                    pass

                def abort(self) -> None:
                    pass

            binding = {
                "elf": root / "game.elf",
                "elf_sha256": "b" * 64,
                "release_manifest_sha256": "a" * 64,
            }
            with (
                mock.patch.object(capture, "resolve_release_binding", return_value=binding),
                mock.patch.object(capture, "YmirClient", return_value=Client()),
                mock.patch.object(capture, "save_screenshot", return_value={}),
            ):
                result = capture.main([
                    "--ymir", str(ymir), "--ipl", str(ipl), "--game", str(game),
                    "--release-manifest", str(manifest), "--expect-glyph-index", "12",
                    "--output", str(output),
                ])
            report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(result, 0)
        self.assertEqual(report["release_manifest_sha256"], "a" * 64)
        self.assertEqual(report["elf"]["sha256"], "b" * 64)

    def test_manifest_selects_elf_and_requires_the_exact_game_cue(self) -> None:
        resolver = getattr(capture, "resolve_release_binding", None)
        self.assertTrue(callable(resolver), "HUD capture must verify its release manifest")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            game = root / "game.cue"
            elf = root / "game.elf"
            game.write_bytes(b"cue")
            elf.write_bytes(b"elf")
            verified = SimpleNamespace(
                manifest_sha256="a" * 64,
                document={"outputs": {"elf": {"sha256": "b" * 64}}},
                outputs={"cue": game.resolve(), "elf": elf.resolve()},
            )
            with mock.patch.object(capture, "verify_release_manifest", return_value=verified):
                binding = resolver(root / "release.json", game)
                self.assertEqual(binding["elf"], elf.resolve())
                self.assertEqual(binding["release_manifest_sha256"], "a" * 64)
                other = root / "other.cue"
                other.write_bytes(b"cue")
                with self.assertRaisesRegex(ValueError, "game CUE differs"):
                    resolver(root / "release.json", other)


if __name__ == "__main__":
    unittest.main()
