# Sprint 2 Task T2.14 - why there are no shadows, and the cull funnel nobody had ever read

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `290cc8e3`.
- Task: diagnose the owner-reported absence of shadows, and take the
  per-triangle cull attribution that has never been measured on the real route.
- Target measured: `id-a61d5203793986e7`, the T2.13 product build.
  ELF `bc49eafa8bc9695ec708a7ffa24884be7084be10b0ad7a1bd1ca87bdbe6ccbad`.
  Cadence basis: **5.3538 FPS / 11.2069 VB per frame** (T2.13, `summarize_cadence`).
- **No build was made.** Every number below comes from peeking or single-stepping
  the shipped product ELF. `SATURN_DIAGNOSTIC_MODE` was not needed and
  `tools/saturn/profiles/sourceboot-bob-demo-v1.json` is untouched
  (`diagnostic_mode` still `0`, `git diff` empty).
- Owner steers received mid-task and applied: shadows are **not** to be
  restored; the eventual direction is *"a generic painted sprite under mario
  that doesnt actually have to follow light cues"*; the deliverable is a number,
  not a verdict.

---

## Headline

**Verdict (a): computed and discarded.** Not ambiguous, not a close call, and
the death point is not a per-triangle cull.

**The entire source display list is thrown away every frame - terrain, Mario,
HUD and shadows alike.** Under `SATURN_DEMO_PATH=1` the port suppresses display
submission around the whole of `game_loop_one_iteration()`, so
`exec_display_list()` is never called. What you see on screen is built by a
completely separate renderer (`saturn_demo_render.c`) that has **no shadow
concept at all** - the string `shadow` does not appear in that file.

Three independent runtime witnesses, one 50-second capture, product build:

| Witness | Measured | Meaning |
| --- | ---: | --- |
| `sState.submitted_tasks` | **0.000 / frame** | `exec_display_list()` is never even entered |
| `profile.command_count` | **0.000 / frame** | not one display-list command was ever decoded |
| `sState.scene_graph_walks` | **1.000 / frame** | the geo walk *does* run - the shadow really is built |

So the shadow is constructed in full, every frame, and then discarded together
with the display list that carries it. T2.13's finding that
`calculate_vertex_xyz` executes was correct; what was missing is that nothing
downstream ever reads its output.

**And the brief's stated premise was wrong, which is worth recording because it
would have misdirected the whole task.** The brief says the F3D frontend "*is*
wired on this route: `sourceboot/main.c:2153`". Line 2153 is inside the
`#else` arm. The shipped profile sets `demo_path: 1`, so the live line is
`main.c:2151` - `sm64_saturn_source_runtime_configure(NULL, NULL)` - with the
comment already explaining why: *"do not submit every source display list to
the interpreted frontend as well: doing both doubled the render work"*.

**Now the number the owner asked for, and it is not the one hoped for.**
Gating the shadow path is worth **+0.01 to +0.03 FPS** (0.02-0.06 VB/frame)
against 5.3538 / 11.2069. That is **0.2%-0.5%**, not "several FPS". Section 4
derives it and section 4.4 says plainly why the expectation was too high: **the
expensive part of the shadow path was already removed by T2.13** (five
double-precision trig evaluations per vertex, +8.31%). What is left is the
remainder, and the remainder is small.

**The lead the owner flagged was the right one and it is where nearly all the
remaining cost lives.** Shadow *construction* is 0.03% of sampled cycles -
nothing. Shadow-driven **floor queries** are 4-6x that. Mario's shadow performs
**11 `find_floor` calls per frame** against a measured total of **43.471**, so
shadows own **25.3% of all floor collision work on the route** - and it is
invisible to any vertex-level accounting.

**The best option is neither of the two on the table.** Two of Mario's 11
shadow floor queries duplicate a query his own physics already made
(`mario.c:1322`), and `gMarioState->floorHeight` is sitting there for free. A
generic sprite that reads it needs **zero** floor queries - so **the sprite
captures 100% of the saving that deletion would, and keeps the depth cue.**
There is no performance argument for deleting over replacing.

---

## 1. Deliverable (1): the evidence chain for verdict (a)

### 1.1 The exact death point

`src/game/game_init.c:464`:

```c
if (!display_suppressed && !gfx_pool_overrun)
    exec_display_list(&gGfxPool->spTask);
```

`display_suppressed` is read at `:450-451` from
`sm64_saturn_source_runtime_display_suppressed()`. It is set `true` at
`src/port/saturn/sourceboot/main.c:547` and cleared at `:559`, and
`game_loop_one_iteration()` is called at `:556` **between those two lines**:

```c
    sm64_saturn_source_runtime_set_display_suppressed(true);   /* :547, #if SATURN_DEMO_PATH */
    ...
    game_loop_one_iteration();                                  /* :556 */
    ...
    sm64_saturn_source_runtime_set_display_suppressed(false);   /* :559 */
```

