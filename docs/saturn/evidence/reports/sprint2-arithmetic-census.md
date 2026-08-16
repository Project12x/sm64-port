# Sprint 2 — whole-image arithmetic census

- Date: 2026-08-15. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, HEAD `e37a1beb`.
- Question from the owner: **"are we still doing divides, or floats, or
  64-bit math?"**
- Scope: the *whole linked image*, not one path. T2.5
  (`sprint2-t2_5-prepare-mario-audit.md`) censused only `demo_prepare_mario`;
  this sweeps every helper class across every reachable caller, plus the
  separate 68K audio image.
- **This is a measurement task. Nothing was changed.** No source edit, no
  rebuild — the analysed ELF is the already-published T2.6 product build.

## Headline

**Divides: essentially fixed. 64-bit math: fine. Float: not fixed — it is
everywhere, and it is now the single largest arithmetic liability in the
port.**

1. **Integer division is nearly gone from the hot path.** Ten `___sdivsi3`
   and five `___udivsi3` call sites are hot-reachable in the whole image, and
   the five hot `___udivdi3` sites are all inside the N64 profiler HUD, which
   is compiled in but runtime-gated off (`gProfilerMode = 0`). `___divdi3`
   survives at exactly four sites, all inside `actor_saturating_mul_i64`,
   which T2.6 demoted to a guarded fallback.
2. **There is no inline stepwise divide anywhere in application code**, and
   the SH-2's hardware DIVU unit **is** being used — by the Q16 renderer
   path (`sm64_saturn_div_s64_s32`) and the Q16 `atan2`. That is precisely
   why the geo walk avoids `___sdivsi3`. The remaining software divides are
   in code that was never routed through it.
3. **64-bit arithmetic is healthy.** 184 `dmuls.l`/`dmulu.l` (the native
   32x32→64 multiply) against 665 multiword add/sub ops on hot functions,
   concentrated in the intentional Q16 kernels. No `___muldi3`, no
   `___moddi3`/`___umoddi3`, no `___ashldi3`/`___ashrdi3`.
4. **Soft float is the finding.** **1,827 soft-float helper call sites are
   hot-reachable**, of which **150 are soft-*double***. `sinf` and `cosf` —
   called from `guRotateF` and `calculate_vertex_xyz` on the per-frame
   matrix path — are implemented in **double precision** in this tree.
5. The converted `atan2s`/`atan2_lookup` are **clean**: zero native-math
   helper calls, so the `FORBIDDEN_CALLER` clauses still hold.
6. **Separately, and not previously reported:** the in-tree verifier now
   **FAILS** on this build — the simulation route's helper total is
   **1,402 against a pinned contract of 582 (+141%)**. See §2a. That is a
   regression signal independent of everything else here.

## 1. Artifact identity

| Field | Value |
| --- | --- |
| ELF | `releases/2026-08-15_1904_t2_6-product/sm64-saturn-sourceboot-e2.elf` |
| SHA-256 | `1b4ff08d763519e71c075d41b04ad9308f1fd56c3761639bb82d8a2ddc260ad4` |
| Size | 9,975,488 B |
| Identical to | `build/saturn/sourceboot/e2-bob-identity-id-6eca5970628d581d/obj/sm64-saturn-sourceboot-e2.elf` (byte-for-byte, verified by `sha256sum` on both) |
| Producing revision | `f4f8ad6b` (per `saturn-release-manifest-v1.json` `provenance.git_revision`) — the T2.6 depth-walk fix |
| Profile | `sourceboot-bob-demo`, `demo_path=1`, `route_replay_mode=1`, `slave_render=1` |
| 68K audio image | `build/saturn/audio68k/pcm68k-heartbeat.elf`, SHA-256 `4f1c3959554f4d118f249cfb2c6af68efa7813b4a7fd9255f39d68442629b4dd` |

Toolchain: `sh-elf-nm` / `sh-elf-objdump` / `sh-elf-readelf` from
`work/yaul-install/bin`; the 68K image via
`work/upstream/SCSP_poneSound/m68k-elf/m68k-elf-{nm,objdump}`.

`sh-elf-nm -u` on the product ELF reports **no undefined symbols**, so the
symbol census below is complete and closed.

## 2. Method — and what was reused

`tools/saturn/verify_sh2_native_math.py` (6,622 lines) already implements
literal-pool `jsr` and PC-relative `bsr` caller attribution with an
immutable route-oracle HOT derivation. **It was read first, and it was
run.** Two things made it insufficient on its own for this task:

1. **Its helper classifier is narrower than the question.** From
   `verify_sh2_native_math.py:3992-4007`:

   ```python
   DIV64_RE = re.compile(r"(?:u?div|u?mod)di3$")
   SOFT_FLOAT_RE = re.compile(
       r"(?:add|sub|mul|div|neg|eq|ne|cmp|ge|le|gt|lt|unord)"
       r"(?:sf|df)(?:2|3)$|(?:fix|float|extend|trunc)[a-z0-9]*$"
   )
   ```

   `DIV64_RE` is 64-bit only. **`___sdivsi3`, `___udivsi3`, `___ashlsi3`,
   `___ashrsi3`, `___lshrsi3` and `___clzsi2` are outside its census
   entirely** — as are the inline `div1` and DIVU questions, which are not
   symbol-level at all.
2. **It is very slow.** Invoked on this ELF with both route oracles it took
   **~95 minutes** and peaked around 670 MB. It did complete, and its result
   is reported in §2a below — but the independent pass described next was
   built while it ran, and the two are cross-checked against each other.

The independent pass:

- **Symbol level** — `sh-elf-nm` over the linked image for every helper
  class named in the task, including explicit absence checks.
