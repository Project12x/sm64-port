# Segment addressing decision: keep identity mapping, add fixed-VMA level overlays

**Status:** Recommendation, pending owner acceptance.
**Date:** 2026-07-26
**Branch at time of writing:** `saturn/bootstrap` @ `63bb9bd`
**Scope:** Decide whether the Saturn port restores real N64 segment semantics
(`sSegmentTable` + `segmented_to_virtual`) or keeps `NO_SEGMENTED_MEMORY=1` and
solves data residency another way. Blocks two parallel sub-projects: textures
(A) and CD streaming (B).

**Investigation only.** No source file was modified in producing this note.

---

## 0. Summary

**Recommendation: keep `NO_SEGMENTED_MEMORY=1`. Solve residency with
fixed-VMA overlay slots in the 4 MiB DRAM cart, each slot's contents linked at
its slot address so native pointers stay correct without runtime translation.**

The single strongest reason: **the binding constraint is residency, not
addressing.** BOB's level-specific data is a measured **87,018 bytes — 4.24% of
the 2,053,952-byte cart image** (§4.2). The other 95.8% is Mario animation data,
actor model banks, and shared texture/skybox banks. Segments were designed to
solve a problem — "the level data must move at runtime, so pointers cannot be
absolute" — that a fixed-address 4 MiB DRAM window does not have. The cart is
contiguous, always at `0x22400000`, and writable. Data loaded to a fixed slot
address has correct absolute pointers by construction, at zero runtime cost.

Restoring segments would buy runtime relocatability the target does not need,
and would cost: 6 pointer-decode sites in the Fast3D frontend, the entire
build-time quad map (which keys on link-time display-list addresses and would
silently stop matching), a per-segment link topology the Saturn linker script
does not have, and a `dma_read` backend that does not exist. It buys nothing
that a fixed slot address does not already give.

---

## 1. Method and verification posture

Every budget figure below was measured by this investigation against the tree
as it stands, not repeated from a prior document. Measurement commands are
cited inline. No build was run and no emulator capture was taken; all ELF
figures come from the **existing** artifact at
`build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf` (linked
2026-07-26 09:26) read with the pinned Yaul `sh-elf` binutils.

Two classes of number are distinguished throughout:

- **Measured** — read directly from the ELF, its map file, the on-disc
  `SOURCE.DAT`, or a struct definition in the tree.
- **Calibrated estimate** — derived from measured data by a stated ratio, and
  labelled as such. Used only for "what would all levels cost", which cannot be
  measured without building targets that do not exist yet.

One figure quoted in the task brief — "roughly 124 KiB free after registering 47
models" — is a **runtime** value of `sPoolFreeSpace`. It could not be verified
without a capture, which this task was told not to run. It is treated as
unverified throughout; §4.4 gives the deterministic bounds instead. See
Open Question O1.

---

## 2. How segments work in this tree today

### 2.1 The switch

`src/port/saturn/sourceboot/Makefile:96` compiles the sourceboot target with
`-DNO_SEGMENTED_MEMORY=1` (castleviewer does the same at
`src/port/saturn/castleviewer/Makefile:29`).

Under that define:

- `src/game/memory.c:139-141` — `segmented_to_virtual()` returns its argument
  unchanged.
- `src/game/memory.c:143-145` — `virtual_to_segmented()` returns its argument
  unchanged, ignoring the segment id.
- `src/game/memory.c:147-148` — `move_segment_table_to_dmem()` is a no-op.
- `src/game/memory.c:393-487` — the entire segment-loading block
  (`load_segment`, `load_to_fixed_pool_addr`, `load_segment_decompress`,
  `load_segment_decompress_heap`, `load_engine_code_segment`) is compiled out.
- `src/game/memory.h:79-84` — those five functions are replaced by **empty
  variadic macros**, so every call site expands to nothing. This is why
  `levels/bob/script.c:60-68`'s eight `LOAD_MIO0`/`LOAD_RAW` commands are
  harmless: `level_cmd_load_mio0` (`src/engine/level_script.c:282-285`) is still
  in the dispatch table at `level_script.c:817`, still executes, but its body
  reduces to `sCurrentCmd = CMD_NEXT;`.
- `include/level_commands.h:37-48, 135-150, 172-182` — `EXECUTE`,
  `EXIT_AND_EXECUTE`, `FIXED_LOAD`, `LOAD_RAW`, `LOAD_MIO0`, and
  `LOAD_MIO0_TEXTURE` emit `NULL` for every ROM pointer and `0x0000` for the
  segment id. The commands are shape-preserved but semantically inert.
- `include/segment_symbols.h:4-93` — the whole file becomes empty. No
  `_xxxSegmentRomStart` / `_xxxSegmentRomEnd` symbols are declared or required.
- Three places gain *extra* work rather than losing it, because identity mapping
  means level data is no longer a private copy: `level_cmd_set_terrain_data`
  (`level_script.c:632-646`) and `level_cmd_set_macro_objects`
  (`level_script.c:656-670`) `memcpy` the data into the level pool, because the
  game mutates it in place; `src/engine/surface_load.c:566` adds
  `get_area_terrain_size()` to size that copy.

`sSegmentTable[32]` (`src/game/memory.c:98`) still exists and
`set_segment_base_addr` / `get_segment_base_addr` (`memory.c:108-115`) are still
compiled, but under `NO_SEGMENTED_MEMORY` nothing ever writes the table and
nothing ever reads it.

### 2.2 What survives unchanged

Critically, **the engine tree is already segment-correct**. There are **201
`segmented_to_virtual()` call sites** across `src/`, `levels/`, `actors/`, and
`include/` (measured: `grep -rc segmented_to_virtual --include=*.c --include=*.h
src/ levels/ actors/ include/`, excluding `src/port/`). Every one of them
compiles under either mode and does the right thing under either mode. The
engine's 11 sites in `src/engine/level_script.c` alone
(`level_script.c:103,116,150,155,199,208,633,639,650,658,662`) cover script
control flow, terrain, rooms, and macro objects.

