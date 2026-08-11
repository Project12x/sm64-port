#!/usr/bin/env python3
"""Focused contracts for candidate-local SM64 asset extraction."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
import importlib.util
import os
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("extract_assets", ROOT / "extract_assets.py")
assert SPEC is not None and SPEC.loader is not None
EXTRACT_ASSETS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(EXTRACT_ASSETS)


class ExtractAssetsOutputRootTests(unittest.TestCase):
    def test_path_list_is_canonical_and_contains_generated_us_assets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_root = Path(temporary) / "generated"
            first = output_root / "levels" / "bob" / "0.rgba16.png"
            second = output_root / "textures" / "skyboxes" / "water.png"
            for path in (first, second):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(path.name.encode("ascii"))
            (output_root / ".assets-local.txt").write_text(
                "# This file tracks the assets currently extracted by extract_assets.py.\n7\n",
                encoding="utf-8",
            )
            manifest = Path(temporary) / "assets-v1.txt"
            asset_map = {
                "textures/skyboxes/water.png": [1, {"us": [1, 0]}],
                "levels/bob/0.rgba16.png": [1, 1, {"us": [2, 0]}],
                "levels/japanese-only.png": [1, 1, {"jp": [3, 0]}],
            }

            EXTRACT_ASSETS.write_path_list(manifest, output_root, asset_map, ["us"])

            rows = manifest.read_text(encoding="utf-8").splitlines()
            self.assertEqual(rows[0], "sm64-saturn-path-list-v1")
            self.assertEqual(rows[1:], sorted(rows[1:], key=lambda row: row.encode("utf-8")))
            self.assertEqual(len(rows[1:]), 3)
            self.assertIn(first.resolve().as_posix(), rows)
            self.assertIn(second.resolve().as_posix(), rows)

    def test_clean_is_scoped_to_explicit_output_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            output_root = Path(temporary) / "generated"
            generated = output_root / "levels" / "bob" / "0.rgba16.png"
            generated.parent.mkdir(parents=True)
            generated.write_bytes(b"generated")
            (output_root / ".assets-local.txt").write_text(
                "# This file tracks the assets currently extracted by extract_assets.py.\n"
                "7\nlevels/bob/0.rgba16.png\n",
                encoding="utf-8",
            )

            source_sentinel = ROOT / "levels" / "bob" / "0.rgba16.png"
            source_existed = source_sentinel.exists()
            result = subprocess.run(
                [
                    str(ROOT / ".venv-saturn-tools" / "Scripts" / "python.exe"),
                    str(ROOT / "extract_assets.py"),
                    "--output-root",
                    str(output_root),
                    "--clean",
                ],
                cwd=ROOT,
                check=False,
                capture_output=True,
                text=True,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertFalse(generated.exists())
            self.assertFalse((output_root / ".assets-local.txt").exists())
            self.assertEqual(source_sentinel.exists(), source_existed)

    def test_clean_rejects_absolute_traversal_and_nul_rows_before_deleting(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            output_root = base / "generated"
            sentinel = output_root / "levels" / "bob" / "keep.png"
            sentinel.parent.mkdir(parents=True)
            sentinel.write_bytes(b"keep")
            outside = base / "outside.png"
            outside.write_bytes(b"outside")
            for row in ("../outside.png", str(outside.resolve()), "bad\x00name"):
                with self.subTest(row=row):
                    manifest = output_root / ".assets-local.txt"
                    manifest.write_text(
                        "# This file tracks the assets currently extracted by extract_assets.py.\n"
                        f"7\nlevels/bob/keep.png\n{row}\n",
                        encoding="utf-8",
                    )
                    with self.assertRaisesRegex(ValueError, "clean asset path"):
                        EXTRACT_ASSETS.clean_assets(manifest.open(), output_root)
                    self.assertTrue(sentinel.is_file())
                    self.assertEqual(outside.read_bytes(), b"outside")

    @unittest.skipUnless(hasattr(os, "symlink"), "symlinks are unsupported")
    def test_clean_rejects_symlink_ancestor_without_touching_outside_file(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            output_root = base / "generated"
            outside = base / "outside"
            outside.mkdir()
            victim = outside / "victim.png"
            victim.write_bytes(b"outside")
            output_root.mkdir()
            link = output_root / "escape"
            try:
                link.symlink_to(outside, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"symlink creation unavailable: {error}")
            manifest = output_root / ".assets-local.txt"
            manifest.write_text(
                "# This file tracks the assets currently extracted by extract_assets.py.\n"
                "7\nescape/victim.png\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "symlink|reparse|escapes"):
                EXTRACT_ASSETS.clean_assets(manifest.open(), output_root)
            self.assertEqual(victim.read_bytes(), b"outside")

    def test_remove_file_prunes_only_empty_descendants_and_preserves_root_and_parent(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            output_root = base / "generated"
            target = output_root / "levels" / "bob" / "asset.png"
            target.parent.mkdir(parents=True)
            target.write_bytes(b"asset")

            EXTRACT_ASSETS.remove_file("levels/bob/asset.png", output_root)

            self.assertFalse(target.exists())
            self.assertFalse((output_root / "levels").exists())
            self.assertTrue(output_root.is_dir())
            self.assertTrue(base.is_dir())


if __name__ == "__main__":
    unittest.main()
