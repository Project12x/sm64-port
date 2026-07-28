# Task 6 view-distance sweep — 2026-07-28

Fresh dual-worker route captures from the partition-cache commit, all using
the BIOS handoff-yield recipe, `--dram-cart`, and the same `-poly0` profile.
Each report reaches `replay_ticks=600` with `fault_flags=0`,
`slave_timeouts=0`, and no command-capacity rejects.

| view radius | render ms/frame | slave share | frame serial / 3,600-frame window* | route VDP1 emissions |
| ---: | ---: | ---: | ---: | ---: |
| 2,048 | 17.6779 | 20.1632% | 803 | 340,400 |
| 4,096 | 18.2931 | 20.7460% | 799 | 355,803 |
| 6,000 | 17.0403 | 24.1155% | 795 | 372,593 |

\* The frame-serial column is the absolute completed-submit count in the
same 3,600-emulator-frame capture window. It is retained as the comparable
rate signal here; the final 15/12 FPS gate still requires the paired
frame-serial differencing protocol and is not claimed by this sweep.

The sweep does not produce a monotonic FPS lever in this emulator window:
the 6,000-radius run is faster than the 2,048 and 4,096 runs despite more
emitted geometry. The result is therefore a measured non-lever for cadence
selection, not a claim that the larger radius is cheaper. The ≥50% slave-share
gate remains open. Screenshots are retained as diagnostic visual evidence and
are not gallery-promoted.

Paired capture artifacts:

- `task5b-cachepart-dual-2026-07-28.{json,png}` (2,048 baseline)
- `task6-view4096-2026-07-28.{json,png}`
- `task6-view6000-2026-07-28.{json,png}`
