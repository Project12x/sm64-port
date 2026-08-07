# Saturn Port Provenance and Reuse Ledger

Last updated 2026-07-26.

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

### Rulesobeyer/Optimized-Tris-to-Quads-Converter

| Field | Record |
|---|---|
| Repository | <https://github.com/Rulesobeyer/Optimized-Tris-to-Quads-Converter> |
| Pinned commit inspected | `1e1cdb1aaf55bb3e222cd8ecf7233f9065af392c` |
| License | Apache-2.0 |
| Role | Host-side triangle-pair selection prior art |
| Files inspected | `README.md`, `LICENSE`, `__init__.py`, `blender_manifest.toml` |
| Reuse mode | Pattern-only |

The add-on models each dissolvable shared edge as a binary choice, maximizes
selected choices, and constrains each triangle to at most one selected edge.
The Saturn compiler adopts that candidate-graph/matching pattern, but no source
is copied. The Blender/PuLP implementation accepts any selected two-triangle
edge and therefore does not enforce Saturn material, winding, normal,
projected-convexity, UV, or animation constraints. The target-specific
implementation in `tools/saturn/quad_pairing.py` uses strict prefilters; this
architecture mismatch is why the Apache implementation is not directly used.
Candidate selection is delegated to the pinned NetworkX dependency below.

### NetworkX 3.6.1

| Field | Record |
|---|---|
| Repository | <https://github.com/networkx/networkx> |
| Release / pinned commit | `networkx-3.6.1` / `7530809bfa1ea7ed6fdf918a4d1431488953cb1f` |
| License | BSD-3-Clause |
| Role | Exact maximum-cardinality, maximum-integer-quality quad matching |
| Files inspected | `LICENSE.txt`, `networkx/algorithms/matching.py` |
| Reuse mode | Hash-pinned host-tool dependency |

The compiler calls `networkx.max_weight_matching` with `maxcardinality=True`
and integer edge weights. NetworkX's inspected blossom/primal-dual
implementation documents exact integer arithmetic and `O(nodes^3)` runtime.
It is never imported or linked by Saturn target code. The locked universal
wheel SHA-256 is recorded in `tools/saturn/requirements.txt`.

### R11/saturn-libs

| Field | Record |
|---|---|
| Repository | <https://github.com/R11/saturn-libs> |
| Pinned commit inspected | `cecf21a68dfca4388887b28e906bb37b95f0849c` |
| License | MIT |
| Role | VDP2 background and controller-state reference for M1 title presentation |
| Files inspected | `LICENSE`, `README.md`, `saturn-vdp2/core/saturn_vdp2_bg_core.c`, `saturn-vdp2/saturn/saturn_vdp2_bg_saturn.c`, `saturn-vdp2/tests/test_vdp2_bg.c`, `saturn-smpc/core/saturn_smpc_core.c`, `saturn-smpc/tests/test_smpc_buttons.c` |
| Reuse mode | Pattern-only |

The source separates host-testable background/controller state from the SGL
hardware layer. Its NBG1 RGB555 backdrop below NBG0 text and edge-triggered
button pattern will inform original Yaul code in the title proof. SGL calls and
hardware-address conventions are an architecture mismatch with this project;
no R11 code is copied. The exact M1 destination and test evidence are tracked
in `UPSTREAM_CODE_LEDGER.md`.

### SaitoTsutomu/Tris-Quads-Ex

| Field | Record |
|---|---|
| Repository | <https://github.com/SaitoTsutomu/Tris-Quads-Ex> |
| Pinned commit inspected | `f5acd93873728c45d48c3398382aec380a280182` |
| License | Apache-2.0 |
| Role | Independent triangle-pair matching objective for host regression |
| Files inspected | `README.md`, `__init__.py`, `blender_manifest.toml` |
| Reuse mode | Pattern-only |

The Blender add-on selects shared-edge choices while allowing at most one
choice per triangle and favoring selected edge length. This validates the
matching *shape*, but not Saturn rendering safety: it has no material,
winding, UV, convexity, or deformation gate. The project retains NetworkX for
the deterministic matching implementation and may add this objective only as a
host regression oracle. Blender and PuLP are not project dependencies.

### HailToDodongo/pyrite64

