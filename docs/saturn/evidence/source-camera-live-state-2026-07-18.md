# Source-camera live-state probe — 2026-07-18

This paused Ymir read distinguishes a valid source-camera scene from the blank
VDP1 output that preceded viewport rejection. It is emulator debugging
evidence, not retail-hardware proof.

- route: USA BIOS, 900 requested frames, screenshot sequence 2400;
- live painter items: 1,297 visible, 231 rejected;
- camera position: `(-711, 106, 1355)`;
- Q16 forward: `(-54965, 3347, -35762)`;
- Q16 up: `(2805, 65575, 1825)`;
- Q16 right: `(35741, 0, -54933)`;
- stopped PC / PR: `0x0600B054` / `0x0600AF9A` inside `user_init`'s frame loop;
- screenshot frame hash: `566167443d54a6b7be6463eba3093d06`;
- screenshot SHA-256: `c4fbbce9606a3bebc679bf1a89eb0f4fcf9fddfa08bcb59c344d5cf7c46a7e0b`;
- complete local capture-report SHA-256:
  `2cc1efc8ddd20e0981a0aa8556f792cfd1d83bb7884b484f6d2b7843b56e335a`.

The nonzero sort counters and normalized basis prove that the blank frame was
not caused by missing Castle geometry or an uninitialized source camera.
Viewport rejection and VDP1 coordinate saturation subsequently produced the
accepted Stage 87 frame.
