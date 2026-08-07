# Changelog

## [Unreleased]

### Added

- Added a build-time mutation gate and a static structural gate for the VDP2
  gameplay HUD (Task 7 of `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`),
  plus closed three test-coverage gaps Task 5's review had flagged but
  deferred.

  **Why a mutation gate, not just a passing test:** Task 6's
  `test_publish_only_rewrites_changed_cells` passing proves the dirty-cell
  diff *can* produce zero writes on an unchanged snapshot; it does not by
  itself prove that behavior is load-bearing rather than accidental (e.g. a
  refactor that quietly always-writes but happens not to break any other
  assertion would slip through unnoticed). `saturn_hud_publish.c`'s pass-2
  writer-selection `if` is now wrapped in
  `#ifdef SM64_SATURN_HUD_TEST_MUTATE_DIRTY_GATE`, which forces every cell
  to be treated as changed regardless of `prior->glyph`. The plan's given
  snippet leaves `prior` computed-but-unread on that branch, which fails
  `-Wunused-variable` under this project's `-Werror`; added `(void)prior;`
  to keep the mutation build compiling without weakening it (the real gate
  in the `#else` arm is untouched). New Make target
  `verify-saturn-hud-layout-mutation` builds that mutant and requires it
  exit nonzero via the existing `tools/saturn/expect_failure.py` convention
  (14+ other call sites already use this pattern in this Makefile) --
  confirmed the mutant fails with "unchanged snapshot triggered 4
  rewrites", caught by the fixture.

  **Why a grep gate for VDP1:** this HUD is VDP2-only by design (character
  cells on NBG0), and Task 8 is about to introduce a VDP1 frame-bank type
  that legitimately carries the HUD snapshot through a `sm64_saturn_vdp1_*`
  name. A static, structural proof that the HUD's own rendering logic
  (`saturn_hud_layout.c`, `saturn_hud_publish.c`, `saturn_hud_atlas.c`)
  never references `vdp1_cmdt`/`VDP1_CMDT`/`sm64_saturn_vdp1_` closes the
  door on that boundary eroding silently in a future edit, without needing
  a compiler-level dependency check (these files already don't include any
  VDP1 header). `saturn_hud.h`/`saturn_render_snapshot.h` are deliberately
  excluded from the grep for exactly that Task 8 reason. New Make target
  `verify-saturn-hud-no-vdp1` passes against the real files; also verified
  it actually catches a violation, not just that it passes today, by
  temporarily appending a fake `sm64_saturn_vdp1_frame_bank_fake_reference`
  comment to `saturn_hud_layout.c`, confirming the gate correctly failed,
  then reverting via `git checkout --` before committing.

  Both new targets added to `.PHONY` alongside every other `verify-*`
  target in this file.

  **Coverage-gap closure (beyond the plan's literal text):** Task 5's
  review had left three `TODO(Task 7)` comments in `saturn_hud_layout.c`,
  each recording a real mutation-testing finding that the original 4 tests
  in `tools/saturn/saturn_hud_layout_test.c` never exercised: the
  `cannon_active` gate (no test ever set it), both `camera_status` switch
  statements (MARIO/LAKITU/FIXED and C_DOWN/C_UP -- no test ever set
  `camera_status` to a case-matching value, so only `default:` ever ran),
  and the `stars < 100` branch (only the `>=100` sibling had coverage, via
  the existing capacity test's `stars=9999`). Added 5 new tests closing all
  three: `test_layout_places_cannon_reticle_when_active` /
  `test_layout_omits_cannon_reticle_when_inactive` (both directions of the
  boolean gate -- a single inversion mutation breaks both, but each also
  independently catches an always-true or always-false variant alone),
  `test_layout_camera_mode_switch_selects_correct_glyph` /
  `test_layout_camera_cbutton_switch_selects_correct_glyph` (every case
  label of both switches, table-driven, asserting the *sibling* glyphs are
  absent so a swapped-glyph mutation is caught rather than just "some"
  camera-mode glyph appearing), and
  `test_layout_star_count_below_100_uses_two_digit_field` (stars=42; the
  `<100` and `>=100` branches coincidentally produce the same total cell
  count -- 4 either way -- so the assertion targets the two signals only
  `<100` can produce: the multiply glyph's presence and an exact 2-digit
  field instead of 3). All 5 wired into `main()`.

  Verified each new test against a real mutation of its own target code
  path, not just that it passes today (project standing policy): inverted
  the `cannon_active` gate (both new cannon tests failed, plus incidentally
  broke an existing Task 6 publish test since `cannon_active` defaults to 0
  almost everywhere); swapped `CAM_MARIO_HEAD`/`CAM_LAKITU_HEAD` in the mode
  switch (`test_layout_camera_mode_switch_selects_correct_glyph` failed,
  naming `CAM_STATUS_MARIO`); swapped `CAM_ARROW_DOWN`/`CAM_ARROW_UP` in the
  C-button switch (`test_layout_camera_cbutton_switch_selects_correct_glyph`
  failed, naming `CAM_STATUS_C_DOWN`); forced the stars branch to always
  take the `>=100` path (`test_layout_star_count_below_100_uses_two_digit_field`
  failed: "did not render the multiply glyph"). Each mutation was applied
  with `Edit`, built, run, observed red, then reverted with
  `git checkout --` and confirmed byte-identical to `HEAD` before moving to
  the next. Replaced the three now-resolved `TODO(Task 7)` comments with
  notes naming the covering test, so they stop claiming the gap is still
  open.

  Verified by direct execution from a from-scratch `build/saturn/host-tests`
  directory: normal build (11 tests, exit 0), mutation build (exit 1,
  caught by `expect_failure.py`, exit 0), no-VDP1 grep gate (exit 0, both
  via real `make` and standalone). Hit the same MSYS2 `make`
  recipe-shell-strips-`TMP`/`TEMP` quirk Task 6 already documented (`Cannot
  create temporary file in C:\WINDOWS\: Permission denied`); worked around
  it identically -- reproduced the exact `make -n` command lines directly
  in a shell with a correctly populated environment. `verify-saturn-hud-no-vdp1`
  itself has no such issue (grep needs no temp files) and was additionally
  confirmed to run clean through real `make` directly.

- Added `src/port/saturn/gfx/saturn_hud_publish.{h,c}` (Task 6 of the VDP2
  gameplay HUD plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`):
  `sm64_saturn_hud_publish()` diffs the layout Task 5's
  `sm64_saturn_hud_layout_build()` computes for the current frame against
  the layout actually published last time, and calls Task 4's
  `sm64_saturn_hud_atlas_write_cell()` only for cells whose `(col,row,glyph)`
  changed since then -- including writing `SM64_SATURN_HUD_GLYPH_BLANK` for
  any cell that was occupied last publish and is not occupied this publish.
  Pure host-testable logic with no VRAM access of its own; a test double
  stands in for the real atlas writer in the host test, exactly as the plan
  specified.

  Verified the real, current Task 4/5 public interfaces
  (`saturn_hud_atlas.h`, `saturn_hud_layout.h`) before writing any code
  against them, per this plan's established practice -- both matched what
  the plan's Step 3 snippet assumed exactly (field names/types on
  `sm64_saturn_hud_cell_t`, `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` = 40, and
  `sm64_saturn_hud_layout_build()`'s signature), so the plan's given
  implementation needed no interface-mismatch fixes this time.

  Added `test_publish_writes_blank_for_vacated_cells` to
  `tools/saturn/saturn_hud_layout_test.c`, beyond the plan's own given test.
  The plan's `test_publish_only_rewrites_changed_cells` never clears
  `HUD_FLAG_LIVES`, so it only ever changes a digit glyph within a group
  that stays on screen -- it never exercises `sm64_saturn_hud_publish()`'s
  first diff pass (the one that blanks cells no longer occupied) at all;
  deleting that whole pass would still pass the plan's test. The new test
  drives the lives group from shown to fully hidden and checks that every
  previously-occupied cell is rewritten exactly once, that every one of
  those rewrites specifically carries `SM64_SATURN_HUD_GLYPH_BLANK` (not
  just "some glyph"), and that republishing the same now-empty snapshot a
  second time costs zero further writes -- proving `state->last_count`
  actually shrinks to 0 rather than leaving stale occupied bookkeeping
  behind.

  Traced the specific correctness questions this task's brief called out
  before trusting the algorithm: a cell whose glyph changes while a
  different cell's glyph also changes in the same publish is handled
  independently per cell (no shared state between diff decisions); the
  first-ever publish (`state->primed == 0`) writes every cell because
  `last_count` is already 0 from `sm64_saturn_hud_publish_init()`, so
  `find_cell()` would return `NULL` regardless of the explicit `primed`
  guard (the guard is belt-and-suspenders, not load-bearing); and
  `find_cell()`'s O(count) per-call / O(count^2) total linear scan is a
  performance-only characteristic, not a correctness gap, given
  `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` is a fixed 40 and Task 5's layout
  builder is documented never to emit duplicate `(col,row)` keys for
  simultaneously-visible glyphs. Also confirmed by construction that the
  "clear vacated cells" pass and the "write occupied cells" pass can never
  both target the same `(col,row)` in one call (they partition on
  membership in the new layout's cell list), so a glyph that changes while
  staying occupied always gets exactly one write, never a spurious
  blank-then-rewrite pair.

  Verified by direct execution: compiled and ran the full host test
  (`gcc -std=c11 -Wall -Wextra -Werror`, zero diagnostics, exit 0, all 6
  tests including the 2 new ones), then ran a real mutation-testing pass
  (project standing policy) rather than trusting the given tests --
  disabling the vacate pass, forcing pass 2 to always/never write, weakening
  `find_cell`'s `&&` to `||`, and writing the wrong glyph on vacate were all
  5/5 caught (test suite goes red for each); restored the clean
  implementation afterward and confirmed a final clean build.

  Hit the same `Makefile.saturn.mk`/Cygwin-`make` `OS` quirk Task 5
  documented, worked around identically (`OS=Windows_NT` on the command
  line). Also hit a related but distinct quirk this time: this sandbox's
  MSYS2 `make` (`/c/msys64/usr/bin/make`, Cygwin-built) strips `TMP`/`TEMP`/
  `TMPDIR` entirely from its recipe shell's environment (confirmed with a
  minimal diagnostic Makefile target), which breaks the native
  (non-MSYS-linked) MinGW `gcc.exe`'s ability to create its intermediate
  temp files ("Cannot create temporary file in C:\WINDOWS\: Permission
  denied") -- unrelated to the already-documented `OS`-variable quirk, and
  reproducing consistently even after this task's files existed. Worked
  around the same way the plan's own guidance anticipated for the Python-
  wrapper quirk: reproduced the exact compiler and run commands `make -n`
  would have issued and ran them directly in a shell with a correctly
  populated environment, which is what actually proves RED and then GREEN.

  Added `saturn_hud_publish.c` to the `verify-saturn-hud-layout` Makefile
  target's compile line, per the plan's Step 2.

  **Provenance correction:** the plan's Step 4 text asserted, as the
  rationale for an "honest negative finding," that "Z-Treme's own HUD
  counters redraw unconditionally every frame." Both pinned references
  (`Lobotomy-Software/SlaveDriver-Engine` and `Maxime-XL2/SONIC-Z-TREME`)
  are vendored locally, so this was checked directly rather than copied
  into permanent provenance docs unverified. SlaveDriver's own text/HUD
  module, `PRINT.C`/`PRINT.H`, has no dirty-tracking of any kind and
  renders text as VDP1 sprites (`sega_spr.h`, `EZ_setLookupTbl`), not VDP2
  character-pattern cells at all -- not even the same rendering mechanism
  this task's atlas uses. Z-Treme's only candidate HUD-counter call site,
  `slPrint("RINGS : ", slLocate(0,4))` in `SRC/game.c:30`, is commented out
  in the pinned snapshot, and `slPrint`/`slLocate` are proprietary SGL
  primitives with no available source in this repository. The more precise,
  defensible finding recorded in `docs/saturn/UPSTREAM_CODE_LEDGER.md`
  ("Task 23A Task 6") and `docs/saturn/PROVENANCE.md` instead: neither
  pinned reference offers inspectable per-cell VDP2 dirty-diffing logic to
  adopt or contrast against, because neither has a live, readable call site
  to inspect at all -- not that either one demonstrably redraws
  unconditionally. Added alongside, not overwriting, Task 4's existing
  `ztFont2NBG3` citation in both files.

  **Provenance correction, corrected (2026-08-07):** a spec review caught
  that the "Provenance correction" above is itself wrong. It cited an
  unrelated, commented-out `slPrint("RINGS : "...)` label inside
  `ztReset()` (`SRC/game.c:30`, a one-time player-death/respawn path) and
  concluded no live per-frame HUD-counter call site existed in either
  reference -- but never checked `draw_stats()` (`ZT_RENDERING.c:146-152`),
  which is exactly what the plan's own text names, at the plan's line 20.
  Re-read the pinned Z-Treme tree directly and traced the real call chain:
  `draw_stats()` issues unconditional `slPrintHex`/`slLocate` calls for
  `LIVES`, `OWNED` ("Rings or weapons", `ZTE_DEF.H:282`), and a
  `TIMER`-derived clock, gated by nothing (its own early-return at line 148
  is commented out, and the only active gate, line 165, guards later
  debug-only readouts, not these three); it is called unconditionally for
  the local player from `ztRender()` (`ZT_RENDERING.c:792-793`, gated only
  on `currentPlayer->PLAYER_ID == 0`, true for `PLAYER_1`, `main.c:119`),
  which `main_loop()` calls unconditionally every invocation
  (`SRC/game.c:772`), inside `ztGameLoop()`'s `while(1)` loop paced by
  `slSynch()` (`ZT_GAME.c:70-107`). The plan's original claim was correct
  all along: Z-Treme's own HUD counters do redraw unconditionally every
  frame. Fixed `docs/saturn/UPSTREAM_CODE_LEDGER.md` and
  `docs/saturn/PROVENANCE.md` to cite `draw_stats()` instead of the
  unrelated dead line, restoring the plan's original framing, while leaving
  the SlaveDriver `PRINT.C` finding (VDP1-sprite-based text, not VDP2
  cells) unchanged, since that part was independently confirmed accurate
  and not in question. The underlying negative finding these citations
  support -- neither reference implements *per-cell* VDP2 dirty diffing, so
  `saturn_hud_publish.c` is original engineering -- is unaffected; only the
  supporting evidence for Z-Treme's half of it was wrong and is now
  corrected. No code or test changes; this is a documentation-only fix.

- Added `src/port/saturn/gfx/saturn_hud_layout.{h,c}`: a pure, host-testable
  function (`sm64_saturn_hud_layout_build()`) that decides which glyph goes in
  which VDP2 tile cell for a given `sm64_saturn_hud_snapshot_t` (Task 5 of the
  VDP2 gameplay HUD plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`).
  No VRAM access, no Yaul dependency -- takes a snapshot, writes up to
  `SM64_SATURN_HUD_LAYOUT_MAX_CELLS` (40) `(col,row,glyph)` cells, and returns
  the count actually used.

  Resolved an intentional test/gate contradiction flagged in the plan itself:
  the plan's power-meter test sets `snapshot.wedges = 3` but leaves
  `power_meter_animation` at its `memset`-zeroed default while still expecting
  the meter glyph to appear, apparently conflicting with a `!= 0` gate. Read
  the real `src/game/hud.h` (`enum PowerMeterAnimation`: `POWER_METER_HIDDEN`
  is the first enumerator, value 0) and the real `hud.c`
  (`render_hud_power_meter()`, `hud.c:229-257`) rather than assuming: the
  source's own gate is `if (sPowerMeterHUD.animation == POWER_METER_HIDDEN)
  return;`, i.e. it renders for all four non-hidden phases (EMPHASIZED,
  DEEMPHASIZING, HIDING, VISIBLE), not just the resting VISIBLE state. So
  `power_meter_animation != 0` is exactly correct and was kept unchanged; the
  bug was in the test, which was missing
  `snapshot.power_meter_animation = 1;` (`POWER_METER_EMPHASIZED`, matching
  the numeric-literal convention `saturn_hud_snapshot_test.c` already
  established for this field, since neither host test can include
  `src/game/hud.h` without pulling in the full N64 `PR/ultratypes.h` chain).
  Fixed the test, not the gate, and documented why inline.

  Also found and fixed a placement bug the plan's own Step 3 snippet did not
  flag: most of its literal `(col,row)` values are outside the atlas's real
  visible grid. `sm64_saturn_hud_atlas_write_cell()` (Task 4) silently
  no-ops any write with `col >= 20` or `row >= 14`
  (`saturn_hud_atlas.c`'s `HUD_TILE_COLS`/`HUD_TILE_ROWS`, confirmed by
  reading the real, current file rather than the plan's summary of it) --
  and the plan's snippet placed lives/coins at `row=26`, stars at
  `col=24-27`, the timer at `col=20-25`, and the camera glyphs at
  `col=26-27`, all past those bounds. None of Task 5's or Task 6's own tests
  would have caught this: they only check glyph presence and total count,
  never `col`/`row`, and Task 6's dirty-cell publisher (already drafted in
  the plan) forwards this layout's `(col,row)` straight into
  `sm64_saturn_hud_atlas_write_cell()` with no remapping. Left uncorrected,
  this would have made lives, coins, stars, the timer, and the camera status
  indicator permanently invisible on real hardware and in emulation --
  passing every test in this 10-task plan while silently defeating its
  stated goal. Re-derived every placement from `hud.c`'s real pixel
  coordinates (`SCREEN_WIDTH`/`SCREEN_HEIGHT` = 320x240,
  `include/config.h:38-39`) divided down to this port's 16px/20x14 grid,
  clustering everything into the bottom four tile rows -- and checked that
  no two glyphs able to be visible in the same frame ever target the same
  cell, including the realistic simultaneous case (`HUD_DISPLAY_DEFAULT`'s
  LIVES | COIN_COUNT | CAMERA_AND_POWER bits plus STAR_COUNT/TIMER,
  `level_update.h:106-116`), not just the tests' synthetic worst case.
  Documented the full derivation and the final grid assignment in a comment
  in `saturn_hud_layout.c`.

  **Code-review correction (2026-08-07):** the first version of that
  derivation comment (and this entry) claimed lives/coins/stars/camera "all
  sit at y=205-209" in the source, as if the bottom-row clustering were a
  pixel-derived transcription for all four groups. A code-quality review
  traced the actual rendering paths and found this conflates two different
  Y-axis conventions that coexist in `hud.c`: lives/coins/stars
  (`HUD_TOP_Y=209`) and the timer (`y=185`) all go through
  `print_text()`/`print_text_fmt_int()` -> `render_text_labels()` ->
  `render_textrect()`, which applies an unconditional Y flip,
  `s32 rectBaseY = 224 - y;` (`src/game/print.c:391`, confirmed by reading
  the real file) -- putting them at actual screen y≈15/y≈39, near the
  **top**, not the bottom. Only the camera status icon
  (`render_hud_camera_status()`, `y=205`) uses the unflipped
  `render_hud_tex_lut()` path directly, so it genuinely is near the bottom.
  So in the real game, lives/coins/stars/timer cluster near the top and
  only the camera icon is near the bottom -- the opposite of what the
  original comment claimed for 4 of the 5 groups. This was a documentation
  defect only: the shipped layout was already safe (in-bounds) and
  internally consistent (collision-free) either way, confirmed by the spec
  reviewer's exhaustive brute-force check of all 1,638,400 possible input
  combinations. Fixed by correcting the derivation comment in
  `saturn_hud_layout.c` to state plainly that only the camera icon's bottom
  placement is a literal match to the source's screen semantics, and that
  the rest of the bottom-clustering is a deliberate choice driven by the
  tile grid's coarseness, not a pixel-derived one -- comment-only, no logic
  changes (mechanically confirmed: built the pre-fix and post-fix
  `saturn_hud_layout.c` side by side against five representative snapshots,
  including the pathological worst case, and diffed the emitted
  `(col,row,glyph)` cells -- byte-identical). Also named the per-group
  `HUD_ROW_*`/`HUD_COL_*` constants that were previously bare numeric
  literals scattered across roughly 15 call sites, so a future edit to one
  group's position is grep-auditable against every other group instead of
  relying solely on this prose comment, and added `TODO(Task 7):` markers
  at the three branches the reviewer's mutation testing found completely
  unexercised by the current 4 tests (the cannon reticle path, the camera
  mode/C-button switch cases, and the star-count `<100` branch) so Task 7
  -- the plan's dedicated mutation-test task -- picks them up.

  Verified by direct execution, not just reading: compiled and ran the host
  test (`gcc -std=c11 -Wall -Wextra -Werror`, zero diagnostics, exit 0), then
  ran a real mutation-testing pass (project standing policy) rather than
  trusting the given tests -- flipping the power-meter gate and inverting
  the lives-flag check were both caught (test suite goes red); an
  off-by-one in `push_cell`'s capacity guard (`>` for `>=`) survives
  uncaught, because the tests' "9999 lives/coins/stars" pathological input
  only reaches ~20-24 cells against the 40-cell buffer, never the actual
  boundary -- a real, pre-existing test-coverage gap (inherited from the
  plan's Step 1 test, not introduced here) worth knowing about, though the
  shipped code uses the correct `>=` guard. Also confirmed `push_clamped_int`
  truncates to the low-order N decimal digits rather than saturating at the
  field's max value (e.g. a 2-digit field showing 105 would render "05", not
  "99") -- the pathological test's all-9s values (9999) happen to read
  identically either way, which masks the distinction; documented inline as
  a known, pre-existing display-fidelity limitation of the fixed-width
  tile HUD, not a capacity or memory-safety issue.

  Hit two known host-tooling environment quirks getting a real run: this
  sandbox's Cygwin `make` does not see the `OS` environment variable at all
  (confirmed with a minimal repro Makefile), so `Makefile.saturn.mk`'s
  `ifeq ($(OS),Windows_NT)` branch silently picked the wrong
  `SATURN_TOOLS_PYTHON`/`HOST_EXEEXT` values; fixed by passing
  `OS=Windows_NT` on the `make` command line rather than editing the
  Makefile. Separately, the venv Python's `subprocess.run()` test-runner
  wrapper can't launch an MSYS-style `/d/...` path via native Windows
  `CreateProcess` (`WinError 2`) -- pre-existing and orthogonal to this
  task, would affect any `verify-*` target's Python-wrapped run step
  equally. Both are environment issues, not defects in the Makefile target
  or the code; worked around by running the make-built binary directly,
  which is what actually proves the test passes.

  Added `verify-saturn-hud-layout` (Makefile target + `.PHONY` entry),
  following the exact convention already established by
  `verify-saturn-hud-snapshot`.

- Closed two code-review gaps in `saturn_hud_atlas.c` (Task 4, above) found by
  a stricter-warning-level review pass (`-Wconversion -Wsign-conversion
  -Wshadow -Wcast-align -Wcast-qual -Wdouble-promotion -Wundef
  -Wstrict-prototypes`, still zero diagnostics -- these are both missing
  guards, not compiler-catchable bugs):

  First, `sm64_saturn_hud_atlas_write_cell()` bounds-checked `col`/`row` but
  not `glyph`: a future caller computing an out-of-range
  `sm64_saturn_hud_glyph_t` (e.g. Task 5's not-yet-written unclamped digit
  arithmetic) would have `cpd_addr` land past the last real character
  pattern and write a garbage glyph into a valid, visible PND cell --
  discoverable only at Task 9/10's visual check, far from where the bug
  would actually be introduced. Added `|| glyph >= SM64_SATURN_HUD_GLYPH_COUNT`
  to the existing guard clause, matching the established index-plus-enum-
  sentinel shape already used by
  `saturn_demo_render.c:471-472`'s `demo_terrain_template_valid_set()`
  (confirmed by reading it: same two-condition-plus-`_COUNT` pattern).

  Second, nothing enforced that the atlas's character-pattern data
  (`SM64_SATURN_HUD_GLYPH_COUNT * HUD_CHAR_BYTES`, currently 32 * 512 =
  16384 bytes) stays under the 32768-byte gap to `HUD_PND_BASE`; a future
  glyph addition could silently grow past that boundary and corrupt the
  pattern-name table's own VRAM region, a cross-structure corruption that
  would be very hard to trace back from a Ymir visual glitch. Added a
  `_Static_assert` at file scope, matching the invariant-enforcement shape
  already used by `saturn_pcm_protocol.h:164`
  (`SM64_SATURN_PCM_BANK_OFFSET < SM64_SATURN_PCM_SOUND_RAM_BYTES`).
  Verified the assert actually fires, not just that it compiles: built a
  scratch copy with `HUD_PND_BASE`'s offset shrunk from `0x08000` to
  `0x2000` (8192 bytes, below the real 16384-byte requirement) and
  confirmed `sh-elf-gcc -fsyntax-only` fails with exactly the written
  message ("HUD character-pattern data overflows into the PND region"),
  then discarded the scratch copy -- the real file was never edited to an
  invalid state.

  Also folded the reviewer's optional minor suggestion: added
  `HUD_CHAR_DIM` (16) so the eight power-meter crop calls reference a named
  constant instead of a bare `16U, 16U` pair. Left the second optional
  suggestion (extracting the quadrant-reorder arithmetic into a pure,
  host-testable function with a unit test) as a note for a later task
  rather than doing it here: that would mean building a new host-testable
  module and Makefile `verify-*` target ahead of Task 5, which already
  plans a pure, host-testable `saturn_hud_layout.c` of its own -- doing it
  now risks scaffolding that Task 5 would then have to reconcile with
  rather than build.

- Added `src/port/saturn/gfx/saturn_hud_atlas.{h,c}`: a one-time VDP2 NBG0
  character/cell-mode glyph atlas (Task 4 of the VDP2 gameplay HUD plan,
  `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`). `sm64_saturn_hud_atlas_init()`
  uploads every HUD glyph (digits, camera-status icons, power-meter wedges,
  a procedural cannon reticle, a transparent blank) into VDP2 character-
  pattern VRAM and configures a dedicated NBG0 plane for them, separate from
  the existing NBG1 sky bitmap and NBG3 dbgio text; `sm64_saturn_hud_atlas_write_cell()`
  writes one pattern-name-data cell. This is the first character/cell-mode
  VDP2 usage in this port -- the only prior usage (the NBG1 sky bitmap,
  `introface`'s title screen, dbgio's own NBG3 console) is bitmap mode or an
  already-existing library device, a structurally different Yaul API path,
  so there was no in-repo precedent to reuse for the pixel-upload shape.

  Re-verifying the plan's own code snippet against the real vendored Yaul
  source (rather than trusting it, per this project's standing rule to
  check permissive reference code before writing to it) found and fixed
  three mismatches between the plan's assumptions and reality:

  1. **VDP2 `CHAR_SIZE_2X2` character-pattern data is four separately-
     addressed, individually-contiguous 8x8 pixel cells** (top-left/top-
     right/bottom-left/bottom-right, 64 words each), not one flat 16-wide
     raster. Confirmed two independent ways: `vdp2_scrn_pnd_set()`'s aux-
     mode character-number bit-packing
     (`third_party/libyaul/libyaul/scu/bus/b/vdp/vdp2_scrn_cell.c:320-356`)
     supplements the pattern-name table's character number with implicit
     low bits that select one of the four sub-cells; and, independently,
     Yaul's own `satconv` texture converter's `TILE_16x16` case reads
     exactly those four 8x8 quadrants, in that order, into one contiguous
     buffer (`third_party/libyaul/tools/satconv/tile.c:177-208`). The
     plan's snippet copied source pixels into a character-pattern slot
     with a flat `dest[index] = pixels[index]` loop, which would have
     interleaved rows from different quadrants and produced a scrambled
     glyph on real hardware (and in cycle-accurate emulation) for every
     glyph wider than 8px -- i.e. everything except the two 8x8 camera
     arrows, which happened to work by coincidence since their source
     width equals one cell's width. Fixed by replacing the single-pixel-
     count `hud_atlas_upload_one()` helper the plan specified with
     `hud_atlas_upload_pattern()`, which performs the same quadrant
     reordering as `satconv`'s `TILE_16x16` case, generalized with a
     source-stride parameter so one helper also serves the crop case
     below and the 8x8 arrows (whose unwritten quadrants are now
     explicitly blanked to solid transparent, rather than left at
     whatever the VRAM bank previously held -- Saturn VRAM is not
     guaranteed zeroed at power-on).
  2. **The real generated `sm64_saturn_hud_power_meter_1..8` arrays are
     `[1024]` (32x32 source pixels), not `[256]`** as the plan's snippet
     assumed (confirmed by reading the real Task-2-generated
     `build/saturn/sourceboot/generated/saturn_hud_glyphs_generated.h`,
     which the plan itself expected this task to check rather than trust).
     This matches the plan's own prose ("Power meter source art is
     32x32") but not its code, which called the upload helper with a
     literal `256U` pixel count against a 1024-element array -- not an
     out-of-bounds read (256 < 1024), but the wrong 256 pixels: the first
     8 full 32-wide source rows, not a 16x16 top-left square. Fixed by
     giving `hud_atlas_upload_pattern()` a source-stride parameter so the
     power-meter calls can correctly crop the top-left 16x16 region
     (stride 32, width/height 16), matching the plan's own stated
     "one representative 16x16 cell... via the top-left quadrant" intent.
  3. **This port's real VDP2 TV mode is 320x224** (`VDP2_TVMD_HORZ_NORMAL_A`
     / `VDP2_TVMD_VERT_224`, set in `user_init()`,
     `src/port/saturn/sourceboot/main.c:1512-1514`), giving a 20x14 visible
     grid of 16x16 cells (320/16, 224/16) -- not the 32x28 the plan's
     `HUD_TILE_COLS`/`HUD_TILE_ROWS` and header doc comment assumed "at
     this screen resolution". 32x32 is real too, but it is the raw
     `CHAR_SIZE_2X2` page's hardware capacity (`VDP2_SCRN_PAGE_WIDTH_CALCULATE`/
     `PAGE_HEIGHT_CALCULATE` in `scrn_macros.h`, fixed regardless of TV
     resolution), not what is actually on screen; conflating the two
     wouldn't have corrupted memory (32x32 cells are all validly
     addressable within the allocated PND page) but would have let a
     later layout task silently place HUD elements in the invisible
     16 columns / 18 rows outside the real 320x224 raster, a bug that
     would only have surfaced at Task 9/10's visual verification stage,
     much later. Fixed by splitting the single constant into
     `HUD_PAGE_STRIDE_COLS` (32, used only internally for the real PND
     address stride) and corrected `HUD_TILE_COLS`/`HUD_TILE_ROWS` (20/14,
     the public bounds `sm64_saturn_hud_atlas_write_cell()` checks against).

  All three were resolvable by adjusting the code to match verified
  reality rather than requiring an escalation. Verification performed:
  no target link is possible yet (needs Task 8's Makefile wiring and the
  full SH-2 game-tree link), but the real `sh-elf-gcc` 14.3.0 cross-
  compiler (found already installed at `work/yaul-install/bin/`) was used
  to both `-fsyntax-only` check and fully compile-to-object-file this
  source against the real vendored Yaul headers and the real generated
  glyph header, with `-Wall -Wextra -Wpedantic` and zero diagnostics. The
  resulting object's symbol table confirms every `sm64_saturn_hud_*` glyph
  array resolved, both public functions are correctly exported (`T`), the
  internal helper is correctly local (`t`), and the only unresolved
  externs are the two genuinely-external Yaul calls (`cpu_cache_purge`,
  `vdp2_scrn_cell_format_set`) plus compiler-generated helpers -- stronger
  verification than a plain read-through, though still short of an actual
  target boot. Added the Z-Treme `ztFont2NBG3()` pattern-only citation
  (dedicated character-mode plane, own VRAM region, single static page,
  topmost priority -- priority itself is set later, in Task 8) plus the
  Yaul-dependency verification notes to `docs/saturn/UPSTREAM_CODE_LEDGER.md`
  and `docs/saturn/PROVENANCE.md`.

- Added two read-only accessors to `hud.c`/`hud.h` (`get_hud_camera_status`,
  `get_hud_power_meter_state`) and a new `sm64_saturn_hud_snapshot_t` type
  (`src/port/saturn/gfx/saturn_hud.h`), then wired a `hud` field of that type
  onto the existing `sm64_saturn_render_snapshot_t` and filled it from
  `sourceboot_capture_render_snapshot()` (Task 3 of the VDP2 gameplay HUD
  plan, `docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md`). This rides
  the project's existing double-buffered, generation-tracked render-snapshot
  rail instead of building a second capture mechanism: `hud` inherits that
  struct's generation coherence for free. `render_hud()` itself is
  unmodified -- the two new accessors are pure reads of state it already
  computes every tick (`sPowerMeterHUD`/`sCameraHUD`), at the same
  external-visibility level `gHudDisplay` already has as an `extern` global.
  `saturn_hud.h` stays header-only (struct definition only, no capture
  function), per the task's explicit design constraint.
  One placement decision not spelled out in the task's snippet: the new
  HUD-capture block was inserted immediately before
  `sm64_saturn_render_snapshot_publish()`, not after it and not at the
  function's closing brace. `sourceboot_capture_render_snapshot()`'s actual
  body (`main.c:348-474` pre-change) continues past the `publish` call with
  quarantine-on-failure cleanup, so "the end of the function" and "before
  publish" are different places; writing `snapshot->hud.*` after `publish`
  would race a reader that may have already claimed the buffer via
  `acquire_ready`. Everything must be written into the snapshot before the
  single `publish` call that hands it off.
  Added `tools/saturn/saturn_hud_snapshot_test.c` (a host-only, pointer-free
  struct-shape test, no Yaul dependency) and a matching
  `verify-saturn-hud-snapshot` Makefile target/`.PHONY` entry, following the
  exact convention already established by `verify-render-snapshot-bank` and
  `verify-actor-instance-snapshot`. Verified RED (missing `saturn_hud.h`)
  then GREEN (compiles clean under `-Wall -Wextra -Werror`, runs, exit 0)
  directly with `gcc`, and confirmed the unmodified `verify-saturn-hud-snapshot`
  target itself also passes end-to-end. Also re-ran the pre-existing
  `verify-render-snapshot-bank` host test and `test_render_snapshot_source.py`
  (which regex-asserts no `sm64_saturn_render_snapshot_t` field contains
  `*`) after adding the `hud` field -- both still pass, no regression.
  Environment note for whoever runs this next: in this sandbox, MSYS
  `make` (`/c/msys64/usr/bin/make`) does not see the `OS` environment
  variable when invoked from the plain Bash tool, so `Makefile.saturn.mk`'s
  `ifeq ($(OS),Windows_NT)` silently takes the POSIX branch and points
  `SATURN_TOOLS_PYTHON` at a nonexistent `.venv-saturn-tools/bin/python`.
  Running the same target through PowerShell (with `.venv-saturn-tools`'s
  native-Windows `Scripts/python.exe`) avoids that, but `$(SATURN_REPO_ROOT)`
  is computed via GNU Make's own `$(realpath ...)`, which this MSYS build
  always renders MSYS-style (`/d/Code/...`); that path form is fatal to
  `subprocess.run()` under a native-Windows Python (`_winapi.CreateProcess`
  has no notion of `/d/...`), so the existing python-subprocess-wrapper
  convention (used by `verify-pcm-protocol`, `verify-render-snapshot-bank`,
  `verify-actor-instance-snapshot`, and now this target) only completes
  end-to-end in this sandbox with `SATURN_REPO_ROOT` pinned to a native
  Windows-style path on the command line. This is a pre-existing sandbox/
  toolchain friction affecting every target using that convention, not
  something introduced here, and out of this task's scope to fix.

- Closed two code-review gaps in the Task 3 HUD-snapshot capture above:

  First, `sourceboot_capture_render_snapshot()`'s HUD-capture block had no
  comment explaining why it must precede `sm64_saturn_render_snapshot_publish()`
  -- this function otherwise consistently explains ordering rationale inline
  (e.g. the observer-frame-timing comment at the top of the same function),
  and a future refactor (e.g. hoisting HUD capture into a helper called at
  the end of the function) could silently reintroduce the exact publish-
  ordering race Task 3 was careful to avoid, since nothing at the call site
  itself said not to move it. Added a comment directly above the block.

  Second, `saturn_hud.h`'s and the host test's own comments both claimed
  `sm64_saturn_hud_snapshot_t` is pointer-free and fixed-width, but nothing
  actually enforced either claim: `tools/saturn/test_render_snapshot_source.py`'s
  pre-existing regex sweep (`test_snapshot_types_have_no_pointer_fields`,
  which already protects `sm64_saturn_render_snapshot_t` and the actor
  bridge types from exactly this class of regression) never opened
  `saturn_hud.h`, and `saturn_hud_snapshot_test.c`'s `sizeof(...) == 0U`
  check only proves the struct isn't literally empty. Confirmed live: a
  `char *debug_label;` injected into the struct compiled clean under the
  same `-Wall -Wextra -Werror` the Makefile target uses and the host test
  still exited 0. Fixed by adding
  `_Static_assert(sizeof(sm64_saturn_hud_snapshot_t) == 22U, ...)` to
  `saturn_hud.h` (22 bytes independently verified via a host `offsetof`
  probe before trusting it: field layout packs 8 `int16_t`/`uint16_t`
  members through offset 16, one padding byte between the `int8_t`
  `power_meter_animation` at offset 16 and the `int16_t` `power_meter_y`
  at offset 18 to satisfy 2-byte alignment, then two trailing `uint8_t`
  fields through offset 21 -- no trailing struct padding needed since 22
  is already even) and adding `sm64_saturn_hud_snapshot` (pointing at the
  new `HUD` path constant) to `test_render_snapshot_source.py`'s existing
  `names` tuple. Re-ran the same `char *debug_label;` mutation after both
  fixes: the `_Static_assert` now fails the build
  (`static assertion failed: "hud snapshot ABI must remain fixed-width"`)
  and the Python sweep now fails independently
  (`AssertionError: sm64_saturn_hud_snapshot must not carry live game,
  graph, VDP1, or VRAM pointers`) -- both gates catch it, not just one.
  Restored the clean file and re-confirmed the host test, the Python
  sweep, and the pre-existing `verify-render-snapshot-bank` host test all
  pass clean afterward.

- Added `tools/saturn/extract_hud_glyphs.py`, a local-ROM-derived extractor
  for the real SM64 gameplay-HUD glyphs (digits, multiply/coin/Mario-head/star
  icons, apostrophe/double-quote, camera-status icons, power-meter wedge
  textures), following the same MIO0-decode + assets.json-offset-lookup +
  RGB1555-conversion pipeline established by `extract_mario_textures.py`.
  This is Task 1 of the planned VDP2 gameplay HUD (`docs/superpowers/plans/
  2026-08-06-saturn-hud-vdp2.md`); the HUD itself has no visible on-screen
  presentation yet, only diagnostic dbgio text. Several offsets in the
  original task plan were wrong and were corrected against `bin/segment2.c`'s
  `main_hud_lut`/`main_hud_camera_lut` arrays and the real `assets.json`: the
  digit-range formula only covered digits 0-4, `glyph_multiply`/`glyph_coin`/
  `glyph_mario_head`/`glyph_star` and the four non-`cam_camera` camera icons
  pointed at the wrong glyph slots (JP-only glyphs shift the US MIO0 layout),
  `cam_mario_head` is not a separate texture (the game reuses
  `texture_hud_char_mario_head`, so the manifest aliases it to
  `glyph_mario_head`'s key), and the power-meter filenames were wrong
  (`power_meter_one_segment` is singular, and the 8-wedge state is
  `power_meter_full`, not `power_meter_eight_segments`). As with all
  Nintendo-derived extractors in this repo, output is generated only under
  `build/` (gitignored) from the user's own ROM and is never committed.

- Closed a code-review gap in the HUD glyph extractor: the test suite had
  zero coverage of the actual pixel-extraction logic in `build_header`,
  confirmed by live mutation testing (a swapped `width, height, size,
  regions = entry` unpack and a `"big"` -> `"little"` endianness change in
  the RGB1555 conversion both passed the suite unnoticed). Added
  `test_build_header_extracts_correct_pixels_and_metadata` to
  `test_extract_hud_glyphs.py`, which feeds `build_header` a tiny hand-built
  synthetic MIO0 blob and asserts concrete pixel words, width/height,
  offset, and sha256; re-verified to catch both mutations before fixing
  them back out. Also fixed `extract_hud_glyphs.py`'s
  `decoded_bases.setdefault(base, mio0_decode(rom, base))`, which looked
  like a decode-once-per-base cache but wasn't: Python evaluates
  `setdefault`'s second argument eagerly on every call regardless of
  whether the key already exists, so `mio0_decode` ran once per glyph (30x)
  instead of once per distinct MIO0 segment (2x). This was wasted work, not
  a correctness bug -- re-running the real extraction against the real ROM
  after the fix produced byte-identical output. Removed the now-dead
  `json`/`Path` imports and the stale `power_meter_eight_segments` fixture
  key from the test file.

- Wired `tools/saturn/extract_hud_glyphs.py` into the sourceboot Makefile
  (Task 2 of the VDP2 gameplay HUD plan): a new grouped target generates
  `saturn_hud_glyphs_generated.h` and `saturn_hud_glyphs_manifest.json`,
  mirroring `SOURCEBOOT_MARIO_TEXTURE_HEADER`/`source-mario-textures`
  exactly (same `&:` grouped-output shape, same `--rom`/`--assets`/
  `--output`/`--manifest` invocation style). `source-hud-glyphs` is now a
  prerequisite of `source-assets`, so a full sourceboot asset build
  regenerates the HUD glyph data automatically, the same as every other
  ROM-derived Saturn asset. One deliberate deviation from the original task
  plan text: the plan's snippet introduced a new `SOURCEBOOT_HUD_GLYPH_DIR`
  variable hardcoded to `$(ROOT)/build/saturn/sourceboot/generated`, but
  that path is already the existing `SOURCEBOOT_GENERATED` variable (used
  directly, with no dedicated dir variable, by all the BOB-asset targets) --
  defining a second variable with the same value would have been dead
  duplication with no precedent elsewhere in the file, so the header/
  manifest paths are defined directly off `$(SOURCEBOOT_GENERATED)` instead.
  Scope note for whichever later task adds the C-side HUD renderer: this
  change does not yet add the generated header to `$(SH_OBJS_UNIQ)`'s
  order-only prerequisite list (the mechanism that blocks every compile
  until generated headers exist) because no `.c` file includes it yet --
  that wiring should land alongside the first consumer, mirroring how
  `SOURCEBOOT_MARIO_TEXTURE_HEADER` is listed there. Verified standalone
  (`make -f src/port/saturn/sourceboot/Makefile source-hud-glyphs`) against
  the real ROM: produces all 30 glyphs from `GLYPH_MANIFEST`, is a no-op on
  re-run, and `source-hud-glyphs` shows up in `source-assets`'s expanded
  prerequisite list per `make -p`.

### Changed

- Extended the iterative geo runtime seam with depth-first child/sibling
  scheduling, deferred children-first dispatch, explicit leave actions, and a
  callback-driven host trace. The new contract proves event order and state
  tokens before source handlers are moved; the production recursive policy
  remains red until that conversion is complete.

- Added the production-oriented geo-walk runtime seam with explicit node and
  sibling cursors, bounded overflow latching, and a host C contract. It is
  linked into sourceboot alongside the LWRAM owner but is not yet selected by
  `rendering_graph_node.c`; the recursive source-policy gate intentionally
  remains red until every handler has an enter/leave conversion.

- Refined the generated traversal owner to use a dedicated 16-byte SH-2
  continuation frame with both node and sibling cursors. The original 12-byte
  host scheduler frame remains a contract fixture; production storage now has
  the state required for the upcoming enter/leave dispatcher without changing
  the recursive source path yet.

- Added the deterministic full-game geo-depth manifest and linker-owned
  `.lwram_geo_traversal` arena. The generator scans all actor/level GeoLayout
  sources (518 current inputs), accounts for structural nesting plus shared,
  held-object, and callback edges, emits a 256-frame capacity with a recorded
  SHA-256 identity, and rejects missing, duplicate, undercounted, reordered,
  or undersized manifests. The linker now asserts generated byte size,
  alignment, actor-arena ordering, and slave-stack non-overlap. This is storage
  and map infrastructure only: production traversal still uses the recursive
  source dispatcher and no target/manual/FPS claim is made.

- Added the bounded Saturn geo-walk scheduler contract and host gate that will
  carry source scene-graph continuation state outside the SH-2 call stack. The
  contract records enter/leave phases, matrix/context tokens, high-water usage,
  and fail-closed capacity overflow; production source traversal is not yet
  switched over, so this change does not claim target stability or FPS.

- Reconciled the current no-texture manual image with its build identity:
  `SATURN_DEMO_PATH=0` intentionally uses the source Fast3D RGB-only VDP1
  emitter, while `SATURN_DEMO_MARIO_TEXTURES=1` only covers the Mario demo
  assets. A fresh `SATURN_DEMO_PATH=1` dual-SH2 BOB image is kept as a
  textured visual diagnostic; the full-game source route still requires
  texture residency and texture-aware VDP1 lowering. This preserves the
  DRAM/profile contract and avoids hiding the gap by changing the default.

- Added source-owned SH-2 exception capture to the dual-SH2 sourceboot path:
  both master and slave vector tables now preserve a register frame in a
  linked HWRAM record before delegating to Yaul's normal green-reset/debug
  handler. This corrects the earlier desktop evidence that over-attributed the
  blank-green screen to master-stack exhaustion; the runtime gate remains open
  until the current `.ymir-profile` desktop launch either stays stable or yields
  a decoded frame. No single-SH2 fallback, texture bypass, or linker-margin
  relaxation is introduced.

- The dual-SH2 sourceboot image now places the downward-growing slave stack in
  an explicitly reserved 16 KiB tail of LWRAM instead of the small HWRAM
  window at `0x06001E00`. The old placement could exhaust during nested render
  callbacks and let an exception frame descend below HWRAM, producing the
  observed desktop black-screen crash. Linker assertions keep static LWRAM
  arenas below the reservation; VDP1/Gouraud transport ownership and the
  dual-SH2 requirement are unchanged. Host/link evidence and a bounded Ymir
  stack probe pass, while target stability, manual visuals, and FPS remain
  open.

- The production sourceboot link now preserves the dual-SH2 configuration
  (`SATURN_SLAVE_RENDER=1`) while making CPU-only renderer scratch explicitly
  LWRAM-owned. Scene-admission traversal borrows caller-supplied scratch from
  the phase-owned terrain command bank, and CD staging/file-list storage borrows
  the prefix of the pre-initialization LWRAM main pool before `main_pool_init()`
  resets it. VDP1 command banks, Gouraud/SCU-visible staging, and other uncached
  transport storage remain in their existing HWRAM owners. The fresh serialized
  BOB link (`e2-bob-identity-id-bd57c0a81635606c`) passes with HWRAM margin
  `0x1BC8` and full-section LWRAM margin `0x4780`; this is source/link evidence only and
  does not claim target/P2, Ymir/manual, texture, audio, or FPS closure. A
  single-SH2 build is not a production substitute.

- Sourceboot now keeps the immutable build-identity tuple in HWRAM `.bootdata`
  instead of cart-resident `.rodata`. The pre-cart guard therefore validates
  before `source_cart_load()` can safely read the DRAM cart; the previous
  placement dereferenced a non-resident cart VMA and spun at `main.c:1423`,
  presenting as a black screen. The new identity-residency test and a fresh
  Pipe-4 BOB image preserve the existing profile-managed DRAM launch contract.

- Sourceboot now places the CPU-only Fast3D frontend state in the NOLOAD
  LWRAM work arena and initializes it before the bootstrap VDP2 profile read.
  This reclaims 44,616 bytes of HWRAM for the remaining link-capacity gate
  without moving VDP1 command/Gouraud staging or any SCU-DMA-visible buffer;
  the explicit init ordering preserves startup semantics for the un-zeroed
  LWRAM section.

- Reconciled sourceboot's HWRAM VDP1 command-bank placement with the deferred
  frame-bank runtime contract: command sources are now admitted only when they
  lie in the bounded HWRAM/LWRAM ranges legal for CPU-DMAC, while SCU Gouraud
  staging remains HWRAM-only. The host gate covers both HWRAM initialization and
  cart rejection. Host compiler recipes now inherit `HOST_CC_ENV`, and the
  MSYS2 launcher preflights the transitive GCC/binutils DLL closure and puts
  both runtime directories first on `PATH`, preventing bare helper launches
  from producing missing-DLL dialogs.

- Sourceboot now bootstraps its generated build-identity spec from canonical
  route, input, camera, cart, scene, actor, animation, and feature-selected
  audio provenance before selecting an output directory. Each manifest is
  hash-validated by the existing identity generator, so a clean tree no longer
  fails on a missing spec and a failed bootstrap cannot silently reuse a stale
  identity or handwritten label. The descriptive identity label remains an
  emitted artifact, while the Yaul object directory now uses a validated short
  configuration-hash tag to stay within Windows path limits.
  The bootstrap now seals a conservative full source/config/linker/tool closure
  rather than a three-file whitelist. Its scene, dependency, actor, and
  animation fields hash the exact generated feature-off comparator payloads;
  semantic audio deliberately fails closed until a staged S64A/AUDIO.DAT and
  sound-CPU image are integrated. This prevents provisional recipes from being
  represented as final package bytes; Task 22 remains the final-package owner.
  Clean sourceboot builds now run a distinct `identity-assets` stage before
  seal-stage Make parsing, so exact generated bytes are produced rather than
  silently borrowed from a dirty workspace. The sealed closure includes the
  source sky/texture roots and exact generated compile/`incbin` inputs,
  including `water.png` and its baked sky output.
  The closure now also seals generated BOB scene/BSP/fragment and quad-map
  headers used by compiled C sources. The asset-stage escape is restricted to
  the sole `identity-assets` goal; invalid stage values and attempts to use
  the asset stage for normal build/verify goals fail during Make parsing.
  Identity sealing now derives the complete sourceboot host-generated include
  closure through the same `prepare_sourceboot_assets.py` traversal and source
  roots as the asset producer, validates every selected `build/us_pc` target,
  and includes text strings. Feature-on Mario animation additionally requires
  and seals the generated actor-bank C source. Multiword stage values now fail
  before Make can treat them as a bypass request.
  Generated host headers are now followed transitively from the discovered
  asset targets, so `text_menu_strings.h` and any future quoted generated
  header include are sealed or cause a clean fail-closed diagnostic. Feature-on
  actor-bank-C absence and byte mutation are covered explicitly.

- Added a dedicated 16-byte-aligned, NOLOAD `0x10000` LWRAM actor-runtime
  owner in sourceboot, replacing standalone actor observer/bank storage and
  explicitly clearing it through the cache-through alias before binding the
  existing lifecycle. Linker symbols and exact-size/alignment assertions make
  the reservation visible. This source-only step leaves linked route, target,
  Ymir/manual, and FPS evidence open.

- Relocated the complete 32-byte-aligned sourceboot VDP1 command double-buffer
  from LWRAM to ordinary HWRAM `.bss`, preserving its explicit backend
  initialization and frame-bank lifetime while reclaiming `0x20000` LWRAM
  bytes for the separately planned actor arena. The linker now rejects any
  future `.lwram_cmdts` input. This source/link preparation does not claim
  target transfer, P2, Ymir/manual, or FPS evidence.

- Added an exact producer-owned pre-acquire actor-bank recycle path for
  stranded WRITING and unacquired READY generations. It validates bank index,
  nonzero generation, and expected phase; clears the full cache-through
  snapshot payload and metadata before publishing FREE; preserves the other
  bank and monotonic last-published generation; and rejects quarantine,
  rendering, complete, free, stale, or double dispositions. Capture and the
  immediate sourceboot acquire-refusal path now use this narrow recovery path,
  while post-acquire handoff quarantine behavior remains unchanged.

- Repaired sourceboot's actor-observer generation handoff so a source tick
  computes the frame pipeline's nonzero successor once, before its geo walk,
  and uses that value for observer opening, source-tick publication, actor
  capture, profiling, camera bypass, and idle-probe publication. This prevents
  the `UINT32_MAX -> 0` observer/capture mismatch while preserving scheduler
  cadence, the existing action-generation validation, and all public ABIs.
  Host source-contract mutations now reject raw increment, capture, and camera
  consumers that bypass the named successor. Target visibility, sourceboot
  image/BIOS handoff, Ymir/manual, and FPS evidence remain separate gates.

- Separated Task 14's source object-pool identity domain from its compact
  drawable snapshot domain. The observer now accepts and tracks all 240
  source-attested `OBJECT_POOL_CAPACITY` slots (including parent identities
  and incarnation reuse) while its immutable 188-byte snapshots and 64-byte
  queue descriptors retain the existing 64-observation ceiling. This grows
  the fixed observer sidecars from 12,320 to 13,024 bytes; the fixed 65,536
  byte actor arena remains unchanged by reducing the derived output-record
  ceiling from 2,806 to 2,718 records. Slots outside the 240-entry source
  pool still fail closed, and a 65th compact observation still latches
  overflow. This host-only repair does not alter sourceboot timing, package
  reservation, renderer cutover, target/Ymir behavior, or FPS claims.

- Added a bounded, host-only Task 16 lifecycle handoff from the authoritative
  actor snapshot bank to the existing actor queue and stable batch builder.
  It acquires one exact ready bank generation, regenerates each descriptor's
  snapshot-derived identity while preserving caller-owned material/output
  fields, quarantines post-acquire mismatches, requires terminal queue state
  before batch/complete, and requires an explicit output-consumer
  acknowledgement before queue reset then bank retirement. Zero snapshots are
  a production-safe no-op. This deliberately does not attach the handoff to
  sourceboot, change the feature-off Mario wrapper, introduce meshlet work, or
  claim target/Ymir/manual/FPS evidence.
  Review hardening makes zero-count publish and own an explicit terminal queue
  generation, binds descriptor and source ordinals to their captured snapshot
  order, rejects insufficient worst-case batch storage before acquisition, and
  records queue-reset completion so a transient bank-retire failure can be
  retried without a destructive second reset. Symbolic boundary fixtures cover
  the exact actor, output, and batch ceilings.

- Added the Task 16 feature-off actor compatibility boundary. The existing
  ACTOR_ADMIT/ACTOR_LOWER world-graph descriptors now route through explicit
  Mario callback wrappers when `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0`, while
  an accidental feature-on build fails closed until the source-derived generic
  actor cutover is separately reviewed. This preserves the current Mario
  renderer and does not claim generic actor meshes, target/Ymir output, or FPS
  improvement. The wrapper contract gate now parses each preprocessor branch
  separately and mutation-proves both feature polarity and each callback's
  exact delegation, preventing an inverted branch from appearing green.

- Added the host-only Task 21 completion/ack ABI slice without changing any
  existing 16-byte command word meaning. The v2 mailbox now reserves a
  pointer-free 32-entry completion ring, full 32-bit active/prepared package
  status, explicit accepted/rejected/stale/fault/dropped/finished/prepared/
  committed results, and ticket-returning enqueue APIs. Completion-aware
  callers require an advertised capability; pending two-lap cursor tickets
  cannot be reused before a terminal acknowledgment, malformed or duplicate
  completions fail closed, status reads are bounded by a stable publication
  flag, and the final eight completion slots are reserved for required
  acknowledgments. A typed PLAY_REFRESH enqueue rejects zero or greater-than-
  16-bit package generations before any sound-RAM write because its existing
  word 3 remains a non-wrapping per-boot epoch. This does not add an MC68000
  producer, source service, package commit, target/Ymir, or audible behavior;
  asynchronous FINISHED identity remains deferred to a versioned tagged-event
  extension and may not yet mutate source policy.
  Rereview repairs bind each pending cursor to its exact opcode. Legacy
  enqueue remains an explicit no-ack path only while completion capability is
  absent; capability mode rejects it before any wire write and requires every
  command to reserve a ticket. An unmatched/corrupt completion latches the
  transport fault so a delayed old acknowledgment cannot retire a later
  same-cursor/opcode ticket. The repair also replaces the reusable writing
  flag with an advancing even/odd publication
  sequence, saturate the live command-ring counters at `0xffff`, and use one
  closed status/opcode matrix. Wrong same-class acknowledgments, FINISHED as a
  command acknowledgment, dropped stop commands, and generic acceptance of
  package prepare/commit now fail without retiring the original ticket.
- Added the bounded Task 21 project-owned sound-CPU boot contract. Cold boot
  and explicit recovery now have a host-proven order from staged-byte
  validation through generic SMPC stop, bounded stopped-state proof, then
  512-KiB selection,
  owner-validated clear/copy/mailbox publication, restart, and bounded READY
  plus heartbeat advance. Warned Yaul sound convenience calls are prohibited;
  the generic call's undocumented OREG31 byte is telemetry rather than a
  boolean shortcut, and every modeled failure returns a named fault without an
  unbounded wait. This does not enable sourceboot audio or claim package,
  target, Ymir, hardware, manual, audible, or performance completion.
  Soundtest now invokes and verifies that memory-mode callback and retains a
  persistent command count, last command, and raw OREG31 diagnostic. Host
  tests prove both `0x00` and `0xFF` are retained after typed completion rather
  than interpreted as false/true.
- Routed the soundtest and PCM protocol host executables through Python
  subprocess launch so MSYS quoted-path/DLL resolution cannot create GUI
  missing-DLL failures; protocol and proof semantics are unchanged.

- Added bounded BOB actor-effect infrastructure derived from the generated
  closure rather than runtime behavior-name branches. An exact pinned oracle
  covers billboard, alpha-cutout, translucent, shadow, particle, decal,
  projectile, reward, and effect records; identity, inventory, role, unknown
  feature, stale generation, and fixed-capacity mutations fail closed. The
  pointer-free descriptor path preserves source lifetime, uses the existing
  fixed-point billboard basis, distinguishes cutout replacement from true
  VDP1 half-transparency, and produces stable far-to-near order. The nine
  `GEO_CULLING_RADIUS`/`GEO_BRANCH_AND_LINK` records and all production
  observer effect fields remain explicitly unresolved, so this host-only slice
  does not claim target/Ymir/manual output, complete BOB effects, or FPS gain.
  The effect ABI requires the canonical nonzero actor-bank hash at admission
  and carries a compact trusted bank token through descriptor and lowering
  output. One shared validator protects direct lowering and master ordering
  from stale bank identity, crafted unknown bits, and unresolved source state
  instead of assuming every public descriptor came from admission.
  Lowering also requires the caller's current frame and scene-package
  generations, closing a self-referential check that could otherwise accept an
  old descriptor after ordering or across a direct-lower call.

- Added the bounded Task 19 actor-capability admission infrastructure. The
  generated BOB closure now has an independent exact oracle for ANIMATED,
  SWITCH, PARENTED, HELD, MODEL_MUTATION, and LOD, including behavior-spawned
  children and boss rewards. Generic selection admits ANIMATED, SWITCH, and
  MODEL_MUTATION only; PARENTED, HELD, and LOD fail closed until typed immutable
  snapshot evidence exists. A resealed S64F stored-mask mutation verifies that
  unknown serialized bits fail after content-hash verification. This does not
  claim runtime pose/queue/lane/parent/held/model/despawn/reward integration,
  enemy completion, target/Ymir output, manual playability, or an FPS change.

- Added closure-derived generic actor capability records for the bounded Task
  18 slice. Rigid/opaque/static-transform/platform/collectible class bits and
  transform/scale/material/surface/lifecycle runtime masks now travel in the
  pointer-free S64F v2 family bank; the family record grows from 52 to 56
  bytes, and the C validator rejects unknown or unsupported masks. The pinned
  BOB Area 1 header-content digest is externally asserted, while the pre-change
  v1 digest is rejected; the mutation test reseals its payload before checking
  the unknown-runtime-bit branch. Platform/collectible hints are analyzer
  vocabulary only and are marked unverified; schema v1 has no typed evidence
  field, so those classes and runtime surface remain unavailable/fail-closed.
  The serial family-bank gate and independent RED oracle distinguish the eight
  opaque closure records as seven representatives (the checkerboard behaviors
  share one family), five additional rigid/static-transform representatives,
  and one non-rigid `bhvBreakBoxTriangle` representative: 13 unsupported
  representatives/reasons covering 14 closure records. No family allow-list
  or fabricated source field is used. This is host evidence only and does not
  claim target/Ymir/manual/FPS completion.

- Hardened the bounded SCSP scheduler after rereview. Stale package correction
  is now transactional for the caller: an emitted key-off returns applied
  success, while zero command capacity records a fault and retains the keyed
  shadow for retry instead of forgetting hardware state. Software total-level
  attenuation is the sole simplified envelope owner; SCSP EG remains at its
  neutral immediate/full setting, and no source-faithful Project12x ADSR claim
  is made without full envelope-table traces. The MC68000 gate now discovers
  the pinned PoneSound bundle, force-rebuilds every Task 17 object, and attests
  the PoneSound commit, exact GCC executable hash/version, fresh `elf32-m68k`
  artifact, and empty undefined-symbol set, preventing cached host state from
  masquerading as freestanding evidence.

- Added a bounded MC68000-local audio scheduling infrastructure slice that
  consumes Task 15's pointer-free note events without exposing native voice
  state to either SH-2. A 20-note allocator protects music from ordinary SFX,
  steals the lowest-priority/oldest eligible SFX deterministically, preserves
  source note-duration, tuning, pan, and release inputs, and reports drops and
  malformed or duplicate generations. A simplified timer-driven linear
  attack/decay/sustain/release model feeds software attenuation and is not a
  trace-backed close-port of Project12x's arbitrary envelope tables. A
  32-slot desired-voice shadow emits only changed scalar register commands,
  with key-off before reassignment and key-execute last. Raw SCSP writes remain
  confined to an explicit `scsp_pcm8` command executor. Serialized host gates
  and a freestanding MC68000 relocatable-module/undefined-symbol gate pass;
  the modules are deliberately not linked into the heartbeat image or runtime
  loop because Task 12's real package/sequence assets and residency bindings
  are absent. Production timer IRQ wiring, complete source envelope tables,
  full PCM68K image/package, target, Ymir, hardware, manual, and tempo evidence
  remain open rather than inferred from this infrastructure result.

- Added a bounded, pointer-free M64 sequence VM scaffold for the MC68000
  audio lane. It preserves Project12x compressed-delay, tempo-accumulator,
  call/return, loop, branch, and note timing semantics while emitting scalar
  control/note events only; it performs no SCSP writes and is not linked into
  sourceboot. An eight-frame stack, 64-instruction tick budget, bounded event
  output, and strict operand/target validation fail closed on malformed,
  truncated, unknown, or non-progressing streams. The serialized
  `verify-sequence-vm` gate launches through Python subprocess to avoid the
  inherited MSYS quoted-executable/DLL failure. No expanded seq00 bytes are
  present, so full sequence/catalog, S64P, target, Ymir, and manual audio
  claims remain open.

- Tightened the sequence VM's source parity at the layer boundary: portamento
  uses the source's special one-byte timing form when its mode bit is set, and
  layer transpose consumes the source unsigned byte representation. The
  focused VM gate covers the special portamento operand shape.

- Hardened the bounded sequence VM's source contract after review. Layer
  velocity and short-note duration now persist across delayed ticks (including
  the source `0x80` initial duration); an explicit US versus EU/SH format
  selector disambiguates the reserve/unreserve opcodes and EU/SH relative
  branch predicates; channel init/disable/test commands maintain bounded
  active/finished masks; and RED tests cover stack overflow/underflow,
  zero-count 256 loops, output overflow, invalid complete targets, branch
  polarity, layer call/loop, format-specific reserve operands, and persistent
  short-note state. This remains a host-only VM slice: no target compiler,
  SCSP/PCM driver, expanded seq00 payload, full sequence catalog/S64P closure,
  Ymir, hardware, or manual audio evidence is claimed.

- Narrowed the format-specific VM boundary after rereview. Large-layer note1
  now stores and emits its source-mandated zero duration, EU/SH layer relative
  jumps use the bounded flow engine, and `seq_initchannels` accumulates selected
  channel bits instead of discarding earlier selections. EU/SH sequence `0xda`
  fade-state and `0xdc` tempo-add forms are rejected before operand consumption
  until their distinct state machines are represented; the RED fixture covers
  these fail-closed cases and the large-note transition. This keeps the claim
  limited to the bounded host VM and does not imply full EU/SH package support.

- Added a dedicated, bounded actor-instance queue and master-only batch merge
  without expanding the proven eight-entry world graph. Each pointer-free job
  owns one Task 14 snapshot identity and a disjoint output span, either SH-2
  may claim it exactly once through the existing P2/TAS publication pattern,
  and stale generation/package/bank/incarnation or claimant failures
  quarantine only that instance. Stable batches preserve published painter
  order while grouping adjacent compatible family/material work. The queue's
  fixed memory report derives from the shared snapshot ceiling; BOB capacity
  remains an explicit package-evidence gate rather than an assumed claim. The
  DLL-safe inherited gate also now checks the worker context's current inline
  references instead of requiring primitive/material pointers that were
  deliberately removed from that context. Rereview accounting now places the
  Task 14 two-generation bank container, observer, queue, 64 master batches,
  and 2,806 eight-byte output records in one 16-byte-aligned 65,536-byte actor
  arena; record 2,807 fails closed. Batch construction reads its count through
  the queue's P2 alias. This remains infrastructure source-incomplete until
  generic actor-meshlet preparation and a production drain/cutover exist; the
  concurrent retirement race and all target evidence remain open. The runtime
  memory report exposes every component, output byte, and alignment byte, and
  executable boundary mutations pin overflow quarantine, the valid 64th
  instance, and exact-fit publication.

- Added the scene-neutral admission boundary for validated render packages.
  Generic cluster/node/portal views now reject malformed metadata before
  traversal, perform conservative Z-Treme-derived frustum tests before
  transform/classify/lower, preserve mandatory clusters, and emit ordered
  bounded references with cycle/capacity telemetry. BOB adapts through a
  deterministic package-view helper while the full runtime remains
  scene-independent; target/Ymir activation and FPS evidence remain open.
  The serial DLL-preflight gate also uses Python subprocess launches for the
  host executables, avoiding the inherited MSYS quoted-path EOF failure.
  The production BOB render-prep now constructs and consumes this package
  view; frame-owned orientation overrides static metadata, portal endpoints
  must agree in both adjacency lists, and queued nodes are marked before
  enqueue to keep bounded traversal deterministic.
  The sourceboot link list now includes the admission implementation, and the
  generated BOB header emits the generic admission node/ref section consumed
  by render prep; the 1,183-node source BSP remains available to the fallback
  painter without forcing the generic bounded worklist to truncate it.
  Generated scene headers now include the admission ABI directly, so emitted
  node/ref records are compile-time type checked at the consumer boundary.
  Zero lateral view rows now force a depth-only conservative fallback instead
  of inheriting stale package axes; package validation requires global cluster
  coverage, node containment, endpoint-only portal ownership, and zeroed
  reserved fields, while mandatory clusters are retained even when their node
  is outside the current frustum.
  Generated render-cluster records now explicitly initialize the optional scene
  identity fields, keeping the generated-header Werror smoke gate aligned with
  the generalized ABI.

- Added the generic source-resolved actor-instance seam. The geo observer records
  only authoritative scalar decisions, while two bounded snapshot banks publish
  pointer-free actor state with pool-slot/incarnation identity, package/bank
  hashes, parent/held offsets, switch/render/shadow/effect fields, and
  fail-closed stale-family, malformed-source, and capacity handling. The
  sourceboot capture is ordered after the authoritative tick while the
  observation window opens before that tick; host gates cover typed-field
  mutations, model-less controllers, despawn/reuse, and immutable bank
  lifecycle. Production geo hooks resolve bounded pool/model scalars, but
  unresolved generated family/scene/bank identity is deliberately rejected
  rather than fabricated. The render-snapshot gate now launches its Windows
  host test through Python subprocess to avoid the inherited MSYS quoted-path
  EOF error.

- Hardened actor snapshot publication after lifecycle review: observer
  overflow now latches for the frame, capture bounds-checks every source pool
  slot before reading incarnation state, and separately reports legal source
  pool slots that exceed the bounded observer capacity instead of silently
  dropping them. Telemetry resets per source frame so despawn/reuse and
  capacity failures remain attributable. Actor banks reject stale or duplicate
  generations with the shared wrap-safe helper, retain independent generation
  tickets per physical bank, and publish/acquire lifecycle state through the
  SH-2 cache-through alias with compiler fences. The physical bank/observer
  assertion now binds to Yaul's target-declared `LWRAM_SIZE`; the Task 16
  fixed actor-arena cap remains a separate queue contract. Production source
  fields without an authoritative generated registry (family, scene package,
  bank, visibility/range/switch/opacity/held-parent/effect state) remain
  explicitly zero/default and are rejected by capture rather than fabricated.
  The linked sourceboot gate remains open until the Yaul/MSYS make wrapper is
  repaired and the generated actor-family registry is bound at the production
  geo seam.

- Connected actor capture to the real two-bank sourceboot handoff. A published
  bank is claimed for the render overlap window and is retired only at the
  terminal boundary; failed publication or rendering quarantines that exact
  generation. This keeps actor state immutable while preserving the bounded
  64-instance package ceiling.

- Hardened the Saturn audio package boundary after ABI review: chunk and
  package SHA-256 values are recomputed by the C residency validator, malformed
  replacement generations fail closed without mutating the active plan, and
  the MC68000 token retains the complete source digest. The catalog binds
  BOB/WF closures through content-addressed S64P dependency records, preserves
  signed PCM polarity and source metadata, rejects empty sequence inputs, and
  emits checkout-portable sample paths. These checks keep generated audio
  artifacts deterministic while leaving playback/transport integration open.
  The MC68000 header and implementation now share the same full 32-byte source
  digest field, so target compilation cannot silently validate a stale CRC ABI.
  Scene dependency digests now hash framed sequence, bank, and PCM bytes and
  publish an explicit `audio/bob` or `audio/wf` root with its selected chunk
  hashes.
  Sequence 00 now requires the expanded generated payload; a wrapper-only
  `sound_data.c` input fails closed and blocks the complete package gate until
  the real source asset is supplied.
  Residency plans now model a bounded scratch/work span, reject all overlaps
  through a shared public validator, and require validated active/replacement
  plans at commit and MC68000 acceptance rather than trusting caller spans.
  AIFF MARK/INST loop markers and bank-side tuning/envelope/pan bindings are
  retained in the manifest, invalid all-zero/short `.m64` control streams fail
  closed, and C package validation rejects overlapping descriptor payloads.
  MC68000 acceptance now receives both active and replacement plans and rejects
  cross-generation span overlap before acknowledging a replacement.
  The disjointness helper is null-safe before inspecting either plan, ignores
  zero-length spans, and MC68000 admission validates both active and
  replacement plans.

- Added the source-authoritative S64A audio catalog compiler.  It consumes the
  35 sequence mappings, 38 banks, and 219 user-extracted AIFF samples directly,
  records source/package hashes, emits aligned big-endian AUDIO.DAT chunks and
  BOB/WF closure manifests, and converts PCM16 to deterministic Saturn PCM8.
  The bounded residency contract retains an active generation until the
  MC68000 acknowledgement and rejects post-boot whole-RAM clears; generated
  catalog data stays untracked and target playback/transport integration remains
  a later gate.

- Closed the remaining S64F admission-integrity gaps: runtime capability
  selection now ranks total family capability bits, the host validator rejects
  empty/unknown-flag banks, and the C validator recomputes and optionally
  binds the payload SHA-256 before exposing records.  The executable family
  test now checks the unequal-capability selection invariant and payload
  tamper rejection.

- Corrected generic family selection to exclude model-less/controller records
  from drawable runtime admission while retaining them in the closure report;
  both the Python proof and C selector now require the immutable geometry flag.

- Added a generic, content-addressed S64F actor-family bank compiler for every
  BOB closure record.  Source geo vocabulary, material flags, animation,
  multiplicity, effects, and model variants become stable capability records;
  unsupported geo nodes and stale closure hashes remain explicit so supported
  families can still be inspected without pretending the closure is complete.
  Family payloads use bounded offsets and source provenance, and the C ABI
  selects the smallest supported capability/capacity record without a
  Goomba- or scene-specific runtime branch.  Target/Ymir activation and final
  scene-package linkage remain intentionally open.

- Hardened complete-animation promotion against extreme Q16.16 translation
  sums and matrix overflow by failing closed before narrowing; sourceboot now
  hands each selected pose through a two-slot immutable render buffer so frame
  overlap cannot overwrite a worker's vertices.  The diagnostic sweep hashes
  that selected pose without invoking a second evaluator, while the legacy
  walking-bank compatibility fields remain feature-off only.

- Added the feature-selectable compact Mario pose evaluator.  The enabled
  sourceboot path consumes the source-selected animation ID/frame after the
  authoritative geo tick, evaluates one bounded 20-joint pose from the
  validated 209-ID S64B bank, and feeds the existing meshlet admission path;
  it does not introduce a Saturn animation clock or duplicate material/switch
  ownership.  Feature-off retains the legacy generated pose selector.  The
  serialized variant builder seals matching ELF/CUE/ISO hashes, while the
  non-promotable animation-sweep validator rejects missing/duplicate IDs,
  diagnostic-only evaluator symbols, fallback/corrupt telemetry, and identity
  drift.  Target/Ymir sweep evidence remains outstanding until a linked
  variant reports all 209 IDs.

- Replaced sourceboot's optional silent-audio path with a feature-selected,
  source-authoritative SH-2 policy adapter while preserving every public
  `src/audio/external.h` signature and retaining the silent translation unit
  as the feature-off rollback.  The adapter keeps the six-entry background
  queue, priority/duplicate handling, secondary music, jingles, fades,
  lower/unlower constraints, bank masks, one published SFX per bank,
  continuous freshness, stops, getters, and moving-source spatial updates on
  the SH-2.  Admission uses the inherited requested-priority plus exact
  distance/front weighting, and volume/pitch retain per-level acoustic reach,
  bank range, moving-speed, constant-frequency, and vibrato rules before
  quantization; distance uses the repository's existing target `sqrtf`
  service instead of a duplicate 24-iteration divider loop.  Active source
  positions are reevaluated each game-audio tick; waiting discrete requests
  enter a bounded 256-record request queue before one per-bank selection, so
  same-frame bank masks/stops/getters observe the inherited pre-admission
  state; queued pointer-token positions are reevaluated at admission rather
  than frozen at `play_sound()`, and requests expire after the inherited
  countdown.  Pending requests also retain their token lease across
  same-frame stop calls until they are admitted or rejected,
  and spatial refresh is keyed by the full sound handle as well as the source
  token so simultaneous cross-bank sounds sharing one position keep their
  own bank- and flag-specific volume, pitch, and priority,
  published requests retire through generation-matched completion feedback,
  and invalid per-bank sound IDs fail before consuming an identity slot.
  Jingle/secondary completion, published-SFX lowering, and global fades now
  produce bounded aggregate semantic actions; the latter uses one non-menu
  bank-mask record instead of nine indistinguishable channel records.  The
  Saturn/SH two-tick jingle guard, secondary `0xFF` no-op, normal-volume
  sentinel, and stop-bank lowering restoration remain source-compatible;
  early generation-matched ENV completion is retained until the guard drains
  rather than being lost, while a completion made stale by a newer ENV
  generation is discarded so it cannot block later feedback.
  Protocol-v2 `PLAY_REFRESH` records carry only fixed-width
  `soundBits`, generation-tagged source tokens, package/freshness generations,
  and quantized volume/pan/pitch; raw `f32 *pos` identities stay in a bounded
  SH-2 table, and exhausted token slots retire instead of wrapping into an
  ABA collision.  The task deliberately does not add a 68000 sequence VM, sample
  packages, SCSP voice integration, or claim target/Ymir audio.

- Added a static source audit and an illustrative digest model for the proposed
  state-only source-geo optimization, while leaving it deliberately disabled.
  The audit identifies animation, painting, water/moving-texture,
  camera/matrix-derived object, lifecycle, and visibility work interleaved
  with display construction, but it does not execute the real graph and is not
  differential evidence.  A reserved digest API therefore fails closed without
  touching caller output.  The optimization remains blocked, its true
  normal-versus-suppressed graph differential remains undone, and the normal
  full geo walk stays authoritative.

- Hardened the compact Mario actor bank after independent review.  The target
  decoder now proves every packed GEO1 table boundary and count, joint/branch
  ownership and node ordering, RGB555 material, meshlet bounds/tier spans,
  globally gap-free source-ordinal ownership, exact tier primitive/vertex relationships,
  primitive material/vertex ownership, and the compiler-derived minimum
  scratch requirement before exposing a bank.  Callers may bind validation to
  the expected eight-word source identity, so a nonzero but wrong source digest
  also fails closed.  Host validation pins the repository's actual 193-path
  animation inventory at commit `68f9dd10` via canonical path-set SHA-256
  `2d7c66e9…c67e1`, in addition to unique provenance paths, lowercase hashes,
  per-animation membership, and payload digest binding.  This rejects both
  in-range internally inconsistent GEO1 payloads, coordinated duplicate/gap
  meshlet partitions, degenerate primitive shapes, and self-consistent resealed
  filename repartitions; the JSON schema is advisory while executable
  validation owns semantic authority.

- Added a deterministic, content-addressed, big-endian `S64B` Mario actor
  bank that retains all 209 source animation IDs from all 193 animation files
  as deduplicated, length-prefixed index/value channels instead of expanding
  8,140 frames into roughly 24.16 MiB of posed vertices and light inputs.  The
  596-KiB payload carries checkout-stable source hashes, the 20-joint source
  hierarchy, joint-local vertex ownership, branch/node ordinals, materials,
  primitives, meshlets, a 3,928-byte scratch bound, and an S64P dependency
  descriptor.  A bounded C decoder rejects malformed stream spans, hashes,
  skeletons, and ownership; differential generation proves every frame of the
  legacy idle and walking fixtures produces exact vertices and light inputs.
  The old Mario animation-object converter and 1.32-MiB compatibility mesh
  remain byte-identical.  That compatibility mesh still represents only the
  normal-cap/front-eye/open-hand selection; source switch-variant geometry and
  production frame/action cutover remain explicitly owned by Task 10.

- Replaced the single PCM proof command ring with pointer-free, big-endian
  semantic-audio protocol v2 rings: eight protected control records at
  `0x04040` and twenty-four SFX records at `0x040C0`.  Two-lap cursors retain
  every physical slot, validate corrupt producer/consumer distances, and
  publish producer/consumer cursors only after record/telemetry bytes.  The
  MC68000 validates both rings and the protocol version before consumption,
  always spends its bounded poll budget on control first, and reports separate
  control/SFX saturation and consumption counters.  This prevents an SFX
  burst from dropping future music/package control while keeping rendering
  and simulation independent of audio service.  The exact v1 ABI remains a
  tested historical contract, and the audible proof soundtest now emits the
  equivalent reset/master/proof-tone commands through v2 only after the new
  host gates pass.

- Bound every S64P residency reference to a bounded, nonzero lease token
  instead of trusting an aggregate consumer count. Duplicate acquisition,
  duplicate/stale release, and token-table exhaustion now fail closed without
  changing another snapshot, VDP1 frame-bank, actor-bank, or audio-voice
  lease, preventing an old generation from being unloaded while a legitimate
  consumer still owns it.

- Hardened S64P residency after independent review.  Malicious resealed roots
  can no longer drive an unsigned offset underflow and out-of-bounds scan.
  Residency now copies roots and feature-active payloads into explicit,
  non-overlapping caller-owned spans and rehashes those owned bytes at atomic
  commit, so later mutation of CD/cart staging input cannot alter a published
  generation.  Absolute aligned placement retains old and new generations
  without overlap; refcounted render-snapshot, VDP1-frame-bank,
  actor/animation-bank, and audio-voice hooks prevent early reuse.  Sourceboot
  now exposes only the aligned CART range above `SOURCE.DAT` and strictly
  rejects an optional linked provisional root before entering the game loop.
  Runtime stable-ID, zero-generation, and zero-byte rules now match the Python
  S64P validator, and the target cart boundary explicitly declares its
  freestanding memory primitive rather than relying on an implicit prototype.

- Added bytewise big-endian target validation and exact-generation residency
  for version-one `S64P` roots.  Root, section, canonical dependency-set, and
  external payload hashes now fail closed before placement; feature-inactive
  payloads remain validated without consuming residency.  Root plus active
  payloads commit atomically, old generations cannot be evicted until their
  render, bank, and voice consumers retire, and immutable render snapshots
  carry only scalar package/bank identities.  Available CART capacity is
  injected explicitly so the existing native-pointer `SOURCE.DAT` prefix is
  never mistaken for free memory, and sourceboot rejects provisional roots.

- Added the generic, versioned, big-endian `S64P` scene-root compiler,
  validator, and C ABI emitter.  Roots now bind all eight closed section kinds,
  sorted content-addressed actor/animation/audio descriptors, package and
  dependency-set hashes, lifetimes, destinations, dependency masks, alignment,
  and scratch/budget claims.  The first BOB area-1 artifact is deliberately
  marked provisional and rejected by normal validation, so it can exercise
  deterministic world/collision/sky/BSP packing without inventing unfinished
  feature payload hashes or being mistaken for Task 22's final root.  External
  dependency edges are expressed as stable-ID references and normalized to
  canonical descriptor ordinals, preventing shuffled compiler inputs from
  silently changing masks; generated scenes include one shared guarded ABI
  header rather than redeclaring package types per root.

- Closed the last Task 3 fail-open scanner paths after final rereview.  Every
  indexed symbol reached through native functions, data, action tables, or
  function-pointer tables must now resolve uniquely, rather than applying the
  ambiguity check only to direct-call syntax.  Behavior audio now follows a
  bounded, sink-directed value flow through direct sound APIs, local aliases,
  forwarding-wrapper parameters, and reached sound tables; passing a
  `SOUND_*` value to an unrelated call no longer invents an SFX dependency.
  This preserves the 86-record / 133-source BOB closure while narrowing its
  audio union to the 54 source-proven IDs actually capable of reaching a sink.

- Closed the final Task 3 scene-closure provenance bypasses.  The bounded
  native index now resolves callbacks and reachable helpers across canonical
  repository `src` definitions (while excluding mutually exclusive port
  overlay stubs), and missing callback definitions fail before output.
  Behavior records retain every concrete model/geo variant with exact
  model-to-geo binding provenance, every spawned child has one complete type,
  and schema validation resolves the cited behavior/model/geo/animation
  symbols rather than trusting path membership.  Audio IDs now come from
  concrete call arguments, local value flow, and reached data definitions, so
  generic-helper comparison constants cannot leak into a behavior.  Per-site
  BehaviorScript recurrence replaces block-wide capacity inference, BOB's
  Goomba triplet bound is backed by explicit state/child-deletion attestations,
  and entry `JUMP_LINK`s are expanded with comments removed before area/music
  selection.  Consumers now receive both White Puff bubble and mist variants;
  the authoritative BOB host closure remains 86 behaviors and grows from 127
  to 133 hash-covered sources because the additional binding evidence is
  explicit.

- Repaired the scene-closure collector's repository boundary: native callback,
  helper, respawner, particle, sound-spawner, and loot/triangle-effect routes
  are now walked across bounded `src/game` sources, with every reached source
  hashed and every unresolved dynamic creation rejected.  Recursive
  `JUMP_LINK` evaluation now separates area-local objects/music from global
  model loads, resolves both geo and display-list model roots plus animation
  tables, and records schema-checked root provenance.  Live counts no longer
  treat a recurrent spawn burst as total capacity: tighter source-proven
  active-set/one-shot bounds are retained (including BOB's 11 Goombas), while
  recurrent paths without a provable cadence/lifetime use the explicit
  `OBJECT_POOL_CAPACITY` ceiling and fail if that source cap is unavailable.
  This closes omitted BOB respawner and mist/white-puff paths and expands the
  authoritative host closure from 76/78 to 86 records / 127 source hashes.

- Closed the remaining scene-closure rereview gaps by making behavior-spawn
  rules repository-relative, hash-covered source attestations of their native
  owners, exact model/behavior sites, and bounded capacity expressions.  Rule
  routes now traverse local helpers, action/data tables, and audited generic
  dispatch bridges, rejecting duplicate owners/edges, unrelated sources,
  nonexistent sites, and invented counts; this corrected stale water-bomb,
  explosion, Koopa-shell, coin-helper, default-star, and wooden-post edges.
  Audio dependency collection now follows only each behavior's reachable
  native functions/data and resolves every reached `SOUND_*` identifier to one
  declared `SOUND_ARG_LOAD(SOUND_BANK_*)` entry, hashing `include/sounds.h` and
  failing on missing or ambiguous declarations.  The stricter BOB result is 76
  records / 78 source hashes, preventing unrelated sounds from leaking out of
  a shared source file while preserving fail-closed helper/area/geo/schema and
  canonical-byte guarantees.

- Scene closure now walks bounded source-defined native helper chains from
  BehaviorScript callbacks, rejecting unknown computed spawn arguments before
  output. Recognized grill-table expansion remains explicit, while newly
  discovered concrete coin/star helper effects must be reviewed as normal
  source-attested edges.

- Scope LevelScript inline object discovery to the requested `AREA`, while
  retaining only that area's linked local scripts, so objects from another
  area cannot inflate a scene package's dependency or capacity closure.

- Extended BOB's reviewed native closure to include Koopa-shell wave, droplet,
  flame, and sparkle chains plus exclamation-box computed cap/star/marker
  contents. This prevents those visible effects and rewards from being omitted
  merely because their native spawn calls are reached through helpers or a
  source-owned contents table.

- Hardened scene-closure derivation after review: every reachable native
  `spawn_object*` edge now requires a source-attested, unique reviewed rule or
  generation fails. The BOB closure consequently includes model-less
  controller products (checkerboard platforms, grill halves, cannon opening,
  hidden pole 1-Up triggers) and transitive explosion/sparkle effects that the
  initial inventory omitted. Capacity is now evaluated per compatible act,
  reachable spawn cycles fail explicitly, and SFX banks derive from source
  identifiers rather than a blanket `general` label.

- Added a deterministic, generic scene-closure generator and its versioned
  schema. It follows LevelScript declarations, macro presets, model/geo
  bindings, BehaviorScript children, and explicitly reviewed native computed
  spawn rules, with source hashes and fail-closed validation. BOB's former
  hand-maintained Goomba count is now one generated capacity fact (11), so
  model-less controllers, rewards, effects, and cyclic behavior graphs cannot
  be silently excluded from later actor, animation, or audio package work.

- Closed the first independent-review gaps in the sourceboot build identity and
  historical A9A archive. The effective-config digest and identity-derived
  output label now bind every remaining compiler-affecting wrapper control,
  including atan2, demo/camera/slave-render, flat-fragment, trace, and
  diagnostic geo-walk switches, preventing distinct binaries from sharing one
  claimed identity. Baseline archival now requires the exact accepted cadence
  and target records, the hash and parsed 32-Mbit DRAM content of the actual
  Ymir profile, and the exact successful matching launch report and logs. It
  also rejects a conflicting manifest before creating or copying archive
  files, so a failed archive attempt cannot leave a partial accepted trio.

- Bound sourceboot diagnostic artifacts to a fixed-width, versioned target
  identity containing the complete feature tuple, behavior configuration, and
  hash-verified source/route/input/camera/cart/scene/actor/animation/audio
  inputs. Capture now resolves the identity from the exact ELF, checks the
  loaded target bytes before telemetry, and derives labels from those compiled
  bytes, closing the prior gap where a directory label could describe
  behavior or packages the ELF did not contain. The accepted 5.294 FPS A9A
  ELF/ISO/CUE is separately archived after exact hash, CUE-reference, profile,
  config, capture, and preserved-commit ancestry checks so later feature-off
  descendants cannot silently replace the historical rollback baseline.

- Reconciled the SH-2 native-math verifier with the live descriptor-queue
  renderer route. The pinned oracle now follows the lifecycle and master/slave
  callback tables instead of removed frame/terrain-worker symbols, and every
  oracle identity must own an unambiguous linked disassembly block. Sourceboot
  may suppress only the two exact BOB camera-trigger calls proven unreachable
  through the null camera-table guard; route, table, control-flow, third-call,
  or owned-block drift remains fail-closed. The proof is bound to the reviewed
  source-file identities and exact linked-ELF SHA-256, rejects mutation of the
  BOB level value before current-level publication, derives callbacks only from
  the table returned into runtime activation, and validates start/poll ownership
  inside those root function bodies rather than accepting unrelated decoys.

- Hardened the Task 10 sourceboot pipeline selector: `SATURN_RENDERER_PIPELINE`
  now accepts only the reviewed `2`, `3`, and `4` variants and is passed into
  the SH-2 preprocessor flags. Previously `-pipeN` changed only the output
  directory, allowing an artifact label to claim a pipeline that the compiler
  never selected; the new source contract catches that drift before a target
  build.

- Corrected the sourceboot memory-map verifier to require `.uncached` to be
  initialized `PROGBITS`, matching the pinned Yaul ELF contract. The section
  contains the slave SH-2 entry and executable cache-through helpers, so the
  previous `NOBITS` requirement would approve an image that omitted required
  bytes and reject the real linked artifact. The P2 address, physical HWRAM
  end, margin, and LWRAM checks remain fail-closed and unchanged.

- Repaired the A9A target's HWRAM boot boundary after retries against the
  unchanged reviewed ELF failed target identity at both 600 and 4,096 startup
  VBlanks. Its exact map placed `___end` at `0x061040D0`, `0x40D0` bytes past
  physical HWRAM, because the bulk primitive-tier and cluster-LOD arrays had
  been moved into P2 `.uncached`; the linker margin subtraction wrapped and
  did not reject the image. Those arrays now share one CPU-only LWRAM object
  reached through one canonical P2 alias on both SH-2s, while the small
  exact-generation lifetime record remains uncached. Linker and ELF gates now
  reject HWRAM/LWRAM upper-bound overflow before subtracting their required
  margins, including the route-0 LWRAM floor. This is a source repair only:
  independent review, a fresh serialized target build, repaired-image boot,
  P2/map evidence, capture, and FPS remain open.

- Repaired the A9A Step 11 throughput observer after the sole target build
  exposed an intentional runtime-layout evolution. The capture now recognizes
  exactly two source-validated SH-2 layouts: the reachable 92-byte legacy
  runtime with telemetry at byte 28 and the reviewed 104-byte marker-enabled
  runtime with telemetry at byte 40. It reads the resolved symbol size and
  decodes every sequence/counter relative to that layout; nearby or unknown
  sizes still fail closed. Git history retains the initial pre-Ymir observer-
  contract failure; the canonical report path was later updated by the
  authorized unchanged-target identity retry documented above. No target
  rebuild, Ymir launch, capture retry, or FPS claim accompanied the observer
  repair itself.

- Closed the second A9A review-fix source round by moving every LOD lifetime
  object read by either SH-2 into the linker-owned P2 `.uncached` partition.
  Runtime marker clocks now stamp the real notify and positive-retirement
  release sites, and publish the phase record before waking the slave or
  exposing retirement so neither CPU can observe a half-published boundary.
  The production-linked integration gate combines a deferred scene reset with
  terminal quarantine, proves reset happens only after exact-generation
  finish, asserts nonzero `QQ`, and catches late-marker and ignored-generation
  mutations. Target/Ymir evidence remains open pending two-stage rereview.

- Hardened the A9A frame-lifetime split after independent review. A renderer-
  owned generation gate now defers source scene/LOD resets until the active
  slave generation retires, lifecycle observers timestamp the actual notify
  and retirement publications, and sourceboot attributes complete master
  construction from first service through final lowering while retaining
  master finalization as a reported subset. Terminal queue telemetry is
  refreshed before reset so failed generations publish their real quarantine
  count. A production-linked host harness covers the N/N+1 transition and
  catches all four regressions. Historical cadence v1 compatibility is
  explicitly limited to direct or saved 60-byte buffers; live target capture
  continues to require the current v2 76-byte symbol.

- Split sourceboot's accepted demo renderer into exact-generation start and
  poll/finalize phases so slave construction for frame `N` can remain active
  while the master executes the single queued source tick for `N+1`. The
  retained snapshot and BUILDING bank now survive PENDING; positive slave
  retirement permits one master drain/merge/Gouraud/VDP1 lowering pass, while
  failure quarantines without serial replay. A8 remains the sole transfer and
  resident-list owner, so this changes CPU construction lifetime without
  moving presentation or VRAM ownership.
- Extended the cache-through cadence record from version 1/60 bytes to version
  2/76 bytes with separate slave-work overlap and master-finalization counters.
  Historical v1 direct/saved buffers remain decodable; live target observation
  requires v2. Version 2 reports the slave interval as a non-additive overlap
  window so phase attribution cannot double-count source work that ran
  concurrently.

- VDP2 composition now consumes the immutable camera carried by the displayed
  VDP1 bank together with explicit displayed/rendered/simulation generation
  metadata. The HUD labels that tuple, and a mismatched camera or render bank
  is rejected before VDP2 side effects; a tuple change also refreshes the HUD
  immediately instead of waiting for the normal metric interval. Bounded
  simulation lead therefore cannot silently mix sky or telemetry with an
  older framebuffer.

- Added the hardware-free A9 frame scheduler model with a presentation-scoped
  two-tick simulation budget, explicit generation-matched render/transfer
  completion, wrap-safe generation validity, bounded once-per-field
  service/poll actions, two-phase target publication acknowledgement,
  previous-frame reuse, and dropped-credit telemetry. Mutation gates reject
  four-tick catch-up, repeated-observation credit, and incomplete publication.
- Replaced sourceboot's per-outer-loop simulation catch-up with the reviewed
  six-action frame adapter. Authoritative game logic remains 30 Hz, useful
  render/transfer/presentation service remains field-rate, successful hardware
  publication is acknowledged before VDP2/cadence evidence, and generation
  zero stays reserved across scheduler, snapshots, and VDP1 banks. Existing
  `vblank_credit` telemetry names remain stable, but now report discarded whole
  30 Hz tick credits rather than raw fields.
- Corrected throughput reporting to derive FPS from the cadence trace's real
  ISR VBlank clock after simulation and presentation generations were
  decoupled. This prevents a false 60-FPS report; the exact A9 adapter capture
  measures 4.463 FPS mean, a 2.752x improvement over its pinned baseline.

- Added a 60-byte cache-through A9 cadence trace and exact-capture decoding for
  wrap-safe VBlank crossings in simulation, synchronous frame construction,
  and transport/presentation. This replaces misleading absolute claims from the
  16-bit FRT accumulators while leaving scheduler behavior unchanged.

- Extended the exact-identity sourceboot throughput capture with a configurable
  presentation-event depth and bounded final diagnostics on cadence failure.
  This replaces one-interval A8 guesses with a repeatable multi-frame sample
  while retaining queue/runtime evidence when a slow target misses the bound.

- Fixed A8's first live publication failure when Mario is fully culled by
  meshlet admission. Zero admitted actor positions now publish a terrain-only
  two-job graph instead of manufacturing invalid zero-length actor jobs and
  permanently quarantining both VDP1 source banks. Visible actors retain the
  four-job terrain-plus-actor graph and the same dependency checks. Actor
  preparation now reports success separately from its admitted count so an
  invalid pose or meshlet failure still fails closed rather than masquerading
  as successful culling.

- Fixed A8's target-only VDP1 transfer-descriptor initialization by explicitly
  converting Yaul's integer VRAM address to the descriptor pointer type. Host
  mocks exposed the address as a pointer and therefore missed the SH-2 compile
  failure; a source contract now guards the target-safe conversion.

- Replaced sourceboot's per-emitter blocking Gouraud transfer and CPU command
  upload with an A8 two-phase frame-bank transport. After the prior VDP1 list
  is overwrite-safe, command and Gouraud descriptors commit atomically to one
  serial queue, use completion-interrupt-owned CPU-DMAC channel 0 and guarded
  SCU-DMA level 0, and retire before one master-owned resident-list
  arm/publication. Stale iterations service both serial stages without one
  stage per VBlank; published banks carry immutable VDP2 camera state; and a
  partial resident-VRAM failure disables plotting instead of reusing old
  metadata. Full declared destination ranges and Gouraud alignment are checked.
  Exact per-ticket failures drain their accepted sibling before quarantine,
  preserving the prior publication. This removes the immediate transport wait
  from the accepted frame path and reports zero at nonexistent wait sites;
  rereview and target/Ymir/FPS validation remain pending.

- Hardened A7 publication after review: wrap-safe ordering now quarantines a
  late completed bank instead of regressing the current publication; manager
  initialization rejects aliased, overlapping, or misaligned command/Gouraud
  storage; and both renderer paths return failure when Gouraud queue submission
  remains unavailable after one bounded drain/retry. Sourceboot therefore
  retains the prior complete frame instead of publishing commands whose
  Gouraud dependency was never submitted.

- Replaced sourceboot's ad-hoc VDP1 bank XOR with an explicit two-bank
  lifecycle covering construction, transfer obligations, publication,
  quarantine, and retirement. Only a renderer-confirmed complete frame may
  publish; failure retains the previous complete bank, and build, published,
  and displayed generations are tracked separately. This closes the stale or
  overwritten source-bank hazard required before deferred DMA. Transfers still
  complete synchronously in this slice, so it intentionally claims no FPS
  improvement; A8 will introduce asynchronous submission and polling.

- Added a bounded sourceboot throughput capture for the remaining A5.9 queue
  observation gate. It binds an explicit CUE/ELF/Ymir triple by hash, verifies
  a linked immutable ELF code window in the running target before sampling,
  reads runtime and queue records through P2, and fails closed unless terminal
  queue telemetry and two VDP2 presentation edges are coherent. This replaces
  unreliable manual HUD transcription without changing target code, queue
  policy, or the already measured 3--4 VDP1 FPS result.

- Added bounded automatic desktop-Ymir performance capture. The helper reads
  Ymir's native one-second window-title counters for VDP1 framebuffer swaps,
  VDP1 completed draw calls, VDP2 frames, GUI rate, and emulation speed,
  records every sample with exact CUE/ISO identity, and leaves the visible
  emulator open for manual testing. This removes OCR and manual title-bar
  transcription from FPS comparisons.

- Recorded the first desktop-Ymir result for the atomic shared-SH-2 renderer:
  it remains roughly 3–4 FPS, matching the prior A3+A4 candidate. The cutover
  is retained as a correctness/ownership foundation, but no performance gain
  is claimed; per-CPU phase claims and terminal waits are now the required
  evidence before further scheduler conclusions.

- Cut the accepted Saturn frame atomically from three fixed terrain/Mario
  joins to one four-phase dependency graph shared by both SH-2s. The renderer
  publishes self-contained terrain and live-pose Mario contexts before the
  first claim, lets master and slave steal eligible admit/lower work, waits
  for both terminal descriptors and positive slave callback retirement, then
  performs deterministic terrain/Mario assembly before the master alone
  lowers final VDP1 commands. Incomplete publication or execution preserves
  the prior complete command list without a serial full-frame replay. This is
  source-complete pending independent review; no new target build, CUE, Ymir,
  cache-behavior, or FPS evidence is claimed.

### Fixed

- Hardened A5.9 sourceboot queue capture against false-positive evidence. A
  terminal sequence reused across two presentation edges now fails the capture
  instead of being silently omitted; ELF identity bytes must come from an
  allocated `SHT_PROGBITS` section contained in `PT_LOAD`; and JSON-RPC
  notifications are independently count- and byte-bounded in reports. These
  changes preserve the fail-closed observation contract without touching the
  target or scheduler.

- Tightened that A5.9 ELF identity proof to require the section's virtual and
  file offsets to share the same affine mapping inside the selected `PT_LOAD`.
  Separate range containment could otherwise hash bytes at one file offset
  while probing a different loaded address; malformed inputs now fail closed.

- Added a separate bounded A5.9 sourceboot-load window before queue telemetry
  observation. After BIOS handoff the collector advances exactly one VBlank
  per identity retry and records its wait/attempt count, so a real CUE whose
  disc payload has not yet loaded does not consume the cadence budget or get
  mistaken for a wrong ELF. A never-matching image remains a failed
  target-identity report and no telemetry is read first.

- Restored the desktop launcher to the proven `ymir-agent/build-agent`
  executable and explicit `--profile`/`--disc` arguments. The prior default
  had drifted to `build-agent2`, which could launch without the intended disc
  or 32-Mbit RAM profile and made test sessions unreliable.

- Fixed A5.9 retirement telemetry publication so the slave writes its retired
  generation/sequence before releasing the positive retirement marker. The
  earlier order allowed the master to leave its wait and snapshot stale zero
  telemetry even though the callback had returned; a source-order mutation
  test now pins the SH-2 and host paths to release-marker-last ordering.

- Moved the two master-only terrain merge streams from HWRAM into the existing
  LWRAM work arena. Activating the reviewed four-phase queue made its callback
  graph reachable and exposed a 10,032-byte HWRAM link overflow; retaining
  these 27,744 bytes in scarce HWRAM provided no cross-CPU or VDP ownership
  benefit. Final sorting and VDP1 lowering remain master-owned.

- Fixed two target-blocking terrain handoff defects found in the independent
  A5.8 cutover review. The single WORLD_ADMIT producer no longer inherits the
  legacy two-lane rendezvous, and WORLD_LOWER rebuilds its local owner map from
  the exact DONE admit claimant before choosing cached versus P2 position
  payloads. The sole admit producer transforms the complete visible set
  directly, so a slave claimant never rereads its freshly cached owner bytes
  through the obsolete producer-0 P2 alias. An executable two-generation
  callback fixture poisons prior owner state and proves both slave-to-master
  and master-to-slave handoffs.

- Fixed the A5.8 render-job queue's SH-2 include boundary. The first guarded
  serial sourceboot target build exposed that `CPU_CACHE_THROUGH` was used
  without importing Yaul's cache definition; the queue now includes the
  narrow target cache header while host builds remain independent of Yaul.
  This restores target compilation without activating the dormant CPU-DUAL
  queue or changing the accepted renderer path.

### Added

- Added bounded A5.9 dual-SH-2 scheduling telemetry after the atomic queue
  produced no visible FPS uplift. The VDP2 HUD and append-only profile now
  expose master/slave claims for each world/actor admit/lower phase, exact
  notified/retired generation, master retirement-wait iterations, failures,
  and quarantines. A delayed-slave host schedule proves the current coarse
  graph permits the master to consume all four jobs before the slave runs;
  this is diagnostic evidence only and does not change scheduling policy.

- Added the dormant A5.8 ordered terrain-command and callback-context
  contracts. Final terrain sorting now retains each descriptor-local command
  image alongside its result without growing the eight-byte SH-2 reference;
  pointer-free P2 release records bind terrain and Mario callback snapshots to
  exact generation, phase, byte bound, producer lane, and claimant identity.
  Every phase-specific API rejects corrupt, stale, incomplete, wrong-phase,
  wrong-claim, out-of-range, and cross-lane host cases. Queue snapshots are
  self-contained: Mario copies dynamic compact
  refs inline and terrain copies the transform job/work order rather than
  following cached nested or stack pointers. This does not activate CPU-DUAL
  or change the accepted live renderer.

- Added the isolated standalone Saturn PCM68K audibility candidate. The
  source-built 68K now programs four bounded SCSP PCM8 slots using attributed
  PoneSound register/pitch patterns; a deterministic 4,408-byte CC0 proof bank
  and Yaul soundtest exercise SNDOFF/copy/SNDON, heartbeat validation, bounded
  enqueue, controller-triggered play/stop/volume, and visible telemetry. This
  remains outside sourceboot and does not change renderer scheduling or the
  accepted FPS comparison image. The owner manually confirmed audible A/B/C
  playback and X stop behavior in desktop Ymir; automated telemetry/cost
  evidence and sourceboot promotion remain separate open gates.

- Added payload-kind-aware output-span validation to the dormant A5.8 render
  queue. WORLD_ADMIT positions, WORLD_LOWER records/commands, ACTOR_ADMIT
  projected vertices, and ACTOR_LOWER primitive references may reuse their
  own bounded bank-local offsets, while overlap within one physical payload
  kind and unknown or mismatched type/callback pairs fail before publication.
  The descriptor remains pointer-free and 16 bytes; this removes a combined
  graph activation blocker without activating CPU-DUAL or changing the live
  renderer.

- Added the dormant Mario half of the A5.8 descriptor-owned render queue.
  ACTOR_ADMIT now has a claimant-selected transform payload and ACTOR_LOWER
  requires its exact completed predecessor before classification; terminal
  metadata and ordered master assembly validate the complete pose/ref payload
  before restoring the Castle-proven animation emission banks. This remains
  source-only: no CPU-DUAL callback or default renderer path changed, and
  combined activation still requires output-namespace review, ordered terrain
  command lookup, callback-context publication, and target/cache evidence.

- Added an isolated bounded SH-2-to-68K PCM command path. The pointer-free,
  big-endian ring rejects corrupt indices, invalid commands, and full queues
  without spinning; the 68K consumes at most eight commands per heartbeat,
  maintains four deterministic round-robin voice states, and publishes command
  telemetry. Three generated-proof sample records are fixed and bounded, but
  no PCM bytes or SCSP register writes exist yet, so this is not an audibility
  claim and does not alter sourceboot or renderer scheduling.

- Added the isolated, source-built fixed-address MC68000 heartbeat image for the
  Saturn audio prototype. Its byte-addressed mailbox now publishes protocol
  identity, BOOTING/READY state, and a wrapping heartbeat; host tests execute
  those transitions and an ELF/map verifier rejects bad entry/reset vectors,
  nonzero images, reserved-stack, 16 KiB, mailbox/bank overlap, and unresolved
  symbols. The minimal vector/linker
  shape is an attributed MIT close-port from pinned PoneSound. No SCSP slot,
  sourceboot, renderer, target build, or Ymir image changes in this increment;
  the approved exact-path `m68k-elf` GCC 11.1.0 bundle now produces a verified
  deterministic 1,190-byte BIN. Its stripped target headers are replaced only
  inside the freestanding audio68K build by GCC target-width typedefs; generated
  ELF/BIN/MAP files and tool binaries remain uncommitted.

- Added a dormant descriptor-indexed terrain merge assembler. It accepts only
  completed WORLD_LOWER outputs whose P2 metadata, claimant lane, generation,
  count, and sequence agree with the exact descriptor; it validates every
  result identity before the master performs its existing stable depth order.
  Queue streams are not coerced back into the legacy master/slave arenas, so
  later work stealing cannot silently select a fixed range. The live renderer
  remains legacy until Mario reaches the same contract, leaving the 3–4 FPS
  rollback candidate unchanged. A generation-current descriptor accessor now
  makes that assembler fail closed if any published WORLD_LOWER is READY or
  claimed rather than silently omitting it. The executable graph contract also
  preserves the valid all-culled case: once every lower job is DONE, zero
  result records form a successful empty stable merge.

- Added a source-provenanced animated-actor generalization spike selecting
  Goomba as the first non-Mario proof. It records the real BOB instance budget,
  source model/geo/animation/behavior inputs, required billboard/alpha/shadow
  treatment, and the descriptor-owned queue contract so future enemy support
  can reuse the Mario actor path without silently inventing a BOB-only or
  fixed-split renderer. A host-only inventory fixture keeps those assumptions
  explicit; this does not activate enemy rendering or change the 3–4 FPS
  rollback baseline.

- Hardened dormant A5.8 terrain lowering with a callback-side graph proof.
  WORLD_LOWER now requires exactly one immutable, completed WORLD_ADMIT
  predecessor and validates that predecessor's P2 publication before it reads
  transformed positions. This closes the malformed/unready dependency gap
  found in review without activating the queue or changing target behavior.

- Added the next dormant A5.8 terrain queue foundation: WORLD_ADMIT now
  publishes transformed-position completion through a descriptor-indexed,
  P2-visible release record, while WORLD_LOWER records its exact result count,
  sequence, claimant state, and writer lane before runtime may mark it DONE.
  Terminal merge now derives those values from the completed descriptor rather
  than accepting a caller-supplied count. The legacy worker remains the active
  renderer, so this intentionally changes no target behavior or FPS result.

- Added the A5.8 dormant terrain queue producer/reader seam. A WORLD_LOWER
  callback now passes its exact descriptor span and actual claimant lane into
  the common transform/classify/compact producer, seals its descriptor-owned
  result arena before graph runtime may publish `DONE`, and exposes a terminal
  reader that derives record and command aliases from that exact job identity.
  The fixed-split legacy wrapper remains the default path because Mario and
  persistent per-job merge counts are not yet migrated; this intentionally
  changes no target behavior or FPS result.

- Added a maintained roadmap and reconciled state/plan status with the
  reviewed A5 ownership bridge and graph-aware runtime. The next accepted
  milestone is now explicitly the atomic terrain/Mario renderer conversion;
  the 3–4 FPS A3+A4 Ymir build remains its rollback baseline until a reviewed
  target replacement exists.

- Clarified the full-game renderer roadmap: the legacy Castle demo already
  proved source-driven full Mario animation, so the Saturn path preserves and
  optimizes that bridge rather than rebuilding animation. Future enemies use
  the same animated-actor renderer contract, with additional per-family asset
  and feature coverage. The plan now distinguishes its committed coarse
  BSP/frustum/portal-window admission from a future, evidence-driven general
  occlusion/PVS extension.

- Added the first A5.8 terrain descriptor-binding seam and an executable C
  live-cutover contract. A future WORLD callback now proves its exact claimed
  queue descriptor before deriving terrain record/command addresses from the
  recorded output span and claimant lane; it cannot choose storage from a
  range begin or fixed split. The default renderer still uses the accepted
  legacy worker until terrain and Mario callbacks can be converted together,
  so this changes no target behavior or FPS result. The old Python-only
  source check is replaced by a host-compiled test because the configured
  Windows Python launcher is unavailable in this workspace.

- Added A5.8.1 graph-aware render-job runtime drains. The sole eventual
  CPU-DUAL owner now has an activation path that claims only graph-eligible
  descriptors, so a published consumer cannot run ahead of its required
  producer merely because it appears earlier in queue storage. The focused
  host contract deliberately publishes `WORLD_LOWER` before its `WORLD_ADMIT`
  producer and proves the producer still runs first. Renderer payload routing
  and live activation remain pending; no target/FPS claim changes.

- Added the A5.7 render-job graph foundation: P2-visible dependency masks stop
  consumers claiming before every producer is `DONE`, failed producers
  quarantine only ready dependents, and terrain multi-result consumers use
  exact `(job_index, output_index)` identities. This supplies the missing
  phase boundary discovered in A5.6 preflight without activating a second
  CPU-DUAL callback or changing the accepted A3+A4 renderer path.

- Added descriptor-indexed physical payload-bank helpers for A5.6. A claimed
  job selects master or slave storage from its recorded queue claimant and
  exact output span, while a reader refuses non-terminal/mismatched metadata
  and uses the bridge's P2 policy. This is the payload migration primitive for
  terrain and actor banks; the live fixed-split renderer is not yet switched.

- Added the source-only A5.6 render-job runtime lifecycle. It holds one
  queue/callback-table/context owner and, on SH-2 only, installs the sole
  polling CPU-DUAL entry; publication remains separate and host coverage
  proves the slave drains an exact claimed descriptor to terminal state. The
  live renderer is intentionally not bound yet because its fixed physical
  payload partitions still need descriptor-owned migration, preserving the
  accepted A3/A4 candidate as rollback baseline.

- Added the A5.5 descriptor-to-result bridge for the future opportunistic
  renderer. It routes result reads and writes using the exact queue descriptor
  index and actual master/slave claim, and rejects readers before that job is
  `DONE`, so a work-stealing master cannot accidentally select the slave cache
  alias. The queue now exposes source-side arming only—not a polling callback
  attachment or notification—and cannot activate a slave until the atomic
  renderer cutover replaces the legacy worker.

- Added descriptor-owned terrain and actor output-bank publication for the A5
  SH-2 work queue. The CPU that actually claims a descriptor now publishes its
  output lane through an atomic P2-visible release record, so an opportunistic
  master steal cannot read its own cached work through the slave alias. This is
  a source-only prerequisite: the accepted renderer still uses its existing
  fixed worker while live queue integration, master VDP1 ordering validation,
  and target evidence remain pending.

- Extended the source-only A5 queue contract with master/slave polling drains
  and a local callback table. Descriptors still carry only callback
  IDs; resolution happens after an exact-once claim and exposes the claiming
  CPU to the callback, which is required before a future work-stealing render
  path can publish cache-correct output ownership. The host fixture now proves
  the slave drains all callback IDs without per-job function pointers; target
  scheduling and FPS behavior remain unchanged until the existing fixed-lane
  renderer is converted to descriptor-owned output banks.

- Added the source-only A5 immutable render-job queue contract: fixed-width,
  pointer-free terrain/actor descriptor records publish through cache-through
  release words; master and slave claims are exact-once and terminal work alone
  can retire a generation. This establishes an auditable queue boundary before
  the active A3/A4 renderer candidate is rewired, avoiding a scheduler change
  that would obscure its pending review and target evidence.

- Added generated, bounded Mario actor meshlets (at most 32 primitives each)
  with material/opacity partitions, source ordinals, tight bounds, and compact
  near/mid/far primitive and position remaps. The serial master path now
  rejects behind meshlets before actor transform dispatch, transforms each
  admitted position once, preserves opaque source order, and
  puts textured/translucent work into stable fixed far-to-near depth bins;
  this replaces the quadratic actor insertion sort without moving camera,
  material, Gouraud, texture-slot, terrain-relative insertion, or VDP1
  ownership away from the master. Target visual/counter evidence and
  independent reviews remain required.

- Added an isolated, pointer-free SH-2/68K PCM wire-protocol foundation for
  the future standalone soundtest. The fixed big-endian mailbox/ring map and
  host contract prevent separately built CPUs from exchanging compiler-layout
  dependent structures, while leaving sourceboot, SCSP, the render queue, and
  the accepted FPS candidate unchanged.

### Fixed

- Corrected the dormant A5.8 terrain queue route so classification receives
  the actual claimant/execution lane explicitly. A legal slave claim whose
  descriptor begins at input offset zero can no longer be mistaken for master
  work by a `begin == 0` rule; that legacy-only inference remains confined to
  the fixed worker adapter. This is source-only and does not activate the
  queue or alter target behavior.

- Hardened A5.8.1 graph-runtime descriptor access after review. A claimed job
  is now fetched through a P2/cache-through accessor that revalidates the
  exact claimant state; the runtime no longer raw-dereferences its cached
  queue owner after a graph claim. A static mutation guard protects this
  boundary. This remains source-only and does not activate the renderer.

- Hardened the A5.7 job graph after review: independent READY work is no
  longer mistaken for a blocked dependent, cyclic/self dependency masks fail
  before queue publication, and failed-producer quarantine reaches every
  reverse-chain ready dependent before a generation can merge or reset.

- Corrected the A5.6 queue runtime to read its shared generation through the
  queue's SH-2 cache-through accessor. The slave poll can no longer observe a
  stale P1 queue header before deciding whether to drain work; a source gate
  rejects direct runtime generation dereferences while preserving host tests.

- Removed A5.5's premature Yaul CPU-DUAL callback registration and notify.
  The source-only ownership bridge now reserves and validates its one-owner
  lifecycle without compiling a second callback beside the legacy fixed-split
  worker. The later atomic renderer cutover must remove that worker before it
  binds the queue to CPU-DUAL.

- Renamed A5.5's misleading slave attach/notify API to explicit source arming.
  This prevents callers from treating the unbound bridge as an active worker;
  the static source gate now scans both queue and bridge sources for CPU-DUAL
  activation.

- Hardened A5 output-bank publication to bind each published cache lane to the
  actual queue release record, rather than trusting a callback-supplied claim
  value. Forged publication before a queue claim now fails closed, while an
  actual master or slave claim remains the sole source of output ownership;
  this preserves P2 peer reads before live queue wiring reaches the renderer.

- Corrected Mario meshlet admission to project each meshlet's live animation
  pose after Mario yaw, rather than using its neutral-pose AABB centre. Whole
  meshlets now cull only when their furthest live extent is behind the view
  plane, choose LOD from their nearest live extent, and bin translucent work by
  its furthest live extent. The generated compact position streams now provide
  the globally deduplicated transform references directly, so telemetry matches
  actual transform work and walking/animated poses cannot be rejected from
  stale neutral bounds.

- Added the first A3 scene-neutral render-cluster contract and a focused host
  gate. It chooses a hysteretic near/mid/far compact position span from a
  cluster AABB before transforms, rejects empty/behind optional spans, and
  carries only generated offsets and snapshot generation. The BOB generator
  now also emits deterministic compact unique position-reference streams for
  each LOD tier, so the remaining fragment-bank integration can replace its
  full-position marking without changing ownership or presentation logic.

- Extended A3's generated compact position streams to the active BSP-fragment
  bank and the Mario actor bank. The terrain renderer now admits its selected
  build tier before transform, filters the matching far primitives while
  retaining the mandatory route prefix, and marks only that immutable tier
  span instead of expanding every accepted primitive's four corners. This
  preserves the existing post-transform projected/near tests while exposing
  cluster and position admission/transform counters; target performance and
  visual evidence are still outstanding.

- Tightened Mario's generated far-tier policy to retain one deterministic
  source-primitive residue, rather than merely omitting one. The checked-in
  neutral pose now carries 228 far references versus 424 near/mid references,
  so the actor stream is a real compact future-workload input rather than a
  nominal tier with the same unique positions.

- Wired Mario's selected compact tier references into the actor transform
  dispatch. Worker ranges now index the immutable reference span and retain an
  explicit original-vertex ownership map for cache-through reads, so FAR
  transforms 228 selected vertices without classifying untransformed vertices
  as valid. Added deterministic BOB and BSP-fragment cluster metadata plus
  host gates for tight bounds, single material identity, mandatory retention,
  in-range/unique tier references, and a strictly smaller FAR stream.

- Routed accepted terrain work through the scene-neutral render-cluster
  contract before transforms. Generated BOB and fragment banks now provide
  Q16 tight bounds and exact per-cluster tier spans; the runtime derives a
  camera view, maintains per-cluster hysteresis (reset at scene changes),
  admits only validated clusters, and marks exactly their returned references.
  This replaces the former global tier span while retaining the existing
  post-transform projected, near, material, and capacity tests.

- Added Saturn-only immutable two-slot render snapshot banks with fixed-width
  camera, scene, Mario, pose-selector, and generated-bank-ID records. The
  master publishes a generation only after camera and actor records agree;
  illegal lifecycle transitions, stale acquires, double acquires, and timed-out
  quarantined slots fail closed so later pre-transform admission cannot mix
  live game state or reuse an unsafe bank.

### Fixed

- Moved A3's bulk per-cluster LOD and admitted-result scratch from HWRAM BSS
  into the linker-owned CPU-only LWRAM section. The initial target A3 link
  exceeded HWRAM by 29,680 bytes; the route-0 sourceboot candidate now keeps
  its required 4 KiB libyaul heap floor while retaining the same admission
  behavior. This is a build-budget repair, not a measured FPS claim.

- Corrected A3 render-cluster header integration for sourceboot's declared
  include paths. The scene-neutral gfx header now reaches its isolated GPL
  promotion dependency relatively, and its host gate no longer supplies a
  hidden `gpl` include directory. This prevents target compilation from
  depending on a host-only include-path accident; target/Ymir evidence remains
  pending.

- Corrected A3 admission/transform generation agreement at 32-bit wrap. The
  renderer now derives one nonzero frame generation before either consumer,
  reuses it for admission and publication, and fails closed on a mismatched
  admission result. Focused host coverage includes the exact
  `UINT32_MAX -> 1` transition and hysteresis reset; target visual/counter
  evidence remains pending.

- Corrected A3 generic compact-cluster admission to derive conservative depth
  from the immutable Q16 camera-forward vector instead of world Z. This keeps
  yawed and pitched optional terrain from being rejected or assigned the wrong
  LOD tier, while preserving mandatory work and the exact selected compact
  position span before transform. Focused host fixtures now cover both rotated
  views; target visual and counter evidence remain pending.

- Corrected the Saturn terrain runtime-contract fixture to distinguish optional
  worker-owned post-light RGB1555 shades from immutable VDP1 material words.
  It now exercises both no-shades and live-shades publication through sorted
  master/slave spans. The compact writer now drops supplied shade values unless
  `POST_LIGHT_SHADES` is set, keeping ignored bytes zero while preserving the
  live four-word shade payload; this prevents material-like values leaking
  across the master/worker boundary under a clear flag.

- Serialized every A2 snapshot terminal transition through its release claim
  lock. A timeout quarantine can no longer be overwritten by a stale
  `READY → RENDERING` claimant; completion and positive retirement likewise
  revalidate their owned state before publishing, preserving fail-closed bank
  ownership when the two SH-2s contend.

- Hardened snapshot-bank recovery so public initialization touches only
  already-free slots: quarantined and in-flight generations remain terminal or
  owned until explicit lifecycle retirement. Release state and peer payload are
  now accessed through SH-2 P2 cache-through helpers (host identity aliases
  preserve fixture coverage), and callers can use one exported generation
  validator instead of duplicating mixed-frame checks.

- Made `READY → RENDERING` exclusive across both SH-2s. An uncached release
  lock now uses the SH-2 `tas.b` bus-atomic transition (with a host atomic
  equivalent), so only one renderer can claim a snapshot generation; the
  losing contender must observe no acquired payload.

- Corrected snapshot publication so producers clear, initialize, and return
  the bulk payload through its SH-2 P2 cache-through address before releasing
  `READY`. This prevents a clean release word from racing ahead of dirty P1
  cache lines; host identity aliases cover lifecycle behavior, while target
  cache visibility remains a separate evidence gate.

- Added an opt-in desktop Ymir launch helper that records the exact SDL3
  command, working directory, project profile, staged CUE, and CUE-referenced
  ISO identities before manual testing. It keeps the 32-Mbit DRAM cart
  profile-managed and writes durable stdout/stderr logs beside its timestamped
  JSON report. This prevents the short-lived launcher from closing the GUI's
  pipe handles after monitoring, while preserving later crash diagnostics
  without silently launching a different emulator mode or disc.

- The bounded sourceboot boot-trace reader now verifies a caller-selectable
  linked text probe (default `main`) at every BIOS/checkpoint and final
  sample through both P1 and P2, alongside raw master-SH-2 register evidence.
  It records the ELF-derived expected bytes and SHA-256 plus observed PC/SP,
  so a bad trace word cannot be interpreted before the loaded code identity is
  established.
- The sourceboot boot-trace reader now samples every checkpoint and final
  record through both the resolved P1 address and its SH-2 P2 cache-through
  alias, retaining each address, raw byte vector, and decoded words
  independently. This separates a cache/alias disagreement from a real target
  memory overwrite without changing the bounded capture cadence or legacy P1
  fields.
- The sourceboot boot-trace reader now accepts an optional positive
  `--post-bios-checkpoint-interval`. When set, it samples the raw trace after
  every bounded post-BIOS execution chunk, including the final remainder, so
  a single headless capture can locate the first sentinel mutation without
  changing the default one-sample capture behavior.
- The sourceboot boot-trace reader now records raw 32-byte samples at
  protocol-ready and each existing BIOS-handoff boundary, with accumulated
  emulated frames plus any stopped SH-2 PCs. This makes the first loss of the
  trace sentinel observable, instead of attributing an end-of-run bad word to
  the entire boot sequence.
- The sourceboot post-BIOS trace reader now binds each diagnostic launch to
  the CUE, its referenced ISO, and its CUE-local ELF, recording SHA-256,
  size, and timestamp identities for all three. It rejects stale ISO/ELF
  pairs and generic same-byte CUE wrappers before Ymir starts, so symbols from
  an unrelated ELF cannot be attributed to the loaded disc.
- Sourceboot's boot trace now seeds its magic and version in ELF `.data`, so
  a bounded debugger read can distinguish a wrong RAM address or mapping
  (all zeroes) from execution that never reached `user_init()` (valid header,
  stage 0) without changing runtime cache-through publication.
- Sourceboot's cache-through boot trace now publishes at `user_init()` entry
  and after VBlank callback registration, making an all-zero post-BIOS record
  distinguish a pre-main handoff failure from later scheduler or VDP work.
- Sourceboot debug builds now publish a low-cost, symbol-resolvable RAM trace
  across bootstrap, scheduler, VDP1, and VDP2 boundaries. The bounded headless
  Ymir reader reports its last stage and raw words after BIOS handoff, so the
  repeated post-BIOS freeze can be diagnosed without a GUI launch or a timing
  measurement.
- Default-off `diag-skip-geo` measures a risky duplicate geo-walk upper bound;
  it requires demo/replay and is not a full-game mode or promotion path.
- Declared Saturn-only build support so obsolete PC/N64 guidance cannot imply a
  supported configuration.

### Fixed

- Repaired sourceboot's fragment-mode compatibility defaults. Two deferred
  `?=` aliases could recurse when neither spelling was supplied, preventing
  route-0 Make parsing before compilation. The canonical fragment value is now
  resolved eagerly only when it is absent, while the legacy spelling remains a
  compatible default and existing invalid/mismatched-value checks remain in
  force. An isolated parser matrix covers default, both one-sided aliases,
  matching/mismatched inputs, and mutation back to the recursive form. This
  fixes a host build invocation boundary only; a linked map, target, Ymir/manual,
  and FPS evidence remain separate.

- Corrected Task 14 actor snapshot publication so capture writes the bulk
  payload through the existing SH-2 cache-through bank alias before the
  unchanged publish transition. Previously capture addressed the cached bank
  parameter while publish/acquire used the uncached alias, allowing READY to
  become observable before dirty payload bytes reached shared memory. The
  188-byte ABI, two-bank lifecycle, observer capacity/generation validation,
  quarantine behavior, and fixed actor-arena accounting are unchanged. The
  new exact acquire fixture and cached-destination mutation gate are host-only
  evidence; target cache-race, sourceboot, Ymir/manual, and FPS evidence remain
  open.

- Failed post-BIOS trace captures now still write a bounded JSON evidence
  report containing any raw `mem.peek` bytes/words, Ymir protocol
  notifications, and capped stderr before returning failure. This preserves
  the observable target state when a bad trace header or missing byte payload
  would previously discard the only crash-boundary evidence.
- The post-BIOS trace reader now resolves the target ELF through the audited
  DLL-safe MSYS wrapper and accepts the SH-ELF leading-underscore symbol ABI,
  preventing an otherwise valid trace capture from stopping before emulation.
- Post-BIOS trace writes now use the SH-2 cache-through alias and pin their
  eight-word ABI at 32 bytes, so Ymir and hardware debuggers read current
  backing-WRAM telemetry instead of dirty cached data.
- The post-BIOS trace reader now rejects zero frame requests before issuing a
  Ymir `exec.run_for` RPC, preventing an invalid diagnostic capture request.
- Restored the sourceboot startup VDP2 begin/commit retirement barrier before
  frontend and scheduler initialization. This drains the sky-DMA work queued
  by `user_init()` before the first paired VDP1/VDP2 presentation, avoiding a
  new post-BIOS hang path while retaining the one-VBlank presentation cadence.
- Fenced sourceboot presentation to one observed VBlank generation: elapsed
  credit is sampled only at outer-loop entry, recovery is capped at one extra
  simulation tick, and excess eligible credit is counted and dropped. This
  prevents slow rendering from refilling catch-up work and submitting multiple
  VDP1 plots for one displayed VDP2 field; VDP1 and geometry-free VDP2 now
  commit together at one terminal boundary.
- The `diag-skip-geo` host policy gate now executes the real Make validation
  matrix and inspects the actual normal compile-time branch, preventing inert
  comments or the wrong source region from satisfying diagnostic containment.
- `diag-skip-geo` now rejects observable whitespace-padded and malformed flag
  values before any prerequisite, compiler-flag, or output-tag decision, closing
  a spelling that could activate the unsafe diagnostic without demo/replay.
