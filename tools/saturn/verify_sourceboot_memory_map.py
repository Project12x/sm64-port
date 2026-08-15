#!/usr/bin/env python3
"""Inspect sourceboot ELFs and enforce camera transport memory budgets."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# Historical developer-machine literals, kept as fallbacks so existing
# invocations that never exported the environment variables keep working.
YAUL_BIN_FALLBACK = Path("D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin")
MSYS_USR_BIN_FALLBACK = Path(r"C:\msys64\usr\bin")
_FALLBACK_WARNINGS: set[str] = set()


def _warn_fallback(variable: str, fallback: Path) -> None:
    if variable in _FALLBACK_WARNINGS:
        return
    _FALLBACK_WARNINGS.add(variable)
    print(f"warning: {variable} is unset; falling back to {fallback}",
          file=sys.stderr)


def _toolchain_bin() -> Path:
    root = os.environ.get("YAUL_INSTALL_ROOT", "").strip()
    if root:
        return Path(root) / "bin"
    _warn_fallback("YAUL_INSTALL_ROOT", YAUL_BIN_FALLBACK)
    return YAUL_BIN_FALLBACK


def _msys_usr_bin() -> Path:
    root = os.environ.get("MSYS2_ROOT", "").strip()
    if root:
        return Path(root) / "usr" / "bin"
    _warn_fallback("MSYS2_ROOT", MSYS_USR_BIN_FALLBACK)
    return MSYS_USR_BIN_FALLBACK
HWRAM_TOP = 0x06100000
HWRAM_BASE = 0x06000000
LWRAM_TOP = 0x00300000
LWRAM_BASE = 0x00200000
VDP1_COMMAND_BANK_BYTES = 2 * 2048 * 32
GOURAUD_STAGING_BYTES = 2 * 1536 * 8
MINIMUM_FINAL_MARGIN = 0x1B00
MINIMUM_LWRAM_MARGIN = 0x4000
SH2_P2_BASE = 0x20000000
SH2_PARTITION_MASK = 0xE0000000
SCC_START = 0x002CBB20
SCC_SIZE = 0x2F7C0
SCC_END = 0x002FB2E0
PHASE_PREDECESSORS = {
    "fixed-baseline": {"transport"},
    "candidate-q12": {"fixed-baseline"},
    "candidate-q16": {"fixed-baseline"},
    "final-baseline": {"fixed-baseline"},
    "frozen-numeric": {"candidate-q12", "candidate-q16"},
    "shadow": {"frozen-numeric"},
    "lakitu": {"shadow"},
    "default-core": {"lakitu"},
    "bridges": {"default-core"},
    "complete-island": {"bridges"},
    "final-q": {"complete-island"},
}
CAMERA_VARIANT_ROLES = {
    1: "camera-source-baseline",
    2: "camera-bypass-diagnostic",
    3: "camera-fixed-candidate",
}


@dataclass(frozen=True)
class Section:
    name: str
    address: int
    size: int
    kind: str


@dataclass(frozen=True)
class Symbol:
    name: str
    address: int
    size: int


@dataclass
class ElfLayout:
    path: Path
    sha256: str
    sections: dict[str, Section]
    symbols: dict[str, Symbol]


def _tool_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment["PATH"] = str(_msys_usr_bin()) + os.pathsep + environment.get("PATH", "")
    return environment


def _run(tool: Path, *arguments: str) -> str:
    completed = subprocess.run(
        [str(tool), *arguments], check=False, capture_output=True, text=True,
        env=_tool_environment(),
    )
    if completed.returncode != 0:
        raise ValueError(f"{tool.name} failed: {completed.stderr.strip()}")
    return completed.stdout


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_elf(path: Path) -> ElfLayout:
    path = path.resolve()
    toolchain = _toolchain_bin()
    section_output = _run(toolchain / "sh-elf-readelf.exe", "-SW", str(path))
    symbol_output = _run(toolchain / "sh-elf-nm.exe", "-S", "--defined-only", str(path))
    sections: dict[str, Section] = {}
    section_re = re.compile(
        r"\[\s*\d+\]\s+(\S+)\s+(\S+)\s+([0-9a-fA-F]+)\s+"
        r"[0-9a-fA-F]+\s+([0-9a-fA-F]+)"
    )
    for line in section_output.splitlines():
        match = section_re.search(line)
        if match:
            name, kind, address, size = match.groups()
            sections[name] = Section(name, int(address, 16), int(size, 16), kind)
    symbols: dict[str, Symbol] = {}
    for line in symbol_output.splitlines():
        parts = line.split()
        if len(parts) >= 4 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]) and \
                re.fullmatch(r"[0-9a-fA-F]+", parts[1]):
            name = parts[-1]
            symbols[name] = Symbol(name, int(parts[0], 16), int(parts[1], 16))
        elif len(parts) >= 3 and re.fullmatch(r"[0-9a-fA-F]+", parts[0]):
            name = parts[-1]
            symbols[name] = Symbol(name, int(parts[0], 16), 0)
    return ElfLayout(path, _sha256(path), sections, symbols)


def _ranges_overlap(left: Section, right: Section) -> bool:
    return left.address < right.address + right.size and \
        right.address < left.address + left.size


def _tag_gate(layout: ElfLayout, route: int, stage_sectors: int) -> None:
    text = str(layout.path).replace("\\", "/")
    if "e2-bob" not in text:
        return
    missing = [tag for tag in (f"camroute{route}", f"stage{stage_sectors}")
               if tag not in text]
    if not missing:
        return
    # Identity-directory outputs intentionally use a short hash tag to stay
    # within Windows path limits. Bind the omitted role fields to the exact
    # generated identity spec instead of accepting an unlabelled ELF.
    spec = layout.path.parents[2] / "generated" / "saturn_build_identity_spec.json"
    try:
        values = json.loads(spec.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError("ELF path lacks role tags and identity spec is unavailable") from error
    if values.get("camera_route") != route or \
            values.get("cart_stage_sectors") != stage_sectors:
        raise ValueError("ELF identity spec does not match requested role")


def _c_symbol(layout: ElfLayout, name: str) -> Symbol | None:
    """Resolve host-mock spelling or SH-ELF's leading-underscore C ABI."""
    return layout.symbols.get(name) or layout.symbols.get(f"_{name}")


