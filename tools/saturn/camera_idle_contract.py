"""Pure, raw-first decoder and validator for SCC1 camera-idle captures."""
from __future__ import annotations

from dataclasses import dataclass
import math
import struct

SCC1_MAGIC = 0x53434331
SCC1_VERSION = 1
SCC1_HEADER_WORDS = 24
SCC1_SAMPLE_WORDS = 81
SCC1_SAMPLE_COUNT = 600
SCC1_PAYLOAD_WORDS = SCC1_SAMPLE_WORDS * SCC1_SAMPLE_COUNT
SCC1_WORDS = SCC1_HEADER_WORDS + SCC1_PAYLOAD_WORDS
SCC1_BYTES = SCC1_WORDS * 4
SBR4_MAGIC = 0x53425234
SBR4_VERSION = 4
REPLAY_TICKS = 2000
ROUTE_ID = 2
ZOOM_DIST_BITS = 0x43AF0000
REQUIRED_FLAGS = 0x3F
ROLE_IDS = {
    "camera-source-baseline": 1,
    "camera-bypass-diagnostic": 2,
    "camera-fixed-candidate": 3,
}

FLOAT_OFFSETS = frozenset(
    set(range(4, 10)) | set(range(12, 30)) | set(range(32, 42)) |
    set(range(44, 51)) | set(range(52, 59)) | {61, 63} |
    set(range(65, 75)) | {78}
)
PACKED_OFFSETS = frozenset({10, 11, 30, 31, 42, 43, 51, 59, 60, 62,
                            64, 75, 76, 77, 79, 80})


class Scc1Error(ValueError):
    """A raw SCC1 window violates its host-side contract."""


@dataclass(frozen=True)
class Scc1Sample:
    source_tick: int
    applied_input: int
    state_flags: int
    state_words: tuple[int, ...]


@dataclass(frozen=True)
class Scc1Capture:
    raw: bytes
    header: tuple[int, ...]
    samples: tuple[Scc1Sample, ...]


def _float(word: int) -> float:
    value = struct.unpack(">f", word.to_bytes(4, "big"))[0]
    if not math.isfinite(value):
        raise Scc1Error("SCC1 contains a non-finite float")
    return value


def _signed16(word: int) -> tuple[int, int]:
    """Decode the two explicit two's-complement halves of a packed word."""
    high = (word >> 16) & 0xFFFF
    low = word & 0xFFFF
    return (high - 0x10000 if high & 0x8000 else high,
            low - 0x10000 if low & 0x8000 else low)


def _signed32(word: int) -> int:
    return word - 0x100000000 if word & 0x80000000 else word


def _sample_word(sample: Scc1Sample, offset: int) -> int:
    if offset == 0:
        return sample.source_tick
    if offset == 1:
        return sample.applied_input
    if offset == 2:
        return sample.state_flags
    if offset == 3:
        return 0
    return sample.state_words[offset - 4]


def _variant_for_role(role: str) -> int:
    try:
        return ROLE_IDS[role]
    except KeyError as error:
        raise Scc1Error(f"unknown SCC1 role {role!r}") from error