This is the important asymmetry: **turning segments back on requires no engine
change at all.** The standing discipline of leaving `src/engine/`, `src/game/`,
`levels/`, `actors/`, and `include/` unmodified is not what blocks option A.
What blocks it is on the *port* side and the *build* side (§3, §6.1).

### 2.3 What "restoring segments" would actually require

`segmented_to_virtual` (`memory.c:118-123`) computes
`sSegmentTable[addr >> 24] + (addr & 0x00FFFFFF)`. For this to work, the *stored
pointer must already be segment-relative* — i.e. `0x07xxxxxx` for level data,
`0x0Exxxxxx` for scripts, and so on.

In this tree, level data is C source arrays (`levels/bob/leveldata.c:1-30`
`#include`s twenty-odd generated `.inc.c` files). The linker assigns them real
addresses in `.cart_rodata` at `0x224xxxxx`. There is no mechanism producing
`0x07xxxxxx` pointers. On N64 that came from linking each segment as its own
output section at the segment's base VMA. Restoring it on Saturn means
introducing per-segment output sections and per-segment link products — a build
topology `src/port/saturn/sourceboot/sourceboot-cart.x` does not have and
`Makefile.saturn.mk` does not produce.

So option A is not "flip a define". It is "rebuild the asset link topology,
then flip a define, then repair the port-side consumers".

---

## 3. What currently depends on identity mapping

The engine does not (§2.2). The Saturn port does. Enumerated exhaustively:

### 3.1 Fast3D frontend — 6 raw-pointer dereference sites

All in `src/port/saturn/gfx/saturn_fast3d_frontend.c` (line numbers as of the
current working tree, which has uncommitted changes in this file from another
agent — verify before acting):

| # | Line | Site | What it dereferences |
|---|------|------|----------------------|
| 1 | `:1328` | `command = (Gfx *)task->task.t.data_ptr;` | root display list from the SPTask |
| 2 | `:1343` | `Gfx *target = (Gfx *)(uintptr_t)command->words.w1;` | `G_DL` call/branch target |
| 3 | `:820` | `sm64_saturn_matrix_decode_q16((const int32_t *)(uintptr_t)w1, …)` | `G_MTX` 64-byte Q16.16 payload |
| 4 | `:822` | `const float *gbi_floats = (const float *)(uintptr_t)w1;` | `G_MTX` float payload (non-Saturn path) |
| 5 | `:940` | `const Vp_t *vp = (const Vp_t *)w1;` | `G_MOVEMEM` / `G_MV_VIEWPORT` |
| 6 | `:1050` | `memcpy(raw, (const void *)w1, sizeof(raw));` | `G_MOVEMEM` / `G_MV_LIGHT` light payload |
| 7 | `:1136` | `const Vtx_t *src = (const Vtx_t *)w1;` | `G_VTX` vertex array |

(Seven rows; #3 and #4 are the two arms of one `#ifdef`, so **six distinct
runtime sites**.) Under real segments each would need a
`segmented_to_virtual()` wrapper. The file's own comment at `:1116-1119`
records the current contract explicitly: *"w1 is a raw pointer (this port's
display lists reference final-linked addresses, not N64 segments…)"*.

Six wrappers is not a large change. The next item is.

### 3.2 The build-time quad map — a hard dependency, not a wrapper

`saturn_fast3d_quad_map_bind()` (`saturn_fast3d_frontend.c:201-241`) binds the
per-display-list quad table by **pointer identity**:

```c
if (sm64_saturn_quad_map_lists[i].display_list == list) {   /* :223 */
```

`sm64_saturn_quad_map_lists[]` is generated by `tools/saturn/quad_map.py` into
`build/saturn/sourceboot/generated/saturn_quad_map.c`, which takes the address
of link-time symbols (`extern const Gfx mario_butt_dl[];` etc.). Under real
segments, `list` at runtime is a *pool* address produced by `load_segment()`,
which changes with pool state. The comparison would never match. The quad map
would silently disable — no fault flag, no counter, every merged VDP1 quad
degrading back to two triangles.

Fixing this under option A means the quad map must key on
`(segment_id, offset)` rather than address, which means the generator must know
each display list's segment-relative offset, which means it must run *after* the
per-segment link — a new build-order dependency. This is the single largest
port-side cost of option A, and it is a correctness-silent failure mode if
missed.

### 3.3 The cart image itself

`src/port/saturn/sourceboot/source_cart.c:1-7` states the current contract in
its file header:

> `SOURCE.DAT` is the linked `.cart_rodata` section. Loading that section at
> its final `0x22400000` VMA preserves all native pointers in generated SM64
> assets; it is deliberately not an offset-package or a general heap.

`docs/saturn/ENGINE_PORT_ARCHITECTURE.md:146-163` already reasoned to the same
place, and is worth quoting because it pre-empts the whole question:

> Original SM64 source tables contain native pointers to model/display-list
> data, behavior callbacks, and animation records; an offset-only IR package
> cannot be substituted for those symbols transparently. The bootstrap therefore
> builds a separate, read-only source-data image **linked for the detected cart
> address**.

The doc frames this as "a compatibility bridge, not permission to use cartridge
DRAM as a heap". The recommendation in §7 argues the bridge is in fact the
right permanent mechanism for *original-source* data, and that the offset-based
package (`CARTRIDGE_ASSET_POLICY.md:50-51`, gate 6) remains the right mechanism
for *generated Saturn IR* — the two are not competitors.

### 3.4 Everything else

