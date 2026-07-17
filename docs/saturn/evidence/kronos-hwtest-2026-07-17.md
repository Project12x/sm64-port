# Kronos BIOS-backed hwtest evidence — 2026-07-17

This is emulator evidence using a real user-provided Saturn BIOS. Retail
hardware remains authoritative for bus timing and cartridge compatibility.

## Inputs and configuration

| Component | Identity |
|---|---|
| BIOS | `Sega Saturn BIOS (USA).bin`, 512 KiB, SHA-256 `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f` |
| Frontend | RetroArch 1.22.2, Git `69a4f0e` |
| Core | Kronos Libretro, SHA-256 `7b4c82c5611d3f63d451673b92e9dfa72cbf98508bdc573f207b791d47b99d7c` |
| Disc | `sm64-saturn-hwtest.iso`, SHA-256 `f96cc450642fac762343f4a78a53c6f70efdebd2a58bf7e174b5285e8fa43925` |
| Core options | `kronos_force_hle_bios = "disabled"`; `kronos_addon_cartridge = "4M_extended_ram"` |
| Run | 3,600 frames, NTSC, screenshot at frame limit |

## Result

The visible screen reported:

```text
cart id: 0x5C (4 MiB detected)
cart test: PASS
CPU copy: 1280 ticks
SCU DMA: FAIL (183 ticks)
VDP1 polygon: PASS (54329 ticks)
telemetry: 0x06010000
status: 0x0000001B
```

The status value is `CART_PRESENT | CART_PASS | VDP1_PASS | STARTED`; the
complete bit is set immediately after the final text flush, so it is not shown
in the on-screen pre-completion printout. The screenshot SHA-256 is
`45eacab64f358cac2d1058b7d1b1cd09becd661835729ed99971b67a93cf1d40`.

This is the first BIOS-backed run to exercise the expanded 4 MiB cartridge
path. Kronos
accepted the ID and completed the destructive memory test; its SCU DMA result
failed and must be reproduced on retail hardware before diagnosing the bus
implementation or target code.
