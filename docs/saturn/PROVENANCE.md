# Saturn Port Provenance and Reuse Ledger

Last updated 2026-07-16.

This ledger records the exact prior art inspected for the Saturn port, the
permission known at the time of inspection, and how the project may use it. It
is an engineering record, not legal advice.

## Reuse-mode vocabulary

- **Dependency**: consume the upstream package and preserve its required terms.
- **Fork/direct-copy/close-port**: reuse implementation code and preserve all
  applicable license, attribution, notice, and change requirements.
- **Pattern-only**: learn an architectural technique from permissive code but
  write a target-specific implementation.
- **Behavior study**: observe external behavior or high-level outcomes without
  copying implementation.
- **Clean-room**: write a behavior specification, separate it from the source
  implementation, and implement from that specification.

## Source ledger

### Project12x/sm64-port

| Field | Record |
|---|---|
| Repository | <https://github.com/Project12x/sm64-port> |
| Pinned commit | `2b17d081c9798b31b91dc71f37994b0da28cffc9` |
| Role | Existing project baseline and intended upstream fork |
| Root license | No root license file found at the pinned revision |
| Reuse mode | Existing inherited baseline; distribution/legal review required |
| Files/areas inspected | `README.md`, root `Makefile`, `src/pc/`, static display-list game data |

This project does not claim to relicense the inherited tree. Any distribution
plan must separately address the rights and obligations of the decompilation,
port code, tools, and user-supplied game assets.

### malucard/sm64-psx

| Field | Record |
|---|---|
| Repository | <https://github.com/malucard/sm64-psx> |
| Pinned commit | `3073845688ea273da78d539b20c45110d8a868c3` |
| Saturn-stub origin | `e93316cd791aaad109c5cf1cefda171c0cb8145f` |
| Role | Console-port feasibility and architectural prior art |
| Root license | No root license file found at the pinned revision |
| Current reuse mode | Behavior study only |
| Permission needed | Explicit license or written permission for original PSX-port changes before implementation reuse |
| Files/areas inspected | `README.md`, `Makefile`, `src/port/saturn/crt0_saturn.c`, `src/port/saturn/gfx_backend_saturn.c`, PSX graphics/fixed-point/preprocessor areas |

The inspected Saturn files are non-building scaffolding. The graphics file is
still PS1-specific and the selected `Makefile.ss.mk` is absent. No code from
this repository was copied into this bootstrap change.

If permission is not obtained, document the required behavior at the interface
level and perform a clean-room implementation. Do not translate, mechanically
rewrite, or closely port unlicensed PSX-specific source.

### yaul-org/libyaul 0.3.1

| Field | Record |
|---|---|
| Repository | <https://github.com/yaul-org/libyaul> |
| Pinned commit | `6012f79f237773378c8014e70d8998ad95a38d98` |
| Version | `0.3.1` |
| License | MIT |
| Intended reuse mode | Git submodule dependency plus pattern-only study where a custom SM64 renderer is required |
| Repository destination | `third_party/libyaul` at the exact pinned commit |
| Files/areas inspected | `LICENSE`, `VERSION`, `README.md`, `yaul.env.in`, `Makefile`, `env.mk`, `libyaul/build/build.pre.mk`, `libyaul/build/build.post.bin.mk`, `libyaul/build/build.post.iso-cue.mk`, `libyaul/kernel/dbgio/dbgio.h`, `libyaul/kernel/dbgio/dbgio.c`, DRAM-cart C/header, VDP1/VDP2 APIs, DMA, dual-CPU, CD block, fixed-point and libmic3d render/types areas |

The project intends to preserve libyaul's MIT license and attribution as
recorded in the repository-level `THIRD_PARTY_LICENSES.md`.

The hello-disc Makefiles use the installed build-fragment interface documented
by the pinned MIT-licensed libyaul source. `src/port/saturn/hello/main.c` is an
original bring-up program written against the public dbgio and VDP2 APIs; it is
not copied from `libyaul-examples`.

The first renderer will use libyaul's hardware APIs but will not use libmic3d
as a drop-in scene renderer. The inspected mesh/pipeline assumptions do not
match the required SM64 primitive conversion and near-plane clipping behavior.
This is an architecture mismatch, not a reason to recreate libyaul's low-level
hardware support.

