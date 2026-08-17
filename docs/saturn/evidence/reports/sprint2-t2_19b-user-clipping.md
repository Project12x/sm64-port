# Sprint 2 Task T2.19b — VDP1 user clipping, established and declined

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `eef321fe`.
- Task: enable VDP1 user clipping, now that T2.17 measured VDP1 plotting
  93.67% of the frame and wasted fill costs frames.
- Verdict: **the premise does not hold, and the change was not shipped.** The
  port already programs system clipping at the exact screen bounds, and VDP1
  system clipping is a **full rectangle anchored at (0,0)** — not a
  lower-right-only bound. A user clip box equal to the screen therefore
  removes **zero** pixels. No narrower box is available without changing what
  is drawn. What landed instead is the executable proof of that, retained as
  the gate the per-package variant must pass.
- Fill saved by the change as scoped: **exactly zero**, proved, not estimated.
- No build was run and no emulator was launched. **No FPS is reported.**
- Commit: `dbe6c4ca` (oracle + two gates), plus the docs commit carrying this
  report.

---

## 1. Where clipping is programmed today

| Thing | Where | Value |
| --- | --- | --- |
| System-clip command, list index 0 | `src/port/saturn/gfx/saturn_vdp1_backend.h:69-70` (heap variant) and `:105-106` (caller-storage variant, the one sourceboot uses) | lower-right `(319, 223)` |
| The clip value | `src/port/saturn/sourceboot/main.c:2328` | `INT16_VEC2_INITIALIZER(319, 223)` |
| Local coordinates, list index 1 | `saturn_vdp1_backend.h:71-72` and `:107-108`; value at `main.c:2329` | `(0, 0)` |
| Display | `src/port/saturn/sourceboot/main.c:2027-2029` | `INTERLACE_NONE`, `HORZ_NORMAL_A`, `VERT_224` → 320×224 progressive |
| Per-command clip bits | nowhere | `user_clipping_mode` has **no assignment anywhere in `src/`** |

Both frame banks get their own fixed prefix, written once after the cold actor
upload retires (`main.c:2330-2347`), so system clipping is live on the product
path for either bank. Local coordinates of `(0,0)` mean command coordinates
*are* framebuffer coordinates — there is no offset to reconcile, unlike
SlaveDriver, whose `EZ_userClip` needs `+160`/`+120` (`SPR.C:322-331`).

The bit itself lives in `CMDPMOD` bits 10-9, spelled by libyaul as one
two-bit field, `user_clipping_mode:2`
(`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp1/cmdt.h:114`). Every
`vdp1_cmdt_draw_mode_set` in the port sets only `color_mode`, `cc_mode` and
occasionally `end_code_disable`. There are 19 such call sites under
`src/port/saturn/gfx/` — 14 in `saturn_demo_render.c`, 2 each in
`saturn_fast3d_vdp1_emit.c` and `saturn_ir_texture.c`, 1 in
`saturn_actor_material.c` — and none touches bits 10-9.

## 2. The mode, proven rather than inferred

The task required the mode be proven from documentation and from the
references, because getting it backwards blanks the screen. It is proven from
**first-party Sega headers in-tree**, which is stronger than either.

SGL's own header defines all three states literally
(`work/upstream/sonic-z-treme/Compiler/SGL_302j/INC/SL_DEF.H:190-192`):

```c
#define No_Window   (0 << 9)   /* no window (default) */
#define Window_In   (2 << 9)   /* display inside the window */
#define Window_Out  (3 << 9)   /* display outside the window */
```

SlaveDriver's header names the same two non-default values, with the same
numbers and unambiguous comments
(`work/upstream/slavedriver-engine/SPR.H:87-88`):

```c
#define  UCLPIN_ENABLE       0x0400     /* CLIP IN enable  */
#define  UCLPOUT_ENABLE      0x0600     /* CLIP OUT enable */
```

Sega's SGL manual states the same three states in prose —
`Documentation/DOC/210A_US/SPRITE.TXT:392-394`: WindowIn "Display inside a
specified window", WindowOut "Display outside a specified window", NoWindow
"Display without regard to windows (default)".

**So the field value the port would want is `2` — draw inside — and bit 10 is
the enable, bit 9 the invert.** Both references set exactly that on nearly
every command.

Two independent emulators were read as behaviour cross-checks only; both are
GPL and no code from either was copied:

- **Ymir** (GPL-3.0), which is also this project's measurement instrument:
  `libs/ymir-core/src/ymir/hw/vdp/renderer/vdp_renderer_sw.cpp:965-995` layers
  a user-clip test on top of the system-clip test and comments the polarity
  explicitly — "clippingMode = false -> draw inside, reject outside".
- **Kronos/Yabause** (GPL-2.0):
  `yabause/src/core/video/opengl/compute_shader/include/vdp1_prog_compute.h:42-53`,
  whose shader branches on `((CMDPMOD >> 9) & 0x3u) == 2u` labelled
  `//Draw inside` and `== 3u` labelled `//Draw outside`.

### 2.1 An encoding trap for whoever ships this later

libyaul declares the `CMDPMOD` bitfield MSB-first — `msb_enable` is commented
"Bit 15" and appears first (`cmdt.h:110-119`). GCC allocates bitfields from the
MSB on big-endian targets and from the LSB on little-endian ones, and this
port builds SH-2 **big-endian** (`-m2 -mb`,
`src/port/saturn/sourceboot/Makefile:807`). So on target,
`.user_clipping_mode = 2` correctly encodes `0x0400`.

**The same struct on an x86 host encodes the fields in the opposite order**,
so a host fixture must not reuse libyaul's bitfield to reason about the word.
The oracle below therefore models `CMDPMOD` as a raw `uint16_t` and does its
own shift-and-mask, matching what the hardware and both references see.

## 3. The interaction with system clipping — the finding that decides the task

**VDP1 system clipping is a rectangle anchored at (0,0), covering all four
edges.** The command carries only the lower-right corner, which invites the
reading that the top and left are unclipped and that user clipping is the only
way to bound them. That reading is wrong. Ymir's predicate
(`vdp_renderer_sw.cpp:997-1006`) rejects `x < 0`, `x > sysClipH`, `y < 0` and
`y > sysClipV`; its changelog entry "Extend line clipping to the left and top
edges by one pixel to compensate for some inaccuracies"
(`ymir-agent/CHANGELOG.md:714`) is only meaningful under that same reading.
Kronos declares a `systemclipX1` field (`sys/vdp1/include/vdp1.h:57`) and never
writes or reads it — it is dead, not a contrary model.

Consequences, in order:

1. **A user clip box of `(0,0)`–`(319,223)` is exactly the system clip box.**
   Every pixel it would reject is already rejected. Zero fill removed.
2. **A narrower box is the only variant with any effect** — which is precisely
   SlaveDriver's arrangement, per-sector portal bounding boxes
   (`WALLS.C:2247-2253`), and step 2 of
   `sprint2-reference-technique-gaps.md` §5 item 4.
