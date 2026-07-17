# Yabause HLE hwtest visible-failure evidence — 2026-07-17

This is development emulator evidence. Yabause HLE does not emulate the
retail 4 MiB DRAM cartridge and cannot establish retail timing or pass/fail
behavior.

## Inputs

| Component | Identity |
|---|---|
| Frontend | RetroArch 1.22.2, Git `69a4f0e` |
| Core | Libretro Yabause 0.9.15 nightly, core SHA-256 `04b20b371275ad8e29072c6a96f38a66a6f30984f2f279b3e29e53165b01067e` |
| Disc | `sm64-saturn-hwtest.iso`, SHA-256 `fc5bec20537355332ebe9eb2a8eade3e8f78b515265c2fdea554409430b42034` |
| Run | 600 frames, HLE BIOS, audio disabled for visual smoke testing |

## Result

The captured 320x224 screen visibly reports:

```text
cart id: 0x00 (REJECTED)
cart test: FAIL
SCU DMA: FAIL (0 ticks)
VDP1 polygon: FAIL (0 ticks)
telemetry: 0x06010000
status: 0x00000010
```

Screenshot SHA-256:
`c845f34837e85686335a7d70bbf71782e0a15019ebdb992ef66d111f7594d284`.

The `0x10` status is the new `STARTED` bit; the HLE run correctly reaches the
visible rejection path, while DMA/VDP1 are intentionally skipped after cart
detection fails. This is not evidence that a retail cartridge returns ID
`0x00`; retail hardware remains authoritative.
