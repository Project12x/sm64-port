#!/usr/bin/env python3
"""Deterministic S64A v1 catalog compiler.

The compiler consumes the repository's user-extracted sound tree directly.  It
never invokes the PC build: source JSON, m64 and AIFF identities are hashed
before packaging and a missing input is a hard error.  Sample PCM is reduced to
signed Saturn PCM8; all sequence/bank metadata is retained as canonical
JSON so tuning, envelopes and control-flow bytes remain inspectable.

Every on-disk US m64 consumed is size-checked against the exact byte count
assets.json pins for it (fail-closed on mismatch or missing pin): the decode
walker only validates the sequence-level prefix -- channel/layer script
bodies are opaque by design -- so the size pin is the packaging defense
against a truncation landing entirely in the opaque region.
"""
from __future__ import annotations

import aifc
import hashlib
import json
import math
import re
import struct
from dataclasses import dataclass
from pathlib import Path

from gen_sequence_bank import SEQUENCE_DEFINES, SequenceBankError, parse_sequence_bank
from m64_decode_walk import FORMAT_US, walk_sequence
from scene_package_schema import SCHEMA as SCENE_CLOSURE_SCHEMA

MAGIC = b"S64A"
VERSION = 1
CHUNK_ALIGNMENT = 2048
RESIDENT_LIMIT = 480 * 1024
HEADER = struct.Struct(">4sHHIIIIII32s32s")
CHUNK = struct.Struct(">4sIIIII32s")
HEADER_SIZE = HEADER.size
CHUNK_SIZE = CHUNK.size
PACKAGE_HASH_OFFSET = 64


class AudioPackageError(ValueError):
    pass


_M64_PIN_PREFIX = "sound/sequences/us/"


def _load_m64_size_pins(root: Path) -> dict[str, int]:
    """Load the exact byte sizes assets.json pins for the extracted US m64s.

    Entry format (verified against the repo manifest):
    "sound/sequences/us/NAME.m64" -> [size, {"us": [rom_offset]}].  All 34
    extracted US sequences carry a pin, so a missing manifest -- or later, a
    missing per-sequence entry -- fails packaging closed.
    """
    pins_path = root / "assets.json"
    if not pins_path.is_file():
        raise AudioPackageError(
            f"missing {pins_path}: assets.json size pins are required to "
            "package extracted m64 sequences (truncation defense for the "
            "regions the decode walk keeps opaque)")
    try:
        data = json.loads(pins_path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise AudioPackageError(
            f"invalid assets.json at {pins_path}: {error}") from error
    pins: dict[str, int] = {}
    for key, value in data.items():
        if not key.startswith(_M64_PIN_PREFIX) or not key.endswith(".m64"):
            continue
        if (not isinstance(value, list) or not value or
                not isinstance(value[0], int) or value[0] <= 0):
            raise AudioPackageError(
                f"assets.json entry for {key} does not pin a positive size")
        pins[key] = value[0]
    return pins


@dataclass(frozen=True)
class Sample:
    stable_id: str
    source: str
    source_sha256: str
    rate: int
    frames: int
    pcm8: bytes
    aiff_metadata: dict[str, object]


def _canonical(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, sort_keys=True,
                       separators=(",", ":")) + "\n").encode("utf-8")


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _align(value: int, alignment: int = CHUNK_ALIGNMENT) -> int:
    return (value + alignment - 1) & -alignment


def _aiff_metadata(raw: bytes) -> dict[str, object]:
    """Preserve AIFF MARK/INST loop facts instead of discarding them in aifc."""
    metadata: dict[str, object] = {"markers": [], "instrument": None}
    cursor = 12
    while cursor + 8 <= len(raw):
        kind = raw[cursor:cursor + 4]
        size = int.from_bytes(raw[cursor + 4:cursor + 8], "big")
        payload = raw[cursor + 8:cursor + 8 + size]
        if len(payload) != size:
            break
        if kind == b"MARK" and len(payload) >= 2:
            count = int.from_bytes(payload[:2], "big")
            pos = 2
            markers = []
            for _ in range(count):
                if pos + 7 > len(payload): break
                marker_id = int.from_bytes(payload[pos:pos + 2], "big")
                marker_pos = int.from_bytes(payload[pos + 2:pos + 6], "big")
                name_len = payload[pos + 6]
                name = payload[pos + 7:pos + 7 + name_len].decode("latin1", "replace")
                markers.append({"id": marker_id, "position": marker_pos, "name": name})
                pos += 7 + name_len + ((name_len + 1) & 1)
            metadata["markers"] = markers
        elif kind == b"INST" and len(payload) >= 20:
            metadata["instrument"] = {
                "base_note": payload[0], "detune": int.from_bytes(payload[1:2], "big", signed=True),
                "low_note": payload[2], "high_note": payload[3],
                "low_velocity": payload[4], "high_velocity": payload[5],
                "gain": int.from_bytes(payload[6:8], "big", signed=True),
                "sustain_loop": {"play_mode": int.from_bytes(payload[8:10], "big"),
                                  "begin_marker": int.from_bytes(payload[10:12], "big"),
                                  "end_marker": int.from_bytes(payload[12:14], "big")},
                "release_loop": {"play_mode": int.from_bytes(payload[14:16], "big"),
                                  "begin_marker": int.from_bytes(payload[16:18], "big"),
                                  "end_marker": int.from_bytes(payload[18:20], "big")}}
        cursor += 8 + size + (size & 1)
    return metadata


def parse_aiff(path: Path) -> Sample:
    """Parse canonical mono PCM16 AIFF and convert samples to Saturn PCM8."""
    try:
        with aifc.open(str(path), "rb") as stream:
            if stream.getnchannels() != 1 or stream.getsampwidth() != 2:
                raise AudioPackageError(f"{path}: expected mono PCM16 AIFF")
            rate = stream.getframerate()
            frames = stream.getnframes()
            raw = stream.readframes(frames)
            if stream.getcomptype() != b"NONE":
                raise AudioPackageError(f"{path}: compressed AIFF is unsupported")
    except (EOFError, OSError, ValueError) as error:
        raise AudioPackageError(f"{path}: invalid AIFF: {error}") from error
    if len(raw) != frames * 2 or rate <= 0:
        raise AudioPackageError(f"{path}: truncated PCM payload")
    pcm8 = bytearray(frames)
    for index in range(frames):
        # AIFF is big-endian signed PCM.  Preserve the signed PCM8 polarity
        # consumed by the SCSP driver; no DC-bias or unsigned reinterpretation.
        sample = int.from_bytes(raw[index * 2:index * 2 + 2], "big", signed=True)
        pcm8[index] = (sample >> 8) & 0xFF
    source = path.as_posix()
    # Basenames repeat across extracted sample banks (00..1C are common), so
    # the stable identity includes the source bank directory.
    stable_id = "/".join(path.parts[-2:]).rsplit(".", 1)[0]
    return Sample(stable_id, source, _sha(path.read_bytes()), rate, frames, bytes(pcm8),
                  _aiff_metadata(path.read_bytes()))


