# Task 14 real final closure — Mario body-chain real depth, guard removal, and link attempt — 2026-08-09

Closure verification for the `GRAPH_NODE_TYPE_START`/`GRAPH_NODE_TYPE_CULLING_RADIUS`
conversion (commit `24b156fe`) — the task that actually closes the original
geo-recursion crash. This report supersedes
`task14-wave4-full-traversal-capacity-margin-2026-08-09.md`'s "headline
finding" (that Mario's real shipped render path detours into real recursion
at `START`): that detour no longer exists as of `24b156fe`, and this report
measures the real consequence.

## 1. Policy test

```
$ python tools/saturn/geo_walk_source_policy_test.py
geo walk source policy: PASS (2 allowlisted permanent call sites, 0 unaccounted)
```

(Ran again after the guard removal in §2 below — the count dropped from 3 to
2, matching the updated `ALLOWED_ENCLOSING_CALL_SITES` in
`tools/saturn/geo_walk_source_policy_test.py`.)

## 2. Guard disposition — `sSaturnGeoWalkActive` removed as dead code

**Traced concretely, not "probably":**

- `saturn_geo_walk_enter`'s switch (`src/game/rendering_graph_node.c`) now
  has a real case for every node type that can legitimately appear as a
  walk token: `MASTER_LIST, ORTHO_PROJECTION, PERSPECTIVE, CAMERA,
  BACKGROUND, LEVEL_OF_DETAIL, SWITCH_CASE, TRANSLATION_ROTATION,
  TRANSLATION, ROTATION, SCALE, BILLBOARD, ANIMATED_PART, DISPLAY_LIST,
  GENERATED_LIST, SHADOW, START, CULLING_RADIUS, OBJECT_PARENT, OBJECT,
  HELD_OBJ` — 20 types. The only type left uncased is `GRAPH_NODE_TYPE_ROOT`,
  which by construction never appears as a walk token: `geo_process_root`
  drives its own children via real recursion directly
  (`geo_process_node_and_siblings(node->node.children)`), never through
  `saturn_geo_walk_process_children`. Confirmed by grep: `init_graph_node_root`
  has exactly one call site (`src/engine/geo_layout.c:216`,
  `geo_layout_cmd_node_root`, the level-script `GEO_ROOT` command), never
  nested as a child anywhere else in the codebase.
- Therefore `saturn_geo_walk_enter`'s `default:` case
  (`saturn_geo_walk_dispatch_legacy` → `geo_try_process_children` → real
  recursive `geo_process_node_and_siblings`) is unreachable for any node
  type that can legitimately appear as a walk token.
- `saturn_geo_walk_process_children`'s `sSaturnGeoWalkActive` reentrancy
  guard's fallback branch (plain recursion instead of starting a fresh walk)
  fires **only** when `saturn_geo_walk_process_children` is called while a
  walk is already active. Every one of its callers is a `saturn_geo_enter_*`
  helper or a `geo_process_*` top-level wrapper, reached either (a) once,
  from `geo_process_root`'s own top-level real-recursion pass over root's
  direct children (no walk active yet), or (b) as a walk token *inside* an
  already-running walk's own `saturn_geo_walk_enter` switch, which returns
  child pointers to the runtime instead of ever calling
  `saturn_geo_walk_process_children` itself. The **only** path that could
  call it from *inside* an active walk was the now-unreachable default-case
  detour. Grep confirms every `geo_process_*` wrapper function
  (`geo_process_master_list`, `geo_process_object`, `geo_process_held_object`,
  etc.) has exactly one call site each, all inside
  `geo_process_node_and_siblings`'s own switch — never called from
  `saturn_geo_walk_enter`'s converted cases, which call the
  `saturn_geo_enter_*` helpers directly instead.
- `sm64_saturn_geo_walk_runtime_run` (`saturn_geo_walk_runtime.c`) is a pure
  iterative `while` loop calling `ops->enter/dispatch/leave` as plain
  callbacks — it never re-enters itself, so no other path into
  `saturn_geo_walk_process_children` while a walk is active exists anywhere
  in the runtime layer either.

