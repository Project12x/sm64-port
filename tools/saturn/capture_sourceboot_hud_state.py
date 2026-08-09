#!/usr/bin/env python3
"""Boot the built sourceboot image headless, run to a fixed VBlank count,
peek the VDP2 HUD pattern-name table, and assert a specific tile-grid cell
holds the expected glyph. This is the automated in-emulator check that runs
BEFORE the owner's manual desktop-Ymir acceptance (Task 10) -- it proves the
atlas/tilemap actually reached VRAM correctly without a human watching every
intermediate build.

Reuses ``YmirClient`` from ``capture_route_views.py`` (Popen + background
reader threads + blocking ``.call(method, params)`` per JSON-RPC id) rather
than reimplementing the transport.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

from capture_route_views import YmirClient

# --- Real, current VDP2 HUD VRAM layout -------------------------------------
#
# Mirrors src/port/saturn/gfx/saturn_hud_atlas.c's HUD_CPD_BASE/HUD_PND_BASE
# and third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2/vram.h's
#   VDP2_VRAM_ADDR(bank, offset) = 0x25E00000 + (bank << 17) + offset
# expanded to literal hex here because the RPC needs a real address, not the
# macro form. Task 8's implementer found and fixed a real VRAM collision: the
# atlas was originally placed in bank 1 (== VDP2 bank A1), which is actually
# the second half of sourceboot's 512x256 NBG1 sky bitmap (banks A0+A1), not
# a disjoint region. The atlas now lives in bank 2 (VDP2 bank B0), confirmed
# unused by the sky bitmap (banks 0-1) or dbgio's NBG3 console/backscreen
# gradient (bank 3) -- see the block comment on HUD_CPD_BASE in
# saturn_hud_atlas.c for the full derivation.
HUD_CPD_BASE = 0x25E40000  # VDP2_VRAM_ADDR(2, 0x00000)
HUD_PND_BASE = 0x25E48000  # VDP2_VRAM_ADDR(2, 0x08000)

# VDP2_SCRN_CHAR_SIZE_2X2 + VDP2_SCRN_PLANE_SIZE_1X1 pages are always a fixed
# 32x32-cell grid in VRAM addressing terms (VDP2_SCRN_PAGE_WIDTH/HEIGHT_
# CALCULATE in scrn_macros.h return 32 regardless of TV resolution). This is
# the *internal* PND row stride sm64_saturn_hud_atlas_write_cell() uses
# (cell_index = row * HUD_PAGE_STRIDE_COLS + col) -- it is NOT the same
# number as the port's 20x14 *visible* tile bound (HUD_TILE_COLS/ROWS in the
# same file), which only bounds which cells are reachable/on-screen, not how
# the page is addressed in VRAM. Using 20 here instead of 32 would compute
# the wrong byte offset for every row past the first.
HUD_PAGE_STRIDE_COLS = 32

# Each character pattern is one 16x16 RGB1555 (VDP2_SCRN_CHAR_SIZE_2X2)
# glyph: 16 * 16 texels * 2 bytes/texel.
HUD_CHAR_DIM = 16
HUD_CHAR_BYTES = HUD_CHAR_DIM * HUD_CHAR_DIM * 2

# sm64_saturn_hud_layout.c's HUD_ROW_COUNTERS/HUD_COL_LIVES: the first cell
# of the "lives" cluster (Mario head glyph, then a multiply glyph, then a
# 2-digit count) on this port's 20x14 visible grid. HUD_FLAG_LIVES is one of
# HUD_DISPLAY_DEFAULT's bits (level_update.h) and is active during ordinary
# gameplay, so this cell should hold SM64_SATURN_HUD_GLYPH_MARIO_HEAD as soon
# as the source tick has run past level load with the default HUD flags.
DEFAULT_EXPECT_COL = 1
DEFAULT_EXPECT_ROW = 12


def pnd_cell_address(col: int, row: int) -> int:
    return HUD_PND_BASE + (row * HUD_PAGE_STRIDE_COLS + col) * 2


def expected_character_number(glyph_index: int) -> int:
    """Reproduce VDP2_SCRN_PND_CONFIG_3's bit-packing (libyaul's
    scrn_macros.h) for the glyph sm64_saturn_hud_atlas_init()/
    sm64_saturn_hud_atlas_write_cell() would have written:

        VDP2_SCRN_PND_CONFIG_3(cram_mode, cpd_addr, pal_addr) =
            (pal_num(pal_addr) & 0xF) << 12 |
            ((VDP2_SCRN_PND_CP_NUM(cpd_addr) >> 2) & 0x0FFF)
        VDP2_SCRN_PND_CP_NUM(addr) = (addr) >> 5

    with pal_addr == 0 (this atlas always calls
    VDP2_SCRN_PND_CONFIG_3(0, cpd_addr, 0), see saturn_hud_atlas.c), so the
    full 16-bit PND word's low 12 bits equal
    ``((HUD_CPD_BASE + glyph_index * HUD_CHAR_BYTES) >> 7) & 0x0FFF``.

    CONFIG_3 (not CONFIG_1) because the encoding is dictated by the screen's
    cell format, CHAR_SIZE_2X2 + AUX_MODE_1: with 2x2 characters the char
    number's low 2 bits (8x8 sub-cell selectors) drop out of the PND word,
    which holds char# bits 13-2 instead of 11-0 (scrn_macros.h:159-161;
    vdp2_scrn_cell.c:347-351 "Character number in pattern name table: bits
    13~2"). An earlier revision of this tool mirrored the atlas's CONFIG_1
    bug (the Task 9 blank-HUD defect: every cell resolved into the bank-A0
    sky bitmap) and therefore false-PASSED it -- keeping this helper derived
    from the *format-correct* macro, not from whatever the C side happens to
    call, is the point of re-deriving it here.

    This is a VRAM-address-derived hardware "character number", NOT the same
    small integer as the sm64_saturn_hud_glyph_t enum ordinal -- the atlas
    header's "index IS the VDP2 character-pattern number" comment means the
    enum order matches VRAM slot order, not that the raw PND field equals
    the bare enum value. Computing it here (instead of hardcoding a magic
    constant in the Makefile) keeps this assertion correct across any future
    HUD_CPD_BASE relocation, which is exactly the class of bug Task 8 already
    hit once.
    """
    cpd_addr = HUD_CPD_BASE + glyph_index * HUD_CHAR_BYTES
    return (cpd_addr >> 7) & 0x0FFF


def read_pnd_cell(client: YmirClient, col: int, row: int) -> int:
    """Read one 16-bit VDP2 pattern-name word. mem.peek's real response
    shape (confirmed against capture_hwtest.py/capture_camera_idle.py/
    capture_route_views.py, all of which already call mem.peek) is
    ``{"data": [<byte>, ...]}`` -- NOT ``{"bytes": [...]}`` -- and
    ``client.call()`` already unwraps the JSON-RPC "result" envelope, so no
    second ``["result"]`` indexing is needed here either. None of those real
    call sites pass a "target" parameter to mem.peek (unlike regs.read,
    which does need one to disambiguate sh2.master/sh2.slave); VDP2 VRAM is
    on the shared bus both CPUs see identically, so this omits it too rather
    than guessing at an unverified parameter name.
    """
    result = client.call("mem.peek", {
        "address": pnd_cell_address(col, row),
        "count": 2,
    })
    data = result.get("data")
    if not isinstance(data, list) or len(data) != 2:
        got = len(data) if isinstance(data, list) else "no"
        raise RuntimeError(
            f"HUD PND read at col={col} row={row} (address "
            f"0x{pnd_cell_address(col, row):08X}) returned {got} bytes, expected 2"
        )
    return int.from_bytes(bytes(data), byteorder="big")


def save_screenshot(result: dict[str, Any], path: Path) -> dict[str, Any]:
    """Decode and save a video.capture PNG. Mirrors
    capture_route_views.py's screenshot_identity()."""
    if result.get("mime_type") != "image/png":
        raise RuntimeError("Ymir video.capture did not return a PNG")
    png = base64.b64decode(result["data"], validate=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)
    return {
        "path": str(path),
        "bytes": len(png),
        "sha256": hashlib.sha256(png).hexdigest(),
        "sequence": result.get("sequence"),
        "width": result.get("width"),
        "height": result.get("height"),
        "frame_hash": result.get("hash"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", required=True, type=Path, help="path to ymir-headless.exe")
    parser.add_argument("--ipl", required=True, type=Path, help="Saturn BIOS/IPL image")
    parser.add_argument("--game", required=True, type=Path, help="built sourceboot .cue path")
    parser.add_argument("--timeout", type=float, default=1700.0,
                        help="Ymir wall-clock budget in seconds (default: 1700)")
    parser.add_argument("--startup-frames", type=int, default=3600,
                        help="emulated frames to run after the fixed BIOS boot "
                             "macro, before reading the HUD PND cell (default: 3600)")
    parser.add_argument("--expect-col", type=int, default=DEFAULT_EXPECT_COL,
                        help=f"HUD tile-grid column to sample (default: {DEFAULT_EXPECT_COL}, "
                             "HUD_COL_LIVES in saturn_hud_layout.c)")
    parser.add_argument("--expect-row", type=int, default=DEFAULT_EXPECT_ROW,
                        help=f"HUD tile-grid row to sample (default: {DEFAULT_EXPECT_ROW}, "
                             "HUD_ROW_COUNTERS in saturn_hud_layout.c)")
    parser.add_argument("--expect-glyph-index", type=int, required=True,
                        help="sm64_saturn_hud_glyph_t ordinal expected at "
                             "(--expect-col,--expect-row) once the lives readout "
                             "(Mario head glyph) is live, e.g. "
                             "SM64_SATURN_HUD_GLYPH_MARIO_HEAD (12)")
    parser.add_argument("--output", required=True, type=Path,
                        help="path for the JSON evidence report")
    parser.add_argument("--screenshot-output", type=Path, default=None,
                        help="optional PNG path (default: --output with .png suffix)")
    args = parser.parse_args()

    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.startup_frames <= 0:
        parser.error("--startup-frames must be positive")
    if not 0 <= args.expect_col < HUD_PAGE_STRIDE_COLS or not 0 <= args.expect_row < HUD_PAGE_STRIDE_COLS:
        parser.error(
            f"--expect-col/--expect-row must fit the {HUD_PAGE_STRIDE_COLS}x"
            f"{HUD_PAGE_STRIDE_COLS} addressable VDP2 page (0..{HUD_PAGE_STRIDE_COLS - 1})"
        )
    if not 0 <= args.expect_glyph_index <= 0x0FFF:
        parser.error("--expect-glyph-index must fit the 12-bit PND character-number field")
    for label, path in (("Ymir executable", args.ymir), ("IPL", args.ipl), ("game", args.game)):
        if not path.is_file():
            parser.error(f"{label} not found: {path}")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.output = args.output.resolve()
    screenshot_path = (
        args.screenshot_output.resolve()
        if args.screenshot_output is not None
        else args.output.with_suffix(".png")
    )

    client: YmirClient | None = None
    raw_word: int | None = None
    screenshot_result: dict[str, Any] = {}
    try:
        client = YmirClient(args.ymir, args.ipl, args.game, args.timeout)

        def run_for(frames: int) -> None:
            remaining = frames
            while remaining > 0:
                chunk = min(remaining, 3600)
                client.call("exec.run_for", {"frames": chunk})
                remaining -= chunk

        # The proven USA-BIOS boot macro (capture_route_views.py/
        # capture_hwtest.py --bios-input): region/language navigation, then
        # release every button before letting the game run.
        run_for(120)
        client.call("input.pulse", {"buttons": 0x4000})
        run_for(30)
        client.call("input.pulse", {"buttons": 0x0400})
        run_for(1200)
        for _ in range(5):
            client.call("input.pulse", {"buttons": 0x4000})
            run_for(30)
        client.call("input.pulse", {"buttons": 0xFFF8})

        run_for(args.startup_frames)

        raw_word = read_pnd_cell(client, args.expect_col, args.expect_row)
        screenshot_result = client.call("video.capture")
        client.shutdown()
    except BaseException:
        if client is not None:
            client.abort()
        raise

    assert raw_word is not None
    observed_character_number = raw_word & 0x0FFF
    expected = expected_character_number(args.expect_glyph_index)
    passed = observed_character_number == expected

    screenshot_identity = save_screenshot(screenshot_result, screenshot_path)

    report = {
        "evidence_kind": "sourceboot-hud-automated-capture",
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": str(args.game),
        "startup_frames": args.startup_frames,
        "expect_col": args.expect_col,
        "expect_row": args.expect_row,
        "expect_glyph_index": args.expect_glyph_index,
        "pnd_address": pnd_cell_address(args.expect_col, args.expect_row),
        "observed_pnd_word": raw_word,
        "observed_character_number": observed_character_number,
        "expected_character_number": expected,
        "pass": passed,
        "screenshot": screenshot_identity,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))

    if not passed:
        print(
            f"HUD PND mismatch at (col={args.expect_col},row={args.expect_row}): "
            f"expected character number {expected} (glyph index "
            f"{args.expect_glyph_index}), observed {observed_character_number} "
            f"(raw word 0x{raw_word:04X})",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
