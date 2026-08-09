#!/usr/bin/env python3
"""RED/GREEN tests for the standalone sequence-bank generator.

All fixtures are synthetic: fake "assembled" seq00 blobs and fake .m64 byte
patterns constructed in temp dirs.  No Nintendo sequence bytes are committed.
The layout assertions encode the real `assemble_sound.py --sequences`
byte layout for a big-endian 32-bit target (the Saturn consumer):

  u16 magic (3 = TYPE_SEQ), u16 entry count,
  entry_count * (u32 absolute offset, u32 length) index table,
  16-byte alignment before data, per-entry garbage alignment to 16,
  final zero pad to 64.
"""
from __future__ import annotations

import hashlib
import json
import shutil
import struct
import tempfile
import unittest
from pathlib import Path

from gen_sequence_bank import (SequenceBankError, assemble_sequence_source,
                               build_sequence_bank, convert_msys_path,
                               discover_toolchain, parse_sequence_bank)

ROOT = Path(__file__).resolve().parents[2]
ASSEMBLE_SOUND = ROOT / "tools" / "assemble_sound.py"


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _make_fixture(temp: Path) -> dict[str, object]:
    """Synthetic three-sequence source tree; entry 0 is real-shaped (>1024)."""
    seq_dir = temp / "sequences"
    seq_dir.mkdir()
    banks_dir = temp / "sound_banks"
    banks_dir.mkdir()
    (banks_dir / "bank_a.json").write_text("{}\n", encoding="utf-8")
    (banks_dir / "bank_b.json").write_text("{}\n", encoding="utf-8")
    blobs = {
        "00_fake": bytes((i * 7 + 3) & 0xFF for i in range(1500)),
        "01_fake": bytes((i * 5 + 1) & 0xFF for i in range(37)),
        "02_fake": bytes((i * 11 + 9) & 0xFF for i in range(20)),
    }
    for name, blob in blobs.items():
        (seq_dir / f"{name}.m64").write_bytes(blob)
    sequences_json = temp / "sequences.json"
    sequences_json.write_text(json.dumps({
        "comment": "synthetic fixture",
        "00_fake": ["bank_a"],
        "01_fake": ["bank_a", "bank_b"],
        "02_fake": ["bank_b"],
    }), encoding="utf-8")
    return {"seq_dir": seq_dir, "banks_dir": banks_dir,
            "sequences_json": sequences_json, "blobs": blobs}


def _build(temp: Path, fixture: dict[str, object], out_name: str = "out") -> dict[str, Path]:
    out_dir = temp / out_name
    out_dir.mkdir(exist_ok=True)
    paths = {"bin": out_dir / "sequences.bin",
             "bank_sets": out_dir / "sequences.bank_sets.bin",
             "manifest": out_dir / "sequences.manifest.json"}
    build_sequence_bank(
        sequence_files=sorted(fixture["seq_dir"].glob("*.m64")),
        sequences_json=fixture["sequences_json"],
        sound_banks_dir=fixture["banks_dir"],
        output_bin=paths["bin"],
        bank_sets_out=paths["bank_sets"],
        manifest_out=paths["manifest"],
        assemble_sound_py=ASSEMBLE_SOUND)
    return paths