So for the entire duration of the frame's simulation and geo walk, the
display-list submission at `game_init.c:464` is skipped. Confirmed at runtime:
`submitted_tasks` and `unhandled_tasks` are **both** 0.000/frame, which is a
stronger statement than "dropped" - `exec_display_list()`
(`saturn_source_runtime.c:203`) increments one or the other on every entry, so
zero on both means it is never called at all.

### 1.2 The full chain, each link witnessed

| # | Link | Evidence | Status |
| --- | --- | --- | --- |
| 1 | The geo walk runs | `scene_graph_walks` 1.000/frame, `scene_graph_walks_suppressed` 0.000 | **runs** |
| 2 | `saturn_geo_enter_shadow` is reached | observed in the cycle profile at 0.0134-0.0141% of sampled cycles | **runs** |
| 3 | `create_shadow_below_xyz` returns non-NULL | `make_shadow_vertex` / `calculate_vertex_xyz` / `get_vertex_coords` all observed executing | **runs** |
| 4 | `geo_append_display_list(..., 4\|5\|6)` appends | `_geo_append_display_list` observed at 0.035-0.049% | **runs** |
| 5 | The master list is submitted | `submitted_tasks` **0.000/frame** | **NEVER HAPPENS** |
| 6 | The F3D interpreter decodes it | `command_count` **0.000/frame**, `triangle_count` **0.000/frame** | never runs |
| 7 | Triangles are transformed/culled | every `reject_*` counter **0.000/frame** | never reached |

The shadow dies at link 5, along with everything else in the display list.

### 1.3 The renderer that *does* draw has no shadow support

`src/port/saturn/gfx/saturn_demo_render.c` - the renderer that produces every
VDP1 command on this route - contains **zero occurrences of `shadow`**
(case-insensitive). There are unused shadow fields in
`saturn_actor_effect.h:59-61` and `saturn_actor_instance.h:37,61`, but
`saturn_demo_render.c` references neither `actor_effect` nor
`EFFECT_MATERIAL` at all. There is no code path by which a shadow could reach
VDP1 on this build.

### 1.4 Hypothesis (b) and its sub-hypotheses - all refuted, not merely unfavoured

The brief asked for one hypothesis to be tested explicitly. It is refuted, but
for a reason that makes it moot rather than wrong:

- **Painter-sort coplanarity (the brief's named hypothesis).** Refuted
  upstream. The depth key is real and would have mattered: it is
  `max(view_space_z)` over a primitive's corners
  (`saturn_demo_render.c:2047-2052`), binned by `key >> 7` into 64 bins
  (`saturn_terrain_depth_bins.h:19-20,38-43`), drained far-to-near by
  `sm64_saturn_vdp1_backend_link_depth_bins()`
  (`saturn_vdp1_backend.h:194-275`). There is **no depth bias, no epsilon, no
  decal flag and no forced bucket anywhere in the live key computation** - so
  had a shadow ever reached this stage, coplanar overdraw would have been a
  genuine risk. It never reaches it. Recorded because it becomes a live design
  constraint the moment a sprite is added (section 5.3).
- **Layer 4/5/6 handling.** Moot - no layer is consumed, because no display
  list is consumed.
- **`dl_shadow_circle` material / combine mode.** Moot for the same reason.
- **`shadowSolidity` arriving zero.** Refuted: a zero-solidity path returns
  `NULL` from `correct_shadow_solidity_for_animations`
  (`shadow.c:663-665`) *before* vertices are built, yet
  `make_shadow_vertex`/`calculate_vertex_xyz`/`get_vertex_coords` are all
  observed executing. Vertices are being built, so the display list is non-NULL.
- **Near/far or vertex-range limits.** Refuted directly: `reject_near_far`,
  `reject_z_near`, `reject_z_far`, `reject_vertex_range`, `reject_offscreen`,
  `reject_span`, `reject_w_nonpositive` are **all 0.000/frame** across 119
  intervals.

**This is not ambiguous and I am not hedging to avoid picking.** (a) and (b)
are distinguished by a counter that reads exactly zero on a path that
increments on every entry.

---

## 2. Deliverable (2): the cull funnel

`tools/saturn/capture_route_counters.py` (landed, commit `4bfe7008`), 120
samples, 119 intervals, product build `id-a61d5203793986e7`, 50.3 s.
Evidence: `sprint2-t2_14-route-counters.json`.

### 2.1 Why nobody had ever captured this, diagnosed

The counters were described in the brief as "fully wired but never captured".
The reason was never diagnosed, so here it is:
`sourceboot_capture_route_checkpoint()` is guarded by

```c
#if SATURN_SOURCEBOOT_ROUTE_REPLAY && !SATURN_SOURCEBOOT_LIVE_INPUT   /* main.c:599 */
```

and the shipped profile sets **`live_input_mode: 1`**. The variable's own
definition (`main.c:297`) sits behind the *same* guard, so the publisher and
the storage are both compiled out and `_sourceboot_route_checkpoint` is **not
present in the product ELF's symbol table at all** - so every tool that went looking for the
symbol found nothing and reported nothing. The underlying
`sourceboot_fast3d.profile` struct is live regardless; the new tool reads it
directly and bypasses the publisher.

**No diagnostic build is required.** T2.13 found VDP1 *command* count needs
`SATURN_DIAGNOSTIC_MODE=2`; the funnel counters do not. They are ordinary
unconditional increments in `saturn_demo_render.c` and
`saturn_fast3d_frontend.c`, present in the product build, and now readable.

### 2.2 The funnel, measured

Per unit of `frame_serial`. The counters are **cumulative** (the per-frame
`memset` lives in `sm64_saturn_fast3d_frontend_submit`, which never runs), so
these are deltas.

| Stage | Counter | Mean / frame | Median | Range | Share |
| --- | --- | ---: | ---: | ---: | ---: |
| display-list commands decoded | `command_count` | **0.000** | 0 | 0-0 | 0% |
| display-list triangles seen | `triangle_count` | **0.000** | 0 | 0-0 | 0% |
| vertex positions transformed | `triangles_transformed` | **338.656** | 337 | 337-367 | - |
| primitives emitted | `triangles_emitted` | **502.807** | 503 | 472-531 | 100% |
| **VDP1 commands emitted** | `triangles_vdp1_emitted` | **502.807** | 503 | 472-531 | **100.0%** |
| near/far composite reject | `reject_near_far` | **0.000** | 0 | 0-0 | 0% |
| backface reject | `reject_backface` | **0.000** | 0 | 0-0 | 0% |
| degenerate reject | `reject_degenerate` | **0.000** | 0 | 0-0 | 0% |
| vertex-range reject | `reject_vertex_range` | **0.000** | 0 | 0-0 | 0% |
| command-capacity reject | `reject_command_capacity` | **0.000** | 0 | 0-0 | 0% |
| VDP1 arena-capacity reject | `reject_vdp1_arena_capacity` | **0.000** | 0 | 0-0 | 0% |
| w<=0 reject | `reject_w_nonpositive` | **0.000** | 0 | 0-0 | 0% |
| z-near reject | `reject_z_near` | **0.000** | 0 | 0-0 | 0% |
| z-far reject | `reject_z_far` | **0.000** | 0 | 0-0 | 0% |
| offscreen reject | `reject_offscreen` | **0.000** | 0 | 0-0 | 0% |
| span reject | `reject_span` | **0.000** | 0 | 0-0 | 0% |

Supporting witnesses, same capture:

| Counter | Mean / frame |
| --- | ---: |
| `sState.submitted_tasks` | 0.000 |
| `sState.unhandled_tasks` | 0.000 |
| `sState.scene_graph_walks` | 1.000 |
| `sState.input_polls` | 1.000 |
| `gNumCalls.floor` (`find_floor`) | **43.471** (median 43, 41-49) |
| `gNumCalls.wall` | 11.353 |
| `gNumCalls.ceil` | 1.807 |

### 2.3 The honest reading - this is not a funnel, and saying so is the finding

The brief asked for "submitted -> transformed -> emitted -> VDP1-emitted, with
every reject counter and each stage's share". The measurement will not support
that shape, and forcing it into that shape would be a fabrication. Three
separate problems, all of which are themselves the answer to "how much does
each cull stage kill":

1. **Eleven of the sixteen counters are structurally dead on this route, not
   merely zero.** `reject_backface`, `reject_vertex_range`,
   `reject_command_capacity`, `reject_w_nonpositive`, `reject_z_near`,
   `reject_z_far`, `reject_offscreen`, `reject_span`, `command_count` and
   `triangle_count` are incremented **only** in `saturn_fast3d_frontend.c`,
   which never executes. They cannot ever be non-zero on a `demo_path=1` build.
   The demo renderer touches only three reject buckets at all
   (`reject_degenerate`, `reject_near_far`, `reject_vdp1_arena_capacity`), and
   all three measure zero.
2. **`triangles_transformed` does not count triangles.** It is incremented as
   `profile->triangles_transformed += required_positions`
   (`saturn_demo_render.c:4701`) - it counts *vertex positions*, and only
   terrain ones. Actor emission never touches it. That is why "emitted" (502.8)
   exceeds "transformed" (338.7): they are not successive stages of one
   pipeline, they are two unrelated tallies with misleading names.
3. **`triangles_emitted` and `triangles_vdp1_emitted` carry no independent
   information.** Every one of the ~11 increment sites bumps both in the same
   two lines (e.g. `saturn_demo_render.c:3517-3518`, `:3550-3551`,
   `:3704-3705`, `:4182-4184`). They are equal at every sample by construction,
   not by measurement.

**So the real answer to the standing question is: on the shipped route, the
per-triangle cull stages kill nothing, because they do not run.** All culling
that actually happens is upstream and uncounted - cluster admission
(`sm64_saturn_scene_admit_with_scratch`, 2.241% of sampled cycles), BSP
ordering, actor meshlet depth bounds (`actor_meshlet_live_depth_bounds`,
1.943%), and the arena budget eviction at `saturn_demo_render.c:4735-4740`.
None of those has a reject counter. That is the gap this measurement actually
exposes, and it is worth more than the table above.

Cross-check against T2.8: it measured **552.2 VDP1 commands per present** on a
`SATURN_DIAGNOSTIC_MODE=2` build; this reads **502.8 VDP1 emissions per frame**
on the product build. Different instruments, different builds, ~9% apart -
consistent, and the first time the two have been compared.

---

## 3. What the shadow path actually costs

Two independent burst-sampled cycle profiles of the same product ELF via
`tools/saturn/capture_softfloat_profile.py` (T2.13's tool, reused unmodified
for run A). Run A: 800 bursts x 400 steps = **320,000 samples, 807,307 cycles**.
Run B: 1,200 x 400 = **480,000 samples, 1,208,727 cycles**. Both after a
1,800-VBlank warm-up. Evidence:
`sprint2-t2_14-softfloat-profile-a61d5203.json`.

Run B used a scratchpad copy with the caller filter widened; that attempt did
**not** pay off (section 7.1) and nothing in this report depends on it beyond
its symbol shares, which corroborate run A.

### 3.1 Direct shadow construction - negligible

| Symbol | Run A | Run B |
| --- | ---: | ---: |
| `_saturn_geo_enter_shadow` | 0.0141% | 0.0134% |
| `_make_shadow_vertex` | 0.0061% | 0.0041% |
| `_calculate_vertex_xyz` | 0.0031% | 0.0096% |
| `_make_shadow_vertex_at_xyz` | 0.0028% | 0.0028% |
| `_get_vertex_coords` | - | 0.0036% |
| **total** | **0.0261%** | **0.0335%** |

**~0.03% of sampled cycles.** For scale, that is 1/80th of
`_demo_render_finalize` alone. T2.13 did its job: the shadow's arithmetic is no
longer a cost worth naming.

### 3.2 The floor queries - where the remaining cost is, exactly as the owner suspected

`create_shadow_below_xyz` and its callees issue **11 `find_floor` calls per
frame** for Mario's shadow. Derived from source, each site cited:

| # | Site | Calls | Note |
| ---: | --- | ---: | --- |
| 1 | `shadow.c:905` `find_floor(xPos, yPos, zPos, &pfloor)` | 1 | in `create_shadow_below_xyz` |
| 2 | `shadow.c:232` `find_floor_height_and_data(parentX, parentY, parentZ, ...)` | 1 | in `init_shadow`; wrapper around `find_floor` (`surface_collision.c:381`) |
| 3 | `shadow.c:439` `find_floor_height_and_data(*xPosVtx, s.parentY, *zPosVtx, &dummy)` | **9** | in `calculate_vertex_xyz`, **once per shadow vertex**, at nine distinct XZ positions |
| | **total** | **11** | |

Site 3 is the `SHADOW_WITH_9_VERTS` arm, and Mario is
`GEO_SHADOW(SHADOW_CIRCLE_PLAYER, 0xB4, 100)` - nine vertices. The arm is
guarded by `if (gShadowAboveWaterOrLava)`, which takes the cheap
`*yPosVtx = s.floorHeight` shortcut instead. **Measured: `gShadowAboveWaterOrLava`
is `0` on all 120 samples**, so the nine-query arm is the live one on this route.

Against the measured total:

| Quantity | Value | Source |
| --- | ---: | --- |
| `find_floor` calls per frame, all callers | **43.471** | `gNumCalls.floor`, exact engine counter |
| Mario's shadow's share | **11** | derivation above |
| **shadow share of all floor collision** | **25.3%** | 11 / 43.471 |

Each `find_floor` performs exactly **two** `find_floor_from_list` traversals -
one dynamic partition, one static (`surface_collision.c:542,546`) - so the
shadow drives ~22 of ~87 list traversals per frame.

Cost of the floor subsystem:

| Symbol | Run A | Run B |
| --- | ---: | ---: |
| `_find_floor_from_list` | 0.6710% | 0.4499% |
| `_find_floor` | 0.0358% | 0.0239% |
| **total** | **0.7068%** | **0.4738%** |

**Shadow-attributable floor cost = 25.3% x [0.4738% .. 0.7068%] = 0.120% ..
0.179% of sampled cycles.**

### 3.3 Total, and the honest error bars

| Component | Sampled-cycle share |
| --- | ---: |
| shadow construction | 0.026% - 0.034% |
| shadow-driven floor queries | 0.120% - 0.179% |
| shadow display-list allocation (part of `_alloc_display_list` 0.009-0.015%, `_geo_append_display_list` 0.035-0.049%) | < 0.01% |
| **shadow path, total** | **0.15% - 0.21%** |
| **as a share of non-idle cycles** (idle is 73.33%) | **0.55% - 0.79%** |

**The 25.3% is a lower bound on the shadow share of floor work**, not a point
estimate: it counts Mario only. Any other shadow-bearing actor live on the
route adds more. I did not establish how many there are - see section 7.2.

---

## 4. What gating is worth, against 5.3538 FPS / 11.2069 VB

### 4.1 Two conversions, both stated, because they disagree by 3x

There is no measured A/B here - producing one needs a build, which this task
did not make. Both bounds are arithmetic on measured shares:

**Naive bound** (cost share = frame share): 0.18% of 11.2069 VB =
**0.020 VB/frame**.

**T2.13-amplified bound.** T2.13 removed 2.73% of sampled cycles and gained
0.9310 VB of 12.1379 - **7.67% of the frame from 2.73% of sampled cycles, an
amplification of 2.81x**. T2.13 section 11 item 5 flags this gap as
*unexplained* rather than as a mechanism, so applying it is an extrapolation
from one data point. Applied: 0.18% x 2.81 = 0.506% of frame =
**0.057 VB/frame**.

Converting at 11.2069 VB/frame (d(FPS)/d(VB) = -59.94/11.2069² = -0.477 FPS per VB):

| Bound | VB/frame saved | FPS gained | % of current |
| --- | ---: | ---: | ---: |
| naive | 0.020 | **+0.010** | 0.18% |
| T2.13-amplified | 0.057 | **+0.027** | 0.51% |

### 4.2 The number

**Gating the shadow path is worth +0.01 to +0.03 FPS, i.e. 0.2% to 0.5%.**
5.3538 becomes about 5.36-5.38. VB/frame 11.2069 becomes about 11.19-11.15.

Both bounds are **below the run-to-run spread of the two profiles that produced
them** (`_find_floor_from_list` read 0.671% and 0.450% on two sweeps of the
identical ELF - a 1.5x spread). A single `summarize_cadence` capture would
probably not be able to distinguish a gated build from this one.

### 4.3 Explicitly, so the owner is not double-counting

**T2.13 already banked the expensive part of the shadow path.** It removed five
double-precision trig evaluations per shadow vertex and measured
**+0.4106 FPS (+8.31%), -0.9310 VB/frame**. That win is already inside the
5.3538 baseline. **The 0.01-0.03 FPS above is the remainder, not the whole
historical shadow cost.** The full historical cost of the shadow path was
roughly 8.3% + 0.4% - and 8.3 of those 8.7 points are already collected.

### 4.4 Why "several FPS" was too high, and where several FPS actually is

The shadow path is not the frame. From the same profile of the same build:

| Symbol | Sampled-cycle share | vs. whole shadow path |
| --- | ---: | ---: |
| `___slave_polling_entry` (idle) | 43.220% | 240x |
| `_sm64_saturn_source_runtime_wait_vblank` (idle) | 30.107% | 167x |
| `_demo_render_finalize` | 2.392% | **13x** |
| `_sm64_saturn_scene_admit_with_scratch` | 2.241% | **12x** |
| `_demo_terrain_queue_world_lower` | 2.185% | **12x** |
| `_actor_meshlet_live_depth_bounds` | 1.943% | **11x** |
| `_saturn_geo_enter_object` | 1.705% | **9x** |
| **entire shadow path** | **~0.18%** | 1x |

**73.33% of sampled cycles are idle** and each of the top five non-idle
application symbols is 9-13x the entire shadow path. Several FPS is in that
table; it is not in shadows. This corroborates T2.13 section 1.5 on a second
build.

---

## 5. The sprite option, costed

Owner direction: *"shadows are instead a generic painted sprite under mario
that doesnt actually have to follow light cues"*. Both references do exactly
this, which is a strong signal it is the Saturn-shaped answer.

### 5.1 What it saves - and it saves everything deletion would

The decisive finding: **Mario's physics already computes the ground Y the
sprite needs.** `src/game/mario.c:1322`:

```c
m->floorHeight = find_floor(m->pos[0], m->pos[1], m->pos[2], &m->floor);
```

That is the *same query at the same position* that `shadow.c:905` and
`shadow.c:232` each repeat. A sprite that reads `gMarioState->floorHeight`
needs **no floor query of its own**.

| Option | Floor queries/frame | Construction cost | Saving vs. today |
| --- | ---: | ---: | ---: |
| today (9-vert mesh) | 11 | 0.03% | - |
| **delete entirely** | 0 | 0 | 0.15% - 0.21% |
| **generic sprite reading `gMarioState->floorHeight`** | **0** | ~0 | **0.15% - 0.21%** |
| naive sprite with its own `find_floor` | 1 | ~0 | 0.14% - 0.19% |

**The sprite and full deletion are worth the same number.** There is no
performance argument for deleting rather than replacing. The difference between
them is a gameplay depth cue, and per the owner's own framing that cue is how
the player judges landing position when platforming.

Added cost: **one VDP1 command** against a measured 502.8 per frame (+0.2%).
Note this is not quite free - T2.8 found VDP1 needs ~22.5 VBlanks to retire a
552-command frame against a 15.7-VBlank frame, i.e. it is **over-subscribed by
~1.4x** and silently relies on plot overrun. One more command is inside the
noise of that, but "VDP1 is idle" is not the right justification; "the fence
never blocks the CPU, so it costs no cadence" is (T2.8: 0 waits in 1,349
frames).