| Field | Record |
|---|---|
| Repository | <https://github.com/HailToDodongo/pyrite64> |
| Pinned commit inspected | `297a10e606af6149327364d8b694f136c62b506e` |
| License | MIT |
| Role | Asset-boundary reference for M2/M3 IR and offline conversion |
| Files inspected | `LICENSE`, `README.md`, `src/project/assets/model3d.h`, `src/project/assets/collision.h`, `src/renderer/n64Mesh.h`, `src/renderer/animation.h` |
| Reuse mode | Pattern-only |

Pyrite64's separate model/material, mesh-part, collision, and animation
records are a useful shape for an original limited Fast3D-to-Saturn IR. Its
C++ desktop/libdragon runtime does not fit Yaul/SH-2 execution, so no code is
copied or linked.

### VGKintsugi/Ghidra-SegaSaturn-Loader

| Field | Record |
|---|---|
| Repository | <https://github.com/VGKintsugi/Ghidra-SegaSaturn-Loader> |
| Pinned commit inspected | `c489a190a79d2634b9ecf82e2c0dcec8fd999cf5` |
| License | Apache-2.0 |
| Role | Optional developer-side ISO and emulator-save-state inspection |
| Files inspected | `README.md` |
| Reuse mode | External tool / behavior study |

The loader may aid later debugging of generated media and compatible emulator
state. It is not part of the Saturn executable or asset compiler, and no code
is copied.

### zeux/meshoptimizer 1.1

| Field | Record |
|---|---|
| Repository | <https://github.com/zeux/meshoptimizer> |
| Release / pinned commit | `v1.1` / `dc9d09ed83e1004aef47a1c3c597e0ec64848a37` |
| License | MIT |
| Role | Multi-stream mesh IR, adjacency, remapping, and seam-preservation prior art |
| Files inspected | `LICENSE.md`, `README.md`, `src/meshoptimizer.h`, `src/indexgenerator.cpp`, `src/vfetchoptimizer.cpp` |
| Reuse mode | Pattern-only |

The Saturn mesh IR follows meshoptimizer's useful separation of position,
attribute, and index streams and its rule that remapping must account for all
representation-relevant streams. No meshoptimizer implementation is copied or
linked. The current Python compiler needs VDP1-specific material, winding,
sampled-projection, and deformation-pose gates that do not match
meshoptimizer's GPU-oriented optimization contract; that architecture mismatch
is why it remains pattern-only. A later native host-tool performance pass may
adopt the MIT library directly, with attribution added at that point.

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

The direct-source E2 loader adds an attributed configuration adaptation of
`sh-elf/lib/ldscripts/yaul.x` at
`src/port/saturn/sourceboot/sourceboot-cart.x`. It preserves the normal Yaul
HWRAM layout, then links source-tree read-only data at the documented 32 Mbit
DRAM-cart base (`0x22400000`). `source_cart.c` is original project code using
the public `dram-cart.h`, CD block, and CDFS APIs to stage the separately
emitted `SOURCE.DAT` file from CD into the cart. The source path and exact
pin are recorded in `UPSTREAM_CODE_LEDGER.md`; no Yaul loader implementation
is copied.

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
| Files/areas inspected | DRAM-cart, dual-CPU, VDP1, CD-block, and SCSP examples; `vdp1-mesh/vdp1-mesh.c` and `vdp1-drawing/vdp1-drawing.c` for the VBlank-driven peripheral lifecycle |

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

**Amendment 2026-07-26.** "None of their source is copied or linked into port
code" was already imprecise about one thing and is now explicitly qualified:
GCC's *runtime library* (`libgcc.a`) has always been linked into every target
image, as it must be on a CPU with no FPU and no hardware divide. That is
permitted by the GCC Runtime Library Exception, not by the recipe being
tool-only. The entry immediately below extends the same relationship from a
prebuilt `libgcc.a` to a small set of libgcc runtime *sources* compiled by the
same compiler.

### GCC `soft-fp` (vendored, `third_party/gcc-soft-fp/`)