- **Caller attribution** — full `sh-elf-objdump -d` (1,002,610 lines),
  then a register-tracking scan that models SH-2's two call forms: a
  PC-relative literal-pool load (`mov.l <pool>,rN ! <addr> <symbol>`)
  followed by `jsr @rN`/`jmp @rN`, and direct `bsr <addr> <symbol>`. This is
  the same mechanism `verify_sh2_native_math.py` uses.
- **HOT derivation** — a direct-call graph from the same disassembly, seeded
  from the per-frame roots and **closed over the checked-in route oracles'
  declared indirect edges** (`sh2_native_math_route_oracle_v1.txt`,
  `sh2_native_math_sim_route_oracle_v1.txt`). Roots: `_game_loop_one_iteration`,
  `_sm64_saturn_demo_render_{poll,start}_frame`,
  `_sm64_saturn_render_job_runtime_{poll_slave,drain_master}`,
  `_sm64_saturn_render_lifecycle_{start,poll}`,
  `_sourceboot_render_overlap_phase`, `_sourceboot_present_generation`,
  `_sourceboot_game_loop`. Closure: **1,278 functions**.

**A direct-edge-only closure is an under-approximation and was caught being
one.** The first pass (694 functions) reported `___divdi3` as hot=0, because
`_demo_render_prepare_publish` is reached only through the render lifecycle
callback table. Adding the oracle's declared indirect edges recovered the
real chain and moved `___divdi3` to hot=4:

```
_actor_meshlet_live_depth_bounds  <- ['_actor_meshlet_core']
  _actor_meshlet_core  <- ['_actor_mario_prepare_with_carry.constprop.0']
    _actor_mario_prepare_with_carry.constprop.0  <- ['_sm64_saturn_actor_meshlets_prepare']
      _sm64_saturn_actor_meshlets_prepare  <- ['_demo_render_prepare_publish']
```

**"HOT" throughout this report means *statically reachable per frame*, not
*executed every frame*.** Several reachable callers are runtime-gated off on
the BOB demo route — see §8.

## 2a. The existing verifier's own result — and a regression it exposes

`verify_sh2_native_math.py` completed on this ELF. It **verifies the ELF
identity independently**: `elf_sha256 =
1b4ff08d763519e71c075d41b04ad9308f1fd56c3761639bb82d8a2ddc260ad4`, matching
§1. Root `_game_loop_one_iteration`, closure **701 functions**,
`analysis_mode = code-only`.

**It exits FAILED**, for two separate reasons, both worth acting on:

**(a) The sim-route helper total has more than doubled since the contract
was pinned.**

```
helper_total = 1402   (contract_before_expected_total = 582)
```

`sh2_native_math_sim_audit_contract_v2.txt` pins 582 against conversion
baseline ELF `54d36006…`. This build reports **1,402** — a **+141%**
increase on the simulation route. That is a regression signal nobody has
looked at, and it is independent of everything else in this report.

**(b) Seven indirect transfers are not declared in the sim route oracle:**

```
_camera_course_processing +286 jsr        _play_cutscene +186 jsr
_camera_course_processing +316 jsr        _play_mode_change_area +94 jsr
_emit_words +40 jsr                       _play_mode_change_level +14 jsr
_exec_display_list +30 jmp
```

Three resolve to a `ConstSet` (`_sTaskSubmit*`, `_sTransitionUpdate*`); four
are genuinely `UNKNOWN`.

### The two passes agree

The verifier's per-caller counts match the independent pass almost exactly
where the closures overlap:

| Caller | Verifier | This report |
| --- | ---: | ---: |
| `_find_wall_collisions_from_list` | 110 | 110 |
| `_create_transformation_from_matrices` | 78 | 78 |
| `_rotate_triangle_vertices` | 61 | 61 |
| `_envfx_update_snow_normal` | 41 | 41 |
| `_get_pos_from_transform_mtx` | 33 | 33 |
| `_evaluate_cubic_spline` | 71 | 70 |
| `_sm64_saturn_audio_spatial_quantize` | 80 | 74 |

The verifier's totals are lower overall (1,402 vs 1,827) because its closure
is the **simulation route only** (701 functions); the independent pass adds
the render-lifecycle roots (1,278 functions). The verifier also classifies
`_sqrtf` (14), `_cosf` (4) and `_sinf` (3) as helpers in their own right via
its `LIBM_NAMES` set, which this report instead accounts for by counting the
soft-double helpers *inside* them.

**Its sim-route census contains zero `___divdi3` and zero `___udivdi3`**,
consistent with §4: those live on the *render* route, reached through
`_demo_render_prepare_publish`, which is outside this root.

## 3. Symbol-level census

### Present and referenced

| Symbol | Addr | Class |
| --- | --- | --- |
| `___divdi3` | `0600473c` | 64-bit signed divide |
| `___udivdi3` | `060049a8` | 64-bit unsigned divide |
| `___udiv_qrnnd_16` | `06004570` | 64-bit divide inner step |
| `___lshrdi3` | `060045d0` | 64-bit logical shift right |
| `___sdivsi3` | `06004464` | 32-bit signed divide |
| `___udivsi3` | `06004526` | 32-bit unsigned divide |
| `___ashlsi3`/`_r0`, `___ashrsi3`, `___lshrsi3`/`_r0` | `060041f4`–`06004334` | variable 32-bit shifts |
| `___clzsi2` | — | count-leading-zeros (soft-fp normalisation) |
| `___addsf3` `___subsf3` `___mulsf3` `___divsf3` | `0607d42c`… | **soft float** |
| `___eqsf2` `___nesf2` `___gesf2` `___gtsf2` `___lesf2` `___ltsf2` | `0607e63c`… | soft-float compare |
| `___fixsfsi` `___fixunssfsi` `___floatsisf` `___floatunsisf` | `0607e800`… | soft-float convert |
| `___adddf3` `___subdf3` `___muldf3` `___divdf3` | `0607eae0`… | **soft double** |
| `___gedf2` `___gtdf2` `___ledf2` `___ltdf2` | `06080890`… | soft-double compare |
| `___fixdfsi` `___fixunsdfsi` `___floatsidf` | `06080ae0`… | soft-double convert |
| `___extendsfdf2` `___truncdfsf2` | `06080ce0`, `06080e38` | float↔double |
| `___powisf2` | `06004698` | float integer power |
| `___cpu_divu_init`, `_cpu_divu_ovfi_set` | `06087ec0`, `06087f24` | Yaul DIVU driver (boot only — see §5) |

