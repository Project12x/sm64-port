# Sprint 2 Task T2.19a — the Mario "double-emit" is not a double-emit

- Date: 2026-08-16. Worktree `.worktrees/saturn-recovery`, branch
  `saturn/recovery`, base HEAD `eef321fe`.
- Task: delete the redundant one of the two VDP1 commands Mario emits per
  textured primitive (`saturn_demo_render.c:3902-3961`), now that T2.17 put
  VDP1 at 93.67% occupancy and made command count a live lever.
- **Verdict: NOT DELETED. Neither command is redundant. Both reach the
  screen.** The pair is an alpha-keyed decal composite: an opaque Gouraud
  polygon base with a transparency-keyed RGB1555 sprite drawn over it.
  Deleting either is a fidelity regression, not an optimisation, so under the
  task's own honesty clause this stops here.
- **No source change was made.** No build, no emulator. Two new host artefacts:
  an oracle that pins the composite (`tools/saturn/actor_double_emit_test.c`,
  nominal PASS, four mutations FAIL) and an unexecuted capture script for the
  measurement this task could not take (§5.3).
- The premise's arithmetic is also wrong independently of the fidelity
  finding: the deletion could never have "roughly halved" actor commands
  (§5).

---

## 1. Which command is visible — the answer is *both*, and here is the proof

The task asked for proof rather than assumption. Three facts compose; each is
established separately, and only the third is a judgement call about hardware
semantics.

### 1.1 Geometry — the two commands cover exactly the same quad

`demo_emit_mario_range` gives the polygon the projected vertices of
`sm64_mario_primitives[p][1..4]` and gives the sprite those of
`sm64_mario_textured_source_vertices[tile_start/4][0..2]`, with the fourth
corner repeating the third. Those are *different tables*, so equality is not
obvious and was checked over the whole table, not sampled:

| Check over all 50 textured primitives | Result |
| --- | ---: |
| `prims[p][1..4] == {tsv[r][0], tsv[r][1], tsv[r][2], tsv[r][2]}`, order-identical | **50 / 50** |
| same vertex set but permuted order | 0 |
| different vertex set | 0 |
| polygon quad already degenerate (`d == c`) | **50 / 50** |

This is forced by the generator, not coincidence.
`tools/saturn/extract_mario_actor.py:891` marks every textured triangle
`pairing_forbidden`, and `:944-951` refuses to emit a tile start unless the
primitive has exactly one source triangle. So a textured Mario primitive is
always one source triangle, and both commands necessarily read that triangle's
three positions from the same projected-vertex array.

**The sprite is therefore a decal on the polygon's own surface, not a
neighbouring surface.**

### 1.2 Order — the sprite is on top, and by construction

`demo_emit_mario` allocates the slots consecutively (`:4160-4167`):
`s_actor_slots[i] = command_slot++`, then, only for a textured primitive,
`s_actor_texture_slots[i] = command_slot++`. Both commands are tagged with the
**same** painter bin, `(uint16_t)(ref->sort_key >> 16)` (`:3933`, `:3958`).

`sm64_saturn_vdp1_backend_link_depth_bins`'s ordering contract
(`saturn_vdp1_backend.h:163-168`) is "far-to-near by descending bin, **stable
in original producer order within a bin**". Same bin plus ascending producer
order means the polygon is linked first and the sprite immediately after it.
VDP1 walks the chain in order with no depth buffer, so **the sprite draws over
the polygon**.

Asserted end-to-end through the real relink in
`test_sprite_is_drawn_over_a_polygon_that_keeps_transparency` — the fixture
does not reason about the contract, it runs it and reads the emitted links.

### 1.3 Transparency — the sprite does not cover the polygon, and cannot

This is the fact the task's premise missed.

`vdp1_cmdt_draw_mode_set` (`third_party/libyaul/.../vdp1/cmdt.h:269-279`) is
branchless and type-sensitive:

```c
const uint16_t comm = (cmdt->cmd_ctrl & 0x0004);
const uint16_t pmod_bits = (comm << 5) | (comm << 4);
cmdt->cmd_pmod = pmod_bits | draw_mode.raw;
```