| Field | Record |
|---|---|
| Upstream | GCC, <https://gcc.gnu.org/> — `libgcc/soft-fp/`, `include/longlong.h`, `libgcc/config/sh/sfp-machine.h` |
| Versions pinned | `soft-fp/*` and `longlong.h` from **GCC 14.3.0**; `config/sh/sfp-machine.h` from **GCC 15.2.0** |
| Upstream change that motivates it | `e95512e2d5a317e8c043f232158df4b38186e51c`, "SH: Use softfp for sh-elf", 2024-10-10 (GCC 15; never backported to 14) |
| Licences | `soft-fp/*` and `longlong.h`: LGPL-2.1-or-later **with the unlimited linking exception** stated in each file header. `sfp-machine.h`: GPL-3.0-or-later **with the GCC Runtime Library Exception 3.1**. |
| Reuse mode | **Direct-copy, verbatim and unmodified**, of a runtime library the project already links in prebuilt form |
| Files vendored | 8 headers + 36 `.c` files under `soft-fp/`, `include/longlong.h`, `config/sh/sfp-machine.h` — 46 files, all byte-identical to upstream |
| Destination | Compiled by the project's pinned `sh-elf-gcc 14.3.0` into `libsm64softfp.a` and linked ahead of `-lgcc` by `src/port/saturn/sourceboot/Makefile` |
| Notices preserved | Yes — every file retains its unmodified upstream header; `third_party/gcc-soft-fp/README.md` records origin, versions, and the fact that nothing was modified |

**Why vendored rather than taken as objects from the GCC 15.2.0 toolchain.**
The design note's option (a1) was to extract prebuilt `.o` files from the
GCC 15.2.0 `libgcc.a` present on this machine and link those. Those objects
were checked and are compatible — identical `EF_SH_MACH` (`0x2, sh2`), same
big-endian ELF32, single multilib (`.;`) on both toolchains, identical
`--target=sh-elf --with-endian=big --with-cpu=m2 --disable-multilib`
configuration, and the four integer helpers they import (`__clzsi2`,
`__ashlsi3_r0`, `__lshrsi3_r0`, `__udiv_qrnnd_16`) all already present in the
14.3.0 archive, with `_ashiftlt.o`, `_lshiftrt.o` and `_udiv_qrnnd_16.o`
byte-identical between the two versions.

Vendoring the sources was preferred anyway, for three reasons that cost
nothing: the `soft-fp` sources are byte-identical between GCC 14.3.0 and
15.2.0 (verified file by file), so there is no version delta to inherit; the
shipped code is then built by the one compiler this project is validated
against, removing cross-version object mixing from the risk surface entirely;
and the host differential test can compile the *same* files it ships rather
than a stand-in. It also leaves no prebuilt binary in the repository. The
GCC 15.2.0 install on this machine is in any case only partly usable — its
`cc1.exe` fails to start (missing runtime DLL, exit 127), so only its binutils
and prebuilt archives were ever available.

**Verification.** `make -f Makefile.saturn.mk verify-softfp-bitexact` compiles
these sources for the host with this same `sfp-machine.h` and diffs every
routine against the host CPU's IEEE-754 hardware, bit for bit, over all 2^32
inputs per single-argument routine plus >=1e7 seeded random pairs per binary
operation. `src/port/saturn/sourceboot/Makefile`'s `verify` target asserts on
the linked ELF that no `fp-bit.c` internal symbol survives.

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
| Files inspected | `DMA.C`, `DMA.H`, `SCL_FUNC.C`, `INITMAIN.C`, `MEMCPY.S`, `LINK.S`, `WALLASM.S:253-353`, `README.md`, `LICENSE.txt` |
| Reuse mode | Close-port in explicitly GPL-3.0-or-later components with preserved notices: bounded DMA queue and Task 1's DIVU launch/schedule; otherwise pattern-only |

The engine demonstrates a queued DMA abstraction that chooses CPU copying or a
Saturn-side transfer based on address ranges, waits for completion, and keeps
the queue interrupt-safe. Its `SCL_FUNC.C`/`INITMAIN.C` code also shows a
hand-managed VDP2 register and frame-display path. These are strong candidates
for direct adaptation in the future Saturn renderer/DMA component; the
adaptation boundary and required Yaul rewrites are recorded in
`docs/saturn/SLAVEDRIVER_ADAPTATION.md`. GPL-derived
components must retain the applicable GPL licensing, notices, and
corresponding-source obligations. The project owner has authorized
GPL-2.0/GPL-3.0 use for this public project. The bounded DMA queue is now
close-ported under `src/port/saturn/gpl/`; its raw-register and pointer-width
assumptions were replaced with public Yaul APIs as recorded in
`docs/saturn/SLAVEDRIVER_ADAPTATION.md`.

