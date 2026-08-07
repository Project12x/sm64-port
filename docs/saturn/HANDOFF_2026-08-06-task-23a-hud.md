# Handoff — Task 23A: Source-Semantic HUD on VDP2 (2026-08-06)

## Mission

Continue the full-game Saturn completeness sprint at **Task 23A: Deploy the
source-semantic gameplay HUD on VDP2**. This is a real implementation task,
not a diagnostic overlay or a BOB-only shortcut. BOB is the current proving
ground; the HUD backend must remain usable by the complete game and later
levels.

The governing implementation plan is:

`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md`

The plan, `STATE.md`, and `ROADMAP.md` now call this **named Task 23A**. The
planning change is committed at `6852d999` (`docs(saturn): add named VDP2 HUD
task`).

## Start here

Read these files before editing code:

1. `STATE.md`
2. `ROADMAP.md`
3. `ARCHITECTURE.md`
4. The Task 23A section in the governing plan
5. `.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md`
6. `src/game/hud.c`
7. `src/game/level_update.h`
8. `src/port/saturn/gfx/saturn_vdp2_frame.h`
9. `src/port/saturn/gfx/saturn_vdp2_frame.c`
10. `src/port/saturn/sourceboot/main.c`
11. `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`

The source HUD is authoritative. `render_hud()` and `gHudDisplay` define the
meaning of lives, coins, stars, power, camera status, timer, keys, and cannon
reticle state. The existing `sourceboot_vdp2_hud_write()` path is diagnostic
text (`dbgio`) only; it is not the in-game HUD and must not be mistaken for
Task 23A completion.

## Current checkpoint and boundaries

- Worktree: `sm64-port/.worktrees/sh2-native-math-purge`
- Branch: `sh2/native-math-purge`
- Accepted rollback: BOB, live input, Q16 camera, renderer Pipeline 4,
  profile-managed 32-Mbit DRAM cart, dual SH-2
- Accepted observed baseline: approximately 4–6 FPS; exact prior evidence is
  5.294 FPS mean. This task has **no new FPS threshold**.
- The full-game sprint has 30 named tasks. Tasks 22/23 final scene/audio
  closure, Task 23A HUD, and Tasks 24–29 remain separate gates.
- Do not replace the dual-SH2 path with a single-SH2 workaround.
- Do not change texture flags, shrink capacities, weaken linker margins, or
  bypass source semantics to make a screen appear.
- Do not claim target, Ymir, visual, audio, or FPS success from host tests.

The worktree is intentionally dirty with unrelated memory/exception,
sourceboot, renderer, and SDD evidence work. Inspect `git status` first and
stage only files owned by Task 23A. In particular, do not overwrite or bulk
stage the existing source-exception files, sourceboot memory edits, or other
audit artifacts.

## Task 23A contract

The implementation must:

- Preserve the source meanings of every required `gHudDisplay` flag and field:
  lives, coins, stars, power meter, camera status, timer, keys, and cannon
  reticle.
- Capture a fixed-width, pointer-free snapshot after the authoritative source
  tick and before the displayed frame publication.
- Carry the same displayed/rendered/simulation generation tuple used by VDP2
  composition. A stale or mismatched snapshot must fail closed rather than
  displaying mixed-frame values.
- Render through a bounded VDP2 NBG tilemap and resident glyph/icon atlas.
- Update only changed tile cells/spans; do not rebuild a full text surface on
  every frame.
- Keep all VDP2 writes within the existing VDP2 ownership/composition boundary.
- Keep `dbgio` as diagnostics only.

The implementation must **not**:

- Emit VDP1 commands or add HUD geometry to the VDP1 command banks.
- Read live gameplay globals from a VBlank callback.
- Change source tick cadence, input, camera, actor animation, audio, VDP1
  ownership, or frame-fence ownership.
- Introduce a second HUD semantic model that can drift from `render_hud()`.

## Planned files

The governing plan names these deliverables:

- Create `src/port/saturn/gfx/saturn_hud.h`
- Create `src/port/saturn/gfx/saturn_hud.c`
- Create `tools/saturn/test_saturn_hud.py`
- Create `docs/saturn/evidence/reports/saturn-hud-target-2026-08-06.md`
- Modify `src/port/saturn/gfx/saturn_vdp2_frame.h/.c`
- Modify `src/port/saturn/sourceboot/main.c`
- Modify the Saturn HUD asset-generation rule and `Makefile.saturn.mk`
- Modify `CHANGELOG.md` in the same commit as the behavior change

