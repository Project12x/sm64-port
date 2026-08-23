# The frame, decomposed and settled — VDP1 emission is the block

**Date:** 2026-08-23 · **Build:** shipped `id-2ff8718cc26fed8d` · 29 intervals.

This closes the "measure `drain_master` before building anything" action. It
needed no new instrumentation: the answer was already in the captures, and two
of my earlier inferences were wrong.

## The phase definitions, read from the source

`saturn_render_overlap_phase.c:96-113` defines the cadence phases exactly:

- `slave_work` = `retirement_vblank − notification_vblank`
- `master_finalize` = `terminal_vblank − retirement_vblank`
- `construction` = `(notification_vblank − construction_begin_vblank) + finalization`

The last one is the trap: **`construction` already includes `finalization`.** It
is not a separate span. So the pre-notification span is
`construction − finalization` = 7.345 − 5.000 = **2.345 VB**, and there is no
mysterious extra 4.4 VB block. My earlier claim that ~4.4 VB of construction
was post-notify `drain_master` was arithmetic on a misunderstood definition.

## The frame, per frame (10.483 VB)

| span | VB | who works | who idles |
|---|---:|---|---|
| pre-notification | **2.345** | master | slave |
| notify → slave retire (`slave_work`) | **2.897** | slave (graph + actors); master advances simulation | — |
| slave retire → terminal (`master_finalize`) | **5.000** | master alone | **slave** |
| **total** | **10.242** ≈ 10.483 | | |

The residual is rounding across 29 intervals. This accounts for the whole frame,
and it lines up with the independent idle measurement: the slave is 50.75% idle
= **5.687 VB/frame**, against a 5.000 VB window in which it has nothing to do.

## `drain_master` does nothing

From the queue telemetry (`latest_coherent_queue`, generation 30):

- `qm` (master claims per callback) = **[0, 0, 0, 0]**
- `qs` (slave claims per callback) = **[1, 1, 1, 1]**
- `master_failures` 0, `slave_failures` 0

**The slave already drains the entire render-job graph. The master claims zero
jobs.** The graph is only four jobs (world admit/lower, actor admit/lower), so
this is not a sampling fluke — there is nothing left for the master to take.

Consequences:

- **The "let master and slave drain concurrently" plan is dead.** There is no
  master drain to overlap. The sequential lifecycle ordering costs nothing.
- **The `demo_prepare_position_owners` shared-array race is therefore not a
  blocker on anything today.** It remains a real hazard if concurrency is ever
  introduced there, and is worth a comment, but it blocks no current work.
- **`master_finalize`'s 5.000 VB is essentially all `finalize`** — the terrain
  emit loop, Mario emit, generic actor emit, `vdp1_backend_finish` and
  `link_depth_bins` (`saturn_demo_render.c:5118-5147`).

## What this makes the single biggest lever

**VDP1 command emission: 5.000 VB/frame — 47.7% of the frame — executed by the
master alone while the slave sits idle.** Nothing else in the frame is close.

Command mix, measured: actors **53.98%**, textures/terrain the balance
(`present.commands.actor_share_of_total`; an earlier capture put terrain at
~40%).

This also reprices the VDP2 ground migration upward for the second time, and
for a better reason than before. VDP2 does not merely save VDP1 *fill* time
(measurement already showed VDP1 is not the frame limiter) or CPU-side
transform. Ground that becomes an RBG0 rotation plane is removed from **both**
of the two master-only spans:

- it is not admitted → cuts into the 2.345 VB pre-notification span, of which
  `spatial_admit` is 91%;
- it is not emitted → cuts into the 5.000 VB finalize span, where terrain is
  roughly 40% of commands.

Those are the only two spans in the frame where the master works alone.
