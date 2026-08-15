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
MAIN_C = ROOT / "src/port/saturn/sourceboot/main.c"
PCM_VOICE_C = ROOT / "src/port/saturn/audio68k/pcm_voice.c"
SOUND_CPU_C = ROOT / "src/port/saturn/audio/saturn_sound_cpu.c"
AUDIO_LIVE_C = ROOT / "src/port/saturn/sourceboot/source_audio_live.c"

# Sound-RAM facts measured on the R1 candidate by
# tools/saturn/probe_sound_ram_verify.py (2026-08-15).
SOUND_RAM_BYTES = 0x80000
STAGED_BANK_END = 0x4AB49
BIOS_LEFTOVER_RING_BASE = 0x30000

INFINITE_SPIN = re.compile(r"for\s*\(\s*;\s*;\s*\)|while\s*\(\s*1\s*\)")


def public_functions(text: str) -> set[str]:
    """Inherited N64 audio ABI names owned by the translation unit.

    Saturn-internal workspace APIs (``sm64_saturn_``-prefixed bind/bytes/
    reset entries) are not part of the external.h ownership contract and are
    excluded so both the semantic unit and the stub stay comparable to the
    inherited header.
    """
    return {
        name
        for name in re.findall(
            r"^(?:struct SPTask \*|void|u16)\s*(\w+)\s*\(", text, re.M
        )
        if not name.startswith("sm64_saturn_")
    }


def function_body(text: str, name: str) -> str:
    """Extract a function definition's body by brace counting.

    Anchors on ``name(...)`` followed by an opening brace so the forward
    declaration (which ends with ``;``) is skipped.
    """
    for match in re.finditer(rf"\b{re.escape(name)}\s*\([^;{{)]*\)", text):
        rest = text[match.end():]
        stripped = rest.lstrip()
        if not stripped.startswith("{"):
            continue
        start = match.end() + (len(rest) - len(stripped))
        depth = 0
        for index in range(start, len(text)):
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
                if depth == 0:
                    return text[match.start():index + 1]
        raise AssertionError(f"unbalanced braces after {name}")
    raise AssertionError(f"no definition found for {name}")


def require_after(text: str, earlier: str, later: str, why: str) -> None:
    """Assert regex ``later`` matches somewhere after regex ``earlier``.

    Regex anchors (not fixed offsets or line numbers) so reformatting the
    C source cannot silently retire the ordering guarantee.
    """
    earlier_match = re.search(earlier, text)
    if earlier_match is None:
        raise AssertionError(f"missing required step {earlier!r}: {why}")
    if re.search(later, text[earlier_match.end():]) is None:
        raise AssertionError(f"{later!r} must follow {earlier!r}: {why}")


def enum_value(text: str, name: str) -> int:
    """Read an enumerator's literal value out of C source."""
    match = re.search(rf"\b{re.escape(name)}\s*=\s*(0[xX][0-9A-Fa-f]+|\d+)",
                      text)
    if match is None:
        raise AssertionError(f"no enumerator named {name}")
    return int(match.group(1), 0)


