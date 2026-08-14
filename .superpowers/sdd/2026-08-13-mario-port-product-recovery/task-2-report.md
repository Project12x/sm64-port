# Task 2 — baseline Mario restoration

Status: **ACTIVE — DIAGNOSTIC CUE BOOTED; OWNER ACCEPTANCE PENDING**

## Verified source-level result

- Accepted source anchor: `d7b04d61` (A9A Pipe-4 renderer donor).
- Current `demo_emit_mario_range` diverged from the donor by using a neutral
  fixed Gouraud row with material base color and textured `CC_GOURAUD`.
- A focused source-policy regression was RED against that state and GREEN 4/4
  after a bounded local restoration of per-material RGB-times-light Gouraud
  entries, neutral polygon base, and direct textured `CC_REPLACE`.
- This implementation is uncommitted inside an already dirty renderer file. It
  is not a target-proven or accepted transplant.

## Build attempts

1. Current normal-BOB serial build stopped before SH-2 compilation at
   `compile_actor_family_bundle.py`: `family report does not match
   closure-derived semantics`. Dynamic-actor disablement did not bypass the
   unconditional package build/seal.
2. Isolated `d7b04d61` donor build bound only canonical ROM, extracted-PC source
   assets, and Python environment. The apparent missing Windows `./tools/mio0`
   was caused by `make -n` propagating dry-run into the extractor's child tool
   build. A real extractor run regenerated the donor BOB PNG and helpers, then
   stopped at the sound branch's literal `python3`, which selects the unavailable
   Windows App Execution Alias instead of the approved interpreter. A donor-only
   `5808cdbf` compatibility repair replaces that literal with `sys.executable`;
   its focused RED/GREEN regression passes. It does not modify game, renderer,
   package, or actor semantics.
3. With that compatibility repair, donor extraction regenerated its own assets
   and a serial Pipe-4 build produced an ELF (`60c978973e682f8a1cdb3c060d58b8038ce021f7718d8f9c5a430b5cfbaff8d5`),
   ISO (`0e6eb40e868016df8b321187a27cd35738edcbe34d81938b01ef09a2484b3270`),
   and CUE (`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`).
   The generic in-repo Ymir USA-BIOS boot macro ran 3,300 VBlanks with DRAM cart
   and captured a 320x224 frame (`401737ae49bb65bfe91f0488ff0ee7b4b7aed53701cb5cbcadd711df27696375`).
   It proves the donor CUE boots and presents BOB terrain; it does **not** prove
   Mario is visually correct, controllable, animated, or audible.
4. A separate exact-CUE headless run held logical Saturn D-pad Up (`0x1000`) for
   120 frames after startup and captured at VBlank 3,420. The resulting 320x224
   image SHA-256 is `f9dd7a3f133438aed8ab9d6950385b12b04e5690ee0a06ebeb4892d36be002b5`
   and visibly contains red/blue Mario on BOB ground. This demonstrates one
   input-associated rendered state; it is not a substitute for owner checks of
   controls, camera, collision, animation, materials/depth, cadence, or audio.

The visible desktop Ymir session uses this exact CUE for owner review. The
detailed donor log is
`sm64-port/.worktrees/task2-a9a-donor-20260814/build/saturn/task2-a9a-donor-feasibility-20260814.md`.

## Audio scope fact

This historical renderer donor intentionally links
`src/port/saturn/sourceboot/source_audio_stub.c`. Its `play_sound`,
`play_music`, and `audio_init` implementations are no-ops, so this exact CUE
cannot produce music or SFX. Silence during this manual session is expected
source behavior, not a BIOS/profile/Ymir diagnosis. Mario acceptance remains
the narrow purpose of this candidate; real audio is the next dedicated product
change only after that gate.

## Remaining boundary

The next action is owner visual/audio review of the exact launched CUE. If Mario
fails any material, depth-order, animation, scale, input/camera, or audio gate,
the next causal change must be confined to that observed live defect. Do not
open actor, audio, or Whomp's Fortress expansion first.

## 2026-08-14 — current normal-path build, manual gate pending

The current normal sourceboot path, not the historical donor, now links after
one bounded memory-layout repair. `sModeInfo` (72 bytes, master-only camera
transition state) moves to explicitly reset NOLOAD LWRAM. The fixed VDP1
command double-buffer remains HWRAM-owned and unchanged in size; it is merely
placed before generic BSS so its required 32-byte alignment does not waste the
last eight bytes of the HWRAM floor.

The exact `-j1` build bound generic dynamic actors and semantic audio and
exited zero as `id-9a233e934e5ed93c`. Map evidence is
`___end=0x060fe0e8`, leaving `0x1f18` HWRAM bytes against the required
`0x1f00`. Fresh artifact hashes are CUE
`cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` and ISO
`e5eb59df8444330a593a1b66cafcfeea76152b7a2d8cb92994a98c330d9ccd40`.
Ymir is visibly launched with that named CUE and the worktree profile.

An independent explicit-BIOS/DRAM-cart headless run reached live BOB gameplay
at sequence 5,500. Its 320x224 image SHA-256 is
`0f68c9f4e4954b424467f9e62619495b1d2e0c8eae2330382fdc61a50c72ced8`; Mario
and an ordinary scene object are visible and no exception was observed. The
generic hwtest cadence decoder is not valid for this sourceboot trace layout,
so its zero-FPS field is not used.

This does not replace the manual product gate. The owner must still confirm
correct Mario scale/material/Gouraud/depth/animation, a normally spawned BOB
actor with correct texture/height, and audible semantic music/SFX before any
claim of recovery.
