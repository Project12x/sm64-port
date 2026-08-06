#!/usr/bin/env python3
"""Source-level contract checks for the bounded geo-walk scheduler."""

from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "src" / "port" / "saturn" / "runtime" / "saturn_geo_walk.h"
SOURCE = ROOT / "src" / "port" / "saturn" / "runtime" / "saturn_geo_walk.c"
TEST = ROOT / "tools" / "saturn" / "geo_walk_contract_test.c"


def test_scheduler_contract_is_pointer_free_and_bounded() -> None:
    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    test = TEST.read_text(encoding="utf-8")
    assert "#include <stdint.h>" in header
    assert "uintptr_t node;" in header
    assert "uint16_t capacity;" in header
    assert "uint16_t high_water;" in header
    assert "bool overflowed;" in header
    assert "SM64_SATURN_GEO_WALK_OVERFLOW" in header
    assert "walk->depth >= walk->capacity" in source
    assert "walk->overflowed = true" in source
    assert "test_overflow_latches_and_stops" in test


def test_scheduler_has_no_platform_or_allocator_dependency() -> None:
    text = HEADER.read_text(encoding="utf-8") + SOURCE.read_text(encoding="utf-8")
    assert not re.search(r"yaul|vdp|scu|malloc|calloc|free", text, re.IGNORECASE)


if __name__ == "__main__":
    test_scheduler_contract_is_pointer_free_and_bounded()
    test_scheduler_has_no_platform_or_allocator_dependency()
    print("geo walk source contract: PASS")