Keep the asset and tilemap layout Saturn-shaped: bounded resident data,
explicit palette/tile ownership, and no pointer-rich peer-visible state.

## Safe implementation order

1. **Inventory the source contract.** Enumerate all flag combinations and
   formatting/state transitions in `hud.c`; preserve hidden/default states,
   camera/timer/key/reticle behavior, and numeric formatting.
2. **Write RED host tests first.** Cover field/flag mapping, power transitions,
   dirty-cell diffs, stale/mismatched generations, fixed-buffer overflow,
   deterministic formatting, and a mutation proving that the HUD path cannot
   append VDP1 work.
3. **Define the snapshot.** Use fixed-width scalar fields, explicit reserved
   zeros, generation metadata, and bounded lengths. No pointers or source
   object addresses may cross into VDP2/interrupt-visible state.
4. **Implement the atlas/tilemap backend.** Generate or load the glyph/icon
   atlas once, initialize the NBG tilemap once, and publish only changed cells
   at the VDP2 commit boundary.
5. **Wire the authoritative handoff.** Capture after the source tick and feed
   the snapshot through the existing displayed-generation tuple. Leave source
   cadence and all renderer/audio owners unchanged.
6. **Run focused host gates.** Keep all failures and unimplemented layouts
   unchecked; a green formatter test is not target evidence.
7. **Review before target work.** Obtain the independent specification and
   quality review, then perform exactly one serialized `-j1` target candidate
   build with the DLL/MSYS preflight wrapper.
8. **Capture exact Ymir evidence.** Use the matching ELF/CUE, repository Ymir
   profile, and 32-Mbit DRAM cart. Verify that the running image is the newly
   built identity; never launch a stale image. Record visual HUD correctness,
   generation behavior, and any limitations. Do not add an FPS gate.
9. **Reconcile documentation immediately.** Mark each individual Task 23A
   step in the plan and SDD ledger as `complete`, `source-complete`, `active`,
   or `blocked`; record commit, review verdict, tests, target/Ymir result, and
   every remaining gate. Update `STATE.md`, `ROADMAP.md`, and `CHANGELOG.md`
   in the same transition where required.

## Build and runtime discipline

- Serialize all cross-toolchain builds and Ymir runs (`-j1`); the owner's CPU
  is busy and concurrent builds cause toolchain/DLL failures.
- Use the worktree-local MSYS home/temp and the repository's DLL-preflight
  launcher. Missing `msys-2.0.dll`, `msys-gcc_s-seh-1.dll`, `libgmp-10.dll`,
  `libmpfr-6.dll`, or `libisl-23.dll` is an environment failure, not a reason
  to alter the runtime implementation.
- Use the desktop Ymir invocation/profile path, not a headless-only command,
  for manual acceptance. The CUE must be loaded and the DRAM cart enabled.
- Pause between expensive build/capture phases when the host is overloaded,
  but keep the task/ledger live and report the exact stopping point.

## Acceptance checklist

Task 23A is not complete until the following are independently recorded:

- [ ] Source flag and field semantics match `gHudDisplay`/`render_hud()`.
- [ ] Host RED/mutation suite covers defaults, flags, formatting, dirty cells,
      generations, overflow, and no-VDP1 behavior.
- [ ] VDP2 atlas/tilemap initialization and bounded dirty updates are wired.
- [ ] Gameplay HUD is visible for lives, coins, stars, power, camera, timer,
      keys, and cannon reticle in BOB.
- [ ] Pause, dialog, and course-complete layouts use the same backend.
- [ ] Reviewed target candidate builds and links with exact identity.
- [ ] Profile-managed Ymir manual visual/controls acceptance is recorded.
- [ ] No target/Ymir/FPS claim is made for an unchecked or failed gate.
- [ ] Plan, SDD ledger, STATE/ROADMAP, evidence report, and CHANGELOG are
      reconciled before reporting completion.

## Handoff completion rule

If the HUD implementation is not fully target-accepted, leave Task 23A
unchecked and hand back the exact code commit, review status, tests run,
artifact identities, and remaining gate. Do not report “HUD complete” because
the diagnostic text seam or a host fixture works.
