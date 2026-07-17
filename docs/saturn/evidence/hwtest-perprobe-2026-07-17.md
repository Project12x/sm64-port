# Per-probe VDP1 timing capture — 2026-07-17

This record predates the visible timing-screen addition. The newer exact build
and screenshot are recorded in `hwtest-visible-probes-2026-07-17.md`.

The hardware-test disc now submits each VDP1 primitive mode as its own
four-command list and records an independent FRT interval in the extended
telemetry block. The corrected source was rebuilt and verified with the pinned
SH-2 toolchain:

```sh
make -f Makefile.saturn.mk hwtest verify-hwtest
```

Current artifacts:

| Artifact | SHA-256 |
|---|---|
| ELF | `2e8e050c4765b7a7621adeed6c9b32c14b4b4ffdfab35653cd62e51ddcf2ba0e` |
| BIN | `ca6705f1c49b80cae7da72a03d37da5373ea6284b6f0baf88cbd552b5c3b32f9` |
| ISO | `a44aac596fda0b75c874983577c07322615c691ba3d815b812b049b6eb5f5842` |

Kronos run configuration: USA BIOS, `4M_extended_ram`, 3,600 frames. The
visible result was cart `0x5C`/4 MiB PASS, CPU copy `1281` ticks, SCU DMA FAIL
(`168` ticks), and aggregate VDP1 PASS (`44334` ticks). The screenshot is
`screenshots/2026-07-17/hwtest-kronos-bios-4m-perprobe.png`, SHA-256
`1f6bfd46f530028d9e7aca382f897de3f4544e9d96ef4e0c4e805361d89e386c`.

This confirms the corrected command sequencing runs under a BIOS-backed
emulator. Per-mode timing values themselves require a WRAM telemetry read;
retail hardware remains authoritative for bus behavior and budgets.