### Explicitly absent (checked, not assumed)

`___moddi3` · `___umoddi3` · `___modsi3` · `___umodsi3` · `___mulsi3` ·
`___muldi3` · `___ashldi3` · `___ashrdi3` · `___floatdisf` · `___floatdidf` ·
`___fixsfdi` · `___fixdfdi` · `___unordsf2` · `___unorddf2` · `___nedf2`

**No modulo helper of any width is linked.** SH-2 GCC computes `%` from the
divide helper plus multiply-subtract, so modulo cost is folded into the
`___sdivsi3`/`___udivsi3` rows.

### fp-bit vs soft-fp — the substitution holds

The `fp-bit` marker symbols `___pack_f`, `___unpack_f`, `___pack_d`,
`___unpack_d`, `__fpadd_parts`, `___fpcmp_parts_f`, `___fpcmp_parts_d` are
**all absent**, while the soft-fp entry points are all present as `T`. The
Makefile gate at `src/port/saturn/sourceboot/Makefile:1597-1608` asserts
exactly this in both directions, and it passes. The mechanism, from
`Makefile:762-782`:

> "the SH7604 has no FPU, so every f32 operation in the SM64 engine is a
> libgcc call, and the pinned GCC 14.3.0 selects `fp-bit.c` — the portable C
> reference soft-float, and the slowest correct one there is. A float add
> there is four out-of-line calls (`__unpack_f` x2, `__fpadd_parts`,
> `__pack_f`) through a 56-byte stack frame."

and

> "Archive semantics do the rest: the soft-fp members satisfy `___addsf3` and
> friends first, so fp-bit's members are never pulled in at all."

The substitution is real and working. **It makes each float operation ~3x
cheaper; it does not make float free, and §6 is what remains.**

## 4. Helper table — referenced, call sites, hot/cold

Counts are static call sites in the linked image, not dynamic executions.

| Symbol | Referenced | Hot sites | Total sites | Hot callers | Verdict |
| --- | --- | ---: | ---: | --- | --- |
| `___mulsf3` | yes | **493** | 1596 | `_find_wall_collisions_from_list`, `_create_transformation_from_matrices`, `_sm64_saturn_audio_spatial_quantize`, `_evaluate_cubic_spline`, `_rotate_triangle_vertices`, … | **HOT — worst offender** |
| `___addsf3` | yes | **342** | 1246 | same family | **HOT** |
| `___floatsisf` | yes | **264** | 607 | `_saturn_geo_enter_*`, `_append_*_vertex_buffer` | **HOT** |
| `___subsf3` | yes | **224** | 756 | `_get_pos_from_transform_mtx`, `_guRotateF`, `_guOrthoF` | **HOT** |
| `___ashlsi3_r0` | yes | 159 | 169 | mostly *inside* soft-fp helpers | HOT (derived) |
| `___lshrsi3_r0` | yes | 108 | 117 | mostly *inside* soft-fp helpers | HOT (derived) |
| `___gesf2` | yes | 98 | 547 | `_add_surface`, collision, camera | HOT |
| `___fixsfsi` | yes | 91 | 361 | `_rotate_triangle_vertices`, `_find_floor`, `_envfx_*` | HOT |
| `___lesf2` | yes | 82 | 587 | collision, camera | HOT |
| `___muldf3` | yes | **38** | 116 | **`_sinf` (14), `_cosf` (8)**, `_calculate_vertex_xyz`, `_scale_shadow_with_distance` | **HOT — soft double** |
| `___divsf3` | yes | 38 | 81 | `_sm64_saturn_audio_spatial_quantize` (7), `_guOrthoF` (6), `_vec3f_normalize` | HOT |
| `___eqsf2` | yes | 34 | 97 | `_render_dialog_entries`, audio spatial | HOT |
| `___extendsfdf2` | yes | 33 | 128 | float→double promotion sites | HOT |
| `___clzsi2` | yes | 29 | 33 | *inside* soft-fp helpers only | HOT (derived) |
| `___udiv_qrnnd_16` | yes | 24 | 24 | **only** `___divdi3`, `___udivdi3`, `___divsf3`, `___divdf3` | HOT (derived) |
| `___adddf3` | yes | 18 | 66 | soft-double users | HOT |
| `___truncdfsf2` | yes | 17 | 88 | `_sinf`, `_cosf`, `_calculate_vertex_xyz` | HOT |
| `___subdf3` | yes | 15 | 34 | `_sinf` (6), `_cosf` (3) | HOT |
| `___fixdfsi` | yes | 11 | 26 | `_envfx_update_snow_normal`, `_sinf`, `_cosf` | HOT |
| `___fixunssfsi` | yes | 10 | 16 | `_sm64_saturn_audio_spatial_quantize` (6) | HOT |
| `___sdivsi3` | yes | **10** | 14 | `_saturn_geo_enter_perspective`, `_get_vertex_coords`, `_push_clamped_int`, `_approach_s16_asymptotic_bool`, `_next_lakitu_state`, `_move_into_c_up` | **HOT — real 32-bit divides** |
| `___udivsi3` | yes | **5** | 9 | `_sm64_saturn_vdp2_frame_begin`, `_format_integer`, `_bhv_cmd_animate_texture`; also *inside* `___divdi3`/`___udivdi3` | **HOT** |
| `___floatsidf` | yes | 5 | 24 | `_envfx_update_snow_normal`, `_sinf`, `_cosf` | HOT |
| `___udivdi3` | yes | **5** | 5 | **`_draw_profiler_bar` (4), `_draw_profiler_mode_1` (1)** | HOT-reachable, **runtime-gated off** |
| `___gedf2` | yes | 4 | 12 | `_add_surface` (2), `_sinf`, `_cosf` | HOT |
| `___ashrsi3` | yes | 4 | 8 | `_init_controllers`, pause-menu render | HOT (menu = inactive) |
| `___divdf3` | yes | 4 | 5 | `_render_dialog_box_type` (2), `_set_transition_color_fade_alpha`, `_calculate_skybox_scaled_x` | HOT (dialog = inactive) |
| `___divdi3` | yes | **4** | 4 | **`_actor_saturating_mul_i64` only** | **HOT-reachable, guarded fallback — see §7** |
| `___ledf2` | yes | 3 | 21 | `_add_surface` | HOT |
| `___fixunsdfsi` | yes | 2 | 2 | `_set_transition_color_fade_alpha`, `_linearly_interpolate_solidity_negative` | HOT (inactive) |
| `___lshrdi3` | yes | 1 | 2 | `_sm64_saturn_sha256_finish`, `_update_and_return_cap_flags` | cold / boot |
| `___powisf2` | yes | 1 | 1 | `_evaluate_cubic_spline` | HOT (camera splines) |

