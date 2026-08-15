"""Host tests for capture_sprint2_peaks.py's pure decode/analysis logic."""

from __future__ import annotations

import pytest

from capture_sprint2_peaks import (
    MARIO_SNAPSHOT_BYTES,
    PEAK_PROBE_MAGIC,
    acceptance,
    decode_mario_snapshot,
    decode_memb,
    decode_peak_probe,
    decode_refs,
    resolve_local_symbol,
    stain_scan,
    tlsf_walk,
    validate_post_bios_frames,
    validate_sample_interval,
)


def be32(value: int) -> bytes:
    return value.to_bytes(4, "big")


def test_decode_peak_probe_valid() -> None:
    words = [PEAK_PROBE_MAGIC, 3199, 3980, 412, 861, 1433, 60, 64]
    decoded = decode_peak_probe(b"".join(be32(word) for word in words))
    assert decoded["magic_valid"] is True
    assert decoded["gfx_pool_entries_last"] == 3199
    assert decoded["gfx_pool_entries_highwater"] == 3980
    assert decoded["gfx_pool_task_count"] == 412
    assert decoded["vdp1_commands_last"] == 861
    assert decoded["vdp1_commands_highwater"] == 1433
    assert decoded["vdp1_gouraud_last"] == 60
    assert decoded["vdp1_gouraud_highwater"] == 64


def test_decode_peak_probe_pre_init_reads_are_lenient() -> None:
    decoded = decode_peak_probe(bytes(32))
    assert decoded["magic_valid"] is False
    assert decoded["vdp1_commands_highwater"] is None


def test_decode_peak_probe_rejects_wrong_length() -> None:
    with pytest.raises(ValueError):
        decode_peak_probe(bytes(28))


def test_decode_memb_field_order_matches_memb_h() -> None:
    words = [0, 532, 14, 0x060A0000, 5, 3, 0x060A0100, 0]
    decoded = decode_memb(b"".join(be32(word) for word in words))
    assert decoded["block_size"] == 532
    assert decoded["block_count"] == 14
    assert decoded["next_index"] == 5
    assert decoded["alloc_count"] == 3


def test_decode_refs_counts_referenced_slots() -> None:
    counts = [1, 0, 1, 1] + [0] * 10
    raw = b"".join(count.to_bytes(2, "big") for count in counts)
    decoded = decode_refs(raw)
    assert decoded["referenced_slots"] == 3
    assert decoded["counts"] == counts


def test_stain_scan_finds_highest_offsets() -> None:
    pool = bytearray(256)
    pool[10] = 0xFF   # control-region stain
    pool[100] = 0x01  # area stain
    result = stain_scan(bytes(pool), area_start=64)
    assert result["highest_nonzero_offset"] == 100
    assert result["highest_nonzero_offset_in_area"] == 100


def test_stain_scan_untouched_area_reports_minus_one() -> None:
    pool = bytearray(256)
    pool[10] = 0xFF
    result = stain_scan(bytes(pool), area_start=64)
    assert result["highest_nonzero_offset_in_area"] == -1


def synthetic_pool() -> bytes:
    """control(16) + used 24 + free 40 + used 20 + sentinel, 128 B total."""
    pool = bytearray(128)
    pool[16:20] = be32(24)        # used block, prev used
    pool[44:48] = be32(40 | 0x1)  # free block
    pool[48:56] = be32(0xDEAD) + be32(0xBEEF)  # free-list link stains
    pool[88:92] = be32(20 | 0x2)  # used block, prev free
    pool[112:116] = be32(0x2)     # zero-size used sentinel
    return bytes(pool)


def test_tlsf_walk_synthetic_chain() -> None:
    walk = tlsf_walk(synthetic_pool(), control_size=16)
    assert walk["valid"] is True
    assert walk["block_count"] == 3
    assert walk["sentinel_offset"] == 112
    assert walk["used_block_count"] == 2
    assert walk["used_payload_bytes"] == 44
    assert walk["free_payload_bytes"] == 40
    assert walk["largest_free_block"] == 40
    assert walk["highest_used_payload_end"] == 112
    assert [block["free"] for block in walk["blocks"]] == [False, True, False]


