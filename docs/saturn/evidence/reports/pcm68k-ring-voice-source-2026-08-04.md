# PCM68K bounded ring and voice-state evidence — 2026-08-04

## Result

The isolated audio lane now has an executable SH-2 producer contract and a
68K command consumer/voice-state contract. The real 68K image links the
consumer and stays below its fixed driver/stack boundary. No SCSP register,
sample byte, sourceboot module, renderer path, CUE, or Ymir session changed;
this is not an audibility claim.

## Implemented contract

- one-slot-empty 32-entry ring with no spin or bulk sample copy;
- pointer-free, aligned big-endian 16-bit commands and indices;
- payload publication before the SH-2 producer index;
- corrupt indices, null state, invalid opcodes, and full queues fail closed;
- four round-robin voice states model key-off before reuse;
- only `PLAY`, `STOP_ALL`, and `SET_MASTER` are recognized;
- proof sample metadata is deterministic, aligned, and below the 32 KiB cap;
- at most eight commands are consumed per heartbeat iteration; and
- telemetry is published before each 68K consumer-index update.

Independent review initially returned NO-GO because `STOP_ALL` left the
mailbox's active-slot field stale and final-state-only tests could not detect
early index publication. The repair tracks explicit active-slot state, resets
it to `0xFFFF` after stop-all, and adds compile-time write observers whose
executable fixture requires producer and consumer indices to be the final
writes. The fixture also covers corrupt producer and consumer indices.
Fresh independent rereview returned GO with no remaining blocking findings.

## TDD and verification

The transport fixture first failed because `saturn_pcm_transport.h` did not
exist. The voice fixture independently failed because `pcm_voice.h` did not
exist. The mailbox telemetry fixture then failed on its new undefined offsets,
and the boot fixture failed against deliberately nonzero RAM because producer,
consumer, and telemetry fields were not initialized.

After implementation, five direct host C compile/execute gates passed with
`-std=c11 -Wall -Wextra -Werror`: protocol, transport, publication ordering,
68K model, and heartbeat.
The exact-path GCC 11.1.0 `m68k-elf` bundle compiled and linked the consumer
into the real image. `verify_pcm68k_image.py` passed with:

```json
{"driver_end":2148,"image_base":0,"loaded_end":2148,
 "stack_bottom":15360,"stack_top":16380}
```

The top-level Make host wrapper was attempted but is not credited: MSYS Make
translated the repository root to `/d/...`, which native Python interpreted as
the inaccessible UNC path `\\d\\...`. Direct host gates and the direct image
verifier are green; the wrapper remains an infrastructure defect.

## Remaining audible gates

1. implement reviewed SCSP PCM8 slot words and pitch conversion behind the
   already host-testable voice boundary;
2. generate and hash the three public-domain PCM proof waveforms;
3. build the standalone Yaul `soundtest` CUE and copy the verified driver/bank;
4. prove heartbeat, command consumption, zero drops, and bounded high-water in
   Ymir; and
5. obtain separate manual confirmation that one proof sample is audible.
