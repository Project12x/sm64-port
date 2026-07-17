# Visible per-probe timing capture — 2026-07-17

The final hwtest screen now prints the six independent VDP1 timing slots in
addition to the WRAM telemetry address. This makes a BIOS-backed emulator run
auditable from the screenshot alone.

## Exact build

| Artifact | SHA-256 |
|---|---|
| ELF | `70342e4095aeb341a26e90f6353a0c2dbd9a3fad5935abea16ac395950a3f864` |
| BIN | `1fa20aace3165e8268e21c2ce123507939125e6370b9b687df544d3c2f657307` |
| ISO | `d3f1a3f2bfd89066bc14325745eae425783599a9e3eda2d4c8bdb1dad92a93f7` |

## Kronos run

USA BIOS, `4M_extended_ram`, NTSC, 3,600 frames. The visible screen reports:

```text
cart id: 0x5C (4 MiB detected)
cart test: PASS
CPU copy: 1280 ticks
SCU DMA: FAIL (168 ticks)
probes Q/T: 5473/59868
probes C/X: 59609/59690
probes Tx/G: 52683/59688
status: 0x0000001B
```

The screenshot is
`screenshots/2026-07-17/hwtest-kronos-bios-visible-probes.png` with SHA-256
`c81511f104b65b2e362edb2676ef039a51b48feed6715d992836e9b822a467a9`.
The SCU-DMA failure remains an emulator observation; retail hardware is still
required before changing the bus implementation.