def validate_layout(layout: ElfLayout, *, route: int, stage_sectors: int,
                    required_final_margin: int) -> dict[str, Any]:
    _tag_gate(layout, route, stage_sectors)
    if required_final_margin < MINIMUM_FINAL_MARGIN:
        raise ValueError("required final floor is below the A7 linker minimum")
    end = layout.symbols.get("___end")
    if end is None:
        raise ValueError("ELF lacks ___end")
    if end.address > HWRAM_TOP:
        raise ValueError("ELF end is past HWRAM top")
    if HWRAM_TOP - end.address < required_final_margin:
        raise ValueError("ELF HWRAM margin is below required final floor")
    uncached = layout.sections.get(".uncached")
    if uncached is None or uncached.kind != "PROGBITS" or \
            uncached.address & SH2_PARTITION_MASK != SH2_P2_BASE:
        raise ValueError("ELF uncached section is not the exact P2 PROGBITS range")
    uncached_physical_start = uncached.address & ~SH2_P2_BASE
    uncached_physical_end = uncached_physical_start + uncached.size
    if uncached_physical_start < HWRAM_BASE or \
            uncached_physical_end != end.address:
        raise ValueError("ELF uncached section end disagrees with ___end")
    stage = _c_symbol(layout, "s_source_cart_stage")
    stage_bytes = stage_sectors * 2048
    if stage is not None:
        if stage.size != stage_bytes:
            raise ValueError("ELF cart-stage symbol size is wrong")
    else:
        # Current sourceboot lends the first phase-local slice of the future
        # LWRAM main pool to the CD reader, then reinitializes that pool before
        # game allocations. This removes a permanent HWRAM staging bank while
        # retaining an auditable physical storage owner in the ELF.
        pool = _c_symbol(layout, "sourceboot_main_pool")
        if pool is None or pool.address < LWRAM_BASE or \
                pool.address + stage_bytes > LWRAM_TOP:
            raise ValueError("ELF cart-stage phase workspace is outside LWRAM")
    route_marker = _c_symbol(layout, "sm64_saturn_camera_route_marker")
    variant_marker = _c_symbol(layout, "sm64_saturn_camera_variant_marker")
    if route_marker is None or route_marker.address != route:
        raise ValueError("ELF camera route marker is wrong")
    if variant_marker is None or variant_marker.address not in CAMERA_VARIANT_ROLES:
        raise ValueError("ELF camera variant marker is not a Phase A camera role")
    command_banks = layout.sections.get(".lwram_cmdts")
    if command_banks is None:
        # Current sourceboot keeps the two CPU-DMAC command banks in HWRAM.
        # The linker forbids the legacy .lwram_cmdts input; use the exported
        # owner symbol so this gate describes the actual transport placement.
        command_owner = _c_symbol(layout, "sourceboot_vdp1_cmdts")
        if command_owner is None or command_owner.size != VDP1_COMMAND_BANK_BYTES or \
                command_owner.address % 32 != 0 or \
                command_owner.address < HWRAM_BASE or \
                command_owner.address + command_owner.size > HWRAM_TOP:
            raise ValueError("ELF command banks are not the exact aligned HWRAM range")
        command_banks = Section(
            "sourceboot_vdp1_cmdts", command_owner.address,
            command_owner.size, "NOBITS")
    elif command_banks.kind != "NOBITS" or \
            command_banks.size != VDP1_COMMAND_BANK_BYTES or \
            command_banks.address % 32 != 0 or \
            command_banks.address < LWRAM_BASE or \
            command_banks.address + command_banks.size > LWRAM_TOP:
        raise ValueError("ELF command banks are not the exact aligned LWRAM range")
    lwram_bulk = layout.sections.get(".lwram_bss")
    if lwram_bulk is None or lwram_bulk.kind != "NOBITS" or \
            lwram_bulk.address < LWRAM_BASE or \
            lwram_bulk.address + lwram_bulk.size > LWRAM_TOP:
        raise ValueError("ELF LWRAM bulk section is outside physical LWRAM")
    gouraud = _c_symbol(layout, "sourceboot_gouraud_staging")
    if gouraud is None or gouraud.size != GOURAUD_STAGING_BYTES or \
            gouraud.address % 8 != 0 or gouraud.address < HWRAM_BASE or \
            gouraud.address + gouraud.size > HWRAM_TOP:
        raise ValueError("ELF Gouraud staging is not the exact aligned HWRAM range")
    capture = layout.sections.get(".lwram_camera_capture")
    capture_symbol = _c_symbol(layout, "sourceboot_camera_idle_capture")
    if route == 0:
        if capture is not None and capture.size != 0:
            raise ValueError("route 0 contains SCC1 capture storage")
        if capture_symbol is not None:
            raise ValueError("route 0 contains SCC1 capture symbol")
    else:
        if capture is None or capture.kind != "NOBITS" or \
                capture.address != SCC_START or capture.size != SCC_SIZE:
            raise ValueError("route 1 SCC1 section layout is wrong")
        if capture_symbol is None or capture_symbol.address != SCC_START or \
                capture_symbol.size != SCC_SIZE:
            raise ValueError("route 1 SCC1 symbol layout is wrong")
        if capture.address + capture.size != SCC_END:
            raise ValueError("route 1 SCC1 end address is wrong")
        for section in (command_banks, layout.sections.get(".lwram_bss")):
            if section is None:
                raise ValueError("ELF lacks LWRAM bulk section")
            if _ranges_overlap(capture, section):
                raise ValueError(f"SCC1 overlaps {section.name}")
        if LWRAM_TOP - SCC_END < 0x4000:
            raise ValueError("SCC1 leaves less than the LWRAM floor")
    lwram_end = max(
        command_banks.address + command_banks.size
        if command_banks.address < LWRAM_TOP else LWRAM_BASE,
        lwram_bulk.address + lwram_bulk.size,
        capture.address + capture.size if capture is not None else LWRAM_BASE,
        *(
            section.address + section.size
            for name in (".lwram_actor_runtime", ".lwram_camera_capture")
            if (section := layout.sections.get(name)) is not None
        ),
    )
    if LWRAM_TOP - lwram_end < MINIMUM_LWRAM_MARGIN:
        raise ValueError("ELF LWRAM margin is below required final floor")
    return {
        "path": str(layout.path), "elf_sha256": layout.sha256,
        "end": end.address, "hwram_margin": HWRAM_TOP - end.address,
        "stage_bytes": stage_bytes,
        "capture_address": capture.address if capture else None,
        "capture_size": capture.size if capture else 0,
        "command_bank_address": command_banks.address,
        "command_bank_size": command_banks.size,
        "gouraud_staging_address": gouraud.address,
        "gouraud_staging_size": gouraud.size,
        "uncached_address": uncached.address,
        "uncached_size": uncached.size,
        "lwram_end": lwram_end,
        "lwram_margin": LWRAM_TOP - lwram_end,
    }


