# Actor-emit offload — paper analysis before any code

**Date:** 2026-08-23 · Target: the 5.000 VB/frame (47.7%) `master_finalize`
span, executed by the master alone while the slave is idle.

Analysis only. No code changed.

## Headline: depth-bin ordering is *not* the blocker

That was the stated risk, and it does not survive contact with the code.

`sm64_saturn_vdp1_backend_link_depth_bins` (`saturn_vdp1_backend.h:195`) runs
**after all emission is complete**, over one contiguous arena range. Its
contract is "far-to-near by descending bin, **stable in original producer order
within a bin**". Mechanically (pass 2) it walks the live range *backwards* and
prepends each command onto its bin's intrusive chain, so within a bin the chain
ends up in **ascending arena index order**. Producer order *is* arena index
order — nothing else.

Emission order today is terrain → Mario → generic actors
(`saturn_demo_render.c:5118-5137`), so actor commands already occupy the
**highest** indices.

**Therefore: any design that keeps the actor command block at higher arena
indices than terrain and Mario reproduces today's ordering exactly.** Depth bins
are per-command tags validated independently in pass 1; disjoint index ranges
never interact. Concurrency does not perturb the sort, because the sort does not
run until both producers are done.

That removes the risk that motivated this analysis. The real obstacles are
elsewhere, and they are the same class S1.11 already solved once.

## The actual blockers

### H1 — the command arena is a shared bump allocator, and terrain's count is not predictable

`sm64_saturn_command_arena_t` carries one `cursor` / `live_count` /
`previous_end`, bumped per emitted command. Two producers cannot share it.

Worse for the obvious fix: the slave cannot be told where to start, because the
terrain command count is **not** knowable in advance. The terrain loop skips
results (`if (result->primitive_id >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;`)
and `demo_emit_terrain_result` is not guaranteed one command per result. So a
simple "terrain gets [head, X), actors get [X, end)" split cannot be computed
before emission.

Note what *is* already known before emission: `essential_actor_commands`
(Mario + `generic_actor_output_count`) is computed at
`saturn_demo_render.c:5084-5087`, and the arena **already reserves a tail for it**
via `sm64_saturn_command_arena_budget_before_tail`. The reservation exists; only
the placement is sequential.

### H2 — `gouraud_bank->used` is read by actor emit while terrain emit writes it

Actor emit validates `table_index >= transaction->gouraud_bank->used`
(`:4710`) — a **live read** of a field terrain emit advances as it allocates its
own optional shading. Concurrently that is a stale-read hazard on cached HWRAM.
Fix is trivial: snapshot the actor-visible upper bound into the actor context
before emission, since the actor range is already fully reserved at `:5099`
before any emission begins.

### H3 — Gouraud staging false-shares at the reservation boundary

`sm64_saturn_gouraud_table_t` is `uint16_t colors[4]` = **8 bytes**, so **two
entries share every 16-byte SH-2 cache line**. The actor range
`[generic_actor_gouraud_first, +count)` and terrain's allocations from `used`
onward are *adjacent*, so the boundary pair straddles one line. Two CPUs writing
it would each discard the other's half.

Fix: round `generic_actor_gouraud_first` and the reserved count up to even
indices. Costs at most one wasted 8-byte entry. This is the identical hazard to
the lane workspace fixed in `eddd2d38`.

### H4 — shared profile counters inside the emit path

`transaction->profile->actor_flat_replace++` and siblings are written during
emit. Same shared-accumulator problem S1.11 solved with an uncached per-phase
block folded by the master afterwards. Pattern already exists; reuse it.

### H5 — the scheduling gap

The slave **has already retired** by the time finalize runs — retirement is what
ends the `slave_work` span and starts `master_finalize`. Emitting actor commands
on the slave therefore needs a **third dispatch**, after
`demo_reserve_mario_gouraud` and the actor gouraud reservation at `:5099`
(because `generic_actor_gouraud_first` is an input). The auxiliary-dispatch hook
from S1.11 supports this; the constraint is only that at most one dispatch is
outstanding at a time, which this ordering satisfies.

### H6 — an explicit invariant is being broken

`saturn_demo_render.c:5117` states: *"The master owns all VDP1 lowering."* This
is a deliberate design statement, not an accident, and the existing slave/master
split is built the other way round — the slave produces terrain *results* and
the master lowers them into commands. Breaking it is the substance of this
change and should be recorded as such, not slipped in.

## Candidate designs

**D1 — slave writes a staging buffer, master memcpys into the arena tail.**
Slave computes actor commands into a dedicated buffer; master appends after
terrain and Mario. Ordering preserved by construction (H-headline). No shared
cursor, no gap, no arena partitioning. Cost: one copy of
`generic_actor_output_count × sizeof(vdp1_cmdt_t)` ≈ 310 × 32 B ≈ **10 KB per
frame**, serial on the master. Against a 5.000 VB span (~2.2 M cycles) a 10 KB
copy is small, but it is not free and it is on the critical path.

**D2 — slave writes directly into a fixed arena tail; master compacts.**
Actor block placed at `[capacity_end − A, capacity_end)` so the slave's start
index is known. Terrain grows from the head under its existing budget, so
overlap is already prevented. Leaves a **gap** when terrain emits fewer than its
budget, which pass 1 of `link_depth_bins` would reject (it validates every tag
across a contiguous range), so the master must `memmove` the actor block down.
Same copy cost as D1 with more coupling to arena internals. **No advantage over
D1.**

**D3 — two arenas, link across both.** Rejected: `link_depth_bins` validates and
walks a single contiguous range with `end` doubling as chain terminator and
sentinel. Supporting two ranges means changing the linker's contract, which is
pinned byte-for-byte by `tools/saturn/vdp1_painter_chain_test.c`.

**Recommendation: D1.**

## Expected value

Measured mix: **574.4 VDP1 commands per present**, actors **53.98%** (310.1).
If emission cost tracks command count, the 5.000 VB span splits roughly 2.3 VB
actors / 2.7 VB terrain+Mario. Overlapping them gives
`max(2.7, 2.3) + copy ≈ 2.9 VB`, saving **~2 VB/frame**: 10.483 → ~8.5 VB,
i.e. **5.72 → ~7.0 FPS (+23%)**.

Two caveats on that estimate. It assumes emission cost is proportional to
command count, which is unverified — actor commands involve Gouraud table
construction and a bundle re-resolve per instance, so they may well be *more*
expensive each than terrain's, which would improve the split rather than worsen
it. And it ignores bus contention between two CPUs writing command memory, which
works the other way.

## Recommended sequence

1. Snapshot `gouraud_bank->used` into the actor context (H2) and even-align the
   Gouraud reservation (H3). Both are small, independently correct, and
   verifiable by frame identity with **no** concurrency introduced yet.
2. Move the actor emit's profile counters into the existing uncached phase block
   (H4). Again no concurrency, frame-identity verifiable.
3. Only then introduce the third dispatch and D1's staging buffer, behind a
   feature flag, validated the way S1.11 was: **caches-on frame identity plus
   caches-on cadence**, since the default harness cannot see coherency defects.

Steps 1–2 are the S1.11 playbook and carry little risk. Step 3 is where the
invariant at `:5117` changes and where the win is.
