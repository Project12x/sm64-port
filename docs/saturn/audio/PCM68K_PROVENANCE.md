# PCM/68K transport provenance

## Reference

| Field | Record |
| --- | --- |
| Upstream | `ponut64/SCSP_poneSound` |
| Pinned revision | `31782e4c61337327f23eb9aa45ecd37fe0944ea0` |
| License | MIT; verbatim text in [PONESOUND_MIT.txt](PONESOUND_MIT.txt) |
| Local inspected checkout | `sm64-port/work/upstream/SCSP_poneSound` |
| Files inspected | `LICENSE`, `README.md`, `documentation.md`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, `jo_demo/pcmsys.c`, `jo_demo/pcmsys.h` |
| Reuse mode | Pattern-only for the protocol; close-port of the vector-at-zero/reset-entry and linker-section shape in `audio68k/start.S` and `audio68k/linker.ld`. |

## Current increment

`src/port/saturn/audio/saturn_pcm_protocol.h` is original project code. It
uses fixed byte offsets and explicit big-endian stores because the inspected
reference demonstrates the necessary separate-binary, fixed sound-RAM
communication boundary. It does not copy its `sysComPara`, `_PCM_CTRL`,
driver, wrapper, ADX/CDDA path, or `sdrv.bin`.

The shared ABI intentionally differs from the reference:

- no C structs or C pointers cross between SH-2 and 68K;
- every shared field is a defined big-endian byte sequence;
- the transport is a 32-entry, 16-byte command ring rather than the
  reference's mutable control-array protocol; and
- the first scope supports only PCM `PLAY`, `STOP_ALL`, and `SET_MASTER`.

No Nintendo sample, sequence, or prebuilt driver data is included.

## Heartbeat image increment

`src/port/saturn/audio68k/start.S` and `linker.ld` closely port only the
approved vector/reset/linker shape. Each adapted file carries the upstream
repository, pinned SHA, MIT license, copyright, and local change boundary.
The local image differs materially: it has a complete 1 KiB vector table, a
reserved `0x3C00-0x3FFC` stack below `0x4000`, a hard 16 KiB driver cap,
byte-exact BSS clearing, and a fixed mailbox at `0x4000`. `main.c` and
`pcm68k_heartbeat.h` are original
project code that publish protocol magic/version/state/heartbeat only.

No SCSP slot is touched in this increment. PoneSound's driver logic, mutable C
control structs, ADX/CDDA code, high sound-RAM stack, and `sdrv.bin` remain
excluded.

The heartbeat image was compiled with the exact-path GCC 11.1.0
`m68k-elf` bundle stored in the pinned upstream checkout. Its compiler,
assembler, linker, objcopy, nm, readelf, child executables, and colocated DLLs
were validated before use. GCC needed `-B<bundle>/` to locate `cc1`; the bundle
has no target C library headers, so `audio68k/stdint.h` privately derives fixed
integer types from GCC target-width built-ins. No tool executable, DLL, or
generated heartbeat binary is committed or distributed by this increment.