`VDP1_CMDT_POLYGON == 4` and `VDP1_CMDT_DISTORTED_SPRITE == 2`
(`cmdt.h:28-38`). So:

| Command | `cmd_ctrl & 4` | forced `pmod_bits` | resulting SPD (bit 6) |
| --- | ---: | ---: | --- |
| Mario base **polygon** | `4` | `0xC0` (ECD\|SPD) | **set — opaque over its whole quad** |
| Mario detail **sprite** | `0` | `0x00` | **clear — transparency processing ON** |

Neither call site asks for SPD; the polygon gets it because the hardware
requires it for a non-textured command, and the sprite does not get it at all.
With SPD clear, every texture word equal to VDP1's direct-colour transparent
code `0x0000` is **skipped**, and the pixel keeps whatever the polygon put
there.

**And the texture data really is full of that code.**
`tools/saturn/extract_mario_textures.py:23-34` says so in as many words —
"VDP1's transparent direct-color code is exactly 0x0000, so discard the source
RGB payload when N64 A1 is clear" — and the generated manifest
(`build/saturn/marioturntable/generated/mario_eye_uv_tiles.json`) records the
result:

| Manifest field | Value |
| --- | ---: |
| `vdp1_transparent_word_count` | **7,216** |
| `vdp1_opaque_word_count` | 5,584 |
| `vdp1_nonzero_msb_clear_word_count` | 0 |
| `lowering` | *"atomic neutral-base Gouraud polygon plus alpha-keyed CC_REPLACE texture detail"* |

Recomputed independently from the generated header (50 tiles × 16 × 16 =
12,800 words), which agrees exactly:

| Census over the 50 Mario texture tiles | Result |
| --- | ---: |
| transparent words (`0x0000`) | **7,216 of 12,800 (56.38%)** |
| tiles with **zero** transparent texels | **0 of 50** |
| tiles that are **entirely** transparent (256/256) | **6 of 50** |
| next most transparent tile | 255 of 256 |
| least transparent tile | 4 of 256 |

### 1.4 The sampling-independent case

A partly transparent tile drawn very small on screen could in principle miss
every transparent texel, so §1.3's 56.38% alone is a strong argument rather
than a closed one. The six fully transparent tiles close it, because they do
not depend on sampling at all:

| tile rank | primitive | material RGB |
| ---: | ---: | --- |
| 21 | 171 | 31, 24, 15 |
| 28 | 178 | 31, 24, 15 |
| 30 | 180 | 31, 24, 15 |
| 34 | 184 | 31, 24, 15 |
| 39 | 189 | 31, 24, 15 |
| 43 | 193 | 31, 24, 15 |

For these six primitives the sprite writes **zero pixels at any size**. The
Gouraud polygon is 100% of what is on screen. All six carry the same skin-tone
material. **Deleting "the invisible polygon" would delete six visible surfaces
of Mario outright.**

### 1.5 It was designed this way, and a shipped gate already says so

Three independent records, none of which the task's framing accounted for:

- the generated manifest calls the pair **"atomic"** (§1.3);
- `docs/saturn/evidence/reports/2026-08-13-bob-convergence-handoff.md:38-44`
  records this three-part arrangement as **"the A9A policy"** and the observed
  VDP1 banks as "the target manifestation of the tested color contract";
- `tools/saturn/test_render_snapshot_source.py:145-146` **already asserts both
  commands' sort keys in source text**, including
  `"detail->cmd_link = (uint16_t)(ref->sort_key >> 16);"`. Deleting the detail
  command would have turned an existing gate red.

The commit that introduced it is `a4732c05 feat(saturn): add Mario texture
**overlay** lowering`.

### 1.6 What the reference sweep actually found, and what it did not

