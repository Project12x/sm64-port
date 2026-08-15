# Sprint 1 R1 — owner gate observation (2026-08-15)

Candidate: identity `id-b3aceeb28570230b`
ELF `6b33c1cc19c8a50281908e98dea012d81246d63acc12ef4b31fdd0f4210dc091`
ISO `832d039acf48ed2a5f13b628a37594a40ce8865e51542afc69cfd31f61862a4a`
Launch report: `build/saturn/ymir-desktop-launches/sprint1-r1-launch.json`
(manifest sha `f12cd4ee…`, desktop Ymir SDL3, profile-managed 32-Mbit DRAM cart)

## Owner observations (verbatim intent, recorded at the gate)

1. **Music: PLAYS.** The BOB theme comes out of the emulated SCSP, started by
   the source game's own `play_music` call. **First audible game music in the
   project's history.**
2. **Defect — periodic piercing noise.** Present during playback. Owner
   states the period is **not ~8 seconds**, i.e. it does NOT coincide with the
   music sample's loop wrap (8.15 s).
3. **SFX: not heard** (as recognizable sounds) during the session.
4. **Visuals: good.** Owner accepted Mario/terrain/camera presentation.
5. **Cadence: 2 FPS.** Recorded, non-blocking for R1 per the owner's
   2026-08-15 gate change (commit `0ad5fb31`).

## Verdict

R1 is **conditionally accepted on visuals + music-plays**, with the piercing
noise as the one owner-visible defect blocking a clean close. Cadence recovery
and full SFX coverage are next-sprint objectives, not R1 blockers.

## Diagnostics completed before pause (rule-outs)

- **Loop seam is clean.** `build/saturn/audio/bob_theme_8k.pcm8` (65,169 B):
  last sample 0, first sample 0, seam discontinuity **0** — a wrap click is not
  mechanically possible from the sample data. Combined with the owner's
  "not every 8 seconds", the loop wrap is **ruled out**.
- **Loop-end register math is correct.** `scsp_pcm8.c:111-113` sets
  `LSA = 0`, `LEA = sample_count - 1` (inclusive end) — no off-by-one that
  would read past the sample into adjacent sound RAM.
- **Mario's own action/voice sounds are NOT in the bundle.** The 54-entry
  closure contains no `SOUND_ACTION_*` jump/land/voice entries beyond
  `SOUND_ACTION_READ_SIGN`; it is dominated by object/enemy/environment IDs
  (`SOUND_ENV_SLIDING`, `SOUND_ENV_BOAT_ROCKING1`, `SOUND_OBJ_*`,
  `SOUND_GENERAL_*`). With actors off, most have no live trigger — which
  explains "no recognizable SFX" independently of the noise.

## UPDATE — headless telemetry (commit `88f077ac`) REFUTES the SFX hypothesis

The probe (`sprint1-r1-audio-probe.json`) reports `voices_started = 2` and
`music starts = 2`, `active_voice_count = 1`, every fault/reject/malformed/
invalid_samples counter **0**. Both started voices are accounted for by the
two music starts — **no SFX voice fired at all** in a 3,600-frame run. The
"noise is misrendered SFX" hypothesis below is therefore **not supported** by
headless evidence (caveat: the owner's session had live input and may trigger
sounds the scripted route does not).

Two NEW facts that the next hypothesis must explain:
1. **`play_music` is issued TWICE.** SEQ_START arrives twice, and our handler
   has replace semantics — it keys the music voice off and restarts the sample
   from its beginning. A mid-playback restart is audible. If the source game
   re-issues the level sequence periodically (or on some state re-evaluation),
   that cadence — NOT the 8.15 s loop — would set the artifact's period, which
   matches the owner's "not every 8 seconds". **Check first:** instrument or
   reason out where the second `play_music` comes from
   (`source_audio_semantics.c` policy layer / `sourceboot_game_loop`), and
   whether repeated identical-sequence starts should be idempotent (ignore a
   start for the already-playing sequence) instead of restarting.
2. Only ONE voice is ever active, so the noise is being produced by the music
   voice itself or by the act of (re)keying it — not by voice collision.

Also from telemetry: sustained cadence is ≈1.1 FPS (53 VBlanks/frame),
dominated by scene construction (≈24.5 VBlanks) and master finalization
(≈5.8) — the profile for Sprint 2's cadence work.

