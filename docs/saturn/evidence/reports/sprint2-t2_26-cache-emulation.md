# Sprint 2 Task T2.26 — SH-2 cache emulation on the headless path, and what it changes

- Date: 2026-08-17. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `f93bb4bf`. The emulator tree
  `D:/Code/RetroDev/sm64-saturn-port/ymir-agent` is a **separate tree**; its
  changes are listed separately in section 1.1, are committed separately as
  `cbd87caa`, and are not part of any port-tree commit.
- Task: expose Ymir's `m_emulateSH2Caches` on `ymir-headless`, re-baseline the
  accepted build on that basis, and re-price the slave block T2.25 declined to
  offload because it could not price it.
- **Headline: the flag works, the numbers move, and the direction is the one
  the sprint feared — but the size is smaller than the fear.**
  **Master-resident work inflates +8.11% under cache emulation; slave-resident
  work inflates +27.30%.** That is the coherency tax, measured for the first
  time. It is a tax on the work *already* on the slave; the *differential*
  penalty for moving a unit of work from master to slave is **~1.18x**, which
  does not overturn the one genuinely eligible offload candidate.
- **Bus arbitration is still not modelled, with caches on or off.** Named
  cause in section 6. Offload figures remain upper bounds — but now for one
  specific, named, remaining reason instead of two vague ones.
- **T2.25's coherency fix (`f25206c7`) shows no measurable effect**, on either
  basis, on every reported figure. Section 7 says exactly why, and why that is
  still the first time anyone could have asked.
- **Wall-clock cost is negligible.** For a fixed instruction budget it is
  **-1.0%** (inside noise); for an event-counted capture it is **+7 to +8%**
  per emulated VBlank. This is not a spot-check-only instrument.
- **Correctness result, independent of the timing question:**
  `presentation_generation_delta == 1` on all 29 intervals under **both** bases,
  `master_failures = slave_failures = 0`, `qn = qr = 30`, `qw = qf = qq = 0`.
  **The port runs correctly on a coherency-modelling emulator** — no dropped or
  duplicated fields, no quarantined job, on either build.
- Evidence written by this task:
  `sprint2-t2_26-throughput-cacheoff-30events.json`,
  `sprint2-t2_26-throughput-cacheon-30events.json`,
  `sprint2-t2_26-throughput-t2_25build-cacheoff-30events.json`,
  `sprint2-t2_26-throughput-t2_25build-cacheon-30events.json`,
  `sprint2-t2_26-idle-cacheoff-tick180.json`,
  `sprint2-t2_26-idle-cacheon-tick180.json`.
- **No port product source was changed and no port build was made.** Both
  captured builds are the sealed release artifacts.

---

## 1. What was added, and where

### 1.1 Emulator (`ymir-agent`, separate tree, commit `cbd87caa`)

The brief's premise was verified before anything was written.
`m_emulateSH2Caches` is a runtime toggle, not a compile-time constant:
`Saturn::UpdateSH2CacheEmulation(bool)`
(`libs/ymir-core/src/ymir/sys/saturn.cpp:801-810`) purges both caches on the
off->on edge, ORs in `m_forceSH2CacheEmulation`, and re-selects the
`Step`/`RunFrame` instantiations through `UpdateFunctionPointers()`.
`saturn.cpp:150-151` binds the option into both SH-2s;
`apps/ymir-sdl3/.../emu_event_factory.cpp:554-556` drives it from the GUI.
**Nothing under `apps/ymir-headless` referenced it** — confirmed by grepping
the whole tree for `emulateSH2Cache`, `EnableSH2CacheEmulation`,
`IsSH2CacheEmulationEnabled` and `forceSH2CacheEmulation`. That is now fixed.

| File | Change |
| :--- | :--- |
| `apps/ymir-headless/src/config.hpp` | `bool emulate_sh2_cache{false}` on `HeadlessConfig`, with the "different basis, not a correction" note attached to the field itself |
| `apps/ymir-headless/src/config_parser.hpp` | `--sh2-cache` / `--no-sh2-cache` in `ParseCliConfig`, `emulate_sh2_cache` TOML key in `LoadConfigFile`, key added to `IsHeadlessConfigKey`, override in `ApplyCliConfig` — the same shape as the existing `--dram-cart` / `--no-dram-cart` pair |
| `apps/ymir-headless/src/debug_service.cpp` | `Initialize()` calls `m_saturn->EnableSH2CacheEmulation(true)` before `Reset(true)` when configured. `Saturn::Reset` does not clear `m_emulateSH2Caches` (`saturn.cpp:171-200`), so the setting also survives `exec.reset` |
| `apps/ymir-headless/src/debug_service.cpp` | `instance.ready` now carries `sh2_cache_emulation`, read back from `IsSH2CacheEmulationEnabled()` |
| `apps/ymir-headless/src/main.cpp` | one stderr line reporting the resolved setting |

