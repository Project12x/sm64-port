#!/usr/bin/env python3
"""RED/GREEN tests for the S64A source and package contract."""
from __future__ import annotations

import hashlib
import json
import shutil
import struct
import aifc
import tempfile
from pathlib import Path

from saturn_audio_package import (AudioPackageError, CHUNK_ALIGNMENT, HEADER,
                                  RESIDENT_LIMIT, _load_sequences, compile_catalog,
                                  parse_aiff, validate_audio_dependency)


ROOT = Path(__file__).resolve().parents[2]


def synthetic_walkable_payload(size: int) -> bytes:
    """Synthetic sequence script that passes the decode-walk validator:
    mutebhv, then delay-1 padding, then the 0xFF end opcode."""
    assert size >= 3
    return bytes([0xD3, 0x20]) + b"\xfe" * (size - 3) + b"\xff"


def copy_complete_sound(temp: str | Path) -> Path:
    root = Path(temp) / "repo"
    shutil.copytree(ROOT / "sound", root / "sound")
    # The checked-in wrapper intentionally has no generated include.  Tests
    # that exercise the complete package contract provide a clearly synthetic
    # expanded payload (decode-walk valid, since the packager now walks every
    # packaged sequence); the real asset remains a required user input.
    (root / "sound/sequences.bin.inc.c").write_bytes(synthetic_walkable_payload(2048))
    return root


def write_synthetic_sequences_bin(path: Path, seq0: bytes | None = None) -> bytes:
    """Synthetic 35-entry big-endian TYPE_SEQ bank; entry 0 is >1024 bytes.

    Mirrors the assemble_sound.py --sequences layout without any real
    Nintendo bytes: u16 magic 3, u16 count, count * (u32 offset, u32 length),
    16-aligned data, zero pad to 64.
    """
    count = 35
    data_start = (4 + count * 8 + 15) & -16
    if seq0 is None:
        seq0 = synthetic_walkable_payload(1500)
    seq0_len = (len(seq0) + 15) & -16
    entries = [(data_start, seq0_len)]
    cursor = data_start + seq0_len
    for _ in range(1, count):
        entries.append((cursor, 16))
        cursor += 16
    header = struct.pack(">HH", 3, count)
    header += b"".join(struct.pack(">II", offset, length) for offset, length in entries)
    header += bytes(data_start - len(header))
    payload = seq0 + bytes(seq0_len - len(seq0)) + bytes(range(16)) * (count - 1)
    raw = header + payload
    raw += bytes(((len(raw) + 63) & -64) - len(raw))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(raw)
    return raw


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
    with aifc.open(str(sample), "rb") as source:
        first = int.from_bytes(source.readframes(1), "big", signed=True)
    assert parsed.pcm8[0] == ((first >> 8) & 0xFF)
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        out = Path(temp) / "AUDIO.DAT"
        manifest = Path(temp) / "audio_manifest.json"
        result = compile_catalog(root, out, manifest)
        assert result["format"] == "S64A"
        assert result["sequence_count"] == 35
        assert result["bank_count"] == 38
        assert result["sample_count"] == 219
        assert any(item["path"] == "sound/sound_data.c"
                   for item in result["source_inventory"])
        assert all(sample["loop_source"] == "bank-metadata" and
                   sample["loop_start"] is None and sample["tuning"] is None
                   for sample in result["samples"])
        assert any(binding.get("tuning") is not None
                   for sample in result["samples"]
                   for binding in sample["bank_bindings"])
        assert result["package_size"] == out.stat().st_size
        assert all(x["resident_bytes"] <= RESIDENT_LIMIT for x in result["closures"].values())
        raw = out.read_bytes()
        assert raw[:4] == b"S64A"
        assert len(raw) % 1 == 0
        assert manifest.is_file()


def test_source_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temp:
        incomplete = Path(temp) / "incomplete"
        shutil.copytree(ROOT / "sound", incomplete / "sound")
        expect_failure(lambda: compile_catalog(incomplete, Path(temp) / "missing-expanded"),
                       "missing expanded sequence 00")
        root = copy_complete_sound(temp)
        (root / "sound/sequences.json").unlink()
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"), "missing catalog")
        shutil.copy(ROOT / "sound/sequences.json", root / "sound/sequences.json")
        sample = next((root / "sound/samples").glob("**/*.aiff"))
        sample.unlink()
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"), "stale sample")
        shutil.copy(ROOT / "sound/samples" / "instruments/00.aiff", sample)
        sequence = root / "sound/sequences/us/03_level_grass.m64"
        sequence.write_bytes(b"")
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"),
                       "empty sequence")
        sequence.write_bytes(b"\0" * 8)
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"),
                       "invalid sequence control flow")
        original = (ROOT / "sound/sequences/us/03_level_grass.m64").read_bytes()
        malformed = bytearray(original)
        malformed[128:131] = b"\xfc\xff\xff"
        sequence.write_bytes(malformed)
        expect_failure(lambda: compile_catalog(root, Path(temp) / "x"),
                       "out-of-range sequence control flow")


def test_alignment_hash_drift_and_duplicate() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
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
        # Preserve the inventory cardinality while creating an actual duplicate
        # two-digit bank ID; the loader must reject the identity collision.
        bank_one = root / "sound/sound_banks/03.json"
        bank_one.rename(root / "sound/sound_banks/00_duplicate.json")
        expect_failure(lambda: compile_catalog(root, Path(temp) / "duplicate"),
                       "duplicate bank ID")