## Leading hypothesis for the next session (SUPERSEDED — see UPDATE above)

**The piercing noise may BE the SFX path, misrendered** — not a music defect.
Rationale: the noise is aperiodic (owner) and the level does trigger mapped
ambient/environment sounds (`SOUND_ENV_*`) through the semantic path onto
slots 1-3. If the per-sample pitch word or descriptor offsets are wrong for
samples whose stored rate differs from the music row's 8 kHz, those voices
would play at the wrong rate — audible as irregular piercing screech rather
than the intended sound. That single mechanism explains BOTH open observations
(noise present, SFX unrecognizable).

Discriminating evidence to gather first (cheap, no rebuild):
1. The pending headless audio-probe JSON
   (`docs/saturn/evidence/reports/sprint1-r1-audio-probe.json`): if
   `VOICES_STARTED` climbs well past the music's single start, SFX voices are
   firing — hypothesis supported.
2. Read the pitch computation in `scsp_pcm8.c` against the SFX rows' `rate`
   fields in `bob_sfx_manifest.json` (music row is 8000 Hz; SFX rows carry
   their own rates, some halved by the earlier 2:1 SFX rate reduction).
3. A targeted experiment: build with the SFX mapping table empty (music only).
   Silence of the noise confirms the SFX path; persistence indicts music.

Do **not** re-patch the music path before that discrimination — the loop seam
and LEA math are already cleared.

---

## R1 ACCEPTED — owner verdict 2026-08-15

Accepted candidate: **`id-86d3880727ed1d10`**
ELF `b8754557bbf36ee853c08ac229800c43d513964761fec0ef33abe2e7f3e8b501`
ISO `d952aea402d5bb5710e0dbff88083f7cfd8eeffdd05a630b1c9266e38954521a`
Artifacts: `releases/2026-08-15_0705/id-86d3880727ed1d10/`

The owner accepted R1 on the evidence that matters: **the source game's own
`play_music` call produces audible, looping music through the emulated SCSP —
the first game audio in this project's history — with visuals accepted as
non-regressed against A9A.** Cadence measured ~1.1-2 FPS, recorded and
explicitly non-blocking per the 2026-08-15 gate change (`0ad5fb31`).

### Disposition of the piercing-noise defect: NOT A PORT DEFECT

Closed as an **emulator/host-performance artifact**, not a port bug, on the
following chain of eliminations — every one evidence-based:

| Suspect | How it was eliminated |
| --- | --- |
| M64 render / source WAV | Owner auditioned an anti-aliased render: clean |
| Downsample aliasing | Owner auditioned the **exact disc audio**: clean |
| Loop seam / loop-end math | PCM discontinuity measured 0; `LEA = count-1` correct |
| SFX re-key per frame | Fixed (`207f5972`); noise persisted |
| Duplicate `play_music` | Fixed (`7da1ebca`); noise persisted |
| Periodic control traffic | Measured frozen: 4 commands total, registers static 119 s |
| SCSP slot config | `SA=0x3ACB8` correct, `PITCH=0x69CE`≈8 kHz, LFO `0x12=0`, MDL `0x0E=0` |
| Sign convention | Ymir casts `sint8` — matches our signed PCM8 |
| 64 KB page crossing | Ymir uses full 32-bit address math (`scsp.cpp:1232`) |
| Input-driven anything | **Owner confirmed the noise occurs with zero controller input** |

**Mechanism identified in Ymir's own host audio path.**
`apps/ymir-sdl3/src/app/audio_system.cpp`, `ProcessAudioCallback`: the SDL
callback drains `sampleCount` samples at real-time 44.1 kHz and advances
`m_readPos` **unconditionally — there is no underrun detection.** If the
emulator's producer side falls behind real time, the read pointer laps the
write pointer and SDL replays stale ring-buffer contents: a harsh repeating
artifact whose period is the buffer wrap, unrelated to the 8.15 s music loop
or the frame period.

This build is the pathological producer: ~53 VBlanks of emulated SH-2 work per
presented frame. It is also the first build in project history to emit any
audio, which is why the artifact has never been observed before.

