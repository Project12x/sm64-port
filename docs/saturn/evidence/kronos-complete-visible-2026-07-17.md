# Completion-bit visible capture — 2026-07-17

The hardware-test screen now sets the completion bit before printing the final
status line. This keeps the human-visible result synchronized with the WRAM
telemetry contract.

![Visible complete status in Kronos](screenshots/2026-07-17/hwtest-kronos-complete-visible.png)

Kronos/USA BIOS, 3,600 frames, visibly reported:

```text
cart id: 0x5C (4 MiB detected)
cart test: PASS
SCU DMA: PASS (168 ticks)
tex Q/T: 59687/59688
status: 0x80007FEF
```

The `0x80000000` completion bit is visible in the status word. The remaining
low bits encode the measured cart, DMA, VDP1, and lifecycle flags from
`docs/saturn/HWTEST.md`.

| Artifact | SHA-256 |
|---|---|
| Screenshot | `6a7bff930daaf600f87380372bb4d64ee55ec4553167ba8d05945bc0f79084b8` |
| Manifest | `b2bcfb5698794b1b76ee9c60613e4df7c9ac86f73b1dbeed9295b7c950810405` |
| ISO | `fbe7dde56e1b086d1944793c878423fc6a98e32b1bb6ad303fd71d3a9115b5bc` |

The full RetroArch/Kronos provenance is in
`reports/hwtest-kronos-complete-visible.manifest.json`.
