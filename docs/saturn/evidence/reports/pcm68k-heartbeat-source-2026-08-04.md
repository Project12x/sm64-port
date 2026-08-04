# PCM68K heartbeat source evidence — 2026-08-04

## Result

The isolated audio lane now has a source-defined MC68000 image boundary and an
executable host model for its boot/heartbeat publication. This is not an
audibility claim and does not alter the sourceboot FPS image.

## Implemented contract

- reset vectors occupy exactly `0x0000-0x03FF`, with entry at `0x0400`;
- all loadable code/data/BSS must end by the reserved stack bottom at `0x3C00`;
  the stack ends at `0x3FFC`, below the mailbox at `0x4000`, with the PCM bank
  fixed at `0x8000`;
- ELF entry must be `0x400`; the loaded reset vector must agree with that entry
  and its initial stack word must agree with the fixed map symbol;
- startup clears only linker-declared BSS;
- the 68K publishes big-endian magic `0x5036`, protocol version `1`, BOOTING,
  then READY plus a monotonically wrapping 16-bit heartbeat; and
- the heartbeat image does not touch SCSP registers or consume commands.

## TDD evidence

The Python suite first failed with `ModuleNotFoundError: No module named
'verify_pcm68k_image'`. The protocol C fixture separately failed on all new
status-field symbols, and the heartbeat fixture failed because
`pcm68k_heartbeat.h` did not exist.

After implementation, these focused host gates passed:

```text
.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py Pcm68kImageContractTests
Ran 11 tests ... OK

gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/audio \
  -Isrc/port/saturn/audio68k tools/saturn/pcm68k_heartbeat_test.c \
  src/port/saturn/audio68k/main.c -o .../pcm68k-heartbeat-green.exe
.../pcm68k-heartbeat-green.exe

gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/audio \
  tools/saturn/pcm_protocol_test.c -o .../pcm-protocol-green.exe
.../pcm-protocol-green.exe
```

The image verifier fixtures cover valid GNU-map layout, nonzero load base,
wrong ELF entry, disagreeing reset PC/initial SP, driver encroachment into the
reserved stack, driver end beyond 16 KiB, mailbox overlap, writable PCM-bank
overlap both across and exactly at its boundary, and unresolved symbols. An
independent review initially found that stack-top-only validation allowed the
first `jsr` to overwrite an accepted image ending at `0x3FFC`; the explicit
`0x3C00` stack bottom and mutation close that defect.

## Cross-image evidence

The owner-approved exact-path toolchain is GCC 11.1.0 targeting `m68k-elf`,
located under the pinned PoneSound checkout. The driver was passed the bundle
with `-B` so every child tool resolved there. The real image verifier reports:

```json
{"driver_end":1192,"image_base":0,"loaded_end":1190,
 "stack_bottom":15360,"stack_top":16380}
```

`readelf` independently reports ELF32, big-endian, MC68000, executable, entry
`0x400`, `.vectors` at `0x0000` with size `0x400`, and `.text` at `0x0400`.
The first eight BIN bytes are `00 00 3F FC 00 00 04 00`, encoding the fixed
initial SP and reset PC. Artifact identities are:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `pcm68k-heartbeat.elf` | 10,164 | `d2d770a3f85984a0d5d1065642dd2519ea873a5d1d6cb36a6cd60c46133b1f76` |
| `pcm68k-heartbeat.bin` | 1,190 | `672290dea99debdd92ba843340f9b6985098934eb4a5a3a5bdcec839b6a18f36` |
| `pcm68k-heartbeat.map` | 2,729 | `d6a20dc06fe772b0f106979702c28faf5d4092b48e1b913bb064c520f23e809c` |

The generated files remain under `build/saturn/audio68k` and are not committed.
A second clean compile/link/objcopy sequence reproduced all three hashes
exactly. The image verifier, 11 fixture tests, heartbeat C gate, and protocol C
gate were then rerun and passed.

## Unexecuted gates

No target CUE or Ymir run is credited. The remaining audible path is:

1. add the bounded SH-2 ring writer and four-voice 68K consumer;
2. generate the public-domain proof bank;
3. build the standalone `soundtest` CUE; and
4. capture heartbeat/command telemetry and obtain a separate manual audible
   confirmation.
