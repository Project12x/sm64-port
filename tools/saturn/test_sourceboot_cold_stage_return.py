"""Guard the single cold-upload / VDP1-bank lifetime handoff in sourceboot."""

from __future__ import annotations

import argparse
import subprocess
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAIN = Path("src/port/saturn/sourceboot/main.c")
LINKER = Path("src/port/saturn/sourceboot/sourceboot-cart.x")


def source_for_revision(revision: str | None) -> str:
    if revision is None:
        return (ROOT / MAIN).read_text(encoding="utf-8")
    return subprocess.check_output(
        [
            "git",
            "-c",
            f"safe.directory={ROOT.as_posix()}",
            "show",
            f"{revision}:{MAIN.as_posix()}",
        ],
        cwd=ROOT,
        text=True,
    )


def require_after(text: str, earlier: str, later: str) -> None:
    earlier_at = text.index(earlier)
    try:
        later_at = text.index(later, earlier_at + len(earlier))
    except ValueError as error:
        raise AssertionError(f"missing required later step: {later!r}") from error
    if later_at <= earlier_at:
        raise AssertionError(f"{later!r} must follow {earlier!r}")


class SourcebootColdStageReturnTest(unittest.TestCase):
    revision: str | None = None

    def test_cold_stage_returns_bank_zero_before_gameplay(self) -> None:
        text = source_for_revision(self.revision)
        self.assertIn("return sourceboot_vdp1_cmdts[0];", text)
        require_after(
            text,
            "sm64_saturn_vdp1_frame_bank_set_init(",
            "sm64_saturn_source_scene_bundle_init(",
        )
        require_after(
            text,
            "sm64_saturn_source_scene_bundle_init(",
            "sm64_saturn_vdp1_backend_init_with_storage(\n"
            "                    &sourceboot_vdp1_backend, sourceboot_vdp1_cmdts[0]",
        )
        require_after(
            text,
            "&sourceboot_vdp1_backend, sourceboot_vdp1_cmdts[0]",
            "&spare_backend, sourceboot_vdp1_cmdts[1]",
        )
        require_after(
            text,
            "&spare_backend, sourceboot_vdp1_cmdts[1]",
            "sourceboot_game_loop(void)",
        )

    def test_semantic_audio_uses_explicit_main_pool_workspace_before_thread5(self) -> None:
        text = source_for_revision(self.revision)
        post_cart = text.split("sourceboot_post_cart_init(void)", 1)[1].split(
            "sourceboot_init_sky_bitmap();", 1
        )[0]
        game_loop = text.split("sourceboot_game_loop(void)", 1)[1]

        self.assertIn('#include "audio/external.h"', text)
        self.assertIn("sourceboot_reset_lwram_state();", post_cart)
        self.assertNotIn("sm64_saturn_source_audio_live_boot(", post_cart)
        require_after(game_loop, "main_pool_init(", "sourceboot_audio_init()")
        require_after(game_loop, "sourceboot_audio_init()", "thread5_game_loop(NULL)")
        require_after(game_loop, "thread5_game_loop(NULL)", "main_pool_available() < 0x8000U")
        audio_init = text.rsplit("sourceboot_audio_init(void)", 1)[1].split(
            "sourceboot_game_loop(void)", 1
        )[0]
        self.assertIn("sm64_saturn_source_audio_semantic_workspace_bytes()", audio_init)
        self.assertIn("sm64_saturn_source_audio_live_workspace_bytes()", audio_init)
        self.assertEqual(audio_init.count("MEMORY_POOL_RIGHT"), 2)
        self.assertIn("sm64_saturn_source_audio_semantic_workspace_bind(", audio_init)
        self.assertIn("sm64_saturn_source_audio_live_workspace_bind(", audio_init)
        # Fail-open contract (Task 7): the semantic workspace binds only as
        # the last step of a fully successful init -- after the sound-CPU
        # boot -- so any audio failure leaves it unbound (fail-closed no-ops)
        # instead of feeding a dead mailbox.  sound_init() then initializes
        # the freshly bound workspace.
        require_after(
            audio_init,
            "sm64_saturn_source_audio_live_boot(",
            "sm64_saturn_source_audio_semantic_workspace_bind(",
        )
        require_after(
            audio_init,
            "sm64_saturn_source_audio_semantic_workspace_bind(",
            "sound_init();",
        )

    def test_semantic_audio_uses_c_spelling_for_assembled_cart_symbols(self) -> None:
        text = source_for_revision(self.revision)
        for symbol in (
            "sm64_saturn_sourceboot_pcm68k_driver",
            "sm64_saturn_sourceboot_sfx_metadata",
            "sm64_saturn_sourceboot_sfx_pcm",
        ):
            self.assertIn(f"extern const uint8_t {symbol}[];", text)
            self.assertNotIn(f"extern const uint8_t _{symbol}[];", text)

    def test_semantic_audio_text_precedes_generic_hwram_text(self) -> None:
        script = (ROOT / LINKER).read_text(encoding="utf-8")
        self.assertLess(script.index(".cart_rodata :"), script.index("  .text :"))
        audio = script.split(".cart_rodata :", 1)[1].split("  .text :", 1)[0]
        for object_name in (
            "source_audio_semantics.o",
            "source_audio_live.o",
            "saturn_audio_policy.o",
            "saturn_audio_spatial.o",
            "saturn_pcm_transport.o",
            "saturn_sound_cpu.o",
        ):
            self.assertIn(object_name, audio)
        self.assertIn("___sourceboot_cart_rodata_start", audio)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--revision")
    args, remaining = parser.parse_known_args()
    SourcebootColdStageReturnTest.revision = args.revision
    unittest.main(argv=[__file__, *remaining])