**Every `___divdi3` call site, exhaustively:**

```
_actor_saturating_mul_i64  @0x606fe60 (jsr)
_actor_saturating_mul_i64  @0x606fe8a (jsr)
_actor_saturating_mul_i64  @0x606feb6 (jsr)
_actor_saturating_mul_i64  @0x606fece (jsr)
```

**Every `___udivdi3` call site, exhaustively:**

```
_draw_profiler_bar      @0x60675ec  @0x6067600  @0x606762a  @0x606763e
_draw_profiler_mode_1   @0x60677ca
```

## 5. Inline divides — `div1`/`div0s`/`div0u` and the DIVU unit

### There are none in application code

A naive opcode scan reports 3,652 `div1` across 237 functions. **That number
is almost entirely wrong**, and the reason matters. Symbol ranges from
`objdump` include literal pools and, in this image, whole embedded asset
blobs. Disassembling the region inside `_sm64_saturn_vdp2_frame_begin`
(0x06079920, size 0x3ac) that appeared to hold 11 `div1`:

```
 6079be8:	22 6f       	muls.w	r6,r2
 6079bea:	31 94       	div1	r9,r1
 6079bec:	22 6f       	muls.w	r6,r2
 6079bee:	31 9c       	add	r9,r1
 6079bf0:	22 6f       	muls.w	r6,r2
 6079bf2:	31 a0       	cmp/eq	r10,r1
```

The repeating `22 6f` halfword is the high half of `0x226f....` pointers
into the cart asset segment, and the companion halfwords ascend
monotonically (`31 8c, 31 94, 31 9c, 31 a0, 31 a4, …`). **This is a pointer
table, not code.**

Applying a strict detector — a run of ≥8 `div1` with a `div0s`/`div0u` setup
within the preceding 40 instructions, in the same function — the *only*
genuine stepwise divides in the entire image are:

```
REAL stepwise divide: ___sdivsi3            @0x6004478  run~32 div1  (setup present)
REAL stepwise divide: ___udiv_qrnnd_16      @0x6004576  run~16 div1  (setup present)
REAL stepwise divide: _sm64_saturn_sourceboot_actor_bundle @0x2240369c  run~8 div1
```

The third is `_sm64_saturn_sourceboot_actor_bundle` — a **data symbol** at
`0x224003b0` in the cart region spanning ~430 KB to
`_sm64_saturn_bob_sky_bitmap_end` at `0x2246a1e0`. It is the actor asset
bundle, and an 8-long `div1` run inside 430 KB of arbitrary bytes is
coincidence. 229 functions show `div1` with **no** `div0s`/`div0u` anywhere
in them, which is the signature of pool data throughout.

**Conclusion: GCC's SH-2 backend never expands a divide inline; every
integer division in this image goes through a helper.** The helper table in
§4 is therefore the *complete* divide census. **No power-of-two divisor is
being serviced by a divide** — those were already strength-reduced to
shifts, which is why `___ashlsi3`/`___lshrsi3` are the shift symbols present
and no power-of-two division survives to a helper.

### The DIVU hardware unit IS used — and a correction

**An earlier pass of this census claimed the DIVU was idle. That claim was
wrong and is retracted here.** It came from grepping the disassembly for
the 32-bit literal `0xffffff00`, which found only one hit. GCC does not
materialise these addresses as 32-bit literals: it uses a **sign-extended
16-bit** `mov.w`. The correct evidence, from `_sm64_saturn_div_s64_s32`
(`0x06067c46`, size `0x30`):

