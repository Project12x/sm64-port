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
| 3 | `status` | bit 0 cart present, bit 1 cart pass, bit 2 DMA pass, bit 3 VDP1 pass, bit 31 complete |
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

Run the disc paused in Ymir, then read `0x06010000` with `mem.peek`. Emulator
timings are development evidence only; retail hardware remains authoritative.

## Host-side classifier

`tools/saturn/asset_classifier.py` scans C display-list macros without needing a
baserom. For decoded geometry, pass a JSON array of primitives containing
`indices`, `uvs`, and `material`; the report separates topology, material, UV
rectangle, and VDP1 eight-pixel extent rejections. This is the first host-side
slice of the six-way Saturn IR classifier, not yet a renderer or texture
converter.
