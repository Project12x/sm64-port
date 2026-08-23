# S1.11 — slave activation: verified blocker analysis and design

**Date:** 2026-08-23 · **Basis:** post-T5 tree, sealed baseline
`id-3481ef86ab8e531e` (5.3870 mean) and `id-b227747c65db2929` (5.4037 mean).

S0.1 measured **master 0.00% idle, slave 74.04% idle** on the closure build.
This document records what actually blocks spending that idle, verified against
the code rather than assumed, and picks the design.

---

## 1. The structural blocker is the lifecycle order, not the queue

`saturn_render_lifecycle.c:34-72` runs, per frame:

```
prepare_publish  ->  notify  ->  [poll] slave_retired?  ->  drain_master  ->  finalize
```

`demo_generic_actor_prepare` is called from inside `prepare_publish`
(`saturn_demo_render.c:4619`); `sm64_saturn_render_job_runtime_notify()` is at
`:4710`. **The slave is asleep for the entire generic-actor phase.**

The consequence that reframes the whole task: at `:65` the master *blocks*
polling `slave_retired()` before it may run `drain_master`. So moving actor work
onto the slave **buys nothing on its own** — the master would simply wait longer
at the same barrier. Any real win requires the notify to move *earlier*, so the
slave's actor work overlaps the master's remaining terrain/Mario prepare.

This was not visible from the idle measurement alone, and going straight to an
implementation would have produced a zero-gain change.

## 2. Coherency blockers — three of five were already handled

Verified by reading, not assumed:

| # | Concern | Verdict |
|---|---|---|
| 1 | Actor instance queue state | **Already safe.** `saturn_actor_instance_queue.c:23` routes every access through `actor_queue_uncached()` (`CPU_CACHE_THROUGH`). `drain_master`/`drain_slave` are symmetric and claim atomically. |
| 2 | Bundle residency `lane_claim[]` — adjacent bytes, one per CPU | **Already safe.** `source_scene_owner` is `SOURCE_SCENE_UNCACHED` (`source_scene_bundle.c:64`), so the two lanes' claim bytes are not cached and cannot false-share. |
| 3 | Per-lane meshlet workspace | **Was a real hazard; fixed alongside this document.** `s_actor_phase_workspace` lives in `.lwram_bss` (cached alias, origin `0x00200000`) and was `__attribute__((aligned(4)))`. The bundle's `workspace_lane_stride` is 1456 = 16×91, already a cache-line multiple, so **only the base alignment was wrong**: lane 1 begins 1456 bytes past a merely 4-aligned base, putting the tail of lane 0 and the head of lane 1 in the same 16-byte SH-2 cache line. Now aligned to 16. |
| 4 | `transaction->generic_actor_gouraud_count` | **Open.** A shared mutable accumulator incremented inside the process callback (`saturn_demo_render.c:3191-3194`), in cached HWRAM. |
| 5 | `stats->demo_actor_meshlets_{tested,admitted,culled}` and `demo_actor_positions_admitted` | **Open.** Four shared read-modify-write counters (`saturn_actor_meshlets.c:691,881-884`) — the only profile fields the meshlet path writes. |

## 3. The design: move the whole actor phase to the slave, not a split drain

The obvious approach — have master and slave both drain the shared actor queue —
requires solving blockers 4 and 5 as genuinely concurrent accumulators.

**Giving the entire generic-actor phase to the slave is both simpler and worth
more.** Only one CPU ever touches actor state, so:

- blockers 4 and 5 stop being races and become a **sequenced handoff** — the
  slave accumulates into its own block, the master folds it in after
  `slave_retired()`, which is already a barrier;
- the workspace hazard disappears entirely (the slave uses lane 1, the master
  never touches the actor workspace);
- the master sheds the measured actor cost: pose evaluation 4.977% + depth walk
  3.359% + `demo_generic_actor_emit` 2.127% ≈ **10.5% of a 100%-saturated
  master**, onto a CPU that is 74% idle.

Required change, in order:

1. **(landed)** cache-line align the lane workspace.
2. Put the slave's accumulators (gouraud count + the four meshlet counters) in
   `CPU_CACHE_THROUGH` memory; the master folds them after retirement.
3. Publish the actor descriptor list to the slave through the existing
   `demo_render_queue_context_open` mechanism — the established pattern in this
   codebase for handing a context across CPUs — rather than letting the slave
   read `s_demo_render_transaction` (cached HWRAM, `0x060dcf20`, master-owned).
4. Split `demo_generic_actor_prepare` into descriptor-build (master, cheap: one
   `u32` per instance) and process (slave), and move the notify to between them
   so the slave's actor work overlaps the master's terrain/Mario prepare.

Step 4 is the one that actually converts idle into frame rate; steps 1–3 only
make it safe.

## 4. Validation constraint that must not be skipped

`capture_sourceboot_throughput.py` runs with **`sh2_cache_emulation: false`**,
and every accepted baseline is caches-off. **The default harness therefore
cannot observe any of the coherency bugs above.** A green cadence run is not
evidence of correctness for this work.

Validation must be correctness *by construction* — every shared mutable byte
through `CPU_CACHE_THROUGH`, which is what the rest of this codebase already
does — plus a `--sh2-cache` run as a separate basis. Caches-on costs about
−7.5% FPS and is never a correction to the caches-off number.