def preprocessor_block_around(text: str, anchor: str) -> str:
    """Extract the innermost #if...#endif block containing ``anchor``."""
    anchor_at = text.index(anchor)
    open_at = text.rindex("#if", 0, anchor_at)
    depth = 0
    for match in re.finditer(r"^[ \t]*#[ \t]*(if\w*|endif)", text[open_at:], re.M):
        if match.group(1).startswith("if"):
            depth += 1
        else:
            depth -= 1
            if depth == 0:
                return text[open_at:open_at + match.end()]
    raise AssertionError(f"unterminated #if block around {anchor!r}")


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

    def test_level_script_owns_music_not_a_sourceboot_bootstrap_call(self) -> None:
        """The level script is the sole owner of the level music start.

        sourceboot used to issue its own bootstrap ``play_music`` on the claim
        that the generic BOB path had no level-update caller.  A control-ring
        dump disproved that: the ring carried SET_MASTER, SEQ_START, RESET,
        SEQ_START, and the trailing pair is the game's own
        ``init_mario_after_warp`` -> ``set_background_music`` responding to
        BOB's ``SET_BACKGROUND_MUSIC``.  The bootstrap call bypassed
        sound_init.c's ``sCurrentMusic`` bookkeeping, so the game's guard could
        not suppress the duplicate.  Ownership must stay in the level script.
        """
        main = (ROOT / "src/port/saturn/sourceboot/main.c").read_text(
            encoding="utf-8"
        )
        self.assertNotRegex(
            main,
            re.compile(r"^[^\S\n]*play_music\s*\(", re.M),
            "sourceboot must not issue a bootstrap play_music; the level "
            "script owns music so sCurrentMusic bookkeeping stays correct",
        )
        script = (ROOT / "levels/bob/script.c").read_text(encoding="utf-8")
        self.assertRegex(
            script,
            re.compile(r"SET_BACKGROUND_MUSIC\([^)]*SEQ_LEVEL_GRASS", re.S),
            "music ownership must still exist somewhere: BOB's level script "
            "must carry SET_BACKGROUND_MUSIC with SEQ_LEVEL_GRASS",
        )

    def test_audio_init_failure_does_not_hang(self) -> None:
        """Audio boot failure must mute audio only, never stall the console.

        The constitution's fail-open rule forbids an infinite spin anywhere in
        the audio init/boot path: a bad SFXB bundle or sound-CPU handshake
        timeout must fall through to the unbound (fail-closed no-op) semantic
        layer and continue boot.  main.c's legitimate spins (build identity,
        cart load, render init, the main frame loop) are outside these regions
        and stay untouched.
        """
        text = MAIN_C.read_text(encoding="utf-8")
        audio_init = function_body(text, "sourceboot_audio_init")
        self.assertNotRegex(
            audio_init, INFINITE_SPIN,
            "sourceboot_audio_init must report failure, not spin",
        )
        # The feature-gated caller block in sourceboot_game_loop that invokes
        # sourceboot_audio_init() and starts level music.
        caller_block = preprocessor_block_around(text, "sourceboot_audio_init()")
        self.assertIn("SATURN_FEATURE_SEMANTIC_AUDIO", caller_block.splitlines()[0])
        self.assertNotRegex(
            caller_block, INFINITE_SPIN,
            "audio boot failure must fall through to the silent no-op path, "
            "not hang the console",
        )

    def test_audio_init_resets_unbound_state_before_any_bind(self) -> None:
        """Unbound audio state must be explicitly cleared before any bind.

        Both modules keep their workspace pointer in NOLOAD .lwram_bss,
        which is never crt0-zeroed: the fail-closed contract (unbound means
        s_state == NULL means every entry no-ops) only holds if sourceboot
        parks both modules unbound before the first bind attempt.  Without
        the resets a failed init on real hardware leaves garbage pointers
        that game audio calls would dereference.
        """
        text = MAIN_C.read_text(encoding="utf-8")
        body = function_body(text, "sourceboot_audio_init")
        first_bind = min(
            body.index("sm64_saturn_source_audio_live_workspace_bind"),
            body.index("sm64_saturn_source_audio_semantic_workspace_bind"),
        )
        for reset in (
            "sm64_saturn_source_audio_semantics_reset()",
            "sm64_saturn_source_audio_live_reset()",
        ):
            self.assertIn(
                reset, body,
                f"{reset} must park never-zeroed LWRAM state unbound",
            )
            self.assertLess(
                body.index(reset), first_bind,
                f"{reset} must run before any workspace bind attempt",
            )

    def test_music_is_hardware_looped_not_serviced(self) -> None:
        """Music is one hardware-looped SCSP sample, not a serviced cadence.

        Tasks 4-6 removed the sequence-VM era's retrigger/cadence machinery
        (music_service, music_fallback_period, the MUSIC_POLLS_PER_TICK
        divider) along with the MUSIC_DIRECT_FALLBACK define.  What replaces
        the cadence is the SM64_SATURN_PCM_SAMPLE_LOOP flag reaching the SCSP
        voice start: a non-looped row is a music fault, never a retrigger
        schedule.
        """
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        for removed in (
            "music_service",
            "music_fallback_period",
            "SM64_SATURN_PCM_MUSIC_POLLS_PER_TICK",
            "MUSIC_DIRECT_FALLBACK",
        ):
            self.assertNotIn(
                removed, pcm_voice,
                f"{removed} belongs to the removed sequence-VM era",
            )
        music_start = function_body(pcm_voice, "sm64_saturn_pcm_music_start")
        self.assertIn(
            "SM64_SATURN_PCM_SAMPLE_LOOP", music_start,
            "the loop flag gating the SCSP start is the music contract",
        )
        self.assertIn("sm64_saturn_pcm_start_voice", music_start)

    def test_m68k_mapped_zero_audio_ram_is_not_rejected_as_null(self) -> None:
        makefile = (ROOT / "src/port/saturn/audio68k/Makefile").read_text(
            encoding="utf-8"
        )
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        self.assertIn("-DSM64_SATURN_PCM_MAPPED_ZERO=1", makefile)
        self.assertIn("!defined(SM64_SATURN_PCM_MAPPED_ZERO)", pcm_voice)
        self.assertIn("sm64_saturn_pcm68k_consume_mapped_zero", pcm_voice)

    def test_music_starts_from_bundle_trailer_row_on_slot0(self) -> None:
        """Music comes from the SFXB trailer row, started on the pinned slot.

        The looped trailer-row path IS the target-safe packaged-sample music
        (there is no fallback define): sm64_saturn_pcm_music_start reads the
        trailer's music_sample_index field and starts that row on the pinned
        music slot.  Presence discrimination: a zero index means the bundle
        carries no music -- silence, not a fault.
        """
        pcm_voice = PCM_VOICE_C.read_text(encoding="utf-8")
        music_start = function_body(pcm_voice, "sm64_saturn_pcm_music_start")
        self.assertIn(
            "SM64_SATURN_PCM_SFX_BUNDLE_MUSIC_SAMPLE_INDEX_FIELD",
            music_start,
            "music must be named by the bundle trailer's sample-index field",
        )
        self.assertRegex(
            music_start,
            r"sm64_saturn_pcm_start_voice\(\s*state,\s*"
            r"SM64_SATURN_PCM_MUSIC_SLOT\b",
            "the trailer row must start on the pinned music slot",
        )
        self.assertRegex(
            music_start,
            r"if\s*\(\s*music_index\s*==\s*0U?\s*\)",
            "a music-less bundle must be silent, not a fault",
        )

    def test_effect_dsp_is_programmed_before_any_sound_ram_staging(self) -> None:
        """The SCSP effect DSP must be neutralised before we stage samples.

        Measured 2026-08-15: SCSP common register 0x402 read back 0x0118 --
        RBP = 24, RBL = 2 -- putting the effect DSP's reverb ring at
        [0x30000, 0x40000) in sound RAM, with a live BIOS microprogram left
        in MPRO.  The DSP rewrote that window continuously, over the staged
        bank: 65,354 bytes of the PCM bank and 21,320 bytes of the music
        sample at 0x3ACB8 (2.665 s of every 8.146 s loop) plus 8 SFX
        samples were being read out of the reverb ring.  The port had never
        written RBP, RBL, MPRO, COEF or MADRS, so it inherited whatever the
        BIOS left -- which varies by revision and region.  The boot must
        therefore program that state explicitly, with the sound CPU stopped
        and before any byte of driver, metadata or PCM reaches sound RAM.
        """
        source = SOUND_CPU_C.read_text(encoding="utf-8")

        quiesce = function_body(
            source, "sm64_saturn_sound_cpu_program_effect_dsp"
        )
        for offset in (
            "SM64_SATURN_SCSP_DSP_MPRO_OFFSET",
            "SM64_SATURN_SCSP_DSP_COEF_OFFSET",
            "SM64_SATURN_SCSP_DSP_MADRS_OFFSET",
            "SM64_SATURN_SCSP_DSP_RING_OFFSET",
        ):
            self.assertIn(
                offset, quiesce,
                f"{offset} must be programmed, not inherited from the BIOS",
            )
        self.assertRegex(
            quiesce, r"mpro\s*\[[^\]]+\]\s*=\s*0",
            "MPRO must be zeroed: an all-NOP microprogram asserts neither "
            "MWT nor MRD, so the DSP cannot touch sound RAM at all",
        )
        self.assertRegex(
            quiesce,
            r"\*\s*ring\s*=\s*\(uint16_t\)\s*SM64_SATURN_SCSP_DSP_RING_WORD",
            "0x402 must be written with the programmed RBP/RBL word",
        )
        self.assertRegex(
            source, r"SM64_SATURN_SCSP_DSP_RING_RBL\s*<<\s*7",
            "0x402 packs RBP in bits 0-6 and RBL in bits 7-8 (Ymir "
            "scsp.hpp WriteReg402)",
        )

        rbp = enum_value(source, "SM64_SATURN_SCSP_DSP_RING_RBP")
        rbl = enum_value(source, "SM64_SATURN_SCSP_DSP_RING_RBL")
        # Ymir scsp_dsp.hpp: ring base = RBP << 12 words, length =
        # 0x2000 << RBL words; both counted in 16-bit units.
        ring_base = rbp * 0x2000
        ring_bytes = (0x2000 << rbl) * 2
        self.assertNotEqual(
            ring_base, BIOS_LEFTOVER_RING_BASE,
            "the programmed ring must not land back on the BIOS leftover "
            "window that corrupted the bank",
        )
        self.assertGreaterEqual(
            ring_base, STAGED_BANK_END,
            "the ring must start above the staged PCM bank end",
        )
        self.assertLessEqual(
            ring_base + ring_bytes, SOUND_RAM_BYTES,
            "the ring must fit inside 512 KiB sound RAM",
        )

        sh_block = preprocessor_block_around(
            source, "sm64_saturn_sound_cpu_program_effect_dsp();"
        )
        self.assertIn("__sh__", sh_block.splitlines()[0])
        sh_arm = sh_block.split("\n#else", 1)[0]
        set_512k = function_body(sh_arm, "sm64_saturn_sound_cpu_yaul_set_512k")
        self.assertIn(
            "sm64_saturn_sound_cpu_program_effect_dsp()", set_512k,
            "the target SCSP configuration entry must program the effect "
            "DSP, so every cold-boot path inherits the fix",
        )

        boot = function_body(source, "sm64_saturn_sound_cpu_boot")
        require_after(
            boot, r"set_512k_mode\s*\(", r"clear_owned_regions\s*\(",
            "the DSP must be quiesced before sound RAM is cleared",
        )
        require_after(
            boot, r"set_512k_mode\s*\(", r"copy_staged_regions\s*\(",
            "the DSP must be quiesced before staged samples are copied",
        )

        live = function_body(
            AUDIO_LIVE_C.read_text(encoding="utf-8"),
            "sm64_saturn_source_audio_live_boot",
        )
        require_after(
            live,
            r"sm64_saturn_sound_cpu_yaul_set_512k\s*\(",
            r"memset\s*\(\s*\(void\s*\*\)\s*sound_ram",
            "the live cold boot must quiesce the DSP before clearing "
            "sound RAM",
        )
        require_after(
            live,
            r"sm64_saturn_sound_cpu_yaul_set_512k\s*\(",
            r"memcpy\s*\([^;]*SM64_SATURN_PCM_BANK_OFFSET[^;]*pcm",
            "the live cold boot must quiesce the DSP before the PCM bank "
            "copy, or the ring overwrites what we just staged",
        )


if __name__ == "__main__":
    unittest.main()