### 5.2 What the references do

**Reuse mode: behavior-only / doc-study. No code copied from either GPL tree.**

| Repo | Path | SHA | License | Verified |
| --- | --- | --- | --- | --- |
| SlaveDriver Engine | `work/upstream/slavedriver-engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0 (`LICENSE.txt`) | 2026-08-16 |
| Sonic Z-Treme | `work/upstream/sonic-z-treme` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 text, README contradicts it - treat as licence-unclear | 2026-08-16 |
| Jo Engine | `work/upstream/joengine` | `556d081146211b6a1cfa6591d70f9487d406758b` | **MIT** (correcting T2.13, which recorded BSD-3-Clause) | 2026-08-16 |

Files inspected: `slavedriver-engine/WALLS.C`, `SPR.C`, `SPR.H`, `AI.C`,
`DSP/INCLUDE/SL_DEF.H`; `sonic-z-treme/.../ZTE/ZT_RENDERING.c`,
`ZT_SPRITES.H`, `ZT_COLLISION.c`, `ZT_LOAD_MODEL.c`, `ZT_LOADING.c`,
`SRC/game.c`, `Compiler/SGL_302j/INC/SL_DEF.H`; `joengine/jo_engine/3d.c`,
`vdp1_command_pipeline.c`, `jo/sprites.h`, `jo/3d.h`.

- **SlaveDriver** (`WALLS.C:2560-2591`, in `drawSprites()`): a single scaled
  sprite in VDP1 hardware shadow mode -
  `EZ_scaleSpr(ZOOM_MM, COLOR_4|COMPO_SHADOW, 0, mapPic(0), pos, NULL)`.
  `COMPO_SHADOW` *darkens the framebuffer* rather than painting an opaque blob
  (`SPR.H:44`). Base 48x48, scaled by
  `shadowScale = (F(128) - abs(shadowPos.y - feetPos.y)) >> 7` - it shrinks with
  height above ground and **vanishes past 128 units**. Ground Y from
  `findFloorDistance()`. Screen-flat; it does **not** follow the floor normal.
  Opt-out flag `SPRITEFLAG_NOSHADOW`.
- **Sonic Z-Treme** (`ZT_RENDERING.c:622`, in `display_player()`):
  `ztPutSprite(16, 0, ..., toFIXED(1.69), 0, MESHoff | CL_Shadow)` - a
  camera-facing billboard at **fixed scale, no height falloff at all**, culled
  past 640 units, texture `SHADOW.ZTI`. Ground Y from a **dedicated cheap
  collision pass**, `shadowCalculation()` (`ZT_COLLISION.c:168-177`), which the
  author's own comment calls a rough check for the player's shadow. This
  independently confirms the shape of section 5.1's finding: the sprite needs a
  ground Y, and you get it from something cheaper than real collision - or, in
  our case, from a query already made.
- **Jo Engine**: no drop shadow at all. Only a per-sprite VDP1 shadow-filter
  toggle, unused in every sample. Nothing to take.

### 5.3 The one design constraint the references settle

There is **no depth bias, epsilon, decal flag or forced bucket** anywhere in
this port's live depth-key path. A ground quad under Mario is on a knife edge:
terrain keys off a primitive's farthest corner
(`saturn_demo_render.c:2047-2052`), actors off a meshlet's furthest vertex
(`saturn_actor_meshlets.c:852`), both `>> 7` into 64 bins that coincide
numerically **without any `_Static_assert` binding them**. If the shadow quad's
max-Z lands in a nearer bin than a Mario meshlet's, it drains later and paints
over Mario's feet.

**SlaveDriver's answer is the one to copy, and it needs no bias.** It abandons
the global sort entirely (`SPR.C:159-163`, a bump allocator; draw order ==
emission order) and emits each shadow **immediately before its own character in
the same loop iteration**. Mapped onto this port: emit the shadow in the actor
stream with its `sort_key` **forced to Mario's own bin**, ordered before
Mario's meshlets. That makes the ordering structural rather than numerical, and
it is exactly what the existing producer order already does for terrain
(`saturn_demo_render.c:4745` before `:4761`).

Also worth knowing: `SORT_BFR` ("use the position of the polygon displayed just
before") exists in all three vendored SGL headers and is **used by none of
them** - the obvious primitive for decal-on-surface ordering is untouched prior
art. And the sort-mode enum ordering **differs between the vendored SGL
copies** (SlaveDriver `SORT_MIN=0`; SGL 3.02j `SORT_BFR=0`), so numeric sort
values are not portable between these trees.

---

## 6. What removal or replacement would orphan

Scoped so a follow-up task does not have to rediscover it.

**Fully dead if the shadow path goes:**

- `src/game/shadow.c` in its entirety (~950 lines). Its only external entry is
  `create_shadow_below_xyz`, called only from
  `rendering_graph_node.c:1384`.
- `saturn_geo_enter_shadow` and `geo_process_shadow`
  (`rendering_graph_node.c:1335-1420`), and the `GRAPH_NODE_TYPE_SHADOW`
  dispatch.
- **T2.13's own additions**: `struct Shadow.floorDownwardAngleBam` and
  `.floorTiltBam`, and the `sins`/`coss` table indexing in
  `calculate_vertex_xyz`'s Saturn arm.
- **T2.13's gates**: `tools/saturn/shadow_trig_test.c`, `verify-shadow-trig`
  and `verify-shadow-trig-mutation`, both currently in `verify-all`. Removing
  the code without removing these leaves two gates testing nothing.
- The four shadow globals - `gShadowAboveWaterOrLava`, `gMarioOnIceOrCarpet`,
  `sMarioOnFlyingCarpet`, `sSurfaceTypeBelowShadow`.

**Verified: the four globals have zero gameplay dependency.** Grepped across
all of `src/`; outside `shadow.c` and `shadow.h` their *only* readers are the
layer selection at `rendering_graph_node.c:1406-1411`, which feeds the
discarded `geo_append_display_list`. Nothing in Mario's physics, camera,
surface handling or object code reads any of them. **Gating shadows cannot
change gameplay**, which also means it cannot change the deterministic route -
a useful property, since it makes a gated build directly comparable to
`id-a61d5203793986e7`.

**Flag for later phases:**

- **S6 (levels).** `GEO_SHADOW` nodes are baked into every actor's geo layout.
  Removal must not break geo-layout *parsing* for actors that still carry the
  node - gating inside `saturn_geo_enter_shadow` is safe; deleting the node
  type from the graph walker is not.
- **S5 (game flow) / S6.** `SHADOW_SQUARE_PERMANENT` / `_SCALABLE` /
  `_TOGGLABLE` are used by moving platforms, where the shadow is a genuine
  gameplay cue for platform position, not decoration. A sprite that only ever
  attaches to Mario does not cover these.
- The donor (`#else`) arms in `shadow.c` are untouched by any Saturn change and
  should stay that way.