3. **No narrower box is safe here.** The whole 320×224 framebuffer is
   displayed; there is no letterbox at the VDP1 level (the "8-line letterbox
   crop" at `saturn_fast3d_frontend.c:442-458` is a source-viewport mapping
   from the N64's 240 lines, not a black band on the Saturn framebuffer), and
   the HUD is a sparse VDP2 NBG0 tile layer (`main.c:1204-1228`; the
   `verify-saturn-hud-no-vdp1` gate exists precisely to keep it off VDP1),
   not an opaque band that could hide VDP1 output. Any inset would delete
   visible pixels and violate the correctness bar.

Also worth recording because it is the opposite of what one would assume:
**both engines leave hardware pre-clipping on** — neither sets
`pre_clipping_disable` / `PCLP_ENABLE` — and so does this port, by default.
There is no gap there.

## 4. The clip rectangle, and why

The correct rectangle for this renderer is `(0,0)`–`(319,223)`: the full
screen, identical to the system clip, because local coordinates are `(0,0)`
and the display is 320×224 with nothing occluding it. That is also the reason
the change is worthless — the correct rectangle is the one that removes
nothing.

## 5. Fill saved, and its basis

**Zero. This is proved, not estimated.**
`tools/saturn/vdp1_user_clip_test.c` rasterises 22 primitives under both
configurations and compares the framebuffer byte-for-byte *and* the written
pixel count:

```
vdp1-user-clip: 22 primitives, 511955 pixels walked, 228674 written
vdp1-user-clip: user clip (0,0)-(319,223) is bit-identical to system
                clipping alone -- 0 writes saved
```

Shipping it anyway would be a small negative, not a wash:

- The enable bit is inert without a user-clip-coordinates command, so the
  change requires **one extra VDP1 command per list**. In Ymir's model that is
  a flat 16 cycles per frame — "Every command costs 16 cycles to fetch, even
  if skipped" (`ymir-agent/libs/ymir-core/src/ymir/hw/vdp/vdp.cpp:1021`).
- On real hardware it adds one comparison per plotted pixel, against 228k-class
  pixel counts, for no rejected pixel.

**What a narrower box would save cannot be estimated without a build, and is
not estimated here.** The oracle's positive control shows the *mechanism*
works — a 32×24 inset removes 80,813 of 228,674 writes on the synthetic
fixture — but that is a property of the fixture's primitive placement, not of
Bob. Producing the real number needs per-package screen boxes, which do not
exist yet, and a route capture.

## 6. The instrument cannot see this lever at all

This is the part that reaches past T2.19b and should change how the remaining
fill-rate levers are ranked.

**Ymir's VDP1 cost model is purely geometric and ignores clipping entirely.**
`VDP::VDP1CalcCommandTiming` (`vdp.cpp:1101`) reads the command's raw
sign-extended vertices straight out of VRAM and charges `max(|dx|,|dy|)` per
scanline span (`vdp.cpp:1185-1220` for the polyline/line cases; the quad cases
use the same raw extraction through `QuadStepper`). It never consults
`sysClip`, `userClip`, or `CMDPMOD`. A partially- or wholly-offscreen
primitive is charged its **full** modelled cost. The renderer's
`VDP1IsQuadSystemClipped` early-out
(`vdp_renderer_sw.cpp:1031-1050`) speeds up the *host*, not the emulated
clock, and is system-clip-only in any case.

Ymir's author says so directly, in a TODO listing what is not yet modelled
(`vdp.cpp:265-273`):

> `- high-speed shrink, end codes, user clipping (all of these reduce costs)`

**Therefore:** even a correct, narrower, genuinely hardware-effective user
clip would measure as **zero or slightly negative** on
`capture_sourceboot_throughput.py`. The same applies to HSS (gap row 5) and to
any other lever that suppresses writes rather than reducing what is emitted.
In this instrument, only **fewer commands, or commands with smaller vertex
extents**, move VDP1 time — which is exactly what T2.19c (terrain LOD) and the
Mario double-emit fix do.

## 7. Equivalence verdict

**PASS — bit-identical.** `tools/saturn/vdp1_user_clip_test.c` models VDP1's
pixel-acceptance predicate (system clip first, then the user-clip test with
the mode polarity from §2) and compares the shipped configuration against the
candidate over a framebuffer of 320×224 bytes.

The primitive set is deliberately weighted toward the cases that decide the
question — the straddlers, not the fully-inside ones:

| Class | Members |
| --- | --- |
| Fully inside | `fully-inside`, `fills-exactly-the-screen` |
| Fully outside | left, right, above, below |
| Straddling one edge | left, right, top, bottom |
| Straddling a corner | top-left, top-right, bottom-left, bottom-right |
| Overhanging everything | `overhangs-every-edge` |
| One-pixel boundary | column 0, column 319, row 0, row 223 |
| Non-axis-aligned | three diagonals exiting through a corner, the right edge, and the bottom edge |

Two further assertions guard the model itself:

- **No accepted pixel may land outside the framebuffer.** If system clipping
  did not cover all four edges, this trips and every conclusion above is void.
  It does not trip.
- **Direction check** independent of the raster: with `Window_In`, a point
  inside the box draws and one outside does not; with `Window_Out`, exactly
  the reverse.

## 8. Mutation results

All three required mutations FAIL, each through the assertion it was aimed at.
Gate `verify-vdp1-user-clip-mutation` compiles the fixture three times with a
different `-D` and requires a non-zero exit via
`tools/saturn/expect_failure.py`.

| Mutation | Define | Result |
| --- | --- | --- |
| Invert the clip mode (draw-inside → draw-outside) | `SM64_SATURN_TEST_MUTATE_USER_CLIP_MODE` | **FAIL as required** — `raster differs at (0, 0): shipped=18 candidate=0` |
| Shrink the clip rectangle by one pixel | `SM64_SATURN_TEST_MUTATE_USER_CLIP_RECT` | **FAIL as required** — `raster differs at (319, 0): shipped=18 candidate=0` |
| Clear the enable bit (bit 10) | `SM64_SATURN_TEST_MUTATE_USER_CLIP_ENABLE` | **FAIL as required** — `a narrowed clip box removed nothing (shipped=228674 narrowed=228674 written)` |

The third mutation is the reason the fixture carries a **positive control**
rather than only an equivalence check. Clearing the enable bit leaves
equivalence trivially true — that is the whole point of the finding — so a
suite built only on equivalence would let it through. The positive control
requires a deliberately narrowed box to remove pixels, so a disabled clip is
caught. Any future change that narrows the box inherits both halves.

## 9. Gates

| Gate | Result |
| --- | --- |
| `verify-vdp1-user-clip` | **PASS** (new) |
| `verify-vdp1-user-clip-mutation` | **PASS** — all three mutations caught (new) |
| `verify-terrain-clip` | **PASS** |
| `verify-vdp1-painter-chain` | **PASS** |
| `verify-vdp1-transfer-pipeline` | **FAIL, pre-existing and unrelated** — see below |

`verify-vdp1-transfer-pipeline`'s C fixture passes; its Python source contract
`test_a8_deferred_transfer_runtime_contract.py:61-66` fails on
`test_partial_destination_failure_poison_blocks_old_bank_presentation`. The
regex demands `static\s+bool\s+sourceboot_vdp1_destination_poisoned\s*;`, but
the declaration is split across two lines by its storage attribute
(`src/port/saturn/sourceboot/main.c:828-829`):

```c
static bool sourceboot_vdp1_destination_poisoned
    SOURCEBOOT_LWRAM_STATE;
```

The state itself is present and used at `main.c:1158`, `:1792`, `:1808` and
`:1822`. This is literal-text drift in the contract test, the same class as
the known `verify-sourceboot-presentation-boundary` failure, and `main.c` is
unmodified in this worktree — the failure is at HEAD, not caused by this work.
Both gates were added to `verify-all`.

## 10. Owner-visible risk, and what would make this worth revisiting

**Risk of what landed: none to the product.** No file under `src/` was touched.
The change is two new Make targets and one host fixture.

**Risk that this task's premise is repeated.** The reference-technique gap row
3 and its adopt-next item 4 both read as though enabling the bit is a saving in
itself. It is not; only step 2 is, and step 2's saving is invisible to the
instrument. `sprint2-reference-technique-gaps.md` §2.3 should be annotated
before someone spends a build on it.

This becomes worth shipping only when **all three** hold:

1. Per-package screen-space boxes exist and are proven supersets of each
   package's visible extent — otherwise a wrong box deletes geometry, which is
   the one failure mode the owner gate sees instantly.
2. The target is real hardware, or Ymir's VDP1 cost model has grown the
   clipping term its TODO promises. Until then the lever cannot be measured,
   only argued.
3. The per-package boxes are actually narrow. SlaveDriver's are portal
   bounding boxes in a corridor engine, where they are small. Bob is open
   terrain seen from a chase camera; its per-package boxes may cover most of
   the screen, in which case even step 2 saves little. That is a measurement
   nobody has taken.

Until then the fill-rate work that does move this instrument is the one that
reduces emitted extent: terrain LOD, the Mario double-emit, and command-count
reduction.

---

## 11. Provenance

| Source | Licence | Reuse mode | What was taken |
| --- | --- | --- | --- |
| `work/upstream/sonic-z-treme/Compiler/SGL_302j/INC/SL_DEF.H:190-192` | Sega SGL, in-tree reference | pattern-only | `Window_In`/`Window_Out` numeric encoding |
| `work/upstream/sonic-z-treme/Documentation/DOC/210A_US/SPRITE.TXT:392-394` | Sega SGL docs | documentation | WindowIn/WindowOut/NoWindow semantics |
| `work/upstream/slavedriver-engine/SPR.H:87-88`, `WALLS.C:2247-2253` | GPL | pattern-only | clip constant values; per-sector box arrangement, not adopted |
| `ymir-agent` (Ymir) `vdp_renderer_sw.cpp:965-1050`, `vdp.cpp:265-273,1013-1220` | GPL-3.0 | cross-check / behaviour-only | clip polarity confirmation; cost-model reading. No code copied. |
| `work/upstream/libretro-kronos` (Kronos/Yabause) `vdp1_prog_compute.h:42-53` | GPL-2.0 | cross-check / behaviour-only | clip mode encoding confirmation. No code copied. |