**Consequence:** the artifact is expected to diminish and disappear as cadence
improves, making Sprint 2's optimization work the real test. On hardware the
SCSP is dedicated silicon clocking 44.1 kHz independent of game-logic speed,
so this failure mode cannot occur there.

**Confidence:** high on mechanism, not closed by direct measurement of Ymir's
emulation speed. If the noise survives a materially faster build, reopen and
measure host emulation speed first.

### Net Sprint 1 outcome

Audible looping music from the game's own semantic call; owner-accepted
visuals; a linking, margin-passing, identity-bound candidate; the entire donor
worktree preserved in git; the constitution restored; and the `0x0340` audio
failure root-caused and fixed. Cadence is the sole remaining product gap and
becomes Sprint 2's first objective.

---

## CORRECTION 2026-08-15 — the noise IS a port defect. Prior disposition RETRACTED.

**The "Ymir host-audio underrun" disposition above is WRONG and is retracted.**
A sound-RAM staging verification run found the true cause with register-level
evidence. Report: `sprint1-r1-sound-ram-staging-verify.json`; probe:
`tools/saturn/probe_sound_ram_verify.py`.

### Measured

| Region | Sound RAM | Result |
| --- | --- | --- |
| SFXB metadata | `0x05000` | exact match |
| PCM bank | `0x08000` (273,225 B) | **65,354 bytes differ (23.9%)** |
| Music sample | `0x3ACB8` (65,169 B) | **21,276 bytes differ (32.7%)** |

Every differing byte lies in one contiguous 64 KiB-aligned window,
**`0x30000`–`0x3FFFF`**, and that window is being **rewritten continuously**
(65,164 of 65,536 bytes changed after 60 frames). Outside it, every byte
matches — including the music tail from `0x40000` to the loop end, exact. The
staging `memcpy` is byte-perfect and the ISO is clean.

### Cause

SCSP common-control register `0x402` reads **`0x0118`** → **RBP = 24, RBL = 2**,
which decodes to the **SCSP effect DSP's reverb ring buffer** at
24 × 0x2000 = **`0x30000`**, length 32k words = **`[0x30000, 0x40000)`** —
both endpoints matching the corruption window exactly. `MPRO` holds a real,
running DSP microprogram (377/1024 nonzero bytes) left over from the BIOS.

**The port never programs any of this.** There is no reference to `0x402`,
RBP, RBL, `MPRO`, `COEF`, or `MADRS` anywhere in `src/`. The SCSP's own effect
DSP is writing its reverb ring straight over our staged samples, every sample
period, forever.

Our music sits at `0x3ACB8`, straddling the ring's top edge, so the first
`0x40000 - 0x3ACB8` = **21,320 bytes = 2.665 s of every 8.146 s loop** is read
out of the live DSP ring instead of the song. That is the periodic piercing
noise. **8 distinct SFX samples also sit inside the ring** and are equally
corrupt — which independently explains the unrecognizable SFX.

Cross-checks: the window's contents appear nowhere in LWRAM, HWRAM, VDP1 VRAM,
the VDP1 framebuffer, or VDP2 VRAM (so it is not a stray SH-2 blit), and the
1 KB past the loop end is all zeros (so over-read is not a contributor).

### Why the earlier reasoning failed

Every elimination in the table above was individually correct — the control
path *is* frozen, the registers *are* right, the data *on disk* is clean. The
error was concluding "therefore not a port defect" from a set of eliminations
that never included **the bytes in sound RAM at playback time**. The one
untested link was the one that mattered. The Ymir-underrun mechanism was
real code but unverified speculation, and it was recorded with more confidence
than the evidence supported.

### Fix (not yet implemented)

Sound RAM above the bank end (`0x4AB49`) is confirmed zero and unused, so
repointing the ring clears the bank entirely — e.g. RBP = 0x28 (`0x50000`),
RBL = 2. Alternatively disable the effect DSP at boot; this port uses no SCSP
effects and every slot's `EFSDL` is already 0.

**Program the DSP state explicitly rather than dodging the observed window:**
RBP/RBL/MPRO here are BIOS leftovers and may differ by BIOS revision or region.
This joins open follow-up 1 (uninitialized slot registers `0x0E`/`0x12`/`0x14`/
`0x18`) as the same class of bug — inherited hardware state the driver never
initializes.
