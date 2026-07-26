# Sourceboot performance diagnosis — 2026-07-26

Target: `sm64-saturn-sourceboot-e2` (BOB, DRAM cart enabled, Ymir SDL3).
Branch: `saturn/bootstrap`. Investigation only — no source was changed.

**Headline: the dominant cost is not the cartridge. It is libgcc's generic C
soft-float (`fp-bit.c`), which absorbs ~82% of SH-2 time. The cart hypothesis is
arithmetically refuted as the primary cause and is bounded at a few percent of
the current frame.**

---

## 1. The symptom

The user ran `sourceboot` interactively for the first time and reported roughly
one frame every two seconds.

### 1.1 What was previously believed

- A "~7 FPS" figure has circulated. It came from the **castleviewer / m4**
  target, which renders a pre-baked mesh IR resident in work RAM. It shares no
  frontend, no data path and no memory layout with sourceboot. It does not
  transfer, and nothing in this document should be compared against it.
- The m4 telemetry figures "sort ≈ 101 ms, command build ≈ 63 ms" are likewise
  castleviewer numbers. Sourceboot has **no equivalent measurement at all**
  (§6).
- `docs/saturn/SEGMENT_ADDRESSING_DECISION.md` (commit `44b4f6e`) flagged that
  all SM64 `.rodata` lives in cartridge memory and that this diverges from
  `docs/saturn/CARTRIDGE_ASSET_POLICY.md`. It did not measure the cost. This
  document measures it and finds it is real but small at the current frame time.

### 1.2 The measured frame rate

**Method.** `sm64_saturn_fast3d_profile_t.frame_serial` is incremented once per
`sm64_saturn_fast3d_frontend_submit()` (`src/port/saturn/gfx/saturn_fast3d_frontend.c:1298`).
Captures record a 248-byte `probe_window` memory dump, and for the recent
sourceboot captures that window is anchored at `_sourceboot_fast3d`, whose first
member is `profile` (`saturn_fast3d_frontend.h:450`). Decoding the window
big-endian against the struct field order recovers the whole profile.

Two captures from the **same build**, with byte-identical capture configuration
(`frames` 240, `bios_input` true, `dram_cart` true, `handoff_yield` true, no
input pulses, same `.cue`) differ only in `--post-poke-frames`:

| capture | emulated video frames after poke | `frame_serial` |
| --- | ---: | ---: |
| `docs/saturn/evidence/reports/e2-sourceboot-mario-render-cost-2026-07-25.json` | 25,000 | 151 |
| `docs/saturn/evidence/reports/e2-sourceboot-mario-render-cost-ppf35000-2026-07-25.json` | 35,000 | 214 |

Δ = **63 rendered frames per 10,000 emulated video frames.**

- 10,000 video frames ÷ 59.94 Hz = 166.8 s of emulated time.
- → **0.378 rendered FPS**, i.e. **2.65 s per rendered frame**, i.e. one render
  per 158.7 video frames.

This is a differential measurement in *emulated* time between two runs of one
binary, so boot, CD load and intro cancel out. It independently confirms the
user's "one frame every two seconds" and it is the number every other figure in
this document is measured against.

At the Saturn SH-2 clock of 28.6364 MHz, 2.65 s ≈ **75.9 million SH-2 cycles per
rendered frame**. A 30 fps budget is 954,547 cycles. The frame is **~80× over
budget**.

### 1.3 The trend across sprints

`frame_serial` at `post_poke_frames = 25000`, decoded the same way. Different
builds and different scene content, so this is indicative, not controlled:

| date | capture | `frame_serial` @ ppf 25000 |
| --- | --- | ---: |
| 2026-07-22 | `e2-sourceboot-freeroam-25000` | 387 |
| 2026-07-23 | `e2-sourceboot-q16-matrix-freeroam` | 233 |
| 2026-07-24 | `e2-sourceboot-gouraud-freeroam` | 228 |
| 2026-07-25 | `e2-sourceboot-mario-freeroam` | 143 |
| 2026-07-26 | `e2-sourceboot-quadmerge-freeroam` | 143 |

The target has lost ~63% of its throughput in four days as Mario, Gouraud and
more terrain came online. Nothing here is a cliff; it is steady accumulation.

---

## 2. The sampling profile — the deciding evidence

Every capture records `registers_at_stop`, including `pc` and `pr`, at an
emulator frame boundary. Frame boundaries are asynchronous to guest code, so
each capture is one unbiased sample of where the master SH-2 was executing.
Fifty-three sourceboot captures exist; de-duplicating on `(pc, pr,
post_poke_frames)` — several captures are re-runs of the same deterministic
point — leaves **34 independent samples**.

**Method.** `sh-elf-nm -n --print-size` on
`build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf`, then binary
search each `pc` into the symbol table.

