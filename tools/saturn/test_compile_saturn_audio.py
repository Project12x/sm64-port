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

from m64_decode_walk import FORMAT_US, walk_sequence
from saturn_audio_package import (AudioPackageError, CHUNK_ALIGNMENT, HEADER,
                                  RESIDENT_LIMIT, _load_sequences, compile_catalog,
                                  parse_aiff, validate_audio_dependency)


ROOT = Path(__file__).resolve().parents[2]


def synthetic_walkable_payload(size: int) -> bytes:
    """Synthetic sequence script that passes the decode-walk validator:
    mutebhv, then delay-1 padding, then the 0xFF end opcode."""
    assert size >= 3
    return bytes([0xD3, 0x20]) + b"\xfe" * (size - 3) + b"\xff"


def write_us_m64_pins(root: Path) -> None:
    """Give a temp packaging root the assets.json m64 size pins the packager
    enforces, copied verbatim from the real repo manifest (sizes only -- the
    temp tree's m64s are copies of the same extracted files)."""
    data = json.loads((ROOT / "assets.json").read_text(encoding="utf-8"))
    pins = {key: value for key, value in data.items()
            if key.startswith("sound/sequences/us/") and key.endswith(".m64")}
    (root / "assets.json").write_text(json.dumps(pins), encoding="utf-8")


def pin_m64_size(root: Path, filename: str, size: int) -> None:
    """Re-pin one sequence in a temp root's assets.json, for tests that
    rewrite a sequence with synthetic content and still need to exercise the
    decode walker behind the size gate.  The entry mirrors the real
    assets.json shape ([size, {"us": [rom_offset]}]) so a future loader
    shape-tightening does not silently break these tests."""
    pins_path = root / "assets.json"
    pins = json.loads(pins_path.read_text(encoding="utf-8"))
    pins[f"sound/sequences/us/{filename}"] = [size, {"us": [0]}]
    pins_path.write_text(json.dumps(pins), encoding="utf-8")


def copy_complete_sound(temp: str | Path) -> Path:
    root = Path(temp) / "repo"
    shutil.copytree(ROOT / "sound", root / "sound")
    # The checked-in wrapper intentionally has no generated include.  Tests
    # that exercise the complete package contract provide a clearly synthetic
    # expanded payload (decode-walk valid, since the packager now walks every
    # packaged sequence); the real asset remains a required user input.
    (root / "sound/sequences.bin.inc.c").write_bytes(synthetic_walkable_payload(2048))
    write_us_m64_pins(root)
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
        grown = seq.read_bytes() + b"\0"
        seq.write_bytes(grown)
        # Keep the size pin consistent with the mutation: this test is about
        # identity drift, not the fail-closed size gate (covered separately).
        pin_m64_size(root, "03_level_grass.m64", len(grown))
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
        write_us_m64_pins(root)
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
        pin_m64_size(root, sequence.name, 5)  # size gate passes; walker fires
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
        pin_m64_size(root, sequence.name, 6)
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
        pin_m64_size(root, sequence.name, len(valid))
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


def test_m64_size_pins_fail_closed() -> None:
    """Reviewer-proven residual exposure, closed by the assets.json size pin:
    a truncation landing entirely in the opaque channel-script region passes
    the decode walker (which validates only the sequence-level prefix, by
    design) and previously sealed into AUDIO.DAT with exit 0.  The real file
    is temp-copied and mutated at test time; no Nintendo bytes are committed."""
    key = "sound/sequences/us/03_level_grass.m64"
    pinned = json.loads((ROOT / "assets.json").read_text(encoding="utf-8"))[key][0]
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        sequence = root / key
        original = sequence.read_bytes()
        assert len(original) == pinned, (len(original), pinned)
        truncated = original[:2000]  # the reviewer's exact cut
        # The cut lands entirely in the opaque region: the walker alone still
        # passes it, which is exactly why the size pin exists.  (If a future
        # walker change starts catching this, re-examine whether the pin test
        # still exercises the opaque-region scenario.)
        walk_ok, _ = walk_sequence(truncated, FORMAT_US)
        assert walk_ok, "expected the opaque-region truncation to pass the walker"
        sequence.write_bytes(truncated)
        try:
            compile_catalog(root, Path(temp) / "truncated")
        except AudioPackageError as error:
            message = str(error)
            assert "03_level_grass" in message, message
            assert "2000" in message and str(pinned) in message, message
        else:
            raise AssertionError("expected size-pin mismatch failure")
        # A sequence with no assets.json entry fails closed too: all 34
        # extracted US m64s are pinned, so a missing pin is a defect.
        sequence.write_bytes(original)
        pins = json.loads((root / "assets.json").read_text(encoding="utf-8"))
        del pins[key]
        (root / "assets.json").write_text(json.dumps(pins), encoding="utf-8")
        try:
            compile_catalog(root, Path(temp) / "unpinned")
        except AudioPackageError as error:
            assert "size pin" in str(error), str(error)
        else:
            raise AssertionError("expected missing-pin failure")
        # Missing assets.json entirely fails closed as well.
        (root / "assets.json").unlink()
        expect_failure(lambda: compile_catalog(root, Path(temp) / "no-pins"),
                       "missing assets.json size pins")


