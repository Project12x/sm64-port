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

## Follow-up Ymir CD-block patch

The linked GPL Ymir fork then received commit
`9da9c76b` (`feat(cdblock): implement sector copy and move commands`). It
enables dispatcher commands `0x65`/`0x66`, routes copied buffers through the
configured filter chain, implements move deletion, and makes `0x67` return a
clean zero-error response. The fork was rebuilt successfully.

A fresh 600-frame capture with that binary reported
`diagnostics.cd_block_copy_unimplemented: false`, but still returned the same
120 zero bytes. Its stderr now stops at unhandled SMPC on-chip register accesses
before any copy/move command is issued. This narrows the remaining Ymir boot
gap; it does not constitute valid Saturn telemetry.

## Follow-up SCI patch and register snapshot

The fork then received commit `ef8a4e16` (`feat(sh2): retain minimal SCI
register state`), adding register-visible latches for the SH-2 SCI offsets used
by the USA BIOS. A rebuilt 1,800-frame capture removed the unhandled SCI log
messages and archived the stopped master register snapshot in
`evidence/reports/ymir-sci-2026-07-17-r3.json`.

The master SH-2 still stops at PC `0x060402E4` with zero telemetry, and no
CD-block copy/move command is issued. The capture runner now records
`registers_at_stop` so subsequent emulator work can be compared at the exact
same boot point.

The archived `boot_window` at `0x060402C0` disassembles at the stop PC to:

```asm
mov.l   @(0x240,gbr),r0
mov     r0,r4
mov.l   @(0x240,gbr),r0
cmp/eq  r0,r4
bt      .-8
```

With `GBR = 0x06020000`, the BIOS is polling the shared event word at
`0x06020240`. This is the current Ymir investigation target; it is not
evidence that the Saturn image or cartridge test has run.

The follow-up capture reads that word as `0x000006C7`, exactly matching the
stopped `R0`/`R4` values. It is static across the 1,800-frame run, confirming
that the wait loop is not receiving its expected event update.
