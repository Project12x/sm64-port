# Ymir headless BIOS input experiment — 2026-07-17

The Ymir fork now connects a deterministic control pad and exposes
`input.pulse`. A Right pulse (`0x4000`) moved the USA BIOS language selection
from Japanese to English; an A pulse (`0x0400`) committed the selection and
produced the BIOS transition frame.

Screenshots:

- [English selected](screenshots/ymir-input-pulse-right-2026-07-17.png), frame
  hash `9f5d8383a01677c68edd540e263c5162`.
- [After A confirmation](screenshots/ymir-input-pulse-a-2026-07-17.png), frame
  hash `3d1b5414fb4592a610b61ae533068a80`.

After the transition, a 1,200-frame run still left the `SAT0` region at
`0x06010000` zeroed and stopped in the BIOS polling loop. This removes the
first-boot language prompt as the blocker; the remaining failure is in the
BIOS/CD handoff path. It remains emulator evidence only and does not replace
retail hardware validation.

The controller/debug changes are in Ymir fork commit `ad2e05fb`.