### yaul-org/libyaul-examples

| Field | Record |
|---|---|
| Repository | <https://github.com/yaul-org/libyaul-examples> |
| Pinned commit | `66b648eb059bb8bb7392eac70821605a68205b85` |
| Role | Hardware-usage examples and behavioral reference |
| Root license | No root license file found at the pinned revision |
| Current reuse mode | Behavior study only |
| Files/areas inspected | DRAM-cart, dual-CPU, VDP1, CD-block, and SCSP examples |

Do not copy example code unless its applicable license or an explicit grant is
identified and recorded here. No example code was copied into this bootstrap
change.

### andwn/marsdev and andwn/sh-gcc-toolchain

| Field | Record |
|---|---|
| Repositories | <https://github.com/andwn/marsdev>, <https://github.com/andwn/sh-gcc-toolchain> |
| Pinned commits | Marsdev `3318d3f39823154b24ce48bfa5d8fe6e3f6cde3f`; SH toolchain `e5d330c1528758da70bb9e41a3927c648d28a77b` |
| Licenses | Marsdev: MIT; SH toolchain: zlib license embedded in `README.md` |
| Role | Checksum-verified source recipe for a workspace-local SH-2 cross compiler |
| Reuse mode | Build-tool dependency and close use of the documented recipe; no source vendored |
| Files inspected | Marsdev `LICENSE`, `README.md`, `Makefile`, `.gitmodules`; SH toolchain `README.md`, `Makefile` |
| Versions selected | GCC 14.3.0, binutils 2.44, target `sh-elf`, big-endian SH-2, without newlib |

The recipe downloads GCC, binutils, and GCC prerequisites with recorded
SHA-256 values. Those GPL/LGPL build inputs and the GPL `xorriso` host utility
are tools only: none of their source is copied or linked into port code. A
temporary change to GCC's generated `auto-host.h` was required for current
MSYS2 headers; it stayed under the ignored workspace build directory.
The original `tools/saturn/xorrisofs-reproducible` wrapper uses xorriso's
documented `SOURCE_DATE_EPOCH`, `--modification-date`, and
`--set_all_file_dates` contracts to remove current-time and copied-file
metadata from the ISO; no xorriso source is reused.

### yaul-org/libyaul-docker and libyaul-packages

| Field | Record |
|---|---|
| Repositories | <https://github.com/yaul-org/libyaul-docker>, <https://github.com/yaul-org/libyaul-packages> |
| Pinned commits | Docker `e0b4c2d63f1a39f213a67c6ca31e6bc582976de6`; packages `6b2ca3b7f31cbf50b18a577e86d7c9ca324e5f62` |
| Licenses | MIT |
| Role | Official container layout used by the portable bootstrap wrapper |
| Reuse mode | Close adaptation of the documented Docker invocation; no Dockerfile or package source copied |
| Files inspected | Docker `LICENSE`, `README.md`, `Dockerfile`; package repository `LICENSE`, package recipes and repository configuration |

The maintained package endpoint found in these sources is Linux-oriented.
The older MinGW feed documented by libyaul was unavailable during the
2026-07-16 bootstrap, so it was not bypassed or replaced with unverified
binaries.

### Emulator references

| Source | License/status | Use |
|---|---|---|
| `Project12x/Ymir` fork of `StrikerX3/Ymir` | GPL-3.0; upstream base `244d5c841e0cb9b0eb1402b39a7742f7f973b1c2`, agent-debug commit `6efc5324943c27f4db7a1b7c8bcf90f51459b12e` | Primary automated development emulator with execution control, frame hashing, and PNG capture; separate process/tool only |
| `Yabause/yabause` / Libretro Yabause | GPL-2.0; Libretro Windows core 0.9.15 tested | First development emulator; tool only |
| `FCare/Kronos` / Libretro Kronos | GPL-2.0; official release/core researched | Candidate BIOS-backed second development emulator; tool only |
| Mednafen Saturn documentation | Official emulator documentation; BIOS required | Compatibility/performance caveats and future test procedure |