The task cited `sprint2-t2_0-reference-sweep.md` §"Mario double-emit". **That
section does not exist**; T2.0 has no occurrence of the term. The passage is
`sprint2-reference-technique-gaps.md:308-315`, and its own wording is
`"Both are drawn"` — it never claimed one was invisible. What it claimed was
that the *pattern* has no precedent in SlaveDriver or Z-Treme, which is true
and which this task does not dispute. Neither reference has an alpha-keyed
decal over a lit base for a character; SlaveDriver draws its player as a
single scaled sprite and Z-Treme as one command per model face. That is a
statement about a technique the port uses and they do not — not evidence that
a command is wasted.

**The same passage also undercounts the textured primitives by 5×**: it says
"only ~10 Mario primitives carry a texture start" and cites
`saturn_mario_actor_mesh.h:49320-49327`, which is the first 8 lines of a
54-line table. The true count is **50** (primitives 59–68 and 160–199), and
`SM64_MARIO_TEXTURED_SOURCE_TRIANGLE_COUNT` states it directly.

---

## 2. Verdict

**Both commands contribute to the final image.** The polygon supplies the
Gouraud-lit base colour of Mario's textured surfaces and is the *entire*
visible surface for 6 of the 50; the sprite supplies the texture where its
texels are opaque. Removing the sprite un-textures Mario's eyes, cap and
overalls detail. Removing the polygon punches 56% of those surfaces to
background and erases six of them completely.

Per the task's honesty clause this is a fidelity change, not an optimisation,
and it is the owner's call rather than this agent's. **Stopping here.**

---

## 3. The oracle, and its mutation results

`tools/saturn/actor_double_emit_test.c` — host C11, `-pedantic -Wall -Wextra
-Werror`. Because the deletion is not being made, the oracle pins the
composite instead of pinning a byte-equal deletion. It uses the **real**
painter relink (`saturn_vdp1_backend.h`) and the **real** generated mesh
tables; only the Yaul command-word encoding is a host double, and the one
piece of it that matters — `vdp1_cmdt_draw_mode_set`'s branchless
`pmod_bits` — is reproduced verbatim from the pinned header.

**One deliberate departure from the existing fixtures:** libyaul's
`vdp1_cmdt_draw_mode_t` bitfield union is *not* used. Its field order maps to
CMDPMOD bit positions only under big-endian MSB-first bitfield allocation
(SH-2); a host x86 compile of the same declaration places `end_code_disable`
and `trans_pixel_disable` at the opposite end of the word. Using it would have
tested the host's ABI instead of the target's arithmetic. Bit positions are
taken from the header's own comments (`cmdt.h:108-124`) and named explicitly.

| Test | What it pins |
| --- | --- |
| `test_setter_forces_opacity_on_polygons_only` | the mechanism in isolation: the *same* draw-mode request yields SPD set for a polygon and SPD clear for a distorted sprite |
| `test_polygon_and_sprite_cover_the_same_quad` | all 50 textured primitives, exact index equality including the repeated fourth corner |
| `test_sprite_is_drawn_over_a_polygon_that_keeps_transparency` | per primitive, through the real relink: both commands present, byte-identical vertex words, prefix→polygon→sprite chain, and the SPD asymmetry |

**Mutation matrix.** Each perturbs exactly one thing the task named.

| Mutation | Perturbs | Result |
| --- | --- | --- |
| `..._DROP_POLYGON` | removes the command the task assumed was invisible | **FAIL** (abort, exit 3) |
| `..._DROP_SPRITE` | removes the other one — a command that must survive | **FAIL** (abort, exit 3) |
| `..._SPRITE_BIN_SHIFT` | changes the survivor's sort key (sprite into bin+1) | **FAIL** (abort, exit 3) |
| `..._SPRITE_OPAQUE` | sets SPD on the sprite — the *only* change that would actually make the polygon redundant | **FAIL** (abort, exit 3) |
| nominal | — | **PASS** (`actor double-emit composite: RESULT OK`) |

The two `DROP_*` mutations initially failed to *compile* under `-Werror`
(unused variable, then unused function). A mutation that cannot be built
proves nothing, so both arms were kept referenced explicitly; the results
above are from mutations that build and then abort.

