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
PCM_VOICE_C = ROOT / "src/port/saturn/audio68k/pcm_voice.c"


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

    def test_normal_bob_sourceboot_starts_level_music_through_semantic_api(self) -> None:
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(
            encoding="utf-8"
        )
        self.assertIn('#include "seq_ids.h"', main)
        self.assertRegex(
            main,
            re.compile(
                r"sourceboot_audio_init\(\).*?play_music\(SEQ_PLAYER_LEVEL,\s*"
                r"SEQUENCE_ARGS\(4,\s*SEQ_LEVEL_GRASS\),\s*0U\)",
                re.S,
            ),
        )

    def test_semantic_music_runs_at_consumer_audio_cadence(self) -> None:
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        self.assertRegex(
            pcm_voice,
            r"SM64_SATURN_PCM_MUSIC_POLLS_PER_TICK\s*=\s*1U",
            "a multi-second music poll divider is silent on the live 240 Hz consumer",
        )

    def test_m68k_mapped_zero_audio_ram_is_not_rejected_as_null(self) -> None:
        makefile = (ROOT / "src/port/saturn/audio68k/Makefile").read_text(
            encoding="utf-8"
        )
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        self.assertIn("-DSM64_SATURN_PCM_MAPPED_ZERO=1", makefile)
        self.assertIn("!defined(SM64_SATURN_PCM_MAPPED_ZERO)", pcm_voice)
        self.assertIn("sm64_saturn_pcm68k_consume_mapped_zero", pcm_voice)

    def test_normal_music_has_a_target_safe_packaged_sample_fallback(self) -> None:
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        self.assertIn("SM64_SATURN_PCM_MUSIC_DIRECT_FALLBACK", pcm_voice)
        self.assertIn("sm64_saturn_pcm_play_sample(state, sample_index", pcm_voice)
        self.assertIn(
            "diagnostic-only when the target scalar ABI rejects",
            pcm_voice,
        )


if __name__ == "__main__":
    unittest.main()
