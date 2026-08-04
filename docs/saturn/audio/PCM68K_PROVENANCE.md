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

## Bounded command/voice-state increment

`saturn_pcm_transport.c` and `audio68k/pcm_voice.c` remain original project
code. They implement the approved pointer-free ring and a hardware-independent
four-voice state model; they do not copy PoneSound's mutable control arrays,
driver loop, register words, pitch calculation, or PCM bytes. The three proof
metadata records reserve aligned regions for later deterministic generated
samples and carry no copyrighted sample content.

The SH-2 producer performs one occupancy check, writes at most eight aligned
16-bit words, then publishes its producer index. The 68K takes a producer
snapshot, consumes at most eight records, publishes telemetry, then publishes
each consumer index. Corrupt indices, invalid sample IDs, unknown opcodes, and
full rings fail closed or increment visible counters. SCSP programming remains
an explicit later gate.

## SCSP PCM8 and proof-bank increment

`audio68k/scsp_pcm8.c` is a documented close-port of only PoneSound's SCSP
slot-word layout, 44.1 kHz OCT/FNS pitch equation, key-off-before-programming,
and key-execute-last sequence. The inspected upstream files are
`PROJ/main.c` and `jo_demo/pcmsys.c` at the pinned revision above. The local
implementation changes the boundary to aligned native 68K 16-bit MMIO writes,
restricts ownership to slots 0-3, validates every offset/count/rate, uses a
bounded freestanding divider, and excludes PoneSound's driver loop, timers,
channel scan, shared structs, ADX, CDDA, and prebuilt binaries.

`tools/saturn/gen_pcm_proof_bank.py` is original project code. Its integer
square-wave and LFSR formulas produce three signed mono PCM8 samples totaling
4,408 bytes. The generated waveform bytes are dedicated to the public domain
under CC0-1.0 and contain no extracted game audio. Generated BIN/header/JSON
artifacts remain ignored build output.
