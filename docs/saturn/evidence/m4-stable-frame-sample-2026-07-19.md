# M4 stable frame sample — 2026-07-19

This is a Ymir software-VDP capture of the Castle Area 1 source scene after
the persistent command-lowering cache and Yaul input-polarity correction.
Ymir is an emulator/debugger; this is not retail Saturn timing evidence.

## Probe contract

- Symbol: `saturn_frame_sample`
- Address: `0x060999D4` for this build
- Size: 42 bytes, 21 big-endian `uint16_t` fields
- Sequence: `130` (even, complete)
- Magic/version/size: `0x4653 / 1 / 42`
- Mario state: idle, animation frame `2`, world position `(-1023, 0, 1152)`

## Complete frame budget

| phase | FRT ticks |
|---|---:|
| update | 2,296 |
| sort | 5,041 |
| command | 569 |
| VDP wait | 2,634 |
| VBlank | 3,495 |
| render total | 10,540 |
| loop total | 14,035 |

At the existing `FRT_TICKS_PER_SECOND_X10 = 2,095,500` calibration this is
`14.93` loop FPS. The prior rolling profile read could observe a partially
updated frame and reported misleadingly small phase values; this record is
published after `sm64_saturn_frame_profile_loop_total()` and uses an odd/even
sequence guard.

## Image and cache evidence

- Screenshot: `screenshots/ymir-m4-stable-frame-sample-neutral-2026-07-19.png`
- Report: `reports/ymir-m4-stable-frame-sample-neutral-2026-07-19.json`
- PNG SHA-256: `3d2d56d9ff48fa2a955d40e5474b3b01bed7210639e117e08be022bf534f71d6`
- The neutral command-cache comparison has the same PNG hash before and after
  the CPU command-lowering skip. VDP1 is still re-armed/uploaded every frame.

The companion right-input capture is retained as a diagnostic image, but its
post-run frame is neutral at the spawn position. A future pulse-time probe
must sample while the held input is active before it can be accepted as a
movement-position proof.
