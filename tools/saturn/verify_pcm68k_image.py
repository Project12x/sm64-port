#!/usr/bin/env python3
"""Verify the fixed-address contract of the freestanding Saturn 68K image."""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
from pathlib import Path


DRIVER_LIMIT = 0x4000
MAILBOX_START = 0x4000
CONTROL_RING_START = 0x4040
SFX_RING_START = 0x40C0
COMMAND_REGION_END = 0x4240
WORK_START = 0x5000
PCM_BANK_START = 0x8000
EM_68K = 4
PT_LOAD = 1
PF_W = 2


class ImageContractError(ValueError):
    pass


def _elf_contract(data: bytes) -> tuple[int, list[tuple[int, int, int, int, int]]]:
    if len(data) < 52 or data[:4] != b"\x7fELF":
        raise ImageContractError("input is not an ELF image")
    if data[4] != 1 or data[5] != 2:
        raise ImageContractError("image must be big-endian ELF32")
    header = struct.unpack_from(">16sHHIIIIIHHHHHH", data, 0)
    if header[2] != EM_68K:
        raise ImageContractError("image is not an MC68000 ELF")
    entry = header[4]
    program_offset, program_size, program_count = header[5], header[9], header[10]
    if program_size < 32 or program_offset + program_size * program_count > len(data):
        raise ImageContractError("ELF program headers are truncated")
    segments: list[tuple[int, int, int, int, int]] = []
    for index in range(program_count):
        fields = struct.unpack_from(">IIIIIIII", data,
                                    program_offset + index * program_size)
        kind, offset, address, _, file_size, memory_size, flags, _ = fields
        if kind != PT_LOAD:
            continue
        if file_size > memory_size or offset + file_size > len(data):
            raise ImageContractError("ELF load segment is truncated")
        segments.append((address, memory_size, file_size, flags, offset))
    if not segments:
        raise ImageContractError("ELF has no loadable segment")
    return entry, segments


def _map_symbols(text: str) -> dict[str, int]:
    required = ("__image_start", "__driver_end", "__mailbox_start",
                "__control_ring_start", "__sfx_ring_start",
                "__command_region_end", "__work_start", "__pcm_bank_start",
                "__stack_bottom", "__stack_top")
    symbols: dict[str, int] = {}
    for symbol in required:
        match = re.search(
            rf"(?m)^\s*(0x[0-9a-fA-F]+)\s+{symbol}(?:\s*=.*)?\s*$",
            text)
        if match is None:
            raise ImageContractError(f"map is missing {symbol}")
        symbols[symbol] = int(match.group(1), 16)
    return symbols


def verify_image(elf_path: Path, map_path: Path,
                 unresolved_output: str = "") -> dict[str, int]:
    elf_data = elf_path.read_bytes()
    entry, segments = _elf_contract(elf_data)
    symbols = _map_symbols(map_path.read_text(encoding="utf-8"))
    image_base = min(address for address, _, _, _, _ in segments)
    loaded_end = max(address + size for address, size, _, _, _ in segments)

    if image_base != 0 or symbols["__image_start"] != 0:
        raise ImageContractError("image base must be zero")
    if entry != 0x400:
        raise ImageContractError("ELF entry must be fixed at 0x400")
    vector_segment = next(
        ((address, file_size, offset)
         for address, _, file_size, _, offset in segments
         if address <= 0 and address + file_size >= 8), None)
    if vector_segment is None:
        raise ImageContractError("ELF does not load the reset vectors")
    vector_address, _, vector_offset = vector_segment
    vector_file_offset = vector_offset - vector_address
    initial_stack, reset_pc = struct.unpack_from(">II", elf_data,
                                                 vector_file_offset)
    if reset_pc != entry:
        raise ImageContractError("reset vector does not match ELF entry")
    if initial_stack != symbols["__stack_top"]:
        raise ImageContractError("initial stack does not match map stack top")
    if any(address + size > PCM_BANK_START and (flags & PF_W)
           for address, size, _, flags, _ in segments):
        raise ImageContractError("writable content overlaps the PCM bank")
    if loaded_end > MAILBOX_START:
        raise ImageContractError("loadable image overlaps the mailbox")
    if symbols["__driver_end"] > DRIVER_LIMIT:
        raise ImageContractError("driver end exceeds the 16 KiB cap")
    if symbols["__driver_end"] > symbols["__stack_bottom"]:
        raise ImageContractError("driver content overlaps the reserved stack")
    if loaded_end > symbols["__driver_end"]:
        raise ImageContractError("loadable image exceeds declared driver end")
    if symbols["__mailbox_start"] != MAILBOX_START:
        raise ImageContractError("mailbox symbol is not fixed at 0x4000")
    if symbols["__control_ring_start"] != CONTROL_RING_START:
        raise ImageContractError("control ring symbol is not fixed at 0x4040")
    if symbols["__sfx_ring_start"] != SFX_RING_START:
        raise ImageContractError("SFX ring symbol is not fixed at 0x40c0")
    if symbols["__command_region_end"] != COMMAND_REGION_END:
        raise ImageContractError(
            "command region end symbol is not fixed at 0x4240")
    if symbols["__work_start"] != WORK_START:
        raise ImageContractError("work region symbol is not fixed at 0x5000")
    if symbols["__pcm_bank_start"] != PCM_BANK_START:
        raise ImageContractError("PCM bank symbol is not fixed at 0x8000")
    if not (symbols["__driver_end"] <= symbols["__stack_bottom"] <
            symbols["__stack_top"] < MAILBOX_START):
        raise ImageContractError("reserved stack is outside free driver memory")
    unresolved = [line.strip() for line in unresolved_output.splitlines()
                  if line.strip()]
    if unresolved:
        raise ImageContractError("unresolved symbols: " + ", ".join(unresolved))
    return {
        "image_base": image_base,
        "loaded_end": loaded_end,
        "driver_end": symbols["__driver_end"],
        "stack_bottom": symbols["__stack_bottom"],
        "stack_top": symbols["__stack_top"],
        "control_ring_start": symbols["__control_ring_start"],
        "sfx_ring_start": symbols["__sfx_ring_start"],
        "command_region_end": symbols["__command_region_end"],
        "work_start": symbols["__work_start"],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--map", type=Path, required=True)
    parser.add_argument("--nm", default="m68keb-elf-nm")
    arguments = parser.parse_args()
    completed = subprocess.run([arguments.nm, "-u", str(arguments.elf)],
                               check=False, capture_output=True, text=True)
    if completed.returncode != 0:
        raise ImageContractError("nm failed: " + completed.stderr.strip())
    result = verify_image(arguments.elf, arguments.map,
                          unresolved_output=completed.stdout)
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
