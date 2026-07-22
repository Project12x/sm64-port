# E2 sourceboot cart READY probe — 2026-07-21

The source-cart acceptance gate (open since the 2026-07-19 cart-bank
milestone) is passed. First formally captured `READY` probe:

| Field | Value | Required |
| --- | --- | --- |
| magic | `0x53434152` (`SCAR`) | `SCAR` |
| stage | `5` (`READY`) | `READY (5)` |
| expected size | `1,694,976` bytes | current build's `SOURCE.DAT` size |
| copied size | `1,694,976` bytes (exact) | == expected |
| cart id | `0x5C` (`DRAM_CART_ID_4MIB`) | `0x5C` |
| cart size | `4,194,304` bytes | `4,194,304` |
| status | `0` (`OK`) | `OK (0)` |

Report: [`reports/e2-sourceboot-cart-ready-2026-07-21.json`](reports/e2-sourceboot-cart-ready-2026-07-21.json)

## Capture conditions

- Build: branch `saturn/bootstrap`, commit `0e79039` tree (disc Product ID
  `T-SM64SB01`, LWRAM CPU-copy VDP1 upload, 0x28000-byte main pool).
- Emulator: `ymir-headless` with the new 32 Mbit DRAM cart support
  (`ymir-agent` commit `bf3e4a4a`), deterministic bounded run:
  240 BIOS frames + 860 post frames (1,100 emulated frames total), USA
  BIOS input macro, `--handoff-yield`.
- Probe address `0x060E25B0` resolved from the build's own symbol table
  (per the standing rule: always re-resolve after relinking; it moves).
- Deterministic sweep context: at 900-1,060 total frames the same probe
  reads `COPYING` with monotonically increasing `copied` (0xD8000 →
  0x178000) and correct id/size/status throughout — the full copy is
  observable in flight, not just its endpoint.

## Why this is emulator evidence, not retail proof

Per the standing evidence rules: Ymir does not model every hardware
restriction (notably the SCU-DMA/LWRAM lockup this port already fixed by
source-level analysis). Retail hardware remains the final authority.

## Known issue directly after this capture

The probe record is destroyed between frames 1,100 and 1,140 by a
runtime writer that sweeps 8-byte `{pointer, 0x54F0}` records across the
`.bss` region spanning at least `gAudioRandom` through the loader's
stage buffer. This happens after `READY` (the values above were read
intact before the sweep) during early game init, while the frontend's
profile counters are still all zero (no display list submitted yet).
Writer identification is in progress via deterministic frame bisection;
the earlier GUI-only illegal-instruction cascade is plausibly the same
sweep eventually reaching state that later code executes or vectors
through.
