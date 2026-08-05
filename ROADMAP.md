# Roadmap

## Now — isolate the post-A8 CPU frame cost

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
source- and target-complete with focused host gates green and independent
PASS/APPROVED rereview. Its serial CPU-DMAC/SCU-DMA lane carries transfer
ownership across fields and removes the accepted path's immediate transport
wait. The exact Pipe4 target rebuild exits zero and produces ELF SHA-256
`5926ff276342694249a16b9007de2b2c9d3d241f8f456a9c0db50a8f17d9cab5`.
The first automatic Ymir run exposed and retained a runtime-red zero-actor
publication failure. The scene-neutral repair now treats zero admitted Mario
meshlets as a valid terrain-only two-job graph while actor-preparation errors
remain fail-closed. The final exact ELF is `10e92064...df569ab`. A ten-event
automatic run yields nine stable 36--38-field intervals: 1.63 FPS mean, 1.62
median, and 1.58 1%-low. All ten queue generations retire with the same 2+2
master/slave split and `QW=QF=QQ=0`. A8 therefore closes a correctness and
lifetime prerequisite but does not improve cadence. Split CPU construction
from simulation timing and use that result to scope A9 overlap; do not spend a
manual-test cycle looking for an uplift the automatic evidence disproves.
The field-resolution split instead proves simulation dominance: 283 of 333
measured fields (85.0%) are spent across six source simulation ticks per
presented frame; construction is 49 fields (14.7%), transport/presentation is
one field, and nothing is unattributed. The two-tick catch-up limit resets on
each outer iteration, allowing six ticks before one presentation and dropping
222 more credits across nine intervals. Fix the scheduler so normal+recovery
credit is bounded per presentation generation. Worker-materialized terrain
commands remain a source-backed later reduction, not the current dominant
lever.
The pure presentation-scoped scheduler model reached first source-complete with
normal, four-tick, repeated-credit, and incomplete-publication coverage.
Independent review returned NO-GO: wrapped generation zero aliases unset
sentinels, publish can reopen SERVICE/POLL during the same field, and the new
gate is not yet in `verify-all`. Publication must also be an intent followed by
exact-generation success acknowledgement because the target arm/publish path
is fallible. Those repairs are now source-complete with independent rereview
GO, while the adapter's seven source-contract tests are RED against the legacy
loop. Step 5 is active; the compatibility adapter does not own sourceboot sequencing yet;
the existing renderer and A8 transport remain unchanged for that first
visible-uplift experiment.
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
