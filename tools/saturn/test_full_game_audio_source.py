#!/usr/bin/env python3
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
EXTERNAL_H = ROOT / "src/audio/external.h"
SEMANTICS_C = ROOT / "src/port/saturn/sourceboot/source_audio_semantics.c"
STUB_C = ROOT / "src/port/saturn/sourceboot/source_audio_stub.c"
SOURCEBOOT_MAKE = ROOT / "src/port/saturn/sourceboot/Makefile"


def public_functions(text: str) -> set[str]:
    return set(re.findall(r"^(?:struct SPTask \*|void|u16)\s*(\w+)\s*\(", text, re.M))


class FullGameAudioSourceContract(unittest.TestCase):
    def test_semantic_translation_unit_owns_every_external_signature(self) -> None:
        declared = public_functions(EXTERNAL_H.read_text(encoding="utf-8"))
        defined = public_functions(SEMANTICS_C.read_text(encoding="utf-8"))
        self.assertEqual(declared, defined)

    def test_silent_stub_remains_a_complete_feature_off_rollback(self) -> None:
        declared = public_functions(EXTERNAL_H.read_text(encoding="utf-8"))
        defined = public_functions(STUB_C.read_text(encoding="utf-8"))
        self.assertEqual(declared, defined)

    def test_build_selects_exactly_one_semantic_symbol_owner(self) -> None:
        makefile = SOURCEBOOT_MAKE.read_text(encoding="utf-8")
        self.assertIn("ifeq ($(SATURN_FEATURE_SEMANTIC_AUDIO),1)", makefile)
        self.assertIn("source_audio_semantics.c", makefile)
        self.assertIn("source_audio_stub.c", makefile)
        self.assertNotRegex(makefile, r"SH_SRCS\s*:=.*source_audio_stub\.c")

    def test_transport_payload_has_no_pointer_width_field(self) -> None:
        header = (ROOT / "src/port/saturn/audio/saturn_audio_policy.h").read_text(encoding="utf-8")
        match = re.search(
            r"typedef struct sm64_saturn_audio_play_refresh \{(?P<body>.*?)\}",
            header,
            re.S,
        )
        self.assertIsNotNone(match)
        body = match.group("body")
        self.assertNotIn("*", body)
        self.assertNotIn("uintptr_t", body)
        self.assertIn("uint16_t source_token", body)
        self.assertIn("uint16_t package_generation", body)
        self.assertIn("uint16_t freshness_generation", body)


if __name__ == "__main__":
    unittest.main()
