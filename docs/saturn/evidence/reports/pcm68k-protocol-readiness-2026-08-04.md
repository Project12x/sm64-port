# PCM68K protocol readiness — 2026-08-04

## Scope

This is the first source-only increment of the standalone audio lane. It does
not link `sourceboot`, does not initialize SCSP, does not schedule the 68K,
and does not change renderer, SH-2 queue, frame cadence, or the accepted
3–4 FPS Ymir candidate.

## Landed contract

- `src/port/saturn/audio/saturn_pcm_protocol.h` fixes the 512 KiB sound-RAM
  map: 16 KiB driver area, 4 KiB mailbox, 12 KiB reserve, then PCM data at
  `0x08000`.
- The mailbox contains a 32 × 16-byte ring beginning at `0x04040`.
- `PLAY`, `STOP_ALL`, and `SET_MASTER` have fixed nonzero opcode values.
- `sm64_saturn_pcm_put_be16` and `sm64_saturn_pcm_get_be16` are byte-wise;
  no shared C structure or pointer has been introduced.

## Host evidence

`tools/saturn/pcm_protocol_test.c` was added before the header and initially
failed to compile because `saturn_pcm_protocol.h` did not exist. After the
minimal header implementation, the following direct host contract passed:

```text
gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/audio \
  tools/saturn/pcm_protocol_test.c \
  -o build/saturn/host-tests/pcm-protocol-local.exe
build/saturn/host-tests/pcm-protocol-local.exe
```

The repository target `make -f Makefile.saturn.mk verify-pcm-protocol` is
provided for the normal MSYS environment but was not run in this CPU-constrained
source-only lane. No target build or Ymir run is credited.

## Remaining gates

1. Build and verify a source-built fixed-address 68K heartbeat image.
2. Add the bounded SH-2 producer and 68K consumer/voice model with host tests.
3. Generate a public-domain proof bank and boot it only in standalone
   `soundtest`.
4. Obtain serial Ymir telemetry and a separate manual audible confirmation.
5. Request explicit approval before touching `source_audio_stub.c` or adding
   audio to the FPS comparison image.

The detailed task sequence remains
[the approved PCM68K plan](../../../superpowers/plans/2026-08-01-saturn-audio-transport-prototype.md).
