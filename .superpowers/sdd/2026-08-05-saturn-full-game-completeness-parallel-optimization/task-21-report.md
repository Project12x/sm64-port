# Task 21 report — Wave 0/1 sound-CPU boot contract

Status: source-complete for the bounded host boot-contract slice; broader Task
21 remains source-incomplete. No sourceboot feature-on, package/residency,
completion channel, target, Ymir, hardware, manual, or audible claim is made.

## RED evidence

Tests were written before production code. The direct strict compile failed as
expected because `saturn_sound_cpu.h` did not exist. The static ownership test
failed 2/2: there was no project generic-command owner and
`src/port/saturn/soundtest/main.c` contained both warned Yaul convenience
calls. Those failures named the missing wrapper and ownership boundary.

## Implementation

`saturn_sound_cpu.{h,c}` is the single project-owned boundary. The boot model
accepts only cold boot or explicit recovery and requires this order:

```text
pre-staged bytes validated -> generic SNDOFF -> bounded stopped poll
-> SCSP 512-KiB mode -> clear/copy/mailbox owner callbacks
-> explicit publication barrier -> generic SNDON
-> bounded protocol READY plus heartbeat advance
```

Each failure enters a named FAULT state and returns without an unbounded wait.
The target adapter maps Yaul's generic synchronous call into a typed project
`COMPLETED` result; the undocumented OREG31 byte is retained as telemetry and
is never cast to boolean. Actual stopped state and READY/heartbeat probes are
separate bounded evidence. The host callback can return typed rejection and
the model reports SOUND_OFF/SOUND_ON faults.

The wrapper itself does not own package spans: bounded caller callbacks must
validate staged bytes and manifest-owned clear/copy/mailbox regions before the
stop window. Ordinary scene changes have no API here and therefore cannot use
the cold/recovery clear path. Soundtest now routes its proof commands through
the project wrapper, but remains only the existing proof program.

The protocol/soundtest host executables were also moved behind Python
subprocess launch, preventing the inherited MSYS quoted-path/DLL launcher from
opening GUI missing-DLL dialogs. This changes invocation only, not protocol
semantics.

## Reference and reuse record

- Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, MIT,
  `smpc/smc.h:93-102,199-215`: dependency/API-boundary use.
- PoneSound `31782e4c61337327f23eb9aa45ecd37fe0944ea0`, MIT,
  `PROJ/main.c:507-577`, `jo_demo/pcmsys.c:218-272`: pattern-only.
- libyaul-examples `66b648eb059bb8bb7392eac70821605a68205b85`,
  license unclear, `scsp-ponesound-pcm8/ponesound.c:63-92`: pattern-only;
  no source/binary copied.

## Gates actually run

Initial GREEN after implementation:

```text
strict C11/pedantic/Wall/Wextra/Werror direct compile + boot test: PASS
test_sound_cpu_command_ownership.py: PASS 2/2
```

The first expanded run exposed a test-harness-only Windows stack overflow
(`0xC00000FD`) because repeated 512-KiB compound-literal assignments allocated
multiple temporaries. Replacing them with bounded `memset` reuse made the test
green. Two inherited direct-EXE recipes then hit `/usr/bin/sh` unmatched-quote
launcher failures after successful compilation; Python subprocess launch
closed those invocation defects.

Final serialized DLL/MSYS-preflight gate:

```text
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 \
  verify-audio-sound-cpu-boot verify-soundtest-boot \
  verify-pcm-protocol verify-audio-protocol-v2
PASS (3.1 s)

audio sound-CPU strict C test: PASS
warned/generic command ownership: PASS 2/2
soundtest boot regression: PASS
PCM protocol v1 regression: PASS
audio protocol v2 regression: PASS
```

## Remaining blockers

- Sourceboot audio service/loader and feature-on/fail-closed integration.
- Real expanded sequence 00, reproducible `AUDIO.DAT`, and selective S64A
  package reads/residency within 512-KiB sound RAM.
- Final non-provisional BOB S64P audio identity from Task 22.
- Completion/accepted-command/package-generation status channel and live
  Task 15/17 MC68000 scheduler linkage.
- Target compiler/link, DRAM artifact, Ymir/hardware/manual and owner-heard
  music/SFX evidence. Task 23 owns the first audible claim.

## Fix round 1 — stopped-state ordering and retained diagnostics

Independent review of `7a1bb170` was SPEC/QUALITY FAIL, C0/I2/M0. The repaired
contract now encodes and tests the required order
`stage→SNDOFF→bounded stopped wait→512-KiB select/verify→clear/copy/mailbox→`
`barrier→SNDON→READY+heartbeat`. Soundtest's proof lifecycle invokes the same
512-KiB target adapter after its synchronous generic SNDOFF and returns the
named `512K_MODE_FAILED` result if verification fails.

Soundtest also owns a persistent `sm64_saturn_sound_cpu_yaul_result_t` and
publishes its completed-count, last-command, and raw OREG31 diagnostics. The
target generic adapter routes every completed command through a host-testable
recording seam. Tests prove raw `0x00` and `0xFF` are both retained with typed
`COMPLETED` status, while an unsupported command is rejected without changing
diagnostics; the byte is never cast to boolean.

