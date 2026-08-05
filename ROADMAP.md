# Roadmap

## Now — A5.9 observe atomic dual-SH-2 scheduling

Move terrain execution/merge and Mario transform/classify from fixed CPU-range
ownership into graph-gated, descriptor-owned payload banks. Replace the legacy
CPU-DUAL callback only in one atomic default-path cutover. Gate: a reviewed
target CUE with a manual desktop-Ymir comparison against the accepted 3–4 FPS
baseline.

The ordered terrain-command lookup and pointer-free P2 callback-context gate
are source-review GO and target compile/link/section-green. The single-owner
live cutover is source-complete, reviewed, target-green, and manually boots,
but its roughly 3–4 FPS result did not improve on A3+A4. Per-phase claim,
notify/retire, wait, failure, and quarantine telemetry is now source-complete
and host-green. Repair rereview is GO at `e98210ba`; one serialized target
build is target-green with the appended profile ABI linked and both memory
margins intact. The owner confirmed it remains roughly 3--4 VDP1 FPS, but
manual HUD transcription is unreliable. The active tooling correction
automates desktop Ymir's native VDP1/VDP2/draw counters and, if the available
debug boundary supports it, the queue telemetry too. Those live values
determine the scheduler repair. The FPS half is now live-proven: ten automatic
running-counter snapshots report VDP2 median 60 FPS and VDP1 median 4 FPS
(range 3--4). The first review's evidence-durability repairs are host-green
and rereview is GO. The queue half now has a strict-TDD, host-green capture
tool that binds and hashes the explicit CUE/ELF/Ymir artifacts, proves an ELF
code window exists in target memory, and rejects incoherent P2 telemetry while
sampling each VBlank. A valid matching live capture remains required; queue
counter capture stays open and no scheduler change is authorized yet.
The first cutover build reached link and exposed a 10,032-byte HWRAM overflow;
the active narrow repair relocates 27,744 bytes of master-only terrain merge
scratch to LWRAM. Independent review and the one target rebuild now pass; the
flat desktop result is retained as the A5.8 baseline.

## Next — frame lifetime and transfer overlap

Complete A6 localized recovery, A7 alternating source-bank ownership, A8
deferred VDP1 transfer, and A9 true frame overlap. Gate: no partial-frame
publication, measured terminal waits, and source/target contract evidence.

## Then — full-game hardening

Generalize generated scene banks, animated actor/enemy banks, and coarse
BSP/frustum/portal-window admission beyond BOB. Add full occlusion/PVS only
when level evidence proves the coarse path is insufficient.

## Parallel prototype — Saturn PCM audio

The standalone PCM68K soundtest is independently reviewed and owner-accepted:
heartbeat advances, commands are consumed, A/B/C are audible, X stops, and
drops remain zero. The next audio boundary is an explicitly approved
sourceboot integration slice with measured transport cost. Positional audio,
sample extraction, music, and sequencing remain later work.
