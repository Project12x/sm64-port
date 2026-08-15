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

## Fix build — candidate `id-86d3880727ed1d10` (2026-08-15)

- Date: 2026-08-15 (06:37–07:05 local); successful build 06:48:42–07:03:56
  (15m14s). Worktree `.worktrees/saturn-recovery`, branch `saturn/recovery`.
- **No emulator launch. Live owner verification is still PENDING** — no claim
  is made here about boot, visuals, audible SFX, music, input, or FPS.

### Commits under test

| SHA | Subject |
| --- | --- |
| `207f5972` | `fix(audio)`: update already-playing SFX instead of re-keying them every frame |
| `7da1ebca` | `fix(audio)`: remove the duplicate bootstrap `play_music`; the level script owns music |

The first implements this document's prescribed fix at the driver boundary:
`sm64_saturn_pcm_play_sample()` scans SFX slots 1..3 for a voice already
carrying the resolved sample and, on a hit, calls the new
`sm64_saturn_scsp_pcm8_update()` (attenuation 0x0C and pan/send 0x16 only)
without writing `KEYS`, advancing the rotor, or incrementing `voices_started`.
Both level words are now composed by one shared `put_level_words()` helper
that the key-on path uses too, so the refresh and key-on paths cannot drift.
Coalesced refreshes are counted and published to the new
`SM64_SATURN_PCM_SFX_REFRESHES_OFFSET` diagnostic word at `0x7F20`, appended
after the music diagnostics — no existing mailbox, ring, or diagnostic offset
moved. The second closes open follow-up 3 above.

Host tests: `verify-pcm68k-model` 18 tests pass (three new refresh
regressions, mutation-verified: disabling the already-playing check fails
`test_repeated_play_refresh_updates_without_rekey`; dropping either the
pan/send or the attenuation write fails
`test_repeated_play_refresh_still_applies_volume_and_pan`).
`tools/saturn/test_full_game_audio_source.py` 10 tests pass.
`verify-scsp-pcm8` and `verify-pcm68k-heartbeat-host` also pass.

### Build attempts

| Attempt | Result |
| --- | --- |
| 1 (`id-ea4f28132a36100c`) | FAILED at `source-actor-family-bundle`: "publication is stale for current inputs" — the expected consequence of the audio package class byte count changing (the MC68000 driver image grew). No ELF; superseded. |
| 2 | FAILED "publication is incomplete": deleting `actors-v3-g15/` is only half the sanctioned repair — the sourceboot rule runs `--verify-publication` and never publishes. |
| 3 (`id-86d3880727ed1d10`) | **SUCCEEDED** after republishing generation 15 explicitly (`compile_actor_family_bundle.py` without `--verify-publication`, then re-verified: PASS). |

Sanctioned repair, recorded in full so it is not re-derived: delete
`build/saturn/packages/bob/1/actors-v3-g15/`, run
`compile_actor_family_bundle.py --root . --closure
build/saturn/packages/bob/1/closure.json --family-report
build/saturn/packages/bob/1/actors/actor-families.json --model-ids
include/model_ids.h --package-generation 15 --output-dir
build/saturn/packages/bob/1/actors-v3-g15` (publish mode), then re-run the
same command with `--verify-publication`. `scene-v3-g15` was left untouched.

Invocation: the same 27-variable set as stage 1b/stage 2 (pool 208, R1 tuple,
`SATURN_FEATURE_SEMANTIC_AUDIO=1`) via `with-msys-toolchain.ps1` -> MSYS
`sh --noprofile --norc -l`, sourcing `../../.yaul.env` then
`unset COMPILER_PATH`. Only the usual benign warnings.

### Identity and hashes

Sealed identity **`id-86d3880727ed1d10`**, config label
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg86d3880727ed`.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `b8754557bbf36ee853c08ac229800c43d513964761fec0ef33abe2e7f3e8b501` |
| `sm64-saturn-sourceboot-e2.iso` | `d952aea402d5bb5710e0dbff88083f7cfd8eeffdd05a630b1c9266e38954521a` |
| `sm64-saturn-sourceboot-e2.cue` (88 B; not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `obj/sm64-saturn-sourceboot-e2.map` | `a3f38545449d703214c0f4a6cafa207a20ce2d0b8c2833836a8075d51b268c0d` |
| `bob_sfx_metadata.bin` (1,232 B) | `b13869267e2b7af56cbfed8bb8943405ef501137dcb1378e92d1333fa84279a9` |
| `bob_sfx_pcm.bin` (273,225 B) | `b76e6cbc913aa3aed0600848def97abf7d806d83f5c4096840ed03a049601924` |

Both SFX-bundle blobs are byte-identical to R1 `id-b3aceeb28570230b`: the
audio *content* is unchanged, only the MC68000 driver code that plays it.
ISO size unchanged at 5,181,440 B. Artifacts preserved to
`releases/2026-08-15_0705/id-86d3880727ed1d10/`; the superseded R1 candidate
was preserved to `releases/2026-08-15_0637/id-b3aceeb28570230b/` before this
build, hashes re-verified against the stage-2 record.

### Margin gate output (verbatim; auto-selected THIS build's ELF)

```
verify-memory-map: checking /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery/build/saturn/sourceboot/e2-bob-identity-id-86d3880727ed1d10/obj/sm64-saturn-sourceboot-e2.elf
verify: D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery\build\saturn\sourceboot\e2-bob-identity-id-86d3880727ed1d10\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FDF08
  hwram_remaining = 0x20F8 bytes (required >= 0x1F00)
  lwram_end       = 0x002F5D40
  lwram_remaining = 0xA2C0 bytes (floor >= 0x4000)
  RESULT          = OK
```

Delta vs R1 `id-b3aceeb28570230b`: `hwram_remaining` 0x20D8 -> **0x20F8**,
i.e. **+0x20 (32 bytes) of HWRAM headroom**; `___end` moved down 0x20 to
`0x060FDF08`. Removing the bootstrap `play_music` call and its `seq_ids.h`
include is the only SH-2-side change, and it shrank `.text` accordingly. The
SFX driver fix is MC68000 code and does not consume SH-2 RAM.
`lwram_remaining` is unchanged at 0xA2C0 (41,664 B) against the 0x4000 floor.
Slack over the 0x1F00 gate is now 0x1F8 (504 B) — still thin, but 32 B better
than the R1 candidate.

### Status

Both fixes are built into a launchable CUE with the margin gate passing.
**The owner observation has not happened.** The next step is the owner's Ymir
session on `id-86d3880727ed1d10`; keep/revert follows that live result, per
the constitution.

### New follow-up found while fixing (not an R1 blocker)

4. **The driver treats `STOP_HANDLE`/`STOP_SOURCE`/`STOP_BANK` as accepted
   no-ops** (`pcm_voice.c`), so nothing but a `RESET` or the SFX rotor ever
   clears `voices[slot].active`. With the coalescing fix in place this has a
   new consequence worth knowing before listening: if the policy layer keeps a
   *discrete* sound published and the game re-requests it with the same source
   token before the rotor has reclaimed that slot, the driver refreshes the
   voice's level rather than re-keying it, so that particular repeat may not be
   heard. In practice the three SFX slots churn quickly, and this is strictly
   better than the pre-fix behaviour where every sound was restarted every
   frame. Implementing the stop opcodes (keying off the matching voice) is the
   real repair and is a separate bounded change.
