# Fast3D matrix stack and crude VDP1 emission — design

Status: draft, pending user review.
Author: session design dialogue, 2026-07-20.
Supersedes: nothing. Extends `docs/saturn/HANDOFF_2026-07-20.md` §"Next
implementation: shared Fast3D-to-VDP1 lowering", step 1 and (per this
session's revision) step 3.

## Scope

This increment extends `src/port/saturn/gfx/saturn_fast3d_frontend.c` from a
pure display-list walker/counter into a real Fast3D interpreter that produces
a visible, untextured, flat-shaded VDP1 frame from the actual source display
list emitted by `game_loop_one_iteration()`.

In scope:

- Decode `G_MTX`, `G_POPMTX`, `G_MOVEMEM` (viewport case), `G_VTX`,
  `G_GEOMETRYMODE`, `G_TRI1`, `G_TRI2`.
- A new fixed-point (Q16.16) 4x4 matrix stack module, host-testable
  independent of the Saturn target.
- Per-vertex MP (modelview * projection) transform, perspective divide,
  viewport mapping.
- Backface culling.
- Crude VDP1 emission: one degenerate-quad polygon command per source
  triangle, flat colored, no texture.
- Coarse painter ordering using the existing shared depth-quad
  infrastructure (see "Painter ordering reuse" below) — **conditional on
  the user's answer to open question 5**, since the handoff's unqualified
  "none may become a Bob-specific exception" wording for painter ordering
  may or may not permit even this narrow, reuse-based form. If declined,
  this increment ships with raw display-list emission order instead.
- New profile counters so a headless probe can report transform/emission
  workload without a screenshot.
- Native host tests for the new matrix module and decode logic, wired into
  the existing native C contract-test harness, with mutation verification
  per the project's global testing rule.

Out of scope (deferred to later, separately evidenced increments):

- Real near/far/frustum clipping. This increment only *rejects* triangles
  that fail the existing visibility test; it does not clip and re-emit
  partial geometry.
- Textures, lighting, Gouraud shading. Flat color only, explicitly labelled
  as such in any capture per the handoff's visual-gate requirement.
- Fine-grained/global painter ordering. Only the coarse depth-bucket
  exception below.
- GPL-licensed renderer reuse (Sonic Z-Treme, SlaveDriver-Engine world
  renderer). Both are pattern-study candidates for later clipping/ordering
  work per `docs/saturn/UPSTREAM_CODE_LEDGER.md`, but pulling either into
  `saturn_fast3d_frontend.c` would make that file GPL-derived and require it
  to move behind the `src/port/saturn/gpl/` isolation boundary documented in
  `docs/saturn/SLAVEDRIVER_ADAPTATION.md`. That boundary decision is
  independent of this increment and should be made before, not during, any
  later increment that touches those references.
- Offline triangle-to-quad pairing (`tools/saturn/quad_pairing.py`). That
  tool builds adjacency-matched true quads for precompiled *static* meshes
  (Castle Area 1 tiles). It does not apply to a live, per-frame Fast3D
  triangle stream from a running level script, where triangle adjacency is
  not known ahead of time. This increment's degenerate-quad-per-triangle
  emission is a distinct, simpler mechanism for dynamic geometry.
- The cart `READY` probe capture and the first real screenshot. Both are
  gated on the user manually driving the Ymir SDL GUI profile (see handoff
  §"Cart bank gate"). This increment's code must compile-verify and pass
  host tests independent of that; the actual capture is a separate,
  user-executed step.

  Note: the handoff's "Completion sequence after resuming" list
  (`HANDOFF_2026-07-20.md` lines 320-336) orders step 3 (obtain a genuine
  `READY` probe) before step 4 (implement the lowering stage and capture a
  frame), whereas this increment performs step 4's implementation half
  without first securing that probe. This is an intentional, acceptable
  deviation from that literal session-resumption order, not a contradiction
  of it: the "Next implementation" section's own step ordering
  (`HANDOFF_2026-07-20.md` lines 256-278), which this increment follows
  directly, places capture after implementation, and this increment's code
  correctness does not depend on cart presence. The deviation is called out
  here explicitly rather than left implicit.

## Reference / attribution record