def test_tlsf_walk_truncated_chain_is_invalid() -> None:
    pool = bytearray(64)
    pool[16:20] = be32(200)  # runs past the pool with no sentinel
    walk = tlsf_walk(bytes(pool), control_size=16)
    assert walk["valid"] is False
    assert walk["sentinel_offset"] is None


def test_resolve_local_symbol_accepts_sh2_underscore() -> None:
    listing = "06090000 b __private_pool\n0609a000 b __other\n"
    assert resolve_local_symbol(listing, "_private_pool") == 0x06090000


def test_resolve_local_symbol_rejects_ambiguity() -> None:
    listing = "06090000 b __private_pool\n060a0000 b __private_pool\n"
    with pytest.raises(ValueError):
        resolve_local_symbol(listing, "_private_pool")


def test_resolve_local_symbol_rejects_missing() -> None:
    with pytest.raises(ValueError):
        resolve_local_symbol("06090000 b __other\n", "_private_pool")


def test_frame_and_interval_validation() -> None:
    assert validate_post_bios_frames(7200) == 7200
    with pytest.raises(ValueError):
        validate_post_bios_frames(7199)
    assert validate_sample_interval(300) == 300
    with pytest.raises(ValueError):
        validate_sample_interval(601)
    with pytest.raises(ValueError):
        validate_sample_interval(0)


def mario_raw(position, valid=1) -> bytes:
    raw = bytearray(MARIO_SNAPSHOT_BYTES)
    for index, value in enumerate(position):
        raw[index * 4:index * 4 + 4] = value.to_bytes(4, "big", signed=True)
    raw[55] = valid
    return bytes(raw)


def test_decode_mario_snapshot_position_and_valid() -> None:
    decoded = decode_mario_snapshot(mario_raw([-1234, 200, 5678]))
    assert decoded["position"] == [-1234, 200, 5678]
    assert decoded["valid"] == 1


def test_decode_mario_snapshot_rejects_wrong_length() -> None:
    with pytest.raises(ValueError):
        decode_mario_snapshot(bytes(40))


def acceptance_sample(position, valid=1, yaw=0, vdp=1) -> dict:
    return {
        "peak_probe": {"magic_valid": True},
        "mario": {"position": list(position), "valid": valid},
        "area_yaw": yaw,
        "exception_magic": 0,
        "boot": {"vdp2_presentation_generation": vdp},
    }


def test_acceptance_movement_gate_uses_mario_position() -> None:
    moved = acceptance(
        [acceptance_sample([0, 0, 0], vdp=1),
         acceptance_sample([100, 0, 50], vdp=2)],
        failure=False,
    )
    assert moved["route_movement_observed"] is True
    assert moved["pass"] is True
    idle = acceptance(
        [acceptance_sample([7, 8, 9], vdp=1),
         acceptance_sample([7, 8, 9], vdp=2)],
        failure=False,
    )
    assert idle["route_movement_observed"] is False
    assert idle["pass"] is False


def test_acceptance_ignores_invalid_mario_snapshots() -> None:
    result = acceptance(
        [acceptance_sample([0, 0, 0], valid=0, vdp=1),
         acceptance_sample([100, 0, 0], valid=0, vdp=2)],
        failure=False,
    )
    assert result["route_movement_observed"] is False


def test_analyze_private_pool_reports_stain_below_terminals() -> None:
    from capture_sprint2_peaks import TLSF_CONTROL_SIZE, analyze_private_pool

    pool = bytearray(TLSF_CONTROL_SIZE + 128)
    base = TLSF_CONTROL_SIZE
    pool[base:base + 4] = (24).to_bytes(4, "big")            # used block
    pool[base + 20] = 0xAB                                    # payload stain
    pool[base + 28:base + 32] = (88 | 0x1).to_bytes(4, "big")  # top free block
    pool[base + 32:base + 40] = b"\xde\xad\xbe\xef" * 2       # free-list links
    sentinel = base + 28 + 4 + 88
    pool[sentinel:sentinel + 4] = (0x2).to_bytes(4, "big")    # sentinel
    analysis = analyze_private_pool(bytes(pool))
    assert analysis["walk"]["valid"] is True
    assert analysis["stain_below_sentinel"] == base + 39
    assert analysis["stain_below_top_free_block"] == base + 20