**Default is off.** Every existing capture, reproduction instruction and
accepted baseline keeps working with no change — proved by measurement in
section 3, not asserted.

### 1.2 Port capture tooling (worktree)

| File | Change |
| :--- | :--- |
| `tools/saturn/capture_route_views.py` | `YmirClient(..., *, sh2_cache: bool = False)` appends `--sh2-cache`; new `_assert_cache_basis()` runs on the `instance.ready` notification |
| `tools/saturn/capture_sourceboot_throughput.py` | `--sh2-cache`; records `sh2_cache_emulation` at the top level of the report |
| `tools/saturn/capture_idle_attribution.py` | `--sh2-cache`; records `tracing.sh2_cache_emulation` |

`YmirClient` is the single launcher for 19 capture tools, so the keyword-only,
default-false parameter reaches all of them without touching any call site.

**The guard fails closed in both directions**, which is the part that matters:

```python
if self.sh2_cache and reported is not True:      # old binary, or flag ignored
    raise RuntimeError(...)
if not self.sh2_cache and reported is True:      # stray Ymir.toml key
    raise RuntimeError(...)
```

A pre-T2.26 `ymir-headless` silently ignores unknown arguments and omits the
field, so without this a capture could have run with caches **off** while
labelling itself caches-on. That is worse than no capture. The old binary is
now rejected outright when `--sh2-cache` is requested.

`python -m pytest tools/saturn/test_capture_sourceboot_throughput.py -q` ->
**45 passed, 7 subtests passed**, unchanged.

### 1.3 How to rebuild

The port was **not** rebuilt. Only `ymir-headless` was:

```
cmake --build D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2 \
      --config Release --target ymir-headless -- -m
```

Incremental, four translation units, under a minute (Visual Studio 17 2022
generator, `x64-windows` vcpkg triplet, cache already configured).

**The prior binary was preserved before the build, per the build-preservation
rule**, and every number below states which binary produced it:

| Binary | SHA-256 | Size | Role |
| :--- | :--- | ---: | :--- |
| `build-agent2/apps/ymir-headless/Release/ymir-headless-pre-t2_26-nocache.exe` | `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943` | 3,255,296 | **byte-identical copy of the binary every prior capture in this project used** (matches the emulator identity recorded from T2.7 onward). Preserved so those captures stay reproducible |
| `build-agent2/apps/ymir-headless/Release/ymir-headless.exe` | `3156bea3f416ff98d061a2cdc81461dfbeb26da6674dbba211868d0d1677f98b` | 3,256,320 | **T2.26 binary — produced every number in this report**, both bases |

---

## 2. What cache emulation actually is, in this emulator

Read before measuring, because the answer determines how the numbers may be
used.

**It is a real cache, functionally.** `SH2::MemRead`
(`libs/ymir-core/src/ymir/hw/sh2/sh2.cpp:644-745`) implements four-way
associative lookup, 16-byte line fill, LRU update, and the full `CCR`
semantics (`CE`, `CP`, `TW`, `OD`, `ID`). Each SH-2 owns its own `m_cache`, and
**there is no snooping between them** — on hardware or here. So a cached P0
read of a line the peer wrote through P2 now genuinely returns stale data.
That is the mechanism the whole coherency discipline exists to defeat, and it
is now simulated.

**It is also a timing change, and this is what moved the cadence.**
`SH2::AccessCycles` (`sh2.cpp:964-997`):

| Partition | caches **off** | caches **on** |
| :--- | :--- | :--- |
| `0b000` cached area, read | **1 cycle, always** ("Simplified model - assume cache hits on all accesses to cached area") | hit -> 1 cycle; **miss -> `m_bus.GetAccessCycles`** |
| `0b000` cached area, write | **1 cycle** | **`m_bus.GetAccessCycles`** — falls through to the cache-through arm, correct for a write-through cache |
| `0b001` / `0b101` cache-through | `m_bus.GetAccessCycles` | unchanged |