TDD RED was observed before the repair: strict compilation failed on the
missing raw-completion seam and missing soundtest `set_512k_mode` member. Fresh
GREEN evidence:

```text
strict direct wrapper and soundtest C11/Werror binaries: PASS
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 \
  verify-audio-sound-cpu-boot verify-soundtest-boot \
  verify-pcm-protocol verify-audio-protocol-v2
PASS (3.0 s; ownership 2/2)
```

No sourceboot service/package/residency/completion, MC68000 semantic linkage,
target, Ymir, hardware, manual, audible, or performance gate is reclassified.

## Wave 2 — host completion/ack ABI

Wave 2 extends only the SH-2-side mailbox contract and transport. It reserves
the unused v2 header through `0x403f` plus a 32-entry, 16-byte completion ring
at `0x4240`. Existing control/SFX records and PLAY_REFRESH words are unchanged.
The transport returns pointer-free ring/cursor/opcode tickets, validates the
same two-lap arithmetic on completion, blocks ticket ABA reuse until terminal
retirement, requires an advertised completion capability, and rejects corrupt,
wrong-ring, wrong-opcode, wrong-lap, duplicate, zero-generation, or unsupported
status records without advancing the consumer.

The header exposes full 32-bit active/prepared package generations. Status
reads use a bounded stable-flags/write-in-progress check to avoid accepting a
split-field publication. The producer-side ABI reserves eight of 32 completion
slots for required acknowledgments and defines saturating 16-bit telemetry;
this is a contract only, not an MC68000 producer claim. ACCEPTED and COMMITTED
are application acknowledgments; FINISHED is explicitly not acceptance.

Review found that the command-ack record cannot also carry the complete
sequence/player/source identity required for asynchronous FINISHED events.
Wave 2 therefore does not permit FINISHED to mutate source policy. That needs
a separately reviewed tagged semantic-event record/version. Likewise, no
PACKAGE_PREPARE/COMMIT payload, producer linkage, source service, or real
residency transaction is claimed here.

TDD RED was observed twice: first on all absent layout/status/ticket APIs, then
on the absent capability, cursor-retirement, control-reserve, saturating
counter, stable-status, and typed PLAY_REFRESH protections. The typed
PLAY_REFRESH entry point treats zero as reserved, generation 1 as the initial
boot epoch, and `0xffff` as terminal; larger 32-bit generations are rejected
before any sound-RAM write rather than silently truncated.

Focused serialized GREEN:

```text
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 \
  verify-pcm-protocol verify-audio-protocol-v2 \
  verify-audio-completion-abi verify-pcm-transport \
  verify-audio-sound-cpu-boot verify-soundtest-boot
PASS (4.1 s; sound-CPU ownership 2/2)
```

The PCM transport/publication executables now use the same Python subprocess
launcher as the earlier soundtest/protocol repair, closing the inherited MSYS
quoted-path GUI-launch failure without changing test semantics. Independent
review remains required before the bounded slice is marked accepted. Target,
Ymir, hardware, manual, audible, and FPS evidence remain open.

## Wave 2 fix round 1

Independent rereview of `d23ec834` returned SPEC/QUALITY FAIL, C0/I5/M0. The
five findings were reproduced as direct RED cases before repair: wrong
same-class opcode retirement, legacy/no-ack reuse of a pending cursor, an
off/on/off status-publication race, live `0xffff` saturation wrap, and illegal
status/opcode pairs.

The pending table now retains the exact opcode and completion poll compares it
before either consumer publication or retirement. Unticketed compatibility
enqueue is explicitly no-ack but cannot bypass that reservation. Status flags
encode an advancing publication sequence (flags 1 stable, 3 writing, 5 next
stable); the reader requires matching stable-even values around the split
fields. A read observer changes 1 to 5 between generation halves and proves
the mixed `0x11114444` value is discarded before the coherent
`0x33334444` retry is returned.
Bits 2..15 are the bounded sequence, bit 1 is the in-progress parity bit, and
the future producer must not wrap all 16384 sequence values within one bounded
three-attempt read; Wave 2 still links no producer.

Both live command saturation paths now use the saturating helper. A single
closed matrix governs poll and required-capacity classification: FINISHED is
not a command ACK in this wave, DROPPED_SFX applies only to PLAY_REFRESH,
PREPARED/COMMITTED require their matching package opcodes, and generic
ACCEPTED cannot retire either package transaction command.

The fix remains host-only. No MC68000 producer, source service, package
transaction, target/Ymir/manual, audible, or performance claim is added.

Fresh serialized GREEN after the repair:

```text
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 \
  verify-pcm-protocol verify-audio-protocol-v2 \
  verify-audio-completion-abi verify-pcm-transport \
  verify-audio-sound-cpu-boot verify-soundtest-boot
PASS (4.1 s; sound-CPU ownership 2/2)
```