def test_residency_safety_contract() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        result = compile_catalog(root, Path(temp) / "AUDIO.DAT")
        dependencies = result["s64p_audio_dependencies"]
        assert dependencies and all(validate_audio_dependency(result, item)
                                    for item in dependencies)
        tampered = dict(dependencies[0])
        tampered["content_sha256"] = "0" * 64
        assert not validate_audio_dependency(result, tampered)
        assert all(not Path(sample["source"]).is_absolute()
                   for sample in result["samples"])
        for closure in result["closures"].values():
            assert closure["generation"] == 1
            assert closure["active_generation_eviction"] == "rejected"
            assert closure["post_boot_sound_ram_clear"] == "rejected"


def test_generated_sequences_bin() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        # No sequences.bin.inc.c: the generated bank replaces the PC-build
        # product entirely on this path.
        shutil.copytree(ROOT / "sound", root / "sound")
        bin_path = root / "build/saturn/audio/generated/sequences.bin"
        raw = write_synthetic_sequences_bin(bin_path)
        entries = _load_sequences(root, bin_path)
        seq0 = entries[0]
        offset, length = seq0["source_range"]
        assert length > 1024 and seq0["bytes"] == length
        assert seq0["sha256"] == hashlib.sha256(raw[offset:offset + length]).hexdigest()
        assert seq0["control_flow"] == "source-generated"
        assert seq0["source"] == "build/saturn/audio/generated/sequences.bin"
        assert all("source_range" not in entry for entry in entries[1:])
        result = compile_catalog(root, Path(temp) / "AUDIO.DAT",
                                 Path(temp) / "audio_manifest.json",
                                 sequences_bin=bin_path)
        assert result["sequence_count"] == 35
        assert any(item["path"] == "build/saturn/audio/generated/sequences.bin"
                   for item in result["source_inventory"])
        # Without --sequences-bin the fallback still fails closed, and the
        # guard names the standalone generator as the fix.
        try:
            compile_catalog(root, Path(temp) / "fallback")
        except AudioPackageError as error:
            assert "gen_sequence_bank" in str(error)
        else:
            raise AssertionError("expected fallback inventory failure")


def test_decode_walk_fail_closed() -> None:
    """The decode walker is the packaging authority: any finding fails
    packaging closed, naming the sequence and the offending offset."""
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        sequence = root / "sound/sequences/us/03_level_grass.m64"
        # Review finding M-A: a synthetic valid sequence truncated mid-opcode
        # passes the byte-scan heuristic but must now fail packaging.
        valid = bytes([0xd3, 0x20, 0xd5, 0x32, 0xdd, 0x78, 0xdb, 0x66,
                       0xfd, 0x40, 0xff])
        sequence.write_bytes(valid[:5])  # cuts 0xdd's operand
        try:
            compile_catalog(root, Path(temp) / "truncated")
        except AudioPackageError as error:
            message = str(error)
            assert "03_level_grass" in message, message
            assert "offset" in message and "truncated" in message, message
        else:
            raise AssertionError("expected truncated-sequence failure")
        # Channel-pointer table entry past EOF.
        sequence.write_bytes(bytes([0xd3, 0x20, 0x90, 0x40, 0x00, 0xff]))
        try:
            compile_catalog(root, Path(temp) / "channel")
        except AudioPackageError as error:
            message = str(error)
            assert "03_level_grass" in message, message
            assert "target-out-of-range" in message, message
        else:
            raise AssertionError("expected channel-pointer failure")
        # The synthetic valid script itself must pass end to end.
        sequence.write_bytes(valid)
        result = compile_catalog(root, Path(temp) / "valid")
        assert result["sequence_count"] == 35
        # The generated seq00 payload is walked too: an invalid opcode in
        # the bank's entry 0 fails packaging closed, naming the sequence.
        bad_seq0 = bytes([0xd3, 0x20, 0xb0]) + b"\xfe" * 1496 + b"\xff"
        bad_bank = root / "build/bad-seq00.bin"
        write_synthetic_sequences_bin(bad_bank, seq0=bad_seq0)
        try:
            compile_catalog(root, Path(temp) / "seq00",
                            sequences_bin=bad_bank)
        except AudioPackageError as error:
            message = str(error)
            assert "00_sound_player" in message, message
            assert "unknown-opcode" in message, message
        else:
            raise AssertionError("expected generated-seq00 walk failure")


def test_generated_sequences_bin_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        shutil.copytree(ROOT / "sound", root / "sound")
        missing = root / "build/saturn/audio/generated/sequences.bin"
        try:
            compile_catalog(root, Path(temp) / "x", sequences_bin=missing)
        except AudioPackageError as error:
            assert "gen_sequence_bank" in str(error)
        else:
            raise AssertionError("expected missing generated bank failure")
        bad_magic = root / "build/bad-magic.bin"
        bad_magic.parent.mkdir(parents=True, exist_ok=True)
        bad_magic.write_bytes(b"\x00\x07\x00\x23" + bytes(2048))
        expect_failure(lambda: compile_catalog(root, Path(temp) / "y",
                                               sequences_bin=bad_magic),
                       "wrong sequence bank magic")
        truncated = root / "build/truncated.bin"
        truncated.write_bytes(struct.pack(">HH", 3, 35) +
                              struct.pack(">II", 288, 1 << 20) + bytes(2048))
        expect_failure(lambda: compile_catalog(root, Path(temp) / "z",
                                               sequences_bin=truncated),
                       "entry 00 out of range")


if __name__ == "__main__":
    for test in (test_aiff_and_catalog, test_source_fail_closed,
                 test_alignment_hash_drift_and_duplicate, test_residency_safety_contract,
                 test_generated_sequences_bin, test_decode_walk_fail_closed,
                 test_generated_sequences_bin_fail_closed):
        test()
    print("compile_saturn_audio: 7/7")