```
 6067c54:	92 0d       	mov.w	6067c72,r2	! ff08     <- r2 = 0xFFFFFF08 (DVCR)
 6067c56:	61 22       	mov.l	@r2,r1
 6067c58:	21 39       	and	r3,r1
 6067c5a:	22 12       	mov.l	r1,@r2                     <- clear sticky overflow
 6067c5c:	91 0a       	mov.w	6067c74,r1	! fefc     <- r1 = 0xFFFFFEFC (base)
 6067c5e:	11 61       	mov.l	r6,@(4,r1)                 <- DVSR   0xFFFFFF00
 6067c60:	11 45       	mov.l	r4,@(20,r1)                <- DVDNTH 0xFFFFFF10
 6067c62:	11 56       	mov.l	r5,@(24,r1)                <- DVDNTL 0xFFFFFF14 (starts divide)
 6067c64:	50 13       	mov.l	@(12,r1),r0                <- DVCR   status
 6067c66:	c8 01       	tst	#1,r0
 6067c6a:	51 16       	mov.l	@(24,r1),r1                <- quotient
```

This is `sm64_saturn_div_s64_s32`
(`src/port/saturn/gfx/saturn_render_native_math.h:44-66`), whose `__sh__`
branch drives the hardware divider directly. It exists as three out-of-line
local copies (`t`, sizes `0x30`/`0x2c`/`0x2c`) and has **seven hot
callers**:

```
_sm64_saturn_q16_vec3_normalize, _saturn_geo_enter_ortho_projection.part.0,
_saturn_geo_enter_perspective,   _saturn_geo_enter_camera,
_sm64_saturn_vec3_normalize_q16, _demo_terrain_queue_world_lower, _main
```

`_atan2_lookup_q16` (`0x06009170`) uses the identical idiom
(`! ff08` … `! fefc`, stores at +4/+20/+24) — this is
`sm64_saturn_atan2_q16_index`
(`src/port/saturn/runtime/saturn_engine_math_q16.h:44`), and it is why
`_atan2s`/`_atan2_lookup` show **zero** software-divide helper calls.

`_sm64_saturn_slavedriver_divu_q16_start_asm` (`0x0607d410`) is a third
DIVU user — hand-written assembly using the 32-bit literal form — reached
only from `_sm64_saturn_sourceboot_q16_kernel_probe_run`.

**So the DIVU is the working mechanism behind the Q16 conversion, not an
untapped resource.** The correct framing of the remaining software divides
is that they are in code the Q16 conversion has not reached. Notably
`_saturn_geo_enter_perspective` uses **both**: the DIVU for the aspect
ratio *and* one `___sdivsi3` elsewhere in the same function.

## 6. Float — the actual answer to the owner's question

**Float did not go away. It was made cheaper and then left in place.**

**1,827 soft-float helper call sites are hot-reachable**, of which **150 are
soft-double**. Top hot callers:

| Sites | Function | Note |
| ---: | --- | --- |
| 110 | `_find_wall_collisions_from_list` | collision, every frame |
| 78 | `_create_transformation_from_matrices` | matrix |
| 74 | `_sm64_saturn_audio_spatial_quantize` | **port's own code**, audio tick |
| 70 | `_evaluate_cubic_spline` | camera splines |
| 61 | `_rotate_triangle_vertices` | |
| 41 | `_envfx_update_snow_normal` | 9 soft-double; envfx inactive on BOB |
| 33 | `_get_pos_from_transform_mtx` | matrix |
| **32** | **`_sinf`** | **all 32 are soft-double** |
| 29 | `_guRotateF` | calls `sinf`/`cosf` |
| 28 | `_calculate_vertex_xyz` | 9 soft-double |
| 26 | `_mtxf_align_terrain_triangle` | |
| **20** | **`_cosf`** | **all 20 are soft-double** |

### `sinf`/`cosf` are double-precision, in-tree, and on the matrix path

`lib/src/math/sinf.c:22-36` — the libultra-derived implementation:

```c
static const du P[5] = {{1.0},
                        {-0.16666659550427756},
                        {0.008333066246082155},
                        {-1.980960290193795E-4},
                        {2.605780637968037E-6}};
...
float sinf(float x)
{
    double dx;  // double x
    double xsq; // x squared
```

Its callers are `_guRotateF` and `_calculate_vertex_xyz` — per-frame matrix
construction. Each `sinf` evaluates a 5-term polynomial in **double**, so
one call is ~14 `___muldf3` + 6 `___subdf3` + conversions, on a CPU with no
FPU. This is the most expensive single float construct on the frame path.

### The port's own per-frame core

Restricting to symbols owned by the port (`saturn_*`, `demo_*`, `actor_*`,
`geo_*`) rather than inherited SM64 engine code, **148 soft-float call
sites** remain, concentrated in:

| Sites | Function | Helpers |
| ---: | --- | --- |
| 74 | `_sm64_saturn_audio_spatial_quantize` | `mulsf3` 18, `subsf3` 11, `addsf3` 10, `lesf2` 10, `gesf2` 9, `divsf3` 7, `fixunssfsi` 6, `eqsf2` 3 |
| 18 | `_saturn_geo_enter_animated_part` | `mulsf3` 6, `addsf3` 6, `floatsisf` 6 |
| 18 | `_saturn_geo_enter_shadow` | `mulsf3` 10, `addsf3` 3, `floatsisf` 3, `subsf3` 1, `fixsfsi` 1 |
| 16 | `_saturn_geo_enter_object` | `mulsf3` 3, `addsf3` 3, `gesf2` 3, `subsf3` 2, `floatsisf` 2 |
| 6 | `_saturn_geo_enter_held_object` | `mulsf3` 3, `floatsisf` 3 |

**These five are the port's own code and are the cleanest remediation
targets in the report** — the Q16 conversion that was applied to the matrix
stack and to `atan2s` simply has not reached the geo-walk enter functions or
the audio spatialiser.