| Reference | Pin / license | Files inspected | Reuse mode |
| --- | --- | --- | --- |
| Inherited SM64 fork | Covered by `PROVENANCE.md`:148-161's blanket `Project12x/sm64-port` entry (`src/pc/`), which records "No root license file found at the pinned revision" and requires that "any distribution plan must separately address the rights and obligations of the decompilation, port code." This increment relies on that existing entry and does not resolve its caveat. | `src/pc/gfx/gfx_pc.c`: `gfx_sp_matrix` (~L560-600), `gfx_sp_pop_matrix`, `gfx_sp_movemem`/`gfx_calc_and_set_viewport` (~L937-961), `gfx_sp_geometry_mode` (~L932-935), F3DEX_GBI_2 command dispatch (~L1371-1437, control-flow/matrix/vtx/geometrymode opcodes), G_TRI1/G_TRI2 decode (~L1438-1452: G_TRI1's F3DEX_GBI_2 body at L1440 is `gfx_sp_tri1(C0(16,8)/2, C0(8,8)/2, C0(0,8)/2)`; G_TRI2 at L1448-1451, active under this build because F3DEX_GBI_2E transitively defines F3DEX_GBI_2 and F3DEX_GBI per `include/PR/gbi.h`:90-99) | Behavior/semantics reference. The float-matrix algorithm, s15.16 split-matrix decode, 11-deep modelview stack, viewport formula, and F3DEX2 `G_MTX_PUSH` XOR convention are ported to fixed point; this is a distinct implementation (no FPU on SH-2), not a copy. |
| `src/port/saturn/gfx/saturn_projected_workarea.h` | in-tree, SPDX MIT, adapted from `libyaul/libmic3d render.c` at pinned `6012f79f237773378c8014e70d8998ad95a38d98` | whole file | Direct reuse (already in tree). This increment is the first *runtime* consumer of `sm64_saturn_projected_quad_analyze`/`_is_visible` outside `castleviewer`'s precompiled-quad path. |
| `src/port/saturn/gfx/saturn_render_queue.h` | in-tree, no external license question | whole file (`sm64_saturn_render_item_t`/`sm64_saturn_render_queue_t`, lines 22-38); usage in `castleviewer/main.c` (push/emit split, e.g. lines 638-644, 848-853, 1097-1109); host test `test_source_identified_render_queue` in `tools/saturn/runtime_contract_test.c:75-111` | Considered, not reused this increment (see "Painter ordering reuse" below for rationale). |
| Yaul | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | `vdp1/cmdt.h` (`vdp1_cmdt_t` layout, `vdp1_cmdt_list_init`), `scu/map.h` (`LWRAM`/`HWRAM` macros) | Existing dependency/API use; no copying. |
| `malucard/sm64-psx` | no repository-wide license established | (none newly inspected this increment) | Explicitly excluded. Already study-only in `docs/saturn/PSX_PORT_ARCHITECTURE_LESSONS.md` for licensing reasons; separately a poor technical fit here since its GTE coprocessor matrix semantics do not transfer to SH-2/VDP1. Not referenced for this increment's matrix or emission code. |