def select_transport(
    *, baseline_elf: Path, stage8_elf: Path, stage4_elf: Path,
    baseline_end: int, required_post_transport_margin: int,
    required_final_margin: int,
) -> tuple[dict[str, Any], dict[str, Any]]:
    baseline_layout = inspect_elf(baseline_elf)
    stage8_layout = inspect_elf(stage8_elf)
    stage4_layout = inspect_elf(stage4_elf)
    baseline = validate_layout(
        baseline_layout, route=0, stage_sectors=16,
        required_final_margin=required_final_margin,
    )
    if baseline["end"] != baseline_end:
        raise ValueError("baseline ___end does not match the pinned baseline")
    stage8 = validate_layout(
        stage8_layout, route=1, stage_sectors=8,
        required_final_margin=required_final_margin,
    )
    stage4 = validate_layout(
        stage4_layout, route=1, stage_sectors=4,
        required_final_margin=required_final_margin,
    )
    selected = stage8 if stage8["hwram_margin"] >= required_post_transport_margin else stage4
    selected_sectors = 8 if selected is stage8 else 4
    if selected["hwram_margin"] < required_post_transport_margin:
        raise ValueError("neither transport staging size leaves the required HWRAM margin")
    fixture = {
        "schema": "sm64-saturn-camera-memory-v1",
        "stage_sectors": selected_sectors,
        "required_post_transport_margin": required_post_transport_margin,
        "required_final_margin": required_final_margin,
        "baseline_end": baseline["end"],
        "selected_end": selected["end"],
        "selected_elf_sha256": selected["elf_sha256"],
        "cart_stage_bytes": selected["stage_bytes"],
        "hwram_delta": selected["end"] - baseline["end"],
        "hwram_margin": selected["hwram_margin"],
        "lwram_capture_start": SCC_START,
        "lwram_capture_end": SCC_END,
        "lwram_remaining": LWRAM_TOP - SCC_END,
    }
    report = {
        "schema": "sm64-saturn-sourceboot-memory-phase-v1",
        "phase": "transport",
        "selected_stage_sectors": selected_sectors,
        "required_post_transport_margin": required_post_transport_margin,
        "required_final_margin": required_final_margin,
        "baseline": baseline, "stage8": stage8, "stage4": stage4,
        "selected": selected,
        "lwram": {
            "current_end": SCC_START, "capture_start": SCC_START,
            "capture_end": SCC_END, "remaining": LWRAM_TOP - SCC_END,
            "discovery_allowance": LWRAM_TOP - SCC_END - 0x2000,
        },
    }
    return fixture, report