def validate_raw_layout(capture: Scc1Capture) -> None:
    """Validate only facts encoded directly in an SCC1 byte window."""
    header = capture.header
    if len(header) != SCC1_HEADER_WORDS or len(capture.samples) != SCC1_SAMPLE_COUNT:
        raise Scc1Error("SCC1 has an invalid header or sample count")
    expected_header = {
        0: SCC1_MAGIC, 1: SCC1_VERSION, 2: SCC1_HEADER_WORDS,
        3: SCC1_SAMPLE_WORDS, 4: SCC1_SAMPLE_COUNT, 6: 2,
        7: SBR4_MAGIC, 8: SBR4_VERSION, 9: REPLAY_TICKS,
        22: SCC1_PAYLOAD_WORDS, 23: ROUTE_ID,
    }
    for index, value in expected_header.items():
        if header[index] != value:
            raise Scc1Error(f"SCC1 header word {index} is invalid")
    if header[5] not in ROLE_IDS.values():
        raise Scc1Error("SCC1 camera variant is invalid")
    if header[11] != header[9] + header[10]:
        raise Scc1Error("SCC1 first source tick does not follow idle start")
    if header[12] != 0 or header[13] != REQUIRED_FLAGS:
        raise Scc1Error("SCC1 final input/state is not neutral and quiescent")
    if any(header[index] != 0 for index in range(14, 19)):
        raise Scc1Error("SCC1 error counter is nonzero")
    for sample in capture.samples:
        if sample.applied_input != 0:
            raise Scc1Error("SCC1 sample input is not neutral")
        if sample.state_flags != REQUIRED_FLAGS:
            raise Scc1Error("SCC1 sample state flags are invalid")
        if _sample_word(sample, 3) != 0:
            raise Scc1Error("SCC1 sample reserved word is nonzero")
        for offset in FLOAT_OFFSETS:
            _float(_sample_word(sample, offset))
        for offset in PACKED_OFFSETS:
            word = _sample_word(sample, offset)
            if offset == 64:
                _signed32(word)
            else:
                _signed16(word)
            if offset in (77, 80) and (word & 0xFFFF) != 0:
                raise Scc1Error("SCC1 packed reserved bits are nonzero")
        if _sample_word(sample, 71) != ZOOM_DIST_BITS:
            raise Scc1Error("SCC1 gCameraZoomDist witness is invalid")


def decode_scc1(raw: bytes) -> Scc1Capture:
    if len(raw) != SCC1_BYTES:
        raise Scc1Error(f"expected {SCC1_BYTES} bytes, got {len(raw)}")
    words = struct.unpack(">48624I", raw)
    header = tuple(words[:SCC1_HEADER_WORDS])
    for base in range(SCC1_HEADER_WORDS, len(words), SCC1_SAMPLE_WORDS):
        if words[base + 3] != 0:
            raise Scc1Error("SCC1 sample reserved word is nonzero")
    samples = tuple(
        Scc1Sample(
            source_tick=words[base], applied_input=words[base + 1],
            state_flags=words[base + 2],
            state_words=tuple(words[base + 4:base + SCC1_SAMPLE_WORDS]),
        )
        for base in range(SCC1_HEADER_WORDS, len(words), SCC1_SAMPLE_WORDS)
    )
    capture = Scc1Capture(raw=raw, header=header, samples=samples)
    validate_raw_layout(capture)
    return capture


def validate_header_role(header: tuple[int, ...], *, expected_role: str,
                         expected_idle_start_tick: int, expected_route_id: int) -> None:
    variant = _variant_for_role(expected_role)
    if header[5] != variant:
        raise Scc1Error("declared SCC1 role disagrees with raw camera variant")
    if header[10] != expected_idle_start_tick:
        raise Scc1Error("SCC1 idle start tick is wrong")
    if header[23] != expected_route_id:
        raise Scc1Error("SCC1 route id is wrong")


def validate_stable_samples(samples: tuple[Scc1Sample, ...]) -> None:
    if len(samples) != SCC1_SAMPLE_COUNT:
        raise Scc1Error("SCC1 sample count is invalid")
    first = samples[0]
    for index, sample in enumerate(samples):
        if sample.source_tick != first.source_tick + index:
            raise Scc1Error("SCC1 source ticks are not consecutive")
        if sample.applied_input != 0 or sample.state_flags != REQUIRED_FLAGS:
            raise Scc1Error("SCC1 idle sample is not neutral/quiescent")
        if index and (sample.applied_input, sample.state_flags, sample.state_words) != (
                first.applied_input, first.state_flags, first.state_words):
            raise Scc1Error("SCC1 idle state changed within its capture window")


def validate_scc1(capture: Scc1Capture, *, expected_role: str,
                  expected_idle_start_tick: int, expected_route_id: int) -> None:
    validate_header_role(capture.header, expected_role=expected_role,
                         expected_idle_start_tick=expected_idle_start_tick,
                         expected_route_id=expected_route_id)
    validate_stable_samples(capture.samples)
    if capture.samples[0].source_tick != REPLAY_TICKS + expected_idle_start_tick:
        raise Scc1Error("SCC1 first sample is not at frozen replay plus idle start")


def compare_same_role(first: Scc1Capture, second: Scc1Capture) -> None:
    if first.raw != second.raw:
        raise Scc1Error("same-role SCC1 windows are not byte-identical")