**Ledger gap to flag, not silently fix**: the `gfx_pc.c` row's function-name-
and-line-range citations are more granular than any existing mention of
that file in `docs/saturn/` (`HANDOFF_2026-07-20.md`,
`ENGINE_PORT_ARCHITECTURE.md`, and the M4 evidence file all reference it
only at whole-file granularity). Per `UPSTREAM_CODE_LEDGER.md`'s
per-change checklist ("Before implementing a row, add the exact source
paths used to the commit note and update `PROVENANCE.md` if the reuse mode
changes") and `PROVENANCE.md`'s "Required update points" ("inspects
additional reference files before implementation"), the implementation
plan must either (a) add a dedicated `gfx_pc.c` row to
`UPSTREAM_CODE_LEDGER.md` with these function names, line ranges, and the
pinned local commit, or (b) explicitly confirm with the user that this
increment continues to rely on the existing blanket `Project12x/sm64-port`
entry and its unresolved distribution/rights caveat, rather than leaving
that choice implicit.

## Module: `saturn_matrix.{h,c}`

New files under `src/port/saturn/gfx/`. No `yaul.h`, no Saturn-specific
header, no dependency on anything outside `<stdint.h>`. This mirrors the
existing pattern of `saturn_transform.h`/`saturn_projected_workarea.h`
(pure-C headers already compiled by the native `verify-runtime-contracts`
gate — see "Testing" below) and is what makes host-testing the highest-risk
math possible before any emulator involvement.

Fixed-point format: **Q16.16**, signed 32-bit. Chosen because:

- N64 source matrices are natively s15.16, stored as split
  integer/fraction 16-bit halves (see `gfx_sp_matrix` decode). Q16.16
  reconstructs them with zero conversion loss — same bit layout, no
  rounding step introduced by this port.
- It matches the existing convention in `saturn_transform.h`
  (`sm64_saturn_vec3_normalize_q16`, `sm64_saturn_world_to_view`).

Matrix products accumulate in `int64_t` (SH-2 has native `dmuls.l`,
32x32->64 signed multiply — a hardware op, not software-emulated) and shift
right by 16 on store back to `int32_t`. This relies on an explicit
assumption about input magnitude: every matrix entry reachable through
SM64's object/camera graph — world coordinates, and the composed
rotation/scale/translation sub-blocks after up to 11 levels of modelview
stacking — is assumed to keep its integer part well under Q16.16's own
ceiling of ±32768, so that (a) each raw `int64_t` product (max magnitude
2^31 * 2^31 = 2^62 per term; note two same-sign terms at the true format
extreme already sum to 2^63, one past `INT64_MAX`, so the accumulator is
not unconditionally safe at the format's edges) and the 4-term dot-product
sum stay far inside `int64_t` range in practice, and (b) the final `>>16`
narrowing store does not wrap `int32_t` for any matrix this frontend will
actually produce. This is an assumption about the data, not a property the
arithmetic enforces on its own — a malformed display list or a runaway
hierarchical composition could in principle produce an entry that silently
wraps on the narrowing store rather than trapping, corrupting that matrix
entry rather than merely losing precision. Consistent with this codebase's
existing bounded-resource pattern (`saturn_command_arena.h`,
`saturn_render_queue.h`, `saturn_projected_workarea.h`,
`saturn_texture_residency.h`, each carrying an `overflowed` bool asserted in
`tools/saturn/runtime_contract_test.c`) — noting that pattern has so far
only guarded capacity/count structures, not arithmetic magnitude, so this
is a new but consistent application of it — `sm64_saturn_matrix_mul` should
assert in debug builds, or count-and-flag in release, any product/store
whose value would not round-trip through the `>>16` narrowing, so a
violated bound is visible rather than silently corrupting a matrix entry.

API surface (exact signatures to be finalized in the implementation plan,
not here):

- `sm64_saturn_mtx_t`: `int32_t[4][4]` Q16.16.
- `sm64_saturn_matrix_stack_t`: fixed array of 11 `sm64_saturn_mtx_t`
  (matches the reference's `modelview_matrix_stack_size < 11` guard),
  current depth, overflow flag; plus a separate projection matrix and a
  dirty-flagged MP composite.
- `sm64_saturn_matrix_decode(const int32_t *gbi_words, sm64_saturn_mtx_t *out)`
  — implements the split s15.16 decode.
- `sm64_saturn_matrix_mul(const sm64_saturn_mtx_t *a, const sm64_saturn_mtx_t *b, sm64_saturn_mtx_t *out)`.
- `sm64_saturn_matrix_stack_push/pop/load/mul` — modelview stack ops, with
  push guarded at depth 11 and overflow counted rather than trapped (a
  bounded-resource pattern consistent with the rest of this codebase's
  arenas/queues).
- `sm64_saturn_matrix_stack_mp(...)` — returns the current MP composite,
  recomputing only when the dirty flag is set (see "Lazy MP composition"
  below).

### F3DEX_GBI_2E push semantics

`sourceboot`'s Makefile defines `F3DEX_GBI_2E=1`. Under F3DEX2, the
reference dispatches `gfx_sp_matrix(C0(0, 8) ^ G_MTX_PUSH, ...)` — the push
bit is inverted relative to the raw command parameter. The decode in
`saturn_fast3d_frontend.c` must apply the same XOR before calling the stack
push/load logic, or push/no-push will be backwards for every matrix command
in the real source stream. This is called out explicitly because it is the
single most likely place to introduce a silent, hard-to-see bug: geometry
would still transform, just with the wrong stack depth, and the visible
symptom (subtly wrong pose) would look like a math bug rather than a control
bit bug.

### F3DEX_GBI_2 G_POPMTX count semantics

Under F3DEX2 (active here because `F3DEX_GBI_2E` implies `F3DEX_GBI_2`, per
`include/PR/gbi.h` L90-92), `gSPPopMatrixN` does not encode the pop count
directly: `include/PR/gbi.h` L2886 defines
`gSPPopMatrixN(pkt, n, num) -> gDma2p(pkt, G_POPMTX, (num)*64, 64, 2, 0)`,
i.e. the command's `w1` data word holds `num*64`, not `num`. The reference
recovers the real count with `gfx_sp_pop_matrix(cmd->words.w1 / 64)`
(`src/pc/gfx/gfx_pc.c` L1380). The decode in `saturn_fast3d_frontend.c` must
divide the raw `G_POPMTX` data word by 64 before passing it to
`sm64_saturn_matrix_stack_pop`, or every real `gSPPopMatrix(1)` in the source
display list (raw value 64) will attempt to pop 64 levels instead of 1.
Given the stack's depth-11 bound, this either saturates the
overflow/underflow path immediately or, if unguarded, desyncs the modelview
stack for the rest of the frame — the same "still transforms, wrong stack
depth" failure class called out above for the push-bit XOR, just on the pop
side. `test_matrix_*` in `tools/saturn/runtime_contract_test.c` must include
a case asserting this `/64` recovery (e.g. decode a `gSPPopMatrix(1)`-style
word and assert the stack pops exactly one level, not 64), alongside the
push-XOR test called for above.

### Lazy MP composition

The reference recomputes `MP_matrix = modelview * projection` eagerly after
every `G_MTX`/`G_POPMTX`. `G_MTX` commands arrive in bursts (multiple
pushes/loads before the next `G_VTX`), so eager recomposition wastes a
64-multiply 4x4*4x4 product on intermediate states nothing ever reads. This
port instead dirty-flags the stack on any modelview or projection change and
recomputes MP once, lazily, the first time it's needed for a `G_VTX` in the
current frame.

## Frontend extension

`saturn_fast3d_frontend.c`/`.h` gain:

- A `sm64_saturn_matrix_stack_t` and a viewport record as frontend state
  (currently the frontend struct holds only `profile`).
- A vertex buffer sized at `MAX_VERTICES=64` (`src/pc/gfx/gfx_pc.c` L39),
  matching the reference's vertex-load capacity. The reference actually
  declares `loaded_vertices[MAX_VERTICES + 4]` (gfx_pc.c L102), but the +4
  padding is scratch space for `G_TEXRECT`/`G_FILLRECT` 2D overlay-quad
  corners (`gfx_draw_rectangle`, gfx_pc.c L1197-1263) — a feature this
  increment does not implement (see Scope: textures excluded). Since this
  increment's in-scope `G_TRI1`/`G_TRI2`/`G_VTX` decode path never touches
  those slots, the Saturn vertex buffer is sized at exactly 64 entries
  rather than 68, to avoid spending scarce HWRAM (~9.6 KB budget) on unused
  padding. Each entry holds enough to emit flat-colored geometry: at
  minimum a transformed/projected position (reusing
  `sm64_saturn_projected_vertex_t` from `saturn_projected_workarea.h` — see
  below) and a flat color/material reference. UV decode may be stored for
  forward compatibility but is unused (untextured pass).
- `G_GEOMETRYMODE` tracking, at minimum the backface-cull-mode bit(s), since
  crude emission performs backface culling.
- New `sm64_saturn_fast3d_profile_t` fields (extending the existing
  `frame_serial`/`command_count`/... struct in
  `saturn_fast3d_frontend.h`): `triangles_transformed`,
  `triangles_emitted`, `reject_near_far` (existing
  `sm64_saturn_projected_quad_is_visible` failure), `reject_backface`,
  `reject_degenerate`, `reject_vertex_range`, `reject_command_capacity`,
  `modelview_stack_overflow`, `max_modelview_depth_reached`. Exact naming
  is an implementation-plan detail; the requirement is that every rejection
  path increments a distinct, reportable counter — nothing is silently
  dropped.

Existing control-flow handling (`G_DL`/`G_ENDDL` branch-vs-call, return
stack, depth cap, command cap) is unchanged. The new decode cases do **not**
slot into the existing dispatch as-is, and the implementation plan must
address this explicitly:

- `sm64_saturn_fast3d_count_command`'s current signature,
  `(sm64_saturn_fast3d_profile_t *profile, uint8_t opcode)`
  (`saturn_fast3d_frontend.c:13-14`), receives only the extracted opcode
  byte. Every new case needs data this signature discards: `G_VTX`'s
  vertex-count/start-index and vertex-array pointer split across `w0` bits
  1-19 and all of `w1`; `G_MTX`'s push/replace flag in `w0` bits 0-7 and
  its matrix pointer in `w1`; `G_MOVEMEM`/viewport's selector in `w0` and
  record pointer in `w1`; `G_GEOMETRYMODE`'s clear-mask in `w0` bits 0-23
  and set-mask in all of `w1`; and `G_POPMTX`'s pop count, carried in `w1`
  (see `include/PR/gbi.h`'s `gDma2p`/`gSPGeometryMode`/`gSPVertex` macros
  for the exact bit layout). The implementation plan must change this
  function's signature to take the full command (e.g. `const Gfx
  *command`, or an explicit `(uint32_t w0, uint32_t w1)` pair) before any
  new case can be written, not just add case labels to the existing one.
- Separately, the `G_DL`/`G_ENDDL` handling in
  `sm64_saturn_fast3d_frontend_submit` (`saturn_fast3d_frontend.c:93-124`)
  is a sequence of `if` statements over the full `Gfx *command`, not a
  switch. Only `sm64_saturn_fast3d_count_command`'s internal opcode
  dispatch is an actual `switch`, and it is the wrong one for real decode
  per the point above. The new `G_MTX`/`G_POPMTX`/`G_MOVEMEM`/`G_VTX`/
  `G_GEOMETRYMODE`/`G_TRI1`/`G_TRI2` decode logic needs its own dispatch
  (most naturally a new switch inside `_submit`, alongside — not inside —
  the existing `G_DL`/`G_ENDDL` if-chain, with access to
  `command->words.w0` and `.w1`) rather than being described as joining
  "the existing `sm64_saturn_fast3d_count_command`/`_submit` switch," which
  does not exist as a single structure.

## Memory layout

Measured against the current `sourceboot` link map at handoff time:
`___end = 0x060fda64`, `ram` region top `0x06100000` (from
`sourceboot-cart.x`: `ORIGIN = 0x06004000, LENGTH = 0x000FC000`) — **9,628
bytes** of free HWRAM, and `sourceboot` currently allocates no VDP1 command
list at all. For comparison, `castleviewer`'s link map has 307,748 bytes
free at the same point, because `sourceboot` links the real SM64 engine
(game loop, level scripts, object/camera/collision/animation) that
`castleviewer` doesn't.

Saturn Low Work RAM (Yaul `LWRAM(x) = 0x00200000 + x`, `LWRAM_SIZE =
0x00100000`, 1 MiB) is completely unused by `sourceboot`.
`sourceboot-cart.x` currently declares only `ram` and `cart` MEMORY
regions — no `lwram`. `castleviewer` already uses LWRAM elsewhere
(`collision_pool.c`'s `CASTLE_COLLISION_POOL`, a raw pointer cast to
`LWRAM(0x000C0000U)` consumed by a manual arena allocator) — but that
access has no linker involvement at all: `castleviewer`'s Makefile has no
`SH_LDSCRIPT` override and no `.x` file exists in its directory, so it
links against Yaul's stock `yaul.x`, which declares only a `ram` MEMORY
region and no `lwram` region. **No Saturn-port target in this repo has
ever had a linker-tracked LWRAM region.** The MEMORY-region approach
proposed here — a compiler-placed C array in LWRAM with a linker symbol
and link-time overflow detection — is new surface for this codebase, not
merely "wiring in" an established pattern, and the implementation plan
should treat it as such: confirm the SCU/CPU correctly decodes LWRAM writes
from an SH-2 program under this MEMORY region, and that `make verify`'s
existing E2/`.cart_rodata` VMA checks aren't disturbed by the added
region.

Split:

| Storage | Contents | Size | Rationale |
| --- | --- | ---: | --- |
| HWRAM `.bss` | Modelview stack (11 x 64 B = 704 B) + projection/MP (128 B) + vertex buffer (64 entries) + `vdp1_cmdt_list_t` header (8 B, see "VDP1 backend entry point") | ~1.8-2.3 KB (exact vertex entry size is an implementation-plan detail) | Hot, per-vertex-touched working set; fastest SH-2 access. Static `.bss`, not heap — overflow becomes a link-time failure, not a runtime `malloc`-returns-NULL failure on hardware. |
| LWRAM | VDP1 command list staging (`vdp1_cmdt_t`, `__aligned(32)`, exactly 32 bytes/command, confirmed from `yaul/vdp1/cmdt.h`) | Sized by the implementation plan against measured frame-budget needs, not by memory scarcity — LWRAM has 1 MiB free vs. ~9.6 KB of HWRAM, so the real ceiling is VDP1 fill-rate and the 20 ms frame budget, not command-list capacity | Write-once-per-frame-then-DMA access pattern; the CPU never re-reads a command after building it, so it does not need HWRAM's speed. |

All static allocation, so an overflow in either region is a link failure,
consistent with the rest of this codebase's bounded-resource discipline.

### Linker script change

Add an `lwram (Wx) : ORIGIN = 0x00200000, LENGTH = ...` region to
`sourceboot-cart.x` and a corresponding output section. This is a change to
a file the handoff explicitly flags as sensitive (it places `.cart_rodata`
at the fixed `0x22400000` VMA that the cart loader depends on). The
implementation plan must treat `make verify` in
`src/port/saturn/sourceboot/Makefile` (E2 entry-point and `.cart_rodata` VMA
checks) as a required gate *after* this specific edit, not just after the
final increment — i.e., verify immediately once the linker script changes,
before writing decode logic on top of it.

### VDP1 backend entry point

The shared `src/port/saturn/gfx/saturn_vdp1_backend.h` currently only
exposes `sm64_saturn_vdp1_backend_init`, which calls
`vdp1_cmdt_list_alloc()` (heap-allocated) to obtain a `vdp1_cmdt_list_t *`.
That allocator actually performs *two* separate heap allocations under the
hood: one for the 8-byte `vdp1_cmdt_list_t` header (`{cmdts; count;}`,
`cmdt.h:219-224`) and one for the backing `vdp1_cmdt_t` array
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1_cmdt.c:24-39`), then wires
them together via `vdp1_cmdt_list_init(vdp1_cmdt_list_t *cmdt_list,
vdp1_cmdt_t *cmdts)` (`cmdt.h:260`) — which itself does no allocation, only
`cmdt_list->cmdts = cmdts; cmdt_list->count = 0;`.

Because `sm64_saturn_vdp1_backend_t.list` is declared as a pointer
(`vdp1_cmdt_list_t *list`, `saturn_vdp1_backend.h:15`), a genuinely no-heap
`sm64_saturn_vdp1_backend_init_with_storage` entry point needs caller-owned
storage for *both* pieces, not just the `vdp1_cmdt_t` array already
budgeted for LWRAM: the 8-byte `vdp1_cmdt_list_t` header itself must live
somewhere the backend can point `list` at. The implementation plan should
resolve this explicitly — either (a) change `sm64_saturn_vdp1_backend_t.list`
from a pointer to an embedded `vdp1_cmdt_list_t` value, so
`vdp1_cmdt_list_init(&backend->list, cmdts)` initializes it in place with no
extra storage decision, or (b) keep it a pointer and have
`_init_with_storage` take an additional caller-supplied `vdp1_cmdt_list_t *`
argument (naturally a small static HWRAM instance, since 8 bytes is
negligible next to either the ~2 KB HWRAM or 1 MiB LWRAM budgets). Either
way, the memory layout table above should gain an explicit (if trivial)
line for this header so it isn't rediscovered mid-implementation.

Recommendation for the implementation plan: add this second init function
to `saturn_vdp1_backend.h` rather than fork the header, since the handoff
requires "the existing shared ... command lifetime for VDP1 emission" —
one shared contract, two ways to supply backing storage. This is a
recommendation, not yet a decision the user has confirmed.

No manual cached/uncached address translation is needed for the
LWRAM-backed command list. `vdp1_sync_cmdt_list_put` forwards to
`vdp1_sync_cmdt_put`, whose DMA source is built as
`CPU_CACHE_THROUGH | (uintptr_t)cmdts`
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp_sync.c:408`). OR-ing in
`CPU_CACHE_THROUGH` (`0x20000000`, `cpu/cache.h:25`) maps a cached
`LWRAM(x)` pointer (`0x00200000+x`) to exactly `LWRAM_UNCACHED(x)`
(`0x20200000+x`) for the DMA — the same mechanism that already maps the
existing heap-allocated (HWRAM) `cmdts` pointer used by
`sm64_saturn_vdp1_backend_upload` (`saturn_vdp1_backend.h:70`) today.
Choosing a cached `LWRAM(x)` base for the new command-list storage is
therefore correct as-is; the implementation plan must not add its own
cached-to-uncached translation on top of it.

**Zero-initialization note**: the existing heap path does not rely on any
allocator zero-guarantee — `sm64_saturn_vdp1_backend_init` explicitly
`memset`s `backend->list->cmdts` to zero after `vdp1_cmdt_list_alloc()`
(`saturn_vdp1_backend.h:30-31`), because `vdp1_cmdt_list_alloc()`'s
underlying `memalign()` returns uninitialized memory. `vdp1_cmdt_list_init()`
likewise never zeroes its caller-provided buffer — it only assigns the
pointer and resets `count` (`vdp1_cmdt.c:53-61`). Separately, a new LWRAM
output section would not be `.bss` and so is never visited by crt0's
`_bss_clear()`, which walks only `[__bss_start, __bss_end)`
(`third_party/libyaul/libyaul/kernel/sys/init.c:79-91`) — symbols that
bracket exactly the `.bss` section placed in the `ram` (HWRAM) MEMORY
region, disjoint from LWRAM at `0x00200000`. So there is no path — not the
allocator, not `vdp1_cmdt_list_init()`, not crt0 — that zeroes the new
LWRAM-backed cmdt array automatically. `sm64_saturn_vdp1_backend_init_with_storage`
must therefore perform the same explicit
`memset(cmdts, 0, sizeof(vdp1_cmdt_t) * capacity)` the existing heap path
performs, before first use, mirroring `saturn_vdp1_backend.h:30-31`. The
Memory Layout section's "all static allocation... consistent with the rest
of this codebase's bounded-resource discipline" framing should not be read
as implying zero-init parity between HWRAM `.bss` (crt0-zeroed) and the new
LWRAM output section (not crt0-zeroed) — only capacity-overflow safety is
shared between them, not initialization.

## Crude emission path

VDP1 has no native triangle primitive; it draws quads. Standard technique:
a degenerate quad with one vertex duplicated. **No existing code in this
repo builds a degenerate quad from a live triangle** — `castleviewer`'s
quad emission consumes pre-baked 4-corner primitives from offline asset
tooling (`bake_castle_uv.py`, `quad_pairing.py`), not raw triangles. This
increment introduces that convention fresh: for source triangle indices
`(i0, i1, i2)`, emit corner order `(i0, i1, i2, i2)` (last vertex
duplicated) into `sm64_saturn_projected_quad_analyze`'s 4-index contract.
This is flagged explicitly as new, not reused, so it gets scrutiny.

### Painter ordering reuse

`saturn_projected_workarea.h` already provides exactly the near/far
rejection and depth information this pass needs, MIT-licensed and already
in tree:

- `sm64_saturn_projected_quad_analyze` computes `min_z`/`max_z`/`center_z`
  and clip-flag AND/OR reduction from four projected vertices.
- `sm64_saturn_projected_quad_is_visible` rejects a quad that crosses either
  depth plane or exceeds a maximum screen-space span.

`castleviewer` already uses `quad.max_z` to bucket precompiled primitives
into a coarse painter-order histogram before emission (see
`castle_tile_depth`/bucket usage in `castleviewer/main.c:611,641,798`, and
the separate `mario_bucket_head`/`tail`/`next` scheme at
`castleviewer/main.c:68-69,772-808`). This increment *could* reuse the same
pattern for live triangles: after transform and projection, each surviving
triangle's `max_z` (from the existing quad-analyze call already required
for visibility rejection) would bucket it into a small fixed number of
coarse depth buckets, emitted far-to-near. `max_z` is used rather than
`quad->center_z` because `sm64_saturn_projected_quad_analyze`'s `center_z`
field (`saturn_projected_workarea.h:170-171`) is computed directly from
`indices[0]` and `indices[2]` only, bypassing the corner-reduction loop
that produces `min_z`/`max_z`; for this increment's own `(i0, i1, i2, i2)`
degenerate-quad convention that silently drops `i1`'s depth entirely, so
the bucket key would become an accident of which source vertex the display
list encoded as the triangle's middle index rather than a property of the
triangle's true depth extent. `max_z`, by contrast, is populated by the
reduction loop at `saturn_projected_workarea.h:155-169`, which does scan
all four corner indices `(i0, i1, i2, i2)` — so it is well-defined here:
the true maximum over all three distinct triangle vertices, one of which
happens to repeat.

**This is an open question, not a decision made in this design doc.** The
handoff's source-frame visual gate states, without qualification: "Texture,
lighting, clipping, and painter ordering are global renderer work; none may
become a Bob-specific exception" (`HANDOFF_2026-07-20.md:239-240`). That
wording is a scope/timing policy about painter ordering as a category, not
a test of whether the specific functions used to implement it are new
code. The fact that `sm64_saturn_projected_quad_analyze`/`_is_visible` are
already in tree and MIT-licensed answers a licensing/reuse question; it
does not answer whether doing *any* painter ordering — coarse or fine —
inside `sourceboot` for this increment is itself the kind of target-specific
exception the gate prohibits. `sourceboot` is currently a direct,
single-level Bob-omb closure, so ordering behavior shipped there now is
scoped to Bob-omb regardless of code reuse. The closest existing precedent
for this bucket pattern lives in `castleviewer`, which the handoff
explicitly designates a diagnostic harness, not the production path
(`HANDOFF_2026-07-20.md:14-16`).

Without emitting *some* ordering, VDP1's strict submission-order drawing
with no Z-buffer will produce visually incoherent overlapping geometry,
which risks failing the visual gate's "legible capture" requirement for a
different reason. That tension is real, but this design doc does not
resolve it unilaterally — see open question 5 below. If the user says the
exception is not acceptable, this increment should ship with raw
display-list emission order instead, and any resulting visual incoherence
is documented as a known, accepted limitation until the global ordering
pass lands.

This increment also deliberately does not route emission through the
existing `sm64_saturn_render_item_t`/`sm64_saturn_render_queue_t` pair
(`saturn_render_queue.h`), even though its shape —
`source_bank`/`source_primitive`/`depth_key` fields, fixed capacity,
`overflowed` flag — matches the handoff's step 2 ("lower source triangles
into source-identified intermediate records... do not emit directly from a
scene module") closely, and it is already load-bearing in
`castleviewer/main.c`'s two-phase push-then-draw structure. Two reasons
this increment would use depth-bucketed direct emission instead, if the
ordering exception above is approved:

- `sm64_saturn_render_queue_t` is sized against `castleviewer`'s fixed,
  compile-time-known `DRAW_ITEM_COUNT` (tiles + Mario BSP leaves). A live
  per-frame Fast3D triangle stream from a running level script has no
  equivalent compile-time bound; reusing the same fixed-capacity contract
  here would need its own capacity derivation and overflow-handling story,
  which is implementation-plan-level work, not assumed here.
- `render_queue.order[]` is populated identity-only today (`push` sets
  `order[slot] = slot`); nothing in the tree currently sorts it by
  `depth_key` at runtime — `castleviewer` achieves correct paint order via
  precompiled push order, not runtime resorting. So adopting the struct
  as-is would not, by itself, provide the far-to-near reordering this
  increment's coarse depth-bucket pass needs; that sorting logic would have
  to be added regardless of which container holds the items.

This is a judgment call, not a settled decision — flagged as open question
6 below, since it trades away the source_bank/source_primitive diagnostic
identity that `saturn_render_queue.h` is explicitly designed to preserve in
favor of a simpler direct-emit path for this first increment.

### Per-triangle pipeline

1. MP-transform the three indexed vertices (position already computed at
   `G_VTX` time per lazy MP composition).
2. Perspective divide (x/w, y/w; keep a depth value for z).
3. Viewport map using the real decoded viewport (from `G_MOVEMEM`), not a
   hardcoded resolution.
4. Push all three (as a 4th, duplicated) into
   `sm64_saturn_projected_workarea_t`, matching the existing MIT-licensed
   contract.
5. `sm64_saturn_projected_quad_analyze` + `_is_visible`: reject near/far and
   over-span triangles (counted, not silent).
6. Backface cull via signed area of the (non-duplicated) three corners,
   evaluated in the same **pre-viewport, Y-up view/clip space** that both
   existing techniques in this codebase use — `castleviewer`'s
   `view_triangle_facing`/`view_triangle_is_culled`
   (`castleviewer/main.c:503-518`, pre-projection 3D view space) and the
   reference's `gfx_sp_tri1` (`gfx_pc.c:729-751`, perspective-divided x/w,
   y/w clip space) — i.e. *before* step 3's viewport mapping, not after it.
   Do not compute the signed area from the step-3 screen-space (Y-down)
   coordinates already pushed into `sm64_saturn_projected_workarea_t`: that
   space is produced by the same Y-flip castleviewer's `project_vertex`
   applies (`screen_y = 112 - fix16_high_mul(reciprocal, fixed_y)`,
   `main.c:443`/`482-483`), which inverts the sign of every cross product
   relative to the reference's Y-up convention. If the implementation plan
   instead chooses to cull in screen space for some other reason, it must
   say so explicitly and state the resulting (inverted) comparison
   directions for `G_CULL_FRONT`/`G_CULL_BACK` relative to `gfx_sp_tri1`'s
   `cross <= 0` / `cross >= 0` (`gfx_pc.c:743`/`746`), and add a host test
   asserting a known-facing triangle from the real source display list
   culls/survives correctly under both `G_CULL_BACK` and `G_CULL_FRONT` —
   the same rigor already given the F3DEX2 `G_MTX_PUSH` XOR bit above, and
   for the same reason: a silent sign/convention error here produces
   plausible-looking but subtly wrong geometry, not a crash. Respects the
   tracked `G_GEOMETRYMODE` cull bit (counted).
7. Degenerate-zero-area reject (counted) — guards a triangle that survives
   the above but still has zero screen-space area (e.g. exactly
   edge-on).
8. Depth-bucket for coarse painter order, if the user approves the
   exception in open question 5 below; otherwise emit in raw decode order
   (see "Painter ordering reuse" above).
9. Emit: reserve one `vdp1_cmdt_t` from the LWRAM-backed backend, flat
   color from source material state, no texture.

Vertex-index-out-of-range and VDP1 command-capacity-exceeded are rejected
earlier in the pipeline (at `G_TRI1`/`G_TRI2` decode and at backend
reservation respectively) and counted the same way.

## Testing

The project has two separate, pre-existing host-test populations, and this
increment's math belongs to the one that was not initially assumed:

- `make -f Makefile.saturn.mk verify-tools` runs
  `tools/saturn/test_tools.py` (currently 59 tests) — a Python `unittest`
  suite over **offline asset-tooling scripts** (extraction, baking,
  compilation of precompiled meshes/textures/collision). It does not touch
  target C code and gains no new tests from this increment.
- `make -f Makefile.saturn.mk verify-runtime-contracts` compiles
  `tools/saturn/runtime_contract_test.c` with the **host's native C
  compiler** (`$(CC)`, `-std=c11 -Wall -Wextra -Werror`), against headers
  under `src/port/saturn/gfx/` and `src/port/saturn/platform/` only (no
  Yaul, no cross-compiler), then runs the resulting binary. Its tests are
  plain `assert()`-based functions called from `main()` — no framework.
  This is the correct, already-existing home for `saturn_matrix.h` tests:
  add `test_matrix_*` functions in the same style, exercising the Q16.16
  decode, stack push/pop/overflow, the F3DEX2 push-XOR, the `G_POPMTX`
  `/64` count-recovery (§"F3DEX_GBI_2 G_POPMTX count semantics"), and MP
  composition, each compared against known values (and, where practical,
  against `src/pc/gfx/gfx_pc.c`'s float math on the same inputs, asserting
  bounded error). Also add a case asserting a known-facing triangle from a
  representative source display list culls/survives correctly under both
  `G_CULL_BACK` and `G_CULL_FRONT` (§"Per-triangle pipeline" step 6 —
  this is the test that would catch a Y-up/Y-down sign-convention error in
  the backface cull).

**Gate gap to flag, not silently fix**: `verify-runtime-contracts` is
*not* part of the default `verify-all` chain (`verify-all: verify-tools
classify-source verify-hello verify-hwtest`). Today it must be invoked
explicitly. This increment adds real target-relevant tests to a target that
the standard gate doesn't run. The implementation plan should call this out
to the user as a question — whether to also add `verify-runtime-contracts`
to `verify-all` — rather than changing that unrelated chain unilaterally as
a side effect of this work.

Per the project's global mutation-testing rule: after the new tests pass,
apply 3-5 mutations to `saturn_matrix.c` and the new frontend decode logic
(flip a shift direction, swap a row/column index, negate a translation
term, flip the F3DEX2 push-XOR, off-by-one the stack depth guard, and feed
matrix entries at the Q16.16 magnitude extreme — `INT32_MIN`/`INT32_MAX` —
into `sm64_saturn_matrix_mul` to confirm the overflow guard trips instead
of silently wrapping) and confirm each is caught by a test failure. >20%
survival means the tests need work, per the standing rule.

## Acceptance for this increment

- `src/port/saturn/sourceboot` builds clean (`make source-assets && make &&
  make verify`), including after the `sourceboot-cart.x` LWRAM addition.
- `make -f Makefile.saturn.mk verify-runtime-contracts` passes with new
  matrix/decode tests included, and survives the mutation pass above.
- The extended `sm64_saturn_fast3d_profile_t` reports non-zero
  `triangles_transformed`/`triangles_emitted` and populated reject counters
  when a representative Gfx display list is submitted to
  `sm64_saturn_fast3d_frontend_submit` directly, verified as a new
  host-native test added to `tools/saturn/runtime_contract_test.c` (the
  same harness already extended for matrix/decode tests above) — **not**
  via the existing headless no-cart probe path. That probe's own
  documentation (`HANDOFF_2026-07-20.md:114-116`) states it "proves that
  the source loader is executing and rejecting the absent cartridge before
  source-data access" and "is not a cart-load proof"; `sourceboot/main.c`
  hangs in an unconditional `for (;;) {}` on any cart-load failure, before
  `sm64_saturn_fast3d_frontend_init`, `sm64_saturn_source_runtime_configure`,
  or the game loop ever run, so no display list reaches the frontend in
  that scenario. On-target verification against the real Bob-omb source
  display list is deferred to the same user-gated, cart-`READY`-dependent
  follow-up as the first VDP1 capture below.
- A first VDP1 capture, explicitly labelled untextured/flat-shaded, is
  **desired** but gated on the user driving the cart-profiled Ymir GUI to a
  `READY` probe — not a blocking condition for this increment's code to be
  considered complete and mergeable.

## Open questions for implementation planning

1. Exact vertex-buffer entry layout/size (affects the HWRAM budget table
   above by a small, non-blocking margin).
2. Whether to extend `saturn_vdp1_backend.h` with a caller-storage init
   entry point (and whether that means an embedded `vdp1_cmdt_list_t`
   value or a separate caller-supplied header pointer — see "VDP1 backend
   entry point"), or take a different approach to LWRAM-backed command
   lists (recommendation given above, not yet confirmed).
3. Whether `verify-runtime-contracts` should join `verify-all` (flag to
   user, don't decide unilaterally).
4. Exact coarse-depth-bucket count (castleviewer's existing bucket scheme
   is a starting reference, not a mandated value) — moot if question 5 is
   answered no.
5. Whether the coarse depth-bucket painter-ordering pass described in
   "Painter ordering reuse" is an acceptable narrow exception given the
   handoff's unqualified "none may become a Bob-specific exception" wording
   for painter ordering (`HANDOFF_2026-07-20.md:239-240`), or whether this
   increment should instead ship with raw display-list emission order
   (accepting visually incoherent overlapping geometry as a known,
   documented limitation) until the separately-scoped global ordering pass
   lands. **This determines whether pipeline step 8 and question 4 apply
   at all.**
6. Whether per-triangle emission should route through
   `sm64_saturn_render_item_t`/`sm64_saturn_render_queue_t` as a bounded,
   source-identified intermediate stage (matching handoff step 2 literally)
   instead of depth-bucketing straight to VDP1 command reservation, given
   that module's existing use in `castleviewer` for the same push-then-draw
   shape. Not decided here; flag to the user before implementation.
