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
build and a manual HUD observation are next. That evidence determines the
scheduler repair.
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

Review and manually prove the standalone PCM68K soundtest before any game
integration. The promotion gate requires a live heartbeat, consumed commands,
audible generated PCM, zero drops, and explicit owner approval. Sourceboot
sound effects, positional audio, sample extraction, music, and sequencing stay
deferred until that isolated proof is accepted.