`src/port/saturn/runtime/saturn_source_runtime.c` and
`src/port/saturn/gfx/saturn_texture_residency.h` contain **no** source-pointer
decode. `saturn_texture_residency.h:21-36` is a VDP1 VRAM *destination* helper
only (`base`/`capacity`/`upload(offset,…)`); it never reads an N64 texture
pointer. This matters for sub-project A (§8).

---

## 4. Measured budgets

### 4.1 Cart

| Quantity | Value | How measured |
|---|---|---|
| `.cart_rodata` section | `0x1F5740` = **2,053,952 B** | `sh-elf-readelf -S` on the e2-bob ELF, section 3 |
| `SOURCE.DAT` on disc | **2,053,952 B** | `ls -l build/saturn/sourceboot/e2-bob/cd/SOURCE.DAT` |
| Cart region | ORIGIN `0x22400000`, LENGTH `0x00400000` = **4,194,304 B** | `sourceboot-cart.x:19` |
| Cart used | **48.97%** | 2,053,952 / 4,194,304 |
| Cart free | **2,140,352 B** (2.04 MiB) | derived |

The 4 MiB (32-Mbit) cart address space is **contiguous** `0x22400000`–
`0x227FFFFF`, per the memory map in the installed
`work/yaul-install/sh-elf/include/yaul/dram-cart.h` (the 8-Mbit cart is the
mirrored case, not the 32-Mbit one). The linker script's 4 MiB region is
therefore correct, and there is no bank-boundary hazard for a 4 MiB image.

The cart is **writable DRAM**, not ROM: `source_cart.c:126-142` writes the whole
image into `dram_cart_area_get()` with 16-bit CPU stores, and
`src/port/saturn/cartridge/saturn_cart_bank.c:38-49` exposes a general
`stage(offset, src, bytes)` write path. The `(R)` attribute in
`sourceboot-cart.x:19` is a linker annotation, not a hardware property. **This is
the structural fact the recommendation rests on.**

### 4.2 What is actually in the cart image

Summed from `sm64-saturn-sourceboot-e2.map`, `.cart_rodata` output section
(map lines 28093–36968), aggregating every `.rodata*`/`.rdata` input by
contributing object file. Accounted total 2,055,530 B vs. section size
2,053,952 B — the 1,578 B excess is inter-input alignment padding double-counted
by the naive sum; ~0.08% error.

| Category | Bytes | Share |
|---|---:|---:|
| Actor model banks (`actors/group*.o`, `common*.o`) | 1,002,490 | 48.77% |
| Mario animation data (`mario_anim_data.o`) | 580,632 | 28.25% |
| Shared texture + skybox banks (`bin/*.o`, `water_skybox.o`) | 272,940 | 13.28% |
| **Level: BOB** (`leveldata` 85,666 + `script` 1,084 + `geo` 268) | **87,018** | **4.23%** |
| Saturn port data (trig LUT, quad map, collision catalog) | 64,558 | 3.14% |
| Actor geo layouts (`*_geo.o`) | 19,312 | 0.94% |
| Level: menu | 14,448 | 0.70% |
| Level: castle_grounds | 10,856 | 0.53% |
| Level: ttc | 2,992 | 0.15% |

Two consequences:

1. **Level data is not the problem.** One complete level costs 87 KiB. Even the
   estimated worst case (§4.5) is ~0.5 MiB.
2. **Actor banks are the problem.** BOB's script
   (`levels/bob/script.c:60-68`) needs only `group3` and `group14` plus
   `common0`/`common1`. Summing the map by group symbol, the actor groups BOB
   never uses (`group1,2,4,6,7,8,9,10,11,12,13,15,16,17`) total **285,250 B —
   13.9% of the image — permanently resident and never referenced.** In retail
   that is exactly what segments 0x05/0x06 and `LOAD_MIO0` exist to avoid.

### 4.3 HWRAM

| Quantity | Value | How measured |
|---|---|---|
| `___bss_end` | `0x060DE770` | `sh-elf-nm` on the e2-bob ELF |
| `___end` (after `.uncached`, `sourceboot-cart.x:111`) | `0x060DE844` | `sh-elf-nm` |
| HWRAM top | `0x06100000` | `sourceboot-cart.x:17,134` |
| **HWRAM margin** | `0x217BC` = **137,660 B (134.4 KiB)** | `0x06100000 − 0x060DE844` |
| Link-time floor (libyaul TLSF control block) | 4,096 B | `sourceboot-cart.x:134-135` |

The brief's "roughly 137 KiB" is the byte count, not KiB: **137,660 bytes =
134.4 KiB**. A `main.c:79-86` comment cites 149,084 B measured 2026-07-24; the
current link is 11,424 B tighter. That comment's own warning applies — re-measure
after any static-HWRAM change rather than re-quoting.

### 4.4 LWRAM and the main pool

| Quantity | Value | How measured |
|---|---|---|
| LWRAM region | ORIGIN `0x00200000`, LENGTH `0x00100000` = 1,048,576 B | `sourceboot-cart.x:18` |
| `.lwram_cmdts` (VDP1 staging) | `0x10000` = 65,536 B @ `0x00200000` | `readelf -S`, section 8 |
| `.lwram_bss` (SM64 main pool) | `0x60000` = **393,216 B** @ `0x00210000` | `readelf -S`, section 9; `main.c:51-53` |
| **LWRAM free** | `0x90000` = **589,824 B (576 KiB)** | derived |
| Main pool usable | **393,184 B** | `main_pool_init` head/tail reservation, `memory.c:168-171` |

Deterministic main-pool consumers (each `main_pool_alloc` adds a 16-byte header,
`memory.c:244`):

