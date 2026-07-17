# Saturn hardware-test disc

`make -f Makefile.saturn.mk hwtest` produces a separate diagnostic disc. It
does not boot SM64. The target is deliberately deterministic and leaves a
machine-readable result block in internal work RAM for Ymir's `mem.peek`.

## Telemetry block

The block begins at `0x06010000` and contains sixteen big-endian 32-bit words:

| Word | Field | Meaning |
|---:|---|---|
| 0 | `magic` | `0x53415430` (`SAT0`) |
| 1 | `version` | telemetry schema version, currently `1` |
| 2 | `phase` | hardware characterization phase, currently `1` |
| 3 | `status` | bit 0 cart present, bit 1 cart pass, bit 2 DMA pass, bit 3 VDP1 pass, bit 4 started, bit 31 complete |
| 4 | `cart_id` | detected extended-RAM cartridge ID; required value is `0x5C` |
| 5 | `cart_bytes` | libyaul-reported mapped size |
| 6 | `test_bytes` | progress through the destructive test |
| 7 | `first_bad_offset` | first failing byte offset, or `0xFFFFFFFF` |
| 8-9 | `expected`, `observed` | failing 32-bit pattern pair |
| 10 | `cpu_copy_ticks` | FRT ticks for a volatile cart→WRAM copy |
| 11 | `scu_cart_to_wram_ticks` | SCU-DMA read timing |
| 12 | `scu_wram_to_vdp1_ticks` | SCU-DMA WRAM→VDP1-VRAM timing |
| 13 | `vdp1_draw_ticks` | FRT ticks through the basic polygon submission |
| 14 | `vdp1_command_count` | command-list count used |
| 15 | `vdp1_pixel_estimate` | conservative filled-pixel estimate |

The extended block begins at `0x06010040` and contains fourteen words: cached
and uncached CPU-copy ticks, SH-2 CPU-DMAC ticks/pass, a VDP1 mode mask, and
per-probe timing slots for solid quad, solid repeated-vertex triangle,
Gouraud, transparency, concave, textured quad, and textured repeated-vertex
triangle probes. It is optional for older readers;
the decoder consumes it when a 120-byte read is supplied.

Run the disc paused in Ymir, then read `0x06010000` with `mem.peek`. Emulator
timings are development evidence only; retail hardware remains authoritative.

Ymir returns the bytes as `result.data` in the JSON-RPC response. A complete
headless session can be captured as JSON-lines and decoded directly:

```sh
printf '%s\n' '{"jsonrpc":"2.0","method":"exec.pause","id":1}' \
  '{"jsonrpc":"2.0","method":"mem.peek","params":{"address":"0x06010000","count":120},"id":2}' \
  | ymir-headless --ipl bios.bin --game build/saturn/hwtest/sm64-saturn-hwtest.cue \
  > ymir-session.jsonl
python tools/saturn/telemetry_decode.py ymir-session.jsonl --require-complete
```

The decoder rejects short reads and bad magic, exposes named status flags, and
sets `ok` only when the cart, DMA, VDP1, and complete bits are all present.

For a fully automated emulator capture, use the runner after building
`ymir-headless`:

```sh
python tools/saturn/capture_hwtest.py \
  --ymir path/to/ymir-headless \
  --ipl path/to/bios.bin \
  --game build/saturn/hwtest/sm64-saturn-hwtest.cue \
  --output ymir-hwtest-report.json
```

The report is labeled `evidence_kind: ymir-emulator`; it is not a substitute
for a retail Saturn capture.

For a BIOS-backed Kronos screenshot with an auditable hash manifest:

```powershell
pwsh -File tools/saturn/capture_kronos.ps1 `
  -RetroArch path/to/retroarch.exe `
  -Core path/to/kronos_libretro.dll `
  -Config path/to/retroarch.cfg `
  -Options path/to/Kronos.opt `
  -Game build/saturn/hwtest/sm64-saturn-hwtest.cue `
  -Output docs/saturn/evidence/screenshots/DATE/hwtest-kronos.png `
  -Manifest docs/saturn/evidence/DATE-kronos-manifest.json `
  -Frames 3600 `
  -Bios path/to/saturn_bios.bin `
  -Iso build/saturn/hwtest/sm64-saturn-hwtest.iso
```

For emulator bring-up failures (for example, a BIOS/CD-block incompatibility),
append `--allow-invalid` to preserve the raw 120-byte read and diagnostic error
instead of treating the capture as a passing telemetry report.

## Host-side classifier

The VDP1 polygon probe runs even when the cartridge gate fails; only the
cartridge-dependent SCU DMA measurements are skipped. This keeps emulator and
no-cart captures useful for isolating renderer bring-up from expansion-RAM
behavior.

`tools/saturn/asset_classifier.py` scans C display-list macros without needing a
baserom. For decoded geometry, pass a JSON array of primitives containing
`indices`, `uvs`, and `material`; the report separates topology, material, UV
rectangle, and VDP1 eight-pixel extent rejections. This is the first host-side
slice of the six-way Saturn IR classifier. Decoded reports also include
conservative representation counts for direct quads, untextured triangles,
degenerate textured triangles, split/cropped surfaces, baked surfaces, and
effect fallbacks; this is not yet a renderer or texture converter.

See the [visual progress portfolio](VISUAL_PORTFOLIO.md) for dated emulator
captures and explicit evidence boundaries.

When physical hardware is available, follow the [retail capture handoff](RETAIL_CAPTURE.md)
so raw telemetry, cold-boot repeats, screenshots, and artifact hashes remain
auditable.
