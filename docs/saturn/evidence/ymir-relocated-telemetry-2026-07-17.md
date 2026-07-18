# Ymir capture after telemetry relocation — 2026-07-17

The hardware-test telemetry contract moved from `0x06010000` to
`0x06030000`. The original address overlapped the expanded 84 KiB A.BIN image
loaded at `0x06004000`; the overlap could corrupt the program before telemetry
was observable. Both the source and decoder now use the cache-through alias of
the relocated block (`0x06030000`, extended block `0x06030040`).

The capture runner's `--bios-input` option automated the USA BIOS language and
clock screens through Ymir's `input.pulse`. The BIOS successfully authenticated
the disc and read `A.BIN`, but the relocated block remained zero after the
bounded run. This is a stronger handoff trace than the earlier language-screen
captures, but it is still not valid SAT0 telemetry.

Raw telemetry and report:

- `docs/saturn/evidence/raw/ymir-relocated-telemetry-2026-07-17.bin`
- `docs/saturn/evidence/reports/ymir-relocated-telemetry-2026-07-17.json`

Raw SHA-256: `6edd9f6f9cc92cded36e6c4a580933f9c9f1b90562b46903b806f21902a1a54f`.
