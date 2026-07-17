# Current Kronos capture — 2026-07-17

This is a fresh 3,600-frame BIOS-backed Kronos capture of the current
`sm64-saturn-hwtest.iso`, taken after the telemetry layout assertions were
compiled. It is emulator evidence, not retail hardware evidence.

![Current Saturn hardware-test screen](screenshots/2026-07-17/hwtest-kronos-current.png)

The screen visibly reports cart ID `0x5C`, a passing destructive 4 MiB test,
the independent VDP1 probe timings, and the current SCU-DMA observation.

| Artifact | SHA-256 |
|---|---|
| Screenshot | `dad7c344a3a3ec52596bad6aac896e1e58024a70416ccca4fa68e07e7ee83a78` |
| Manifest | `f7a195b9d251fa240e78adb4b05fb62a8f86c9e8f6da543e6fcb16e72aacc899` |
| ISO | `d3f1a3f2bfd89066bc14325745eae425783599a9e3eda2d4c8bdb1dad92a93f7` |
| USA BIOS | `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f` |

The full provenance manifest is
`reports/hwtest-kronos-current.manifest.json`.