No emulator source, binary, or BIOS is included. The Project12x Ymir fork keeps
its modifications inside the GPL-covered emulator and exposes newline-delimited
JSON-RPC over standard I/O; no Ymir implementation or headers are copied into
this repository. Its implementation record inspected `apps/ymir-headless/src/*`,
Ymir's SDL emulator loop and screenshot service, core software-frame callback,
`Saturn::RunFrame()`/debug-break APIs, and the host-CD worker queue. Existing
BSD-2-Clause xxHash and MIT stb dependencies provide canonical xxh3-128 hashes
and in-memory PNG encoding inside the GPL fork. Reuse mode here is an external
GPL tool/process, not a source dependency. The 2026-07-16 Yabause HLE
smoke result is recorded in `evidence/yabause-hle-2026-07-16.md`. The Kronos
branch of `libretro/yabause` at
`6709c1dd0e26094f005b19c6e473c30809718b78` was inspected to understand its
firmware gate: it requires a real BIOS file to exist before its optional HLE
path. That GPL source informed tool operation only and was not copied. Emulator
results remain separate from retail-hardware evidence.

### Lobotomy-Software/SlaveDriver-Engine

| Field | Record |
|---|---|
| Repository | <https://github.com/Lobotomy-Software/SlaveDriver-Engine> |
| Pinned commit inspected | `a8986591557b6e680550d3c23970284d3b38ff8f` |
| License | GPL-3.0-or-later (`LICENSE.txt`, `README.md`) |
| Role | Saturn FPS-engine prior art for DMA scheduling, fixed-point world organization, and VDP2 setup |
| Files inspected | `DMA.C`, `DMA.H`, `SCL_FUNC.C`, `INITMAIN.C`, `MEMCPY.S`, `LINK.S`, `README.md`, `LICENSE.txt` |
| Reuse mode | Pattern-only / behavior study; no source copied or linked |

The engine demonstrates a queued DMA abstraction that chooses CPU copying or a
Saturn-side transfer based on address ranges, waits for completion, and keeps
the queue interrupt-safe. Its `SCL_FUNC.C`/`INITMAIN.C` code also shows a
hand-managed VDP2 register and frame-display path. These are useful review
inputs for the SM64 renderer and DMA scheduler, but the GPL terms are not
compatible with copying the implementation into this repository. The current
Yaul-based implementation remains a clean-room design using public Yaul APIs.

### Sega hardware documentation

| Reference | Use |
|---|---|
| Sega Saturn Overview Manual, ST-103 | Hardware architecture reference |
| VDP1 User's Manual, ST-013 | Command, polygon, texture, fill, and color-calculation behavior |
| Extended RAM Cartridge Technical Bulletin, ST-TECH-47 | Cartridge detection, map, access, and DMA constraints |

These manuals are documentation references. They are not vendored or
redistributed by this bootstrap change.

## Bootstrap change declaration

The Saturn bootstrap and hardware-test changes contain original integration
code and a small shell wrapper around the documented container workflow. They
copy no implementation source from the PSX port, libyaul-examples, Sega
manuals, or the Docker repository. The libyaul MIT text is reproduced in
`THIRD_PARTY_LICENSES.md`; the hardware-example repository remains
pattern-only because its pinned revision has no root license file.

The following dependency/hello-target commit adds libyaul as a gitlink at the
recorded revision and introduces original Makefiles and hello-screen code. The
Makefile variables and dbgio/VDP2 calls are derived from libyaul's public,
MIT-licensed build and API contracts. No source from the unlicensed examples or
PSX fork is copied.

## Required update points

Update this file whenever the project:

- changes an upstream commit or dependency version;
- inspects additional reference files before implementation;
- changes a source's permission status or reuse mode;
- directly adapts or closely ports permissively licensed code;
- creates a clean-room specification; or
- adds copied attribution, NOTICE, or source-path obligations.

For copied or close-ported code, record the destination file, source file,
source commit, license, substantial modifications, and preserved notices.

## Permission log

| Date | Source | Status | Evidence |
|---|---|---|---|
| 2026-07-16 | `malucard/sm64-psx` | Not requested/recorded in this repository | Reuse remains behavior-only |
| 2026-07-16 | `yaul-org/libyaul` | MIT terms present upstream | Full text recorded in `THIRD_PARTY_LICENSES.md` |
