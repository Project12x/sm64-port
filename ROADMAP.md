# Roadmap

## Now — implement A8 deferred VDP1 transfer

Use the now-proven dual-SH-2 queue as the producer side of a smaller,
deferred VDP1 command stream. First reduce admitted geometry/command volume
before final lowering; then move command transfer behind an explicit
double-buffered ownership boundary. Gate: exact target identity, unchanged
scene/gameplay state, no stale bank, and automatic presentation/queue evidence.

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
sampling each VBlank. Queue counter capture is live-green. The exact coherent
record reports
`QM=[1,1,0,0]`, `QS=[0,0,1,1]`, `QN=QR=3`, and `QW=QF=QQ=0` with no CPU
failures. The measured three-edge cadence remains only 4.8 FPS mean. This rules
out idle-slave ownership and retirement waiting as the primary observed
bottleneck, so no speculative queue reschedule is authorized. The next FPS
slice should reduce geometry/command volume before final lowering and move
VDP1 command transfer behind an explicit deferred/double-buffered boundary,
then use A9 frame overlap once bank lifetime is proven.
The A7 source slice is complete: command/Gouraud banks now have explicit
build, transfer, publication, quarantine, and retirement states; renderer
failure retains the previous publication; and build/published/displayed
generations are separate. Its SH-2 image compiles and links with the exact
bank memory-map contract. Independent review and the unrelated strict
native-math census repair are closed by independent PASS/APPROVED rereview and
the exact audited exit-zero Pipe4 rebuild. Because A7 still uses the
synchronous completion adapter around today's blocking renderer, visible FPS
uplift is expected from A8/A9 rather than from this ownership-only slice. A8 is
active next; no deferred-transfer behavior is complete yet.
The first cutover build reached link and exposed a 10,032-byte HWRAM overflow;
the active narrow repair relocates 27,744 bytes of master-only terrain merge
scratch to LWRAM. Independent review and the one target rebuild now pass; the
flat desktop result is retained as the A5.8 baseline.

## Next — A9 true frame lifetime overlap

Complete A6 localized recovery around the now-closed A7 ownership contract and
the A8 deferred-transfer seam, then allow frame N+1 production while frame N
is presented. Gate: no partial-frame publication, measured terminal waits,
and source/target contract evidence.

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
