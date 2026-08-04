# PCM68K SCSP soundtest source evidence — 2026-08-04

## Result

The isolated audio lane now produces a standalone Saturn CUE containing a
source-built 68K PCM8 service and three generated public-domain proof sounds.
This is a build and source-contract result, not an audibility claim. The target
has not been launched in Ymir, and sourceboot remains unchanged.

Implementation commit: `be9b435d` (`audio: add standalone SCSP PCM8
soundtest`). Independent rereview verdict: GO for source and standalone-build
scope; manual Ymir audibility remains open.

## Implemented contract

- Four fixed SCSP slots are programmed by the 68K with big-endian 16-bit
  writes. Address, loop start/end, pitch, envelope, pan/send, and key words are
  bounded; key-off precedes reuse and key-execute is written last.
- Pitch words derive from the SCSP 44,100 Hz base without libgcc division.
  Exact host fixtures cover 44,100, 22,050, and 11,025 Hz.
- The SH-2 target validates asset caps, issues SNDOFF, clears only the mailbox,
  copies the declared driver and bank regions, issues SNDON, and waits at most
  120 VBlanks for matching magic/version/READY plus heartbeat advance. It never
  enqueues on timeout.
- The first low tone is queued automatically after boot. A/B/C queue low/high/
  noise, X stops all voices, and L/R adjust master volume. VDP2 displays
  heartbeat, commands consumed, voices started, drops, and ring high-water.
- Sourceboot, the FPS renderer, and the dual-SH-2 queue are not linked here.

## TDD and build evidence

The SCSP test first failed because `scsp_pcm8.h/.c` did not exist. The consumer
integration test then failed on the missing hardware-consuming entry point.
The proof-bank test first failed importing the absent generator, and the boot
test first failed on the absent soundtest boot module.

After implementation, focused C11 `-Wall -Wextra -Werror` host executables pass
for the SCSP writer, PCM consumer/hardware bridge, heartbeat, and soundtest boot
state machine. The Python generator suite reports `Ran 2 tests ... OK` and
locks the complete bank SHA-256.

The first 68K link exposed two unintended `__udivsi3` references. Replacing
them with a fixed 32-iteration divider and a 16-entry send-level table removed
all unresolved symbols. The real image verifier reports:

```json
{"driver_end":2920,"image_base":0,"loaded_end":2918,
 "stack_bottom":15360,"stack_top":16380}
```

The first independent review was NO-GO because the generic consumer's host
null guard also rejected the 68K's legitimate address-zero sound-RAM map. The
repair retains null rejection in the generic APIs and adds an explicit
mapped-zero target entry point compiled with null-pointer-check deletion
disabled. A three-mutation disassembly verifier proves `pcm68k_main` selects
that entry point and that it passes address zero to the internal consumer
without an early branch. The same repair wave changed SCSP MMIO to aligned
native 16-bit writes, added an ordered observer proving key-off first and
KYONEX last, and seeds the first populated controller report before accepting
one-shot edges. Fresh independent rereview is GO for the source and standalone
build scope: the reviewer confirmed the mapped-zero path and native `movew`
SCSP sequence in the actual 68K disassembly and reran the focused host, image,
and zero-map gates. Manual Ymir audibility remains deliberately unclaimed.

## Fresh artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `pcm68k-heartbeat.bin` | 2,918 | `273d8b28bf6ff6c7edd756dc1ac54ead81a44526a720870e6dba754b40b52643` |
| `pcm_proof_bank.bin` | 4,408 | `05b33bfb7518118b2ab1601e03bdfac65f3f470526cddc8284b3090122b35b58` |
| `sm64-saturn-soundtest.iso` | 471,040 | `e8fc8f2fa1bb2df21d5054d947e2d0ac74fdd1f4cf5fe42c6631976ed9cb55fc` |
| `sm64-saturn-soundtest.cue` | 84 | `b294ce4593a2c22cf931a2c53b9e35d186982fa116c0211f62d0857384a614c3` |

The SH-2 ELF is ELF32, big-endian, Renesas SuperH SH, flags `sh2`, entry
`0x06004000`, and has no unresolved symbols. The guarded serial MSYS build and
target-local CUE input verification pass.

## Open gates

1. one serialized desktop-Ymir run showing READY, heartbeat advance, consumed
   commands, voices started, zero drops, and bounded high-water;
2. separate owner confirmation that at least one generated sound is audible;
3. a measured SH-2 enqueue cost below 1% of the soundtest source-tick; and
4. explicit owner approval before any sourceboot audio integration.