The renderer follow-up also inspected `WALLS.C`, `WALLASM.S`, `SPRITE.C`,
`OBJECT.C`, and `V_BLANK.C` at the same commit. Those files demonstrate
sector-local object lists, portal/sector traversal, fixed-point near and screen
clipping that carries shade values through generated vertices, VDP1 Gouraud
submission, and explicit master/slave render records. They are behavior and
architecture references for the future world renderer only; no additional
SlaveDriver renderer source was copied.

**2026-07-28 Task 5b worker close-port:** `WALLS.C:1806-1950` was re-read at
the pinned commit for the bounded master/slave work hand-off and spin-count
balancing discipline. That scheduling boundary is close-ported in
`src/port/saturn/gpl/slavedriver_dual_worker.{c,h}`. The callback, result
arrays, cancellation check, and Yaul polling-mode setup are new adaptations;
the worker never shares output ranges between CPUs. The upstream GPL notice,
repository, commit pin, and this material-change record are retained here;
there is no copied world-renderer or polygon code.

**2026-07-27 Task 1 close-port:** `WALLASM.S:253-353` was re-read at the
pinned commit and narrowly adapted into
`src/port/saturn/gpl/slavedriver_projection.sx` and
`src/port/saturn/gpl/slavedriver_projection.h`. Reuse mode is **close-port**:
the adapted portion retains the GPL-3.0-or-later declaration, upstream path,
pin, and a material-change note. It preserves the useful schedule only —
launch SH-2 DIVU, perform independent projection work, then collect — rather
than copying the wall renderer. The register mapping is checked against the
pinned MIT `libyaul` CPU-DIVU header; sourceboot's target-vector probe is new
project code.