So caches-off prices **every** cached-area access — including instruction
fetch and every store — at one cycle. Caches-on prices misses and all stores at
bus cost. Cycles can only rise.

**Crucially, cache-through accesses cost the same under both bases.** The port
publishes cross-CPU data through P2 aliases, so *those* reads were never the
mispriced part. What was mispriced is everything else the slave touches.

---

## 3. The control: the flag is genuinely inert when off

Before any caches-on number is quoted, the new binary must reproduce the
accepted baseline exactly. It does.

`capture_sourceboot_throughput.py`, `summarize_cadence` only, 30 presentation
events, 29 intervals, sealed `id-c0352f297034f653`, on-target identity
**MATCH** (`d88b17a9...23b5`), `status: complete`, route ticks 0-31.

| | Accepted baseline (T2.17/T2.25, old binary) | T2.26 binary, caches off |
| :--- | ---: | ---: |
| FPS mean | 6.7181 | **6.7181** |
| FPS median | 6.6667 | **6.6667** |
| FPS 1% low | 6.0 | **6.0** |
| VB per frame | 8.9310 | **8.9310** |
| target VBlank delta / 29 | 259 | **259** |
| interval distribution | `{7:1, 8:2, 9:24, 10:2}` | **`{7:1, 8:2, 9:24, 10:2}`** |

**Bit-identical on every reported figure.** The same holds for the idle
instrument: `sprint2-t2_26-idle-cacheoff-tick180.json` reproduces T2.20's
tick-180 census to four decimals — master idle **0.000000**, slave idle
**6.1095 VB/frame (68.41%)**, joint `work|work` **2.8215 VB** — against
`sprint2-t2_20-idle-attribution-tick180-c0352f29.json`, which was taken on the
**old** binary. Two different binaries, same numbers.

---

## 4. The re-baseline: caches-off against caches-on

**Caches-on is a different measurement basis, not a correction to the old one.**
Every prior figure in this project remains valid on its own basis and must not
be silently restated against these. Nothing below revises T2.17's 6.7181 or
T2.16's idle split; it adds a second column.

### 4.1 Cadence

Same instrument, same build, same route span (ticks 0-31), same 30 events.

| | caches **off** | caches **on** | delta |
| :--- | ---: | ---: | ---: |
| FPS mean | 6.7181 | **6.2143** | **-7.50%** |
| FPS median | 6.6667 | **6.0000** | -10.00% |
| FPS 1% low | 6.0 | **5.4545** | -9.09% |
| **VB per frame** | **8.9310** | **9.6552** | **+0.7242 (+8.11%)** |
| target VBlank delta / 29 | 259 | 280 | +21 |
| interval distribution | `{7:1, 8:2, 9:24, 10:2}` | `{8:2, 9:7, 10:19, 11:1}` | modal interval 9 -> 10 |
| `presentation_generation_delta` | `1` x29 | `1` x29 | none |
| on-target identity | MATCH | MATCH | — |
| BIOS handoff (startup VBlanks) | 681 | 688 | +7 |

The per-interval phase decomposition moves consistently and still does not
close (as at T2.17 — reported only to show it did not shift oddly):
simulation 4.8276 -> 5.0690, construction 6.0000 -> 6.3448,
transport/presentation 0.1034 -> 0.2069, master finalization 3.8276 -> 3.8966,
slave-work overlap window 2.8276 -> 3.1379, attributed 10.9310 -> 11.6207
against actual intervals of 8.9310 and 9.6552.

### 4.2 Master/slave idle split

`capture_idle_attribution.py`, one contiguous 6,000,000-master-instruction
window, `--warmup-ticks 180`, sealed `id-c0352f297034f653`. Both runs cover
route ticks **180 -> 181** (`global_timer` 180 -> 182): the same route
position, so the comparison is like-for-like.

| | caches **off** | caches **on** |
| :--- | ---: | ---: |
| traced VBlanks | 16.317 | 17.6474 |
| traced frames equivalent | 1.827 | 1.8278 |
| **master idle** | **0.000000** | **0.000000** |
| slave idle | 68.408% = **6.1095 VB/frame** | 62.798% = **6.0633 VB/frame** |
| joint `work \| idle` | 6.1095 VB (68.41%) | **6.0632 VB (62.80%)** |
| joint `work \| work` | 2.8215 VB (31.59%) | **3.5919 VB (37.20%)** |
| joint `idle \| *` | absent | **absent** |

