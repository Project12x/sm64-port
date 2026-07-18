# Retail Saturn capture handoff

This is the handoff template for the first physical Saturn run. Retail
hardware is authoritative for cartridge compatibility, DMA legality, and VDP1
timing; emulator screenshots must not be substituted for these fields.

## Before powering on

Record:

- console model/region and a lab identifier (do not publish a serial number);
- RAM-cart manufacturer/model and whether it is the mandatory 4 MiB unit;
- disc image/CUE SHA-256 from the exact build under test;
- controller, video mode, and capture device settings; and
- the tool used to read WRAM or observe the diagnostic screen.

Do not write the destructive test to a cartridge. The test only writes the
mapped expansion DRAM returned by the Saturn cartridge interface.

## Run and capture

1. Boot the exact `sm64-saturn-hwtest.cue` with the 4 MiB cart installed.
2. Photograph or losslessly capture the final screen, including the cart ID,
   CPU/SCU timings, the seven VDP1 probe measurements, telemetry address, and
   status.
3. Read 120 bytes beginning at `0x06010000` through the approved hardware
   debugger or capture interface and save the unchanged byte payload.
4. Save the raw bytes unchanged, then decode them with
   `tools/saturn/telemetry_decode.py`.
5. Repeat once after a cold power cycle; retain both runs even if they agree.

The first run is a compatibility result only when the base block has magic
`SAT0`, cart ID `0x5C`, complete bit set, and cart/DMA/VDP1 status bits set.
The extended block at `0x06010040` is required for the cached/uncached,
CPU-DMAC, and per-probe timing comparison.

## Manifest shape

Create a JSON manifest beside the raw dump and screenshots:

```json
{
  "schema": 1,
  "evidence_kind": "retail-hardware",
  "console": {"region": "USA", "model": "...", "lab_id": "..."},
  "cartridge": {"manufacturer": "...", "model": "...", "claimed_mib": 4},
  "build": {
    "cue_sha256": "...",
    "iso_sha256": "...",
    "elf_sha256": "..."
  },
  "telemetry": {
    "address": "0x06010000",
    "bytes": 120,
    "raw_file": "telemetry.bin",
    "decoded_report": "telemetry.json"
  },
  "captures": [
    {"kind": "screen", "path": "hwtest.png", "sha256": "..."}
  ],
  "runs": [{"id": 1, "cold_boot": true}, {"id": 2, "cold_boot": true}]
}
```

Keep the raw dump, manifest, decoded report, and lossless screen capture
together. Do not commit BIOS images, ROMs, or proprietary debugger binaries.
