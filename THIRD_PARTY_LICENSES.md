# Third-Party Licenses and Source Boundaries

Last updated 2026-07-16.

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
  for the pinned revision, changes, and source obligations.

Exact commits, inspected files, and reuse modes are maintained in
`docs/saturn/PROVENANCE.md`.

## Maintenance rule

Before adding any third-party code, data, binary, tool, or close port, update
this file and `docs/saturn/PROVENANCE.md` with the source revision, license,
reuse mode, files adapted, attribution, notices, and material modifications.
