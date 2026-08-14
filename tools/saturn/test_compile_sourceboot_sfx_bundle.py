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
    BUNDLE_SAMPLE_BYTES,
    BUNDLE_VERSION,
    build_bob_sfx_bundle,
)


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
        self.assertEqual(bundle.sample_count, 64)
        self.assertEqual(bundle.music_sequence_bytes, 5122)
        self.assertEqual(bundle.music_sample_id, "instruments/19_brass")
        self.assertEqual(bundle.music_sample_index, 63)
        self.assertGreater(bundle.music_sequence_offset, 0x8000)
        self.assertEqual(
            int.from_bytes(bundle.metadata[24:28], "big"),
            bundle.music_sequence_offset,
        )
        self.assertEqual(
            int.from_bytes(bundle.metadata[28:30], "big"),
            bundle.music_sequence_bytes,
        )
        self.assertEqual(
            int.from_bytes(bundle.metadata[30:32], "big"),
            bundle.music_sample_index,
        )
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


if __name__ == "__main__":
    unittest.main()