| Consumer | Bytes | Source |
|---|---:|---|
| `gEffectsMemoryPool` (`mem_pool_init(0x4000, LEFT)`) | 16,416 | `main.c:284`; `sizeof(struct MemoryPool)` = 16 (`memory.c:77-81`) |
| Surface node pool (7000 × 8) | 56,016 | `surface_load.c:558`; `struct SurfaceNode` = 8 B (`surface_load.h:12-16`) |
| Surface pool (2300 × 48) | 110,416 | `surface_load.c:557-559`; `struct Surface` = `0x30` = 48 B (`types.h:229-246`) |
| **Collision subtotal** | **166,432** | **42.3% of the usable pool** |
| BOB terrain copy (`memcpy` into level pool) | 9,972 | `sh-elf-nm` on `_bob_seg7_collision_level` = `0x26F4`; copied by `level_script.c:639-644` |

The brief's "BOB's collision alone consumes about 166 KiB" is confirmed:
**166,432 B = 162.5 KiB**, or 42.3% of the 393,184-byte pool.

The remaining consumers — the level pool holding 47 registered model graphs plus
BOB's own models, areas, and object graph — are runtime-sized. `ALLOC_LEVEL_POOL`
(`level_script.c:350-358`) claims *all* remaining pool space, then
`FREE_LEVEL_POOL` (`level_script.c:363-368`) shrinks it to `usedSpace`. This is
why the free-space figure is a runtime observation and cannot be derived
statically. See Open Question O1.

Deterministic bound: after the effects and collision pools, **226,752 B (221.4
KiB)** of the main pool remain for all level-pool allocation. The unverified
"~124 KiB free" figure implies roughly 102 KiB of level-pool use across
`source_entry.c`'s 47 registrations plus BOB's own — consistent with the ~39 KiB
per-cycle registration cost recorded at `source_entry.c:92-95`, but not
independently confirmed here.

### 4.5 What all levels would cost — calibrated estimate

`levels/*/leveldata.c` are thin `#include` shells; their file sizes are
meaningless. Instead: sum the generated `.inc.c`/`.inc.h` bytes per level
directory and scale by BOB's measured ratio.

- BOB: 334,076 source bytes → **85,666 compiled bytes** (measured). Ratio
  **0.2564**.

| Level | Est. compiled | Level | Est. compiled |
|---|---:|---|---:|
| castle_inside | 520,739 | wf | 94,040 |
| rr | 287,716 | jrb | 86,398 |
| ttm | 246,006 | pss | 85,916 |
| hmc | 216,204 | **bob (measured)** | **85,666** |
| bbh | 205,533 | bitdw | 82,839 |
| ssl | 184,464 | thi | 78,299 |
| ccm | 169,814 | sl | 75,752 |
| lll | 157,711 | castle_grounds | 71,993 |
| bits | 146,666 | ddd | 70,951 |
| wdw | 125,409 | cotmc | 49,919 |
| ttc | 121,089 | vcutm | 44,317 |
| bitfs | 116,306 | castle_courtyard | 39,597 |
| wmotr | 95,976 | totwc | 34,844 |
| | | bowser_1/2/3, sa | 4,659 / 7,305 / 21,059 / 17,928 |

**Total across all 30 level folders: ≈ 3,545,000 B.** Largest single level:
castle_inside at ≈ 520,700 B.

Two conclusions, both load-bearing:

1. **All levels cannot be simultaneously resident.** 3.55 MB of level data plus
   1.26 MB of shared data exceeds the 4 MiB cart. Some form of on-demand
   residency is unavoidable for the stated goal.
2. **Any single level fits comfortably.** The worst case is ~0.5 MiB against
   2.04 MiB of free cart today. One-level-resident is not tight; it is
   generous.

The estimate is crude (the ratio conflates textures, geometry, and collision,
which compress differently from C source to object bytes). Treat ±30% as the
usable confidence band. It is sufficient to answer the only question it is being
asked: does one level fit, and do all levels fit? Yes, and no.

---

## 5. MIO0

**There is no runtime MIO0 decompressor in this tree, and none is needed.**

- `src/game/decompress.h:4` declares `void decompress(void *mio0, void *dest);`.
  **`src/game/decompress.c` does not exist.** The only definition anywhere is
  `asm/decompress.s` — MIPS assembly, unbuildable for SH-2 and not in any
  Saturn `SH_SRCS` list.
- Every caller (`memory.c:450,468`, `level_script.c:283,328`,
  `game_init.c:713`, `level_update.c:1254-1263`) is inside a
  `#ifndef NO_SEGMENTED_MEMORY` block or behind the empty macros at
  `memory.h:81-82`. Nothing references `decompress` in the Saturn link — which
  is why the missing definition is not a link error.
- **Assets in this tree are already expanded.** Textures are PNG sources
  converted to `.inc.c` arrays by `n64graphics` at build time (`Makefile:679-681`);
  geometry and collision are generated `.inc.c` arrays. `.cart_rodata` is a
  linked section, not an archive. Nothing on the Saturn path is compressed at
  any point.
- Host-side MIO0 exists and is built: `tools/libmio0.c` (`mio0_decode` at
  `:149`, `mio0_encode` at `:196`), wired into `tools/Makefile:10,27-28`, used
  only under `Makefile:697 ifeq ($(TARGET_N64),1)`.

**Cost of shipping pre-decompressed instead of compressing:** already paid, and
already measured — the 2,053,952-byte image *is* the uncompressed form. Retail's
MIO0 typically achieves roughly 2:1 on SM64 level data, so compressing would
buy on the order of 1 MiB of cart/CD at the cost of writing and validating an
SH-2 MIO0 decoder plus a decompression scratch buffer.

