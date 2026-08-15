# Sprint 1 / Task 11 Stage A: R1 candidate headless telemetry

- Date: 2026-08-15 (00:36–01:05 local)
- Worktree: `.worktrees/saturn-recovery`, branch `saturn/recovery`, HEAD `0ad5fb31`
- Scope: headless telemetry gates on the R1 candidate. No desktop launch —
  the owner-observed gate is a separate step and is NOT claimed here.
- Gate policy (owner instruction 2026-08-15): music voice active, zero
  protocol faults, no SH-2 exceptions, and gameplay visually rendering are
  HARD gates. FPS is measured and recorded but does NOT block the candidate.

## Candidate identity (re-verified by fresh SHA-256 before any run)

Build `build/saturn/sourceboot/e2-bob-identity-id-b3aceeb28570230b/`,
sealed identity `id-b3aceeb28570230b`, manifest
`saturn-release-manifest-v1.json` (SHA-256
`f12cd4eea4a9c85c11a7ea4a894779fa24bc22cf1baaf676c6f1b6c7419c1a01`).

| Artifact | SHA-256 (measured this session) | Matches manifest |
| --- | --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` (9,982,188 B) | `6b33c1cc19c8a50281908e98dea012d81246d63acc12ef4b31fdd0f4210dc091` | yes |
| `sm64-saturn-sourceboot-e2.iso` (5,181,440 B) | `832d039acf48ed2a5f13b628a37594a40ce8865e51542afc69cfd31f61862a4a` | yes |
| `sm64-saturn-sourceboot-e2.cue` (88 B; not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | yes |

Config label (read back from target RAM during run 1):
`feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgb3aceeb28570`.

## Emulator-binary correction (infrastructure, disclosed)

The task sheet named
`ymir-agent/build-agent/apps/ymir-headless/Release/ymir-headless.exe`
(SHA-256 `81ea3a6e…f7fce`, built Jul 18). That binary predates ymir-agent
commit `bf3e4a4a` (Jul 21 23:18, "feat: add 32Mbit DRAM cartridge support
to ymir-headless") and silently ignores `--dram-cart`. Against it the
candidate boots and then spins forever in its cart-detect loop (final PCs
`0x0607BD88/8A` in `.text`; Ymir stderr shows the unhandled 16-bit write to
`0x257EFFFE`, the DRAM-cart ID register). Two runs were made against the
wrong binary before diagnosis (the first also with the A9A-era
`--startup-vblanks 600`, which is structurally too small for this build —
see below); both failed at the target-identity stage, never reached any
gate measurement, and are void as candidate evidence.

All results below use the cart-capable binary the 2026-08-09 evidence also
used: `ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`
(3,255,296 B, SHA-256
`fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`),
BIOS `.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`.

## Startup-window correction (infrastructure, disclosed)

`capture_sourceboot_throughput.py` binds its identity probe to the first
executable-flagged PROGBITS section of the ELF. On A9A-era builds that was
`.text` at `0x06004000`; on this candidate it is `.cart_rodata` at
`0x22400000` (the build's known-benign RWX section quirk), which is only
populated after the CD-to-cart staging pass. The task sheet's
`--startup-vblanks 600` can therefore never succeed on this layout; the
runs below use the tool maximum `--startup-vblanks 4096`. Identity matched
at attempt 683 — 600 was indeed structurally insufficient, and the match
depth is otherwise consistent with the 2026-08-09 build's 562-frame WRAM
identity depth plus cart staging.

## Run 1: throughput + on-target identity proof

Report: `docs/saturn/evidence/reports/sprint1-r1-throughput.json`
(`capture_sourceboot_throughput.py --startup-vblanks 4096 --max-vblanks
3600`, defaults otherwise; release manifest bound; status `complete`).

- **On-target identity proof: MATCH.** 16 bytes at `0x22400000` equal the
  ELF's own bytes (`d88b17a9…d23b5` both expected and observed), matched at
  startup attempt 683/4096. The 500-byte `saturn_build_identity` struct was
  then read back from target RAM and validated: full match, label equal to
  the manifest's config label (quoted above).
- **SH-2 exceptions: NONE.** All 64 retained `instance.stopped`
  notifications (of 5,296 total across the run) have reason `frame_limit`;
  zero exception/fault/illegal lines in Ymir's captured stderr.
- **Protocol/queue faults: 0.** Terminal coherent queue record:
  `master_failures 0, slave_failures 0, qf 0, qw 0`.
- **FPS (RECORDED, NOT BLOCKING per owner instruction):**
  - Complete tool evidence: 1 measured interval — **2.0 guest FPS**
    (30 VBlanks for the measured frame; mean = median = 2.0 by
    construction from n=1). First presentation appeared ~1,565 VBlanks
    after identity match (initial scene construction latency).
  - Sustained-cadence cross-checks (from the two longer observation
    attempts below): 68 presentations in 3,600 observed VBlanks
    (**≈1.13 FPS**) and 60 in 3,362 (**≈1.07 FPS**). Sustained rate is
    ≈**1.1 FPS**; per-frame cost ≈53 VBlanks, dominated by construction
    (≈24.5 VBlanks/frame) + master finalization (≈5.8) with
    ≈14 dropped-VBlank credits/frame.
  - Measurement-attempt disclosure: the tool's default
    `--presentation-events 2` yields the n=1 interval. Two attempts at a
    full-window distribution (`--presentation-events 100`, then `60`)
    both failed inside the tool — the first because only 68 events fit
    in 3,600 VBlanks, the second in `summarize_cadence` ("phase VBlank
    crossings exceed the observed interval", an attribution invariant
    this build's ≈53-VBlank frames trip). Stopped per the two-attempt
    rule; their diagnostics (source of the sustained figures) are
    preserved in the session scratchpad. A third throughput attempt was
    an instant interpreter-launch failure (empty log, exit 1) and touched
    nothing.

## Run 2: audio mailbox probe

Report: `docs/saturn/evidence/reports/sprint1-r1-audio-probe.json`
(`probe_audio_mailbox.py --frames 3600` — BIOS handoff, 3,600 frames of
target execution, then one peek of the mailbox/SFXB/SCSP/music-diagnostic
regions; exit 0. Two aborted attempts preceded it: one Ymir
closed-stream-at-launch, one caused by a relative `--cue` path that
`YmirClient`'s cwd change breaks — neither produced any measurement.)

Raw mailbox words (sound RAM `0x25A04000`, field names from
`saturn_pcm_protocol.h`):

| Field | Value |
| --- | --- |
| `magic` / `version` / `status` / `heartbeat` | `0x5036` / 2 / 2 / 30,054 (live) |
| `voices_started` | **2** |
| `invalid_samples` | **0** |
| `protocol_faults` | **0** |
| `completion_protocol_faults` | **0** |
| `unknown_opcodes` | 0 |
| `control_saturated` / `sfx_saturated` / `completion_saturated` | 0 / 0 / 0 |
| `control_producer` = `control_consumer` | 4 = 4 (ring drained) |
| `commands_consumed` / `control_consumed` | 4 / 4 |
| `active_voice_count` (end-of-window peek) | **1** |

Raw music diagnostics (`0x25A07F00`): `starts` **2**, `active` **1**,
`notes` 2, `faults` **0**, `reject_mask` **0**, `malformed` 0, `dropped` 0,
`consume_fail` 0, `scsp_fail` 0, `last_failure` 0.

SFXB bundle staged in sound RAM: magic `SFXB` v1, 64 samples, music sample
index 63; the 1,232-byte staged header prefix hash-matches this build's
local `bob_sfx_metadata.bin` (`b1386926…4279a9`, the stage-2 music-wired
blob) — the owner-music bundle on the disc is byte-identical to what the
target staged. SCSP master register `0x020C`.

## Run 3: gameplay screenshot

Screenshot: `docs/saturn/evidence/screenshots/sprint1-r1-candidate.png`
(320x224, 24,032 B, SHA-256
`ff0c77dcc2095a8af6d5c7e850fbb241bbc2bd87e829b718897f905f6982353c`,
Ymir frame hash `e8d912b6fc4c9e6840a796110f647456`, sequence 11100)
Capture report: `docs/saturn/evidence/reports/sprint1-r1-screenshot-capture.json`

`capture_hwtest.py --bios-input --dram-cart --handoff-yield --frames 3600
--post-poke-frames 6000 --allow-invalid --screenshot-output …` — 9,600
frames of target execution after the BIOS handoff, chunked by the tool to
Ymir's 3,600-frame per-request cap. The report records this identity's own
CUE path under `e2-bob-identity-id-b3aceeb28570230b/`, and the on-disk PNG
re-hashes to the value the tool recorded — the image is from THIS
identity's run, not a reused capture. `--allow-invalid` is required and
expected: the `SAT0` telemetry magic at `0x06030000` is stamped only by
the separate hwtest disc, never by a sourceboot disc (same decode_error as
the July 2026 sourceboot captures). It does not affect the screenshot.

**What the frame shows:** BOB gameplay, textured. Green textured grass
terrain with a brown dirt path running across it; two brown brick/wood
structures (the BOB fort walls) with white sloped roofs against black sky;
**Mario** standing on the path center-frame in his red shirt and blue
overalls; a dark **Goomba** actor at lower right; and the full HUD across
the top — Mario-head life counter `x04`, coin counter `x000`, star counter
`x00`. Compared against the A9A-era orientation reference
`e2-sourceboot-mario-freeroam-2026-07-25.png` (Mario on flat white
untextured geometry with a debug overlay), this frame is unambiguously the
gameplay stage rendering with terrain, textures, actor, and HUD present.
Visual quality judgement remains the owner's call; the gate checked here is
only "gameplay stage renders", and it does.

Master SH-2 stopped at `pc=0x060042BE` (`.text`, normal execution — not a
faulted address); all 11 stop notifications have reason `frame_limit`; no
exception/fault lines in captured stderr.

## Gate results

| Gate | Type | Result | Raw evidence |
| --- | --- | --- | --- |
| Music voice active | HARD | **PASS** | music `starts` = 2 (>= 1), music `active` = 1, `active_voice_count` = 1, `voices_started` = 2 (>= 1) |
| Music faults / rejects zero | HARD | **PASS** | music `faults` 0, `reject_mask` 0, `malformed` 0, `dropped` 0, `consume_fail` 0, `scsp_fail` 0 |
| Zero protocol faults | HARD | **PASS** | `protocol_faults` 0, `completion_protocol_faults` 0, `unknown_opcodes` 0, all three saturation counters 0 |
| `invalid_samples` zero | HARD | **PASS** | `invalid_samples` = 0 |
| No SH-2 exceptions | HARD | **PASS** | all stop reasons `frame_limit` across all three runs (64 + 64 + 11 retained); zero exception/fault/illegal lines in any captured stderr; final PCs in `.text` |
| Gameplay visually rendering | HARD | **PASS** | screenshot above: BOB terrain + textures + Mario + Goomba + HUD |
| FPS | RECORDED, NOT BLOCKING | 2.0 mean/median (tool's n=1 interval); ≈1.1 sustained | see run 1; per owner's 2026-08-15 instruction a sub-4 figure does not fail the candidate |

**All six hard gates PASS.** Stage A is headless evidence only — no desktop
launch was performed and no owner-observed claim is made here. Per
`AGENTS.md` the owner-observed CUE remains the only milestone; this report
clears the candidate for that observation, it does not substitute for it.

The recorded FPS is the number the owner will want alongside the visual:
sustained ≈1.1 FPS means ≈53 VBlanks per presented frame, dominated by
scene construction (≈24.5 VBlanks/frame) and master finalization (≈5.8),
with ≈14 dropped-VBlank credits per frame. The 4 FPS floor in `AGENTS.md`
is explicitly overridden as a blocker for this candidate by the owner's
2026-08-15 instruction, but the gap is large and is the obvious next
performance question after the owner's observation.