---

## 7. Honesty - what is wrong with this task, and what did not pay off

### 7.1 Things attempted that did not work

1. **Widening the cycle profiler's caller attribution did not pay off.** The
   tool filters `callers` to helper-class callees only
   (`capture_softfloat_profile.py:299`). I ran a scratchpad copy with that
   filter removed and 480,000 samples, hoping to attribute `find_floor` calls
   to `calculate_vertex_xyz` vs. gameplay directly. It yielded **9 sampled
   entries into `_find_floor_from_list` and 1 into `_find_floor`** - far too
   sparse to attribute anything. Burst sampling catches call *edges* very
   rarely. Reaching ~200 entries would need roughly 10M samples (~15 min of
   stepping). Abandoned in favour of the exact `gNumCalls` counter, which was
   better anyway. **Recorded so nobody repeats it.**
2. **The `est_calls_per_frame` figures in the softfloat profile are unusable at
   this level.** They are derived from those same 1-13 sampled entries. Only
   the `cycle_share` figures in this report should be trusted; I have not
   quoted a single call rate from the profiler.

### 7.2 Limits on the numbers

1. **The 25.3% floor share is a lower bound, not a point estimate.** It counts
   Mario's shadow only. I did not establish how many other shadow-bearing
   actors are live on the BOB route, so the true shadow share of the 43.471
   `find_floor` calls is >= 25.3%. Settling it needs either a per-caller
   counter (a build) or a much longer stepping sweep.
