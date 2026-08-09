#!/usr/bin/env python3
"""Deterministic S64A v1 catalog compiler.

The compiler consumes the repository's user-extracted sound tree directly.  It
never invokes the PC build: source JSON, m64 and AIFF identities are hashed
before packaging and a missing input is a hard error.  Sample PCM is reduced to
signed Saturn PCM8; all sequence/bank metadata is retained as canonical
JSON so tuning, envelopes and control-flow bytes remain inspectable.
"""
from __future__ import annotations

import aifc
import hashlib
import json
import math
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from gen_sequence_bank import SequenceBankError, parse_sequence_bank
from m64_decode_walk import FORMAT_US, walk_sequence

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
        if path is not None and not payload:
            raise AudioPackageError(f"empty extracted sequence asset: {name}.m64")
        if path is not None and (len(payload) < 4 or not any(payload)):
            raise AudioPackageError(f"invalid control flow in extracted sequence asset: {name}.m64")
        if path is not None:
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
             banks: list[dict[str, object]], samples: list[Sample], root: Path) -> dict[str, object]:
    seqs = [x for x in sequences if x["id"] in sequence_ids]
    bank_names = sorted({bank for seq in seqs for bank in seq["banks"] if isinstance(bank, str)})
    selected = [bank for bank in banks if bank["name"] in bank_names]
    sample_map = {sample.stable_id: sample for sample in samples}
    selected_samples = []
    for bank in selected:
        sample_bank = str(bank["sample_bank"])
        for sample_name in sorted(_sample_names(bank)):
            sample = sample_map.get(f"{sample_bank}/{sample_name}")
            if sample is not None and sample not in selected_samples:
                selected_samples.append(sample)
    pcm_bytes = sum(len(sample.pcm8) for sample in selected_samples)
    metadata_bytes = sum(len(_canonical(bank["metadata"])) for bank in selected)
    resident_bytes = _align(pcm_bytes) + _align(metadata_bytes)
    if resident_bytes > RESIDENT_LIMIT:
        raise AudioPackageError(f"{name} resident closure exceeds {RESIDENT_LIMIT}: {resident_bytes}")
    mappings = [mapping for mapping in _sfx_mappings(selected)]
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
    # The dependency digest covers framed source bytes, not merely IDs or a
    # metadata summary.  Any selected sequence, bank, or sample mutation must
    # therefore invalidate the scene root.
    identity.update(_canonical({"scene": name, "generation": 1,
                                "sequence_ids": sequence_ids,
                                "bank_names": bank_names,
                                "sample_ids": [sample.stable_id for sample in selected_samples],
                                "sfx_mappings": mappings}))
    return {"scene": name, "generation": 1, "sequence_ids": sequence_ids,
            "bank_names": bank_names,
            "sample_ids": [sample.stable_id for sample in selected_samples],
            "sfx_mappings": mappings, "chunk_hashes": chunk_hashes,
            "payload_sha256": identity.hexdigest(),
            "resident_bytes": resident_bytes, "resident_limit": RESIDENT_LIMIT,
            "active_generation_eviction": "rejected", "post_boot_sound_ram_clear": "rejected"}


def compile_catalog(root: Path, output: Path, manifest_output: Path | None = None,
                    sequences_bin: Path | None = None) -> dict[str, object]:
    files, source_sha = source_inventory(root, sequences_bin)
    sequences = _load_sequences(root, sequences_bin)
    banks = _load_banks(root)
    # Keep generated manifests checkout-portable.  parse_aiff is intentionally
    # usable on an arbitrary path for the small unit test, but package records
    # must never embed an absolute developer checkout path.
    samples = []
    for path in sorted((root / "sound/samples").glob("**/*.aiff")):
        parsed = parse_aiff(path)
        samples.append(Sample(parsed.stable_id, path.relative_to(root).as_posix(),
                              parsed.source_sha256, parsed.rate, parsed.frames,
                              parsed.pcm8, parsed.aiff_metadata))
    sample_bindings = _sample_bindings(banks)
    sample_records = [{"id": s.stable_id, "source": s.source, "sha256": s.source_sha256,
                       "rate": s.rate, "frames": s.frames, "pcm8_bytes": len(s.pcm8),
                       "loop_start": None, "loop_end": None, "root_key": None,
                       "tuning": None, "loop_source": "bank-metadata",
                       "aiff_metadata": s.aiff_metadata,
                       "bank_bindings": sample_bindings.get(s.stable_id, [])} for s in samples]
    sfx_mappings = _sfx_mappings(banks)
    closures = {"bob": _closure("bob", [3], sequences, banks, samples, root),
                "wf": _closure("wf", [3], sequences, banks, samples, root)}
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
    args = parser.parse_args()
    print(json.dumps(compile_catalog(args.root.resolve(), args.output, args.manifest,
                                     args.sequences_bin), sort_keys=True))


if __name__ == "__main__":
    main()