**2026-07-22 re-consultation** (sourceboot gGfxPool collision investigation,
`docs/saturn/evidence/e2-sourceboot-bad-mtx-pointer-2026-07-22.md`): re-read
`WALLS.C` lines ~1240–1300 specifically for its slave-side polygon buffer
discipline. `slaveResult` is a single fixed-capacity array
(`MAXNMSLAVEPOLYS=1300`) indexed by a monotonic counter `nmSlavePolys`, with
an explicit bounds check *before* every write (`slave_drawRectWall`:
`if (height*width+nmSlavePolys+50>MAXNMSLAVEPOLYS) return;`) that drops data
rather than risking an overflow. No two-ends-growing-toward-each-other
allocator anywhere in this file. Pattern study only (see
`UPSTREAM_CODE_LEDGER.md`'s M5 row) — informs the characterization of SM64's
`gGfxPool`/`alloc_display_list` bump allocator as a ported-pattern fragility,
not a destination for new code; no source copied.

**2026-07-23 re-consultation** (Q16.16 render-matrix sprint,
`docs/superpowers/plans/2026-07-23-q16-render-matrix-sprint.md`): consulted
`FLASH/SQRTTAB.H` at the same pinned commit as behavior precedent for
integer square root on the Saturn — a shipped Saturn engine solving the
same no-FPU problem with table-driven integer sqrt. The sprint's
`sm64_saturn_isqrt64` (`src/port/saturn/gfx/saturn_matrix_kernels.h`) is a
clean-room implementation of the standard bit-by-bit shift/compare
algorithm, not a port of SlaveDriver's table approach; no GPL source was
copied, and the new kernel/constructor headers deliberately live outside
`src/port/saturn/gpl/` as GPL-free code (see the clean-room note under
Sonic Z-Treme below).

### johannes-fetz/joengine

| Field | Record |
|---|---|
| Repository | <https://github.com/johannes-fetz/joengine> |
| Pinned commit inspected | `556d081146211b6a1cfa6591d70f9487d406758b` |
| License | Root `LICENSE` is MIT; inspected engine files also carry a BSD-3-Clause-style source header |
| Role | Practical Saturn C API and VDP1 command-buffer prior art |
| Files inspected | `README.md`, `LICENSE`, `jo_engine/math.c:57-70`, `jo_engine/jo/3d.h`, `jo_engine/3d.c`, `jo_engine/vdp1_command_pipeline.c`, `jo_engine/jo/vdp1_command_pipeline.h` |
| Reuse mode | Direct adaptation of the file-noticed Q16 `dmuls.l`/`xtrct` primitive in `src/port/saturn/gfx/saturn_q16_sh2.h`; pattern-only for the current libyaul renderer |

The non-SGL command pipeline grows the VDP1 list in small command-table blocks,
resets it with system/user clipping and local-coordinate commands, then DMA
flushes the blocks to VDP1 VRAM. The approach is permissively licensed, but the
current renderer already uses libyaul's typed command-list API and persistent
allocation, so copying Jo Engine would add an incompatible allocation layer
without solving a current problem. Its useful lesson is the command lifecycle:
allocate outside the hot loop, rebuild only active commands, and treat the
three setup commands as a fixed prefix.

**2026-07-27 Task 1 direct adaptation:** `jo_engine/math.c:57-70`'s
`jo_fixed_mult` instruction idiom was directly adapted into
`saturn_q16_sh2.h`. The original BSD-3-Clause-style source notice is retained
verbatim at that destination; project changes restrict the API to the Q16.16
primitive, add project names/types, and provide a host-reference fallback.
No Jo divide routine was adopted because it immediately reads the quotient and
therefore serializes the SH-2 hardware divider.

### Maxime-XL2/SONIC-Z-TREME

| Field | Record |
|---|---|
| Repository | <https://github.com/Maxime-XL2/SONIC-Z-TREME> |
| Pinned commit inspected | `cff75451c1616aac1236fc2b44223902b55c706b` |
| License | GPL-3.0 (`LICENSE`; additional asset/Sega-library caveats in `README.md`) |
| Role | Shipped-scale Saturn 3D renderer prior art for Gouraud cost, visibility, model arenas, and DMA |
| Files inspected | `README.md`, `LICENSE`, `Projects/SONIC Z-TREME/ZTE/ZT_RENDERING.c`, `ZT_LOADING.c`, `ZT_LOAD_MODEL.c`, `ZT_SPRITES.H` |
| Reuse mode | Pinned behavior/architecture study plus authorized GPL close-port in `src/port/saturn/gpl/`; only the exact ranges recorded in `UPSTREAM_CODE_LEDGER.md` may be reused, with preserved notices and corresponding-source obligations |

Sonic Z-Treme assigns each eligible polygon a stable Gouraud-table slot while
loading a model, initializes the complete Gouraud work area once, and copies
updated tables during VBlank only while realtime Gouraud is enabled. Its own
README records Gouraud shading as a measurable performance regression relative
to flat lighting. This directly supports the intro-face design already in this
tree: persistent command storage, cached per-vertex lighting, a user toggle,
and Gouraud uploads only when the toggle changes. For later level rendering,
its contiguous model arena, DMA uploads, frustum traversal, octree experiment,
polygon counters, and recommendation to submit work early enough to overlap the
slave SH-2 are valuable measurement targets, not implementation to copy.

The comparison and resulting renderer decisions are maintained in
`docs/saturn/RENDERER_PRIOR_ART.md`.

**2026-07-28 renderer-pipeline sprint policy reconciliation:** the owner
decision dated 2026-07-27 resolves the stale Z-Treme no-sale wording as a
Sega-IP disclaimer rather than a source-code prohibition. This does not
authorize Sega assets, the proprietary SGL binary, or unpinned source. The
Saturn port may close-port the pinned GPL-3.0 ranges listed in
`UPSTREAM_CODE_LEDGER.md`, isolated under `src/port/saturn/gpl/`, while
retaining the GPL notice, change record, and corresponding-source obligations.

**2026-07-22 re-consultation** (sourceboot gGfxPool collision investigation,
`docs/saturn/evidence/e2-sourceboot-bad-mtx-pointer-2026-07-22.md`): inspected
`Projects/SONIC Z-TREME/ZTE/workarea.c` in full (51 lines, not previously
read). Every SGL work-area region (`sort_list`, `zbuffer`, `spritebuf`,
`pbuffer`, `clofstbuf`, `commandbuf`) is a fixed, compile-time,
non-overlapping slot sized from a `MAX_POLYGONS`/`MAX_VERTICES`-class
constant via the `AdjWork` cumulative-offset macro chain — never two runtime
pointers growing toward each other. Pattern study only (see
`UPSTREAM_CODE_LEDGER.md`'s M5 row); no source copied.

**2026-07-23 re-consultation** (Q16.16 render-matrix sprint,
`docs/superpowers/plans/2026-07-23-q16-render-matrix-sprint.md`): Sonic
Z-Treme served as the sprint's existence proof — a real,
substantially-complete Saturn 3D game built entirely on SGL's `FIXED`
(Q16.16) fixed-point API, demonstrating that the exact fixed-point matrix
approach this sprint adopted carries a shipped-scale Saturn renderer. No
additional files were read for this and no source was copied. The sprint's
new headers (`src/port/saturn/gfx/saturn_matrix_kernels.h`,
`saturn_matrix_ctors.h`) are deliberately GPL-free clean-room
implementations: although GPL reuse is authorized for this project, GPL
code is confined to `src/port/saturn/gpl/` and is not mixed next to
SM64-adjacent source, per the license-isolation discipline already
established in this ledger.

**2026-07-27 Task 2 close-port**: the BOB Mesh IR bank adopts the bounded hot
promotion shape from `Projects/SONIC Z-TREME/ZTE/ZT_LOADING.c:320-353` at
pinned commit `cff75451c1616aac1236fc2b44223902b55c706b`. The isolated
`src/port/saturn/gpl/ztreme_hot_promotion.{c,h}` module is GPL-3.0 close-port
code: it copies a bounded cold LWRAM slice into a caller-owned HWRAM arena,
aligns each allocation, reports capacity failure, and returns the hot pointer
used by the renderer. SGL globals, Z-Treme mesh types, and DMA register
assumptions were not copied because Mesh IR v2 is an immutable record stream
and the cartridge policy forbids cart/LWRAM pointer chasing in the frame loop.
This is therefore a small API adaptation, not a wholesale loader port.

**2026-07-28 renderer pipeline sprint**: `src/port/saturn/gpl/ztreme_frustum.{c,h}`
is a GPL-3.0-only close-port of the tri-state AABB/frustum decision from
`ZT_FRUSTUM.c:126-161` at `cff75451c1616aac1236fc2b44223902b55c706b`.
The adaptation consumes this port's Q16 camera basis and generated tight BOB
fragment bounds; it does not import Z-Treme PVS, map bounds, SGL globals, or
asset data. `src/port/saturn/gpl/slavedriver_terrain_result.h` remains the
separate SlaveDriver result-boundary close-port described above.
The one-dispatch wrapper `src/port/saturn/gpl/slavedriver_terrain_worker.{c,h}`
is the corresponding close-port of `WALLS.C:1803-1950`; it preserves the
coarse range hand-off and bounded join while keeping VDP1 allocation and game
state on the master.

**2026-08-07 Task 4 (VDP2 NBG0 HUD glyph atlas)**: `src/port/saturn/gfx/saturn_hud_atlas.{h,c}`
is a **pattern-only** study of `ztFont2NBG3()`
(`Projects/SONIC Z-TREME/ZTE/ZT_VDP2.c:10-26`, confirmed at the pinned commit
by direct read: the function spans exactly those 17 lines). The adopted
pattern is the shape of the setup, not any SGL call or parameter: a
dedicated character-mode plane with its own VRAM region separate from the
world-background plane, a single static page (`slPageNbg3`/
`VDP2_SCRN_PLANE_SIZE_1X1`), the same page mapped to all four map-register
slots (`slMapNbg3(page,page,page,page)` mirrors this task's
`vdp2_scrn_normal_map_t` with `.plane_a`/`.plane_b`/`.plane_c`/`.plane_d`
all set to the same `HUD_PND_BASE`), and topmost display priority
(`slPriorityNbg3(7)` -- this task's own priority 7 is set later, in Task 8's
`sourceboot_vdp2_layers_set`, not in this file). No SGL source is copied;
`ztFont2NBG3` itself configures an 8x8 palette-256 plane (`COL_TYPE_256,
CHAR_SIZE_1x1`) via the proprietary SGL API, structurally different from
this task's 16x16 RGB1555-direct plane (`VDP2_SCRN_CCC_RGB_32768,
VDP2_SCRN_CHAR_SIZE_2X2`) built on Yaul's independently-implemented
`vdp2_scrn_cell_format_set`/`vdp2_scrn_pnd_set`.

Two additional facts were verified directly against real vendored source,
not assumed from either reference engine, because they are not expressed by
any single struct field or enum name in Yaul's public API: (1) VDP2
`CHAR_SIZE_2X2` character-pattern data is four separately-addressed,
individually-contiguous 8x8 pixel cells (top-left/top-right/bottom-left/
bottom-right), not one contiguous 16x16 raster -- confirmed against
`vdp2_scrn_pnd_set()`'s aux-mode character-number bit-packing
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_cell.c:320-356`) and
independently against Yaul's own `satconv` texture converter, whose
`TILE_16x16` case reads exactly those four 8x8 quadrants in that order
(`third_party/libyaul/tools/satconv/tile.c:177-208`). (2) this port's VDP2
TV mode is 320x224 (`VDP2_TVMD_HORZ_NORMAL_A` / `VDP2_TVMD_VERT_224`, set in
`user_init()`, `src/port/saturn/sourceboot/main.c:1512-1514`), giving a
20x14 grid of 16x16 cells, not the 32x28 an earlier draft of the plan
assumed for this file. Both are Yaul-dependency verification (existing
`yaul-org/libyaul` row in `UPSTREAM_CODE_LEDGER.md`), not a new source; no
new upstream entry is needed for them beyond noting the fact here.

**2026-08-07 Task 6 (dirty-cell diff/publish)**: `src/port/saturn/gfx/saturn_hud_publish.{h,c}`
is a **pattern-only** study of `SCL_PriIntProc()`
(`SCL_FUNC.C:681-743`, pinned commit `a8986591557b6e680550d3c23970284d3b38ff8f`,
confirmed by direct read: the function spans exactly those 63 lines, and its
own header comment states it runs once per VBlank-interval interrupt) and
`SOUND.C`'s `slotDirty[32]` (declared line 24; set on write in
`playSoundMegaE()`, line 307; checked and cleared before reuse in
`playSoundE()`, lines 335-337). The adopted pattern is the shape of the
check -- stage or mark changes as they happen, then at one safe point walk
the dirty markers and flush only the ones set, clearing each as it is
consumed -- not any specific register, DMA call, or audio-slot mechanic.
`sm64_saturn_hud_publish()` applies that shape to an original (col,row,glyph)
record comparison against the previous publish's cell list; no SlaveDriver
struct, macro, or DMA call is copied.

