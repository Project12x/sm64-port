# Ymir hwtest capture — 2026-07-17

This is BIOS-backed emulator evidence, not retail hardware evidence.

## Inputs

| Input | Value |
|---|---|
| BIOS | user-provided `Sega Saturn BIOS (USA).bin` |
| BIOS size | 524,288 bytes (512 KiB) |
| BIOS SHA-256 | `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f` |
| Emulator | local `ymir-headless` 0.4.0-dev, Release build |
| Disc | `sm64-saturn-hwtest.cue` / ISO SHA-256 `f96cc450642fac762343f4a78a53c6f70efdebd2a58bf7e174b5285e8fa43925` |

## Session

The headless JSON-RPC service accepted the BIOS and built the CD filesystem.
After `exec.run_for` for 600 frames, the master SH-2 stopped at PC
`0x060402E4`. A `mem.peek` of `0x06010000`, count 120, returned 120 zero bytes;
the `SAT0` magic was not present, so no hwtest telemetry was recorded.

Ymir diagnostics reported an unimplemented CD-block copy operation while the
BIOS was handling the disc. This is an emulator boot/CD limitation, not a
passing or failing cartridge result. The capture runner now supports
`--allow-invalid` to preserve this raw diagnostic read.
New reports retain the complete stderr stream and set
`diagnostics.cd_block_copy_unimplemented: true` for the observed
`Get copy error command is unimplemented` diagnostic.

A fresh 600-frame rerun verified `protocol.ready: true`,
`stopped_reasons: ["frame_limit"]`, and that diagnostic flag. Its local raw
JSON report SHA-256 was
`9fac44a4df2329b729b415a51b24c4402366498dae8d8c23d2c161667abb38c4`.

The runner's new raw-export path also produced exactly 120 bytes; the zeroed
payload SHA-256 was
`6edd9f6f9cc92cded36e6c4a580933f9c9f1b90562b46903b806f21902a1a54f`.
The raw JSON report SHA-256 is
`4da0901627945b6e82a7913ef7f9bb415a3649432d6f8ad0010a4a0ca8c0e786`.

As a control, the same BIOS session and 600-frame protocol sequence were run
with the previously proven `sm64-saturn-hello.cue`. It stopped at the same
master PC (`0x060402E4`), returned zero bytes at `0x06004000`, and emitted the
same CD-block copy diagnostic. The failure is therefore not specific to the
new hwtest IP header or cartridge code.

Retail Saturn execution with the 4 MiB RAM cartridge remains the authoritative
gate for cartridge, DMA, and VDP1 measurements.