def _relative_source(path: Path, root: Path) -> str:
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def source_inventory(root: Path, sequences_bin: Path | None = None) -> tuple[list[Path], str]:
    sound = root / "sound"
    expanded_seq0 = sequences_bin if sequences_bin is not None else sound / "sequences.bin.inc.c"
    required = [sound / "sequences.json", sound / "sound_data.c", expanded_seq0]
    required += sorted((sound / "sound_banks").glob("*.json"))
    required += sorted((sound / "sequences" / "us").glob("*.m64"))
    required += sorted((sound / "samples").glob("**/*.aiff"))
    if not (sound / "sequences.json").is_file():
        raise AudioPackageError("missing sound/sequences.json (user audio inputs required)")
    if not (sound / "sound_data.c").is_file():
        raise AudioPackageError("missing sound/sound_data.c for generated sequence 00")
    if not expanded_seq0.is_file() or expanded_seq0.stat().st_size <= 1024:
        if sequences_bin is not None:
            raise AudioPackageError(
                f"missing generated sequence bank: {expanded_seq0} "
                "(generate it with tools/saturn/gen_sequence_bank.py, or the "
                "compile-audio-sequences make target)")
        raise AudioPackageError(
            "missing expanded sequence-00 payload: sound/sequences.bin.inc.c "
            "(the 338-byte sound_data.c wrapper is not package data; generate "
            "the standalone bank with tools/saturn/gen_sequence_bank.py and "
            "pass --sequences-bin instead)")
    banks = sorted((sound / "sound_banks").glob("*.json"))
    samples = sorted((sound / "samples").glob("**/*.aiff"))
    if len(banks) != 38 or len(samples) != 219:
        raise AudioPackageError(
            f"source audio inventory mismatch: expected 38 banks/219 AIFFs, "
            f"found {len(banks)}/{len(samples)}")
    records = []
    for path in required:
        records.append((_relative_source(path, root), _sha(path.read_bytes())))
    return required, _sha(_canonical(records))


def _extract_generated_seq00(sequences_bin: Path) -> tuple[bytes, int, int]:
    """Extract sequence 00's payload from the generated raw sequence bank.

    The structural index-table parse is shared with the generator
    (gen_sequence_bank.parse_sequence_bank, the assemble_sound.py
    --sequences big-endian 32-bit layout); packager policy checks -- the US
    entry count and a non-empty sequence 00 -- are layered on top.
    """
    raw = sequences_bin.read_bytes()
    try:
        table = parse_sequence_bank(raw)
    except SequenceBankError as error:
        raise AudioPackageError(
            f"invalid generated sequence bank {sequences_bin}: {error} "
            "(expected the assemble_sound.py TYPE_SEQ layout; regenerate "
            "with tools/saturn/gen_sequence_bank.py)") from error
    if len(table) != 35:
        raise AudioPackageError(
            f"generated sequence bank must carry the 35 US sequences, found "
            f"{len(table)}: {sequences_bin}")
    offset, length = table[0]
    if length == 0:
        raise AudioPackageError(
            f"generated sequence bank entry 00 is empty: {sequences_bin}")
    return raw[offset:offset + length], offset, length


def _load_sequences(root: Path, sequences_bin: Path | None = None) -> list[dict[str, object]]:
    raw = json.loads((root / "sound/sequences.json").read_text(encoding="utf-8"))
    size_pins = _load_m64_size_pins(root)
    entries = []
    seq_dir = root / "sound/sequences/us"
    for seq_key, banks in raw.items():
        if seq_key == "comment":
            continue
        seq_id = int(seq_key[:2], 16)
        name = seq_key
        path = next(seq_dir.glob(f"{name}.m64"), None)
        # Sequence 00 is the source engine's generated sound-player script;
        # its bank/control mapping is still captured, but it has no .m64 file.
        if path is None and seq_id != 0:
            raise AudioPackageError(f"missing extracted sequence asset: {name}.m64")
        source_range = None
        if sequences_bin is not None:
            generated_source = sequences_bin
            if path is None:
                payload, seq0_offset, seq0_length = _extract_generated_seq00(sequences_bin)
                source_range = [seq0_offset, seq0_length]
            else:
                payload = path.read_bytes()
        else:
            generated_source = root / "sound/sequences.bin.inc.c"
            payload = generated_source.read_bytes() if path is None else path.read_bytes()
        # Deliberately shadowed by the size-pin gate and decode walker below:
        # kept so empty/degenerate payloads fail with the historical asset-
        # specific messages instead of a generic pin/walk finding.
        if path is not None and not payload:
            raise AudioPackageError(f"empty extracted sequence asset: {name}.m64")
        if path is not None and (len(payload) < 4 or not any(payload)):
            raise AudioPackageError(f"invalid control flow in extracted sequence asset: {name}.m64")
        if path is not None:
            # Exact-size cross-check against the assets.json pin: the decode
            # walk below only validates the sequence-level prefix (channel/
            # layer bodies are opaque by design), so a truncation landing
            # entirely in the opaque region would otherwise package cleanly.
            pin_key = f"{_M64_PIN_PREFIX}{path.name}"
            pinned_size = size_pins.get(pin_key)
            if pinned_size is None:
                raise AudioPackageError(
                    f"sequence {name} has no assets.json size pin "
                    f"({pin_key}); every extracted US m64 is pinned, so a "
                    "missing pin fails packaging closed")
            if len(payload) != pinned_size:
                raise AudioPackageError(
                    f"sequence {name} size mismatch: on-disk {path.name} is "
                    f"{len(payload)} bytes but assets.json pins "
                    f"{pinned_size} bytes (truncated or modified extraction)")
            # Cheap prefilter retained from the pre-walker heuristic: it
            # costs one linear scan and catches the canonical FF FF target
            # anywhere in the payload, including regions the sequence-level
            # walk below treats as opaque (channel/layer script bodies).
            # The decode walker after it is the packaging authority.
            for index, opcode in enumerate(payload):
                if opcode in (0xFB, 0xFC):
                    # The first 128 bytes of extracted m64s carry setup
                    # commands; scanning from 128 keeps the historical
                    # false-positive-free window.
                    if index < 128:
                        continue
                    if index + 2 >= len(payload):
                        continue
                    target_bytes = payload[index + 1:index + 3]
                    if target_bytes == b"\xff\xff":
                        raise AudioPackageError(f"out-of-range jump/call in {name}.m64")
        if path is None and seq_id == 0 and len(payload) <= 1024:
            raise AudioPackageError("expanded sequence-00 payload is too small")
        # Decode-walk validation (the authority): statically walk the
        # sequence-level script exactly as the 68k sequence VM would decode
        # it; any finding fails packaging closed with the sequence named.
        walk_ok, walk_findings = walk_sequence(payload, FORMAT_US)
        if not walk_ok:
            first = walk_findings[0]
            more = (f" (+{len(walk_findings) - 1} more finding(s))"
                    if len(walk_findings) > 1 else "")
            raise AudioPackageError(
                f"sequence {name} failed decode-walk validation at offset "
                f"0x{first.offset:04x}: {first.kind}: {first.detail}{more}")
        source_path = (generated_source if path is None else path)
        entry = {"id": seq_id, "name": name, "banks": banks,
                 "source": _relative_source(source_path, root),
                 "bytes": len(payload), "sha256": _sha(payload),
                 "control_flow": "source-m64" if path else "source-generated"}
        if source_range is not None:
            entry["source_range"] = source_range
        entries.append(entry)
    if len(entries) != 35 or [x["id"] for x in entries] != list(range(35)):
        raise AudioPackageError("sequence catalog must contain stable IDs 00..22")
    return entries