SYNTHETIC_SOUNDS_H = """\
#define SOUND_ARG_LOAD(bank, soundID, priority, flags) (\\
    ((u32) (bank) << 28) | \\
    ((u32) (soundID) << 16) | \\
    ((u32) (priority) << 8) | \\
    (flags))

#define SOUND_BANK_ACTION     0
#define SOUND_BANK_GENERAL    3
#define SOUND_BANK_COUNT     10

#define SOUND_ACTION_TEST_JUMP  SOUND_ARG_LOAD(SOUND_BANK_ACTION,  0x00, 0x80, 8)
#define SOUND_GENERAL_TEST_STAR SOUND_ARG_LOAD(SOUND_BANK_GENERAL, 0x01, 0x80, 8)
#define SOUND_ACTION_TEST_OOB   SOUND_ARG_LOAD(SOUND_BANK_ACTION,  0x30, 0x80, 8)
"""

SYNTHETIC_SEQ_IDS_H = """\
enum SeqId {
    SEQ_SOUND_PLAYER,                 // 0x00
    SEQ_EVENT_CUTSCENE_COLLECT_STAR,  // 0x01
    SEQ_MENU_TITLE_SCREEN,            // 0x02
    SEQ_LEVEL_GRASS,                  // 0x03
    SEQ_COUNT
};
"""

# Bank ordinals index sound/sequences.json's 00_sound_player list:
# 1 -> "01_terrain", 4 -> "04".
SYNTHETIC_SOUND_PLAYER_S = """\
#include "seq_macros.inc"

sequence_start:
seq_startchannel 0, .channel0
seq_startchannel 3, .channel3

.channel0:
chan_setdyntable .channel0_table
chan_end

.channel3:
chan_setdyntable .channel3_table
chan_end

.channel0_table:
sound_ref .sound_action_test_jump

.channel3_table:
sound_ref .sound_general_filler
sound_ref .sound_general_test_star

.sound_action_test_jump:
chan_setbank 1
chan_setinstr 0
chan_end

.sound_general_filler:
chan_setbank 1
chan_setinstr 0
chan_end

.sound_general_test_star:
chan_setbank 4
chan_setinstr 2
chan_end
"""


def write_scene_closure(path: Path, sfx_ids: list[str], sfx_banks: list[str],
                        music: list[str], level: str = "bob") -> Path:
    """Write a synthetic scene closure matching collect_scene_closure.py's
    real document layout (:1040): the packager consumes the top-level
    aggregate lists; record-level provenance belongs to the generator."""
    document = {"schema": "sm64-saturn-scene-closure-v1", "source_root": ".",
                "level": level, "area": 1, "records": [], "scene_sources": [],
                "source_hashes": {},
                "music_sequence_ids": sorted(set(music)),
                "sfx_banks": sorted(set(sfx_banks)),
                "sfx_ids": sorted(set(sfx_ids))}
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=1, sort_keys=True) + "\n",
                    encoding="utf-8")
    return path


def write_closure_join_inputs(root: Path) -> None:
    """Give a temp packaging root the closure join inputs the packager reads:
    include/sounds.h declarations, include/seq_ids.h music IDs, and a
    synthetic sound-player script replacing the real (Nintendo-derived) one."""
    (root / "include").mkdir(parents=True, exist_ok=True)
    (root / "include/sounds.h").write_text(SYNTHETIC_SOUNDS_H, encoding="utf-8")
    (root / "include/seq_ids.h").write_text(SYNTHETIC_SEQ_IDS_H, encoding="utf-8")
    (root / "sound/sequences/00_sound_player.s").write_text(
        SYNTHETIC_SOUND_PLAYER_S, encoding="utf-8")