`..._SPRITE_OPAQUE` is the most useful row: it demonstrates that the whole
finding hinges on one bit, and that the fixture is sensitive to it rather than
to incidental structure.

### 3.1 This oracle is currently ungated — read this

`Makefile.saturn.mk` was **already modified by another agent** when this task
reached the point of adding a target, and the brief forbids resolving that.
**No `verify-actor-double-emit` target was added.** The fixture is committed
and passing but nothing runs it. The recipe to add at integration, matching
`verify-vdp1-painter-chain`'s shape:

```make
verify-actor-double-emit:
	@"$(SATURN_TOOLS_PYTHON)" -c "from pathlib import Path; Path(r'$(SATURN_REPO_ROOT)/build/saturn/host-tests').mkdir(parents=True, exist_ok=True)"
	$(HOST_CC_ENV) $(HOST_CC) -std=c11 -pedantic -Wall -Wextra -Werror \
	  -I"$(SATURN_REPO_ROOT)/tools/saturn/host_stubs" \
	  -I"$(SATURN_REPO_ROOT)/src/port/saturn/gfx" \
	  "$(SATURN_REPO_ROOT)/tools/saturn/actor_double_emit_test.c" \
	  -o "$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-double-emit-test$(HOST_EXEEXT)"
	"$(SATURN_REPO_ROOT)/build/saturn/host-tests/actor-double-emit-test$(HOST_EXEEXT)"
```

plus the four `-DSM64_SATURN_ACTOR_DOUBLE_EMIT_TEST_<name>=1` mutation arms
through `expect_failure.py`, and the target name in the `.PHONY` list and
`verify-all`.

### 3.2 The one link this fixture cannot pin

The texel census is ROM-derived and therefore not committed, so the fixture
asserts "the sprite leaves transparency enabled" but cannot assert "and there
are transparent texels". If the texture extractor were ever changed to emit
fully opaque tiles, the composite really would become redundant and this
fixture would still pass. The manifest field to watch is
`vdp1_transparent_word_count` in `mario_eye_uv_tiles.json`; a small check on
it, gated behind `compile-mario-textures` the way `verify-actor-meshlets` is
gated behind `compile-mario-actor-bank`, would close it. Not built here —
naming the gap rather than leaving it silent.

---

## 4. Owner-visible? — no change was made, so nothing is

There is nothing for the owner to look at from this task. The screen is
byte-identical to `id-c0352f297034f653` because no source file was touched.

The owner-gate item is the *counterfactual*: **had the deletion been made as
briefed, it would have been owner-visible and bad.** Mario's face and hands
(the six all-transparent tiles, all skin-tone material) would have lost their
polygon and rendered as background; his textured surfaces would have shown
background through 56% of their area. This is recorded so the proposal is not
revived from the T2.8 ranked list without reading §1.

---

## 5. The 17.7% figure, re-examined

T2.8 published `Commands per present 552.2 (actor 97.5 = 17.7%, textured
56.0)` and called 17.7% "the number a future command-count task should start
from". The brief warned that two T2.8-derived numbers have already meant
something other than what they were quoted as. **This is a third.**

### 5.1 17.7% is not the double-emit's share

`s_actor_command_count = s_actor_draw_count + s_actor_texture_count`
(`:4112`). The 97.5 actor commands are *both* arms together. The double-emit
is only the second term — one extra command per surviving **textured**
primitive — and the first term is one command per surviving primitive of any
kind. Deleting the "redundant" command would have saved exactly
`s_actor_texture_count`, never 17.7% and never half of it.

### 5.2 "Roughly halve them" was not reachable

Only **50 of 644** compiled Mario primitives (7.76%) carry a texture tile at
all. So `s_actor_texture_count ≤ 50` unconditionally, and the ceiling on the
saving is:

| Bound | Value |
| --- | ---: |
| textured primitives in the mesh | 50 of 644 |
| maximum detail sprites per frame | **50** |
| maximum saving against T2.8's 552.2 commands/present | **≤ 9.05%** |
| saving implied by "roughly halve 17.7%" | 8.85%, i.e. ~48.75 detail sprites |