2. **No measured A/B.** The 0.01-0.03 FPS is arithmetic on measured cycle
   shares, not a `summarize_cadence` delta between a gated and an ungated
   build. The brief's rule that cadence comes from `summarize_cadence` only is
   why no FPS figure here is presented as measured - **the only measured
   cadence numbers in this report are T2.13's 5.3538 / 11.2069.**
3. **The 2.81x amplification factor is extrapolated from one data point** and
   T2.13 itself calls the mechanism unexplained. It is presented as an upper
   bound, not a prediction.
4. **The two profile sweeps disagree by up to 1.5x on individual symbols**
   (`_find_floor_from_list`: 0.671% vs 0.450%). Burst sampling is serially
   correlated within a burst; class-level shares are robust, single symbols at
   the 0.1% level are not. Both runs are reported rather than the friendlier one.
5. **The 11-queries-per-frame figure is derived from source, not counted.** It
   is solid - each site is cited and the `gShadowAboveWaterOrLava == 0`
   precondition is measured - but it is a structural count.
6. **This is not an owner observation.** It is a headless diagnosis. Nothing
   here changes what is on screen; no build was made and no CUE was produced.

### 7.3 Route-specificity

The shadow is genuinely being built on the captured window - links 1-4 of
section 1.2 are all witnessed executing - so this is **not** the "no
shadow-bearing actor was active" case the brief asked me to watch for. The
finding is route-independent in the part that matters: `display_suppressed` is
set for the whole geo walk on **every** frame of **every** `SATURN_DEMO_PATH=1`
build, so no route, pose or camera can make a shadow appear on this renderer.