| samples | share | symbol |
| ---: | ---: | --- |
| 7 | 20.6% | `___unpack_f` |
| 5 | 14.7% | `___mulsf3` |
| 4 | 11.8% | `___divsf3` |
| 4 | 11.8% | `___pack_f` |
| 2 | 5.9% | `___floatsisf` |
| 1 | 2.9% | `___addsf3` |
| 1 | 2.9% | `___subsf3` |
| 1 | 2.9% | `___lesf2` |
| 1 | 2.9% | `___gesf2` |
| 1 | 2.9% | `__fpadd_parts` |
| 1 | 2.9% | `___ashlsi3_r0` (returning to `__fpadd_parts`) |
| 1 | 2.9% | `_create_skybox_facing_camera` |
| 1 | 2.9% | `_sm64_saturn_fast3d_resolve_triangle` |
| 1 | 2.9% | `_vdp1_sync_wait` |
| 1 | 2.9% | `_heave_ho_act_2` |
| 1 | 2.9% | `_saturn_dma_queue_init` |
| 1 | 2.9% | unresolved |

**28 of 34 samples (82.4%) are inside libgcc soft-float.** Exact binomial 95%
confidence interval ≈ **65%–93%**. Adding `_create_skybox_facing_camera` (itself
pure float math) gives 29/34 = 85.3%.

The `pr` register corroborates the call chains: soft-float samples return into
`___addsf3` (18), `___mulsf3` (8), `___divsf3` (6), and into game code —
`_set_background_music`, `_draw_skybox_tile_grid`, `_create_skybox_facing_camera`.

Two numbers matter as much as the 82%:

- **`_vdp1_sync_wait`: 1/34 (2.9%).** The CPU is essentially never waiting on
  VDP1. Fill rate / overdraw is not the bottleneck.
- **`_sm64_saturn_fast3d_resolve_triangle`: 1/34 (2.9%).** The Fast3D frontend —
  the code that walks the cart-resident display lists — is a small fraction of
  the frame.

Caveat, stated plainly: 34 samples is a coarse profile. It cannot separate 82%
from 70% or 90%. It is more than sufficient to establish which subsystem
dominates, and to rule out subsystems that score 1/34.

---

## 3. Hypotheses and verdicts

| # | Hypothesis | Verdict | Deciding evidence |
| --- | --- | --- | --- |
| 1 | A-bus / cartridge read cost is the primary cause | **REFUTED as primary** (real but small) | §4 arithmetic: explaining 75.9 M cycles/frame from ≤190 KB of cart reads would require ~1,600 cycles per longword read. Even reading the *entire* 2,053,952-byte bank every frame needs 148 cycles/longword. Frontend is 1/34 samples. |
| 2 | SH-2 cache is disabled or misconfigured | **REFUTED for code and work RAM; CONFIRMED and unavoidable for the cart** | Yaul's `cpu_cache_purge()` ends with `CE=1` (`libyaul/scu/bus/cpu/cpu_cache.c:42-56`), called from `__sys_init` (`kernel/sys/init.c:54,66`). `.text`/`.rodata`/`.data`/`.bss` are all at `0x060xxxxx` = cached partition. TW (two-way) is never set, so 4-way/4 KB. The cart *is* uncached — `0x22400000` is the cache-through partition and Yaul defines no cached CS0 alias (`scu/map.h:61`, `CPU_CACHE_THROUGH 0x20000000`, `cpu/cache.h:23-35`). But per hypothesis 1 that costs at most a few percent today. |
| 3 | Sort / command-build cost (the m4 101 ms + 63 ms figures) | **DOES NOT APPLY — and unmeasured for sourceboot** | Those figures are castleviewer's. Sourceboot has no phase timers at all (§6). The only bound available is the sampling profile: total renderer share ≈ 1/34. |
| 4 | Emulator overhead is confusing the picture | **REFUTED as an explanation** | The §1.2 measurement is a ratio of *emulated* frame counters, entirely inside guest time. Host wall-clock (15–20 min per 25,000-frame capture) does not enter it. Caveat retained in §7. |
| 5 | **libgcc `fp-bit.c` soft-float on an FPU-less SH-2** | **CONFIRMED — primary cause** | 28/34 samples (§2). Object-level proof of fp-bit in §4.1. 3,041 soft-float call sites in `.text`. |
| 6 | VDP1 fill rate / overdraw | **REFUTED** | 1/34 samples in `_vdp1_sync_wait`. |
| 7 | `-Os` on the whole SM64 engine | **CONFIRMED as a contributing cause** (magnitude unmeasured) | §4.3. |
| 8 | Double-precision promotion in SM64 math | **CONFIRMED as a contributing cause** (magnitude unmeasured) | §4.2. |

**There is more than one significant cause.** Fixing soft-float alone will not
reach playable; §5 ranks the rest.

---

## 4. The measured cost breakdown

### 4.1 Soft-float — confirmed as `fp-bit.c`, the slowest possible implementation

The SH7604 has no FPU, so every `f32` operation in SM64's engine becomes a
libgcc call. **Which** libgcc implementation matters enormously, and this build
has the slow one.

**Method.** `sh-elf-ar t` and `sh-elf-nm --defined-only` on the toolchain
actually used for the link,
`work/yaul-install/lib/gcc/sh-elf/14.3.0/libgcc.a`:

```
_pack_sf.o:      ___pack_f
_unpack_sf.o:    ___unpack_f
_addsub_sf.o:    ___addsf3
_mul_sf.o  _div_sf.o  _fpcmp_parts_sf.o  _thenan_sf.o
_pack_df.o _unpack_df.o _addsub_df.o _mul_df.o _div_df.o ...
```

`_pack_sf.o` / `_unpack_sf.o` / `_addsub_sf.o` are the object names GCC's build
system gives to **`libgcc/fp-bit.c`** compiled with `FLOAT`. The symbols
`___pack_f`, `___unpack_f`, `__fpadd_parts`, `___thenan_sf` are fp-bit.c
internals (fp-bit.h `#define`s `pack_d`→`__pack_f`, `unpack_d`→`__unpack_f`).
This is the portable C reference soft-float — correct, and roughly an order of
magnitude slower than a hand-written SH-2 assembly IEEE-754 path.

**Cost per operation.** Method: `sh-elf-nm --print-size` for extents, then
`sh-elf-objdump -d` over each extent, counting instructions and branch-class
opcodes (`bt bf bra bsr jmp jsr rts`). SH-2 instructions are 2 bytes.

| routine | address | bytes | instructions | branches |
| --- | ---: | ---: | ---: | ---: |
| `___addsf3` | `0x06004be8` | 76 | 38 | 5 |
| `___subsf3` | `0x06004c34` | 80 | 40 | — |
| `___mulsf3` | `0x06004c84` | 308 | 154 | 27 |
| `___divsf3` | `0x06004db8` | 224 | 112 | 22 |
| `__fpadd_parts` (float) | `0x06004a54` | 404 | 202 | 38 |
| `___pack_f` | `0x06005d0c` | 276 | 138 | 22 |
| `___unpack_f` | `0x06005e20` | 164 | 82 | 10 |
| `___floatsisf` | `0x06005090` | 132 | 66 | — |
| `___fixsfsi` | `0x06005114` | 108 | 54 | — |
| `__fpadd_parts` (double) | `0x06005250` | 800 | 400 | — |
| `___muldf3` | `0x06005614` | 548 | 274 | — |
| `___divdf3` | `0x06005838` | 304 | 152 | — |

A single `float + float` is `___addsf3` → `___unpack_f` ×2 → `__fpadd_parts` →
`___pack_f`: a **static budget of 542 instructions** across four calls with 75
branch-class opcodes. Not all of it executes on every input, but there is no
cheap path — fp-bit unpacks to a struct, operates on it, and repacks, every
time.

**Call-site density.** Method: `sh-elf-objcopy -O binary --only-section=.text`,
then scan the 480,552-byte image for 4-aligned big-endian words equal to each
soft-float symbol address (SH-2 calls load the target from a PC-relative literal
pool). This counts literal-pool entries, i.e. roughly one per calling function
per pool, not dynamic calls:

| symbol | pool refs | | symbol | pool refs |
| --- | ---: | --- | --- | ---: |
| `___addsf3` | 492 | | `___floatsisf` | 282 |
| `___mulsf3` | 487 | | `___fixsfsi` | 209 |
| `___ltsf2` | 348 | | `___gesf2` | 108 |
| `___subsf3` | 344 | | `___extendsfdf2` | 79 |
| `___gtsf2` | 285 | | `___truncdfsf2` | 63 |
| `___divsf3` | 57 | | `___muldf3` | 60 |
| others | — | | `___adddf3` | 42 |

**Total 3,041 soft-float literal-pool references in `.text`.** Float arithmetic
is not localised to a hot loop; it is diffused through the entire engine.

**Consistency check.** 82.4% of 75.9 M cycles = 62.5 M cycles/frame in
soft-float. At ~400 cycles per average float op that is ~156,000 float ops per
rendered frame; at ~750 cycles, ~83,000. One SM64 tick (camera, ~30 object
behaviours, surface collision, geo processing with 4×4 float matrix
concatenation) plus skybox is plausibly in that range. The picture is internally
consistent. **The per-op cycle cost itself is unmeasured** — see §6.

### 4.2 Double-precision promotion — a confirmed, separable subset

`___extendsfdf2` (79 sites), `___muldf3` (60), `___truncdfsf2` (63),
`___adddf3` (42), `___divdf3` (5) are all live. Double-precision fp-bit is
materially more expensive than single (`__fpadd_parts` double is 400
instructions vs 202; `___muldf3` 274 vs `___mulsf3` 154).

**Method.** For each double-precision symbol, map every literal-pool reference
back to its containing function via the symbol table. Selected results:

- `_sinf` and `_cosf` each reference `___extendsfdf2`, `___muldf3`, `___adddf3`
  and `___truncdfsf2`. This is not an accident of `-Os`: `lib/src/math/sinf.c`
  is the original N64 source and computes in `double` by construction — a
  `du`-union polynomial `P[5]`, `dx = x`, `xsq = dx * dx`, a 4-term Horner
  evaluation, all `double`. Every `sinf()`/`cosf()` call in the port runs
  ~8 `__muldf3` + ~4 `__adddf3` + 2 conversions through fp-bit's double path.