def _load_banks(root: Path) -> list[dict[str, object]]:
    records = []
    for path in sorted((root / "sound/sound_banks").glob("*.json")):
        value = json.loads(path.read_text(encoding="utf-8"))
        sample_bank = value.get("sample_bank")
        if isinstance(sample_bank, dict):
            sample_bank = sample_bank.get("then") if "then" in sample_bank else None
        records.append({"id": int(path.stem[:2], 16), "name": path.stem,
                        "sample_bank": sample_bank,
                        "source": path.relative_to(root).as_posix(),
                        "sha256": _sha(path.read_bytes()), "metadata": value})
    ids = [x["id"] for x in records]
    if len(set(ids)) != len(ids):
        raise AudioPackageError("duplicate sound-bank IDs")
    if ids != sorted(ids):
        raise AudioPackageError("duplicate or non-canonical bank IDs")
    return records


def _sample_names(bank: dict[str, object]) -> set[str]:
    names: set[str] = set()
    def visit(value: object) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if key in {"sound", "sound_lo", "sound_hi"}:
                    if isinstance(child, str): names.add(child)
                    elif isinstance(child, dict):
                        sample = child.get("sample")
                        if isinstance(sample, str): names.add(sample)
                visit(child)
        elif isinstance(value, list):
            for child in value: visit(child)
    visit(bank["metadata"])
    return names


def _sfx_mappings(banks: list[dict[str, object]]) -> list[dict[str, object]]:
    """Emit stable bank/instrument ordinals for source SFX lookup."""
    mappings = []
    for bank in banks:
        values = bank["metadata"].get("instrument_list", [])
        for sound_id, instrument in enumerate(values):
            if instrument is not None:
                mappings.append({"bank_id": bank["id"], "sound_id": sound_id,
                                 "instrument": instrument})
        if bank["metadata"].get("percussion") is not None:
            mappings.append({"bank_id": bank["id"], "sound_id": 0x7F,
                             "instrument": "percussion"})
    return mappings