---

## 8. Recommendation

1. **Do not spend a build gating shadows on its own.** It is worth
   0.2%-0.5%, below the measurement noise of a single cadence capture, and it
   would cost the same build slot as something 10x larger.
2. **Fold it into the sprite work instead, and prefer the sprite over
   deletion.** They are worth the same number (section 5.1), and the sprite
   keeps the platforming depth cue. The sprite reads
   `gMarioState->floorHeight`, needs **zero** floor queries, emits **one** VDP1
   command, and orders itself by taking SlaveDriver's structural answer - same
   depth bin as Mario, emitted immediately before him - rather than inventing a
   depth bias this renderer does not have.
3. **The real lever is elsewhere and this task measured it again.** 73.33% idle;
   `_demo_render_finalize` 2.392%, `_sm64_saturn_scene_admit_with_scratch`
   2.241%, `_demo_terrain_queue_world_lower` 2.185%,
   `_actor_meshlet_live_depth_bounds` 1.943%, `_saturn_geo_enter_object`
   1.705% - each 9-13x the whole shadow path. This is the second independent
   confirmation of T2.13 section 1.5.
4. **Worth its own look: the geo walk builds a display list nobody reads.**
   Every frame the walk allocates and fills a master display list that
   `game_init.c:464` then declines to submit. The walk itself must stay - it
   owns animation, warp, camera, water and matrix state (`main.c:550-552`) -
   but the *display-list construction* inside it is pure waste, and
   `_saturn_geo_enter_object` (1.705%) plus `_saturn_mtxq_refresh_float_mirror`
   (0.585%), `_alloc_display_list` and `_geo_append_display_list` sit inside
   it. That is ~10x the shadow path and nobody has scoped it. Note
   `SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1` already exists as a **sealed
   upper-bound diagnostic** (`main.c:549-554`) and would bound the prize in one
   build - though it invalidates animation and matrix state, so it measures a
   ceiling, not a candidate.
