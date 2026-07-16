# Hello-disc build evidence — 2026-07-16

This record covers compilation and disc construction. A linked Yabause HLE
smoke test subsequently reached the expected screen; retail Saturn hardware
has not yet been tested.

## Inputs

| Component | Revision/version | Role |
|---|---|---|
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / 0.3.1 | MIT SDK dependency |
| `andwn/marsdev` | `3318d3f39823154b24ce48bfa5d8fe6e3f6cde3f` | MIT toolchain orchestrator reference |
| `andwn/sh-gcc-toolchain` | `e5d330c1528758da70bb9e41a3927c648d28a77b` | zlib-licensed source recipe |
| GCC | 14.3.0 | GPL cross-compiler build tool |
| GNU binutils | 2.44 | GPL build tools |
| xorriso | 1.5.6 | GPL host-only ISO tool |

The compiler target is `sh-elf`, configured for big-endian SH-2. Libyaul was
built and installed from the repository's pinned submodule using the TLSF
allocator option. The hello build does not require an M68K compiler.

## Commands and results

From an environment containing the installed compiler and libyaul:

```sh
make -f Makefile.saturn.mk clean
make -f Makefile.saturn.mk hello
make -f Makefile.saturn.mk verify-hello
```

The clean build produced the ELF, binary, map, ISO, and CUE. Two successive
clean builds were compared after pinning the volume and node timestamps through
`xorrisofs-reproducible` and `SOURCE_DATE_EPOCH=1784160000`; the ELF,
first-read binary, IP header, ISO, and CUE were byte-identical. The verifier
confirmed:

```text
Class:                             ELF32
Data:                              2's complement, big endian
Machine:                           Renesas / SuperH SH
Entry point address:               0x6004000
Flags:                             0x2, sh2
```

The Saturn IP header contains release date `20260716`, version `V0.001`, and
title `SM64 SATURN HELLO`. The first-read binary is 70,844 bytes.

## Artifacts

| Artifact | Size | SHA-256 |
|---|---:|---|
| `sm64-saturn-hello.iso` | 452,608 bytes | `2fa969a031ca31853e1059fe3cead2b3884f89502a5f4aa0db7e83a14c20e017` |
| `sm64-saturn-hello.cue` | 80 bytes | `7c906be6e5f339c2bcaa95bc068be47aa2b0943882d9552e0d91edbd04d0b0d7` |
| `sm64-saturn-hello.elf` | 424,484 bytes | `66167d756524301af824513483a7a554fb5be4629c65e5e564b05fe3f509c34b` |
| `sm64-saturn-hello.bin` | 70,844 bytes | `c9c2cc0c525ed472bd96ac6a0d1c0e206a61a882494772d90abab8df487ceb9c` |
| `IP.BIN` | 4,108 bytes | `a24f7045283105068f94ed584dd00adf8738ce5dccddecd5b9a7f93240e121a5` |

The CUE references the ISO as a single `MODE1/2048` track at index `00:00:00`.

## Observations and open evidence

- Libyaul's pinned linker layout emits one read/write/execute load segment.
  Treat this as an upstream-layout observation and re-evaluate it before any
  memory-protection-sensitive tooling is introduced.
- Pinned libyaul emitted compiler warnings while building some SDK libraries;
  no SDK source was changed for this target.
- First emulator: passed in Yabause HLE; see
  `evidence/yabause-hle-2026-07-16.md`.
- Second emulator: open. Kronos and Mednafen require a user-supplied Saturn
  BIOS for the supported path; no BIOS was downloaded or bundled.
- Retail NTSC-U/PAL Saturn plus supported 4 MiB cartridge gate: open.
- A second-host clean bootstrap is open; the current MSYS2 toolchain build
  required the generated host-config workaround documented in `BUILDING.md`.
