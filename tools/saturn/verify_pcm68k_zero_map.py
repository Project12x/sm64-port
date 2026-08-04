#!/usr/bin/env python3
"""Verify the 68K image consumes commands from its valid address-zero map."""
from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


class ZeroMapContractError(RuntimeError):
    pass


def _function(disassembly: str, name: str) -> str:
    match = re.search(
        rf"^[0-9a-fA-F]+ <{re.escape(name)}>:$(.*?)(?=^[0-9a-fA-F]+ <|\Z)",
        disassembly, re.MULTILINE | re.DOTALL)
    if match is None:
        raise ZeroMapContractError(f"missing {name} in 68K disassembly")
    return match.group(1)


def verify_disassembly(disassembly: str) -> None:
    main = _function(disassembly, "pcm68k_main")
    if "<sm64_saturn_pcm68k_consume_mapped_zero>" not in main:
        raise ZeroMapContractError(
            "pcm68k_main does not select the mapped-zero consumer")
    mapped = _function(disassembly,
                       "sm64_saturn_pcm68k_consume_mapped_zero")
    call = mapped.find("<sm64_saturn_pcm68k_consume_internal>")
    if call < 0:
        raise ZeroMapContractError("mapped-zero entry does not call consumer")
    prefix = mapped[:call]
    if "clrl" not in prefix:
        raise ZeroMapContractError("mapped-zero entry does not pass address zero")
    if re.search(r"\bb(?:eq|ne)(?:s|w|l)?\b", prefix):
        raise ZeroMapContractError("mapped-zero entry rejects address zero")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--objdump", type=Path, required=True)
    args = parser.parse_args()
    result = subprocess.run(
        [str(args.objdump), "-dr", str(args.elf)], check=True,
        text=True, capture_output=True)
    verify_disassembly(result.stdout)
    print("verified PCM68K address-zero command-consumer path")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