**Do not do this yet.** The cart is 49% full, the estimated all-levels figure
(§4.5) fits in the streaming budget without compression, and every byte of
decompression is CPU time on a target already running ~1 fps
(`main.c:363-369`). If cart pressure ever becomes real, `tools/libmio0.c`'s
`mio0_decode` is MIT (queueRAM, `tools/sm64tools.LICENSE`) and directly
portable — a ~60-line straight-line decoder, no allocation. Record it as the
designated fallback, not the plan.

---

## 6. The options

### Option A — Restore real segment semantics

Undefine `NO_SEGMENTED_MEMORY`; make `sSegmentTable` live; restore
`load_segment*`; give `dma_read` a Saturn backend that reads from cart or CD.

**Cost**

- **Build topology, the dominant cost.** Level and actor data must be linked
  into per-segment output sections at segment base VMAs (`0x07000000`,
  `0x05000000`, …) so their internal pointers are segment-relative. That means
  one link product per segment per level, plus a manifest, plus a new step in
  `Makefile.saturn.mk`. `sourceboot-cart.x` has no such sections.
- **`dma_read` backend.** `memory.c:354-372` is `memcpy` on non-N64.
  `load_segment_decompress` reads a decompressed size from an MIO0 header
  (`memory.c:444`) — under a no-compression scheme this path needs replacing,
  not just re-enabling.
- **6 frontend wrapper sites** (§3.1). Genuinely cheap.
- **Quad map redesign** (§3.2). Not cheap, and its failure mode is silent.
- **Pool pressure.** Segment loading allocates from the main pool
  (`load_segment` → `dynamic_dma_read` → `main_pool_alloc`). BOB alone would
  need ≈ 87 KiB of level data plus ≈ 196 KiB of group3/group14 actor data
  resident in a pool with a measured 226,752 B free before the level pool
  (§4.4). **It does not fit.** The pool would have to move out of LWRAM's 384
  KiB into something larger, or segments would have to point into cart memory —
  at which point the runtime translation buys nothing over a fixed address.
- **Per-frame indirection.** Every one of the 201 engine call sites goes from a
  register move to a shift, mask, table load, add, and or. On a 1 fps SH-2 build
  this is not free.

**Unlocks:** retail-faithful `LOAD_MIO0`/`LOAD_RAW` semantics; unmodified
retail level scripts drive residency directly with no new manifest format;
runtime relocatability (data can live anywhere).

**Forecloses:** nothing structurally, but the quad map and any future
pointer-keyed build-time table become second-class.

**Sub-project impact:** textures must call `segmented_to_virtual` on every
`G_SETTIMG` payload and must not cache raw addresses across a segment reload.
Streaming inherits retail's load granularity, which is per-segment and coarse —
a good fit.

### Option B — Keep identity mapping; fixed-VMA overlay slots (recommended)

Partition the 4 MiB cart into fixed, named slots at build time:

```
0x22400000  shared         — mario_geo/anim, common0/1, segment2, effect, generic
                             (1,262,452 B measured) + saturn port data
                             (64,558 B) = 1,327,010 B
            level slot     — one level's leveldata + script + geo    (sized ≈ 550,000 B)
            actor slot 0   — one actor group                         (sized ≈ 150,000 B)
            actor slot 1   — one actor group                         (sized ≈ 150,000 B)
            skybox slot                                              (sized ≈ 140,000 B)
            —— committed 2,317,010 B of 4,194,304 B ——
            ≈ 1,877,000 B (1.79 MiB) spare, enough for double-buffered
            background loads or a third actor slot
```

Each slot's contents are linked **at that slot's address** in a separate link
step, using `ld --just-symbols` against the shared image so cross-references
(a level's display list naming a shared texture, a geo layout naming a shared
model) resolve to the shared image's real addresses. The result is a
position-*dependent* blob whose every internal pointer is already correct for the
one address it will ever be loaded to. `segmented_to_virtual` stays the identity
function and does zero work.

Slot sizes derive from §4.2/§4.5: shared 1,262,452 B of asset data measured
(plus 64,558 B of Saturn port data currently in the same section); level slot from
castle_inside's ≈ 520,700 B estimate; actor slots from `group14`'s measured
149,648 B (the largest non-Mario group; `group0` is Mario and stays shared);
skybox from `water_skybox`'s measured 131,392 B.

**Cost**

- A per-slot link step and a manifest describing which blobs a level needs.
  Mechanically similar to option A's per-segment link, but with **no runtime
  translation and no engine-side change at all**.
- A `.DAT`-per-blob or indexed-archive layout on the CD, and a loader that
  reads a named blob into a slot. `source_cart.c:82-145` already does exactly
  this for one blob; generalizing it to N named blobs is small.
- The level-script `LOAD_MIO0` commands stay inert. Residency is driven by the
  port's manifest, not by the level script — a deliberate divergence from
  retail that must be documented per-level.
- One level, two actor groups, one skybox resident at a time. Enough for every
  retail level (each `levels/*/script.c` loads exactly two groups plus commons).
- `ld --just-symbols` against an ELF is standard binutils and is available in
  the pinned `sh-elf` toolchain, but **has not been exercised in this tree**.
  See Open Question O2.

**Unlocks:** arbitrary levels with zero change to `src/engine/`, `src/game/`,
`levels/`, `actors/`, `include/`; the quad map keeps working by pointer identity
(the generator just emits one table per slot occupant, selected at load time);
the frontend's six deref sites need no wrappers; no per-frame indirection cost;
reclaims the 285,250 B of unused actor groups measured in §4.2.

**Forecloses:** true runtime relocatability. Two levels can never be resident in
the same slot. Data that must be resident concurrently and was not planned for
needs a new slot — a build change, not a runtime one. This is a real
constraint, and the honest statement of it is: **this scheme trades runtime
flexibility for zero runtime cost, and the trade is only correct because the
Saturn's cart window is at a fixed physical address.**

**Sub-project impact:** see §8, §9.

