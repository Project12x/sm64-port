# Clean BIOS-backed Ymir hwtest — 2026-07-17

This capture uses the USA BIOS and the fixed Project12x Ymir fork commit
`4d517116`, with automated language/clock input and no debugger memory write.
`ymir-headless` now yields its host thread after each bounded frame so its
CD-block worker can make progress during `exec.run_for`; this changes host
scheduling only, not emulated cycles.

The disc authenticated, loaded `A.BIN`, entered the Saturn hwtest, and wrote
valid `SAT0`/`SATX` telemetry at `0x06030000`. The VDP1 probe suite completed:
28 commands, mode mask `0x7F`, a 28,672-pixel conservative estimate, and
392,461 draw ticks. The final result is intentionally not an overall pass:
Ymir has no 0x5C 4 MiB DRAM cartridge, so cart detection and its destructive
test correctly show as rejected and cart DMA timings remain zero.

![Ymir BIOS hwtest capture](screenshots/ymir-bios-hwtest-2026-07-17.png)

Machine-readable artifacts:

- `docs/saturn/evidence/raw/ymir-bios-hwtest-2026-07-17.bin`
- `docs/saturn/evidence/reports/ymir-bios-hwtest-2026-07-17.json`

The raw 120-byte telemetry SHA-256 is
`c8fff0dbae69a805ef29545851f49fc30dc1587b8bdd417f6ca7a35933843c45`.
The PNG SHA-256 is
`0642db78ff484064062bbbc0afda0db98607e2bf6d07a12e435a99dbbaf9cb7f`;
its 320x224 RGBA framebuffer hash is
`4bd3d8f1dc2e7219eb229af426bce881`.

This establishes the requested BIOS-backed emulator evidence. It remains
distinct from retail evidence, which still requires actual 0x5C/4 MiB
hardware or an equivalent hardware debugger capture.
