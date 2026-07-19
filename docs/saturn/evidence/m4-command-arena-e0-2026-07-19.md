# M4 E0 bounded command-arena verification — 2026-07-19

The Castle/Mario target now consumes `gfx/saturn_command_arena.h`, a bounded,
scene-neutral index contract for persistent variable-length VDP1 lists. It
owns capacity, the fixed setup prefix, current cursor, prior END index, live
upload count, peak count, and overflow state. Yaul continues to own command
encoding and transfer.

Prior art remains the project ledger's pinned dependencies and pattern study:

- `yaul-org/libyaul` at `6012f79f237773378c8014e70d8998ad95a38d98`
  (MIT), using its public VDP1 command-list APIs as a dependency;
- `johannes-fetz/joengine` at
  `556d081146211b6a1cfa6591d70f9487d406758b` (MIT plus file-level BSD-style
  terms), whose persistent command lifetime and fixed setup prefix remain a
  pattern-only reference. No Jo Engine source was copied.

Verification:

- host C11 runtime-contract test: pass;
- native SH-2 compile/link: pass, ELF entry point `0x06004000`;
- deterministic ISO SHA-256:
  `a49a253caf9a4fecac62c6c1464aae7735dfbd5a0e1f61a39176137bf669194e`;
- Ymir USA-BIOS route: 900 requested target frames, screenshot sequence 2400;
- framebuffer hash: `d1459d9bf3b71542d22f260c11d04f77`;
- PNG SHA-256:
  `ca1601c54a1c6f2f70fa4958598c9406d0e0194e86d4b11f4b26275c723e22ab`.

Both image hashes exactly match Stage 115, closing E0's pixel-identity gate.
The BIOS is user supplied and no BIOS or ROM-derived pixel bank is tracked.

![Pixel-identical Castle/Mario frame after bounded command-arena extraction](screenshots/ymir-m4-command-arena-e0-2026-07-19.png)