### Option C — Identity mapping plus runtime relocation

Ship each blob position-independently with a relocation table (list of offsets
within the blob that hold internal pointers), emitted by a post-link tool, and
apply fixups after loading to wherever the blob lands.

**Cost:** a relocation-emitting tool (parse the blob's ELF relocations before
`objcopy` strips them, filter to internal-only, emit an offset list); a runtime
fixup pass, O(pointers) per load — for BOB's 87 KiB of level data that is a few
thousand word writes into cart DRAM over the slow A-bus; plus the same manifest
and loader as option B. The quad map still breaks, because load addresses vary
(§3.2).

**Unlocks:** blobs can go anywhere, so slots can be sized dynamically and
packed. Useful only if slot fragmentation ever becomes real.

**Verdict:** strictly more machinery than B for a flexibility the measured
budget does not need. Keep as the documented escape hatch if slot sizing ever
proves untenable — the tooling is additive to B, not a replacement for it.

---

## 7. Recommendation

**Adopt option B.** Keep `NO_SEGMENTED_MEMORY=1`; build per-level, per-actor-group,
and per-skybox blobs linked at fixed cart slot VMAs; load them on demand through
a generalized `source_cart.c`.

Reasoning, in order of weight:

1. **The measured shape of the problem does not match what segments solve.**
   Segments exist because N64 level data must be DMA'd from ROM into a RAM
   location the engine does not know at build time. The Saturn's 4 MiB DRAM cart
   *is* a fixed, contiguous, writable window at `0x22400000`
   (`dram-cart.h` memory map; `sourceboot-cart.x:19`). Build time already knows
   the address. Paying a shift-mask-table-load on 201 engine call sites, every
   frame, to recompute an address the linker could have written down, is
   pure loss.

2. **The pool cannot hold what segments would put in it.** §4.4 measures 226,752
   B free in the main pool after the effects and collision pools. BOB alone
   needs 283,298 B (277 KiB) of level plus actor-group data — 87,018 measured
   for the level (§4.2) plus 196,280 measured for `group3` + `group14` and
   their geo layouts. Option A therefore does not
   just cost work — as specified, on this target, **it does not fit** without
   also moving the pool, at which point the pool lives in cart memory and the
   translation is doing nothing.

3. **The engine tree stays untouched either way, so "the machinery is already
   there" is not an argument for A.** All 201 call sites already compile both
   ways (§2.2). What is *not* already there is the per-segment link topology,
   and building that is the same class of work as building option B's per-slot
   link topology — except option B's version ends with no runtime cost, no pool
   pressure, and a working quad map.

4. **The project's own architecture doc already reasoned here.**
   `ENGINE_PORT_ARCHITECTURE.md:146-163` chose "linked for the detected cart
   address" precisely because original SM64 tables carry native pointers and an
   offset-only package cannot transparently replace them. This recommendation
   generalizes that from one image to N slots rather than reversing it. The
   offset-based package of `CARTRIDGE_ASSET_POLICY.md:50-51` remains the right
   container for *generated Saturn IR*; the two coexist.

5. **It is the least reversible-decision-heavy path.** Option B's per-slot link
   and manifest are prerequisites for option C too. If slot sizing ever fails,
   C is an additive tool on top of B. If B is skipped for A, none of A's work
   carries forward to B or C.

**Argument against the recommendation, stated fairly:** option A is the only
path on which the *unmodified retail level scripts drive residency by
themselves*. Under B, `levels/*/script.c`'s `LOAD_MIO0` lines are decorative and
a parallel port-side manifest must be kept in sync with them by hand or by a
generator. That is a real, permanent divergence from retail and a real source of
future bugs (a level whose manifest omits a group it references will render
missing actors, silently). If the project's priority were fidelity of mechanism
rather than rendering arbitrary levels within measured budgets, A would win.
It is not, so it does not — but the manifest-drift risk must be mitigated by
*generating* the manifest from each level's `script.c` `LOAD_*` commands rather
than authoring it, so the two cannot disagree.

---

## 8. Sub-project A (textures): what to assume about pointers

1. **Assume raw, directly dereferenceable pointers.** A `G_SETTIMG` payload is
   the final linked address of a texture array in cart memory. Dereference it.
   Do not write `segmented_to_virtual()` wrappers "for later" — under the
   recommendation there is no later, and unnecessary wrappers on the hot path
   cost SH-2 cycles.
2. **Assume the address is stable for as long as the owning slot is resident,
   and not one instant longer.** A texture cache keyed on source address is
   correct within a level. It **must be invalidated on slot reload.** Design the
   cache with an explicit generation/epoch stamp from day one — the frontend's
   `quad_slot_generation` (`saturn_fast3d_frontend.c:211-221`) is the in-tree
   pattern to copy.
3. **Assume the source data is uncompressed** (§5). No MIO0 path, no
   decompression buffer.
4. **Assume the source lives in cart DRAM on the A-bus, not in WRAM.** This is
   the one place the texture work should *not* follow current practice:
   `CARTRIDGE_ASSET_POLICY.md:19-21` requires cart data to be staged through a
   bounded internal-WRAM ring before hot traversal, and
   `CARTRIDGE_ASSET_POLICY.md:33-36` records that castleviewer already does this
   with an 8 KiB ring. Texture uploads should follow that, not read the cart
   directly per-primitive.
5. **`saturn_texture_residency.h` is greenfield.** It currently models only the
   VDP1 VRAM destination (`base`/`capacity`/`upload(offset,…)`), with no source
   pointer decode anywhere. Nothing there needs unwinding.
6. **Widen the VDP1 texture partition first.** `main.c:240-242` passes
   `texture_size = 0`. `main.c:218-231` already documents that adding texture
   code without widening it uploads nothing silently, and that the widened size
   must be a multiple of 8 or `gouraud_base` misaligns. Read that comment before
   writing a line.

