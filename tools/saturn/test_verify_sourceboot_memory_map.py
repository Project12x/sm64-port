#!/usr/bin/env python3
"""CLI-level tests for the hash-bound sourceboot memory-map gate."""
from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import verify_sourceboot_memory_map as verify


def image(name: str, *, end: int, stage: int, scc: bool,
          camera_variant: int = 1,
          uncached_start: int = 0x060F8000) -> verify.ElfLayout:
    sections = {
        ".uncached": verify.Section(
            ".uncached", 0x20000000 | uncached_start,
            max(0, end - uncached_start), "PROGBITS"
        ),
        ".lwram_cmdts": verify.Section(".lwram_cmdts", 0x00200000, 0x20000, "NOBITS"),
        ".lwram_bss": verify.Section(".lwram_bss", 0x00240000, 0x8BB20, "NOBITS"),
    }
    symbols = {
        "___end": verify.Symbol("___end", end, 0),
        "s_source_cart_stage": verify.Symbol("s_source_cart_stage", 0x06080000, stage * 2048),
        "sourceboot_gouraud_staging": verify.Symbol(
            "sourceboot_gouraud_staging", 0x06070000, 2 * 1536 * 8
        ),
        "sm64_saturn_camera_variant_marker": verify.Symbol(
            "sm64_saturn_camera_variant_marker", camera_variant, 0
        ),
        "sm64_saturn_camera_route_marker": verify.Symbol("sm64_saturn_camera_route_marker", 1 if scc else 0, 0),
    }
    if scc:
        sections[".lwram_camera_capture"] = verify.Section(
            ".lwram_camera_capture", 0x002CBB20, 0x2F7C0, "NOBITS"
        )
        symbols["sourceboot_camera_idle_capture"] = verify.Symbol(
            "sourceboot_camera_idle_capture", 0x002CBB20, 0x2F7C0
        )
    return verify.ElfLayout(Path(name), name * 8, sections, symbols)