**Action taken:** removed `sSaturnGeoWalkActive`, its `if`/fallback branch,
and the two `= true` / `= false` toggles from
`saturn_geo_walk_process_children`; updated the stale present-tense
documentation in `saturn_geo_walk_dispatch_legacy`'s own comment and the
wave 1-4 historical doc block that had (accurately, at the time) argued for
keeping the guard. `tools/saturn/geo_walk_source_policy_test.py`'s
`ALLOWED_ENCLOSING_CALL_SITES` dropped from 3 to 2:
`geo_try_process_children` (the generic children-only bridge, still needed
for `GRAPH_NODE_TYPE_ROOT` and any `GRAPH_RENDER_CHILDREN_FIRST`-flagged
node) and `geo_process_root` (the top-level walk kickoff) — both
structurally necessary bridges, not hazards. Confirmed this is the lowest
achievable count: the policy test's own docstring now states this
explicitly.

## 3. Real depth measurement — Mario's actual body chain

**Scenario modeled:** fully-equipped Mario, moving (so
`geo_switch_mario_stand_run` selects `mario_geo_render_body`, the
`GEO_NODE_START()`-gated, LOD-based branch — the overwhelming majority of
real play time, `src/game/mario_misc.c:343-352`), near LOD range (the common
close-camera case, `actors/mario/geo.inc.c:1790-1793`), normal (non-metal,
non-vanish) body via `geo_switch_mario_cap_effect` case 0
(`mario_geo_load_body`/`mario_geo_body`), right hand in the closed-grip
"holding an object" case
(`geo_switch_mario_hand` case 0, `actors/mario/geo.inc.c:76-85`, the
`GEO_HELD_OBJECT` node at line 84).

**Real node sequence, transcribed 1:1 against the actual
`actors/mario/geo.inc.c`** (line references verified this session, not
carried over from the wave 4 report, which pre-dates the `START`/
`CULLING_RADIUS` conversion this report closes):

```
PERSPECTIVE -> CAMERA -> OBJECT_PARENT -> OBJECT (Mario, canonical trunk)
  -> SHADOW (mario_geo:1810) -> SCALE (mario_geo:1812)
    -> [ASM, ASM siblings, not admitted] -> SWITCH_CASE(stand_run, mario_geo:1816)
      -> START (mario_geo_render_body:1788)
        -> LOD near (mario_geo_render_body:1790) [medium/far siblings pending, not admitted]
          -> SWITCH_CASE(cap_effect, mario_geo_load_body:1748)
            -> ANIMATED_PART root (mario_geo_body:104)
              -> ANIMATED_PART butt (:106)
                -> [ASM, ASM siblings] -> ROTATION torso-tilt (:110)
                  -> ANIMATED_PART torso (:112)
                    -> [ANIMATED_PART head, pending sibling, not descended]
                    -> ANIMATED_PART left-shoulder (:118) [full subtree modeled]
                      -> ANIMATED_PART left-arm (:120) -> left-forearm (:122)
                        -> SWITCH_CASE(hand) -> ANIMATED_PART wrapper (mario_geo_left_hand:57)
                          -> [ASM sibling] -> SCALE (:60) -> DISPLAY_LIST leaf (:62)
                    -> ANIMATED_PART right-shoulder (:128, torso's LAST child)
                      -> ANIMATED_PART right-arm (:130) -> right-forearm (:132)
                        -> SWITCH_CASE(hand) -> ANIMATED_PART wrapper (mario_geo_right_hand:77)
                          -> [ASM sibling] -> SCALE (:80) -> DISPLAY_LIST leaf (:82)
                          -> HELD_OBJECT (:84, sibling of SCALE) <- the load-bearing node
                  -> [left-thigh-mount, right-thigh-mount pending siblings, not descended --
                      independently confirmed shallower than the arm chain: thigh->leg->
                      ankle->[asm,scale]->display = 6 levels from mario_butt vs. rotation->
                      torso->shoulder->arm->forearm->switch->wrapper->[scale/held] = 8-9]
```

**Real per-type push shapes used** (verified against the current
`saturn_geo_walk_enter` switch this session, commit `24b156fe`):
`PERSPECTIVE/CAMERA/OBJECT_PARENT/OBJECT/SCALE/ROTATION/ANIMATED_PART` —
single-subtree, `leave_required=true`; `SHADOW/GENERATED_LIST(ASM)/
SWITCH_CASE/START/LEVEL_OF_DETAIL/DISPLAY_LIST` — no leave;
`HELD_OBJECT` — admitted unconditionally, no children in real content (`GEO_
HELD_OBJECT` is always a leaf command in `geo.inc.c`), so no leave either.
`OBJECT_PARENT`/`OBJECT` take the single-subtree combined-leave path per
wave 3's independently-verified fact that `node.children` is always `NULL`
for both in real content.