- `_guPerspectiveF`, `_guLookAtReflectF` — matrix setup.
- ~60 behaviour functions (`_boo_oscillate`, `_bhv_water_air_bubble_loop`,
  `_scale_bubble_sin`, `_mr_i_act_3`, …), typically one double site each,
  consistent with unsuffixed `double` literals in expressions.

`___divsf3` (57 sites) is the single most expensive single-precision op and
appears in `_geo_process_node_and_siblings`, `_vec3f_normalize`,
`_find_floor_from_list`, `_read_surface_data`, `_guNormalize`, `_next_lakitu_state`
— all per-frame paths.

### 4.3 Optimisation level

**Method.** Read the flag chain. `work/yaul-install/share/build.pre.mk:155`
sets `SH_CFLAGS := -std=c11 ... $(YAUL_CFLAGS)` with **no `-O` flag**;
`.yaul.env` does not define `YAUL_CFLAGS`. `src/port/saturn/sourceboot/Makefile:94`
then appends, after the include:

```
SH_CFLAGS += -Os -g -DDEBUG -DNON_MATCHING=1 ...
```

So the entire SM64 engine, all behaviours, `src/game/`, `src/engine/`, the
`gu*` math library and the Saturn frontend are compiled **`-Os` only**, at
`-m2 -mb` (`src/port/saturn/sourceboot/sourceboot.specs:13-14`), GCC 14.3.0.
`-Os` suppresses inlining and unrolling — the two transforms that most reduce
soft-float call overhead. (libyaul itself builds `-O2 -ffast-math` per
`third_party/libyaul/env.mk:99,132`; the application does not.)

Magnitude **unmeasured** — it requires a rebuild, which was out of scope here.

### 4.4 Cartridge traffic — quantified, and too small to be the cause

**Section sizes.** Method: `sh-elf-readelf -S` on the linked ELF.

| section | VMA | bytes |
| --- | --- | ---: |
| `.text` | `0x06004000` | 480,552 |
| `.cart_rodata` | `0x22400000` | **2,053,952** (`0x1f5740`) |
| `.rodata` | `0x060795bc` | 7,720 |
| `.data` | `0x0607b3e4` | 40,116 |
| `.bss` | `0x060850a0` | 367,984 |
| `.lwram_cmdts` | `0x00200000` | 65,536 |
| `.lwram_bss` | `0x00210000` | 393,216 |

3,615 symbols resolve into `0x22400000`–`0x225f5740`, including every BOB and
actor display list and vertex array (`_bobomb_seg8_dl_08023480`,
`_bobomb_seg8_vertex_08023190`, …). `sourceboot-cart.x:47-58` routes
`*sm64-port?*(.rodata*)` there, and `src/port/saturn/sourceboot/Makefile:196`
asserts the VMA at build time. The policy in
`docs/saturn/CARTRIDGE_ASSET_POLICY.md:19-21` — "No frame loop may chase
pointers through cartridge memory for transformed geometry, sorting, collision,
or command emission" — is genuinely violated. That finding stands.

**Per-frame traffic.** Method: decode `probe_window` from
`docs/saturn/evidence/reports/e2-sourceboot-quadmerge-freeroam-2026-07-26.json`
(the window is anchored at `_sourceboot_fast3d`, and its recorded `telemetry`
block is unusable — `decode_error: unexpected telemetry magic 0x2118DE5A`):

```
frame_serial            143      texture_commands        507
command_count          3453      rdp_commands            430
display_list_calls      232      other_commands          653
display_list_branches     6      triangles_transformed  2311
matrix_commands          84      triangles_emitted       913
vertex_commands         314      triangles_vdp1_emitted  827
triangle_count         2311      reject_backface        1181
```

- **Display-list words: measured.** 3,453 commands × 8 bytes = **27,624 bytes**.
- **Vertex data: NOT measured.** The profile counts `G_VTX` *commands* (314), not
  vertices loaded. Upper bound at F3DEX2's 32-vertex maximum:
  314 × 32 × 16 = **160,768 bytes**. Derived middle estimate from
  2,311 triangles ÷ 314 loads = 7.4 tri/load ⇒ ~10 vtx/load ⇒ ~50,000 bytes.
  **Labelled derived, not measured** — see §6.
- Total cart reads per frame: **~78 KB derived, ≤190 KB bounded.**

**The arithmetic that refutes the hypothesis.** The frame is 75.9 M SH-2 cycles.

| scenario | longword reads | cycles/read needed to explain the frame |
| --- | ---: | ---: |
| derived ~78 KB/frame | 19,500 | **3,892** |
| bounded ≤190 KB/frame | 47,500 | **1,598** |
| absurd: read the entire 2,053,952 B bank every frame | 513,488 | **148** |