**The master still never idles.** T2.25's central structural finding — the
frame is identically the master's work, so overlap converts nothing and only
offload does — survives cache emulation unchanged. Two occupied cells, one of
them empty, on both bases.

### 4.3 The coherency tax, isolated

Both windows are exactly 6,000,000 master instructions, so the cycle totals are
a direct instruction-for-instruction price comparison with no conversion in
between:

| Quantity | caches off | caches on | inflation |
| :--- | ---: | ---: | ---: |
| master cycles / 6,000,000 instructions | 7,300,914 | 7,896,206 | **+8.15%** |
| **master work, cycles/frame** | 3,996,122 | 4,320,004 | **+8.11%** |
| **slave work, cycles/frame** | 1,262,449 | 1,607,141 | **+27.30%** |
| slave idle (spin), cycles/frame | 2,733,672 | 2,712,919 | -0.8% |

**This is the number T2.16 section 7.1 said this rig could not produce.**
Slave-resident work is **3.37x more sensitive** to cache emulation than
master-resident work (27.30 / 8.11). The slave's idle spin is unaffected,
which is the expected control: `___slave_polling_entry` is a tight loop on one
flag and costs the same either way.

The slave-side census says why — the inflation lands on streaming and transform
work, exactly where a small 4-way cache thrashes:

| Slave symbol | off (VB/frame) | on (VB/frame) | x |
| :--- | ---: | ---: | ---: |
| `___slave_polling_entry` (idle) | 6.1095 | 6.0632 | **0.99** |
| `_demo_terrain_queue_world_admit` | 0.0848 | 0.1606 | **1.89** |
| `_memset` | 0.0572 | 0.0968 | **1.69** |
| `___movmemSI4` | 0.0400 | 0.0671 | **1.68** |
| `_demo_prepare_position_owners` | 0.1362 | 0.2137 | **1.57** |
| `_sm64_saturn_ir_transform_one` | 0.2990 | 0.4228 | **1.41** |
| `_sm64_saturn_ir_project_view` | 0.1934 | 0.2622 | **1.36** |
| `_sm64_saturn_lod_lifetime_select` | 0.0454 | 0.0605 | 1.33 |
| `_memcpy` | 0.0410 | 0.0538 | 1.31 |
| `_demo_terrain_queue_world_lower` | 0.7417 | 0.8897 | 1.20 |
| `_demo_actor_lower_compat_wrapper` | 0.2179 | 0.2424 | 1.11 |
| `_demo_actor_queue_vertex_lookup` | 0.2150 | 0.2166 | 1.01 |

The master's heaviest symbols move far less, and in the same direction only
where they stream: `_memset` 1.34x, `_terrain_depth_bins_scatter` 1.21x,
`_memcpy` 1.17x, `_demo_render_finalize` 1.08x.

---

## 5. Re-pricing the slave block

### 5.1 The window survives almost intact

| | caches off | caches on |
| :--- | ---: | ---: |
| master work while slave idle | **6.1095 VB/frame** | **6.0632 VB/frame** |
| as a share of the frame | 68.41% | 62.80% |

**99.24% of the 6.11 VB overlapping window survives in absolute VB/frame
terms.** The share falls only because the frame itself got 8.11% longer. The
block T2.25 declined to price is still there and is still the largest thing in
the frame.

### 5.2 What it now costs to move work into it

The tax is not on the window; it is on the transfer. Combining 4.3:

- work kept on the master costs **1.0811x** under cache emulation;
- work resident on the slave costs **1.2730x**;
- so a unit of work **moved** from master to slave arrives inflated by
  **1.2730 / 1.0811 = 1.1776x** relative to leaving it where it is.

The frame equals the master's work, so moving `X` VB off the master shortens
the frame by `X` **provided the slave still retires in time**. The slave's
headroom is 6.0632 VB of *slave* time, which absorbs
`6.0632 / 1.1776 = 5.15 VB` of master-equivalent work. T2.25's census found
only ~0.4 VB genuinely eligible. **Capacity was never the binding constraint
and still is not; dependency is.** That was an assumption before this task and
is a measurement now.

### 5.3 The one eligible candidate, re-priced

