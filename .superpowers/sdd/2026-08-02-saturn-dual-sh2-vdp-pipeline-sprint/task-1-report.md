# Task 1 report: dual-SH2 and dual-VDP ownership evidence

Status: DONE_WITH_CONCERNS

Commits:

- `perf: expose dual-SH2 and dual-VDP frame stages`
- `perf: count all dual-pipeline fault paths`

Changed files:

- `src/port/saturn/gfx/saturn_fast3d_frontend.h`
- `src/port/saturn/gfx/saturn_demo_render.c`
- `src/port/saturn/sourceboot/main.c`
- `tools/saturn/fast3d_profile_decode.py`
- `tools/saturn/runtime_contract_test.c`
- `tools/saturn/test_tools.py`

The profile appends, in order, `master_worker_started`,
`slave_worker_started`, `vdp1_commands`, `vdp2_active_layers`, and
`pipeline_faults`. The master remains the only owner of game state, final VDP1
ordering, and presentation. The existing bounded terrain dispatch increments
the two worker markers without changing its split or fallback. Presentation
records the VDP1 list count and the existing NBG1|NBG3 VDP2 mask. Timeout and
overflow observations increment `pipeline_faults`; they do not gate, retime,
or reschedule any work.

Tests:

- RED: `.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py Fast3dProfileDecodeTests.test_dual_pipeline_counters_append_and_decode` failed as expected before implementation because the five fields were absent.
- GREEN: `.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py Fast3dProfileDecodeTests.test_dual_pipeline_counters_append_and_decode Fast3dProfileLayoutTests.test_offsets_match_a_compiled_offsetof_probe` passed (2 tests).
- GREEN: host compilation and execution equivalent to `verify-runtime-contracts` passed: `gcc -std=c11 -Wall -Wextra -Werror ... tools/saturn/runtime_contract_test.c ... -o build/saturn/host-tests/runtime-contract-test.exe; build/saturn/host-tests/runtime-contract-test.exe`.
- Corrective GREEN: reran the focused decoder/`offsetof` checks and the host runtime-contract compile/run after adding diagnostic-only `pipeline_faults` increments for the bounded peer-transform fence failure and both Gouraud allocation-exhaustion fallbacks; all passed.
- Attempted required command: `make -f Makefile.saturn.mk verify-tools verify-runtime-contracts`. `make` is absent from PATH. The available `C:\msys64\usr\bin\make.exe` ran `verify-tools`, which completed with one stale profile-fixture failure and 17 unrelated route-schema errors; it did not reach `verify-runtime-contracts`. Running that target alone through MSYS failed before compilation because MSYS converted the workspace path to an inaccessible `\\d\\...` path.

Residual concerns:

- The full tools suite is already inconsistent with the worktree: `Fast3dProfileDecodeTests.test_older_capture_partial_decode_names_what_is_absent` omits six pre-existing appended fields (`flat_primitives`, `gouraud_primitives`, and four terrain-descriptor fields), and 17 `BobParityRouteTests` fail because `tools/saturn/routes/bob_parity_v1.json` no longer matches the SBR4 required-probe schema. These are outside Task 1; this task added its five new fields to the older-capture missing-field expectation.
- The full Make target needs a native/Windows-compatible GNU make environment to avoid MSYS's drive-path conversion. No target build or Ymir run was performed.

Corrective scope:

- `pipeline_faults` now increments once for the existing latched peer-transform wait failure before the serial fallback clears it, and once for each existing Gouraud-bank exhaustion fallback. These additions do not alter ownership, polling bounds, fallback execution, or presentation.