class VerifySourcebootMemoryMapTest(unittest.TestCase):
    def test_accepts_initialized_uncached_progbits(self) -> None:
        layout = image("initialized-uncached", end=0x060F9000, stage=8, scc=False)

        result = verify.validate_layout(
            layout, route=0, stage_sectors=8,
            required_final_margin=0x1B00,
        )

        self.assertEqual(result["uncached_size"], 0x1000)

    def test_rejects_uncached_nobits_that_would_omit_slave_entry_bytes(self) -> None:
        layout = image("missing-uncached-bytes", end=0x060F9000, stage=8, scc=False)
        layout.sections[".uncached"] = verify.Section(
            ".uncached", 0x260F8000, 0x1000, "NOBITS"
        )

        with self.assertRaisesRegex(ValueError, "PROGBITS"):
            verify.validate_layout(
                layout, route=0, stage_sectors=8,
                required_final_margin=0x1B00,
            )

    def test_accepts_all_three_named_phase_a_camera_roles(self) -> None:
        for variant in (1, 2, 3):
            with self.subTest(variant=variant):
                layout = image("role", end=0x060F9000, stage=8, scc=True,
                               camera_variant=variant)
                verify.validate_layout(layout, route=1, stage_sectors=8,
                                       required_final_margin=0x1B00)
        invalid = image("unknown-role", end=0x060F9000, stage=8, scc=True,
                        camera_variant=4)
        with self.assertRaisesRegex(ValueError, "variant marker is not a Phase A camera role"):
            verify.validate_layout(invalid, route=1, stage_sectors=8,
                                   required_final_margin=0x1B00)

    def test_make_verify_passes_the_resolved_readelf_to_native_math_audits(self) -> None:
        makefile = (Path(__file__).resolve().parents[2] / "src" / "port" /
                    "saturn" / "sourceboot" / "Makefile").read_text(encoding="utf-8")
        audit_invocations = makefile.count(
            '"$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_sh2_native_math.py"'
        )
        self.assertEqual(audit_invocations, 2)
        self.assertEqual(makefile.count('--readelf "$(SOURCEBOOT_SH_READELF)"'), 2)

    def test_selects_stage8_when_it_meets_post_transport_floor(self) -> None:
        baseline = image("baseline", end=0x060FDCB0, stage=16, scc=False)
        stage8 = image("stage8", end=0x060FA400, stage=8, scc=True)
        stage4 = image("stage4", end=0x060F8400, stage=4, scc=True)
        with mock.patch("verify_sourceboot_memory_map.inspect_elf",
                        side_effect=[baseline, stage8, stage4]):
            fixture, report = verify.select_transport(
                baseline_elf=Path("baseline"), stage8_elf=Path("stage8"),
                stage4_elf=Path("stage4"), baseline_end=0x060FDCB0,
                required_post_transport_margin=0x5B00,
                required_final_margin=0x1B00,
            )
        self.assertEqual(fixture["stage_sectors"], 8)
        self.assertEqual(report["selected"]["hwram_margin"], 0x5C00)
        self.assertEqual(report["lwram"]["current_end"], 0x002CBB20)
        self.assertEqual(report["lwram"]["capture_end"], 0x002FB2E0)
        self.assertEqual(report["lwram"]["remaining"], 0x4D20)
        self.assertEqual(report["lwram"]["discovery_allowance"], 0x2D20)

    def test_selects_stage4_only_when_stage8_misses_floor(self) -> None:
        layouts = [
            image("baseline", end=0x060FDCB0, stage=16, scc=False),
            image("stage8", end=0x060FA600, stage=8, scc=True),
            image("stage4", end=0x060F8600, stage=4, scc=True),
        ]
        with mock.patch("verify_sourceboot_memory_map.inspect_elf", side_effect=layouts):
            fixture, _ = verify.select_transport(
                baseline_elf=Path("baseline"), stage8_elf=Path("stage8"),
                stage4_elf=Path("stage4"), baseline_end=0x060FDCB0,
                required_post_transport_margin=0x5B00,
                required_final_margin=0x1B00,
            )
        self.assertEqual(fixture["stage_sectors"], 4)

    def test_rejects_missing_end_wrong_stage_overlap_and_low_margins(self) -> None:
        valid = image("valid", end=0x060F9000, stage=8, scc=True)
        mutations = []
        missing = image("missing", end=0x060F9000, stage=8, scc=True)
        del missing.symbols["___end"]
        mutations.append(missing)
        wrong = image("wrong", end=0x060F9000, stage=8, scc=True)
        wrong.symbols["s_source_cart_stage"] = verify.Symbol("s_source_cart_stage", 0x06080000, 7 * 2048)
        mutations.append(wrong)
        overlap = image("overlap", end=0x060F9000, stage=8, scc=True)
        overlap.sections[".lwram_bss"] = verify.Section(".lwram_bss", 0x002F0000, 0x10000, "NOBITS")
        mutations.append(overlap)
        low_final = image("low-final", end=0x060FE600, stage=8, scc=True)
        mutations.append(low_final)
        for layout in mutations:
            with self.subTest(layout=layout.path.name), self.assertRaises(ValueError):
                verify.validate_layout(layout, route=1, stage_sectors=8,
                                       required_final_margin=0x1B00)
        self.assertEqual(
            verify.validate_layout(valid, route=1, stage_sectors=8,
                                   required_final_margin=0x1B00)["hwram_margin"],
            0x7000,
        )

    def test_rejects_uncached_end_above_hwram_before_margin_subtraction(self) -> None:
        overflow = image(
            "overflow", end=0x061040D0, stage=8, scc=False,
            uncached_start=0x060FD7D0,
        )
        self.assertEqual(overflow.sections[".uncached"].size, 0x6900)
        with self.assertRaisesRegex(ValueError, "past HWRAM top"):
            verify.validate_layout(
                overflow, route=0, stage_sectors=8,
                required_final_margin=0x1B00,
            )

    def test_rejects_uncached_section_that_disagrees_with_cached_end(self) -> None:
        layout = image("uncached-mismatch", end=0x060F9000, stage=8, scc=False)
        layout.sections[".uncached"] = verify.Section(
            ".uncached", 0x260F8000, 0x0F00, "PROGBITS"
        )
        with self.assertRaisesRegex(ValueError, "uncached section end"):
            verify.validate_layout(
                layout, route=0, stage_sectors=8,
                required_final_margin=0x1B00,
            )

    def test_rejects_route0_lwram_bulk_storage_below_final_margin(self) -> None:
        layout = image("low-lwram", end=0x060F9000, stage=8, scc=False)
        layout.sections[".lwram_bss"] = verify.Section(
            ".lwram_bss", 0x002F0000, 0xD000, "NOBITS"
        )
        with self.assertRaisesRegex(ValueError, "LWRAM margin"):
            verify.validate_layout(
                layout, route=0, stage_sectors=8,
                required_final_margin=0x1B00,
            )

    def test_rejects_wrong_command_bank_and_gouraud_regions(self) -> None:
        mutations = []
        wrong_size = image("wrong-command-size", end=0x060F9000, stage=8, scc=True)
        wrong_size.sections[".lwram_cmdts"] = verify.Section(
            ".lwram_cmdts", 0x00200000, 0x1FFE0, "NOBITS"
        )
        mutations.append(wrong_size)
        unaligned = image("unaligned-command", end=0x060F9000, stage=8, scc=True)
        unaligned.sections[".lwram_cmdts"] = verify.Section(
            ".lwram_cmdts", 0x00200010, 0x20000, "NOBITS"
        )
        mutations.append(unaligned)
        command_in_hwram = image("command-in-hwram", end=0x060F9000, stage=8, scc=True)
        command_in_hwram.sections[".lwram_cmdts"] = verify.Section(
            ".lwram_cmdts", 0x06020000, 0x20000, "NOBITS"
        )
        mutations.append(command_in_hwram)
        gouraud_in_lwram = image("gouraud-in-lwram", end=0x060F9000, stage=8, scc=True)
        gouraud_in_lwram.symbols["sourceboot_gouraud_staging"] = verify.Symbol(
            "sourceboot_gouraud_staging", 0x00220000, 2 * 1536 * 8
        )
        mutations.append(gouraud_in_lwram)
        wrong_gouraud_size = image("wrong-gouraud-size", end=0x060F9000, stage=8, scc=True)
        wrong_gouraud_size.symbols["sourceboot_gouraud_staging"] = verify.Symbol(
            "sourceboot_gouraud_staging", 0x06070000, 2 * 1536 * 8 - 8
        )
        mutations.append(wrong_gouraud_size)
        missing_gouraud = image("missing-gouraud", end=0x060F9000, stage=8, scc=True)
        del missing_gouraud.symbols["sourceboot_gouraud_staging"]
        mutations.append(missing_gouraud)
        for layout in mutations:
            with self.subTest(layout=layout.path.name), self.assertRaises(ValueError):
                verify.validate_layout(layout, route=1, stage_sectors=8,
                                       required_final_margin=0x1B00)

    def test_rejects_a_configured_final_margin_below_a7_floor(self) -> None:
        layout = image("low-configured-floor", end=0x060F9000, stage=8, scc=True)
        with self.assertRaisesRegex(ValueError, "required final floor"):
            verify.validate_layout(layout, route=1, stage_sectors=8,
                                   required_final_margin=0x1000)

    def test_accepts_sh_abi_leading_underscore_on_c_symbols(self) -> None:
        layout = image("sh-abi", end=0x060F9000, stage=8, scc=True)
        for name in ("s_source_cart_stage", "sourceboot_gouraud_staging",
                     "sourceboot_camera_idle_capture"):
            symbol = layout.symbols.pop(name)
            layout.symbols[f"_{name}"] = verify.Symbol(
                f"_{name}", symbol.address, symbol.size
            )
        result = verify.validate_layout(layout, route=1, stage_sectors=8,
                                        required_final_margin=0x1B00)
        self.assertEqual(result["gouraud_staging_size"], 0x6000)

    def test_check_phase_requires_forward_chain_stage_and_recorded_delta(self) -> None:
        prior = {
            "phase": "transport", "selected_stage_sectors": 8,
            "selected": {"elf_sha256": "a" * 64, "end": 0x060F9000},
        }
        current = image("fixed", end=0x060F9100, stage=8, scc=True)
        with mock.patch("verify_sourceboot_memory_map.inspect_elf", return_value=current):
            report = verify.check_phase(
                elf=Path("fixed"), phase="fixed-baseline", stage_sectors=8,
                previous_report=prior, required_final_margin=0x1B00,
            )
        self.assertEqual(report["incremental_end_delta"], 0x100)
        self.assertEqual(report["predecessor_phase"], "transport")
        for phase, stage, previous in (
            ("shadow", 8, prior), ("fixed-baseline", 4, prior),
            ("transport", 8, prior), ("fixed-baseline", 8, None),
        ):
            with self.subTest(phase=phase, stage=stage), self.assertRaises(ValueError):
                with mock.patch("verify_sourceboot_memory_map.inspect_elf", return_value=current):
                    verify.check_phase(
                        elf=Path("fixed"), phase=phase, stage_sectors=stage,
                        previous_report=previous, required_final_margin=0x1B00,
                    )

    def test_cli_writes_hash_bound_fixture_and_report(self) -> None:
        layouts = [
            image("baseline", end=0x060FDCB0, stage=16, scc=False),
            image("stage8", end=0x060FA400, stage=8, scc=True),
            image("stage4", end=0x060F8400, stage=4, scc=True),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            fixture, output = root / "fixture.json", root / "report.json"
            argv = [
                "select-transport", "--baseline-elf", "baseline",
                "--stage8-elf", "stage8", "--stage4-elf", "stage4",
                "--baseline-end", "0x060FDCB0",
                "--required-post-transport-margin", "0x5B00",
                "--required-final-margin", "0x1B00",
                "--fixture-output", str(fixture), "--output", str(output),
            ]
            with mock.patch("verify_sourceboot_memory_map.inspect_elf", side_effect=layouts):
                self.assertEqual(verify.main(argv), 0)
            fixture_json = json.loads(fixture.read_text(encoding="utf-8"))
            report_json = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(fixture_json["stage_sectors"], 8)
        self.assertEqual(report_json["selected"]["elf_sha256"], "stage8" * 8)


if __name__ == "__main__":
    unittest.main()
