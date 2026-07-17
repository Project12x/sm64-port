# Yabause HLE hwtest visible-failure evidence — 2026-07-17

This is development emulator evidence. Yabause HLE does not emulate the
retail 4 MiB DRAM cartridge and cannot establish retail timing or pass/fail
behavior.

## Inputs

| Component | Identity |
|---|---|
| Frontend | RetroArch 1.22.2, Git `69a4f0e` |
| Core | Libretro Yabause 0.9.15 nightly, core SHA-256 `04b20b371275ad8e29072c6a96f38a66a6f30984f2f279b3e29e53165b01067e` |
| Disc | `sm64-saturn-hwtest.iso`, SHA-256 `c6ccfbb0eabfc0ad06704271dde492a186f8ce154e4c017cec14d45baf08fddb` |
| Run | 600 frames, HLE BIOS, audio disabled for visual smoke testing |

## Result

The captured 320x224 screen visibly reports:

```text
cart id: 0x00 (REJECTED)
cart test: FAIL
SCU DMA: FAIL (0 ticks)
VDP1 polygon: PASS (55518 ticks)
telemetry: 0x06010000
status: 0x00000010
```

Screenshot SHA-256:
`ea3ff341585c9440e655069a223cb436694ee4d3bf3b16688612cefacce75b`.

The `0x18` status combines the `STARTED` and `VDP1_PASS` bits; the HLE run
correctly reaches the visible rejection path, independently measures VDP1, and
skips only the cartridge-dependent DMA path. This is not evidence that a retail
cartridge returns ID `0x00`; retail hardware remains authoritative.
