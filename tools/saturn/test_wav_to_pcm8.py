"""Host tests for the WAV -> raw signed PCM8 music converter."""

from __future__ import annotations

import math
import struct
import sys
import tempfile
import unittest
import wave
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "saturn"))

from wav_to_pcm8 import (  # noqa: E402
    MAX_MUSIC_BYTES,
    effective_limit_samples,
    load_wav_mono_float,
    resample_linear,
)


def _quantize(samples: list[float]) -> bytes:
    """The tool's output quantizer, kept byte-identical to wav_to_pcm8.main."""
    return bytes((max(-128, min(127, round(s * 127.0))) & 0xFF) for s in samples)


def _write_sine_wav_16bit_stereo(path: Path, rate: int, seconds: float,
                                 frequency: float, amplitude: float) -> int:
    frames = int(rate * seconds)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(rate)
        payload = bytearray()
        for i in range(frames):
            value = int(amplitude * 32767.0 *
                        math.sin(2.0 * math.pi * frequency * i / rate))
            payload += struct.pack("<hh", value, value)
        w.writeframes(bytes(payload))
    return frames


class WavToPcm8Test(unittest.TestCase):
    def test_stereo_16bit_sine_resamples_to_expected_count_and_range(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "sine.wav"
            frames = _write_sine_wav_16bit_stereo(
                path, rate=44100, seconds=0.5, frequency=100.0, amplitude=0.8)
            mono, src_rate = load_wav_mono_float(str(path))
        self.assertEqual(src_rate, 44100)
        self.assertEqual(len(mono), frames)
        out = resample_linear(mono, src_rate, 8000)
        self.assertEqual(len(out), int(frames * 8000 / 44100))
        data = _quantize(out)
        signed = [b - 256 if b >= 128 else b for b in data]
        self.assertTrue(all(-128 <= v <= 127 for v in signed))
        # A 0.8 full-scale sine must survive conversion as a bipolar signal
        # near +/-(0.8 * 127) rather than collapsing to silence or clipping.
        self.assertGreater(max(signed), 96)
        self.assertLess(min(signed), -96)

    def test_default_limit_never_exceeds_the_music_byte_cap(self) -> None:
        # The packager's music row is one SCSP sample: u16 count / 16-bit
        # loop end cap it at 65,535 bytes.  The tool's default trim must
        # respect that at every rate instead of the old flat 28 s.
        self.assertEqual(effective_limit_samples(8000, None), MAX_MUSIC_BYTES)
        self.assertEqual(effective_limit_samples(11025, None), MAX_MUSIC_BYTES)
        # At a rate low enough for 28 s to fit, the 28 s default still wins.
        self.assertEqual(effective_limit_samples(2000, None), 2000 * 28)
        self.assertLessEqual(effective_limit_samples(44100, None),
                             MAX_MUSIC_BYTES)

    def test_explicit_max_seconds_above_cap_warns_and_clamps(self) -> None:
        import contextlib
        import io
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            limit = effective_limit_samples(8000, 28.0)
        self.assertEqual(limit, MAX_MUSIC_BYTES)
        self.assertIn("music cap", stderr.getvalue())
        # An explicit request under the cap is honored exactly, silently.
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(effective_limit_samples(8000, 2.0), 16000)
        self.assertEqual(stderr.getvalue(), "")

    def test_trim_limit_bounds_output_like_max_seconds(self) -> None:
        mono = [0.25] * 4000
        out = resample_linear(mono, 8000, 8000)
        limit = int(8000 * 0.25)  # the tool's --max-seconds 0.25 at 8 kHz
        self.assertGreater(len(out), limit)
        trimmed = out[:limit]
        self.assertEqual(len(_quantize(trimmed)), limit)

    def test_8bit_mono_wav_round_trips_within_one_step(self) -> None:
        original = bytes(range(0, 256, 8))  # unsigned 8-bit WAV samples
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "mono8.wav"
            with wave.open(str(path), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(1)
                w.setframerate(8000)
                w.writeframes(original)
            mono, src_rate = load_wav_mono_float(str(path))
        self.assertEqual(src_rate, 8000)
        data = _quantize(resample_linear(mono, src_rate, 8000))
        self.assertEqual(len(data), len(original))
        for source, packed in zip(original, data):
            expected = source - 128
            actual = packed - 256 if packed >= 128 else packed
            # Quantizing at 127/128 of full scale loses at most one step.
            self.assertLessEqual(abs(actual - expected), 1)


if __name__ == "__main__":
    unittest.main()
