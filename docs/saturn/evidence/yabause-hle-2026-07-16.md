# Yabause HLE hello-screen evidence — 2026-07-16

This is development smoke evidence, not a Saturn compatibility or performance
claim. Yabause used its high-level-emulation BIOS because no proprietary Sega
Saturn BIOS was provided.

## Test inputs

| Component | Version/identity |
|---|---|
| Disc image | `sm64-saturn-hello.iso`, SHA-256 `2fa969a031ca31853e1059fe3cead2b3884f89502a5f4aa0db7e83a14c20e017` |
| Frontend | RetroArch 1.22.2, Git identity `69a4f0e` reported by the binary |
| Frontend archive | Official stable `RetroArch.7z`, SHA-256 `b2139b1d0f9d4526dc6b5ce23cbb3efdc766096fa6f2c3df016818b486ac6372` |
| Emulator core | Official Libretro Yabause 0.9.15 nightly for Windows x86-64 |
| Core DLL | SHA-256 `04b20b371275ad8e29072c6a96f38a66a6f30984f2f279b3e29e53165b01067e` |
| Core archive | SHA-256 `b392c76c9a8f7ab2151bea7a7fd2eda87cb66ca3a0bc20eab28c0566574992ab` |

The RetroArch and core binaries were downloaded from the official Libretro
buildbot into an ignored workspace directory. They are GPL tools and are not
distributed by this repository.

## Procedure and result

RetroArch loaded the Yabause core and CUE, ran 600 frames, captured the final
frame, and exited with status 0. The log reported 320x240 core geometry at
60 Hz and nine seconds of content runtime. The captured framebuffer visibly
contained:

```text
SM64 SATURN PORT
libyaul 0.3.1 / 6012f79
Phase 0: hello-disc bring-up
```

Screenshot SHA-256:
`13dcf37fd8bd4f4c9194f03fdc9d6f2df82a106693fc0c864f801108c1df2175`.

Yabause warned that no Saturn BIOS was found and that its HLE BIOS can have
many issues. Audio was deliberately disabled for this visual smoke test;
RetroArch logged audio-driver startup errors, which did not affect the captured
VDP2 screen.

## Remaining execution gates

- Repeat in a supported BIOS-backed emulator. Kronos 2.6.0 was attempted, but
  current source requires a real BIOS file to exist even when its “Force HLE
  BIOS” option is selected. No BIOS was obtained or bundled.
- Boot the same image on retail Saturn hardware.
- Repeat future cartridge and timing tests on hardware; this hello target does
  not yet detect or exercise the 4 MiB DRAM cartridge.
