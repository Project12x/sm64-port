"""Contract tests for the normal BOB semantic-SFX sound-RAM bundle."""

from __future__ import annotations

import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "saturn"))

from compile_sourceboot_sfx_bundle import (  # noqa: E402
    BUNDLE_HEADER_BYTES,
    BUNDLE_MAGIC,
    BUNDLE_MAPPING_BYTES,
    BUNDLE_SAMPLE_BYTES,
    BUNDLE_VERSION,
    MUSIC_SAMPLE_ID,
    SAMPLE_FLAG_LOOP,
    BundleMapping,
    BundleSample,
    SourcebootSfxBundleError,
    _finalize_bundle,
    build_bob_sfx_bundle,
)


def _sfx_fixture() -> tuple[list[BundleMapping], list[BundleSample], bytearray]:
    """One synthetic SFX row so trailer tests need no ROM-derived assets."""
    rows = [BundleSample("sfx_1/fake", 0x8000, 32, 16000)]
    mappings = [BundleMapping("SOUND_TEST", 0x24008080, 0, 1)]
    return mappings, rows, bytearray(b"\x11" * 32)


class MusicPcmTrailerTest(unittest.TestCase):
    """The looped-sample music contract (charter D3, Sprint 1 Task 5)."""

    def test_music_pcm_is_final_looped_row_with_zero_m64_trailer(self) -> None:
        mappings, rows, pcm = _sfx_fixture()
        music = bytes((i * 7) & 0xFF for i in range(100))
        bundle = _finalize_bundle(7, mappings, rows, pcm, music, 8000)

        self.assertEqual(bundle.sample_count, 2)
        self.assertEqual(bundle.music_sample_index, 1)
        self.assertEqual(bundle.music_sample_id, MUSIC_SAMPLE_ID)
        music_row = bundle.samples[-1]
        self.assertEqual(music_row.flags, SAMPLE_FLAG_LOOP)
        self.assertEqual(music_row.rate, 8000)
        self.assertEqual(music_row.sample_count, len(music))
        self.assertEqual(music_row.offset % 2, 0)
        self.assertGreaterEqual(music_row.offset, 0x8000)
        # No m64 payload is appended and the trailer no longer points at one.
        self.assertEqual(bundle.music_sequence_offset, 0)
        self.assertEqual(bundle.music_sequence_bytes, 0)
        self.assertTrue(bundle.pcm.endswith(music))
        self.assertEqual(int.from_bytes(bundle.metadata[24:28], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[28:30], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[30:32], "big"), 1)
        # Wire bytes of the music row itself: offset u32, count u16, rate u16,
        # volume u16, flags u16 carrying exactly the loop bit.
        row = (BUNDLE_HEADER_BYTES + len(bundle.mappings) * BUNDLE_MAPPING_BYTES
               + bundle.music_sample_index * BUNDLE_SAMPLE_BYTES)
        self.assertEqual(int.from_bytes(bundle.metadata[row:row + 4], "big"),
                         music_row.offset)
        self.assertEqual(int.from_bytes(bundle.metadata[row + 4:row + 6], "big"),
                         len(music))
        self.assertEqual(int.from_bytes(bundle.metadata[row + 6:row + 8], "big"),
                         8000)
        self.assertEqual(int.from_bytes(bundle.metadata[row + 10:row + 12], "big"),
                         SAMPLE_FLAG_LOOP)

    def test_without_music_the_trailer_is_all_zero_and_no_row_is_added(self) -> None:
        mappings, rows, pcm = _sfx_fixture()
        bundle = _finalize_bundle(7, mappings, rows, pcm, None, 8000)

        self.assertEqual(bundle.sample_count, 1)
        self.assertEqual(bundle.music_sample_index, 0)
        self.assertEqual(bundle.music_sample_id, "")
        self.assertEqual(bundle.music_sequence_offset, 0)
        self.assertEqual(bundle.music_sequence_bytes, 0)
        self.assertEqual(int.from_bytes(bundle.metadata[24:28], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[28:30], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[30:32], "big"), 0)
        self.assertTrue(all(sample.flags == 0 for sample in bundle.samples))

    def test_oversized_music_fails_closed_with_trim_guidance(self) -> None:
        mappings, rows, pcm = _sfx_fixture()
        with self.assertRaises(SourcebootSfxBundleError) as caught:
            _finalize_bundle(7, mappings, rows, pcm, b"\x01" * 0x10000, 8000)
        self.assertIn("--max-seconds", str(caught.exception))
        self.assertIn("--music-rate", str(caught.exception))


@unittest.skipUnless(
    (ROOT / "build/saturn/audio/generated/bob_audio_closure.json").is_file(),
    "requires ROM-derived assets and the generated BOB audio closure")
class SourcebootSfxBundleTest(unittest.TestCase):
    def test_real_bob_bundle_is_bounded_and_resolves_every_closure_sfx(self) -> None:
        bundle = build_bob_sfx_bundle(
            ROOT,
            ROOT / "build/saturn/audio/generated/bob_audio_closure.json",
            ROOT / "build/saturn/audio/generated/sequences.bin",
        )

        self.assertEqual(bundle.metadata[:4], BUNDLE_MAGIC.to_bytes(4, "big"))
        self.assertEqual(int.from_bytes(bundle.metadata[4:6], "big"), BUNDLE_VERSION)
        self.assertEqual(int.from_bytes(bundle.metadata[6:8], "big"), BUNDLE_HEADER_BYTES)
        self.assertEqual(bundle.mapping_count, 54)
        # 54 semantic rows resolve to 63 sample descriptors; eleven rows use
        # two layered PCM samples and two waveform-only rows remain explicit
        # zero-sample capability gaps rather than proof-tone substitutions.
        # The former 64th row (the sequence-VM's bank-22 music instrument)
        # left with the VM: music is now the optional --music-pcm looped row.
        self.assertEqual(bundle.sample_count, 63)
        self.assertEqual(bundle.music_sequence_offset, 0)
        self.assertEqual(bundle.music_sequence_bytes, 0)
        self.assertEqual(bundle.music_sample_index, 0)
        self.assertEqual(bundle.music_sample_id, "")
        self.assertEqual(int.from_bytes(bundle.metadata[24:28], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[28:30], "big"), 0)
        self.assertEqual(int.from_bytes(bundle.metadata[30:32], "big"), 0)
        self.assertLessEqual(len(bundle.metadata), 0x3000)
        self.assertLessEqual(len(bundle.pcm), 0x78000)
        self.assertEqual(bundle.metadata_bytes,
                         BUNDLE_HEADER_BYTES + bundle.mapping_count * 8 +
                         bundle.sample_count * BUNDLE_SAMPLE_BYTES)
        self.assertEqual(bundle.mapping_for("SOUND_OBJ_BOBOMB_WALK").sample_count, 1)
        self.assertEqual(bundle.mapping_for("SOUND_AIR_BOBOMB_LIT_FUSE").sample_count, 2)
        self.assertEqual(bundle.mapping_for("SOUND_GENERAL_RED_COIN").sample_count, 0)
        self.assertTrue(all(sample.rate <= 44100 for sample in bundle.samples))
        self.assertTrue(all(sample.offset >= 0x8000 for sample in bundle.samples))
        self.assertTrue(all(sample.offset + sample.sample_count <= 0x80000
                            for sample in bundle.samples))
        self.assertTrue(all(sample.flags == 0 for sample in bundle.samples))


if __name__ == "__main__":
    unittest.main()
