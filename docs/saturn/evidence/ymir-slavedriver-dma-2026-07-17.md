# Ymir BIOS capture after SlaveDriver DMA close-port — 2026-07-17

The hardware-test disc was rebuilt after integrating the GPL-isolated
SlaveDriver-derived queue in `src/port/saturn/gpl/`. The image built with the
pinned SH-2 toolchain and was run for 1,200 frames in Ymir with the supplied
USA BIOS.

The capture completed at the frame limit, but the BIOS did not transfer
control to the disc program: `SAT0` telemetry remained zeroed. The stopped
master SH-2 PC was `0x060402E6`, and the BIOS event word at `0x06020240` was
`0x0000046F`. This is consistent with the existing Ymir BIOS handoff issue,
not evidence that the queue itself ran or failed on Saturn hardware.

Raw telemetry and the full JSON capture are recorded at:

- `docs/saturn/evidence/raw/ymir-slavedriver-dma-2026-07-17.bin`
- `docs/saturn/evidence/reports/ymir-slavedriver-dma-2026-07-17.json`

Raw telemetry SHA-256: `6edd9f6f9cc92cded36e6c4a580933f9c9f1b90562b46903b806f21902a1a54f`.
