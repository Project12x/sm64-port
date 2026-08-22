#!/usr/bin/env python3
"""Host contracts for the shared ymir-headless path resolver."""

from __future__ import annotations

import argparse
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

import ymir_binary


def _plant(root: Path, build_dir: str) -> Path:
    binary = root / "ymir-agent" / build_dir / Path(*ymir_binary.YMIR_SUFFIX)
    binary.parent.mkdir(parents=True)
    binary.write_bytes(b"fixture")
    return binary


class YmirResolutionTests(unittest.TestCase):
    """The default must be the supported build, found from any tree depth."""

    def setUp(self) -> None:
        self._env = mock.patch.dict("os.environ", clear=False)
        self._env.start()
        import os

        os.environ.pop(ymir_binary.YMIR_ENV_VAR, None)

    def tearDown(self) -> None:
        self._env.stop()

    def test_resolves_the_supported_build_by_upward_search(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = _plant(root, ymir_binary.YMIR_BUILD_DIR)
            nested = root / "sm64-port" / ".worktrees" / "x" / "tools" / "saturn"
            nested.mkdir(parents=True)
            self.assertEqual(ymir_binary.find_ymir_headless(nested), binary)

    def test_never_resolves_a_superseded_build_directory(self) -> None:
        """The build-agent binary ignores --dram-cart silently; it must not
        be reachable through the default, only through an explicit path."""
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in ymir_binary.SUPERSEDED_BUILD_DIRS:
                _plant(root, name)
            nested = root / "tools" / "saturn"
            nested.mkdir(parents=True)
            self.assertIsNone(ymir_binary.find_ymir_headless(nested))

    def test_missing_binary_message_names_the_superseded_trap(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _plant(root, ymir_binary.SUPERSEDED_BUILD_DIRS[0])
            nested = root / "tools" / "saturn"
            nested.mkdir(parents=True)
            message = ymir_binary.describe_missing_ymir(nested)
            self.assertIn("SUPERSEDED", message)
            self.assertIn("--dram-cart", message)
            self.assertIn(ymir_binary.YMIR_BUILD_DIR, message)

    def test_env_override_wins_and_a_dangling_override_is_not_ignored(self) -> None:
        import os

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            planted = _plant(root, ymir_binary.YMIR_BUILD_DIR)
            override = root / "custom-ymir.exe"
            override.write_bytes(b"fixture")
            os.environ[ymir_binary.YMIR_ENV_VAR] = str(override)
            self.assertEqual(ymir_binary.find_ymir_headless(root), override)
            # A set-but-dangling override must fail, not silently fall back
            # to a binary the user explicitly steered away from.
            os.environ[ymir_binary.YMIR_ENV_VAR] = str(root / "gone.exe")
            self.assertIsNone(ymir_binary.find_ymir_headless(root))
            self.assertIn(ymir_binary.YMIR_ENV_VAR, ymir_binary.describe_missing_ymir(root))
            del planted

    def test_explicit_path_beats_the_default_in_resolve(self) -> None:
        explicit = Path("C:/anywhere/ymir-headless.exe")
        self.assertEqual(ymir_binary.resolve_ymir_headless(explicit), explicit)

    def test_resolve_raises_with_guidance_when_nothing_is_found(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError) as caught:
                ymir_binary.resolve_ymir_headless(None, start=Path(directory))
            self.assertIn("--ymir", str(caught.exception))

    def test_add_ymir_argument_defaults_when_found_and_requires_when_not(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            binary = _plant(root, ymir_binary.YMIR_BUILD_DIR)
            with mock.patch.object(
                ymir_binary, "default_ymir_headless", return_value=binary
            ):
                parser = argparse.ArgumentParser()
                ymir_binary.add_ymir_argument(parser)
                self.assertEqual(parser.parse_args([]).ymir, binary)
            with mock.patch.object(
                ymir_binary, "default_ymir_headless", return_value=None
            ):
                parser = argparse.ArgumentParser()
                ymir_binary.add_ymir_argument(parser)
                with self.assertRaises(SystemExit):
                    parser.parse_args([])

    def test_the_real_tree_resolves_to_the_supported_binary(self) -> None:
        """Environment-coupled by design: this repo ships the binary, and the
        whole point of the module is that this exact path is the default."""
        found = ymir_binary.find_ymir_headless()
        self.assertIsNotNone(found)
        self.assertIn(ymir_binary.YMIR_BUILD_DIR, found.parts)
        self.assertTrue(found.is_file())


if __name__ == "__main__":
    raise SystemExit(unittest.main())
