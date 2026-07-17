# Expanded hwtest build evidence — 2026-07-17

This record covers the first expanded-scene build; the later independent
per-probe timing build is recorded in `hwtest-perprobe-2026-07-17.md`.

The local pinned MSYS2/Yaul cross-build compiled and verified the expanded
DMA/VDP1 hardware-test source.

Command environment used:

```text
sh-elf-gcc 14.3.0
libyaul 0.3.1 at 6012f79f237773378c8014e70d8998ad95a38d98
```

Build and verification:

```sh
make -f Makefile.saturn.mk hwtest verify-hwtest
```

Result:

```text
Verified ELF32 big-endian SH-2 hardware-test image at 0x06004000
```

Current artifact hashes:

| Artifact | SHA-256 |
|---|---|
| `sm64-saturn-hwtest.elf` | `8c835054f319a9e5018b467e48b854cd13db0b591f5f7f9dbd55e62bf3e29b0c` |
| `sm64-saturn-hwtest.bin` | `dfbf3003ed5ebeccf2a6eefe78b763b367c967d0ba0f43d409c2ee8e01562f91` |
| `sm64-saturn-hwtest.iso` | `f96cc450642fac762343f4a78a53c6f70efdebd2a58bf7e174b5285e8fa43925` |
| `sm64-saturn-hwtest.cue` | `c0b93a455ca2e183ce7d3cc247a6f3eb53ec35ee909de70e43d52bd185d83462` |

The expanded runtime submits solid, repeated-vertex, concave,
half-transparent, textured RGB1555, and Gouraud VDP1 probes; it also records
cached/uncached CPU reads and SH-2 CPU-DMAC results in the optional telemetry
block at `0x06010040`. The exact ISO was then run for 3,600 frames in Kronos
with the USA BIOS and 4 MiB extended-RAM option. The resulting screenshot is
archived at `screenshots/2026-07-17/hwtest-kronos-bios-4m-expanded.png` with
SHA-256 `45eacab64f358cac2d1058b7d1b1cd09becd661835729ed99971b67a93cf1d40`.
The visible result was cart `0x5C`/4 MiB PASS, CPU copy `1280` ticks, SCU DMA
FAIL (`183` ticks), and VDP1 PASS (`54329` ticks). Retail execution remains
the authoritative timing and compatibility gate.