An uncached SH-2 longword read across the A-bus to CS0 DRAM costs on the order
of tens of cycles, not thousands. **Even the physically impossible case — the
frontend touching every byte of the 2 MB bank on every frame — needs 148
cycles per longword, and the realistic case needs 1,598.** No cartridge access
cost consistent with the Saturn's bus can produce 2.65 s frames.

Sanity check against the profile: the frontend that performs those reads scores
1/34 samples. Consistent.

**What the cart does cost.** Taking a pessimistic 30 cycles per uncached
longword: 78 KB ⇒ 585,000 cycles ⇒ **~20 ms/frame**; 190 KB ⇒ ~50 ms. That is
0.8%–1.9% of today's frame and invisible. **It is 60%–150% of a 33 ms
frame.** Once soft-float is fixed, this becomes a first-order problem. It is a
real bug with a deferred bill, not a false alarm.

**A-bus latency is unmeasured in this tree.** `src/port/saturn/hwtest/main.c:163-190`
already contains exactly the right harness — a cached read loop, a
`cpu_cache_purge()` + cache-through read loop, a CPU-DMAC transfer and an SCU
transfer, all FRT-timed into `cpu_cached_ticks` / `cpu_uncached_ticks` /
`cpu_dmac_ticks` / `scu_cart_to_wram_ticks`. **Every committed capture records
0 for all four fields** (`ymir-bios-hwtest-2026-07-17.json:253-286`,
`ymir-event-poke-2026-07-17.json`, `ymir-vdp1-valid-abcd-reference-2026-07-18.json`,
the `ymir-hwtest-textured-triangle-*` set). The 2026-07-17 run never got past
BIOS handoff (`docs/saturn/evidence/ymir-slavedriver-dma-2026-07-17.md`). The
harness has never produced a number. See §6.

### 4.5 Cache configuration

- **Cache is enabled.** Yaul's only cache action anywhere in the boot path is
  `cpu_cache_purge()` (`libyaul/scu/bus/cpu/cpu_cache.c:42-56`), a
  read-modify-write on the 8-bit CCR at `0xFFFFFE92` that clears CE, sets CP,
  then sets CE. It is called twice from `__sys_init`
  (`libyaul/kernel/sys/init.c:54,66`). Nothing in libyaul or in
  `src/port/saturn/` ever writes a whole CCR value, so the residual bits are
  BIOS-determined; **the exact CCR contents are not determinable from source.**
- **Two-way mode / 2 KB cache-as-scratchpad is not used.** `cpu_cache_way_mode_set`
  and `CPU_CACHE_MODE_2_WAY` (`cpu/cache.h:71-76`) are dead code across the
  whole tree. Default is 4-way, 4 KB.
- **Cached:** HWRAM `0x06000000` and LWRAM `0x00200000` (`scu/map.h:135,153`).
  All of `.text`, `.rodata`, `.data`, `.bss`, `.lwram_*` therefore run cached.
  The soft-float routines are in cached HWRAM — their cost is real work, not
  cache misses.
- **Cache-through by address partition:** `0x22400000` (the cart),
  `0x25xxxxxx` (VDP1/VDP2/SCU/SCSP), `0x26xxxxxx` (HWRAM uncached mirror).
  `sourceboot-cart.x:102` uses the same `0x20000000 |` idiom for `.uncached`
  and comments "Back to cached addresses" on the next line, so the tree already
  understands the partition scheme.
- **Yaul defines no cached alias for CS0.** The only macro is
  `CS0(x) = 0x22000000 + x` (`scu/map.h:61`), and
  `DRAM(0,0,0)` resolves to exactly `0x22400000`
  (`scu/bus/a/cs0/dram-cart/dram-cart/map.h:38-39`, `dram-cart.c:114`).
  `0x02400000` appears nowhere in the tree. So cart reads are uncached **by
  construction**, not by mistake — but that also means no cheap CCR fix exists
  for them; the fix is to move the data (§5).

**Incidental finding (not perf-related, reported not fixed):**
`src/port/saturn/introface/main.c:188-190` justifies two `cpu_cache_purge()`
calls by claiming VDP2 VRAM is reached "through the SH-2 cacheable bus mapping"
and needs a "write-back". `VDP2_VRAM_ADDR` is `0x25E00000 + …`, i.e.
cache-through; and `src/port/saturn/gfx/saturn_vdp1_backend.h:200-203` correctly
states the SH7604 data cache is write-through with no write-back mode. The two
purges at `introface/main.c:172,191` appear unnecessary. Cosmetic; flagged only
because the comment is wrong and could mislead a later reader.

### 4.6 Emulator versus target

The §1.2 frame rate is a ratio between two guest-side counters measured in
emulated time, so host throughput cannot contaminate it. The 15–20 minute wall
clock for a 25,000-frame capture is a statement about Ymir's speed on this PC
and nothing else.

The residual risk runs the other way: if Ymir under-models SH-2 memory stalls —
HWRAM wait states, A-bus latency, B-bus contention — then **real hardware would
be slower than 2.65 s/frame, not faster.** Ymir's fidelity for A-bus timing is
unverified here and is an open question (§7).

---