**If the decision is ever reversed to option A**, the texture work's exposure is
exactly: one wrapper at the `G_SETTIMG` decode, plus making the cache
invalidation (already required by point 2) fire on segment reload instead of
slot reload. That is a small, bounded exposure — which is itself a reason not to
block the texture sub-project on this decision.

## 9. Sub-project B (streaming): what to assume about pointers

1. **Assume load address is a build-time constant, not a runtime result.** The
   loader's contract is "put blob X at its slot address"; it never returns an
   address, never fixes up, never allocates.
2. **Generalize, do not replace, `source_cart.c`.** The existing loader
   (`source_cart.c:82-145`) already does CD-block init → CDFS filelist → named
   file lookup → 16-sector staged read → 16-bit CPU copy into cart. The
   generalization is: N named files, each with a target slot offset, and a
   size/identity check per blob. `source_cart.h`'s probe struct
   (`source_cart.c:34-44`) gives the diagnostic pattern to extend.
3. **There is no CD capability beyond the boot load today.** `cd_block_init`,
   `cdfs_init`, `cdfs_filelist_root_read`, and `cd_block_sectors_read` are all
   called exactly once, from `main()` before `thread5_game_loop`
   (`main.c:134-139`). Nothing re-enters the CD at any later point. Mid-game
   streaming is new capability, not an extension of existing capability.
4. **Assume CD reads block and are visible.** `docs/saturn/PSX_PORT_ARCHITECTURE_LESSONS.md:53-54`
   explicitly names the PS1 port's per-texture load stutter as "a failure case
   to avoid, not a compatibility target". Load at level transitions, batched per
   slot, never per-primitive. The spare ≈ 1.9 MiB of cart (§6, option B) exists
   to allow a background prefetch into a shadow slot if transitions ever need
   hiding.
5. **Emulator caveat.** `docs/saturn/YMIR_CD_BLOCK_HANDOFF.md:19-44` records that
   Ymir's CD block had `CmdCopySectorData` (0x65) and `CmdMoveSectorData` (0x66)
   commented out and required a fork (`9da9c76b` on `project12x/agent-debug-v0`)
   plus follow-ups `ef8a4e16` and `4d517116` before the disc would boot at all.
   The handoff's own framing applies: emulator evidence only; retail hardware
   is the authority. Any streaming work that goes beyond the already-proven
   boot-load command set should expect to find more gaps.
6. **Generate the manifest from `levels/*/script.c`.** Per §7's stated risk,
   parse each level's `LOAD_MIO0`/`LOAD_RAW`/`LOAD_MIO0_TEXTURE` commands to
   derive which groups and skybox that level needs, rather than authoring a
   parallel list. `tools/saturn/quad_map.py` is the in-tree precedent for a
   build-time source-parsing generator.

---

## 10. Prior-art findings

Licenses and pins verified against the working copies under `work/upstream/`.
Reuse modes are proposals for this decision; none has been enacted.

| Upstream | Pinned SHA | License | Files inspected | Relevance | Proposed reuse mode |
|---|---|---|---|---|---|
| `malucard/sm64-psx` | `3073845688ea273da78d539b20c45110d8a868c3` | **None found at the pin.** No root `LICENSE`/`COPYING`; `README.md` has no terms. Matches this repo's record at `THIRD_PARTY_LICENSES.md:94` | `Makefile:56`, `Makefile.psx.mk:83-92,451-510`, `src/game/memory.c:207-259`, `tools/makextfiles.c:12,84-108`, `src/port/psx/cd_psx.c:138-301`, `src/port/pc/cd_pc.c:9-21` | **Kept segments.** Never defines `NO_SEGMENTED_MEMORY`. Redefines "ROM address" as "offset into a sector-aligned CD blob + 4096" via linker `--defsym`, then `dma_read` issues `cd_read` at that offset. Demand-driven by unmodified level-script opcodes | **Pattern-only / behaviour lessons.** No copying permitted. Its own README reports "long stutters and loading times" — the failure mode §9.4 exists to avoid |
| `Lobotomy-Software/SlaveDriver-Engine` | `a8986591557b6e680550d3c23970284d3b38ff8f` | GPL-3.0-or-later (`LICENSE.txt:1`; `README.md:19` SPDX). Authorized for this project per `THIRD_PARTY_LICENSES.md:119-123` | `FILE.C:70-88,99-116,353-376`, `FILE.H:8-22`, `LINK.S:8-22`, `LEVEL.C:27-75`, `UTIL.C:347-395`, `INITMAIN.C:608` | **Fixed-load-address overlay is exactly option B's mechanism, shipped on retail Saturn.** `link()` reads a file and `LINK.S` blits it to hard-coded `0x06004000` and jumps — no relocations, no symbol table. Level data is index-addressed POD arrays (`LOADPART` macro, 14 uses) with **zero pointer serialization**; the only rebase is an additive `tileBase` on texture/tile IDs (`LEVEL.C:65-68`). Only one CD file open at a time (`FILE.C:99-105`) | **Pattern-only** for the overlay and index-addressing patterns. If any code is adapted, it goes in `src/port/saturn/gpl/` with GPL notices intact, per the isolation discipline at `PROVENANCE.md:419-423` (precedent: `slavedriver_dma_queue.c`) |
| `Maxime-XL2/SONIC-Z-TREME` | `cff75451c1616aac1236fc2b44223902b55c706b` | GPL-3.0 text (`LICENSE:1`) **plus a contradictory no-sale clause** in `README.md:6-12`; vendors proprietary Sega SGL/SBL. Recorded as contested at `PROVENANCE.md:382-385` | `ZTE/ZT_CD.c:3-6,47-50,116,132`, `ZT_LOADING.c:118-146,376-434`, `ZT_LOAD_MODEL.c:226-292`, `SRC/game.c:140-153` | **Third distinct pattern: header-of-counts + one contiguous arena read + pointers reconstructed by a deterministic sequential walk.** No relocation table, no persistent offsets. `GFS_Load(fid, sector_offset, address, size)` gives sector-offset random access within a file — the natural shape for a slot loader. 5 concurrent files vs SlaveDriver's 1 | **Behaviour study only** (matching the existing record). The license contradiction makes copying unsafe regardless of the project's GPL authorization |
| `queueRAM/sm64tools` (vendored) | in-tree at `tools/libmio0.c` | MIT (`tools/sm64tools.LICENSE`) | `tools/libmio0.c:130-450`, `tools/Makefile:10,27-28` | The designated MIO0 fallback if cart pressure ever becomes real (§5) | **Direct-copy available** (MIT, attribution preserved). **Not recommended now** |

