# Task 7 utilization report — BOB route, 2026-07-28

Paired sourceboot captures use the same frozen `bob_parity_v1` route, `-r2048`,
`-poly0`, `--dram-cart`, fresh ELF/CUE pairs, and the 3,600 + 3,600 emulator
frame window. FRT conversion is 3,359 ticks/ms (`CPU_FRT_NTSC_320_8_COUNT_1MS`).
The JSON beside this file is the machine-readable report.

The paired route comparator passes: both captures reach replay tick 600 and
global timer 601 with checkpoint SHA-256
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`, zero
faults/capacity rejects, and zero renderer-counter deltas.

| profile | sim ms/tick | render ms/frame | slave busy share | jobs | timeouts | faults |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| serial | 13.6373 | 2.5638 | 0.00% | 0 | 0 | 0 |
| dual | 6.8872 | 18.0743 | 21.1977% | 801 | 0 | 0 |

Fixed residency/configuration evidence is also present in both probes:

- VDP1: 445,984 bytes reserved; final command counts 573 (serial) and 577 (dual).
- VDP2: 262,592 bytes reserved; live display mask `10` = NBG1 sky bitmap + NBG3 debug text.

The A/B route checkpoint remains deterministic and the absolute-rate checkbox is
already recorded in the sprint plan. The utilization gate does **not** pass:
the dual worker reaches only 21.1977% of measured cumulative render time against
the required 50%. The dual path is therefore diagnostic, not a completion claim;
the next performance investigation is the worker's serial transform/shared-bus
and emission cost. Fresh screenshots are paired under `evidence/screenshots/` but
remain unpromoted until owner visual confirmation.