### Clean bill where it was earned

`_atan2s` and `_atan2_lookup` make **zero** native-math helper calls. The
`FORBIDDEN_CALLER` clauses in `sh2_native_math_sim_audit_contract_v2.txt`
still hold on this build.

`src/port/saturn/gfx/saturn_actor_meshlets.c` (50 KB) contains **zero**
occurrences of `float`, `double`, `f32` or `f64` — not even in comments. The
same is true of `saturn_demo_render.c` (235 KB),
`saturn_fast3d_vdp1_emit.c`, `saturn_actor_bank.c`, `saturn_render_job_*.c`,
`saturn_vdp1_frame_bank.c`, `saturn_scene_admission.c` and
`saturn_hud_atlas.c`. **`sm64_saturn_vdp2_frame_begin` has no float at
all.** Where the Q16 conversion was applied, it is complete.

### Where the float actually is, in source

| Site | File:line | What |
| --- | --- | --- |
| **audio spatialiser** | `src/port/saturn/audio/saturn_audio_spatial.c:156-263` | ~12 `__divsf3`, ~14 `__mulsf3`, ~20 `__addsf3`/`__subsf3`, ~15 compares **per call**; called once per active SFX per tick from `refresh_active_source_positions` (`sourceboot/source_audio_semantics.c:220,239,264`) |
| **`sqrtf`** | `src/port/saturn/compat/sqrtf.c:11-19` | 4 Newton iterations = 4 `__divsf3` + 4 `__addsf3` + 4 `__mulsf3` per call; called from the spatialiser (`:180`) and engine `math_util.c` |
| **animated-part translation** | `src/game/rendering_graph_node.c:1206-1228` | `gCurAnimData[i] * gCurAnimTranslationMultiplier`, `s16 * f32`, then converted straight back to Q16 at `:1247` |
| **shadow anim offset + rotate** | `src/game/rendering_graph_node.c:1354-1380` | ~8 `__mulsf3`, ~6 `__addsf3`; `sins`/`coss` here have exact Q16 twins that are not used |
| **held-object translation** | `src/game/rendering_graph_node.c:1842-1844` | `node->translation[i] / 4.0f` on `s16` input, then `float_to_q16` at `:1860` |
| **`shadow.c` double** | `src/game/shadow.c:366-367` | `M_PI / 180.0` is a **`double`** literal, so `calculate_vertex_xyz` pulls `__extendsfdf2`/`__muldf3`/`__divdf3`/`__truncdfsf2` |
| **float projection path** | `src/port/saturn/gfx/saturn_fast3d_frontend.c:630-775` | **dead on target** — inside `#else` of `SATURN_MTX_IS_Q16`, and `-DSATURN_MTX_IS_Q16=1` is set at `sourceboot/Makefile:738` |
| **castleviewer** | `src/port/saturn/castleviewer/main.c:132-370` | **not linked** into sourceboot |

### The float mirror is a real cost but is not soft-float

`saturn_mtxq_refresh_float_mirror` (`rendering_graph_node.c:193`) performs
**no FP arithmetic** — it is 16 integer bit-packs via
`sm64_saturn_q16_to_float`. But it is called from **11 sites** in the geo
walk (lines 722, 796, 858, 917, 975, 1039, 1259, 1398, 1607, 1876, 2957),
once per matrix push, kept alive solely because gameplay code still reads
`gMatStack` as a real `Mat4` — `obj_is_in_view`, `cameraToObject`/
`play_sound`, and the `GEO_CONTEXT_RENDER`/`GEO_CONTEXT_HELD_OBJ` callback
contract (documented in-place at `:1599-1606` and `:1871-1875`).

**Caution for future readers of helper-attribution output:**
`sm64_saturn_float_to_q16`, `sm64_saturn_q16_to_float`,
`sm64_saturn_float_to_q16_trunc` and `sm64_saturn_world_unit_from_float` are
`static inline` and contain **no FP arithmetic** — they are integer IEEE-754
field decode/encode, and exist precisely to eliminate
`__fixsfsi`/`__floatsisf`. Where they appear near soft-float callers it is
because they were inlined *into* a caller that has float arithmetic of its
own.

## 7. 64-bit math

**This is the healthy part of the image.** On hot functions: **665 multiword
add/sub ops (`addc`/`subc`/`negc`) and 184 `dmuls.l`/`dmulu.l`**. The
`dmul` instructions are the SH-2's native 32x32→64 multiply — exactly what
`sm64_saturn_q16_mul` in `saturn_q16_sh2.h` is built on, so their presence is
the design working.

| addc/subc | dmul | Function |
| ---: | ---: | --- |
| 64 | 9 | `_sm64_saturn_ztreme_frustum_aabb` |
| 28 | 20 | `_saturn_geo_enter_camera` |
| 24 | 6 | `_saturn_geo_enter_ortho_projection.part.0` |
| 22 | 6 | `_demo_render_prepare_publish` |
| 21 | 10 | `_actor_meshlet_live_depth_bounds` |
| 13 | 3 | `_sm64_saturn_vec3_normalize_q16` |
| 12 | 9 | `_sm64_saturn_ir_transform_one` |

`_sm64_saturn_ztreme_frustum_aabb` carried 4 `___divdi3` calls in the pinned
baseline (`sh2_native_math_baseline_v1.txt`: `HOT _sm64_saturn_ztreme_frustum_aabb ___divdi3 4`).
**It now has none** — it is pure `dmuls.l` + multiword add. That conversion
is complete and should be the template for the rest.

### The one surviving `___divdi3` path

`actor_saturating_mul_i64` (`src/port/saturn/gfx/saturn_actor_meshlets.c:140-152`)
still checks overflow by dividing:

```c
    if (left > 0) {
        if (right > 0 && left > INT64_MAX / right) return INT64_MAX;
        if (right < 0 && right < INT64_MIN / left) return INT64_MIN;
```

T2.6 did **not** remove this function; it added `actor_depth_fast`
(`saturn_actor_meshlets.c:575`) and made the divide-based
`actor_depth_reference` (`:458`) a **guarded fallback**, dispatched at
`:615-617`:

```c
            ? actor_depth_fast(transform->vertices[vertex], kernel)
            : actor_depth_reference(transform->vertices[vertex], transform,
```

The guard (`:555-559`) requires unit scale, `|position| < 2^40` and
`|forward| < 2^20`. **Mario at unit scale takes the fast path, so the
steady-state per-frame divide count is 0** — consistent with T2.6's measured
102.9 cycles/visit against T2.5's 3,725.8. But the fallback is live code on
the render path: any actor that violates the guard reverts to **10 64-bit
software divisions per vertex**. Against T2.5's measured 1,408 position
visits/frame (now ~704, since T2.6 made pass 2 zero-call), a full fallback
would cost **~7,040 `___divdi3` calls/frame**.

## 8. The 68K audio image — clean

`build/saturn/audio68k/pcm68k-heartbeat.elf`, 1,664 disassembled
instructions, 63 symbols. Scanning every mnemonic for
`divs`/`divu`/`divsl`/`divul`/`muls`/`mulu` and any `f*` FPU opcode:

**zero matches.** No libgcc helper symbols are present either — the symbol
table is entirely `sm64_saturn_pcm*`, `sm64_saturn_scsp_*`, `memcpy`,
`memset`, `_start`, `_fault`.

**The 68K driver does no division, no multiplication and no floating point.**
It is pure add/shift/compare/move. Nothing to fix.

## 9. Ranked remediation list

Per-frame call counts are given **only where a measurement supports them**.
Where the profile does not establish an execution count, the row says so.

| # | Target | Est. per-frame cost | Existing in-tree replacement |
| --- | --- | --- | --- |
| **1** | **`sinf`/`cosf` double-precision polynomial** (`lib/src/math/sinf.c`), reached via `_guRotateF` and `_calculate_vertex_xyz` | **Not measured.** 52 soft-double sites across the two functions; each call ≈ 14 `___muldf3` + 6 `___subdf3` + 3 conversions | A Q16 sin/cos table. `saturn_q16_sh2.h`'s `sm64_saturn_q16_mul` (`dmuls.l`) is the multiply primitive; the `atan2s` conversion is the precedent — it reached **zero** native-math calls |
| **2** | **`_sm64_saturn_audio_spatial_quantize`** (`audio/saturn_audio_spatial.c:156`) — 74–80 float sites, the largest float concentration in the port's own code | **Once per active SFX per game-loop tick**, from `refresh_active_source_positions` (`source_audio_semantics.c:220,239,264`), plus once per `play_sound` (`:319`). ~60 soft-float ops per SFX, plus `sqrtf`'s own 12 | Q16 throughout; `sm64_saturn_q16_mul` + `sm64_saturn_isqrt64` (already linked and hot) replace the `___divsf3`/`sqrtf` pair. **No Q16 twin exists today** |
| **3** | **`_saturn_geo_enter_{animated_part,shadow,object,held_object}`** — 58 float sites combined | Once per matching graph node per frame. **Node count per frame is not measured anywhere in the evidence set** | `saturn_matrix_kernels.h` / `sm64_saturn_mtxq_*` — `_saturn_geo_enter_camera` and `_saturn_geo_enter_ortho_projection` are already fully Q16 (20 and 6 `dmuls.l`, zero float) and are the direct template |
| **4** | **`actor_saturating_mul_i64`** divide-based overflow check | **0/frame in steady state** (fast path); **~7,040/frame** if the guard rejects | Replace the `INT64_MAX / right` tests with `dmuls.l`-based magnitude comparison, or widen `actor_depth_fast`'s guard so the fallback becomes unreachable and delete it |
| **5** | **`_find_wall_collisions_from_list` (110), `_create_transformation_from_matrices` (78), `_evaluate_cubic_spline` (70)** — inherited SM64 engine float | Every frame; call counts not measured | No in-tree Q16 equivalent exists. Largest raw win, largest fidelity risk — needs its own equivalence oracle like T2.6's |
| **6** | **`_push_clamped_int`** — `value / divisor` at `saturn_hud_layout.c:132` with a runtime `divisor` | Called 7× per HUD build (`:255,263,278,281,290,293,297`), `max_digits` ≤ 3 → **up to ~21 `___sdivsi3`/frame**. Highest *confirmed* per-frame divide count in the report | None needed — `max_digits` is only ever 1/2/3, so a fixed 3-step decimal ladder removes all of them |
| **7** | **`_get_vertex_coords`** — `index % (3 - shadowVertexType)` and `/ (…)` at `src/game/shadow.c:337-340` | Per shadow vertex. `shadowVertexType` is only ever 0 or 1, so the divisor is always **3 or 2** | A 2-entry switch. Note GCC computes the `%` from the same `___sdivsi3`, so one fix removes both |
| **8** | **`_saturn_geo_enter_perspective`'s remaining `___sdivsi3`** | 1 hot site — and this function *already* uses the DIVU for its aspect ratio | Route it through `sm64_saturn_div_s64_s32` like the aspect calculation beside it |
| **9** | `___udivsi3` at `_sm64_saturn_vdp2_frame_begin` (`saturn_vdp2_frame.c:208`) | **~1/second, not 1/frame** — gated behind `metrics_due`, `SM64_SATURN_VDP2_FRAME_HUD_TICK_DIVISOR == 30U` | None needed. Listed only to close it out |
| **10** | `___udivdi3` in `_draw_profiler_bar`/`_draw_profiler_mode_1` | **0/frame** — `gProfilerMode = 0` (`src/game/profiler.c:9`) | None needed. Consider compiling the profiler out to reclaim space |

