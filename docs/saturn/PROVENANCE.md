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

### Sega hardware documentation

| Reference | Use |
|---|---|
| Sega Saturn Overview Manual, ST-103 | Hardware architecture reference |
| VDP1 User's Manual, ST-013 | Command, polygon, texture, fill, and color-calculation behavior |
| Extended RAM Cartridge Technical Bulletin, ST-TECH-47 | Cartridge detection, map, access, and DMA constraints |

These manuals are documentation references. They are not vendored or
redistributed by this bootstrap change.

## Bootstrap change declaration

The `saturn/bootstrap` documentation commit contains only original planning and
provenance text. It copies no implementation source from the PSX port,
libyaul-examples, Sega manuals, or other external repositories. The libyaul MIT
text is reproduced in `THIRD_PARTY_LICENSES.md` in preparation for a future
pinned dependency.

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