Two supporting checks, both negative, were made directly against source
rather than assumed from the task brief that proposed this citation: (1)
`PRINT.C`/`PRINT.H`, SlaveDriver's own text/HUD-print module, contains no
dirty-tracking of any kind, and in fact renders text as VDP1 sprites
(`sega_spr.h`, `EZ_setLookupTbl`) rather than VDP2 character-pattern cells
at all -- it is not a same-mechanism precedent to compare against, dirty-
tracked or not. (2) Sonic Z-Treme (pinned `cff75451c1616aac1236fc2b44223902b55c706b`,
already cited above for `ztFont2NBG3`'s VDP2 text-plane setup) was
re-checked specifically for a per-frame HUD counter redraw to compare
against, this time tracing the real call chain rather than stopping at the
first plausible-looking match. `draw_stats()` (`ZT_RENDERING.c:146-152`)
issues unconditional `slPrintHex`/`slLocate` calls for `LIVES`, `OWNED`
("Rings or weapons", `ZTE_DEF.H:282`), and a `TIMER`-derived clock value,
with no gate before any of the three -- the function's own early-return at
line 148 is itself commented out, and the only active gate in the function,
`if (showDebugStats==false) ... return;` at line 165, guards solely the
debug readouts that come after these three prints, not the prints
themselves. The call chain confirms this genuinely runs every frame:
`draw_stats()` is called unconditionally for the local player from
`ztRender()` (`ZT_RENDERING.c:792-793`, gated only on
`currentPlayer->PLAYER_ID == 0`, true for `PLAYER_1`, `main.c:119`);
`main_loop()` calls `ztRender(&PLAYER_1, ...)` unconditionally every
invocation (`SRC/game.c:772`); and `main_loop()` itself runs inside
`ztGameLoop()`'s `while (1)` loop, paced by `slSynch()` immediately after
(`ZT_GAME.c:70`, `102`, `107`). The plan's original citation was therefore
correct: Z-Treme's own live gameplay HUD counters do redraw unconditionally
every frame, with no dirty tracking of any kind.

**Correction to this entry (2026-08-07):** the paragraph above originally
cited a different, unrelated line -- a commented-out `slPrint("RINGS : "...)`
label inside `ztReset()` (`SRC/game.c:30`), which only runs on player
death/respawn, not per frame -- and concluded from that one dead line alone
that no live per-frame call site existed in either reference. That
conclusion was wrong: it checked a superficially similar but different
function than the one the plan actually cited, and never examined
`draw_stats()` at all. A spec review caught the discrepancy against the
plan text (which names `ZT_RENDERING.c:146-152` explicitly, at the plan's
own line 20); re-reading the pinned tree directly, as above, confirmed the
plan's original claim, replacing the earlier, incorrect "no live call site"
finding recorded here. The original *negative* finding this citation exists
to support is unaffected and still holds, now on correct evidence: neither
pinned reference implements *per-cell* dirty diffing for VDP2 tile content
specifically -- Z-Treme's counters redraw wholesale every frame with no
diffing at all, and SlaveDriver's text system is not VDP2-cell-based to
begin with (see above). `saturn_hud_publish.c`'s cell-level diff is
therefore still original engineering for this project, not adapted from
either engine.

### Sega hardware documentation

| Reference | Use |
|---|---|
| Sega Saturn Overview Manual, ST-103 | Hardware architecture reference |
| VDP1 User's Manual, ST-013 | Command, polygon, texture, fill, and color-calculation behavior |
| Extended RAM Cartridge Technical Bulletin, ST-TECH-47 | Cartridge detection, map, access, and DMA constraints |

These manuals are documentation references. They are not vendored or
redistributed by this bootstrap change.

### Sega SGL 3.02j SDK documentation

| Field | Record |
|---|---|
| Archive | `SGL302J.ZIP` (user-supplied local copy; not vendored) |
| SHA-256 | `429d729952b6837e2af221a5a6e0de4ca58d2ed1ce2471bd65411a62954ff811` |
| License | Proprietary Sega SDK; no license for reuse |
| Files inspected | `DOC/210A_US/` manual chapters: `MATH.TXT`, `SPRITE.TXT`, `WORKAREA.TXT`, `MEMORY.TXT`, `SGLFAQ_F.TXT`, `SCROLL.TXT`, `PER.DOC`, `INT.TXT`, `EVENT.DOC`, `INIT.DOC`, `DMA.DOC`, `SGL020A.TXT`, `SGL0210.TXT`, `BITMAP.DOC`, `PACKS.TXT`, `MANGFS.TXT` |
| Reuse mode | Behavior study (documentation only) |

The archive contains no SGL library source (`LIB/LIBSGL.A` is a compiled
binary); only official documentation chapters were read. No code, headers, or
samples from the archive are copied, linked, or redistributed. Findings are
recorded as study notes in `docs/saturn/SGL_REFERENCE_NOTES.md`; any technique
adopted from the documentation is reimplemented independently against Yaul.
The study surfaced one hardware constraint with direct code impact (SCU DMA
cannot access WORKRAM-L, corroborated by libyaul's own `scu/dma.h`), recorded
there as a critical finding against the current VDP1 upload path.

**2026-07-23 re-consultation** (Q16.16 render-matrix sprint,
`docs/superpowers/plans/2026-07-23-q16-render-matrix-sprint.md`):
documentation study only, per this section's standing reuse mode. SGL's
`FIXED` type is the identical Q16.16 fixed-point format this sprint
adopted, and `slLookAt(FIXED*, FIXED*, ANGLE)` is precisely a fixed-point
camera lookat — the architectural precedent for the sprint's whole
approach of doing the render-matrix pipeline in Q16.16 on the FPU-less
SH-2 (`work/upstream/sonic-z-treme/Compiler/SGL_302j/INC/SL_DEF.H:909`,
`Documentation/DOC/210A_US/MATH.TXT:313`). `slSquart`/`slSquartFX`
(`SL_DEF.H:869-870`) are the precedent for integer square root. As before,
no SGL code, headers, or samples were copied, linked, or redistributed:
the sprint's kernels (`src/port/saturn/gfx/saturn_matrix_kernels.h`,
`saturn_matrix_ctors.h`) are independent clean-room implementations of
standard algorithms, behavior-grounded in this documentation, mirroring
SM64's own `mtxf_*` formulas — not SGL's implementation, which was never
available to read (`LIB/LIBSGL.A` is a compiled binary).

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
| 2026-07-26 | GCC `libgcc/soft-fp` + `config/sh/sfp-machine.h` | No request needed: both carry an explicit linking exception (LGPL-2.1 unlimited linking exception; GCC Runtime Library Exception 3.1) | Exception text quoted in each vendored file's unmodified header; summarised in `THIRD_PARTY_LICENSES.md` and `third_party/gcc-soft-fp/README.md` |
