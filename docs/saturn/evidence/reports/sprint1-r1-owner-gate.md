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

## Leading hypothesis for the next session

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
