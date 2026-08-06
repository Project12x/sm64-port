#!/usr/bin/env python3
"""Reject warned Yaul convenience sound-CPU calls in project production C."""

from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SoundCpuCommandOwnershipTest(unittest.TestCase):
    def test_warned_convenience_calls_are_absent_from_production(self) -> None:
        offenders = []
        for path in (ROOT / "src/port/saturn").rglob("*.c"):
            text = path.read_text(encoding="utf-8")
            for symbol in ("smpc_smc_sndoff_call(", "smpc_smc_sndon_call("):
                if symbol in text:
                    offenders.append(f"{path.relative_to(ROOT)}:{symbol[:-1]}")
        self.assertEqual(offenders, [])

    def test_generic_commands_are_owned_only_by_project_wrapper(self) -> None:
        owners = []
        for path in (ROOT / "src/port/saturn").rglob("*.c"):
            if "smpc_smc_call(" in path.read_text(encoding="utf-8"):
                owners.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(owners, ["src/port/saturn/audio/saturn_sound_cpu.c"])


if __name__ == "__main__":
    unittest.main()