The stated premise is only satisfiable if essentially **every** surviving
Mario primitive is textured in every frame — against a mesh where 92.24% of
primitives cannot be. The realistic figure is far lower, because
`s_actor_texture_count` counts only textured primitives that survive meshlet
admission and rejection in that frame.

### 5.3 The live number was not measured, and why

`tools/saturn/capture_actor_command_share.py` was written to read
`s_actor_command_count` and `s_actor_texture_count` — plain `uint16_t` statics,
recomputed every frame, needing no build or probe — together with
`sourceboot_vdp1_backend`'s published `list.count` as the denominator. Symbol
addresses were resolved against the preserved T2.17 ELF and are recorded below.

**It has never been executed against a live target.** The one attempt failed
at emulator start-up, and the brief's own constraint is "DO NOT launch an
emulator … host gates only", so it was not retried. **Every command-count
number in §5.1–5.2 is a static bound derived from committed tables, not a
measurement**, and the actual per-frame `s_actor_texture_count` on the BOB
route remains unknown. The script is committed unexecuted so the integration
build can take the number in one command:

```
python tools/saturn/capture_actor_command_share.py \
  --ymir <ymir-headless.exe> --ipl <Sega Saturn BIOS (USA).bin> \
  --game releases/2026-08-16_t2_17-product/id-c0352f297034f653/sm64-saturn-sourceboot-e2.cue \
  --elf  releases/2026-08-16_t2_17-product/id-c0352f297034f653/obj/sm64-saturn-sourceboot-e2.elf \
  --actor-address 0x060ea4ec --backend-address 0x0026e33c --profile-address 0x002cdac8 \
  --output docs/saturn/evidence/reports/sprint2-t2_19a-actor-command-share.json \
  --samples 150 --gap-vblanks 7
```

Addresses from
`releases/2026-08-16_t2_17-product/id-c0352f297034f653/obj/sm64-saturn-sourceboot-e2.sym`
(ELF SHA-256 `2933c5d5…2fecd`, matching T2.17): `_s_actor_command_count`
`0x060ea4ec`, `_s_actor_texture_count` `0x060ea4ee`, `_s_actor_draw_count`
`0x060e90ca`, `_sourceboot_vdp1_backend` `0x0026e33c`, `_sourceboot_fast3d`
`0x002cdac8`. The stride is deliberately 7 VBlanks against a ~9 VBlank frame
so the sweep is not phase-locked — T2.16's lesson — and the arena high-water
`peak` is reported alongside, being immune to sampling entirely.

---

## 6. Command-count delta per frame

**Zero.** No command was removed. The delta that was *available* is exactly
`s_actor_texture_count` per frame, bounded above by 50 and unmeasured (§5.3),
and taking it costs the fidelity described in §1–2.

---

## 7. Gates

