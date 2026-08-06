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
pre-staged bytes validated -> SCSP 512-KiB mode -> generic SNDOFF
-> bounded stopped poll -> clear/copy/mailbox owner callbacks
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