5. **Fix the counter names or the counters.** `triangles_transformed` counts
   vertex positions; `triangles_emitted` and `triangles_vdp1_emitted` are
   incremented in lockstep and cannot differ. Eleven of sixteen reject buckets
   are unreachable on the shipped path. Anyone reading these expecting a funnel
   will be misled exactly as this task's brief was.

---

## 9. Reproduction

```
# Everything in this report, against the shipped product ELF. No build.

# 1. Symbol addresses (ELF32 big-endian symbol table):
#    _sourceboot_fast3d  0x002cdac4   (profile is its first member)
#    _sState             0x060f0d28
#    _gNumCalls          0x060dcb64
#    _sourceboot_route_checkpoint -- ABSENT from the product ELF (section 2.1)

# 2. Funnel + task witnesses + collision tally (~50 s):
python tools/saturn/capture_route_counters.py \
  --ymir <ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe> \
  --ipl <sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin> \
  --game <releases/2026-08-16_t2_13-product/id-a61d5203793986e7/...e2.cue> \
  --elf  <releases/2026-08-16_t2_13-product/id-a61d5203793986e7/...e2.elf> \
  --profile-address 0x002cdac4 --state-address 0x060f0d28 \
  --numcalls-address 0x060dcb64 \
  --output docs/saturn/evidence/reports/sprint2-t2_14-route-counters.json \
  --samples 120 --gap-vblanks 11

# 3. Cycle profile for the shadow/floor shares (~90 s), T2.13's tool unchanged:
python tools/saturn/capture_softfloat_profile.py \
  --ymir <...> --ipl <...> --game <...> --elf <...> \
  --nm <work/yaul-install/bin/sh-elf-nm.exe> \
  --output docs/saturn/evidence/reports/sprint2-t2_14-softfloat-profile-a61d5203.json \
  --phases 1 --phase-vblanks 1800 --bursts 800 --burst-steps 400 --gap-vblanks 1 \
  --vblanks-per-frame 11.2069

# 4. Profile struct offsets, if the header ever changes -- host compile,
#    offsetof() on sm64_saturn_fast3d_profile_t. Both ABIs are 32-bit with
#    natural alignment and the struct has no pointer members, so host offsets
#    equal SH-2 offsets.
```

Artifacts preserved to `releases/2026-08-16_t2_14-evidence/`.
Commit: `4bfe7008` (`tools/saturn/capture_route_counters.py` + CHANGELOG +
`sprint2-t2_14-route-counters.json`).