def _sample_bindings(banks: list[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    """Retain bank-side tuning/envelope/pan facts without inventing sample loops."""
    bindings: dict[str, list[dict[str, object]]] = {}
    for bank in banks:
        prefix = str(bank["sample_bank"])
        def visit(value: object) -> None:
            if isinstance(value, dict):
                sound = value.get("sound")
                sample_name = sound if isinstance(sound, str) else (
                    sound.get("sample") if isinstance(sound, dict) else None)
                if isinstance(sample_name, str):
                    facts = {key: value[key] for key in
                             ("tuning", "key", "pan", "release_rate", "envelope",
                              "loop_start", "loop_end") if key in value}
                    if isinstance(sound, dict):
                        facts.update({key: sound[key] for key in
                                      ("tuning", "key", "loop_start", "loop_end")
                                      if key in sound})
                    bindings.setdefault(f"{prefix}/{sample_name}", []).append(
                        {"bank": bank["name"], **facts})
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)
        visit(bank["metadata"].get("instruments", bank["metadata"]))
    return bindings


# --- Scene-closure ingestion -------------------------------------------------
# When --closure is passed, the resident bundle is derived from the
# authoritative scene closure emitted by collect_scene_closure.py instead of
# the hardcoded music-only selection.  Join chain: closure sfx_ids ->
# include/sounds.h declarations -> sound/sequences/00_sound_player.s channel
# dyntables (bank/instrument selection) -> sound_banks/*.json instruments ->
# sample records.  Every unresolvable link fails packaging closed naming the
# SFX ID.  The full provenance validation of the closure document itself
# (per-record source hashes) is the generator's job; the packager validates
# the schema tag and the aggregate fields it consumes, and cross-checks the
# aggregates against the record union when records are present.

_SOUND_PLAYER_SOURCE = "sound/sequences/00_sound_player.s"
# Instrument indices >= 0x80 select the sequence engine's synthesized
# waveforms (see the source engine's set_instrument), not a sampled
# instrument; 0x7F selects the bank's percussion set.
_INSTRUMENT_WAVEFORM_BASE = 0x80
_INSTRUMENT_PERCUSSION = 0x7F


def _load_scene_closure(path: Path) -> dict[str, object]:
    if not path.is_file():
        raise AudioPackageError(f"missing scene closure: {path}")
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except ValueError as error:
        raise AudioPackageError(f"invalid scene closure {path}: {error}") from error
    if not isinstance(document, dict) or document.get("schema") != SCENE_CLOSURE_SCHEMA:
        raise AudioPackageError(
            f"unsupported scene closure schema in {path}: expected "
            f"{SCENE_CLOSURE_SCHEMA}")
    level = document.get("level")
    if not isinstance(level, str) or not level:
        raise AudioPackageError(f"scene closure {path} has no level")
    for field in ("sfx_ids", "sfx_banks", "music_sequence_ids"):
        values = document.get(field)
        if (not isinstance(values, list) or
                any(not isinstance(v, str) for v in values) or
                values != sorted(set(values))):
            raise AudioPackageError(
                f"scene closure {path}: {field} must be a sorted unique "
                "string list")
    records = document.get("records")
    if not isinstance(records, list):
        raise AudioPackageError(f"scene closure {path}: records must be a list")
    if records:
        for field in ("sfx_ids", "sfx_banks", "music_sequence_ids"):
            union = sorted({value for record in records
                            for value in record.get(field, [])})
            if union != document[field]:
                raise AudioPackageError(
                    f"scene closure {path}: {field} does not match the "
                    "record union (truncated or hand-edited closure)")
    return document


def _sound_declarations(root: Path) -> tuple[dict[str, list[tuple[str, int]]], dict[str, int]]:
    """Parse include/sounds.h: SOUND_X -> [(SOUND_BANK_Y, soundID)], plus
    SOUND_BANK_Y -> bank number.

    The declaration regex and the one-SOUND_ARG_LOAD-per-ID / bank-name-split
    conventions are reused from collect_scene_closure.py::_sound_declarations
    (:656-663) and its bank lowering (:816); this parser additionally captures
    the soundID argument (include/sounds.h:11 packs bank<<28 | soundID<<16).
    """
    path = root / "include/sounds.h"
    if not path.is_file():
        raise AudioPackageError(
            f"missing {path}: include/sounds.h declarations are required for "
            "closure-driven SFX selection")
    text = path.read_text(encoding="utf-8")
    declarations: dict[str, list[tuple[str, int]]] = {}
    for match in re.finditer(
            r"^\s*#define\s+(SOUND_[A-Z0-9_]+)\b([^\n]*(?:\\\r?\n[^\n]*)*)",
            text, re.M):
        loads = re.findall(
            r"\bSOUND_ARG_LOAD\s*\(\s*(SOUND_BANK_[A-Z0-9_]+)\s*,\s*"
            r"(0[xX][0-9a-fA-F]+|\d+)", match.group(2))
        if loads:
            declarations.setdefault(match.group(1), []).extend(
                (bank, int(sound_id, 0)) for bank, sound_id in loads)
    bank_numbers = {match.group(1): int(match.group(2))
                    for match in re.finditer(
                        r"^\s*#define\s+(SOUND_BANK_[A-Z0-9_]+)\s+(\d+)\s*$",
                        text, re.M)}
    return declarations, bank_numbers


def _music_sequence_numbers(root: Path) -> dict[str, int]:
    """Parse include/seq_ids.h's enum SeqId into symbol -> sequence number."""
    path = root / "include/seq_ids.h"
    if not path.is_file():
        raise AudioPackageError(
            f"missing {path}: include/seq_ids.h is required to map closure "
            "music_sequence_ids to sequence numbers")
    text = path.read_text(encoding="utf-8")
    match = re.search(r"enum\s+SeqId\s*\{(.*?)\}", text, re.S)
    if not match:
        raise AudioPackageError(f"{path}: no enum SeqId block found")
    numbers: dict[str, int] = {}
    value = 0
    for line in match.group(1).splitlines():
        line = re.sub(r"//.*", "", line).strip().rstrip(",")
        if not line:
            continue
        entry = re.fullmatch(r"([A-Za-z_][A-Za-z0-9_]*)(?:\s*=\s*(0[xX][0-9a-fA-F]+|\d+))?", line)
        if not entry:
            raise AudioPackageError(f"{path}: unsupported SeqId enumerator: {line}")
        if entry.group(2) is not None:
            value = int(entry.group(2), 0)
        numbers[entry.group(1)] = value
        value += 1
    return numbers


def _preprocess_player_source(text: str) -> list[str]:
    """Minimal, fail-closed C-preprocessor pass over the sound player source
    mirroring gen_sequence_bank's cpp invocation (SEQUENCE_DEFINES, i.e. the
    US sequence set).  Only the directive forms the committed file uses are
    supported; anything else fails packaging closed."""
    defines = set(SEQUENCE_DEFINES)
    lines: list[str] = []
    stack: list[bool] = []
    for number, line in enumerate(text.splitlines(), start=1):
        stripped = line.strip()
        if stripped.startswith("#"):
            if stripped.startswith("#include"):
                continue
            if stripped.startswith("#ifdef"):
                stack.append(stripped.split()[1] in defines)
                continue
            if stripped.startswith("#ifndef"):
                stack.append(stripped.split()[1] not in defines)
                continue
            if stripped.startswith("#if "):
                terms = re.findall(r"defined\s*\(\s*([A-Za-z0-9_]+)\s*\)", stripped)
                cleaned = re.sub(r"defined\s*\(\s*[A-Za-z0-9_]+\s*\)", "", stripped[4:])
                if not terms or cleaned.replace("||", "").strip():
                    raise AudioPackageError(
                        f"{_SOUND_PLAYER_SOURCE}:{number}: unsupported #if "
                        "expression (only defined(X) || defined(Y) forms)")
                stack.append(any(term in defines for term in terms))
                continue
            if stripped.startswith("#else"):
                if not stack:
                    raise AudioPackageError(
                        f"{_SOUND_PLAYER_SOURCE}:{number}: unmatched #else")
                stack[-1] = not stack[-1]
                continue
            if stripped.startswith("#endif"):
                if not stack:
                    raise AudioPackageError(
                        f"{_SOUND_PLAYER_SOURCE}:{number}: unmatched #endif")
                stack.pop()
                continue
            raise AudioPackageError(
                f"{_SOUND_PLAYER_SOURCE}:{number}: unsupported preprocessor "
                f"directive: {stripped.split()[0]}")
        if all(stack):
            lines.append(line)
    if stack:
        raise AudioPackageError(
            f"{_SOUND_PLAYER_SOURCE}: unterminated conditional block")
    return lines


def _parse_sound_player(root: Path) -> tuple[dict[str, list[str]], list[str], dict[int, str]]:
    """Parse the (preprocessed) sound player into labeled blocks, the label
    order (for layer fall-through), and the SFX channel map."""
    path = root / _SOUND_PLAYER_SOURCE
    if not path.is_file():
        raise AudioPackageError(
            f"missing {path}: the committed sound player source is required "
            "for closure-driven SFX selection")
    blocks: dict[str, list[str]] = {}
    order: list[str] = []
    prelude: list[str] = []
    current: str | None = None
    for line in _preprocess_player_source(path.read_text(encoding="utf-8")):
        line = re.sub(r"//.*", "", line).strip()
        if not line:
            continue
        label = re.fullmatch(r"(\.[A-Za-z0-9_]+):", line)
        if label:
            current = label.group(1)
            blocks[current] = []
            order.append(current)
            continue
        (blocks[current] if current is not None else prelude).append(line)
    channels: dict[int, str] = {}
    for line in prelude:
        match = re.fullmatch(r"seq_startchannel\s+(\d+)\s*,\s*(\.[A-Za-z0-9_]+)", line)
        if match:
            channels[int(match.group(1))] = match.group(2)
    if not channels:
        raise AudioPackageError(
            f"{_SOUND_PLAYER_SOURCE}: no seq_startchannel channel map found")
    return blocks, order, channels


def _layer_instruments(blocks: dict[str, list[str]], order: list[str],
                       label: str, seen: set[str]) -> set[int]:
    """Collect layer_setinstr values reachable from a layer entry, following
    fall-through into the next labeled block plus layer_jump/layer_call."""
    found: set[int] = set()
    while label in blocks and label not in seen:
        seen.add(label)
        ended = False
        for line in blocks[label]:
            match = re.fullmatch(r"layer_setinstr\s+(\d+)", line)
            if match:
                found.add(int(match.group(1)))
                continue
            match = re.match(r"layer_(?:jump|call)\s+(\.[A-Za-z0-9_]+)", line)
            if match:
                found |= _layer_instruments(blocks, order, match.group(1), seen)
                continue
            if re.match(r"layer_(?:end|ret)\b", line):
                ended = True
        if ended:
            break
        index = order.index(label) + 1
        if index >= len(order):
            break
        label = order[index]
    return found


def _walk_sound_subroutine(blocks: dict[str, list[str]], order: list[str],
                           label: str, sfx_id: str,
                           bank: int | None = None,
                           seen: set[tuple[str, int | None]] | None = None
                           ) -> tuple[set[tuple[int, int]], set[int]]:
    """Walk one sound subroutine collecting (bank ordinal, instrument index)
    pairs (positional pairing: each instrument selection binds to the most
    recent chan_setbank) plus synthesized-waveform indices.  chan_jump and
    the conditional branches are followed as a reachability union; a
    chan_call target that mutates instrument state, or any dynamically
    dispatched control flow, fails closed.  The visited set is keyed by
    (label, bank state) so a shared block revisited under a different bank
    still contributes its pairs; termination stays bounded by the finite
    label and bank-ordinal domains."""
    seen = seen if seen is not None else set()
    pairs: set[tuple[int, int]] = set()
    waves: set[int] = set()
    while label is not None and (label, bank) not in seen:
        seen.add((label, bank))
        if label not in blocks:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: sound player label {label} is undefined")
        next_label = None
        for line in blocks[label]:
            match = re.fullmatch(r"chan_setbank\s+(\d+)", line)
            if match:
                bank = int(match.group(1))
                continue
            match = re.fullmatch(r"chan_setinstr\s+(\d+)", line)
            if match:
                instrument = int(match.group(1))
                if instrument >= _INSTRUMENT_WAVEFORM_BASE:
                    waves.add(instrument)
                elif bank is None:
                    raise AudioPackageError(
                        f"closure SFX {sfx_id}: chan_setinstr {instrument} in "
                        f"{label} has no preceding chan_setbank")
                else:
                    pairs.add((bank, instrument))
                continue
            match = re.match(r"chan_setlayer\s+\d+\s*,\s*(\.[A-Za-z0-9_]+)", line)
            if match:
                for instrument in _layer_instruments(blocks, order, match.group(1), set()):
                    if instrument >= _INSTRUMENT_WAVEFORM_BASE:
                        waves.add(instrument)
                    elif bank is None:
                        raise AudioPackageError(
                            f"closure SFX {sfx_id}: layer_setinstr {instrument} "
                            f"reached from {label} has no bank in effect")
                    else:
                        pairs.add((bank, instrument))
                continue
            match = re.fullmatch(r"chan_jump\s+(\.[A-Za-z0-9_]+)", line)
            if match:
                next_label = match.group(1)
                break
            match = re.match(r"chan_(?:beqz|bltz|bgez)\s+(\.[A-Za-z0-9_]+)", line)
            if match:
                branch_pairs, branch_waves = _walk_sound_subroutine(
                    blocks, order, match.group(1), sfx_id, bank, seen)
                pairs |= branch_pairs
                waves |= branch_waves
                continue
            match = re.fullmatch(r"chan_call\s+(\.[A-Za-z0-9_]+)", line)
            if match:
                for called in blocks.get(match.group(1), []):
                    if re.match(r"chan_set(?:bank|instr|layer)\b", called):
                        raise AudioPackageError(
                            f"closure SFX {sfx_id}: chan_call target "
                            f"{match.group(1)} mutates instrument state")
                continue
            if re.match(r"chan_dyncall\b", line):
                raise AudioPackageError(
                    f"closure SFX {sfx_id}: dynamically dispatched control "
                    f"flow (chan_dyncall) in {label} cannot be resolved")
        label = next_label
    return pairs, waves


def _resolve_closure_sfx(root: Path, closure: dict[str, object],
                         banks: list[dict[str, object]]
                         ) -> dict[str, list[dict[str, object]]]:
    """Resolve every closure sfx_id to its bank/instrument chain entries.
    Fails closed, naming the SFX ID, on any unresolvable link."""
    declarations, bank_numbers = _sound_declarations(root)
    blocks, order, channels = _parse_sound_player(root)
    seq00_banks = json.loads(
        (root / "sound/sequences.json").read_text(encoding="utf-8")).get(
            "00_sound_player")
    if not isinstance(seq00_banks, list) or not seq00_banks:
        raise AudioPackageError(
            "sound/sequences.json has no 00_sound_player bank list")
    bank_by_stem = {str(bank["name"]): bank for bank in banks}
    resolution: dict[str, list[dict[str, object]]] = {}
    derived_banks: set[str] = set()
    for sfx_id in closure["sfx_ids"]:
        loads = declarations.get(sfx_id)
        if not loads:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: no SOUND_ARG_LOAD declaration in "
                "include/sounds.h")
        if len(loads) != 1:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: ambiguous SOUND_ARG_LOAD declaration")
        bank_symbol, sound_id = loads[0]
        derived_banks.add(bank_symbol[len("SOUND_BANK_"):].lower())
        bank_number = bank_numbers.get(bank_symbol)
        if bank_number is None:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: {bank_symbol} has no numeric define")
        channel = channels.get(bank_number)
        if channel is None:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: sound player has no channel for "
                f"bank {bank_number}")
        table = next((match.group(1) for line in blocks[channel]
                      for match in [re.match(r"chan_setdyntable\s+(\.[A-Za-z0-9_]+)", line)]
                      if match), None)
        if table is None or table not in blocks:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: channel {channel} has no dyntable")
        entries = [match.group(1) if match else None
                   for line in blocks[table]
                   for match in [re.fullmatch(r"sound_ref\s+(\.[A-Za-z0-9_]+)", line)]]
        if sound_id >= len(entries) or entries[sound_id] is None:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: sound ID 0x{sound_id:02x} is outside "
                f"the {table} dyntable ({len(entries)} entries)")
        pairs, waves = _walk_sound_subroutine(blocks, order, entries[sound_id], sfx_id)
        chain: list[dict[str, object]] = []
        for ordinal, index in sorted(pairs):
            if ordinal >= len(seq00_banks):
                raise AudioPackageError(
                    f"closure SFX {sfx_id}: chan_setbank {ordinal} is outside "
                    "the 00_sound_player bank list")
            stem = str(seq00_banks[ordinal])
            bank = bank_by_stem.get(stem)
            if bank is None:
                raise AudioPackageError(
                    f"closure SFX {sfx_id}: instrument bank {stem} has no "
                    "sound_banks record")
            if index == _INSTRUMENT_PERCUSSION:
                if bank["metadata"].get("percussion") is None:
                    raise AudioPackageError(
                        f"closure SFX {sfx_id}: bank {stem} has no percussion")
                chain.append({"bank": stem, "instrument_index": index,
                              "instrument": "percussion"})
                continue
            instruments = bank["metadata"].get("instrument_list", [])
            name = instruments[index] if index < len(instruments) else None
            if not isinstance(name, str):
                raise AudioPackageError(
                    f"closure SFX {sfx_id}: instrument {index} is not defined "
                    f"in bank {stem}")
            chain.append({"bank": stem, "instrument_index": index,
                          "instrument": name})
        for wave in sorted(waves):
            chain.append({"waveform": wave})
        if not chain:
            raise AudioPackageError(
                f"closure SFX {sfx_id}: no instrument selection is reachable "
                f"from {entries[sound_id]}")
        resolution[sfx_id] = chain
    declared = set(closure["sfx_banks"])
    if derived_banks != declared:
        difference = sorted(declared ^ derived_banks)
        raise AudioPackageError(
            "closure sfx_banks disagree with the include/sounds.h "
            f"derivation: {', '.join(difference)}")
    return resolution


