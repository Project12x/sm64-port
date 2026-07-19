# E2 source-platform seam — 2026-07-19

## Scope

This is a non-visual E2 checkpoint. It puts the Saturn services immediately
beneath the inherited `src/game/game_init.c` frame loop and installs a bounded
Fast3D `SPTask` consumer. It does **not** claim a booted source level or replace
the existing Castle harness's manual state.

## Original-source boundary retained

- `thread5_game_loop()` retains source setup and level-script entry selection.
- `game_loop_one_iteration()` retains source input decoding, graphics-pool
  selection, level-script execution, and its display boundary.
- `display_and_vsync()` invokes original `exec_display_list(&gGfxPool->spTask)`;
  the Saturn runtime supplies target VBlank presentation after that submission.
- `ControllerAPI` continues to terminate at SM64 `OSContPad` data.
- `src/game/main.c` cannot provide its N64 SP/DP queue implementation under
  `TARGET_SATURN`; the Saturn dispatcher is the sole `exec_display_list()`
  definition in a future complete source-runtime link.

## Saturn implementation

- `src/port/saturn/runtime/saturn_source_runtime.c` supplies controller
  initialization/polling, source tick accounting, presentation VBlank, and a
  target `exec_display_list()` dispatcher.
- The Castle link now includes `src/game/main.c` so the normal source-loop
  globals are link-checked alongside the target dispatcher; its N64 dispatcher
  is unavailable under `TARGET_SATURN`.
- Startup performs a one-command `G_ENDDL` preflight through the real public
  dispatcher and the configured Saturn Fast3D consumer. It is non-visual and
  bounded; failure deliberately stops boot rather than silently discarding the
  source-task route.
- `src/port/saturn/gfx/saturn_fast3d_frontend.c` is a source-neutral Fast3D
  intake. It has a 32-call / 16,384-command bound, recognizes display-list
  control flow and core geometry/texture/RDP categories, and records faults
  instead of walking unbounded memory.
- The intake follows the command ABI present in this fork's
  `src/pc/gfx/gfx_pc.c`; it is new Saturn code, not a copied PC or PS1 backend.

## Upstream/reference ledger

| Source | Pin/license | Files inspected | Reuse mode |
| --- | --- | --- | --- |
| `Project12x/sm64-port` inherited source | current fork / existing project terms | `src/game/game_init.c`, `src/pc/pc_main.c`, `src/pc/gfx/gfx_pc.c`, `src/pc/controller/controller_entry_point.c`, `src/pc/ultra_reimplementation.c` | direct in-tree integration for `game_init.c`; pattern/reference for the new target front end |
| `malucard/sm64-psx` | `3073845688ea273da78d539b20c45110d8a868c3` / no repository-wide license observed | `src/game/game_init.c`, `src/port/gfx/gfx_rsp_jit.c`, `src/port/psx/gfx_dl_exec_psx.c` | behavior/architecture study only; no source copied |
| `yaul-org/libyaul` | `6012f79f237773378c8014e70d8998ad95a38d98` / MIT | VBlank and SMPC APIs recorded in `UPSTREAM_CODE_LEDGER.md` | existing dependency/API use |

## Verification

```text
.venv-saturn-tools\\Scripts\\python.exe tools\\saturn\\test_tools.py
58 tests passed

C:\\msys64\\usr\\bin\\bash.exe -lc
  "cd /c/Users/estee/Documents/Codex/2026-07-16/i-want-to-postulate-a-port/sm64-port &&
   source .yaul.env && make -C src/port/saturn/castleviewer verify"
success: SH-2 ELF, ISO, CUE, and entry-point verification
```

The final link map places `sm64_saturn_fast3d_frontend_submit` at a nonzero
SH-2 text address (`0x06005AAC` for this build), proving that the boot
preflight keeps the target task consumer in the emitted disc rather than only
compiling it into a discarded object section.

The build retains pre-existing warnings for `min`/`max` macro redefinition,
an unused Castle helper, an unused inherited source variable, and Yaul's RWX
load segment. No screenshot entry is added: this checkpoint changes plumbing,
not visible output.