def check_phase(
    *, elf: Path, phase: str, stage_sectors: int,
    previous_report: dict[str, Any] | None, required_final_margin: int,
) -> dict[str, Any]:
    if phase not in PHASE_PREDECESSORS:
        raise ValueError("phase is stale or terminal")
    if previous_report is None:
        raise ValueError("phase report requires a predecessor")
    predecessor = previous_report.get("phase")
    if predecessor not in PHASE_PREDECESSORS[phase]:
        raise ValueError("phase report does not follow the approved predecessor graph")
    selected_stage = previous_report.get("selected_stage_sectors")
    if selected_stage != stage_sectors:
        raise ValueError("phase stage does not match the selected transport stage")
    previous_selected = previous_report.get("selected")
    if not isinstance(previous_selected, dict) or \
            not isinstance(previous_selected.get("end"), int) or \
            not isinstance(previous_selected.get("elf_sha256"), str):
        raise ValueError("predecessor report lacks hash-bound selected layout")
    layout = inspect_elf(elf)
    current = validate_layout(
        layout, route=1, stage_sectors=stage_sectors,
        required_final_margin=required_final_margin,
    )
    return {
        "schema": "sm64-saturn-sourceboot-memory-phase-v1",
        "phase": phase, "predecessor_phase": predecessor,
        "predecessor_elf_sha256": previous_selected["elf_sha256"],
        "selected_stage_sectors": stage_sectors,
        "required_final_margin": required_final_margin,
        "incremental_end_delta": current["end"] - previous_selected["end"],
        "selected": current,
    }


