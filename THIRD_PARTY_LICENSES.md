# Third-Party Licenses and Source Boundaries

Last updated 2026-07-26.

This file records third-party material planned for or studied by the Sega
Saturn port. It does not claim to be a complete audit of the inherited SM64
decompilation and port tree, and it does not grant rights to Nintendo game
assets. No baserom, extracted Nintendo assets, or prebuilt game image should be
distributed by this project.

## Inherited repository

The current `Project12x/sm64-port` baseline at
`2b17d081c9798b31b91dc71f37994b0da28cffc9` has no root license file. This
project does not apply a new blanket license to inherited material. Existing
component-specific license files, copyright notices, and upstream obligations
remain applicable and require a separate release audit.

## libyaul 0.3.1

- Repository: <https://github.com/yaul-org/libyaul>
- Pinned commit: `6012f79f237773378c8014e70d8998ad95a38d98`
- SPDX identifier: `MIT`
- Repository path: `third_party/libyaul` (Git submodule/gitlink).
- Status: pinned external dependency. The parent repository records the exact
  commit while libyaul source remains in its upstream repository.
- Build-time patched copy (Sprint 2 T2.2): the sourceboot build stages a
  patched copy of `libyaul/kernel/mm/internal.c` (TLSF private pool
  0xA000 -> 0x4000) plus a verbatim copy of `libyaul/kernel/internal.h`
  into the generated-sources tree and links that object ahead of the
  prebuilt `libyaul.a` (reuse mode: direct-copy + patch of one MIT
  translation unit; the submodule itself is never modified). The change
  notice and pinned SHA-256 record live in
  `tools/patches/libyaul-private-pool-0x4000.patch`; the staged copy
  retains the upstream copyright header, satisfying the license term
  above.

The upstream license text at the pinned revision is:

