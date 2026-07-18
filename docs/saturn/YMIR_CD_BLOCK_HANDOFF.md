# Ymir CD-block handoff

The current BIOS-backed Ymir capture stops before the Saturn disc reaches the
SM64 Saturn image. This is an emulator limitation, not a hardware-test result.

## Reproduced state

- Ymir fork: `Project12x/Ymir` / `StrikerX3/Ymir`
- inspected fork commit: `6efc5324943c27f4db7a1b7c8bcf90f51459b12e`
- license: GPL-3.0 (`../ymir-agent/LICENSE`)
- BIOS: USA IPL supplied by the user; SHA-256 is recorded in the capture
  evidence
- observed diagnostic: `Get copy error command is unimplemented`
- observed telemetry: 120 zero bytes at `0x06010000`

## Source locations inspected

In `libs/ymir-core/src/ymir/hw/cdblock/cdblock.cpp`, the command dispatcher
currently comments out:

```cpp
// case 0x65: CmdCopySectorData(); break;
// case 0x66: CmdMoveSectorData(); break;
```

The corresponding methods exist near the end of the same file, but are
explicitly TODO/unimplemented and return `kHIRQ_ECPY`. The USA BIOS reaches the
copy-error path (`0x67`) while trying to boot the disc, so the Saturn program
never executes.

## CD-block patch status

The minimum CD-block patch was implemented and pushed to the linked Ymir fork
as commit `9da9c76b` on `project12x/agent-debug-v0`. It enables `0x65`/`0x66`,
routes copied buffers through the filter chain, removes source sectors for
move, and returns a zero-error response for `0x67`.

The rebuilt fork no longer reports the false “Get copy error command is
unimplemented” diagnostic. The USA BIOS still stops earlier on unhandled SMPC
on-chip register accesses, so a second emulator patch is required before the
Saturn image can be expected to execute.

## Original minimum patch shape

Implement the CD-block sector-copy and sector-move state machine in the Ymir
fork, preserving its GPL-3.0 notices and corresponding-source obligations:

1. enable dispatcher cases `0x65` and `0x66`;
2. reserve a destination partition buffer and schedule the asynchronous copy
   or move using the existing partition-manager/transfer primitives;
3. report `kHIRQ_CMOK`/`kHIRQ_ECPY` consistently with the Saturn command
   protocol; and
4. add a focused CD-block test, then rerun `capture_hwtest.py` until the
   `SAT0` magic is present at `0x06010000`.

No Ymir source is copied into this repository by this handoff. The project is
authorized to use GPL code, provided the fork retains its license, notices,
and corresponding source.