## 5. Ranked fix list

Ordered by measured share of the current frame. Cost and risk are engineering
judgements, labelled as such.

### Rank 1 — Eliminate `fp-bit.c` soft-float from the hot path
**Measured share: 82.4% of SH-2 time (95% CI 65–93%), 28/34 samples.**

Options, cheapest first:

1. **Replace libgcc's fp-bit soft-float with an SH-2 assembly IEEE-754
   implementation.** Cost: medium (toolchain rebuild or a link-order override
   providing `__addsf3`/`__subsf3`/`__mulsf3`/`__divsf3`/comparisons/conversions).
   Risk: medium — correctness of a hand-written FP path must be proven against
   the existing host contract tests. Expected win: commonly 3–10× on FP ops for
   assembly vs fp-bit; **unmeasured for this workload.** Whether GCC 14.3.0
   ships a usable `-m2` assembly path in `libgcc/config/sh/` is **unverified**
   and is the first thing to check. Highest value-per-hour of anything here
   because it needs no engine changes.
2. **Convert the hot engine math to fixed point.** The port already did this for
   the render matrix (`-DSATURN_MTX_IS_Q16=1`, `saturn_trig_q16.inc.c`). Extending
   it to camera, `vec3f`, collision and geo processing is the structurally
   correct answer and the only one that reaches N64-class throughput. Cost:
   high, and it diverges further from upstream SM64. Risk: high — precision
   regressions in collision and camera are exactly the class of bug this project
   has spent days on.
3. **Both.** (1) buys time; (2) is the destination.

Interaction with `SEGMENT_ADDRESSING_DECISION.md`: none. This is `.text`-only.

### Rank 2 — Remove double-precision from `sinf`/`cosf` and from constants
**Share: subset of Rank 1, not separately measured. 79 `__extendsfdf2` +
60 `__muldf3` + 63 `__truncdfsf2` + 42 `__adddf3` call sites (§4.2).**

- Replace `lib/src/math/sinf.c` / `cosf.c` with a single-precision or Q16
  implementation. The port already has `saturn_trig_q16.inc.c`; wiring `sinf`/`cosf`
  to it removes the entire double path from the two most-called math functions.
  Cost: low. Risk: low-medium (accuracy affects camera/animation smoothness).
- Add `-fsingle-precision-constant` to `SH_CFLAGS` to stop unsuffixed literals
  promoting whole expressions to `double`. Cost: trivial. Risk: **medium and
  under-appreciated** — it changes the value of every `double` constant in the
  tree and can shift SM64 behaviour. Must be validated against the free-roam
  capture, not assumed safe.

Interaction with the overlay scheme: none.

### Rank 3 — Build the engine at `-O2` instead of `-Os`
**Share: unmeasured. Confirmed present (§4.3).**