```text
MIT License

Copyright (c) 2012-2023 Israel Jacquez <mrkotfw@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

If libyaul is vendored, copied, or included in a binary/source distribution,
preserve this license text and the upstream copyright notice as required.

## NetworkX 3.6.1

- Repository: <https://github.com/networkx/networkx>
- Release commit: `7530809bfa1ea7ed6fdf918a4d1431488953cb1f`
- SPDX identifier: `BSD-3-Clause`
- Status: hash-pinned host-tool dependency for exact offline quad matching;
  never linked into Saturn target binaries.

Copyright (c) 2004-2025, NetworkX Developers. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the NetworkX Developers nor the names of its
   contributors may be used to endorse or promote products derived from this
   software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

## malucard/sm64-psx

- Repository: <https://github.com/malucard/sm64-psx>
- Pinned commit studied: `3073845688ea273da78d539b20c45110d8a868c3`
- Root license at pinned revision: none found.
- Status: implementation not included; no reuse rights are assumed.

Only behavioral and architectural lessons may be used until an explicit
license or written permission is obtained and recorded in
`docs/saturn/PROVENANCE.md`. If no permission is obtained, PSX-specific work
must be reimplemented from a clean-room behavior specification.

## yaul-org/libyaul-examples

- Repository: <https://github.com/yaul-org/libyaul-examples>
- Pinned commit studied: `66b648eb059bb8bb7392eac70821605a68205b85`
- Root license at pinned revision: none found.
- Status: example implementation not included; no reuse rights are assumed.

The examples may inform hardware tests at the behavior level. Do not copy their
source unless the applicable terms are identified or permission is granted.

## Sega hardware manuals

Sega Saturn ST-103, ST-013, and ST-TECH-47 are referenced as hardware
documentation. They are not included or redistributed by this repository.

## GPL development tools and prior art

GPL-licensed development tools and emulator source may be used by this public
project. Any copied, forked, or closely ported implementation must retain its
applicable GPL version, copyright notices, license text, and corresponding
source obligations. Tool-only use does not impose a license on this
repository's independent Saturn target.

The currently recorded GPL references are:

- `Project12x/Ymir` fork of `StrikerX3/Ymir`, GPL-3.0, used as an external
  JSON-RPC emulator/debugging tool;
- `FCare/Kronos` and `Yabause/yabause`, GPL-2.0, used as external BIOS-backed
  or HLE emulator tools; and
- `Lobotomy-Software/SlaveDriver-Engine`, GPL-3.0-or-later, used as Saturn
  DMA/VDP2 prior art. Its bounded DMA queue is close-ported into the isolated
  `src/port/saturn/gpl/` component; see `docs/saturn/SLAVEDRIVER_ADAPTATION.md`
  for the pinned revision, changes, and source obligations; and
- `Maxime-XL2/SONIC-Z-TREME`, GPL-3.0, used as Saturn renderer prior art.
  **Adoptable** — see the note below on its no-sale clause.

Exact commits, inspected files, and reuse modes are maintained in
`docs/saturn/PROVENANCE.md`.

### Sonic Z-Treme's no-sale clause is not a code-licence restriction

An earlier reading of this repository treated Z-Treme as licence-contradictory
— GPL-3.0 text alongside a clause forbidding sale — and therefore
lessons-only. **That reading was wrong**, and it is corrected here.

The two statements are not in conflict because they govern different things:

- **GPL-3.0 governs the Z-Treme team's own code.** It is a clean, ordinary
  GPL-3.0 grant.
- **The no-sale clause concerns Sonic**, which the Z-Treme team does not own.
  It is an acknowledgment that selling the work would infringe Sega's rights,
  not a restriction they are imposing on their own code. They could not
  license those rights in either direction — no clause they write makes
  selling lawful, and none is needed to make the code freely usable.

This is the standard position for decompilation and fan-homebrew projects, and
**it is precisely this project's own position with respect to Nintendo.** This
SM64 port is in the identical situation: the port code is freely licensed, the
underlying IP is not ours, and it cannot be sold for that reason rather than
any licensing one.

Z-Treme is therefore treated as ordinary GPL-3.0 prior art, on exactly the
same terms as `SlaveDriver-Engine`: copied, forked or close-ported code is
isolated in the `src/port/saturn/gpl/` component, retains its GPL-3.0
notices and attribution to Maxime-XL2, and is recorded in
`docs/saturn/PROVENANCE.md` with pinned SHA, files inspected, and reuse mode.

The non-commercial constraint that applies to this project applies for the
same underlying reason it applies to Z-Treme — third-party IP — and is
unaffected by which upstream code is adopted.

## GCC runtime library: `soft-fp` (`third_party/gcc-soft-fp/`)

The SH7604 has no FPU and no hardware divide, so GCC's runtime library has
always been linked into every target image. As of 2026-07-26 a subset of that
runtime is additionally **vendored in source form** and compiled by the
project's own pinned `sh-elf-gcc 14.3.0`, so that the engine's floating point
uses GCC's `soft-fp` rather than the much slower `fp-bit.c` that GCC 14 selects
for `sh-elf`. Every vendored file is byte-identical to upstream; none is
modified.

| Files | Upstream | Version | Terms |
|---|---|---|---|
| `soft-fp/*.c`, `soft-fp/*.h` (44 files) | `libgcc/soft-fp/` | GCC 14.3.0 | LGPL-2.1-or-later **with the unlimited linking exception** |
| `include/longlong.h` | `include/longlong.h` | GCC 14.3.0 | LGPL-2.1-or-later with the unlimited linking exception |
| `config/sh/sfp-machine.h` | `libgcc/config/sh/sfp-machine.h` | GCC 15.2.0 | GPL-3.0-or-later **with the GCC Runtime Library Exception 3.1** |

Both exceptions exist precisely for this use and are quoted verbatim in the
headers of the vendored files. The `soft-fp` files state:

```text
   In addition to the permissions in the GNU Lesser General Public
   License, the Free Software Foundation gives you unlimited
   permission to link the compiled version of this file into
   combinations with other programs, and to distribute those
   combinations without any restriction coming from the use of this
   file.  (The Lesser General Public License restrictions do apply in
   other respects; for example, they cover modification of the file,
   and distribution when not linked into a combine executable.)
```

and `sfp-machine.h` states:

```text
Under Section 7 of GPL version 3, you are granted additional
permissions described in the GCC Runtime Library Exception, version
3.1, as published by the Free Software Foundation.
```

Linking these therefore imposes no obligation on the ROM beyond what linking
`libgcc.a` already did. The obligations that do apply — preserving the notices,
and stating that the files are unmodified — are discharged by the untouched
file headers and by `third_party/gcc-soft-fp/README.md`. Because the files are
unmodified, no change notice is required; if any is ever modified, the LGPL's
modification terms apply to that file and a change note must be added there and
in `docs/saturn/PROVENANCE.md`.

## Permissive visual-slice studies (not included code)

The following repositories are reviewed as pattern-only or external-tool
references. Their source is not copied, linked, vendored, or distributed by
this repository, so this section records attribution and the decision boundary
rather than reproducing licenses that are not included here.

- `R11/saturn-libs`, `cecf21a68dfca4388887b28e906bb37b95f0849c`, MIT:
  VDP2 background and controller-state patterns for an original Yaul M1 title
  layer.
- `SaitoTsutomu/Tris-Quads-Ex`,
  `f5acd93873728c45d48c3398382aec380a280182`, Apache-2.0: matching-objective
  reference for a host regression; Blender/PuLP are not dependencies.
- `HailToDodongo/pyrite64`, `297a10e606af6149327364d8b694f136c62b506e`, MIT:
  asset-boundary pattern for a new original Fast3D-to-Saturn IR.
- `VGKintsugi/Ghidra-SegaSaturn-Loader`,
  `c489a190a79d2634b9ecf82e2c0dcec8fd999cf5`, Apache-2.0: optional external
  developer inspection tool.

See `docs/saturn/UPSTREAM_CODE_LEDGER.md` for the exact inspected paths and
intended destination modules. If any of these sources becomes an incorporated
dependency or copied implementation, update this document with the applicable
license text and notices before merging.

## Maintenance rule

Before adding any third-party code, data, binary, tool, or close port, update
this file and `docs/saturn/PROVENANCE.md` with the source revision, license,
reuse mode, files adapted, attribution, notices, and material modifications.