Policy note: `THIRD_PARTY_LICENSES.md:119-123` authorizes GPL use for this
project provided copied/forked/close-ported code retains its GPL version,
copyright, license text, and source obligations, and records that *"tool-only
use does not impose a license on this repository's independent Saturn target."*
`PROVENANCE.md:419-423` further confines GPL-derived code to
`src/port/saturn/gpl/`. Any adoption arising from this decision must update both
`THIRD_PARTY_LICENSES.md` and `PROVENANCE.md` per the maintenance rule at
`THIRD_PARTY_LICENSES.md:165-167`.

---

## 11. Observations made in passing (not fixed, not in scope)

- **The frame loop currently chases pointers through cartridge memory.**
  `CARTRIDGE_ASSET_POLICY.md:21` states: *"No frame loop may chase pointers
  through cartridge memory for transformed geometry, sorting, collision, or
  command emission."* But `.cart_rodata` at `0x22400000` holds every display
  list, `Vtx` array, and geo layout, and
  `saturn_fast3d_frontend.c:1328-1373` walks them directly on the A-bus every
  frame. This is a pre-existing divergence between the written policy and the
  shipped implementation, not something this decision introduces — but it is
  relevant to any performance work, and it is a reason the recommendation
  routes *texture* traffic through a WRAM ring (§8.4) rather than repeating the
  pattern.
- **`levels/bob/script.c:60-68` executes eight inert segment-load commands per
  level entry.** Harmless (the bodies compile away, `memory.h:79-84`), but under
  the recommendation these lines are the authoritative source for the generated
  residency manifest (§9.6) — so they should be treated as live specification
  rather than dead code, and not "cleaned up".

---

## 12. Open questions

**O1 — The runtime main-pool free figure is unverified.** The "~124 KiB free
after registering 47 models" number originates from a comment at
`src/port/saturn/sourceboot/source_entry.c:92-95` and could not be independently
confirmed here, because it requires reading `sPoolFreeSpace` from a live run and
this task was directed not to run captures. There is also **no in-tree
instrumentation for it**: grepping `src/port/` and `tools/saturn/` for
`sPoolFreeSpace`, `main_pool_available`, and `pool_free` returns nothing, so the
figure was presumably obtained by a one-off debugger read. Recommend adding a
pool-free field to the frame profile so this becomes a captured, regression-
tracked number rather than a comment. The §4.4 deterministic bound (226,752 B
free after the effects and collision pools) is not affected by this gap and is
sufficient to support §7's argument 2.

**O2 — `ld --just-symbols` against the shared image is unproven in this tree.**
Option B's cross-blob symbol resolution depends on it. It is standard binutils
and the pinned `sh-elf` toolchain should support it, but no target here has ever
used it, and interactions with Yaul's build-path mangling
(`*sm64-port?*` wildcards in `sourceboot-cart.x:53-56`) and with `objcopy -R
.cart_rodata` are unexamined. **This should be spiked before committing to
option B** — a two-blob proof-of-concept, not a full implementation.

**O3 — Slot sizing for `castle_inside` is an estimate, not a measurement.** §4.5
puts it at ≈ 520,700 B by a 0.2564 calibration ratio derived from BOB alone. One
level is a thin calibration base, and `castle_inside`'s asset mix (many small
rooms, heavy texture reuse) may not match BOB's. If a slot is sized from this
number and the real figure is 30% higher, the slot overflows. Recommend
compiling `castle_inside/leveldata.c` alone to an object and reading its
`.rodata` size before fixing slot sizes. That is a single-file compile, not a
build of the target, so it does not conflict with concurrent work.

**O4 — Whether one level plus two actor groups is universally sufficient.** Every
`levels/*/script.c` inspected loads exactly two groups plus `common0`/`common1`,
matching retail's segment 0x05/0x06 convention — but only BOB's was read in
full. A survey of all 30 scripts should confirm before slot count is fixed. If
any level needs three, the slot table changes (cheap now, expensive later).

**O5 — A-bus read cost is asserted by project docs, not measured here.**
`CARTRIDGE_ASSET_POLICY.md:19-21` treats cart reads as needing WRAM staging, and
§8.4/§11 rely on that. No timing measurement was taken by this investigation,
and `CARTRIDGE_ASSET_POLICY.md:41` explicitly requires measuring real SCU timing
on hardware rather than inferring from WRAM. If the cart turns out to be fast
enough to traverse directly, the staging-ring requirement in §8.4 relaxes — but
do not assume it without the measurement.

**O6 — Audio and the sound-data segments were not examined.** SM64's audio banks
are segment-loaded on N64. `src/port/saturn/sourceboot/source_audio_stub.c`
stubs audio out on this target, so the question is deferred, not answered. If
audio is ever restored, its residency needs its own slot analysis, and it may be
the one consumer that genuinely wants segment semantics.