def _bank_sample_bank(root: Path, stem: str) -> str:
    meta = json.loads((root / f"sound/sound_banks/{stem}.json").read_text(encoding="utf-8"))
    sample_bank = meta.get("sample_bank")
    if isinstance(sample_bank, dict):
        sample_bank = sample_bank.get("then")
    return str(sample_bank)


def _instrument_chain_samples(root: Path, stem: str, index: int) -> set[str]:
    """Independent expectation walk: instrument -> sound/sound_lo/sound_hi."""
    meta = json.loads((root / f"sound/sound_banks/{stem}.json").read_text(encoding="utf-8"))
    sample_bank = _bank_sample_bank(root, stem)
    inst = meta["instruments"][meta["instrument_list"][index]]
    names = set()
    for key in ("sound", "sound_lo", "sound_hi"):
        value = inst.get(key)
        if isinstance(value, str):
            names.add(f"{sample_bank}/{value}")
        elif isinstance(value, dict) and isinstance(value.get("sample"), str):
            names.add(f"{sample_bank}/{value['sample']}")
    return names


def _whole_bank_samples(root: Path, stem: str) -> set[str]:
    meta = json.loads((root / f"sound/sound_banks/{stem}.json").read_text(encoding="utf-8"))
    sample_bank = _bank_sample_bank(root, stem)
    names: set[str] = set()

    def visit(value: object) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if key in {"sound", "sound_lo", "sound_hi"}:
                    if isinstance(child, str):
                        names.add(f"{sample_bank}/{child}")
                    elif isinstance(child, dict) and isinstance(child.get("sample"), str):
                        names.add(f"{sample_bank}/{child['sample']}")
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
    visit(meta)
    return names


def test_closure_selection_exact() -> None:
    """--closure drives selection: the resident bundle is exactly the union
    of (a) each closure SFX ID's bank/instrument/sample chain and (b) the
    closure's music sequences -- nothing more, deterministically ordered."""
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        write_closure_join_inputs(root)
        closure_path = write_scene_closure(
            root / "closure.json",
            sfx_ids=["SOUND_ACTION_TEST_JUMP", "SOUND_GENERAL_TEST_STAR"],
            sfx_banks=["action", "general"], music=["SEQ_LEVEL_GRASS"])
        result = compile_catalog(root, Path(temp) / "AUDIO.DAT",
                                 Path(temp) / "audio_manifest.json",
                                 scene_closure=closure_path)
        bob = result["closures"]["bob"]
        # Exactly the closure-derived banks: music bank 22 plus the two
        # instrument banks the synthetic sound player maps the SFX to.
        assert bob["sequence_ids"] == [3], bob["sequence_ids"]
        assert bob["bank_names"] == ["01_terrain", "04", "22"], bob["bank_names"]
        expected_samples = (_whole_bank_samples(root, "22") |
                            _instrument_chain_samples(root, "01_terrain", 0) |
                            _instrument_chain_samples(root, "04", 2))
        assert set(bob["sample_ids"]) == expected_samples, bob["sample_ids"]
        assert len(bob["sample_ids"]) == len(set(bob["sample_ids"]))
        # SFX-only banks carry only the resolved instrument mappings; the
        # music bank keeps its full enumeration.
        sfx_only = [m for m in bob["sfx_mappings"] if m["bank_id"] in (1, 4)]
        assert {(m["bank_id"], m["sound_id"]) for m in sfx_only} == {(1, 0), (4, 2)}, sfx_only
        resolution = bob["sfx_resolution"]
        assert resolution["SOUND_ACTION_TEST_JUMP"] == [
            {"bank": "01_terrain", "instrument_index": 0,
             "instrument": "inst0"}], resolution
        assert resolution["SOUND_GENERAL_TEST_STAR"] == [
            {"bank": "04", "instrument_index": 2, "instrument": "inst2"}], resolution
        assert bob["selection"] == "scene-closure-v1"
        # WF has no closure yet: it stays on the hardcoded music-only path,
        # and the manifest records the asymmetry.
        wf = result["closures"]["wf"]
        assert wf["selection"] == "hardcoded-music-fallback"
        assert wf["bank_names"] == ["22"], wf["bank_names"]
        assert result["closure_selection"] == {
            "bob": "scene-closure-v1", "wf": "hardcoded-music-fallback"}
        # Determinism: a second run yields byte-identical closures.
        second = compile_catalog(root, Path(temp) / "AUDIO2.DAT",
                                 scene_closure=closure_path)
        assert json.dumps(result["closures"], sort_keys=True) == \
            json.dumps(second["closures"], sort_keys=True)


