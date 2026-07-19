# 4 MiB RAM Cart asset policy

The Saturn target is the 4 MiB DRAM cartridge configuration (`0x5C`). The
expansion is a bulk-memory tier, not a larger copy of internal WRAM. Runtime
code must preserve that distinction even while the Castle viewer is running in
an emulator without a cartridge.

## Placement rules

| Data | Preferred tier | Reason |
| --- | --- | --- |
| SH-2 stacks, controller state, collision queries | Internal WRAM | latency-sensitive and mutable |
| Per-frame transformed vertices, depth keys, sort buckets | Internal WRAM | touched repeatedly during a frame |
| VDP1 command staging and DMA descriptors | Internal WRAM | source of SCU/VDP1 submissions |
| Converted SM64 texture/UV banks | 4 MiB cartridge | cold/read-mostly bulk data |
| Course BSP metadata and conservative bounds | 4 MiB cartridge | loaded once, reused by many views |
| Animation banks and LOD blocks | 4 MiB cartridge | large, read-mostly source-derived assets |

Cartridge data is staged into a small internal-WRAM ring before hot traversal or
DMA submission. No frame loop may chase pointers through cartridge memory for
transformed geometry, sorting, collision, or command emission.

## Current Castle viewer boundary

The accepted M4 depth-key cache is intentionally an internal-WRAM hot cache:
one signed 32-bit key plus one validity byte per compiled source UV tile
(approximately 4 KiB for the current 830-tile lobby). Source textures, UVs, and
the compiled BSP remain source-derived and are the next candidates for a
cartridge-backed bank. The viewer keeps a host/emulator fallback until the
`0x5C` detection gate is available; this prevents Ymir's no-cartridge path from
silently changing rendering behavior.

## Integration gates

1. Detect `0x5C` and the full 4 MiB mapping before enabling cartridge banks.
2. Measure CPU and legal SCU read timing on a BIOS-backed emulator and retail
   hardware; never infer cartridge speed from internal-WRAM measurements.
3. Copy CD data through internal WRAM, then populate cartridge banks with SH-2
   writes. SCU reads may feed the staging ring; SCU writes to the cartridge are
   not assumed legal.
4. Report peak internal-WRAM bytes, cartridge bytes, and staging-ring bytes in
   the deterministic capture report.
5. Keep a source-textured Castle capture as the visual regression gate after
   each bank-placement change.

This policy follows the Saturn DRAM-cartridge constraints recorded in
`PLAN.md`, `ROADMAP.md`, and the hardware-test telemetry path; it does not
replace source SM64 geometry or materials with hand-authored substitutes.
