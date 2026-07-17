# Textured quad/triangle probe capture — 2026-07-17

The hardware-test disc now includes a true textured-quad versus repeated-vertex
textured-triangle probe. This directly exercises the representation split used
by the host classifier for SM64 geometry conversion.

![Textured quad and triangle probe](screenshots/2026-07-17/hwtest-kronos-textured-triangle.png)

Kronos/USA BIOS, 3,600 frames, visibly reported:

```text
cart id: 0x5C (4 MiB detected)
cart test: PASS
SCU DMA: PASS (168 ticks)
tex Q/T: 59687/59688
status: 0x0000007F
```

The complete-bit status is written immediately after the final screen flush;
the screenshot is therefore a visible-progress capture, while the 120-byte
WRAM contract remains the machine-readable result.

| Artifact | SHA-256 |
|---|---|
| Screenshot | `752a78a1e3b62c8516d2f932ef323c840c911913cf0a4407a332aa4be5000908` |
| Manifest | `5ece463c37509979c0a0f16bb721f3af1e9351bf55bcc9b303d17d63d1d643cb` |
| ISO | `fdc2645fb9601eefc7d5f04348553e149280dd07c5bace887cad122695374559` |

The full RetroArch/Kronos provenance is in
`reports/hwtest-kronos-textured-triangle.manifest.json`.

## Full-length repeat

A second independent 3,600-frame run produced the same visible SCU-DMA PASS
and textured timing result (`59687/59688`). Its screenshot SHA-256 is
`c57572a5756f8cd8fc20017b862c9932cb06f7c01ee9f673ebb875f8a89b99b1`; its
manifest SHA-256 is
`18d1844673f1c488d3562c0c15e427a5b0afb4d1cf79674e59059ff1ba56fc80`.
