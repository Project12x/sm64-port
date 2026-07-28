# Task 5b partition-cache A/B — 2026-07-28

The demo renderer now reads the immutable VDP1 partition layout once per
frame and passes that snapshot through the terrain emission loop. The previous
implementation queried the same layout once per primitive. This is a
master-side hot-path optimization; the slave transform contract and command
ownership are unchanged.

Fresh same-commit `-r2048/-poly0` captures:

| profile | frames | render ms/frame | slave share | jobs | timeouts |
| --- | ---: | ---: | ---: | ---: | ---: |
| serial | 786 | 1.8005 | 0% | 0 | 0 |
| dual | 803 | 17.6779 | 20.1632% | 803 | 0 |

The dual cumulative render interval is 47,682,152 FRT ticks, versus
47,971,375 in the preceding cancel-poll baseline (about 0.6% lower after the
normal capture-window difference). The utilization gate is still open: the
dual worker remains well below the required 50% share. The paired route
comparator passes with identical checkpoint SHA-256
`d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`, zero
faults, zero capacity rejects, and zero renderer-counter deltas.

This is performance diagnostic evidence, not a gallery milestone.

Paired artifacts:

- Dual: `task5b-cachepart-dual-2026-07-28.{json,png}`
- Serial: `task5b-cachepart-serial-2026-07-28.{json,png}`
- Comparator: `task5b-cachepart-serial-vs-dual-2026-07-28.json`