Run from PowerShell; the Bash tool's environment makes GNU Make resolve its
temporary directory to `C:\WINDOWS\` and every host compile fails there. That
is worth knowing but is an environment quirk, not a repository defect.

| Gate | Result |
| --- | --- |
| `verify-actor-meshlets` | **RESULT OK** (exit 0) |
| `verify-terrain-command-stream` | **RESULT OK** (exit 0) |
| `verify-vdp1-painter-chain` | **RESULT OK** (exit 0) |
| `actor_double_emit_test` nominal + 4 mutations | **RESULT OK** (§3) |
| `verify-render-snapshot-bank` | **FAIL — pre-existing, not a regression** |
| `verify-sourceboot-presentation-boundary` | **FAIL — pre-existing** (known, T2.17 §7) |

`verify-render-snapshot-bank` is a **new instance of the same brittle
literal-text class**, and it was not previously reported.
`tools/saturn/test_render_snapshot_source.py:148` asserts the literal
`"records[local].sort_key >> 16"` in `saturn_demo_render.c`. That text is now
`(uint16_t)(record->sort_key >> 16)` (`:4295`) — the generic-actor emitter was
rewritten to a pointer. `git status` shows `saturn_demo_render.c` unmodified
throughout this task, so the gate fails identically at `eef321fe`. **This is
the sixth member of the brittle-host-gate class STATE.md catalogues, and the
second literal-text drift.** Not chased, per the brief.

Two consequences worth stating: this gate has been verifying nothing since the
generic-actor rewrite, and — because the *same* test function asserts the
Mario detail command's sort key — the strongest existing protection against
exactly the change T2.19a proposed has been red and unnoticed.

---

## 8. Honesty — what is wrong with this task

- **The task's premise was false and the task therefore produced no
  optimisation.** The deliverable is a negative result plus a fixture.
- **No command-count measurement was taken** (§5.3). Every number in §5 is a
  static bound from committed tables. The capture script is committed
  unexecuted, which is unverified code, and it is labelled as such here and in
  its commit message.
- **The texel census is ROM-derived and not committed**, so §1.3's 56.38% and
  §1.4's six tiles are reproducible only where the generated header exists.
  They were recomputed independently of the manifest and agree with it, but a
  clean tree cannot re-check them without the ROM.
- **The oracle is ungated** (§3.1) because `Makefile.saturn.mk` was already
  dirty. A committed test with no target is one integration step away from
  being dead weight.
- **The fixture's Yaul layer is a host double.** The load-bearing arithmetic is
  verbatim from the pinned header and the bitfield trap is avoided
  deliberately (§3), but it is a double, and a change to
  `vdp1_cmdt_draw_mode_set` upstream would not be caught by it.
- **No live observation.** No emulator, no CUE, no screenshot. The claim that
  Mario's face would disappear is derived from the command encoding and the
  texture data, not seen.
- **One pre-existing gate failure was found rather than fixed** (§7).

---

## 9. References

Per the standing owner instruction and the reference-code-first rule.

| Repo | Pinned SHA | License | Files inspected | Reuse mode |
| --- | --- | --- | --- | --- |
| `third_party/libyaul` (vendored) | as vendored (`6012f79f`) | MIT | `scu/bus/b/vdp/vdp1/cmdt.h:28-38` (command type codes), `:108-124` (CMDPMOD field layout), `:269-279` (`vdp1_cmdt_draw_mode_set`, the branchless ECD\|SPD force), `:425-453` (type setters) | dependency, read for semantics; the setter body reproduced verbatim in the fixture with attribution |
| `work/upstream/slavedriver-engine` | not re-cloned this task | GPL-3.0-or-later | none | — |
| `work/upstream/sonic-z-treme` | not re-cloned this task | GPL-3.0-or-later | none | — |

**Both reference engines were re-read only through
`sprint2-reference-technique-gaps.md` §2.1, not re-cloned.** `work/` is absent
from this worktree. The gap study's finding — that neither engine draws a
character surface twice — is accepted as correct and is **not** what this task
overturns; §1.6 explains the difference between "no precedent" and
"redundant".

---

## 10. What remains

- **Add `verify-actor-double-emit` to `Makefile.saturn.mk`** at integration
  (§3.1). Until then the fixture protects nothing.
- **Repair `verify-render-snapshot-bank`** (§7). It is red at HEAD, it has been
  verifying nothing, and it is the gate that would otherwise have caught this
  task's proposed change.
- **Take the command-share measurement** on the integration build (§5.3), one
  command, no rebuild.
- **The six fully transparent tiles are a real, separate, much smaller
  saving.** Their *sprite* commands write zero pixels at any size and are pure
  VDP1 fill cost — the only genuinely redundant commands in this path. That is
  at most 6 commands per frame and it is conditioned on ROM-derived data, so it
  needs a data-driven guard (skip emitting a detail sprite whose tile is fully
  transparent, decided at extraction time and baked into
  `sm64_mario_texture_tile_start`) rather than a source edit. Recorded, not
  attempted.
- **Do not revive "delete the Mario double-emit" from T2.8 §9 item 6 without
  reading §1 of this report.** The T2.8 entry and the gap study entry should
  both be annotated.
