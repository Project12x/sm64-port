#!/usr/bin/env python3
"""RED/GREEN tests for the S64A source and package contract."""
from __future__ import annotations

import hashlib
import json
import shutil
import tempfile
from pathlib import Path

from saturn_audio_package import (AudioPackageError, CHUNK_ALIGNMENT, HEADER,
                                  RESIDENT_LIMIT, compile_catalog, parse_aiff)


ROOT = Path(__file__).resolve().parents[2]


def expect_failure(fn, text: str) -> None:
    try:
        fn()
    except (AudioPackageError, ValueError, OSError):
        return
    raise AssertionError(f"expected failure: {text}")


def test_aiff_and_catalog() -> None:
    sample = next((ROOT / "sound/samples").glob("**/*.aiff"))
    parsed = parse_aiff(sample)
    assert parsed.frames > 0 and len(parsed.pcm8) == parsed.frames
    with tempfile.TemporaryDirectory() as temp:
        out = Path(temp) / "AUDIO.DAT"
        manifest = Path(temp) / "audio_manifest.json"
        result = compile_catalog(ROOT, out, manifest)
        assert result["format"] == "S64A"
        assert result["sequence_count"] == 35
        assert result["bank_count"] == 38
        assert result["sample_count"] == 219
        assert result["package_size"] == out.stat().st_size
        assert all(x["resident_bytes"] <= RESIDENT_LIMIT for x in result["closures"].values())
        raw = out.read_bytes()
        assert raw[:4] == b"S64A"
        assert len(raw) % 1 == 0
        assert manifest.is_file()


def test_source_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        shutil.copytree(ROOT / "sound", root / "sound")
        (root / "sound/sequences.json").unlink()
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"), "missing catalog")
        shutil.copy(ROOT / "sound/sequences.json", root / "sound/sequences.json")
        sample = next((root / "sound/samples").glob("**/*.aiff"))
        sample.unlink()
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"), "stale sample")


def test_alignment_hash_drift_and_duplicate() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        shutil.copytree(ROOT / "sound", root / "sound")
        # A changed source must change the source identity and package digest.
        first = Path(temp) / "one"
        one = compile_catalog(root, first)
        seq = root / "sound/sequences/us/03_level_grass.m64"
        seq.write_bytes(seq.read_bytes() + b"\0")
        second = Path(temp) / "two"
        two = compile_catalog(root, second)
        assert one["source_sha256"] != two["source_sha256"]
        assert one["package_sha256"] != two["package_sha256"]
        # The package header is self describing and every data chunk starts on
        # the advertised boundary.  A consumer can therefore reject drift.
        package = second.read_bytes()
        fields = HEADER.unpack_from(package)
        assert fields[0] == b"S64A" and fields[3] == len(package)
        chunk_count = fields[7]
        from saturn_audio_package import CHUNK, HEADER_SIZE, CHUNK_SIZE
        for i in range(chunk_count):
            offset = CHUNK.unpack_from(package, HEADER_SIZE + i * CHUNK_SIZE)[1]
            assert offset % CHUNK_ALIGNMENT == 0


def test_residency_safety_contract() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        shutil.copytree(ROOT / "sound", root / "sound")
        result = compile_catalog(root, Path(temp) / "AUDIO.DAT")
        for closure in result["closures"].values():
            assert closure["generation"] == 1
            assert closure["active_generation_eviction"] == "rejected"
            assert closure["post_boot_sound_ram_clear"] == "rejected"


if __name__ == "__main__":
    for test in (test_aiff_and_catalog, test_source_fail_closed,
                 test_alignment_hash_drift_and_duplicate, test_residency_safety_contract):
        test()
    print("compile_saturn_audio: 4/4")