T2.25's census named the terrain depth-bin merge as the largest genuinely
eligible item. Re-measured on both bases (master-side, inside the slave-idle
window):

| | caches off | caches on |
| :--- | ---: | ---: |
| `_sm64_saturn_terrain_depth_bins_scatter` | 0.1867 | 0.2253 |
| `_sm64_saturn_terrain_depth_bins_digit.isra.0` | 0.1805 | 0.1880 |
| **total** | **0.3672 VB** | **0.4133 VB** |
| frame if removed from the master path | 8.5638 VB -> 7.0063 FPS | 9.2419 VB -> 6.4921 FPS |
| **predicted gain** | **+4.29%** | **+4.47%** |
| slave-time cost of hosting it (x1.1776) | — | 0.487 VB |
| slave idle remaining | — | 5.58 VB |

**The candidate's relative payoff is essentially unchanged by cache emulation**
(+4.29% -> +4.47%), and it still fits in the slave's headroom with 5.58 VB to
spare. **The coherency tax we have been warning about does not kill this
item.** Both figures are estimates from a symbol census, not cadence
measurements, and both remain upper bounds for the reason in section 6.

### 5.4 What is not claimed

- The 1.1776x transfer factor is **inferred**, not a controlled experiment. It
  compares two different code bodies (the master's simulation/emission tail
  against the slave's transform/lower jobs) under the same basis change. A real
  offload would have to be built and measured to confirm it.
- Symbol-level deltas below ~0.05 VB/frame are **inside window-phase noise**.
  The window covers 1.83 frames, so the partial frame at each end contains
  different fractions of each stage depending on where the window opened. Only
  the aggregates (4.3) and the large movers are trustworthy at symbol level.
  Several master symbols show small *decreases* under caches-on, which cache
  emulation cannot cause; those are the phase artefact, not a finding.

---

## 6. Bus arbitration is still not modelled — checked specifically

The brief asked this explicitly, and the answer is **no, and cache emulation
does not change it.** Two independent confirmations:

1. **`Bus::GetAccessCycles`**
   (`libs/ymir-core/include/ymir/sys/bus.hpp:317-329`) is a static per-page
   table lookup: address -> `MemoryPage` -> one of six fixed cycle counts. It
   has no parameter for, and no knowledge of, which CPU is asking or whether
   the other one is mid-access.
2. **`Saturn::StepMasterSH2Impl`** (`saturn.cpp:610-616`) runs the master, then
   advances the slave by exactly the master's cycle count
   (`slaveSH2.Advance<debug, enableSH2Cache>(masterCycles, ...)`). There is no
   arbitration step between them on either template instantiation. The
   `enableSH2Cache` parameter changes only which `Step`/`Advance`
   instantiation is called, not the interleaving.

A tree-wide grep for `arbitr` / `contention` in `ymir-core` returns two hits,
neither relevant: a comment about MIDI sysex, and `ConfigureAccessCycles`'s own
TODO list, which names **"VDP1 VRAM drawing contention"** among four
unimplemented items (`saturn.cpp:711-716`).

**So the position is now precise instead of vague.** Before this task, slave
offload figures were upper bounds because two mechanisms were unpriced. Now
**coherency is priced and contention is not**, and the remaining error has one
name, one location, and a known sign: HWRAM bus contention between two SH-2s
taxes slave-resident work and not master-resident work, so a measured offload
gain is still an over-estimate — by an amount smaller than before, but still
unbounded from below.

### 6.1 And the -7.50% is a floor, not the real cost

Caches-on is **more honest and still optimistic**. Two specific reasons, both
in the emulator source:

1. **Line fills are priced as a single bus access.** `SH2::AccessCycles`
   carries `// TODO: stall bus for 4 accesses` at exactly the miss arm. The
   functional path in `MemRead` does perform four 32-bit bus reads to fill the
   16-byte line (`sh2.cpp:668-674`), but the *timing* path charges one access.
2. **The active access-cycle table was calibrated for caches-off.**
   `ConfigureAccessCycles` sets High Work RAM to
   `SetAccessCycles(0x600'0000, 0x7FF'FFFF, 2,2,2,2,2,2)` — the argument order
   is `(start, end, r8, w8, r16, w16, r32, w32)`, so **2 cycles for every
   access width**. Immediately below it sits a commented-out table the source
   says "pass misctest, but are too slow in practice due to inaccurate SH2
   cache, write buffer and BSC emulation", which would make the same region
   `8,8,8,8,16,16`.

Combining the two, cheaply and without changing anything: a HWRAM cache miss is
currently charged **2 cycles**, where the hardware-calibrated table would charge
**16** for a 32-bit access, and a full four-longword line fill would approach
**64**. The modelled miss penalty is therefore roughly **one to one and a half
orders of magnitude light**.

**Do not read this as "the real cost is 32x -7.50%."** The miss *rate* is
modelled correctly and most accesses hit, so the aggregate error is far smaller
than the per-miss ratio. The defensible statement is the directional one:
**-7.50% FPS and +27.30% slave-work inflation are lower bounds on the real
coherency cost.** Switching to the realistic table and re-measuring is a
separate question and was deliberately not attempted here; it would change
every region at once and invalidate every existing baseline.

---

## 7. T2.25's coherency fix, now that the mechanism is simulated

`f25206c7` adds one instruction — `graph = graph_cache_through(graph)` — at the
top of `sm64_saturn_render_job_graph_propagate_failures()`, so that
`graph->count`, `graph->queue` and `graph->dependency_mask[]` are read through
P2 by both SH-2s instead of through each CPU's own cached alias.

Both sealed builds exist, so this is a direct A/B with no rebuild:
`id-c0352f297034f653` is **pre-fix**, `id-49894e8e2d3ea415` is **post-fix**.

| Build | basis | FPS mean | median | 1% low | VB/frame | interval distribution | pgd |
| :--- | :--- | ---: | ---: | ---: | ---: | :--- | :--- |
| `id-c0352f29` pre-fix | off | 6.7181 | 6.6667 | 6.0 | 8.9310 | `{7:1, 8:2, 9:24, 10:2}` | `1` x29 |
| `id-49894e8e` post-fix | off | **6.7181** | **6.6667** | **6.0** | **8.9310** | **identical** | `1` x29 |
| `id-c0352f29` pre-fix | **on** | 6.2143 | 6.0000 | 5.4545 | 9.6552 | `{8:2, 9:7, 10:19, 11:1}` | `1` x29 |
| `id-49894e8e` post-fix | **on** | **6.2143** | **6.0000** | **5.4545** | **9.6552** | **identical** | `1` x29 |

**No measurable difference — and this time that is a result rather than a
limitation.** Both identity probes MATCH; both queue-health blocks are
identical (`qn = qr = 30`, `qw = qf = qq = 0`, `qm = [0,0,0,0]`,
`qs = [1,1,1,1]`, `master_failures = slave_failures = 0`).

Two reasons, and both are worth recording:

1. **The hazard the fix guards against cannot fire on this route.**
   `propagate_failures()` only quarantines *dependents of a failed producer*,
   and `master_failures = slave_failures = 0` across all 30 events on every
   run. With no failure to propagate, a stale `count` or `dependency_mask` read
   produces the same answer as a fresh one. Cache emulation makes the stale
   read *possible*; it does not make it *consequential* on a route with no job
   failures.
2. **The cost is below the instrument.** The fix moves a handful of reads per
   call from P0 to P2. Under caches-off both cost 1 cycle; under caches-on a P0
   hit costs 1 and the P2 read costs 2 — a few cycles per frame against a
   4.32-million-cycle frame.

**So the fix remains justified by source-level correctness and by its gate**
(`verify_dual_cpu_coherency.py --graph-source`, which fails on the pre-fix
source), **not by measurement.** That was already T2.25's position. What has
changed is that it is now a *tested* claim rather than an untestable one: the
mechanism is simulated, the route was run on both builds, and the difference is
genuinely zero rather than merely unobservable. Turning this into a behavioural
test would need a route that generates a job failure, and this route does not.

---

## 8. Wall-clock cost

| Capture | bound by | caches off | caches on | delta |
| :--- | :--- | ---: | ---: | ---: |
| `capture_idle_attribution` (6,000,000 master instructions) | instructions | **11m 47.4s** | **11m 40.1s** | **-1.0%** |
| `capture_sourceboot_throughput`, `id-c0352f29` (30 events) | guest VBlanks | 38.0s | 42.0s | +10.4% |
| `capture_sourceboot_throughput`, `id-49894e8e` (30 events) | guest VBlanks | 37.1s | 42.0s | +13.2% |

**Cache emulation is close to free on the host, and the two rows say why.**
Host cost scales with *instructions emulated*, not guest cycles. The idle
instrument executes a fixed instruction budget, so it costs the same either way
(the -1.0% is run-to-run noise). Event-counted captures cost more only because
a slower guest needs ~8% more emulated guest time to reach the same 30
presentation events; normalised per emulated VBlank the overhead is **+7.2%**
(40.5 -> 43.4 ms/VBlank on the `id-c0352f29` pair, including its 681/688-VBlank
BIOS handoff).

**This is not prohibitive and should not be treated as a spot-check-only
instrument.**

---

## 9. Recommendation: when to use this basis

1. **Keep the default off, permanently.** Not for cost — for comparability. The
   accepted cadence baselines (5.3538, 6.7181) are quoted across the whole
   sprint and every reproduction instruction in `docs/saturn/evidence/reports`
   assumes them. Section 3 shows the new binary preserves them bit-identically;
   that property is worth more than the accuracy.
2. **Turn it on for every decision that moves work across the SH-2 boundary.**
   That is the whole class this basis exists for: slave offload, job-graph
   widening, changing which lane owns a buffer, adding or removing a
   cache-through alias, and any sort or merge whose input crosses CPUs. On the
   caches-off basis those changes are priced at zero and a regression can
   report as a win. **Run both bases and report both.**
3. **Also turn it on for anything memory-streaming, even master-only.** The
   census shows `_memset` at 1.34x, `_memcpy` 1.17x and
   `_terrain_depth_bins_scatter` 1.21x on the master. A change that trades
   arithmetic for a bigger working set is under-priced on the default basis
   too — a smaller effect than the slave case, but the same sign.
4. **Never mix the two in one table without labelling.** Caches-on is a
   different basis, not a better measurement of the same thing. A caches-on
   figure must never be compared against an accepted caches-off baseline to
   claim a regression or a win.
5. **Keep quoting offload numbers as upper bounds, now for one named reason.**
   Coherency is priced; **HWRAM bus contention is not** (section 6), and the
   modelled miss penalty is itself conservative (section 6.1).
6. **The slave lever is not blocked by the coherency tax.** The tax is real
   (+27.30% on slave-resident work) but the transfer differential is ~1.18x,
   and the one eligible candidate keeps its payoff (+4.29% -> +4.47%). If the
   slave work is reopened, this is the basis to reopen it on.

---

## 10. Honesty — deviations, gaps and what is still unknown

- **Warm-up was `--warmup-ticks 180`, not 150.** Deliberate, and stated so the
  reader can discount it. Deliverable 3 required re-pricing the **6.1095 VB**
  window, which is T2.20's and T2.25's **tick-180** figure; measuring at any
  other tick would have made the comparison unsound. T2.20 section 8 point 4
  explicitly records that "a tick-180 warm-up (used here) spans to 317 and is
  equally safe", and 180 clears the >=150 standard. **A tick-150 pair was not
  run.** Cadence is unaffected — `capture_sourceboot_throughput` is
  event-counted and T2.20 section 8 says not to move its default, so all four
  cadence runs cover route ticks 0-31 as every accepted baseline does.
- **One window per basis, not a repeat capture.** Same design as T2.16 and
  T2.20. The caches-off run agrees with the T2.20 reference to four decimals
  (section 3), which is a strong cross-binary check but is not a variance
  estimate.
- **Symbol-level deltas under ~0.05 VB/frame are noise** — see 5.4. Some master
  symbols appear to get *cheaper* under caches-on, which is impossible; that is
  the 1.83-frame window phase, not a measurement.
- **The 1.1776x transfer factor is an inference from two different code
  bodies**, not a controlled move of the same work between CPUs.
- **Cache emulation was not validated against hardware.** Nothing here says
  Ymir's cache model is *right*; it says it is *present*, and section 6.1 gives
  two specific reasons to believe it is conservative.
- **The realistic access-cycle table was deliberately not enabled.** It would
  change every region at once and invalidate all baselines; it is a separate
  question.
- **No behavioural difference between the pre- and post-fix builds was
  demonstrated**, only the absence of one on this route, with the reason given
  (section 7). Producing one needs a route that generates a job failure.
- **The two trees were committed separately, and deliberately so.** The
  emulator change is `ymir-agent` `cbd87caa` ("feat: expose SH-2 cache
  emulation on ymir-headless"); the port-tree change is `4a6f3127`
  ("feat(tools): capture on the SH-2 cache-emulation basis, and re-price the
  slave block"). No commit mixes them. The port-tree change is tooling and
  evidence only and changes no product behaviour, so it carries
  `SKIP_CHANGELOG=1`.

---

## 11. Reproduction

```bash
# T2.26 binary -- note this is NOT the binary prior captures used
YMIR=<abs>/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe
#   sha256 3156bea3f416ff98d061a2cdc81461dfbeb26da6674dbba211868d0d1677f98b
# prior binary, preserved byte-identical alongside it:
#   .../ymir-headless-pre-t2_26-nocache.exe
#   sha256 fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943

IPL="<abs>/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin"
NM=<abs>/work/yaul-install/bin/sh-elf-nm.exe          # NOT under the worktree
B=<abs>/.worktrees/saturn-recovery/releases/2026-08-16_t2_17-product/id-c0352f297034f653
C=<abs>/.worktrees/saturn-recovery/releases/2026-08-17_t2_25-product/id-49894e8e2d3ea415

# Rebuild ymir-headless (incremental, <1 min)
cmake --build <abs>/ymir-agent/build-agent2 --config Release --target ymir-headless -- -m

# 1. Cadence, both bases, both builds (~40 s each). Drop --sh2-cache for off.
for G in "$B" "$C"; do for F in "" "--sh2-cache"; do
  python tools/saturn/capture_sourceboot_throughput.py --ymir "$YMIR" --ipl "$IPL" \
    --game "$G/sm64-saturn-sourceboot-e2.cue" --elf "$G/obj/sm64-saturn-sourceboot-e2.elf" \
    --release-manifest "$G/saturn-release-manifest-v1.json" \
    --startup-vblanks 4096 --max-vblanks 3600 --presentation-events 30 $F --timeout 900 \
    --output docs/saturn/evidence/reports/sprint2-t2_26-throughput-<tag>.json
done; done

# 2. Idle attribution, both bases (~12 min each).
#    --vblanks-per-frame MUST match the basis: 8.9310 off, 9.6552 on.
python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 180 --windows 1 --window-steps 6000000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame 8.9310 \
  --output docs/saturn/evidence/reports/sprint2-t2_26-idle-cacheoff-tick180.json

python tools/saturn/capture_idle_attribution.py --ymir "$YMIR" --ipl "$IPL" \
  --game "$B/sm64-saturn-sourceboot-e2.cue" --elf "$B/obj/sm64-saturn-sourceboot-e2.elf" \
  --nm "$NM" --warmup-ticks 180 --windows 1 --window-steps 6000000 \
  --slave-stride 16 --io-stride 20000 --vblanks-per-frame 9.6552 --sh2-cache --timeout 9000 \
  --output docs/saturn/evidence/reports/sprint2-t2_26-idle-cacheon-tick180.json
```

**Getting `--vblanks-per-frame` wrong is the one easy way to produce a wrong
number here.** It is a post-hoc scaling constant, and the caches-on cadence
(9.6552) must come from a caches-on cadence run.

---

## 12. References

| Tree | State | Files inspected | Purpose |
| :--- | :--- | :--- | :--- |
| `ymir-agent` (the rig) | read at `bf3e4a4a`, changed in `cbd87caa` | `libs/ymir-core/src/ymir/sys/saturn.cpp:140-170,171-200,600-670,698-760,790-810`; `libs/ymir-core/src/ymir/hw/sh2/sh2.cpp:620-745,768-840,955-1010`; `libs/ymir-core/include/ymir/sys/saturn.hpp:185-240,400-430`; `libs/ymir-core/include/ymir/sys/bus.hpp:296-330`; `libs/ymir-core/include/ymir/hw/sh2/sh2_cache.hpp:1-60`; `libs/ymir-core/include/ymir/core/configuration.hpp:65`; `apps/ymir-headless/src/*` | establish what cache emulation does and does not model; add the headless option |
| this worktree | `f93bb4bf` | `docs/saturn/evidence/reports/sprint2-t2_16-idle-attribution.md` (s7, s9); `sprint2-t2_20-scene-representativeness.md` (s8, s10); `sprint2-t2_25-slave-dispatch.md` (s2-s5) | the claims this task tests |