**Free win, worth calling out separately:**
`saturn_geo_enter_held_object`'s `node->translation[i] / 4.0f`
(`rendering_graph_node.c:1842-1844`) is 3 `__floatsisf` + 3 `__divsf3`
followed immediately by 3 `float_to_q16` at `:1860`.
`(int32_t)node->translation[i] << 14` is the **bit-exact** Q16.16 of the
same value with no rounding loss, and deletes all nine helper calls.

**Ranking rationale.** Items 1–3 are float, and float is where the cost is:
1,827 hot soft-float sites against 22 hot divide sites of all widths. Item 4
is ranked below them because T2.6 already reduced its steady-state cost to
zero — it is a latent risk, not a live one. Items 6–8 are small in absolute
terms but are the cheapest fixes in the report: each is a bounded-divisor
lookup or a one-line reroute through machinery the port already owns.

## 10. What could NOT be determined

1. **Execution counts.** Everything in this report is *static call sites*
   and *static reachability*. "HOT" means reachable per frame, not executed
   per frame. Converting any row above into a cycles-per-frame figure needs
   the T2.4-style profiler probes; **only `actor_saturating_mul_i64`
   (via T2.5/T2.6) and `_sm64_saturn_vdp2_frame_begin` (1/frame by
   construction) have a defensible count here.**
2. **Which reachable callers actually run on the BOB demo route.** The
   1,278-function closure includes `_render_dialog_*`, `_render_pause_*`,
   `_print_*`, `_envfx_*` and `_draw_profiler_*`. `gProfilerMode = 0` makes
   the profiler rows dead; the dialog, pause-menu and envfx rows are almost
   certainly dead on this route too, but **that was not verified against a
   live trace** and their float sites are still counted in the 1,827.
3. **Indirect-call completeness.** The HOT closure covers direct calls plus
   the *declared* edges in the two checked-in route oracles. Any indirect
   call not in those fixtures is invisible to this pass, so **1,827 is a
   lower bound on hot-reachable float**, not an upper bound.
4. **Why the sim-route helper total went 582 → 1,402.** §2a establishes the
   number and that it is a real FAIL against the pinned contract. **It does
   not establish the cause.** It could be genuine regression, closure growth
   since the baseline ELF, or a contract that was pinned against a
   differently-configured build. Someone must diff this census against the
   `54d36006…` baseline before concluding anything. This is the single most
   actionable loose end in the report and it is *not* answered here.
5. **Geo-walk node count per frame**, needed to price remediation item 3.
   Not present in T2.4, T2.5, T2.6 or T2.7.
6. **Whether `actor_depth_fast`'s guard ever rejects in practice.** T2.6
   proved bit-identical output over 685,456 swept cases but did not report
   the fast/fallback dispatch ratio on the live route.
7. **A complete DIVU census.** Three DIVU users are confirmed by direct
   disassembly (`sm64_saturn_div_s64_s32`, `_atan2_lookup_q16`,
   `_sm64_saturn_slavedriver_divu_q16_start_asm`). A broader scan for the
   16-bit address literals (`ff00`/`ff08`/`ff10`/`ff14`/`fefc`) returns 67
   sites across 44 functions, but those constants are also ordinary masks
   (`0xff00`) and small negative integers, so **most of those are false
   positives and the list was not individually adjudicated.** The three
   confirmed users are a lower bound.

### Methodology error found and corrected during this census

An earlier draft asserted **"the DIVU hardware unit is idle"** as a headline
finding, based on `grep -oE '0xffffff(0[0-9a-f]|1[0-4])'` returning zero
matches. That grep was wrong twice over: `objdump` emits literal-pool
annotations **without** an `0x` prefix, and — decisively — GCC materialises
these addresses with a **sign-extended 16-bit `mov.w`** (`! ff08`,
`! fefc`), so no 32-bit form exists to find. The claim was retracted only
because a source-level cross-check surfaced `cpu_divu_64_32_set` call sites
that contradicted it. **Any future opcode- or literal-level census in this
image should cross-check against source before publishing an absence.** The
`div1` false-positive analysis in §5 is the same hazard in the other
direction: 229 functions appear to contain `div1` and none of them do.

## 11. Reproduction

```sh
export PATH=/d/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin:$PATH
ELF=releases/2026-08-15_1904_t2_6-product/sm64-saturn-sourceboot-e2.elf
sha256sum "$ELF"                       # 1b4ff08d...ad4
sh-elf-nm "$ELF" | grep -Ei '(div|mod|sf3|df3|float|fix|extend|trunc)'
sh-elf-nm -u "$ELF"                    # empty
sh-elf-objdump -d --no-show-raw-insn "$ELF" > full.dis

# DIVU: do NOT grep for the 32-bit literal -- GCC uses a sign-extended
# mov.w. Confirm by disassembling the known user instead:
sh-elf-objdump -d --start-address=0x6067c46 --stop-address=0x6067c76 "$ELF"
```

68K image:

```sh
M=/d/Code/RetroDev/sm64-saturn-port/work/upstream/SCSP_poneSound/m68k-elf
$M/m68k-elf-objdump.exe -d build/saturn/audio68k/pcm68k-heartbeat.elf \
  | grep -cE '\b(divs|divu|muls|mulu)\b'           # 0
```