Cost: trivial (one line). Risk: low for correctness; the real risk is **size** —
`-O2` will grow `.text`, and HWRAM free space is only 135,964 bytes
(measured: `___end = 0x060dece4` per `sh-elf-nm`, against the region top
`0x06100000`; `sourceboot-cart.x:130` already ASSERTs a 4 KiB floor for
libyaul's TLSF control block). `-O2` on a 480 KB `.text` could plausibly add
30–60 KB. Measure before and after; the linker ASSERT will catch overflow, but
only after the fact.

A cheaper variant: `-O2` only for the float-heavy translation units
(`src/game/`, `src/engine/`, `lib/src/gu*.c`) and leave the rest at `-Os`.

Interaction with the overlay scheme: `.text` is HWRAM-resident and outside the
cart overlay entirely, but it competes for the same 135,964-byte margin that
any work-RAM staging buffer (Rank 4) also needs. **Rank 3 and Rank 4 contend
for the same budget** and must be planned together.

### Rank 4 — Stage hot geometry into work RAM (the policy fix)
**Bounded share today: ~20–50 ms of a 2,650 ms frame (0.8%–1.9%). Share of a
future 33 ms frame: 60%–150%.**

This is the fix the cart hypothesis called for. It is correct, it is required by
`CARTRIDGE_ASSET_POLICY.md`, and it is **not urgent** — it should be scheduled
after Ranks 1–3, at which point it becomes the top item.

Sizing:

- BOB's level-specific data is **87,018 bytes**
  (`SEGMENT_ADDRESSING_DECISION.md:267` — `leveldata` 85,666 + `script` 1,084 +
  `geo` 268). Resident actor groups BOB never uses total **285,250 bytes**
  (`ibid.:281`).
- Residency budget: **HWRAM free = 135,964 bytes** (measured, §Rank 3);
  **LWRAM free = 589,824 bytes / 576 KiB** (`ibid.:307`).
- So BOB's 87,018 bytes **does** fit in HWRAM today with ~49 KB to spare — but
  that margin is the same one `-O2` would consume, and it leaves nothing for
  Mario or a second level.

Prior art from the two in-tree GPL engines (`work/upstream/`) is unanimous and
worth following:

- **Neither engine ever traverses geometry out of A-bus/cart memory.** Neither
  contains any CS0/CS1/A-bus address at all.
- **SlaveDriver** allocates every level geometry array in HWRAM
  (`LEVEL.C:27-30,51-64`, `mem_malloc(1, …)` where area 1 = `&end`–`0x06100000`,
  `UTIL.C:344-353`) and reserves LWRAM (area 0) strictly for cold data — audio
  samples, pictures, cutscenes, FMV, text. Its `dmaMemCpy` (`DMA.C:81-102`)
  will only engage SCU DMA when the source is HWRAM and the destination is not
  LWRAM; everything else falls back to a hand-written SH-2 `qmemcpy`.
- **Sonic Z-Treme** loads a whole level into LWRAM, then runs an explicit
  post-load promotion pass — `mallocVertices()`,
  `ZT_LOADING.c:320-353`, comment "*Temporary function to move the vertices to
  high work ram*" — `slDMACopy`ing octree nodes, entity tables and every mesh's
  `pntbl` vertex array into a 300 KB HWRAM arena
  (`Common.h:43 HWRAM_DYNAMIC_MEM_SIZE`), leaving polygon attributes, collision
  and PVS in LWRAM. That is precisely the staging pattern
  `CARTRIDGE_ASSET_POLICY.md` describes.
- **Neither engine's comments quantify any bus cost in cycles.** The only
  bus-cost prose found in either tree is Z-Treme's
  `cd/SOUND/README.txt:7-8` on B-bus contention from PCM DMA. So there is no
  upstream figure to borrow for A-bus latency — it has to be measured (§6).

Interaction with `SEGMENT_ADDRESSING_DECISION.md`: **direct and significant.**
That decision keeps identity addressing with fixed-VMA overlay slots at
`0x22400000`. A staging scheme changes what a `Gfx*`/`Vtx*` points at during
traversal, so it must either (a) rewrite pointers at stage time, which breaks
the identity-addressing invariant the decision was built on, or (b) keep cart
VMAs canonical and have the frontend translate on entry into a staged mirror.
Option (b) preserves the decision; option (a) supersedes part of it. **This
choice must be made explicitly before any staging code is written**, not
discovered during implementation.

Note also that SCU DMA cannot touch LWRAM — locks the machine
(`libyaul/scu/scu/dma.h:32`; also `docs/saturn/SGL_REFERENCE_NOTES.md`). So a
cart→LWRAM stage cannot use SCU DMA; it must be CPU copy or CPU-DMAC. Cart→HWRAM
via SCU DMA is fine.

### Rank 5 — Reduce skybox cost
**Share: 1/34 deduped samples in `_create_skybox_facing_camera`; 3/53 in the raw
sample set, plus 2 `pr` hits from `_draw_skybox_tile_grid`. Suggestive, roughly
3–9%, weakly evidenced.**

Cost: low (skip or simplify the skybox for BOB). Risk: low, visual only. Worth
doing only after Rank 1, when 3–9% starts to matter.

### Rank 6 — Put the slave SH-2 to work
**Share: currently 0 — the slave contributes nothing measurable.**

SlaveDriver's precedent (`WALLS.C:1806-1828`, `slaveDraw`/`drawSlaveWalls`, with
the slave publishing results through a `+0x20000000` cache-through alias at
`WALLS.C:1272-1273`) shows the pattern: slave transforms polygons, master
consumes them, both purge cache at the handoff. Best case 2×. Cost: high. Risk:
high (dual-CPU coherence). Do not attempt before Ranks 1–4 — doubling the
throughput of an 80×-over-budget frame gets to 40× over budget.

---

## 6. Missing instrumentation

This diagnosis needed manual struct decoding and a 34-sample opportunistic PC
histogram because none of the following exists. Each item would make the next
diagnosis dramatically cheaper.

1. **No phase timers anywhere in sourceboot.** `sm64_saturn_fast3d_profile_t`
   (`saturn_fast3d_frontend.h:52-335`) has 60+ counters and **zero timing
   fields**. `src/port/saturn/sourceboot/main.c` measures nothing. The only FRT
   access in the whole target is
   `src/port/saturn/sourceboot/source_platform.c:44`,
   `u32 osGetCount(void) { return (u32)cpu_frt_count_get(); }` — a shim for SM64,
   never sampled into telemetry. **Highest-value addition: FRT deltas around
   (game tick) / (fast3d submit) / (VDP1 emit) / (sync wait), stored in the
   profile.** This project's discipline is measurement over inference, and right
   now the renderer literally cannot report how long it took.
2. **No rendered-FPS counter.** §1.2 had to be reconstructed by differencing
   `frame_serial` between two captures that happened to differ only in run
   length. A `frames_rendered` + `frt_ticks_elapsed` pair in the profile would
   make frame rate a single-capture read.
3. **No `vertices_loaded` counter.** `vertex_commands` counts `G_VTX` commands,
   not vertices, so the largest component of per-frame cart traffic (§4.4) is
   derived rather than measured. One `+= n` in the `G_VTX` handler settles it.
   Likewise a `cart_bytes_read` accumulator would turn the whole of §4.4 from
   arithmetic into a reading.
