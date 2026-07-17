# Ymir hwtest capture — 2026-07-17

This is BIOS-backed emulator evidence, not retail hardware evidence.

## Inputs

| Input | Value |
|---|---|
| BIOS | user-provided `Sega Saturn BIOS (USA).bin` |
| BIOS size | 524,288 bytes (512 KiB) |
| BIOS SHA-256 | `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f` |
| Emulator | local `ymir-headless` 0.4.0-dev, Release build |
| Disc | `sm64-saturn-hwtest.cue` / ISO SHA-256 `0ea3a852c4beae57245fb2beae5ce615817994718325aa7ca6eb4f6c16edb3f9` |

## Session

The headless JSON-RPC service accepted the BIOS and built the CD filesystem.
After `exec.run_for` for 3,600 frames, the master SH-2 stopped at PC
`0x060402E4`. A `mem.peek` of `0x06010000`, count 64, returned 64 zero bytes;
the `SAT0` magic was not present, so no hwtest telemetry was recorded.

Ymir diagnostics reported an unimplemented CD-block copy operation while the
BIOS was handling the disc. This is an emulator boot/CD limitation, not a
passing or failing cartridge result. The capture runner now supports
`--allow-invalid` to preserve this raw diagnostic read.

Retail Saturn execution with the 4 MiB RAM cartridge remains the authoritative
gate for cartridge, DMA, and VDP1 measurements.