class SequenceBankLayoutTest(unittest.TestCase):
    def test_index_table_matches_real_layout(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            fixture = _make_fixture(temp)
            paths = _build(temp, fixture)
            raw = paths["bin"].read_bytes()
            magic, count = struct.unpack_from(">HH", raw, 0)
            self.assertEqual(magic, 3)
            self.assertEqual(count, 3)
            table = [struct.unpack_from(">II", raw, 4 + 8 * index)
                     for index in range(count)]
            blobs = fixture["blobs"]
            data_start = _align(4 + count * 8, 16)
            self.assertEqual(data_start, 32)
            expected = []
            cursor = data_start
            for name in ("00_fake", "01_fake", "02_fake"):
                length = _align(len(blobs[name]), 16)
                expected.append((cursor, length))
                cursor += length
            self.assertEqual(table, expected)
            for (offset, _length), name in zip(table, ("00_fake", "01_fake", "02_fake")):
                blob = blobs[name]
                self.assertEqual(raw[offset:offset + len(blob)], blob)
            self.assertEqual(len(raw), _align(cursor, 64))
            # parse_sequence_bank agrees with the table it validates.
            self.assertEqual(parse_sequence_bank(raw), expected)
            manifest = json.loads(paths["manifest"].read_text(encoding="utf-8"))
            self.assertEqual(manifest["entry_count"], 3)
            self.assertEqual(manifest["endian"], "big")
            self.assertEqual(manifest["word_bytes"], 4)
            self.assertEqual([entry["id"] for entry in manifest["sequences"]], [0, 1, 2])
            self.assertEqual([entry["name"] for entry in manifest["sequences"]],
                             ["00_fake", "01_fake", "02_fake"])
            for entry, (offset, length) in zip(manifest["sequences"], expected):
                self.assertEqual(entry["offset"], offset)
                self.assertEqual(entry["bytes"], length)
                self.assertEqual(entry["sha256"], _sha(raw[offset:offset + length]))
            self.assertEqual(manifest["bin"]["sha256"], _sha(raw))
            self.assertEqual(manifest["bin"]["bytes"], len(raw))

    def test_byte_stability_across_two_runs(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            fixture = _make_fixture(temp)
            first = _build(temp, fixture, "one")
            second = _build(temp, fixture, "two")
            self.assertEqual(_sha(first["bin"].read_bytes()),
                             _sha(second["bin"].read_bytes()))
            self.assertEqual(_sha(first["bank_sets"].read_bytes()),
                             _sha(second["bank_sets"].read_bytes()))

    def test_output_exceeds_packager_guard_threshold(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            fixture = _make_fixture(temp)
            paths = _build(temp, fixture)
            raw = paths["bin"].read_bytes()
            self.assertGreater(len(raw), 1024)
            table = parse_sequence_bank(raw)
            self.assertGreater(table[0][1], 1024)

    def test_missing_sequence_input_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            fixture = _make_fixture(temp)
            missing = fixture["seq_dir"] / "01_fake.m64"
            missing.unlink()
            files = sorted(fixture["seq_dir"].glob("*.m64")) + [missing]
            with self.assertRaises(SequenceBankError) as caught:
                build_sequence_bank(
                    sequence_files=files,
                    sequences_json=fixture["sequences_json"],
                    sound_banks_dir=fixture["banks_dir"],
                    output_bin=temp / "x" / "sequences.bin",
                    bank_sets_out=temp / "x" / "sequences.bank_sets.bin",
                    manifest_out=temp / "x" / "sequences.manifest.json",
                    assemble_sound_py=ASSEMBLE_SOUND)
            self.assertIn("01_fake.m64", str(caught.exception))

    def test_missing_sequences_json_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            fixture = _make_fixture(temp)
            fixture["sequences_json"].unlink()
            with self.assertRaises(SequenceBankError) as caught:
                _build(temp, fixture)
            self.assertIn("sequences.json", str(caught.exception))


class ToolchainDiscoveryTest(unittest.TestCase):
    def test_missing_toolchain_fails_closed(self) -> None:
        with self.assertRaises(SequenceBankError) as caught:
            discover_toolchain(env={}, which=lambda name: None)
        message = str(caught.exception)
        self.assertIn("YAUL_INSTALL_ROOT", message)
        self.assertIn("--toolchain-bin", message)

    def test_env_discovery_finds_pinned_binaries(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            bin_dir = temp / "bin"
            bin_dir.mkdir()
            for tool in ("cpp", "as", "objcopy"):
                (bin_dir / f"sh-elf-{tool}.exe").write_bytes(b"")
            tools = discover_toolchain(env={"YAUL_INSTALL_ROOT": str(temp)},
                                       which=lambda name: None)
            for tool in ("cpp", "as", "objcopy"):
                self.assertTrue(Path(tools[tool]).name.startswith(f"sh-elf-{tool}"))
                self.assertTrue(Path(tools[tool]).is_file())

    def test_convert_msys_path(self) -> None:
        self.assertEqual(convert_msys_path("/d/Code/x"), "D:/Code/x")
        self.assertEqual(convert_msys_path("/c/msys64"), "C:/msys64")
        self.assertEqual(convert_msys_path("D:/Code/x"), "D:/Code/x")
        self.assertEqual(convert_msys_path("relative/path"), "relative/path")


class AssembleSourceTest(unittest.TestCase):
    def test_missing_source_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as tempdir:
            temp = Path(tempdir)
            source = temp / "no_such_sequence.s"
            tools = {"cpp": "cpp", "as": "as", "objcopy": "objcopy"}
            with self.assertRaises(SequenceBankError) as caught:
                assemble_sequence_source(source, temp, temp / "work", tools)
            self.assertIn("no_such_sequence.s", str(caught.exception))


if __name__ == "__main__":
    unittest.main()
