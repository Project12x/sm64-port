# Roadmap

## Now — A5 atomic dual-SH-2 renderer migration

Move terrain execution/merge and Mario transform/classify from fixed CPU-range
ownership into graph-gated, descriptor-owned payload banks. Replace the legacy
CPU-DUAL callback only in one atomic default-path cutover. Gate: a reviewed
target CUE with a manual desktop-Ymir comparison against the accepted 3–4 FPS
baseline.

The ordered terrain-command lookup and pointer-free P2 callback-context gate
are source-review GO and target compile/link/section-green. The single-owner
live cutover is source-complete and host-green after repairing the first
review's terrain handoff NO-GO; scoped re-review is GO. The next transition is one serialized target rebuild
and desktop-Ymir comparison; target cache behavior is proven only when that
new live route executes.
The first cutover build reached link and exposed a 10,032-byte HWRAM overflow;
the active narrow repair relocates 27,744 bytes of master-only terrain merge
scratch to LWRAM. Independent review and the one target rebuild now pass;
the next gate is the fresh desktop-Ymir comparison and live cache behavior.

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
