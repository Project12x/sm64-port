# Hardware-test disc build evidence — 2026-07-17

This record covers a real cross-build of the Saturn cartridge/DMA/VDP1
hardware-test target. It does not claim emulator or retail execution.

## Build inputs

| Component | Revision/version | Role |
|---|---|---|
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / 0.3.1 | MIT Saturn SDK |
| `andwn/sh-gcc-toolchain` | `e5d330c1528758da70bb9e41a3927c648d28a77b` | zlib-licensed SH-2 toolchain recipe |
| GCC | 14.3.0 | `sh-elf` big-endian cross compiler |
| binutils | 2.44 | SH-2 ELF/linker tools |

The build used the already-installed local `sh-elf` toolchain and Yaul SDK
layout documented in `BUILDING.md`.

## Commands and results

```sh
make -f Makefile.saturn.mk hwtest
make -f Makefile.saturn.mk verify-hwtest
```

The verifier passed:

```text
Verified ELF32 big-endian SH-2 hardware-test image at 0x06004000
```

The linker reported Yaul's known single RWX load segment. No compiler warning
remained after keeping the detected cartridge ID in a typed local variable.

## Artifact hashes

| Artifact | SHA-256 |
|---|---|
| `build/saturn/hwtest/obj/sm64-saturn-hwtest.elf` | `e55e40d97f687dd2829bf74d729f89e7f34c01d2ff59fcb725e4f8d93690111e` |
| `build/saturn/hwtest/obj/sm64-saturn-hwtest.bin` | `45b3458ecdd76a89b823eda619b4da1016b982e3fdf0438838b9035092e8869b` |
| `build/saturn/hwtest/sm64-saturn-hwtest.iso` | `fc5bec20537355332ebe9eb2a8eade3e8f78b515265c2fdea554409430b42034` |
| `build/saturn/hwtest/sm64-saturn-hwtest.cue` | `c0b93a455ca2e183ce7d3cc247a6f3eb53ec35ee909de70e43d52bd185d83462` |

Key ELF fields are `ELF32`, big-endian, Renesas SuperH SH, entry point
`0x06004000`, flags `0x2, sh2`.

## Execution status

The local workspace contains a built `ymir-headless`, but no legal 512 KiB
Saturn IPL/BIOS image. Ymir capture and retail 4 MiB-cartridge evidence
therefore remain open; no synthetic BIOS result is recorded as hardware
evidence.
