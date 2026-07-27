# Task 1 SH-2 Q16 kernel target-vector evidence (2026-07-27)

The sourceboot image was built from the current `saturn/bootstrap` worktree
and executed in Ymir headless. `sourceboot_q16_kernel_probe` was resolved from
the freshly linked symbol table at `0x060D3868`; it uses the cache-through
alias and is therefore debugger-readable.

The bounded capture used the USA BIOS, 240 BIOS frames, a handoff yield, and
900 post-handoff frames. Headless Ymir reports no DRAM cart, so sourceboot
subsequently enters its expected cart-failure path; the probe deliberately
runs before that gate.

`evidence/reports/task1-q16-kernel-probe-2026-07-27.json` records these
big-endian fields:

| Field | Observed | Expected |
|---|---:|---:|
| magic | `0x51313631` (`Q161`) | `0x51313631` |
| version | `1` | `1` |
| `1.5 * 0.5` Q16 | `0x0000C000` | `0x0000C000` |
| `-1.0 * 0.5` Q16 | `0xFFFF8000` | `0xFFFF8000` |
| `1.5 / 0.5` Q16 | `0x00030000` | `0x00030000` |
| status | `0` | `0` |

The sourceboot `verify` target also runs
`tools/saturn/verify_q16_sh2_disassembly.py`, which proves linked code orders
DIVU launch before both `dmuls.l`/`xtrct` operations and defers the quotient
load until afterward.
