"""Compile the closure-selected BOB SFX subset for sourceboot sound RAM.

The normal sourceboot image cannot carry the project's whole S64A catalog in
its fixed 32-Mbit cart.  This compiler instead materializes only the sound
effects proved reachable by the selected scene closure.  Its two outputs are
pointer-free big-endian bytes: a small MC68000 mapping table for sound-RAM's
reserved metadata window, and the corresponding signed PCM8 payload for the
SCSP bank.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

from saturn_audio_package import (
    AudioPackageError,
    _closure_sfx_decimation_targets,
    _instrument_chain_sample_names,
    _load_banks,
    _load_sequences,
    _decimate_sfx_sample,
    parse_aiff,
)
from write_if_changed import (write_bytes_if_changed,
                             write_text_if_changed)


BUNDLE_MAGIC = 0x53465842  # "SFXB"
BUNDLE_VERSION = 1
BUNDLE_HEADER_BYTES = 32
BUNDLE_MAPPING_BYTES = 8
BUNDLE_SAMPLE_BYTES = 12
SOUND_RAM_RESERVE_OFFSET = 0x5000
SOUND_RAM_PCM_OFFSET = 0x8000
SOUND_RAM_BYTES = 0x80000
SOUND_RAM_RESERVE_BYTES = SOUND_RAM_PCM_OFFSET - SOUND_RAM_RESERVE_OFFSET
# Mirrors SM64_SATURN_PCM_SAMPLE_LOOP (src/port/saturn/audio68k/pcm_voice.h):
# the only sample-row flag bit the MC68000 bundle validator accepts.  The
# SCSP keys its hardware gapless loop from this bit (scsp_pcm8.c).
SAMPLE_FLAG_LOOP = 0x0001
MUSIC_SAMPLE_ID = "music/looped-pcm8"
MUSIC_DEFAULT_VOLUME = 15
MUSIC_DEFAULT_RATE = 8000


class SourcebootSfxBundleError(ValueError):
    """The selected audio closure cannot produce a safe sourceboot bundle."""


@dataclass(frozen=True)
class BundleSample:
    stable_id: str
    offset: int
    sample_count: int
    rate: int
    default_volume: int = 15
    flags: int = 0


@dataclass(frozen=True)
class BundleMapping:
    sound_id: str
    sound_bits: int
    first_sample: int
    sample_count: int


@dataclass(frozen=True)
class SourcebootSfxBundle:
    generation: int
    metadata: bytes
    pcm: bytes
    samples: tuple[BundleSample, ...]
    mappings: tuple[BundleMapping, ...]
    music_sequence_offset: int
    music_sequence_bytes: int
    music_sample_index: int
    music_sample_id: str

    @property
    def metadata_bytes(self) -> int:
        return len(self.metadata)

    @property
    def mapping_count(self) -> int:
        return len(self.mappings)

    @property
    def sample_count(self) -> int:
        return len(self.samples)

    def mapping_for(self, sound_id: str) -> BundleMapping:
        for mapping in self.mappings:
            if mapping.sound_id == sound_id:
                return mapping
        raise KeyError(sound_id)


def _u16(value: int) -> bytes:
    if not 0 <= value <= 0xFFFF:
        raise SourcebootSfxBundleError(f"u16 out of range: {value}")
    return value.to_bytes(2, "big")


def _u32(value: int) -> bytes:
    if not 0 <= value <= 0xFFFFFFFF:
        raise SourcebootSfxBundleError(f"u32 out of range: {value}")
    return value.to_bytes(4, "big")


def _align(value: int, alignment: int) -> int:
    if alignment <= 0 or alignment & (alignment - 1):
        raise SourcebootSfxBundleError("alignment must be a power of two")
    return (value + alignment - 1) & -alignment


def _load_closure(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise SourcebootSfxBundleError(f"invalid audio closure {path}: {error}") from error
    if not isinstance(document, dict):
        raise SourcebootSfxBundleError("audio closure must be an object")
    if document.get("scene") != "bob" or document.get("selection") != "scene-closure-v1":
        raise SourcebootSfxBundleError("sourceboot SFX compiler requires the selected BOB closure")
    if not isinstance(document.get("generation"), int) or not 0 < document["generation"] <= 0xFFFF:
        raise SourcebootSfxBundleError("audio closure has invalid generation")
    for field in ("sample_ids", "sequence_ids"):
        if not isinstance(document.get(field), list) or not document[field]:
            raise SourcebootSfxBundleError(f"audio closure has no {field}")
    resolution = document.get("sfx_resolution")
    if not isinstance(resolution, dict) or not resolution:
        raise SourcebootSfxBundleError("audio closure has no SFX resolution table")
    return document


def _sound_bits_by_symbol(root: Path, selected: set[str]) -> dict[str, int]:
    path = root / "include" / "sounds.h"
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as error:
        raise SourcebootSfxBundleError(f"missing sound declarations: {path}") from error
    found: dict[str, int] = {}
    for match in re.finditer(
        r"^\s*#define\s+(SOUND_[A-Z0-9_]+)\b[^\n]*?/\*\s*"
        r"(0[xX][0-9a-fA-F]+)\s*\*/",
        text,
        re.MULTILINE,
    ):
        symbol = match.group(1)
        if symbol not in selected:
            continue
        value = int(match.group(2), 0)
        if symbol in found or value > 0xFFFFFFFF:
            raise SourcebootSfxBundleError(f"ambiguous sound-bit declaration: {symbol}")
        found[symbol] = value
    missing = sorted(selected - found.keys())
    if missing:
        raise SourcebootSfxBundleError(
            "selected SFX lack an exact numeric SOUND_ARG_LOAD declaration: "
            + ", ".join(missing)
        )
    if len(set(found.values())) != len(found):
        raise SourcebootSfxBundleError("selected SFX have duplicate sound-bit identities")
    return found


def _sample_path(root: Path, stable_id: str) -> Path:
    parts = stable_id.split("/")
    if len(parts) != 2 or any(not part or part in {".", ".."} for part in parts):
        raise SourcebootSfxBundleError(f"invalid closure sample identity: {stable_id!r}")
    path = root / "sound" / "samples" / parts[0] / f"{parts[1]}.aiff"
    if not path.is_file():
        raise SourcebootSfxBundleError(f"missing selected AIFF: {path}")
    return path


def build_bob_sfx_bundle(root: Path, closure_path: Path,
                         sequences_bin: Path,
                         music_pcm: Path | None = None,
                         music_rate: int = MUSIC_DEFAULT_RATE) -> SourcebootSfxBundle:
    """Build immutable metadata and PCM bytes from the selected BOB closure."""
    root = root.resolve()
    closure = _load_closure(closure_path)
    try:
        banks = _load_banks(root)
        sequences = _load_sequences(root, sequences_bin)
    except AudioPackageError as error:
        raise SourcebootSfxBundleError(str(error)) from error
    bank_by_name = {str(bank["name"]): bank for bank in banks}
    resolution = closure["sfx_resolution"]
    assert isinstance(resolution, dict)
    selected_sound_ids = set(resolution)
    sound_bits = _sound_bits_by_symbol(root, selected_sound_ids)
    try:
        decimate = _closure_sfx_decimation_targets(
            sequences, banks, resolution, closure["sequence_ids"])
    except AudioPackageError as error:
        raise SourcebootSfxBundleError(str(error)) from error

    closure_ids = closure["sample_ids"]
    assert isinstance(closure_ids, list)
    if len(set(closure_ids)) != len(closure_ids):
        raise SourcebootSfxBundleError("audio closure contains duplicate sample IDs")
    # Each source SFX can have one or two selected instruments.  Preserve the
    # closure's instrument order and sort only sample names within an
    # instrument so output remains checkout-independent.
    chain_ids: dict[str, tuple[str, ...]] = {}
    required_ids: set[str] = set()
    for sound_id in sorted(selected_sound_ids):
        chain = resolution[sound_id]
        if not isinstance(chain, list) or not chain:
            raise SourcebootSfxBundleError(f"SFX {sound_id} has no resolved chain")
        ids: list[str] = []
        waveform_only = True
        for entry in chain:
            if not isinstance(entry, dict):
                raise SourcebootSfxBundleError(f"SFX {sound_id} has malformed chain entry")
            if "waveform" in entry:
                # The normal scene closure currently reaches the original
                # red-coin synthesized waveform twice.  Do not substitute a
                # proof tone or a random PCM sample: retain a named empty
                # mapping so the driver drops only that unsupported event.
                if set(entry) != {"waveform"} or not isinstance(entry["waveform"], int):
                    raise SourcebootSfxBundleError(f"SFX {sound_id} has malformed waveform")
                continue
            waveform_only = False
            if "bank" not in entry:
                raise SourcebootSfxBundleError(f"SFX {sound_id} has unsupported chain entry")
            bank_name = entry.get("bank")
            instrument = entry.get("instrument")
            index = entry.get("instrument_index")
            if not isinstance(bank_name, str) or not isinstance(instrument, str) or not isinstance(index, int):
                raise SourcebootSfxBundleError(f"SFX {sound_id} has malformed instrument entry")
            bank = bank_by_name.get(bank_name)
            if bank is None:
                raise SourcebootSfxBundleError(f"SFX {sound_id} names absent bank {bank_name}")
            names = _instrument_chain_sample_names(bank, index, instrument)
            if not names:
                raise SourcebootSfxBundleError(f"SFX {sound_id} instrument {instrument} has no sample")
            sample_bank = bank.get("sample_bank")
            if not isinstance(sample_bank, str):
                raise SourcebootSfxBundleError(f"SFX {sound_id} bank {bank_name} has no sample bank")
            for name in sorted(names):
                stable_id = f"{sample_bank}/{name}"
                ids.append(stable_id)
                required_ids.add(stable_id)
        if (not ids and not waveform_only) or len(ids) > 4:
            raise SourcebootSfxBundleError(f"SFX {sound_id} has unsupported chain length {len(ids)}")
        chain_ids[sound_id] = tuple(ids)
    selected_ids = set(closure_ids)
    if required_ids - selected_ids:
        raise SourcebootSfxBundleError("resolved SFX sample set is not closure-selected")

    # The generic BOB SFX path deliberately does not reserve source-selected
    # music PCM. Music is the optional owner-provided looped PCM8 row appended
    # by _finalize_bundle (--music-pcm), not a packaged instrument. This
    # leaves the source closure authoritative for reachability while keeping
    # this first live consumer to actual semantic SFX only.
    sfx_sample_ids = [stable_id for stable_id in closure_ids
                      if isinstance(stable_id, str) and stable_id in required_ids]
    if len(sfx_sample_ids) != len(required_ids):
        raise SourcebootSfxBundleError("audio closure sample IDs are malformed or duplicated")
    decoded: dict[str, object] = {}
    for stable_id in sfx_sample_ids:
        try:
            sample = parse_aiff(_sample_path(root, stable_id))
        except AudioPackageError as error:
            raise SourcebootSfxBundleError(str(error)) from error
        if sample.stable_id != stable_id:
            raise SourcebootSfxBundleError(f"selected sample identity drift: {stable_id}")
        decoded[stable_id] = _decimate_sfx_sample(sample) if stable_id in decimate else sample

    # Keep one physical copy of every source-selected PCM span. The compact
    # descriptor rows below may repeat those physical spans because a sample
    # can belong to more than one semantic SFX chain.
    pcm = bytearray()
    physical_rows: dict[str, BundleSample] = {}
    for stable_id in sfx_sample_ids:
        assert isinstance(stable_id, str)
        sample = decoded[stable_id]
        offset = SOUND_RAM_PCM_OFFSET + len(pcm)
        if offset & 1:
            pcm.append(0)
            offset += 1
        if sample.frames != len(sample.pcm8) or sample.frames == 0:
            raise SourcebootSfxBundleError(f"invalid PCM frame count for {stable_id}")
        if sample.frames > 0xFFFF or not 0 < sample.rate <= 44100:
            raise SourcebootSfxBundleError(f"SCSP-incompatible selected sample {stable_id}")
        if offset + sample.frames > SOUND_RAM_BYTES:
            raise SourcebootSfxBundleError("selected PCM does not fit sound RAM")
        physical_rows[stable_id] = BundleSample(stable_id, offset,
                                                sample.frames, sample.rate)
        pcm.extend(sample.pcm8)
    # The 68K consumes an interval of compact descriptor rows for each SFX.
    # Repeat a descriptor where necessary, never PCM bytes; that preserves
    # chained/layered sound effects without allocating a second PCM bank.
    sample_rows: list[BundleSample] = []
    mapping_rows: list[BundleMapping] = []
    for sound_id in sorted(chain_ids):
        ids = chain_ids[sound_id]
        first = len(sample_rows)
        for stable_id in ids:
            sample_rows.append(physical_rows[stable_id])
        mapping_rows.append(BundleMapping(sound_id, sound_bits[sound_id], first,
                                          len(ids)))
    music_pcm_bytes: bytes | None = None
    if music_pcm is not None:
        try:
            music_pcm_bytes = Path(music_pcm).read_bytes()
        except OSError as error:
            raise SourcebootSfxBundleError(
                f"unreadable music PCM {music_pcm}: {error}") from error
    return _finalize_bundle(int(closure["generation"]), mapping_rows,
                            sample_rows, pcm, music_pcm_bytes, music_rate)


def _finalize_bundle(generation: int, mapping_rows: list[BundleMapping],
                     sample_rows: list[BundleSample], pcm: bytearray,
                     music_pcm: bytes | None,
                     music_rate: int) -> SourcebootSfxBundle:
    """Append the optional looped-music row and emit the final wire bytes.

    Music is one owner-provided raw signed PCM8 span (wav_to_pcm8.py output)
    played through the SCSP's hardware gapless loop; the SEQ_START handler
    keys the row whose index the header trailer carries.  The former m64
    trailer left with the removed sequence VM: the music sequence offset and
    byte words are now always zero, and without --music-pcm every trailer
    word is zero.
    """
    sample_rows = list(sample_rows)
    pcm = bytearray(pcm)
    music_sample_index = 0
    music_sample_id = ""
    if music_pcm is not None:
        if not music_pcm:
            raise SourcebootSfxBundleError("music PCM payload is empty")
        if len(music_pcm) > 0xFFFF:
            raise SourcebootSfxBundleError(
                "music PCM exceeds the 65535-byte SCSP sample limit; trim it "
                "with wav_to_pcm8.py --max-seconds or lower --music-rate")
        if not 0 < music_rate <= 44100:
            raise SourcebootSfxBundleError(
                f"SCSP-incompatible music rate: {music_rate}")
        music_sample_offset = SOUND_RAM_PCM_OFFSET + len(pcm)
        if music_sample_offset & 1:
            pcm.append(0)
            music_sample_offset += 1
        if music_sample_offset + len(music_pcm) > SOUND_RAM_BYTES:
            raise SourcebootSfxBundleError(
                "music PCM does not fit sound RAM; trim it with "
                "wav_to_pcm8.py --max-seconds or lower --music-rate")
        music_sample_index = len(sample_rows)
        music_sample_id = MUSIC_SAMPLE_ID
        sample_rows.append(BundleSample(MUSIC_SAMPLE_ID, music_sample_offset,
                                        len(music_pcm), music_rate,
                                        MUSIC_DEFAULT_VOLUME,
                                        SAMPLE_FLAG_LOOP))
        pcm.extend(music_pcm)
    if not pcm or len(pcm) > SOUND_RAM_BYTES - SOUND_RAM_PCM_OFFSET:
        hint = ("; trim the music with wav_to_pcm8.py --max-seconds or lower "
                "--music-rate" if music_pcm is not None else "")
        raise SourcebootSfxBundleError(
            "selected PCM payload exceeds the sound-RAM bank" + hint)

    metadata_bytes = BUNDLE_HEADER_BYTES + len(mapping_rows) * BUNDLE_MAPPING_BYTES + len(sample_rows) * BUNDLE_SAMPLE_BYTES
    if metadata_bytes > SOUND_RAM_RESERVE_BYTES:
        raise SourcebootSfxBundleError("selected SFX metadata exceeds sound-RAM reserve")
    metadata = bytearray(metadata_bytes)
    metadata[0:4] = _u32(BUNDLE_MAGIC)
    metadata[4:6] = _u16(BUNDLE_VERSION)
    metadata[6:8] = _u16(BUNDLE_HEADER_BYTES)
    metadata[8:10] = _u16(generation)
    metadata[10:12] = _u16(len(mapping_rows))
    metadata[12:14] = _u16(len(sample_rows))
    metadata[14:16] = _u16(BUNDLE_HEADER_BYTES)
    metadata[16:18] = _u16(BUNDLE_HEADER_BYTES + len(mapping_rows) * BUNDLE_MAPPING_BYTES)
    metadata[18:20] = _u16(metadata_bytes)
    metadata[20:24] = _u32(len(pcm))
    metadata[24:28] = _u32(0)  # m64 music-sequence offset: retired with the VM
    metadata[28:30] = _u16(0)  # m64 music-sequence bytes: retired with the VM
    metadata[30:32] = _u16(music_sample_index)
    cursor = BUNDLE_HEADER_BYTES
    for mapping in mapping_rows:
        metadata[cursor:cursor + 4] = _u32(mapping.sound_bits)
        metadata[cursor + 4:cursor + 6] = _u16(mapping.first_sample)
        metadata[cursor + 6:cursor + 8] = _u16(mapping.sample_count)
        cursor += BUNDLE_MAPPING_BYTES
    for sample in sample_rows:
        metadata[cursor:cursor + 4] = _u32(sample.offset)
        metadata[cursor + 4:cursor + 6] = _u16(sample.sample_count)
        metadata[cursor + 6:cursor + 8] = _u16(sample.rate)
        metadata[cursor + 8:cursor + 10] = _u16(sample.default_volume)
        metadata[cursor + 10:cursor + 12] = _u16(sample.flags)
        cursor += BUNDLE_SAMPLE_BYTES
    return SourcebootSfxBundle(generation, bytes(metadata), bytes(pcm),
                               tuple(sample_rows), tuple(mapping_rows),
                               0, 0, music_sample_index, music_sample_id)


def _manifest(bundle: SourcebootSfxBundle) -> dict[str, object]:
    return {
        "schema": "sm64-saturn-sourceboot-sfx-bundle-v1",
        "generation": bundle.generation,
        "metadata_bytes": len(bundle.metadata),
        "metadata_sha256": hashlib.sha256(bundle.metadata).hexdigest(),
        "pcm_bytes": len(bundle.pcm),
        "pcm_sha256": hashlib.sha256(bundle.pcm).hexdigest(),
        "mapping_count": bundle.mapping_count,
        "sample_count": bundle.sample_count,
        "music_sequence_offset": bundle.music_sequence_offset,
        "music_sequence_bytes": bundle.music_sequence_bytes,
        "music_sample_index": bundle.music_sample_index,
        "music_sample_id": bundle.music_sample_id,
        "mappings": [mapping.__dict__ for mapping in bundle.mappings],
        "samples": [sample.__dict__ for sample in bundle.samples],
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--closure", type=Path, required=True)
    parser.add_argument("--sequences-bin", type=Path, required=True)
    parser.add_argument("--metadata-output", type=Path, required=True)
    parser.add_argument("--pcm-output", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--music-pcm", type=Path, default=None,
                        help="raw signed 8-bit mono PCM (wav_to_pcm8.py "
                             "output) packaged as the looped music sample")
    parser.add_argument("--music-rate", type=int, default=MUSIC_DEFAULT_RATE,
                        help="playback rate in Hz for --music-pcm")
    args = parser.parse_args()
    bundle = build_bob_sfx_bundle(args.root, args.closure, args.sequences_bin,
                                  music_pcm=args.music_pcm,
                                  music_rate=args.music_rate)
    for path, data in ((args.metadata_output, bundle.metadata),
                       (args.pcm_output, bundle.pcm)):
        write_bytes_if_changed(path, data)
    write_text_if_changed(
        args.manifest,
        json.dumps(_manifest(bundle), indent=2, sort_keys=True) + "\n",
        encoding="utf-8")


if __name__ == "__main__":
    main()
