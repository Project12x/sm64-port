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

### Yaul PoneSound example loader reference

| Field | Record |
| --- | --- |
| Upstream | `yaul-org/libyaul-examples` |
| Pinned revision | `66b648eb059bb8bb7392eac70821605a68205b85` |
| License evidence | No repository `LICENSE`/`COPYING` file exists at the pinned tree; the inspected source headers say `See LICENSE for details`. Treat the example as license-unclear and copy none of its source or binary assets. |
| Local inspected checkout | `sm64-port/work/upstream/libyaul-examples` |
| Files inspected | `scsp-ponesound-pcm8/Makefile`, `scsp-ponesound-pcm8/ponesound.c`, `scsp-ponesound-pcm8/ponesound.h`, `scsp-ponesound-pcm8/scsp-ponesound-pcm8.c` |
| Reuse mode | Validation/pattern-only. The example proves that a Yaul title can embed and load a separate `sdrv.bin`, select 512-KiB sound RAM, communicate through fixed SCSP RAM, and issue generic SMPC SNDON/SNDOFF commands without modifying Yaul. |

The example clears all sound RAM while bootstrapping and publishes its shared
`start` field from a VBlank callback. Those are observations, not adopted
runtime contracts. The SM64 path permits a complete clear only during cold
boot or explicit recovery and requires MC68000 timer-driven sequence, note,
envelope, desired-voice, and slot-shadow work independent of game VBlank.
Neither the example's `sdrv.bin`/PCM assets nor its native shared C structures
are copied or distributed.

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

## Task 17 bounded scheduler increment

The bounded allocator, desired-voice, timer, and slot-shadow modules inspected
the following pinned sources before implementation:

| Source | Revision / license | Exact inspected ranges | Reuse mode |
| --- | --- | --- | --- |
| In-tree Project12x SM64 audio | repository pin `36d015fb`; inherited tree has no root license file | `src/audio/playback.c:1199-1372`, `src/audio/effects.c:345-543`, `src/audio/seqplayer.c:712-764,783-921,1403-1450,1971-2014` | Bounded semantic adaptation of release/priority allocation, note lifetime, tuning/pan, and note priority. `effects.c:345-543` was studied for envelope state, decay/sustain/release transitions, and action ordering; Task 17's fixed linear ADSR is original simplified infrastructure, not a close-port of Project12x's arbitrary envelope segments, delay scaling, `GOTO`/`RESTART`/`HANG`, or trace-backed output. No N64 heap, pointer, mixer, RSP, or task code copied. Existing notices remain unchanged. |
| `ponut64/SCSP_poneSound` | `31782e4c61337327f23eb9aa45ecd37fe0944ea0`; MIT | `PROJ/main.c:116-156,507-577`, `jo_demo/pcmsys.c:250-272` | Close-port of the already-approved slot-word/key-order and pitch boundary only. The mutable control structs, VBlank scheduling, driver loop, assets, ADX, and CDDA paths remain excluded. |
| `yaul-org/libyaul-examples` | `66b648eb059bb8bb7392eac70821605a68205b85`; repository license unclear at this pin | `scsp-ponesound-pcm8/ponesound.c:63-92,118-121`, `scsp-ponesound-pcm8/scsp-ponesound-pcm8.c:47-71` | Pattern/validation only; no source or binary copied. Its VBlank-driven `start` publication is explicitly not the Task 17 timer contract. |

Task 17 keeps `sm64_saturn_sequence_vm_event_t` as the input scalar contract.
Package residency and ADSR defaults are MC68000-local scalar bindings; desired
voices and 32-slot shadows are never shared-memory ABIs. Software total-level
attenuation is the sole envelope owner; SCSP EG/release words remain at the
neutral immediate/full value `31`, and key-off occurs only after the bounded
software release reaches zero. This is a simplified infrastructure contract,
not source-faithful ADSR parity. The shadow emits
four-byte `(value, slot, field)` commands and performs no MMIO. Only
`sm64_saturn_scsp_apply_slot_command()` translates a validated command to a
native SCSP word write. The 20 semantic notes currently map deterministically
to slots 0-19; the remaining hardware slots stay unowned until production
layer/residency policy is available.

The freestanding gate discovers the local checkout at pinned PoneSound commit
`31782e4c61337327f23eb9aa45ecd37fe0944ea0`, requires GCC 11.1.0 executable
SHA-256 `e863c1bbcbf86e0493989721346dfa28450236505abbcbc5aeb79f48645a286b`,
force-rebuilds all six objects and their relocatable MC68000 module, verifies
`elf32-m68k`, freshness against every input object, the artifact SHA-256, and
an empty undefined-symbol list. It does not link these modules into the
heartbeat image and does not establish a production timer IRQ, package
residency, complete source envelope-table playback, or target audio result.

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
