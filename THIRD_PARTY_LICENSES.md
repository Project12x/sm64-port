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
- Status: planned external dependency; no libyaul source is added by the
  bootstrap documentation commit.

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

## Maintenance rule

Before adding any third-party code, data, binary, tool, or close port, update
this file and `docs/saturn/PROVENANCE.md` with the source revision, license,
reuse mode, files adapted, attribution, notices, and material modifications.
