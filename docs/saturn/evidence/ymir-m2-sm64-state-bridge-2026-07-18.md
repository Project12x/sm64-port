# Original SM64 input-state bridge on Saturn

This is BIOS-backed Ymir evidence, not retail-hardware timing proof. The build
uses the locally supplied US SM64 ROM only to generate the existing source
actor data; no derived Nintendo pixels or ROM data are added by this note.

## Source path executed

- Yaul digital/3D pad → `src/port/saturn/controller/controller_saturn.c`
- existing `OSContPad` and `struct Controller`
- original `adjust_analog_stick()` in `src/game/game_init.c`
- original `update_mario_button_inputs()` and
  `update_mario_joystick_inputs()` in `src/game/mario.c`
- original `atan2s()` and `gArctanTable` in `src/engine/math_util.c`

The target's source-annotated SH-2 disassembly retains each of those routines
inside the live frame loop. This is a staged link of original SM64 code, not a
reimplementation of joystick intent.

## Duration-aware automated result

The accepted input route holds Saturn Up for 120 emulated frames and observes
60 frames before pausing. The shorter three-frame observation produced the
neutral framebuffer because no completed SMPC collection sampled the hold in
that interval. The longer bounded route records:

| State | Build-specific address/offset | Big-endian bytes | Value |
|---|---:|---:|---:|
| `OSContPad.stick_y` | `_source_pad + 3` | `50` | 80 |
| `Controller.rawStickY` | `_source_controller + 2` | `00 50` | 80 |
| `Controller.stickY` | `_source_controller + 8` | `42 80 00 00` | 64.0 |
| `Controller.stickMag` | `_source_controller + 12` | `42 80 00 00` | 64.0 |
| `MarioState.input` | `_source_mario_state + 2` | `00 01` | `INPUT_NONZERO_ANALOG` |
| `MarioState.intendedMag` | `_source_mario_state + 0x20` | `42 00 00 00` | 32.0 |
| `MarioState.intendedYaw` | `_source_mario_state + 0x24` | `80 00` | `0x8000` |

The addresses come from this build's ELF symbols and are intentionally not a
runtime ABI. The complete raw window, input duration, frame sequence, hashes,
and emulator diagnostics are in
[`ymir-m2-sm64-state-up-2026-07-18.json`](ymir-m2-sm64-state-up-2026-07-18.json).

![Neutral source-state frame](screenshots/ymir-m2-sm64-state-neutral-2026-07-18.png)

![Source-owned Up direction](screenshots/ymir-m2-sm64-state-up-2026-07-18.png)

The actor still uses the diagnostic renderer and its known incorrect face
texture mapping. The next port boundary is original collision/action movement,
then original camera/graph output into the shared Castle texture IR.