def test_closure_unresolvable_fails_closed() -> None:
    """A closure SFX ID with no resolvable mapping fails packaging closed,
    naming the ID; so do unknown music IDs and declared-bank mismatches."""
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        write_closure_join_inputs(root)
        out = Path(temp) / "x"
        # Unknown SOUND declaration.
        closure = write_scene_closure(
            root / "c1.json", sfx_ids=["SOUND_FAKE_DOES_NOT_EXIST"],
            sfx_banks=["action"], music=["SEQ_LEVEL_GRASS"])
        try:
            compile_catalog(root, out, scene_closure=closure)
        except AudioPackageError as error:
            assert "SOUND_FAKE_DOES_NOT_EXIST" in str(error), str(error)
        else:
            raise AssertionError("expected unknown-SFX failure")
        # Sound ID beyond the channel's dyntable.
        closure = write_scene_closure(
            root / "c2.json", sfx_ids=["SOUND_ACTION_TEST_OOB"],
            sfx_banks=["action"], music=["SEQ_LEVEL_GRASS"])
        try:
            compile_catalog(root, out, scene_closure=closure)
        except AudioPackageError as error:
            assert "SOUND_ACTION_TEST_OOB" in str(error), str(error)
        else:
            raise AssertionError("expected out-of-table failure")
        # Unknown music sequence symbol.
        closure = write_scene_closure(
            root / "c3.json", sfx_ids=["SOUND_ACTION_TEST_JUMP"],
            sfx_banks=["action"], music=["SEQ_DOES_NOT_EXIST"])
        try:
            compile_catalog(root, out, scene_closure=closure)
        except AudioPackageError as error:
            assert "SEQ_DOES_NOT_EXIST" in str(error), str(error)
        else:
            raise AssertionError("expected unknown-music failure")
        # Closure-declared SFX banks must agree with the sounds.h derivation.
        closure = write_scene_closure(
            root / "c4.json", sfx_ids=["SOUND_ACTION_TEST_JUMP"],
            sfx_banks=["action", "menu"], music=["SEQ_LEVEL_GRASS"])
        try:
            compile_catalog(root, out, scene_closure=closure)
        except AudioPackageError as error:
            assert "menu" in str(error), str(error)
        else:
            raise AssertionError("expected sfx-bank mismatch failure")


def test_closure_resident_overflow_fails_closed() -> None:
    """The 480 KiB resident contract stays fail-closed on the closure path."""
    import saturn_audio_package as sap
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        write_closure_join_inputs(root)
        closure = write_scene_closure(
            root / "closure.json",
            sfx_ids=["SOUND_ACTION_TEST_JUMP"], sfx_banks=["action"],
            music=["SEQ_LEVEL_GRASS"])
        original = sap.RESIDENT_LIMIT
        sap.RESIDENT_LIMIT = 1024
        try:
            compile_catalog(root, Path(temp) / "x", scene_closure=closure)
        except AudioPackageError as error:
            assert "resident closure exceeds" in str(error), str(error)
        else:
            raise AssertionError("expected resident-overflow failure")
        finally:
            sap.RESIDENT_LIMIT = original