**This is a real, compiled, and run probe** against the actual
`src/port/saturn/runtime/saturn_geo_walk_runtime.c` (not a hand-derived
estimate) — every node above (41 total, including the un-descended
placeholder siblings that legitimately occupy stack frames while deeper
subtrees drain) was wired with real `child`/`sibling` pointers matching the
transcribed structure, driven through the unmodified runtime, reading
`walk.high_water`:

```
$ gcc -std=c11 -Wall -Wextra -Werror -Isrc/port/saturn/runtime \
    mario_chain_probe.c src/port/saturn/runtime/saturn_geo_walk_runtime.c \
    -o mario_chain_probe && ./mario_chain_probe
nodes_used=41
ok=1 fail_reason=0 final_depth=0 high_water=19 capacity=256
```

`final_depth=0` confirms no stranded frames (matches wave 3/4's own
validation criterion). `PROBE_TRACE=1 ./mario_chain_probe` traces every
`enter` event; the peak (19) is reached identically at **both** hands'
`SCALE -> DISPLAY_LIST` leaf push, *not* at the `HELD_OBJECT` node itself
(which sits one level shallower, as a sibling of `SCALE` rather than nested
inside it) — a real, run-verified correction of an initial hand-derived
expectation that the held-object hand would be strictly deeper than the
non-holding one; it is not, because `HELD_OBJECT` doesn't add depth beyond
what the display-list leaf already reaches. This is exactly the kind of
mistake hand-derivation is prone to and the reason this report drives the
real runtime instead. The harness is preserved at
`C:\Users\estee\AppData\Local\Temp\claude\D--Code-RetroDev-sm64-saturn-port\4cf09e19-94d4-4d00-a8d5-aa76996f837d\scratchpad\task2_mario_probe\mario_chain_probe.c`
(throwaway probe, not part of the shipped test suite, not committed — same
precedent as wave 3/4's own harnesses).

**Margin, against the manifest's real current values**
(`build/saturn/sourceboot/generated/saturn_geo_depth_manifest.h`,
regenerated this session): `capacity=256`, `safety_margin=16`, usable budget
`256 - 16 = 240` frames.

**Real measured peak: 19 frames. Real margin: 240 - 19 = 221 frames to
spare.**

**This is the load-bearing safety proof the original crash needed:**
Mario's real shipped render path — moving, cap state present, holding an
object — now runs **entirely on the bounded iterative array**, with zero
detours into native C-stack recursion anywhere along the chain, confirmed
by a real run, not an estimate. Before commit `24b156fe` (this session,
earlier today), this exact scenario detoured into real, unbounded recursion
at the very first `START` node (`mario_geo_render_body`'s own top-level
command) — the wave 4 report's "headline finding". That detour is gone.

## 4. Real target link attempt

Toolchain: `work/yaul-install/bin` (sh-elf-gcc 14.3.0, ld), `sm64-port/
.yaul.env`, MSYS2 at `C:\msys64` (not `C:\Program Files\Git`'s bundled
`/mingw64`, `/usr` — the ordinary Git Bash environment resolves those to a
*different*, cygwin-flavored toolchain that silently breaks `make`'s
environment-variable inheritance; using `C:\msys64\usr\bin\bash.exe`
directly, matching `tools/saturn/with-msys-toolchain.ps1`'s intent, was
required to get a real build).

Command (`make -C src/port/saturn/sourceboot`, matching
`Makefile.saturn.mk`'s own `sourceboot` target and the load-bearing `-C`
requirement documented at `Makefile.saturn.mk:710-721` — `-f` instead of
`-C` breaks the Makefile's own relative `-specs=sourceboot.specs`):

```
SATURN_DEMO_PATH=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 \
SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=0 SATURN_DEMO_NEAR_CLIP=0 \
SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=4 \
SATURN_SOURCE_CART_STAGE_SECTORS=4
```

**Compile: fully succeeded.** All ~230 translation units (including the
guard-removal edit to `rendering_graph_node.c`) compiled clean (warnings
only, all pre-existing/unrelated — `-Wbad-function-cast`,
`-Wmaybe-uninitialized`, `-Wunused-function` in `saturn_demo_render.c`, none
touching this session's changes).

**Link: blocked by the same linker-script INCLUDE defect previously
recorded**, not a new or different issue:

```
/d/.../work/yaul-install/bin/ld: cannot open linker script file
saturn_geo_depth_manifest.ld: No such file or directory
collect2: error: ld returned 1 exit status
```

**Root cause, diagnosed concretely this session:** `sourceboot.specs`'s
`*link:` spec (`-T sourceboot-cart.x`) is placed by GCC's internal
`LINK_COMMAND_SPEC` template very early in the assembled `collect2`/`ld`
command line — confirmed by re-running the exact captured `collect2`
invocation with `-v`: `-T sourceboot-cart.x` lands at character offset 108,
while the `-L build/saturn/sourceboot/generated` flag (from
`src/port/saturn/sourceboot/Makefile:526`,
`SH_LDFLAGS += -L$(SOURCEBOOT_GENERATED)`) lands at offset 525 — *after*
`-T`. `ld` processes its command line in order and resolves a script's
`INCLUDE` directives immediately when `-T` is encountered, using only the
`-L` directories registered *so far* — so `sourceboot-cart.x`'s own
`INCLUDE saturn_geo_depth_manifest.ld` (`sourceboot-cart.x:18`) fails: the
directory that would resolve it hasn't been added yet. `sourceboot-cart.x`
itself resolves fine independent of this (it's found via the process's
CWD, which the `-C src/port/saturn/sourceboot` sub-make sets for the whole
build). This ordering is controlled by GCC's internal spec-template
placement of `%{T*}`, not by anything reorderable from the Makefile's own
flag ordering — the proper fix is at the linker-script or specs level
(e.g. giving the top-level script an absolute-path `INCLUDE`, or moving the
manifest fragment somewhere `ld` searches by default), out of scope for
this task.

**Diagnostic-only workaround** (not committed, not a fix — used solely to
confirm what lies beyond this defect, per this task's own instructions):
copying `build/saturn/sourceboot/generated/saturn_geo_depth_manifest.ld`
into the link's CWD (`src/port/saturn/sourceboot/`, since `ld` checks CWD
for `INCLUDE` targets independent of `-L` ordering) let the link proceed
past the defect. This is exactly the "linker-script INCLUDE defect"
previously recorded as one of the two toolchain blockers — same issue, not
a new one — now diagnosed to its precise root cause for whoever picks up
the real fix (Task 14 completion plan Tasks 4-6 territory).

**With that workaround, the link reached the real, separately-tracked
memory-budget assert:**

```
/d/.../work/yaul-install/bin/ld: HWRAM margin below libyaul's TLSF
control-block floor: the heap libyaul builds at ___end would overrun the
top of HWRAM and mirror into low memory. Shrink a static HWRAM consumer.
```

Real numbers, read from the link's own (still-written, despite the
assert failure) `.map` file and `sourceboot-cart.x`'s `MEMORY` block:

- `___end = 0x060ff498`
- `ram` region: `ORIGIN = 0x06004000`, `LENGTH = 0x000FC000` → top =
  `0x06100000`
- Actual margin: `0x06100000 - 0x060ff498 = 0x0B68 = 2,920 bytes`
- Required margin (`__sourceboot_required_hwram_margin`,
  `sourceboot-cart.x:138`): `0x1B00 = 6,912 bytes`
- **Deficit: 6,912 - 2,920 = 3,992 bytes (0xF98) short of the required
  HWRAM margin.**

This is the same, separately-tracked gap Task 14's completion plan Tasks
4-6 already own (HWRAM/LWRAM budget); this report only supplies the real,
current numbers, not a fix. Note this build configuration includes several
`SATURN_DEMO_*`/`SATURN_RENDERER_PIPELINE=4` flags from a prior session's
own link-attempt script that are unrelated to this task's own scope — the
3,992-byte deficit is specific to *this* flag combination and should not be
read as a universal number for every build configuration.

## 5. Bottom line

The original "Mario holding something" master-stack-overrun crash scenario
is **structurally fixed**: every real, content-authored node type Mario's
actual render path can reach — including the `START` node that gated the
whole chain and the `HELD_OBJECT` node the original crash was about — now
runs on the bounded iterative geo-walk array, confirmed by a real,
compiled, and run probe against the actual runtime (19 frames peak, 221
frames of real margin against the current 240-frame usable budget). The
`sSaturnGeoWalkActive` reentrancy guard that used to provide a safety net
for the pre-closure detour is now proven unreachable and has been removed
as dead code, and the policy test reports the lowest achievable call-site
count (2). What remains unresolved is unrelated to recursion safety: a
pre-existing linker-script `INCLUDE` ordering defect blocks the real
on-hardware link for this specific demo configuration, and — once worked
around diagnostically — reveals a real, separately-tracked 3,992-byte HWRAM
budget deficit (Task 14 completion plan Tasks 4-6). Neither of those is a
recursion-safety regression; both are pre-existing, already-scoped gaps
this task was never meant to close.
