# Independent source review — PCM68K heartbeat image

**Date:** 2026-08-04
**Scope:** Uncommitted Task 2 heartbeat-image lane on top of `f782bdec`
**Initial verdict:** **NO-GO** until the stack reservation is made explicit and enforced
**Re-review verdict:** **GO (source scope)**; cross-image/target evidence remains open

## Re-review — 2026-08-04

The blocking stack defect is repaired. `linker.ld` now reserves
`0x3C00-0x3FFB` with `__stack_bottom = 0x3C00` and
`__stack_top = 0x3FFC`, and asserts `__driver_end <= __stack_bottom`.
The independent verifier enforces the same ordering and has a mutation that
places driver content at `0x3C02`; it fails closed. The verifier now also
requires ELF entry `0x0400`, verifies that the reset-vector PC equals that
entry, and verifies that the initial SP equals the map's `__stack_top`.

Fresh evidence:

```text
.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py Pcm68kImageContractTests
Ran 11 tests ... OK

heartbeat host C11/Werror fixture: PASS
protocol host C11/Werror fixture: PASS
```

No new runtime, isolation, or provenance issue was found. The source lane is
approved. The unavailable `m68keb-elf` build, real ELF/BIN/MAP verification,
soundtest CUE, Ymir heartbeat, and audibility gates remain explicitly open.

Before committing, correct the plan/evidence text that still says the focused
suite ran **seven** tests; the fresh reviewed count is **eleven**. This is a
documentation reconciliation amendment, not a source-code no-go.

## Finding

### [P1, resolved on re-review] The 16 KiB image gate permitted immediate runtime stack corruption

`src/port/saturn/audio68k/linker.ld` fixes `__stack_top` at `0x3FFC`, but its
only collision assertion is `__driver_end <= __stack_top`. The Python verifier
implements the same inclusive condition. This permits a loadable image ending
exactly at `0x3FFC`; after reset, the first `jsr pcm68k_main` decrements `%sp`
and writes its return address into `0x3FF8-0x3FFB`, which is inside that accepted
image. Any stack frame or nested call grows the overwrite farther downward.

The gap is executable in the current verifier: a fixture with
`loaded_end == __driver_end == __stack_top == 0x3FFC` is accepted and returns
success. The fixed mailbox and bank remain non-overlapping, but the driver is
not actually protected from its own stack.

**Required repair:** reserve an explicit bounded stack region below `0x4000`
(for example, define `__stack_bottom` separately from `__stack_top`), require
`__driver_end <= __stack_bottom` in the linker and verifier, and add a mutation
that places the image one byte/word into the reserved stack and must fail. The
chosen reservation should cover the compiled heartbeat call depth and leave
room for the planned command consumer rather than merely four bytes for the
first return address.

## Non-blocking verification gap — resolved on re-review

The source linker fixes `ENTRY(_start)`, places `.text` at `0x0400`, and asserts
that `.vectors` is exactly `0x400` bytes. However,
`tools/saturn/verify_pcm68k_image.py` parses but does not validate the ELF entry
field or reset-vector contents, and its synthetic ELF fixtures use an arbitrary
entry value without testing rejection. Add entry/vector checks when real ELF
artifacts become available so the independent artifact gate covers the stated
vector/reset contract instead of relying only on linker-script intent.

## Verified good

- The vector source contains exactly 256 longwords: initial SP, `_start`, and
  254 fault vectors; `_start` is placed at `0x0400` by the linker.
- Startup clears exactly `[__bss_start, __bss_end)` byte-by-byte and falls into
  `_fault` if `pcm68k_main` returns.
- Mailbox offsets are big-endian and remain before the command ring; driver,
  mailbox, reserve, and PCM-bank constants do not overlap.
- The real publisher writes magic `0x5036`, version `1`, BOOTING, then READY and
  a wrapping 16-bit heartbeat. The host fixture covers `0xFFFF -> 0 -> 1`.
- No runtime SCSP register access, sourceboot integration, renderer integration,
  CUE, or audibility claim is present. `scsp_regs.h` contains declarations only.
- Provenance is accurate: the inspected PoneSound checkout is pinned at
  `31782e4c61337327f23eb9aa45ecd37fe0944ea0`, the retained license is MIT, and
  the close-port boundary is limited to the vector/reset/linker shape.
- The missing guarded `m68keb-elf` toolchain is correctly left uncredited.

## Evidence run

```text
.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py Pcm68kImageContractTests
Ran 7 tests ... OK

gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/audio \
  -Isrc/port/saturn/audio68k tools/saturn/pcm68k_heartbeat_test.c \
  src/port/saturn/audio68k/main.c -o .../review-pcm68k-heartbeat.exe
PASS

gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/audio \
  tools/saturn/pcm_protocol_test.c -o .../review-pcm-protocol.exe
PASS

Adversarial stack fixture:
loaded_end = driver_end = stack_top = 0x3FFC
Actual result: ACCEPTED (contract defect reproduced)
```

No cross-compiled ELF/BIN/MAP or target/Ymir result is credited.