4. **The A-bus timing harness has never fired.** `hwtest/main.c:163-190`
   (`dma_test`) already measures cached vs cache-through cart reads, CPU-DMAC
   and SCU, all FRT-timed — and every committed capture records 0 for
   `cpu_cached_ticks`, `cpu_uncached_ticks`, `cpu_dmac_ticks` and
   `scu_cart_to_wram_ticks`. Getting one real reading out of it would replace
   every "tens of cycles" hedge in §4.4 with a number, and is a prerequisite for
   sizing Rank 4 honestly.
5. **Telemetry decoding is broken on recent captures.**
   `e2-sourceboot-quadmerge-freeroam-2026-07-26.json` reports
   `telemetry.decode_error: unexpected telemetry magic 0x2118DE5A`;
   `e2-sourceboot-mario-render-cost-2026-07-25.json` reports magic `0xD2552E11`.
   `tools/saturn/telemetry_decode.py` and the sourceboot telemetry block have
   drifted apart. The profile is currently readable **only** by decoding
   `probe_window` by hand against the ELF, which is how everything in this
   document was obtained. Fixing this is cheap and unblocks everything else.
6. **No sampling profiler.** The 34-sample histogram in §2 is a fortunate side
   effect of `registers_at_stop` being recorded. A capture mode that stops N
   times at pseudo-random intervals and records only `pc` would give a
   thousand-sample profile in one run and settle the 65–93% confidence interval.

---

## 7. Open questions

1. **What does one fp-bit float op actually cost on SH-2?** Static instruction
   counts are measured (§4.1); cycles are not. An FRT-timed microbenchmark of
   `__addsf3`/`__mulsf3`/`__divsf3` in a loop would take an hour and convert the
   central claim from "82% of samples" to "N cycles × M ops = X ms".
2. **How many float ops per frame does SM64 actually execute?** Unmeasured.
   Needed to predict what a 5× faster soft-float buys.
3. **Does GCC 14.3.0 ship a usable assembly IEEE-754 soft-float for `-m2`?**
   Unverified. This gates the cheapest Rank 1 option and should be checked
   before any other work is planned.
4. **How faithfully does Ymir model SH-2 memory stalls and A-bus latency?** If
   it under-models them, real hardware is slower than 2.65 s/frame and the cart
   cost in §4.4 is understated. Nothing in this tree establishes Ymir's timing
   fidelity.
5. **How many vertices per `G_VTX`?** Derived at ~10, bounded at ≤32. Blocks an
   exact cart-traffic figure (§6.3).
6. **Was the 387 → 143 `frame_serial` decline (§1.3) content growth or a
   regression?** The captures differ in build *and* scene, so they cannot
   separate the two. If any of that 63% was a regression rather than added
   geometry, it is a separate and possibly cheap win.
7. **Which pointer-translation strategy will Rank 4 use?** Rewriting pointers at
   stage time breaks the identity addressing that `SEGMENT_ADDRESSING_DECISION.md`
   is built on; translating at frontend entry preserves it. This is a design
   decision, not an implementation detail, and it should be settled in that
   document before staging code is written.
8. **Is one SM64 game tick really running per rendered frame?**
   `main.c` documents "advance exactly one source frame per iteration", but
   `gGlobalTimer` was not captured, so the ratio is unverified. If more than one
   tick runs per render, the per-tick cost is proportionally lower than §4
   assumes.

---

## Appendix: reproduction

All figures were obtained by static analysis of the committed ELF and committed
capture JSON. No build and no emulator run was performed.

```sh
# toolchain used for inspection
export PATH="/d/tmp/gcc15sh/sh-gcc-toolchain/work/bin:$PATH"
ELF=build/saturn/sourceboot/e2-bob/obj/sm64-saturn-sourceboot-e2.elf

sh-elf-readelf -S "$ELF"                    # section sizes and VMAs (§4.4)
sh-elf-nm -n --print-size "$ELF"            # symbol map; ___end for HWRAM margin
sh-elf-objdump -d "$ELF"                    # soft-float instruction/branch counts
sh-elf-objcopy -O binary --only-section=.text "$ELF" text.bin   # literal-pool scan

# the linked libgcc, proving fp-bit
sh-elf-ar t work/yaul-install/lib/gcc/sh-elf/14.3.0/libgcc.a
sh-elf-nm --defined-only work/yaul-install/lib/gcc/sh-elf/14.3.0/libgcc.a
```

Profile decoding: `probe_window.data` is a big-endian byte dump anchored at
`_sourceboot_fast3d` (`0x060be918` in this build); `sm64_saturn_fast3d_profile_t`
is its first member, so field offsets follow the declaration order in
`src/port/saturn/gfx/saturn_fast3d_frontend.h:52`.

PC sampling: `registers_at_stop.pc` from every
`docs/saturn/evidence/reports/*sourceboot*.json`, de-duplicated on
`(pc, pr, post_poke_frames)`, resolved against the symbol map.
