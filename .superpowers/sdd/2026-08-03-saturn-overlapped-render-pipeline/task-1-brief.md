### Task 1: Remove duplicate source scene construction and publish the earliest manual CUE

## Binding execution constraints

- Work only in `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge` on the existing `sh2/native-math-purge` branch.
- Preserve and do not stage unrelated dirty verifier edits, audits, evidence,
  temporary directories, or route files.
- Use test-driven development: create and run the named failing tests before
  implementation, then run the named focused green gates.
- Update this plan's Task 1 steps/status, the approved architecture decision
  ledger, and `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
  during the same task transition before reporting completion.
- Do not launch a target build or Ymir; the controller owns the serial manual
  CUE gate after reviews.
- Run any MSYS/SH tool through `tools/saturn/with-msys-toolchain.ps1`.
- Experimental CUEs do not require the strict native-math census.
- Master SH-2 alone owns live SM64 state and presentation. The Task 1 source
  suppression path must preserve authoritative state updates and the Saturn
  interpreted renderer. PC/N64 builds are outside this task's supported scope.
- Stage only Task 1-owned files. Commit implementation and documentation, then
  self-review the committed range.

**Files:**
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.c`
- Modify: `src/game/area.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- Modify: `tools/saturn/fast3d_profile_decode.py`
- Create: `tools/saturn/test_source_render_suppression.py`
- Modify: `tools/saturn/runtime_contract_test.c`
- Modify: `Makefile.saturn.mk`
- Create: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Modify: architecture spec and this plan

**Interfaces:**
- Produces `sm64_saturn_source_runtime_set_scene_graph_suppressed(bool)` and
  `sm64_saturn_source_runtime_scene_graph_suppressed(void)`.
- Produces counters `scene_graph_walks` and `scene_graph_walks_suppressed`,
  appended to `sm64_saturn_source_runtime_state_t` and the Fast3D profile.
- Retains stateful `do_cutscene_handler()`,
  `print_displaying_credits_entry()`, `render_menus_and_dialogs()`, and warp
  transition state updates. Suppresses `geo_process_root()` and source-only
  viewport/HUD/text/scissor construction when the Saturn IR renderer owns the
  frame.

- [ ] **Step 1: Write the red source-policy test**

  Create a Python test that extracts `render_game()` from `src/game/area.c`
  and requires the scene-graph call to be guarded while stateful calls remain
  outside that guard:

  ```python
  def test_saturn_ir_path_skips_geo_but_keeps_state_updates():
      body = extract_c_function(AREA_C, "render_game")
      assert "sm64_saturn_source_runtime_scene_graph_suppressed" in body
      guarded = extract_if_block(body, "!scene_graph_suppressed")
      assert "geo_process_root(" in guarded
      for call in ("do_cutscene_handler(",
                   "print_displaying_credits_entry(",
                   "render_menus_and_dialogs(",
                   "render_screen_transition("):
          assert call in body
          assert call not in guarded
  ```

- [ ] **Step 2: Add the red runtime contract**

  In `runtime_contract_test.c`, set suppression true/false and assert the
  getter and appended counters do not alias existing state fields. Add
  `verify-source-render-policy` to `Makefile.saturn.mk` to run both tests.

- [ ] **Step 3: Run the focused gate and record the expected failure**

  Run:

  ```powershell
  & tools/saturn/with-msys-toolchain.ps1 C:\msys64\usr\bin\make.exe -f Makefile.saturn.mk verify-source-render-policy
  ```

  Expected: FAIL because the scene-graph suppression API and guarded source
  path do not exist. Record the command/output in the evidence report.

- [ ] **Step 4: Implement the runtime policy and counters**

  Add this API shape without moving existing state fields:

  ```c
  void sm64_saturn_source_runtime_set_scene_graph_suppressed(bool suppressed);
  bool sm64_saturn_source_runtime_scene_graph_suppressed(void);
  void sm64_saturn_source_runtime_note_scene_graph_walk(bool suppressed);
  ```

  Append both counters at the end of runtime/profile structs and extend the
  existing profile decoder fixture for their exact offsets.

- [ ] **Step 5: Split render construction from render-time state updates**

  In `render_game()`, evaluate `scene_graph_suppressed` once. Put
  `geo_process_root()`, viewport setup, HUD/text emission, and source-only
  scissor emission under `if (!scene_graph_suppressed)`. Keep the four named
  stateful calls and warp-transition completion/decrement logic active in both
  paths. Always clear `D_8032CE74` and `D_8032CE78` at function exit.

- [ ] **Step 6: Enable the policy only around Saturn demo source ticks**

  Replace the current display-only suppression pair in
  `sourceboot_run_source_tick()` with paired display and scene-graph policy
  changes. Restore both flags before returning, including early-return/error
  paths. Ordinary interpreted builds retain the original source renderer.

- [ ] **Step 7: Run focused and aggregate host gates**

  Run `verify-source-render-policy`, `verify-runtime-contracts`, and
  `python tools/saturn/test_tools.py`. Expected: all PASS; the source-policy
  test proves the stateful whitelist and the runtime counter layout.

- [ ] **Step 8: Update live documentation before review**

  Mark each completed step here, set Task 1 to `source-complete` only after
  review, and add the exact source audit, commit candidate, tests, and the
  experimental target gate to the evidence report. Add any changed whitelist
  decision to the architecture ledger in the same commit.

- [ ] **Step 9: Commit and run two-stage review**

  Stage only Task 1 files and commit with
  `perf(saturn): bypass duplicate source scene construction`. Obtain spec
  review first, quality review second, resolve findings in follow-up commits,
  and record both final verdicts.

- [ ] **Step 10: Build and manually test the early CUE**

  Build serially with the established `poly2/pipe3`, live-input,
  bootstrap-600, Q16-camera-3 role through the audited wrapper. Launch the
  resulting CUE in Ymir with `.ymir-profile`. Record CUE/ELF SHA-256, source
  commit, whether controls work, whether BOB/Mario remain visible, and the
  owner's qualitative speed result. Do not run the strict native-math census
  for this experimental checkpoint.
