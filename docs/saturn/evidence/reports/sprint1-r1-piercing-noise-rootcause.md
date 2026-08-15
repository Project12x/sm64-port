# R1 piercing-noise defect — root cause (2026-08-15)

Candidate `id-b3aceeb28570230b`. Owner report: periodic piercing noise during
music playback, period **not** the 8.15 s music loop; no recognizable SFX heard.

## Root cause

**Continuous SFX are re-keyed on every game-loop tick.**

SM64's audio design re-asserts *continuous* (non-discrete) sounds every frame;
the sound driver is expected to keep such a sound playing, not restart it.
Our target path restarts it:

1. `sm64_saturn_audio_policy_tick()` runs once per game-loop iteration from
   `audio_signal_game_loop_tick()`. Discrete sounds are skipped once published
   (`saturn_audio_policy.c:833`), but **continuous sounds are re-published
   every tick** via `publish_bank()` (`saturn_audio_policy.c:213`), emitting a
   fresh `PLAY_REFRESH`.
2. On the MC68000, `sm64_saturn_pcm_play_semantic()` (`pcm_voice.c`) contains
   **no already-playing check**. It matches the mapping and calls
   `sm64_saturn_pcm_start_voice()` unconditionally.
3. `sm64_saturn_scsp_pcm8_start()` (`scsp_pcm8.c:100-136`) writes
   `KEYS = KEY_EXECUTE` (key-off), reprograms SA/LSA/LEA/EG/PITCH/PAN, then
   writes `KEYS = KEY_EXECUTE|KEY_ON|PCM8|addr_high` — **a hard restart from
   sample offset 0.**

Net effect: any continuously-asserted sound restarts once per presented frame.
At the measured ~1.1 FPS that is a re-trigger every **~0.9 s** — a repeating
chirp/burst whose period is the *frame* period, not the music loop period.
This matches the owner's "periodic, but not every 8 seconds" exactly, and it
also explains "no recognizable SFX": a continuous sound never plays longer
than one frame's worth before being restarted, so it is perceived as noise
rather than as its intended sound.

## Why earlier evidence appeared to exonerate the SFX path

The 3,600-frame headless snapshot reported `voices_started = 2` (both music)
and `sfx_consumed = 0`, which looked like a clean acquittal. It was not: the
headless route supplies **no controller input**, so no `play_sound` ever fired.
The owner's session had live input. **A negative result from an input-less run
cannot clear an input-driven path** — recorded here as a methodology lesson.

## Supporting measurements (time-series probe, 2 runs, 119 s + 89 s)

With no input, the audio control path is not merely quiet, it is *frozen*:
`voices_started` pinned at 2, `commands_consumed` pinned at 4
(`SET_MASTER`, `SEQ_START`, `RESET`, `SEQ_START` — all at boot), all 32 SCSP
slots' registers byte-identical across every sample, only slot 0 keyed on.
The single measured period in the system is the music loop itself: 11 CA-field
wraps in 89 s = **8.09 s**, against 8.146 s computed from the descriptor.
Frame presentation confirmed live throughout (83 distinct frame hashes / 90
samples), so the freeze is real, not a hung emulator.

Reports: `sprint1-r1-audio-timeseries-run1.json`, `...-run2.json`;
probe: `tools/saturn/probe_audio_timeseries.py`.

## Also ruled out (evidence, not argument)

| Suspect | Disposition |
| --- | --- |
| M64 render / source WAV | Owner auditioned an anti-aliased 8 kHz render: clean |
| Downsample aliasing in `wav_to_pcm8.py` | Owner auditioned the **exact disc audio** as a WAV: clean |
| Loop seam click | Measured PCM discontinuity at the wrap = 0 |
| Loop-end off-by-one | `LEA = sample_count - 1`, correct |
| 64 KB page crossing | Real condition (sample spans 0x50000; `SA_LOW+LEA` overflows 16 bits by 0x2B49) but Ymir uses full 32-bit address arithmetic (`scsp.cpp:1232`), so it cannot corrupt playback there. **Still a real-hardware risk — keep as an open ticket.** |
| Slot register map / envelope | Offsets match the SCSP map; AR=31, RR=31, TL=0 correct for a sustained loop |
| Periodic control traffic | Measured: none — control ring frozen at 4 commands |

## Prescribed fix (one bounded change, next loop iteration)

In `sm64_saturn_pcm_play_semantic()`: before starting a voice, check whether a
voice is already active carrying the same `(source_token, sound_bits)` and the
same resolved sample. If so, **update only** attenuation / pan / pitch and
refresh the freshness bookkeeping — do **not** rewrite `KEYS` with `KEY_ON`.
Key on only for a genuinely new sound or a changed sample. This restores SM64's
own continuous-sound contract at the driver boundary, which is where it belongs.

Regression test (host, mutation-checked): publish a bundle, issue the same
`PLAY_REFRESH` twice, and assert the slot's `KEYS` register is written with
`KEY_ON` exactly once while volume/pan updates still take effect.

## Open follow-ups (not R1 blockers)

1. **Uninitialized SCSP slot registers.** The driver never writes `0x0E` (MDL),
   `0x12` (LFO), `0x14` (ISEL/IMXL), or `0x18` (EFSDL/EFPAN); they retain BIOS
   values. Measured slot 0 carries `0x14 = 0x0007` (IMXL=7, full send into the
   uninitialized SCSP DSP input mixer). Currently inaudible only because every
   slot's `EFSDL` is 0. Initialize these explicitly.
2. **64 KB page-crossing music sample** — harmless in Ymir, unverified on real
   hardware. Align the music row to a 64 KB boundary in sound RAM (218 KB spare)
   before any hardware test.
3. **Second `play_music`.** `main.c:2035`'s bootstrap call bypasses
   `sound_init.c`'s `sCurrentMusic` bookkeeping, so the game's own
   `init_mario_after_warp` → `set_background_music` path fires a second
   `SEQ_START` (with a `RESET` before it). Delete the bootstrap call and let the
   level script drive music. Independent of the noise defect.