def _identity_spec_for(elf: Path) -> dict[str, Any] | None:
    """Read the sealed identity spec adjacent to a built identity ELF.

    Identity ELFs live at <build>/e2-bob-identity-*/obj/<name>.elf and the
    frozen spec at <build>/generated/saturn_build_identity_spec.json -- the
    same file `_tag_gate` binds short-hash identity directories to.
    """
    try:
        spec = elf.resolve().parents[2] / "generated" / "saturn_build_identity_spec.json"
        return json.loads(spec.read_text(encoding="utf-8"))
    except (OSError, IndexError, json.JSONDecodeError):
        return None


def run_verify(*, elf: Path, required_final_margin: int) -> int:
    """Verify one built ELF's HWRAM/LWRAM margins as a build output."""
    try:
        layout = inspect_elf(elf)
    except (ValueError, OSError) as error:
        # Infrastructure failures (missing ELF, missing toolchain, nonzero
        # readelf/nm) must honor the same RESULT contract as layout failures.
        print(f"verify: {elf}")
        print(f"  RESULT          = FAIL: {error}")
        return 1
    print(f"verify: {layout.path}")
    end = layout.symbols.get("___end")
    if end is None:
        print("  ___end          = MISSING")
    else:
        remaining = HWRAM_TOP - end.address
        print(f"  ___end          = 0x{end.address:08X}")
        print(f"  hwram_remaining = 0x{remaining:X} bytes "
              f"(required >= 0x{required_final_margin:X})")
    spec = _identity_spec_for(elf)
    if spec is None or not isinstance(spec.get("camera_route"), int) or \
            not isinstance(spec.get("cart_stage_sectors"), int):
        print("  RESULT          = FAIL: sealed identity spec "
              "(generated/saturn_build_identity_spec.json) is unavailable "
              "next to the ELF; cannot bind camera route and cart stage")
        return 1
    try:
        report = validate_layout(
            layout, route=spec["camera_route"],
            stage_sectors=spec["cart_stage_sectors"],
            required_final_margin=required_final_margin,
        )
    except ValueError as error:
        print(f"  RESULT          = FAIL: {error}")
        return 1
    print(f"  lwram_end       = 0x{report['lwram_end']:08X}")
    print(f"  lwram_remaining = 0x{report['lwram_margin']:X} bytes "
          f"(floor >= 0x{MINIMUM_LWRAM_MARGIN:X})")
    print("  RESULT          = OK")
    return 0


def _integer(value: str) -> int:
    return int(value, 0)


def _write(path: Path, value: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n",
                    encoding="utf-8")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    select = commands.add_parser("select-transport")
    select.add_argument("--baseline-elf", type=Path, required=True)
    select.add_argument("--stage8-elf", type=Path, required=True)
    select.add_argument("--stage4-elf", type=Path, required=True)
    select.add_argument("--baseline-end", type=_integer, required=True)
    select.add_argument("--required-post-transport-margin", type=_integer, required=True)
    select.add_argument("--required-final-margin", type=_integer, required=True)
    select.add_argument("--fixture-output", type=Path, required=True)
    select.add_argument("--output", type=Path, required=True)
    phase = commands.add_parser("check-phase")
    phase.add_argument("--elf", type=Path, required=True)
    phase.add_argument("--phase", required=True)
    phase.add_argument("--stage-sectors", type=int, choices=(4, 8), required=True)
    phase.add_argument("--previous-report", type=Path, required=True)
    phase.add_argument("--required-final-margin", type=_integer, required=True)
    phase.add_argument("--output", type=Path, required=True)
    single = commands.add_parser(
        "verify", help="verify one built ELF's HWRAM/LWRAM margins")
    single.add_argument("--elf", type=Path, required=True)
    single.add_argument("--required-final-margin", type=_integer, default=0x1F00)
    args = parser.parse_args(argv)
    if args.command == "verify":
        return run_verify(elf=args.elf,
                          required_final_margin=args.required_final_margin)
    if args.command == "select-transport":
        fixture, report = select_transport(
            baseline_elf=args.baseline_elf, stage8_elf=args.stage8_elf,
            stage4_elf=args.stage4_elf, baseline_end=args.baseline_end,
            required_post_transport_margin=args.required_post_transport_margin,
            required_final_margin=args.required_final_margin,
        )
        _write(args.fixture_output, fixture)
        _write(args.output, report)
    else:
        previous = json.loads(args.previous_report.read_text(encoding="utf-8"))
        report = check_phase(
            elf=args.elf, phase=args.phase, stage_sectors=args.stage_sectors,
            previous_report=previous,
            required_final_margin=args.required_final_margin,
        )
        _write(args.output, report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