def test_closure_real_bob_sfx_halving_fits_resident_budget() -> None:
    """Task 3's real BOB closure (54 SFX IDs across 9 instrument banks plus
    music bank 22, 48 unique samples) needs 679,936 resident bytes at full
    rate against the 491,520-byte SM64_SATURN_AUDIO_RESIDENT_LIMIT -- a
    deterministic 188,416-byte overflow (the fail-closed mechanism itself
    stays covered by test_closure_resident_overflow_fails_closed's synthetic
    fixture).  Owner-approved fix (2026-08-09 session, explicit approval:
    "halve the sample rate - thats fine"): 2:1 PCM decimation on
    closure-mode SFX samples only; music bank 22 stays full rate.  This must
    now fit, with real margin, end to end against the real repo inputs."""
    import collect_scene_closure as csc
    with tempfile.TemporaryDirectory() as temp:
        root = copy_complete_sound(temp)
        (root / "include").mkdir(parents=True, exist_ok=True)
        shutil.copy(ROOT / "include/sounds.h", root / "include/sounds.h")
        shutil.copy(ROOT / "include/seq_ids.h", root / "include/seq_ids.h")
        document = csc.collect_scene_closure(
            ROOT, "bob", 1, ROOT / "tools/saturn/behavior_spawn_rules.json")
        closure_path = Path(temp) / "closure.json"
        csc.write_closure(closure_path, document)
        assert len(document["sfx_ids"]) == 54, document["sfx_ids"]

        result = compile_catalog(root, Path(temp) / "AUDIO.DAT",
                                 Path(temp) / "audio_manifest.json",
                                 scene_closure=closure_path)
        bob = result["closures"]["bob"]
        assert bob["selection"] == "scene-closure-v1"
        assert len(bob["sample_ids"]) == 48, bob["sample_ids"]
        # Real, independently re-derived numbers: full rate needs 679,936
        # bytes (661,504 aligned PCM + 18,432 aligned metadata).  Halving the
        # 32 SFX-only samples (16 music-bank-22 samples untouched) drops raw
        # PCM from 660,864 to 452,808 bytes, 2048-aligning to 454,656; plus
        # the unchanged 18,432 aligned metadata gives 473,088 resident bytes
        # -- 18,432 bytes (18.0 KiB) of real margin under the 491,520-byte
        # (480.0 KiB) limit.
        assert bob["resident_bytes"] == 473_088, bob["resident_bytes"]
        assert bob["resident_bytes"] <= RESIDENT_LIMIT
        assert RESIDENT_LIMIT - bob["resident_bytes"] == 18_432

        sample_by_id = {s["id"]: s for s in result["samples"]}
        # Spot-check two music-bank-22 samples against an independent direct
        # AIFF parse: rate, frame count, packaged PCM bytes, and the
        # closure's own chunk hash all agree -- music never passes through
        # the SFX decimation path.
        for stable_id in ("instruments/06_kick_drum_1", "instruments/07_rimshot"):
            direct = parse_aiff(ROOT / "sound/samples" / f"{stable_id}.aiff")
            assert sample_by_id[stable_id]["rate"] == direct.rate
            assert sample_by_id[stable_id]["pcm8_bytes"] == len(direct.pcm8)
            expected_hash = hashlib.sha256(direct.pcm8).hexdigest()
            chunk = next(c for c in bob["chunk_hashes"]
                        if c["kind"] == "SAMP" and c["id"] == stable_id)
            assert chunk["sha256"] == expected_hash, stable_id

        # Every one of the closure's 48 samples is either exactly half its
        # source rate/length (the 32 SFX-only samples) or byte-identical to
        # the source (the 16 music samples) -- never anything else.
        halved = 0
        for stable_id in bob["sample_ids"]:
            direct = parse_aiff(ROOT / "sound/samples" / f"{stable_id}.aiff")
            record = sample_by_id[stable_id]
            if record["rate"] == direct.rate // 2:
                assert record["pcm8_bytes"] == len(direct.pcm8[::2])
                halved += 1
            else:
                assert record["rate"] == direct.rate
                assert record["pcm8_bytes"] == len(direct.pcm8)
        assert halved == 32, halved

        # GREEN-twice: independent compiles of the real closure agree byte
        # for byte (pitch-halving must be deterministic, not incidental).
        second = compile_catalog(root, Path(temp) / "AUDIO2.DAT",
                                 scene_closure=closure_path)
        assert json.dumps(result["closures"], sort_keys=True) == \
            json.dumps(second["closures"], sort_keys=True)


def test_generated_sequences_bin_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp) / "repo"
        shutil.copytree(ROOT / "sound", root / "sound")
        write_us_m64_pins(root)
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
                 test_m64_size_pins_fail_closed,
                 test_closure_selection_exact,
                 test_closure_unresolvable_fails_closed,
                 test_closure_resident_overflow_fails_closed,
                 test_closure_real_bob_sfx_halving_fits_resident_budget,
                 test_generated_sequences_bin_fail_closed):
        test()
    print("compile_saturn_audio: 12/12")
