# Saturn-Shaped Full Port — Program Charter

**Date:** 2026-08-14
**Owner goal (verbatim intent):** a Saturn-shaped port of SM64 with appropriate
degradation, audio, saves, 4 MB RAM cart support, and all of the levels; the
cart able to load any level; levels shrunk where needed; code written for the
dual SH-2s and the VDPs; the PS1 port used as reference at times; the last two
weeks of not-yet-converged work recovered.

This charter supersedes the *scope* limits of
`docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md` (the owner has explicitly
expanded the product goal) but **keeps its operating contract in full force**:
one change → one uniquely identified CUE → one observation; two-attempt/
two-hour stop rule; identity binding before every launch; live gates before
infrastructure; unsupported content skips at the smallest safe object.

**Evidence base:** the seven-lane donor-map audit of 2026-08-14
(https://claude.ai/code/artifact/ef0b2f69-1696-4bf7-89df-0f51ebcf8ccf). Every
verdict cited below traces to that audit.

---

## Locked decisions

| # | Decision | Rationale |
|---|---|---|
| D1 | **Recovery branch `saturn/recovery` forks from donor HEAD `b2447f6715bf64d34be4391298f3746db919aa6f`**, in a new worktree `.worktrees/saturn-recovery`. The donor worktree is never modified. | The A9A renderer lineage survives at HEAD nearly unchanged (239 lines); HEAD also carries the DIVU fix `b9679f57` that the historical donor lacks. Branching earlier would re-lose committed target-proven work. |
| D2 | **The donor's entire dirty state is preserved first, in curated cluster commits**, before any build is attempted. | 14 source/test files exist nowhere in git; the tracked tree does not link without them. Preservation is the only irreversibility risk in the whole program. |
| D3 | **Music now = packaged PCM loop through the proven MC68000/SCSP driver with the SCSP hardware loop bit. The M64 sequence VM is banked.** | Handoff Phase B authorizes exactly this ("predecoded/packaged PCM stream or loop"). The VM has two root-cause defects (68K stack overflow; channel-vs-layer decode). The retrigger fallback assumes a 240 Hz timer that does not exist — the hardware loop bit replaces it. |
| D4 | **Music at full-game scale = CD-DA redbook tracks; SFX = SFXB PCM in sound RAM.** | PS1-port-shaped boundary. The full audio catalog (4.46 MB) exceeds the entire cart; CD-DA costs zero sound RAM and zero cart bytes. Needs one bounded CD-block play/loop subsystem (Phase S3). |
| D5 | **Any-level loading = S64P scene packages streamed from CD into the 4 MB DRAM cart window per level, replacing the link-everything-at-boot `SOURCE.DAT` model.** | One level already fills 96.5% of the cart and its pointers are non-relocatable. S64P (destination-class + lifetime sections) was designed for exactly this and is the least over-built piece of the actor lane; it has just never carried real bytes. |
| D6 | **Renderer strategy: generalize the per-level bake pipeline (the A9A demo path's technique) rather than accelerate interpreted Fast3D.** Fast3D stays banked for dynamic/2D geometry. | The accepted 5.294 FPS renderer is compile-time BOB-specialized; the generic interpreter measured 0.378 FPS. Precompiled level IR is how shipped Saturn engines worked (SlaveDriver/Z-Treme, already studied in `work/upstream/`). |
| D7 | **Saves = Saturn backup RAM (BUP) backend behind the existing `osEeprom*` seam.** | All EEPROM calls are silent no-ops today; the seam is already where the PC port abstracts persistence. Bounded, standard subsystem. |
| D8 | **PS1 port (`malucard/sm64-psx`) is a behavior/architecture reference only — never code.** | License not established. `docs/saturn/PSX_PORT_ARCHITECTURE_LESSONS.md` already records the behavior study at pinned commit `3073845688ea273da78d539b20c45110d8a868c3`. |
| D9 | **Feature re-entry order after baseline: audio → Bob-omb/actors → Mario animation → HUD.** Each is its own one-change/one-CUE step. | Mario animation is the least-ready feature (V1-only scale gate, idle-only light streams). HUD contributes memory pressure and re-enters last. |
| D10 | **Recovery worktree path must contain the literal substring `sm64-port`.** | The linker's `*sm64-port?*` glob collects `.cart_rodata`; a path without it silently ships an empty `SOURCE.DAT`. `.worktrees/saturn-recovery` under `sm64-port/` satisfies this. |

## Program phases and gates

Each phase gets its own plan document written **at phase entry** (never
earlier), following `superpowers:writing-plans`, sized to one owner-observable
gate. A phase is closed only by an owner-accepted, identity-bound CUE (or, for
tooling-only phases, by the owner accepting the artifact the tooling produced).

| Phase | Deliverable | Gate |
|---|---|---|
| **R0 — Preserve** | `saturn/recovery` branch carrying all donor work in curated commits; `AGENTS.md` constitution restored | Diff-verified transplant; no build claims |
| **R1 — Baseline + audio** | One CUE at A9A feature parity + semantic audio: non-regressed Mario/BOB visuals, looping real BOB music, one game-triggered SFX, ≥4 FPS | Owner sees and hears it |
| **R2 — Normal Bob-omb** | `dynamic_actor_closure=1` with the four audit patches (VRAM region carve, per-instance skip, publication overwrite, generation regen) | Owner identifies a textured, grounded, normal Bob-omb; nothing else regresses |
| **R3 — Mario animation + HUD** | Version-aware scale, per-animation light streams; HUD re-entry as its own CUE | Owner accepts animated Mario, then HUD |
| **S1 — Any-level cart loader** | S64P packages carry real level bytes; CD→cart per-level load at boot; Whomp's Fortress as the second level through the same executable | Owner plays BOB *and* WF from one disc image, level chosen at boot |
| **S2 — Shrink & degrade pipeline** | Per-level budget report; CLUT16/4bpp texture conversion, mesh decimation, shared-tile materials, per-level bake generalization | A level that previously overflowed fits its budget with owner-accepted visuals |
| **S3 — Full-game audio** | CD-DA music subsystem (play/loop/stop on semantic state), full SFX coverage per level | Owner hears course-correct music across ≥2 levels from redbook tracks |
| **S4 — Saves** | BUP backend behind `osEeprom*`; save/load/star progression persists across power cycle | Owner saves, resets, and resumes |
| **S5 — Game flow & 2D** | VDP2 2D layer: title, file select, star select, pause, dialog, transitions; castle hub level | Owner boots to title and reaches a course through the retail flow |
| **S6 — All levels** | Level-by-level rollout: bake → budget → degrade → actors → audio → verify, one CUE per level | Owner-accepted checklist per course (15 courses + castle + secrets) |
| **S7 — Performance & hardware** | 6–10 FPS presentation band; real-hardware validation; PAL decision; distribution answer for the DRAM-cart requirement | Owner accepts cadence on hardware |

**Sequencing rule:** R-phases are strictly serial. S1 and S2 may interleave
task-wise (S2's shrinking exists to serve S1's budgets) but each keeps its own
gates. S3–S5 may run as parallel lanes after S1 closes, using separate
worktrees per `superpowers:using-git-worktrees`. S6 begins only after S1–S3
close (a level rollout needs loader + budgets + audio).

## Standing constraints (from the constitution, still binding)

1. Before each behavior change, write down: the owner-visible defect, baseline
   and candidate ISO/ELF/identity, one causal hypothesis, smallest files, the
   earliest Ymir observation, the stop time.
2. Stop after two implementation attempts or two hours without a new live
   result; the only allowed responses are revert, bypass, or a smaller
   transplant.
3. Identify builds by the identity-tag/ELF/ISO tuple — never the CUE hash
   (stable 88-byte descriptor collides across builds).
4. Testing budget before the live observation: one focused defect regression,
   target compile/link, console-safety checks only. Broad suites run after a
   live win, before it becomes the new baseline.
5. Unsupported content skips at the smallest safe object; audio failure mutes
   audio only; one bad actor never blanks a frame.
6. `build/saturn/baselines/a9a-2026-08-05/` is immutable.
7. Windows/MSYS build law lives in `docs/saturn/BUILDING.md`; builds are
   invoked via `tools/saturn/with-msys-toolchain.ps1`, `-j1`, with the full
   27-variable set (the profile JSON is not read back into Make).

## Known hazards register (carry into every sprint plan)

- Uncommitted `rendering_graph_node.c` 7-line change silently drops actors
  missing from the identity registry (R2 must reconcile with regenerated
  packages — generation 15 is stale; the tuple regenerates as 24).
- Three efforts overlap in `sourceboot/main.c` (memory relief, HUD, audio
  wiring) — transplants of one drag the others; R0 preserves all three
  together and later sprints edit in place.
- Per-frame audio `.text` relocated to A-bus cart (uncommitted linker delta) is
  an unmeasured cadence risk; R1 decides it by measurement, not assumption.
- HWRAM slack was 24 bytes with every feature on; the margin readback is a
  build output, not a ledger note (Sprint 1 wires the gate).
- All emulator evidence rides a patched Ymir fork (CD-block 0x65/0x66);
  real-hardware validation is S7 scope but assumptions should be flagged as
  they are made.
- The proven 68K driver binary (2,918 B, SHA `273d8b28…`) is not reproducible
  from the current tree; R1 rebuilds a dieted driver and re-proves it on
  target rather than claiming inheritance.

## Sprint index

- **Sprint 1 (R0 + R1):** `docs/superpowers/plans/2026-08-14-sprint1-recovery-baseline-audio.md` — written, ready to execute.
- Sprint 2 (R2): plan at entry; the four patches are pre-scoped in the donor-map audit (actor lane).
- Sprint 3+ (R3, S1…): plan at entry.
