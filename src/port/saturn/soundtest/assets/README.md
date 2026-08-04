# Generated PCM proof assets

`tools/saturn/gen_pcm_proof_bank.py` creates three deterministic signed mono
PCM8 waveforms at 11,025 Hz: a 220 Hz square tone, an 880 Hz square tone, and a
decaying 16-bit-LFSR noise burst. The formulas use integer arithmetic only.

The generated waveform bytes are dedicated to the public domain under
CC0-1.0. They contain no Nintendo, Sega, PoneSound, or other extracted audio.
Generated BIN/header/JSON files live under `build/saturn/soundtest/generated`
and are not committed. The complete first bank is capped at 32 KiB and every
sample begins at a two-byte-aligned offset.