def _instrument_chain_sample_names(bank: dict[str, object], index: int,
                                   instrument: str) -> set[str]:
    """Sample names referenced by one resolved instrument (or the percussion
    set) of a bank, mirroring _sample_names' sound/sound_lo/sound_hi walk."""
    if instrument == "percussion" and index == _INSTRUMENT_PERCUSSION:
        scope: object = bank["metadata"].get("percussion")
        percussions = bank["metadata"].get("percussions")
        if isinstance(scope, str) and isinstance(percussions, dict):
            scope = percussions.get(scope)
    else:
        scope = bank["metadata"].get("instruments", {}).get(instrument)
    names: set[str] = set()

    def visit(value: object) -> None:
        if isinstance(value, dict):
            for key, child in value.items():
                if key in {"sound", "sound_lo", "sound_hi"}:
                    if isinstance(child, str):
                        names.add(child)
                    elif isinstance(child, dict):
                        sample = child.get("sample")
                        if isinstance(sample, str):
                            names.add(sample)
                visit(child)
        elif isinstance(value, list):
            for child in value:
                visit(child)
    visit(scope)
    return names


def _decimate_sfx_sample(sample: Sample) -> Sample:
    """Halve one closure-mode SFX sample's rate by simple 2:1 PCM decimation
    (drop every other frame), applied after AIFF decode and before
    packaging.  Coarse and intentional -- real SCSP hardware commonly ran
    one-shot effects well below 44.1kHz -- not a filtered/bandlimited
    resample.  The halved rate is recorded on the returned Sample so the
    manifest's "rate" field (the field a future SCSP voice allocator reads
    for playback pitch) stays truthful about what was actually packaged."""
    pcm8 = bytes(sample.pcm8[::2])
    return Sample(sample.stable_id, sample.source, sample.source_sha256,
                  sample.rate // 2, len(pcm8), pcm8, sample.aiff_metadata)


def _closure_sfx_decimation_targets(sequences: list[dict[str, object]],
                                    banks: list[dict[str, object]],
                                    sfx_resolution: dict[str, list[dict[str, object]]],
                                    closure_music_ids: list[int]) -> set[str]:
    """Sample identities (the same "sample_bank/name" form as Sample.stable_id)
    eligible for closure-mode SFX rate halving: reachable only through a
    resolved SFX chain, and never through any scene's whole-bank music
    inclusion -- the closure-driven scene's own music banks, or the [3]
    hardcoded fallback every non-closure scene keeps (mirroring the
    fallback selection at compile_catalog's closures loop).  Music samples
    are protected even if a future closure's SFX chain happens to name the
    same bank; ambiguous names are simply left undecimated (fail safe)."""
    bank_by_name = {str(bank["name"]): bank for bank in banks}

    def music_bank_names(sequence_ids: list[int]) -> set[str]:
        seqs = [seq for seq in sequences if seq["id"] in sequence_ids]
        return {bank for seq in seqs for bank in seq["banks"] if isinstance(bank, str)}

    protected: set[str] = set()
    for name in music_bank_names([3]) | music_bank_names(closure_music_ids):
        bank = bank_by_name[name]
        prefix = str(bank["sample_bank"])
        protected |= {f"{prefix}/{sample_name}" for sample_name in _sample_names(bank)}

    eligible: set[str] = set()
    for chain in sfx_resolution.values():
        for entry in chain:
            if "bank" not in entry:
                continue  # synthesized waveform: no sample dependency
            bank = bank_by_name[str(entry["bank"])]
            prefix = str(bank["sample_bank"])
            index = int(entry["instrument_index"])
            names = _instrument_chain_sample_names(bank, index, str(entry["instrument"]))
            eligible |= {f"{prefix}/{sample_name}" for sample_name in names}
    return eligible - protected


def _sequence_payload(root: Path, sequence: dict[str, object]) -> bytes:
    """Read one catalog sequence's script bytes; generated seq00 is a slice
    of the standalone sequence bank addressed by its recorded source_range."""
    data = (root / str(sequence["source"])).read_bytes()
    source_range = sequence.get("source_range")
    if source_range is not None:
        start, length = source_range
        data = data[start:start + length]
        if len(data) != length:
            raise AudioPackageError(
                f"sequence {sequence['name']} source_range is out of range")
    return data


def _closure(name: str, sequence_ids: list[int], sequences: list[dict[str, object]],
             banks: list[dict[str, object]], samples: list[Sample], root: Path,
             sfx_resolution: dict[str, list[dict[str, object]]] | None = None,
             closure_provenance: dict[str, str] | None = None) -> dict[str, object]:
    seqs = [x for x in sequences if x["id"] in sequence_ids]
    music_banks = sorted({bank for seq in seqs for bank in seq["banks"] if isinstance(bank, str)})
    # Closure-driven SFX selection: banks/instruments reached by the resolved
    # chains contribute only their chain samples; music banks stay whole-bank
    # (their m64 bodies are opaque by policy, so any instrument may be used).
    sfx_allowed: dict[str, set[str]] = {}
    sfx_pairs: set[tuple[str, int]] = set()
    if sfx_resolution is not None:
        bank_by_stem = {str(bank["name"]): bank for bank in banks}
        for chain in sfx_resolution.values():
            for entry in chain:
                if "bank" not in entry:
                    continue  # synthesized waveform: no sample dependency
                stem = str(entry["bank"])
                index = int(entry["instrument_index"])
                sfx_pairs.add((stem, index))
                sfx_allowed.setdefault(stem, set()).update(
                    _instrument_chain_sample_names(
                        bank_by_stem[stem], index, str(entry["instrument"])))
    bank_names = sorted(set(music_banks) | set(sfx_allowed))
    selected = [bank for bank in banks if bank["name"] in bank_names]
    sample_map = {sample.stable_id: sample for sample in samples}
    selected_samples = []
    for bank in selected:
        sample_bank = str(bank["sample_bank"])
        chain_names = sfx_allowed.get(str(bank["name"]), set())
        allowed = set(chain_names)
        if sfx_resolution is None or bank["name"] in music_banks:
            allowed |= _sample_names(bank)
        for sample_name in sorted(allowed):
            sample = sample_map.get(f"{sample_bank}/{sample_name}")
            if sample is None:
                if sample_name in chain_names:
                    raise AudioPackageError(
                        f"{name} closure: SFX chain sample "
                        f"{sample_bank}/{sample_name} has no extracted AIFF")
                continue
            if sample not in selected_samples:
                selected_samples.append(sample)
    pcm_bytes = sum(len(sample.pcm8) for sample in selected_samples)
    metadata_bytes = sum(len(_canonical(bank["metadata"])) for bank in selected)
    resident_bytes = _align(pcm_bytes) + _align(metadata_bytes)
    if resident_bytes > RESIDENT_LIMIT:
        raise AudioPackageError(f"{name} resident closure exceeds {RESIDENT_LIMIT}: {resident_bytes}")
    if sfx_resolution is None:
        mappings = [mapping for mapping in _sfx_mappings(selected)]
    else:
        stem_by_id = {bank["id"]: str(bank["name"]) for bank in selected}
        mappings = [mapping for mapping in _sfx_mappings(selected)
                    if stem_by_id[mapping["bank_id"]] in music_banks or
                    (stem_by_id[mapping["bank_id"]], mapping["sound_id"]) in sfx_pairs]
    chunk_hashes = []
    identity = hashlib.sha256()
    def add_chunk(kind: str, stable_id: str, payload: bytes) -> None:
        identity.update(kind.encode("ascii"))
        identity.update(struct.pack(">I", len(payload)))
        identity.update(payload)
        chunk_hashes.append({"kind": kind, "id": stable_id,
                             "bytes": len(payload), "sha256": _sha(payload)})
    for sequence in seqs:
        add_chunk("SEQU", str(sequence["id"]), _sequence_payload(root, sequence))
    for bank in selected:
        add_chunk("BANK", str(bank["name"]), _canonical(bank["metadata"]))
    for sample in selected_samples:
        add_chunk("SAMP", sample.stable_id, sample.pcm8)
    record: dict[str, object] = {
        "scene": name, "generation": 1, "sequence_ids": sequence_ids,
        "bank_names": bank_names,
        "sample_ids": [sample.stable_id for sample in selected_samples],
        "sfx_mappings": mappings,
        # WF has no generated scene closure yet, so its bundle keeps the
        # hardcoded music-only selection; the marker records the asymmetry.
        "selection": ("scene-closure-v1" if sfx_resolution is not None
                      else "hardcoded-music-fallback")}
    if sfx_resolution is not None:
        record["sfx_resolution"] = sfx_resolution
        record.update(closure_provenance or {})
    # The dependency digest covers framed source bytes, not merely IDs or a
    # metadata summary.  Any selected sequence, bank, or sample mutation must
    # therefore invalidate the scene root.
    identity.update(_canonical(record))
    return {**record, "chunk_hashes": chunk_hashes,
            "payload_sha256": identity.hexdigest(),
            "resident_bytes": resident_bytes, "resident_limit": RESIDENT_LIMIT,
            "active_generation_eviction": "rejected", "post_boot_sound_ram_clear": "rejected"}


def compile_catalog(root: Path, output: Path, manifest_output: Path | None = None,
                    sequences_bin: Path | None = None,
                    scene_closure: Path | None = None) -> dict[str, object]:
    files, source_sha = source_inventory(root, sequences_bin)
    sequences = _load_sequences(root, sequences_bin)
    banks = _load_banks(root)
    closure_scene = None
    closure_music_ids: list[int] = []
    sfx_resolution = None
    closure_provenance = None
    if scene_closure is not None:
        closure_document = _load_scene_closure(scene_closure)
        closure_scene = str(closure_document["level"])
        if closure_scene not in ("bob", "wf"):
            raise AudioPackageError(
                f"scene closure level {closure_scene} has no resident bundle "
                "(expected bob or wf)")
        sequence_numbers = _music_sequence_numbers(root)
        known_ids = {sequence["id"] for sequence in sequences}
        for symbol in closure_document["music_sequence_ids"]:
            number = sequence_numbers.get(symbol)
            if number is None:
                raise AudioPackageError(
                    f"closure music sequence {symbol} is not declared in "
                    "include/seq_ids.h")
            if number not in known_ids:
                raise AudioPackageError(
                    f"closure music sequence {symbol} (id {number}) is not in "
                    "the sequence catalog")
            closure_music_ids.append(number)
        closure_music_ids = sorted(set(closure_music_ids))
        sfx_resolution = _resolve_closure_sfx(root, closure_document, banks)
        closure_provenance = {
            "closure_source": _relative_source(scene_closure, root),
            "closure_sha256": _sha(scene_closure.read_bytes())}
    # Closure-mode SFX-only rate halving (owner-approved, 2026-08-09 session):
    # the real BOB closure needs 679,936 resident bytes at full rate against
    # the 491,520-byte RESIDENT_LIMIT.  2:1-decimating the SFX-only samples
    # (never music) closes the gap with real margin; see
    # _closure_sfx_decimation_targets for the exact eligibility rule.  Stays
    # empty -- and this compile byte-identical to before -- whenever no
    # closure is supplied.
    sfx_decimation_targets: set[str] = (
        _closure_sfx_decimation_targets(sequences, banks, sfx_resolution, closure_music_ids)
        if sfx_resolution is not None else set())
    # Keep generated manifests checkout-portable.  parse_aiff is intentionally
    # usable on an arbitrary path for the small unit test, but package records
    # must never embed an absolute developer checkout path.
    samples = []
    for path in sorted((root / "sound/samples").glob("**/*.aiff")):
        parsed = parse_aiff(path)
        sample = Sample(parsed.stable_id, path.relative_to(root).as_posix(),
                        parsed.source_sha256, parsed.rate, parsed.frames,
                        parsed.pcm8, parsed.aiff_metadata)
        if sample.stable_id in sfx_decimation_targets:
            sample = _decimate_sfx_sample(sample)
        samples.append(sample)
    sample_bindings = _sample_bindings(banks)
    sample_records = [{"id": s.stable_id, "source": s.source, "sha256": s.source_sha256,
                       "rate": s.rate, "frames": s.frames, "pcm8_bytes": len(s.pcm8),
                       "loop_start": None, "loop_end": None, "root_key": None,
                       "tuning": None, "loop_source": "bank-metadata",
                       "aiff_metadata": s.aiff_metadata,
                       "bank_bindings": sample_bindings.get(s.stable_id, [])} for s in samples]
    sfx_mappings = _sfx_mappings(banks)
    closures = {}
    for scene in ("bob", "wf"):
        if scene == closure_scene:
            closures[scene] = _closure(scene, closure_music_ids, sequences,
                                       banks, samples, root,
                                       sfx_resolution=sfx_resolution,
                                       closure_provenance=closure_provenance)
        else:
            # Hardcoded music-only fallback: kept only for scenes without a
            # generated closure (currently WF); recorded in the manifest.
            closures[scene] = _closure(scene, [3], sequences, banks, samples, root)
    chunks: list[tuple[bytes, bytes]] = []
    chunks.append((b"META", _canonical({"schema": "S64A", "version": VERSION,
                                         "source_sha256": source_sha, "sequences": sequences,
                                         "banks": banks, "samples": sample_records,
                                         "sfx_mappings": sfx_mappings,
                                         "closures": closures})))
    for seq in sequences:
        payload = b"" if seq["source"] is None else _sequence_payload(root, seq)
        chunks.append((b"SEQU", payload))
    for bank in banks:
        chunks.append((b"BANK", _canonical(bank["metadata"])))
    sample_payload = b"".join(sample.pcm8 for sample in samples)
    chunks.append((b"SAMP", sample_payload))
    for scene in ("bob", "wf"):
        chunks.append((b"CLOS", _canonical(closures[scene])))
    chunk_count = len(chunks)
    cursor = HEADER_SIZE + chunk_count * CHUNK_SIZE
    descriptors: list[bytes] = []
    payload = bytearray()
    for kind, data in chunks:
        offset = _align(cursor)
        payload.extend(bytes(offset - cursor))
        descriptors.append(CHUNK.pack(kind, offset, len(data), CHUNK_ALIGNMENT, 0,
                                       0, bytes.fromhex(_sha(data))))
        payload.extend(data)
        cursor = offset + len(data)
    package_size = HEADER_SIZE + len(descriptors) * CHUNK_SIZE + len(payload)
    header = HEADER.pack(MAGIC, VERSION, HEADER_SIZE, package_size, len(sequences),
                         len(banks), len(samples), chunk_count, 0,
                         bytes.fromhex(source_sha), bytes(32))
    package = bytearray(header + b"".join(descriptors) + payload)
    digest = hashlib.sha256(package).digest()
    package[PACKAGE_HASH_OFFSET:PACKAGE_HASH_OFFSET + 32] = digest
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    result = {"schema": "sm64-saturn-audio-manifest-v1", "format": "S64A",
              "version": VERSION, "package_size": len(package), "package_sha256": digest.hex(),
              "source_sha256": source_sha, "sequence_count": len(sequences),
              "bank_count": len(banks), "sample_count": len(samples), "chunk_count": chunk_count,
              "samples": sample_records,
              "sfx_mappings": sfx_mappings,
              "s64p_audio_dependencies": [
                  {"scene": scene, "root": f"audio/{scene}",
                   "generation": closures[scene]["generation"],
                   "stable_id": f"audio/{scene}",
                   "content_sha256": closures[scene]["payload_sha256"],
                   "chunk_hashes": closures[scene]["chunk_hashes"]}
                  for scene in ("bob", "wf")],
              "source_inventory": [{"path": p.relative_to(root).as_posix(), "sha256": _sha(p.read_bytes())} for p in files],
              "closure_selection": {scene: closures[scene]["selection"]
                                    for scene in ("bob", "wf")},
              "closures": closures}
    if manifest_output:
        manifest_output.parent.mkdir(parents=True, exist_ok=True)
        manifest_output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return result


def validate_audio_dependency(manifest: dict[str, object], dependency: dict[str, object]) -> bool:
    """Validate an S64P AUDIO_DEPENDENCIES binding against this catalog."""
    if not isinstance(manifest, dict) or not isinstance(dependency, dict):
        return False
    for candidate in manifest.get("s64p_audio_dependencies", []):
        if (candidate.get("scene") == dependency.get("scene") and
                candidate.get("generation") == dependency.get("generation") and
                candidate.get("stable_id") == dependency.get("stable_id") and
                candidate.get("content_sha256") == dependency.get("content_sha256")):
            return True
    return False


def main() -> None:
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--sequences-bin", type=Path, default=None,
                        help="generated raw sequence bank from "
                             "tools/saturn/gen_sequence_bank.py; when absent, "
                             "falls back to sound/sequences.bin.inc.c")
    parser.add_argument("--closure", type=Path, default=None,
                        help="scene closure JSON from "
                             "tools/saturn/collect_scene_closure.py; drives "
                             "that scene's resident bundle selection (other "
                             "scenes keep the hardcoded music-only fallback)")
    args = parser.parse_args()
    print(json.dumps(compile_catalog(args.root.resolve(), args.output, args.manifest,
                                     args.sequences_bin, args.closure), sort_keys=True))


if __name__ == "__main__":
    main()
