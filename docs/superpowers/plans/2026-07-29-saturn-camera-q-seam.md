# Saturn Camera Q-Seam Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a selectable Saturn-only persistent Q-format implementation of
the bounded Mario/default/Lakitu camera loop, prove its 600-tick neutral-input
stability and route identity from raw target evidence, reduce the audited
simulation-helper total, and preserve the public camera ABI plus the immutable
`bob-parity-v1` regression contract.

**Architecture:** Land the proof infrastructure before behavior: first add the
separate default-camera replay and a raw-first SCC1 host contract, then add the
target semantic probe, writer inventory, baseline quiescence/range capture, and
deterministic Q16.16-versus-Q20.12 decision. Implement the selected-format
numeric kernel and persistent shadow behind `SATURN_CAMERA_VARIANT=2`; keep
`camera.c` as the sole adapter to private SM64 state and float-only external
floor/collision APIs. Finish by pinning the complete audit closure, capturing
two target runs per role, rerunning the original route, and requiring both
independent A/B pairs to lower `sim_frt_ticks_accum`.

**Tech Stack:** C and SH-2 GNU assembly built by the pinned Yaul
`sh-elf-gcc` toolchain; Python 3 standard-library `unittest`, `struct`,
`dataclasses`, and `hashlib`; GNU Make; `sh-elf-nm`, `sh-elf-objdump`, and
`sh-elf-addr2line`; Ymir build-agent2 raw-memory capture; JSON and Markdown
evidence.

## Global Constraints

- Execute from
  `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`
  on branch `sh2/native-math-purge`. Use
  `git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`
  for every Git command; do not alter global Git configuration.
- Treat
  `docs/superpowers/specs/2026-07-29-saturn-camera-q-seam-design.md`
  as the normative specification. If implementation evidence contradicts the
  spec, stop and amend/re-review the spec instead of weakening a test.
- The proved linked-ELF audit contradiction is the one authorized exception
  to the old v2 digest: Task 3 must capture the legacy facts, harden the
  parser, prove corrected route-0/route-1 equality, receive a clean review,
  and then re-pin v2 exactly once. Disabling the post-link audit, excluding
  all internal-offset targets, or pinning route 1's inflated count is
  forbidden.
- Preserve the public `struct Camera`, `struct LakituState`,
  `update_default_camera`, `next_lakitu_state`, `update_lakitu`,
  `mode_default_camera`, `mode_lakitu_camera`, and `mode_mario_camera`
  signatures. Non-Saturn preprocessing and behavior must remain unchanged.
- Keep `tools/saturn/routes/bob_parity_v1.json` and
  `src/port/saturn/sourceboot/source_demo_data.c` byte-for-byte unchanged.
  The immutable manifest SHA-256 is
  `f62861516c708ef0bda82c0a32bf10706a93b0948160f42b50c1e6ec9ddd44ee`.
- `bob-default-camera-v1` is a separate 2,000-tick route:
  `120 neutral`, `1 R_TRIG`, `1879 neutral`. Its raw SCC1 route ID is `2`.
  The stored `Camera.mode` remains radial; activation is proved by Mario-mode
  dispatch, `gCameraZoomDist == 350.0f`, a nonzero Q generation, and pinned
  nonzero bridge counts.
- SCC1 is exactly 24 header words plus 600 samples of 81 words:
  `194496` bytes (`0x2F7C0`). Pack all fields semantically in the specified
  order. Never `memcpy` `Camera`, `LakituState`, or a compiler-native C struct
  into SCC1.
- Record SCC1 immediately after `game_loop_one_iteration()` and simulation
  timing/accounting inside `sourceboot_run_source_tick()`, not after rendering.
  Pass the existing sourceboot-owned `sourceboot_sim_tick_count` into the
  recorder; do not add a second replay/source counter. `input_replay_ticks`
  intentionally freezes at 2,000.
- Publish target evidence through the SH-2 cache-through alias and publish
  magic last. Read the SCC1 window in bounded chunks and reject any raw/decoded
  disagreement.
- Variant 1 is the unchanged float baseline. Variant 2 is the bounded Q
  island. Both camera roles fix `SATURN_ATAN2_VARIANT=2`; every output path
  includes unconditional camera-variant and selected-route tags.
- A bootstrap tick before the first successful Q seed is unarmed and may use
  the float path. After the first seed, any unsupported dispatch or range
  violation enters one named, noinline cold float-fallback bridge, invalidates
  the shadow, and increments `range_fallback_count`. Accepted Q captures
  require that count and every other fault/reseed counter to be zero.
- No target-hot code may express C 64-bit division or modulo. Q16 state
  multiplication may reuse `sm64_saturn_q16_mul_sh2`. Q12 state
  multiplication must reconstruct `MACH:MACL` and shift by 12. Q12 division
  may narrow a proved Q16 DIVU quotient with exact signed truncation by 16.
- Before kernel implementation, inspect the pinned compatible references:
  Jo Engine `556d081146211b6a1cfa6591d70f9487d406758b`,
  `jo_engine/math.c:57-70`, and SlaveDriver
  `a8986591557b6e680550d3c23970284d3b38ff8f`,
  `WALLASM.S:253-353`. Preserve the existing notices and update
  `docs/saturn/PROVENANCE.md` for any Q12 close adaptation. The unlicensed
  `sm64-psx` pin remains behavior study only.
- Use red-green-refactor. Observe the named failure before adding production
  code, rerun the focused test after each small change, and commit only a
  coherent passing slice.
- On Windows, prepend `C:/msys64/usr/bin` for SH tools so
  `msys-2.0.dll` and `msys-gcc_s-seh-1.dll` resolve. Target builds keep Yaul's
  `COMPILER_PATH`; host fixtures explicitly remove `COMPILER_PATH` and use
  `C:/msys64/mingw64/bin/gcc.exe`. Every Python subprocess that invokes an
  absolute Yaul SH tool must construct an environment whose `PATH` begins
  with `C:\msys64\usr\bin`; direct SH-tool commands run inside the configured
  MSYS shell. Never permanently mutate the caller's `PATH` or
  `COMPILER_PATH`.
- Treat HWRAM as a first-class stop gate. The current measured
  `___end=0x060FDCB0` leaves only `0x2350` total and `0x1350` (4,944 bytes)
  above the mandatory `0x1000` TLSF floor. Before SCC/Q target code, shrink
  the boot-only cart staging buffer through the measured selector in Task 3.
  The post-transport build must leave at least `0x5B00` total HWRAM margin,
  reserving `0x4000` for later camera code plus the immutable final
  `0x1B00` floor (`0x1000` TLSF + `0x0B00` safety). Re-run the map gate after
  every target-code task; never weaken either floor.
- The target capture and performance gates are serial. The user's machine is
  CPU-busy: run every build in this plan with `make -j1`, never overlap two
  build/test commands, and keep subagents serial. `-j1` is an operational
  scheduling change only and must not change flags, artifacts, or evidence
  gates. Any older quoted command below that still spells `make -j2` is
  explicitly superseded: substitute `make -j1` when executing it.
- Each task ends with a scoped review. Do not proceed past an unresolved P1 or
  P2 finding. Update
  `.superpowers/sdd/2026-07-29-sh2-native-math-purge/progress.md` after each
  clean task.

---

## File Responsibility and Interface Map

| File | Sole responsibility | Interfaces and constraints |
| --- | --- | --- |
| `src/port/saturn/sourceboot/source_camera_acceptance_route.h/.c` | Own the new three-sample replay table. | Export only `sm64_saturn_sourceboot_bob_default_camera_v1(uint16_t *)`; do not edit `source_demo_data.c`. |
| `tools/saturn/routes/bob_default_camera_v1.json` | Human- and host-readable route contract. | Exactly three samples totaling 2,000 ticks; route ID 2; its SHA-256 is recorded in every SCC report. |
| `src/port/saturn/runtime/saturn_source_runtime.h/.c` | Own last applied pad telemetry. | Copy the actual post-replay `pads[0]` values into the existing private runtime snapshot; do not change controller deadzones, replay consumption, or replay tick semantics. |
| `tools/saturn/camera_idle_contract.py` | Sole definition of SCC1/SQT1 constants, raw decoders, validators, and role comparator. | `decode_scc1`, `decode_sqt1`, `validate_scc1`, `compare_same_role`, and `compare_camera_roles`; no target or emulator I/O. |
| `tools/saturn/capture_camera_idle.py` | Resolve symbols and read raw target memory. | Use sibling ELF `sh-elf-nm` with an explicit MSYS DLL path; read chunks no larger than Ymir's 65,536-byte response ceiling; return exact SCC1/SBR4 bytes plus ELF/ISO hashes and absolute build markers. |
| `tools/saturn/verify_camera_idle_capture.py` | CLI/report adapter around the pure SCC1 contract. | Decode raw first, compare decoded JSON only as a redundant check, and emit role-bound JSON. |
| `tools/saturn/compare_camera_route_reports.py` | Compare camera-baseline/camera-Q SBR4 reports without changing the legacy/Q16 comparator. | Reuse the immutable SBR4 decoder; require both atan2 variants 2, ELF camera markers 1/2, same-role non-timing determinism, cross-role behavior, and optional strict simulation improvement. |
| `tools/saturn/verify_sourceboot_memory_map.py` | Parse role ELF symbols/sections and enforce incremental HWRAM/LWRAM budgets. | Record `___end`, total/TLSF-usable HWRAM, relevant BSS symbols, SCC/discovery sections, previous-phase delta, and selected cart-stage sectors; reject any final HWRAM margin below `0x1B00`. |
| `src/port/saturn/sourceboot/source_cart.c` | Own the boot-only CDFS-to-cart staging buffer. | Default remains 16 sectors; Task 3 camera artifacts select and tag 8 sectors, or 4 only when the deterministic post-transport `0x5B00` reserve gate requires it. Route and cart hashes must remain exact. |
| `src/port/saturn/runtime/saturn_camera_probe.h` | Shared semantic snapshot ABI between `camera.c` and sourceboot. | Exactly 77 state words, flags, diagnostics, bridge counts, and generation; no engine structs. |
| `src/game/camera.c` | Sole owner of the Q shadow and adapters to camera-private globals. | Explicit seed/import, eligibility, default/Mario seam, Lakitu seam, collision bridges, publish, invalidate, probe packing, and cold fallback. |
| `src/port/saturn/sourceboot/source_camera_idle_probe.h/.c` | Own SCC1 LWRAM storage, fixed/discovery recording, SQT1 HWRAM storage, and magic-last publication. | Build only for replay route 1; sample once per source tick after the game loop; latch SQT1 only on the first successful Mario Q seed. |
| `src/port/saturn/sourceboot/sourceboot-cart.x` | Reserve the replay-only SCC1 `NOLOAD` section and enforce the selected HWRAM floor. | Preserve at least `0x4000` LWRAM after the `0x2F7C0` capture; the section is empty in non-camera builds; camera evidence builds require at least `0x1B00` HWRAM after `___end`. |
| `tools/saturn/camera_q_writer_contract_v1.json` and `verify_camera_q_writers.py` | Classify every bounded-call-graph write. | Every write has exactly one owner: Q-owned, float-API import, public mirror, or invalidation. |
| `tools/saturn/camera_range_contract.py`, `select_camera_q_candidates.py`, and `freeze_camera_q_format.py` | Decode measured operands, nominate candidates, and freeze the post-differential winner. | Range proofs may qualify Q16/Q12; production differential and hash checks freeze Q16 first or reviewed Q12; otherwise exit nonzero. |
| `src/port/saturn/runtime/saturn_camera_q_config.h` | Commit the selected fraction bits and proven envelope. | Generated only by the passing format selector; variant 2 refuses an absent/bootstrap config. |
| `src/port/saturn/runtime/saturn_camera_q_math.h/.c` | Selected-format scalar/vector math and raw IEEE-754 bit bridges. | No camera globals, collision calls, sourceboot logic, or target-hot C 64-bit division/modulo. |
| `src/port/saturn/runtime/saturn_camera_q.h/.c` | Persistent shadow types plus pure state-machine operations. | Seed, default goal, transition, Lakitu smoothing, invalidation, diagnostics, and mirror values; external effects arrive only through named direct bridge functions. |
| `tools/saturn/camera_q_diff_fixture.c` and `test_camera_q.py` | Host differential and mutation corpus. | In-tree float formulas are the reference; literal boundaries and captured operands are both mandatory. |
| `tools/saturn/camera_q_object_contract.py` | Shared canonical SH-object disassembly and selected-candidate equivalence check. | Strip only the objdump input banner, normalize CRLF to LF, compare code/relocations exactly, and provide the canonicalizer later imported by audit v3. |
| `tools/saturn/verify_sh2_native_math.py` | Parse linked SH code and enforce audit-contract v2 and v3. | Task 3 replaces linear pool decoding with terminating delay-slot-aware executable-code/dataflow analysis, emits observation JSON, and re-pins corrected v2 once; Task 8 may change verifier bytes for v3 but must preserve the historically pinned v2 facts. |
| `tools/saturn/compare_sh2_native_math_audit_reports.py` | Compare legacy and corrected route-0/route-1 audit observations. | Require corrected closure/direct-call/helper facts and total to be layout-invariant; report old/new totals plus sorted added/removed facts and finalize the reviewed v2 re-pin. |
| `tools/saturn/sh2_native_math_sim_audit_contract_v2.txt` | Pin the corrected source-simulation audit total. | Rewrite `EXPECTED_TOTAL` exactly once after the Task 3 equality report and independent review, then freeze its new SHA-256 in the verifier. |
| `tools/saturn/sh2_native_math_sim_audit_contract_v3.txt` | Pin final measured Task 3 audit facts. | Generated from the final Q ELF only after its closure is reviewed; exact total must be lower than v2. |
| `docs/saturn/evidence/reports/task3-*` | Durable range, audit, capture, A/B, and final reports. | Every report includes command line, commit, artifact hashes, route digest, role, and raw-derived result. |

The stable camera-side seam is guarded by
`#if defined(TARGET_SATURN) && SATURN_CAMERA_VARIANT == 2` and uses these
private, noinline `camera.c` adapters:

```c
static s32 saturn_camera_q_prepare_tick(struct Camera *c);
static void saturn_camera_q_set_zoom_world(s32 world_units);
static s16 saturn_camera_q_default_seam_tick(struct Camera *c);
static void saturn_camera_q_lakitu_seam_tick(struct Camera *c);
static void saturn_camera_q_publish_bridge(struct Camera *c);
static void saturn_camera_q_invalidate(u32 reason);
static void saturn_camera_cold_float_fallback_tick(struct Camera *c);
```

Place the private shadow after the existing camera globals and before the
transition-function declarations. Do not expose the current effectively
private BSS globals through a Saturn header. The insertion anchors are:

- prepare after `sYawSpeed = 0x400` and before mode dispatch in
  `update_camera`;
- set Q zoom in `mode_mario_camera`/`mode_lakitu_camera`;
- replace the `update_default_camera`/`pan_ahead_of_player` pair only on the
  active Q tick;
- replace the sole `update_lakitu(c)` call only on the active Q tick;
- publish once after Lakitu/post-adjustment and before
  `gLakituState.lastFrameAction`;
- invalidate on initialization, cutscene, unsupported dispatch, and every
  classified later external writer.

The sourceboot-facing probe ABI is:

```c
#define SM64_SATURN_CAMERA_PROBE_STATE_WORDS 77U

typedef struct sm64_saturn_camera_q_diagnostics {
    uint32_t overflow_count;
    uint32_t saturation_count;
    uint32_t divide_fault_count;
    uint32_t unexpected_reseed_count;
    uint32_t range_fallback_count;
    uint32_t bridge_export_count;
    uint32_t bridge_import_count;
    uint32_t shadow_generation;
} sm64_saturn_camera_q_diagnostics_t;

typedef struct sm64_saturn_camera_probe_snapshot {
    uint32_t state_words[SM64_SATURN_CAMERA_PROBE_STATE_WORDS];
    uint32_t camera_flags;
    uint32_t source_dispatch;
    sm64_saturn_camera_q_diagnostics_t diagnostics;
} sm64_saturn_camera_probe_snapshot_t;

s32 sm64_saturn_camera_probe_read(
    sm64_saturn_camera_probe_snapshot_t *snapshot);
```

`camera.c` fills `state_words[0]` through `[76]`, corresponding exactly to
SCC1 sample words 4 through 80. Sourceboot alone packs sample words 0 through
3 and the 24-word header. `source_dispatch` is an out-of-band semantic used
only to latch SQT1; it is not an additional SCC1 word.

---

### Task 1: Add the Separate Camera Route and Role-Binding Configuration

**Files:**

- Create:
  `src/port/saturn/sourceboot/source_camera_acceptance_route.h`
- Create:
  `src/port/saturn/sourceboot/source_camera_acceptance_route.c`
- Create: `tools/saturn/routes/bob_default_camera_v1.json`
- Create: `tools/saturn/camera_acceptance_route.py`
- Create: `tools/saturn/test_camera_acceptance_route.py`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.h`
- Modify: `src/port/saturn/runtime/saturn_source_runtime.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Test: `tools/saturn/runtime_contract_test.c`

**Interfaces:**

- Consumes:
  `sm64_saturn_input_replay_init`,
  `sm64_saturn_input_replay_apply`, and
  `sm64_saturn_source_runtime_configure_input_replay`.
- Produces:
  `sm64_saturn_sourceboot_bob_default_camera_v1(uint16_t *)`,
  `first_mario_dispatch_tick(route_manifest) -> int`,
  runtime fields `last_applied_buttons`, `last_applied_stick_x`, and
  `last_applied_stick_y`, absolute ELF symbols
  `sm64_saturn_camera_variant_marker` and
  `sm64_saturn_camera_route_marker`, plus Make variables
  `SATURN_CAMERA_VARIANT`, `SATURN_SOURCEBOOT_CAMERA_ROUTE`,
  `SATURN_CAMERA_IDLE_START_TICK`, `SATURN_CAMERA_IDLE_DISCOVERY`,
  `SATURN_CAMERA_RANGE_CAPTURE`, and
  `SATURN_SOURCE_CART_STAGE_SECTORS`.

- [ ] **Step 1: Write the failing route immutability and shape test**

  In `test_camera_acceptance_route.py`, hash the old manifest and require the
  new JSON and C table to describe exactly:

  ```python
  EXPECTED_OLD_SHA256 = (
      "f62861516c708ef0bda82c0a32bf10706a93b0948160f42b50c1e6ec9ddd44ee"
  )
  EXPECTED_SAMPLES = [
      {"ticks": 120, "stick_x": 0, "stick_y": 0, "buttons": 0},
      {"ticks": 1, "stick_x": 0, "stick_y": 0, "buttons": 0x0010},
      {"ticks": 1879, "stick_x": 0, "stick_y": 0, "buttons": 0},
  ]
  EXPECTED_FIRST_MARIO_DISPATCH_TICK = 121
  ```

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_acceptance_route.py
  ```

  Expected failure: `bob_default_camera_v1.json` and the new accessor do not
  exist. Confirm the old-manifest hash assertion already passes.

- [ ] **Step 2: Add the manifest and isolated route translation unit**

  The target table is:

  ```c
  static const sm64_saturn_input_replay_sample_t sBobDefaultCameraV1[] = {
      { 120U, 0, 0, 0U },
      {   1U, 0, 0, R_TRIG },
      { 1879U, 0, 0, 0U },
  };

  _Static_assert(120U + 1U + 1879U == 2000U,
                 "camera acceptance route length drift");

  const sm64_saturn_input_replay_sample_t *
  sm64_saturn_sourceboot_bob_default_camera_v1(uint16_t *sample_count);
  ```

  Set JSON `route_version` to `bob-default-camera-v1`, `route_id` to `2`,
  `checkpoint_tick` and `simulation_ticks` to `2000`, and record that the
  one-tick trigger begins at one-based route tick 121. In
  `camera_acceptance_route.py`, parse the manifest, require exactly one
  one-tick `R_TRIG` sample with otherwise neutral input, and return
  `1 + sum(ticks before that sample)` from
  `first_mario_dispatch_tick`; do not hardcode 121 in production code. Rerun
  the focused test and require the C table, JSON samples, derived dispatch
  tick, and total ticks to agree.

- [ ] **Step 3: Record the exact applied pad without changing replay ticks**

  Extend `sm64_saturn_source_runtime_state_t` with:

  ```c
  uint16_t last_applied_buttons;
  int8_t last_applied_stick_x;
  int8_t last_applied_stick_y;
  ```

  Initialize them in `configure_input_replay`. In
  `sm64_saturn_source_runtime_read_controllers`, after replay application and
  any existing normalization, copy the actual `pads[0].button`,
  `pads[0].stick_x`, and `pads[0].stick_y` values into the runtime snapshot.
  Continue mirroring neutral input after `input_replay_complete` becomes true.
  Do not add fields to `sm64_saturn_input_replay_t` or alter its tick update.

  Add runtime-contract assertions for the trigger sample, the 2,000th sample,
  the first post-route neutral sample, and exact signed stick/button values.
  Require `input_replay_ticks` to retain its existing frozen-at-2,000
  behavior.

  Run:

  ```powershell
  make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe
  ```

  Expected result: the runtime contract passes without changing ordinary
  controller behavior.

- [ ] **Step 4: Add strict Make configuration and distinct output tags**

  Add defaults and validation:

  ```make
  SATURN_CAMERA_VARIANT ?= 1
  SATURN_SOURCEBOOT_CAMERA_ROUTE ?= 0
  SATURN_CAMERA_IDLE_START_TICK ?= 0
  SATURN_CAMERA_IDLE_DISCOVERY ?= 0
  SATURN_CAMERA_RANGE_CAPTURE ?= 0
  SATURN_SOURCE_CART_STAGE_SECTORS ?= 16
  ifneq ($(words $(filter $(SATURN_CAMERA_VARIANT),1 2)),1)
    $(error SATURN_CAMERA_VARIANT must be 1 (float) or 2 (Q))
  endif
  ifneq ($(words $(filter $(SATURN_SOURCEBOOT_CAMERA_ROUTE),0 1)),1)
    $(error SATURN_SOURCEBOOT_CAMERA_ROUTE must be 0 (bob-parity-v1) or 1 (bob-default-camera-v1))
  endif
  ifeq ($(SATURN_SOURCEBOOT_CAMERA_ROUTE),1)
    ifneq ($(SATURN_SOURCEBOOT_ROUTE_REPLAY),1)
      $(error SATURN_SOURCEBOOT_CAMERA_ROUTE=1 requires SATURN_SOURCEBOOT_ROUTE_REPLAY=1)
    endif
  endif
  ifneq ($(words $(filter $(SATURN_CAMERA_IDLE_DISCOVERY),0 1)),1)
    $(error SATURN_CAMERA_IDLE_DISCOVERY must be 0 or 1)
  endif
  ifneq ($(words $(filter $(SATURN_CAMERA_RANGE_CAPTURE),0 1)),1)
    $(error SATURN_CAMERA_RANGE_CAPTURE must be 0 or 1)
  endif
  ifneq ($(words $(filter $(SATURN_SOURCE_CART_STAGE_SECTORS),4 8 16)),1)
    $(error SATURN_SOURCE_CART_STAGE_SECTORS must be 4, 8, or 16)
  endif
  ```

  Add unconditional `-camv$(SATURN_CAMERA_VARIANT)`, replay-only
  `-camroute$(SATURN_SOURCEBOOT_CAMERA_ROUTE)`, and route-1
  `-idle$(SATURN_CAMERA_IDLE_START_TICK)-disc$(SATURN_CAMERA_IDLE_DISCOVERY)-range$(SATURN_CAMERA_RANGE_CAPTURE)`
  tags, plus an unconditional
  `-stage$(SATURN_SOURCE_CART_STAGE_SECTORS)` tag. Pass every value through
  `SH_CFLAGS`. Conditionally add the new route `.c` source for route 1. The
  idle/discovery/range/stage values must all be part of the object/output key
  because Yaul does not key objects on `SH_CFLAGS`. Keep their exact order
  `-demo-replay-camrouteN-atan2v2-camvN-idleN-discN-rangeN-stageN-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2`;
  omit idle/discovery/range for route 0. Add linker absolute symbols:

  ```make
  SH_LDFLAGS += \
    -Wl,--defsym=sm64_saturn_camera_variant_marker=$(SATURN_CAMERA_VARIANT) \
    -Wl,--defsym=sm64_saturn_camera_route_marker=$(SATURN_SOURCEBOOT_CAMERA_ROUTE)
  ```

  Test every invalid value and discovery/range on route 0 with a configured
  Yaul environment; require Make to exit 2 before compilation. Add a test that
  changes each flag separately and requires a distinct object/output path.

- [ ] **Step 5: Select the route in sourceboot without touching the old route**

  In `main.c`, use a compile-time branch:

  ```c
  #if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1
      route = sm64_saturn_sourceboot_bob_default_camera_v1(&sample_count);
  #else
      route = sm64_saturn_sourceboot_bob_parity_v1(&sample_count);
  #endif
  ```

  Build one route-0 and one route-1 preprocessing/configuration pass and
  inspect output directories and `sh-elf-nm` output to prove they cannot
  overwrite one another and that the absolute marker values are 0/1 for the
  route and 1 for the baseline camera variant. Run SH tools through the
  configured MSYS shell so their runtime DLLs resolve. Rerun
  `test_camera_acceptance_route.py` and the runtime contract.

- [ ] **Step 6: Commit the additive route/configuration slice**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/sourceboot/source_camera_acceptance_route.h src/port/saturn/sourceboot/source_camera_acceptance_route.c src/port/saturn/sourceboot/Makefile src/port/saturn/sourceboot/main.c src/port/saturn/runtime/saturn_source_runtime.h src/port/saturn/runtime/saturn_source_runtime.c tools/saturn/routes/bob_default_camera_v1.json tools/saturn/camera_acceptance_route.py tools/saturn/test_camera_acceptance_route.py tools/saturn/runtime_contract_test.c
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: add default camera acceptance route"
  ```

---

### Task 2: Build the Pure Raw-First SCC1 Host Contract

**Files:**

- Create: `tools/saturn/camera_idle_contract.py`
- Create: `tools/saturn/test_camera_idle_contract.py`
- Create: `tools/saturn/verify_camera_idle_capture.py`
- Create: `tools/saturn/test_verify_camera_idle_capture.py`
- Create: `tools/saturn/compare_camera_route_reports.py`
- Create: `tools/saturn/test_compare_camera_route_reports.py`
- Preserve unchanged: `tools/saturn/compare_route_reports.py`

**Interfaces:**

- Consumes: the approved 24-word header, 81-word sample, role, route-ID, and
  selected-Q tolerance contracts.
- Produces:
  `decode_scc1(raw: bytes) -> Scc1Capture`,
  `validate_scc1(capture: Scc1Capture, *, expected_role: str,
  expected_idle_start_tick: int, expected_route_id: int,
  expected_bridge_counts: tuple[int, int]) -> None`,
  `compare_same_role(first, second) -> None`, and
  `compare_camera_roles(baseline, q_variant, q_fraction_bits) -> None`.
  The separate SBR4 module produces
  `compare_camera_route_reports(baseline_runs, q_runs, route,
  require_sim_improvement) -> dict`.

- [ ] **Step 1: Write a canonical synthetic SCC1 builder**

  Start `test_camera_idle_contract.py` with constants matching the approved
  layout:

  ```python
  SCC1_MAGIC = 0x53434331
  SCC1_VERSION = 1
  SCC1_HEADER_WORDS = 24
  SCC1_SAMPLE_WORDS = 81
  SCC1_SAMPLE_COUNT = 600
  SCC1_PAYLOAD_WORDS = 48600
  SCC1_BYTES = 194496
  SCC1_ROUTE_ID = 2
  ```

  Build big-endian bytes with all required flags, neutral input, route ID 2,
  baseline zoom bits `0x43AF0000`, and 600 stable semantic samples. Do not
  import production constants into this builder; independence catches layout
  drift.

- [ ] **Step 2: Write failing decoder and raw-integrity cases**

  Tests must require:

  - exact byte length, magic, version, header/stride/sample/payload counts;
  - all reserved words/bits zero;
  - finite IEEE-754 values at sample offsets
    `{4..9,12..29,32..41,44..50,52..58,61,63,65..74,78}`;
  - signed two's-complement unpacking at every packed offset;
  - raw route ID 2 and `gCameraZoomDist` word 71 equal to `0x43AF0000`;
  - rejection of one mutation at each sample word offset 0 through 80.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  ```

  Expected failure: `ModuleNotFoundError: camera_idle_contract`.

- [ ] **Step 3: Implement the decoder with typed immutable records**

  Add:

  ```python
  @dataclass(frozen=True)
  class Scc1Sample:
      source_tick: int
      applied_input: int
      state_flags: int
      state_words: tuple

  @dataclass(frozen=True)
  class Scc1Capture:
      raw: bytes
      header: tuple
      samples: tuple

  def decode_scc1(raw: bytes) -> Scc1Capture:
      if len(raw) != SCC1_BYTES:
          raise Scc1Error(f"expected {SCC1_BYTES} bytes, got {len(raw)}")
      words = struct.unpack(">48624I", raw)
      header = tuple(words[:SCC1_HEADER_WORDS])
      samples = tuple(
          Scc1Sample(
              source_tick=words[base],
              applied_input=words[base + 1],
              state_flags=words[base + 2],
              state_words=tuple(
                  words[base + 4:base + SCC1_SAMPLE_WORDS]
              ),
          )
          for base in range(
              SCC1_HEADER_WORDS,
              len(words),
              SCC1_SAMPLE_WORDS,
          )
      )
      capture = Scc1Capture(raw=raw, header=header, samples=samples)
      validate_raw_layout(capture)
      return capture
  ```

  The implementation must call `struct.unpack(">48624I", raw)` only after the
  exact-length check, slice 24 header words, and construct 600 samples from
  explicit indices. `validate_raw_layout` owns the complete header,
  reserved-bit, sample-count, and finite-value checks.

- [ ] **Step 4: Add within-role validation and determinism**

  Implement:

  ```python
  def validate_scc1(
      capture: Scc1Capture,
      *,
      expected_role: str,
      expected_idle_start_tick: int,
      expected_route_id: int,
      expected_bridge_counts: tuple[int, int],
  ) -> None:
      validate_header_role(
          capture.header,
          expected_role=expected_role,
          expected_idle_start_tick=expected_idle_start_tick,
          expected_route_id=expected_route_id,
          expected_bridge_counts=expected_bridge_counts,
      )
      validate_stable_samples(capture.samples)

  def compare_same_role(first: Scc1Capture, second: Scc1Capture) -> None:
      if first.raw != second.raw:
          raise Scc1Error("same-role SCC1 windows are not byte-identical")
  ```

  Require consecutive source ticks; neutral input and all six flags on every
  sample; exact equality of words 1 through 80 across all 600 samples; zero
  error counters; baseline generation and bridge counts zero; Q generation
  nonzero and bridge counts equal the pinned nonzero pair; and byte-identical
  same-role windows. The fixed first tick is
  `2000 + expected_idle_start_tick`.

- [ ] **Step 5: Add cross-role comparison and artifact-role binding**

  Implement:

  ```python
  def compare_camera_roles(
      baseline: Scc1Capture,
      q_variant: Scc1Capture,
      *,
      q_fraction_bits: int,
  ) -> None:
      validate_cross_role_headers(baseline.header, q_variant.header)
      for baseline_sample, q_sample in zip(
          baseline.samples, q_variant.samples, strict=True
      ):
          compare_packed_fields(baseline_sample, q_sample)
          compare_float_fields(
              baseline_sample,
              q_sample,
              q_fraction_bits=q_fraction_bits,
          )
  ```

  Require exact packed integer fields and compare every float at every sample
  against `max(2 ** -q_fraction_bits, f32_ulp(baseline_value))`; additionally
  require every position/focus component to differ by less than one world
  unit. The report adapter must reject equal ELF hashes, equal ISO hashes,
  declared/raw role mismatch, route-manifest digest mismatch, decoded JSON
  disagreement, a missing raw SCC1 byte window, a missing raw SBR4 byte
  window, or disagreement between raw SBR4 and the SCC route anchor.

- [ ] **Step 6: Add mutations for all contract claims**

  Add focused mutations for truncated/oversized data, each header word,
  one later sample state word, non-neutral input, non-finite float, missing
  flag, route ID, zoom witness, Q generation, bridge counts, same-image roles,
  and raw/decoded disagreement.

  In `compare_camera_route_reports.py`, import the existing immutable
  `load_route`, `decode_probe`, `validate_artifacts`, `REJECT_FIELDS`, and
  exact-field constants rather than copying the SBR4 layout. Accept exactly
  four reports ordered as baseline run1/run2 and Q run1/run2. Require:

  - roles `camera-baseline`, `camera-baseline`, `camera-q`, `camera-q`;
  - raw SBR4 atan2 variant 2 in all four reports;
  - ELF absolute camera markers 1 for baseline and 2 for Q, route markers
    matching the selected manifest, and distinct baseline/Q ELF and image
    hashes;
  - independently raw-decoded SBR4 windows with identical non-timing
    behavior/output fields within each role; timing/provenance may differ;
  - exact global timer, action, face angles, camera mode, triangle fields,
    every renderer reject/fault/output field, and sub-one-unit final Mario and
    camera position divergence for each cross-role pair;
  - when requested, Q `sim_frt_ticks_accum` strictly lower for run1/run1 and
    run2/run2, while render/wait/slave counters remain reported but are not
    acceptance metrics.

  Tests must prove the unchanged `compare_route_reports.py` still rejects
  camera roles and that the new comparator rejects swapped roles, atan2 1,
  wrong/missing ELF markers, same-role non-timing drift, one raw/decoded mutation,
  one reject-field mutation, and a non-improving simulation pair.

  Run all three test files:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_idle_capture.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_camera_route_reports.py
  ```

- [ ] **Step 7: Commit the host SCC1 contract**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add tools/saturn/camera_idle_contract.py tools/saturn/test_camera_idle_contract.py tools/saturn/verify_camera_idle_capture.py tools/saturn/test_verify_camera_idle_capture.py tools/saturn/compare_camera_route_reports.py tools/saturn/test_compare_camera_route_reports.py
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: define raw Saturn camera idle contract"
  ```

---

### Task 3: Add the Semantic Camera Probe and Replay-Only SCC1 Transport

**Files:**

- Create: `src/port/saturn/runtime/saturn_camera_probe.h`
- Create: `src/port/saturn/sourceboot/source_camera_idle_probe.h`
- Create: `src/port/saturn/sourceboot/source_camera_idle_probe.c`
- Create: `tools/saturn/capture_camera_idle.py`
- Create: `tools/saturn/test_capture_camera_idle.py`
- Create: `tools/saturn/verify_sourceboot_memory_map.py`
- Create: `tools/saturn/test_verify_sourceboot_memory_map.py`
- Create: `tools/saturn/compare_sh2_native_math_audit_reports.py`
- Create: `tools/saturn/test_compare_sh2_native_math_audit_reports.py`
- Create after measurement:
  `tools/saturn/fixtures/bob_camera_memory_v1.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json`
- Create after independent review:
  `docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json`
- Create after reviewed parser correction:
  `docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json`
- Create after measurement:
  `docs/saturn/evidence/reports/task3-camera-memory-transport-2026-07-29.json`
- Create after target proof:
  `docs/saturn/evidence/reports/task3-camera-transport-capture-2026-07-29.json`
- Create after target proof:
  `docs/saturn/evidence/reports/task3-camera-transport-scc1-2026-07-29.json`
- Modify: `src/game/camera.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `src/port/saturn/sourceboot/source_cart.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Modify: `tools/saturn/test_camera_idle_contract.py`
- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify exactly once after the parser review gate:
  `tools/saturn/sh2_native_math_sim_audit_contract_v2.txt`

**Interfaces:**

- Consumes: Task 1's route/runtime telemetry and Task 2's SCC1 decoder.
- Produces:
  `sm64_saturn_camera_probe_read(sm64_saturn_camera_probe_snapshot_t *)`,
  `sm64_saturn_sourceboot_camera_idle_probe_reset(void)`,
  `sm64_saturn_sourceboot_camera_idle_probe_record(const
  sm64_saturn_source_runtime_state_t *, uint32_t source_tick)`, the
  `sourceboot_camera_idle_capture` symbol, and the capture CLI defined in Step
  7. The memory verifier produces a hash-bound phase report and the selected
  8- or 4-sector camera-artifact staging fixture.
  The native-math verifier additionally produces schema-1 legacy/code-only
  observation JSON. The audit-report comparator produces the reviewed,
  finalized v2 re-pin report and refuses unequal corrected layout facts.

- [ ] **Step 0: Correct and deliberately re-pin the linked-ELF audit before transport**

  This is a two-phase prerequisite. The current branch worktree already
  contains partial, unstaged Task 3 transport work. Do not author the parser
  there and do not build evidence from it. Record the control commit and
  create a clean detached authoring worktree under the repository's ignored
  `.worktrees` area:

  ```powershell
  $repoRoot = "D:/Code/RetroDev/sm64-saturn-port/sm64-port"
  $controlWorktree = "$repoRoot/.worktrees/sh2-native-math-purge"
  $parserBase = (git -C $controlWorktree -c "safe.directory=$controlWorktree" rev-parse HEAD).Trim()
  $parserBaseShort = $parserBase.Substring(0, 7)
  $parserAuthoring = "$repoRoot/.worktrees/audit-parser-author-$parserBaseShort"
  if (Test-Path $parserAuthoring) { throw "parser authoring worktree already exists: $parserAuthoring" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" worktree add --detach $parserAuthoring $parserBase
  if (git -C $parserAuthoring -c "safe.directory=$parserAuthoring" status --porcelain) { throw "parser authoring worktree is dirty" }
  $saturnPython = "$controlWorktree/.venv-saturn-tools/Scripts/python.exe"
  ```

  Every parser edit, focused test, and parser commit in phase A runs with
  `$parserAuthoring` as its working directory. Do not change `camera.c`, SCC
  transport, cart staging, the sourceboot Makefile, or the v2 contract in
  this authoring worktree.

  First extend the current linear verifier with
  `--audit-observation-only --json-output PATH --analysis-mode
  legacy-linear`. This mode bypasses only the v2 expected-total comparison;
  it still validates the pinned contract/oracle digests, expected root,
  forbidden callers, ELF readability, and JSON schema. Its sorted schema is:

  ```text
  schema_version: 1
  analysis_mode: "legacy-linear" | "code-only"
  producer_commit, parser_sha256, elf_sha256, route_oracle_sha256
  contract_before_sha256, contract_before_expected_total
  root
  closure_functions: [symbol, ...]
  direct_call_facts:
    [{caller, caller_offset, callee, callee_offset, count}, ...]
  helper_call_facts:
    [{caller, caller_offset, helper, helper_offset, count}, ...]
  helper_total
  unresolved_indirect_transfers:
    [{caller, caller_offset, mnemonic}, ...]
  unresolved_effects:
    [{function, instruction_offset, mnemonic, operands, reason}, ...]
  ```

  `caller_offset` and `callee_offset` are nonnegative symbol-relative byte
  offsets serialized as JSON numbers. Closure/call arrays contain only facts
  whose caller belongs to the audit root's derived closure; rows are sorted by
  their displayed fields. `--producer-commit` requires a full lowercase
  40-hex SHA equal to `git rev-parse HEAD` in the producer worktree and is
  recorded verbatim. Observation-only is mutually exclusive with normal
  acceptance and never appears in a Makefile target. Add tests proving it
  cannot suppress a forbidden-caller, bad-root, digest, producer-commit, or
  malformed-ELF failure.

  Do not build either layout probe yet. Both legacy and corrected observations
  are produced later from the same clean detached evidence worktree at the
  reviewed parser commit.

  Observe the parser RED tests in `$parserAuthoring` before changing analysis
  behavior. Independent
  synthetic fixtures must prove:

  - the `_find_floor` pool range `0x0600B040..0x0600B07B` contributes neither
    route 0's decoded `bra` nor route 1's fake
    `bsr ... <_load_static_surfaces+0x9a>`;
  - a pool-decoded `extu.b r10,r8` cannot clear a real literal-loaded `r8`
    and hide four later `jsr @r8` calls to `___mulsf3`;
  - real `bsr` targets `___movmemSI52+0x2` and
    `div0+0x6/+0x8/+0x18` remain distinct internal-offset call facts;
  - `bt`, `bf`, `bt/s`, `bf/s`, `bra`, `bsr`, `jsr`, `rts`, and `rte`
    take the correct fallthrough/target successors and execute exactly one
    delay slot where SH requires it;
  - a literal-resolved `jmp @rN` tail call and a
    `mova`/indexed-`mov.w`/`braf` switch reach their real successors;
  - a disconnected non-entry decoded-line seed starts with every register
    `UNKNOWN` and cannot inherit a symbol loaded on an entry path;
  - merging a known symbol with `UNKNOWN` or a different symbol produces
    `UNKNOWN`, so a stale call target cannot survive a join;
  - `jsr`, `bsr`, and `bsrf` kill `r0-r7`, `pr`, `mach`, and `macl` after
    their delay slots while preserving unwritten `r8-r15`;
  - a recognized but otherwise unmodeled register-writing instruction kills
    its destination;
  - an unparseable register effect produces `unresolved_effect` and fails;
  - a loop-carried signed and unsigned interval widens each expanding bound
    once and reaches a fixed point;
  - signed and unsigned branch predicates refine compatible sets/intervals,
    discard empty paths, and never reinterpret signedness;
  - an interval containing more than 256 possible computed targets is
    rejected as unresolved, while a bounded 256-or-fewer-entry jump table is
    enumerated only when every target is aligned owned executable code;
  - an unresolved `jmp`, `braf`, or `bsrf` in an audited function is reported
    and rejected rather than followed linearly;
  - readelf fixtures cover a nonzero function, a zero-size function ending at
    the next function/section boundary, same-start aliases with deterministic
    canonicalization, rejected same-start/different-end ownership, a rejected
    partial overlap, a filtered `end_sequence`/one-past decoded-line row, and
    an absent line table whose entry CFG passes only when it has no unresolved
    diagnostic.

  Run:

  ```powershell
  Push-Location $parserAuthoring
  & $saturnPython tools/saturn/test_verify_sh2_native_math.py
  Pop-Location
  ```

  Expected RED: fake pool instructions appear as calls/clobbers, delay/switch
  reachability records do not exist, and layout facts differ.

  Implement `analysis-mode=code-only` as a per-function SH control-flow and
  abstract-state work list with separate symbol ownership, code-address
  discovery, and dataflow phases.

  Parse `sh-elf-readelf -SW` first. Only sections with `SHF_EXECINSTR` may own
  code. From `sh-elf-readelf -sW`, accept only `STT_FUNC` symbols whose
  `st_shndx` names one of those sections. A nonzero symbol owns
  `[st_value, st_value + st_size)`. A zero-size symbol ends at the next
  greater function address in the same section, or that section's end.
  Reject any derived range outside its executable section. Same-start
  symbols are aliases only if their effective ends agree; then choose one
  canonical symbol by the exact tuple
  `(binding rank GLOBAL=0/WEAK=1/LOCAL=2, visibility rank
  DEFAULT/PROTECTED=0/HIDDEN/INTERNAL=1, name byte length, UTF-8 name bytes)`;
  retain the other names in a sorted alias list. Reject different effective
  ends at one start and partial overlap between ranges with different starts.

  Parse `sh-elf-readelf --debug-dump=decodedline` only after ranges exist.
  Keep only two-byte-aligned row addresses strictly inside one canonical
  executable function range. Ignore `end_sequence` rows and addresses equal
  to a function or section end. A function entry is always a code seed. Each
  retained non-entry line row is an independent code seed for disconnected
  switch/case code; it does not inherit entry-path state. If a function has no
  usable line rows, analyze its entry CFG alone and reject any unresolved
  indirect transfer or effect.

  Parse objdump rows into address/bytes/mnemonic/operand records but do not
  call a row an instruction until structural code discovery reaches it from
  a seed or a proven successor. Reconstruct the addressed `.text` bytes from
  those rows. Dataflow runs only over discovered code and may add a resolved
  indirect successor only inside a proven function range; arbitrary objdump
  rows never become code merely because they were decoded. Every entry and
  non-entry seed starts with all general registers, `pr`, `mach`, `macl`, and
  fixed `r15` spill slots `UNKNOWN`.

  The finite abstract domain for each register and fixed spill slot is:

  - `UNREACHED` bottom;
  - `UNKNOWN` top;
  - `ConstSet` containing integer constants with a signed/unsigned
    interpretation tag, or containing symbol-address atoms; or
  - typed 32-bit `Interval(signed|unsigned, lo, hi)`.

  Symbol atoms are never coerced into numeric intervals. At a join, identical
  atoms survive and `ConstSet` values union exactly through 256 members. An
  oversized all-integer set becomes its same-signedness hull interval.
  An oversized symbol set, mixed symbol/integer set, or incompatible
  signedness becomes `UNKNOWN`. Interval joins use the compatible typed hull;
  an interval plus a compatible integer set also uses the hull; every other
  combination becomes `UNKNOWN`.

  Branch refinement intersects a `ConstSet` or interval with the proved
  signed/unsigned predicate; an empty intersection discards that path. There
  is no narrowing phase. At a CFG backedge, the first expanding state joins
  normally. On its next expansion, widening sends an expanding lower or
  upper bound to the corresponding 32-bit type minimum or maximum. Each bound
  widens at most once. Apply join/widening per instruction address until no
  abstract state changes; never schedule `UNREACHED`. The finite CFG,
  256-member exact-set cap, finite interval endpoints, and one-time widening
  of each bound guarantee termination.

  Modeled PC-relative symbol loads, register moves, and fixed `r15`
  spill/reloads propagate source facts. Every other recognized
  register-writing instruction kills its destination. An unparseable
  destination or unknown register effect emits `unresolved_effect` and fails
  an audited closure. `jsr`, `bsr`, and `bsrf` resolve their target from the
  pre-delay state, execute the one delay slot, then kill the SH ABI
  caller-clobbered set `r0-r7`, `pr`, `mach`, and `macl` before their return
  successor; unwritten `r8-r15` survive.

  Model only affine constant operations over compatible integer sets and
  typed intervals. Unsupported arithmetic kills its destination to
  `UNKNOWN`; a transfer that needs that value fails unresolved. Model the
  audited switch idioms' `mov #imm`, `and #imm`, `add`,
  shifts/extensions, `mova`, PC-relative `mov.w`/`mov.l`, indexed byte/word
  loads, and the signed/unsigned refinements for `cmp/eq`, `cmp/hs`,
  `cmp/hi`, `cmp/ge`, `cmp/gt`, and `tst`: the `___ashrsi3` mask produces
  `0..31` and the `_render_dialog_entries` `cmp/hi` fallthrough produces
  `0..3`, so each indexed table load has a finite target set. Enumerate a
  computed jump only when its post-refinement `ConstSet` or interval contains
  at most 256 two-byte-aligned targets and every target belongs to owned
  executable code. An oversized, misaligned, non-enumerable, or non-owned set
  is an unresolved-transfer failure, never a linear fallback. Model ordinary,
  conditional, unconditional, call, return, delayed, and computed successors
  named by the tests above. Resolve a delayed transfer target from the
  pre-slot state, execute exactly one slot, and propagate the post-slot state
  to its target/fallthrough. Map a reached direct target address to the
  containing function and retain its nonzero offset; never require exact
  symbol-entry targets and never strip an offset before source-code
  reachability is known.

  Add `--readelf PATH`; it is mandatory for ordinary linked-ELF verification,
  observation, and v3 generation because those modes build a linked call
  graph. It is forbidden in `--object-reference-only`, which compares object
  manifests and canonical object disassembly without constructing a linked
  graph. Tests require omission in object-reference-only and reject supplying
  it there. Every absolute SH-tool child environment prepends
  `C:\msys64\usr\bin` without mutating its parent. Ordinary verification
  always selects `code-only`; normal acceptance rejects `legacy-linear`.
  Sourceboot Makefile integration is deferred until the reviewed re-pin is
  fast-forwarded into the control worktree, so the parser/test commit contains
  no transport-overlapping Makefile diff.

  Create `compare_sh2_native_math_audit_reports.py` with `compare`,
  `finalize`, and read-only `verify-final` subcommands and synthetic tests
  before committing the parser. `verify-final --report PATH` reloads every
  committed repository-relative path named by the final report and rejects
  any absolute or repository-escaping path, hash, parser-commit,
  reviewed-range, reviewed-file inventory, contract, or fact mismatch.
  Temp-repository fixtures additionally prove that changed post-v3 current
  verifier bytes with preserved v2 facts pass, while a historical
  `git show` hash mismatch, non-ancestor `repin_source_commit`, dirty or
  substituted evidence, and current v2 fact drift each fail.
  Run both focused suites in the clean authoring worktree, then commit exactly
  the four parser-owned files:

  ```powershell
  Push-Location $parserAuthoring
  & $saturnPython tools/saturn/test_verify_sh2_native_math.py
  & $saturnPython tools/saturn/test_compare_sh2_native_math_audit_reports.py
  git -c "safe.directory=$parserAuthoring" add -- tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py tools/saturn/compare_sh2_native_math_audit_reports.py tools/saturn/test_compare_sh2_native_math_audit_reports.py
  git -c "safe.directory=$parserAuthoring" commit -m "fix: harden linked SH native math parser"
  $parserCommit = (git -c "safe.directory=$parserAuthoring" rev-parse HEAD).Trim()
  $parserCommitShort = $parserCommit.Substring(0, 7)
  $expectedParserFiles = @(
    "tools/saturn/compare_sh2_native_math_audit_reports.py",
    "tools/saturn/test_compare_sh2_native_math_audit_reports.py",
    "tools/saturn/test_verify_sh2_native_math.py",
    "tools/saturn/verify_sh2_native_math.py"
  )
  $actualParserFiles = @(git -c "safe.directory=$parserAuthoring" diff --name-only $parserBase $parserCommit | Sort-Object)
  if (Compare-Object ($expectedParserFiles | Sort-Object) $actualParserFiles) { throw "parser commit owns unexpected files" }
  $authorStatus = @(git -c "safe.directory=$parserAuthoring" status --porcelain)
  if ($authorStatus.Count -ne 0) { throw "parser authoring worktree is dirty after commit" }
  Pop-Location
  ```

  Only after that parser/test commit exists, create a second detached evidence
  worktree at that exact commit SHA. It is the sole producer of both layout
  ELFs and every re-pin evidence file:

  ```powershell
  $evidenceWorktree = "$repoRoot/.worktrees/audit-parser-evidence-$parserCommitShort"
  if (Test-Path $evidenceWorktree) { throw "parser evidence worktree already exists: $evidenceWorktree" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" worktree add --detach $evidenceWorktree $parserCommit
  if (git -C $evidenceWorktree -c "safe.directory=$evidenceWorktree" status --porcelain) { throw "parser evidence worktree is dirty before build" }
  if ((git -C $evidenceWorktree -c "safe.directory=$evidenceWorktree" rev-parse HEAD).Trim() -ne $parserCommit) { throw "parser evidence worktree is at wrong commit" }

  $transportDiffPaths = @(
    "src/game/camera.c",
    "src/port/saturn/runtime/saturn_camera_probe.h",
    "src/port/saturn/sourceboot/source_camera_idle_probe.h",
    "src/port/saturn/sourceboot/source_camera_idle_probe.c",
    "src/port/saturn/sourceboot/main.c",
    "src/port/saturn/sourceboot/source_cart.c",
    "src/port/saturn/sourceboot/Makefile",
    "src/port/saturn/sourceboot/sourceboot-cart.x",
    "tools/saturn/capture_camera_idle.py",
    "tools/saturn/test_capture_camera_idle.py",
    "tools/saturn/verify_sourceboot_memory_map.py",
    "tools/saturn/test_verify_sourceboot_memory_map.py",
    "tools/saturn/test_camera_idle_contract.py"
  )
  $transportDiff = @(git -C $evidenceWorktree -c "safe.directory=$evidenceWorktree" diff --name-only $parserBase $parserCommit -- $transportDiffPaths)
  if ($transportDiff.Count -ne 0) { throw "parser commit contains Task 3 transport diff: $($transportDiff -join ', ')" }
  $transportOnlyFiles = @(
    "src/port/saturn/runtime/saturn_camera_probe.h",
    "src/port/saturn/sourceboot/source_camera_idle_probe.h",
    "src/port/saturn/sourceboot/source_camera_idle_probe.c",
    "tools/saturn/capture_camera_idle.py",
    "tools/saturn/test_capture_camera_idle.py",
    "tools/saturn/verify_sourceboot_memory_map.py",
    "tools/saturn/test_verify_sourceboot_memory_map.py"
  )
  foreach ($relative in $transportOnlyFiles) {
    if (Test-Path (Join-Path $evidenceWorktree $relative)) { throw "transport-only file exists in clean parser evidence worktree: $relative" }
  }
  ```

  Build the two pure-layout probes serially in that clean detached worktree,
  without `verify`, because the known-bad v2 total is not yet an acceptance
  gate. Convert only the validated evidence path for the MSYS command:

  ```powershell
  $evidenceMsys = ($evidenceWorktree -replace "\\", "/") -replace "^D:", "/d"
  $auditBuildBase = "cd $evidenceMsys && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j1 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  $auditRoute0Command = "$auditBuildBase SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_SOURCE_CART_STAGE_SECTORS=16"
  $auditRoute1Command = "$auditBuildBase SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=16"
  C:/msys64/usr/bin/bash.exe -lc $auditRoute0Command
  C:/msys64/usr/bin/bash.exe -lc $auditRoute1Command

  $auditRoute0Output = "$evidenceWorktree/build/saturn/sourceboot/e2-bob-demo-replay-camroute0-atan2v2-camv1-stage16-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $auditRoute1Output = "$evidenceWorktree/build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle0-disc0-range0-stage16-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $auditRoute0Elf = (Resolve-Path "$auditRoute0Output/obj/sm64-saturn-sourceboot-e2.elf").Path
  $auditRoute1Elf = (Resolve-Path "$auditRoute1Output/obj/sm64-saturn-sourceboot-e2.elf").Path
  $auditObjdump = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe"
  $auditReadelf = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe"
  $auditAddr2line = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-addr2line.exe"
  $legacyRoute0 = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json"
  $legacyRoute1 = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json"
  $correctedRoute0 = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json"
  $correctedRoute1 = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json"
  $proposal = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json"
  $reviewRecord = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json"
  $finalRepin = "$evidenceWorktree/docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json"
  ```

  Emit all four durable observations from the same two ELFs and the same
  parser commit. Scratch copies are optional and never authoritative:

  ```powershell
  Push-Location $evidenceWorktree
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute0Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line --audit-observation-only --analysis-mode legacy-linear --producer-commit $parserCommit --json-output $legacyRoute0
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute1Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line --audit-observation-only --analysis-mode legacy-linear --producer-commit $parserCommit --json-output $legacyRoute1
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute0Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line --audit-observation-only --analysis-mode code-only --producer-commit $parserCommit --json-output $correctedRoute0
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute1Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line --audit-observation-only --analysis-mode code-only --producer-commit $parserCommit --json-output $correctedRoute1
  Pop-Location
  ```

  The already committed comparator's `compare` subcommand accepts the four
  observations below, requires distinct route ELF hashes but identical
  corrected parser/oracle/contract-before hashes, closure functions, direct
  call facts, helper facts, helper total, and empty unresolved-transfer and
  unresolved-effect lists. It writes a proposal containing both legacy
  totals; the one corrected
  total; the complete corrected closure, normalized direct-call, and helper
  arrays; sorted per-route added/removed direct/helper facts; full
  `parser_base_commit`, `parser_commit`, and reviewed range; the exact
  pre-build isolation result (evidence worktree basename, clean detached HEAD,
  empty transport diff, and absent transport-only files); and the relative
  repository path plus SHA-256 of every input. The comparator rejects an
  absolute or repository-escaping input path. Its tests reject false/nonzero
  isolation claims and a worktree basename inconsistent with the parser SHA:

  ```powershell
  Push-Location $evidenceWorktree
  & $saturnPython tools/saturn/test_compare_sh2_native_math_audit_reports.py
  & $saturnPython tools/saturn/compare_sh2_native_math_audit_reports.py compare --legacy-route0 $legacyRoute0 --legacy-route1 $legacyRoute1 --corrected-route0 $correctedRoute0 --corrected-route1 $correctedRoute1 --contract-before tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --parser-base-commit $parserBase --parser-commit $parserCommit --producer-worktree-name (Split-Path $evidenceWorktree -Leaf) --isolation-clean-before-build true --transport-diff-count $transportDiff.Count --transport-only-present-count 0 --output $proposal
  Pop-Location
  ```

  Expected: PASS only when corrected route-0/route-1 facts are byte-for-byte
  equal after deterministic JSON serialization. At this point stop. Do not
  edit `EXPECTED_TOTAL` or `SIM_AUDIT_CONTRACT_V2_SHA256`. The controller
  dispatches an independent reviewer for the verifier, tests, four
  observations, and proposal. The evidence worktree and both build trees stay
  in place and are not rebuilt during review. A clean reviewer writes the
  committed-path candidate `$reviewRecord` with exact keys:

  ```text
  schema_version: 1
  verdict: "clean"
  parser_base_commit
  parser_commit
  reviewed_range: parser_base_commit + ".." + parser_commit
  proposal_sha256
  reviewed_files:
    - tools/saturn/compare_sh2_native_math_audit_reports.py
    - tools/saturn/test_compare_sh2_native_math_audit_reports.py
    - tools/saturn/test_verify_sh2_native_math.py
    - tools/saturn/verify_sh2_native_math.py
    - docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json
    - docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json
    - docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json
    - docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json
    - docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json
  findings: []
  ```

  The comparator requires full 40-hex commit IDs, the exact range string,
  `proposal_sha256` equal to the proposal file hash, and a sorted
  nine-element reviewed-file inventory matching those exact inputs. The
  review record is durable evidence; an ignored reviewer note may supplement
  it but cannot replace it.

  Phase B begins only after that clean review. Read
  `corrected_helper_total` from the reviewed proposal. With `apply_patch`,
  replace the single `EXPECTED_TOTAL` value in
  `$evidenceWorktree/tools/saturn/sh2_native_math_sim_audit_contract_v2.txt`
  exactly once. Compute:

  ```powershell
  $newV2Digest = (Get-FileHash "$evidenceWorktree/tools/saturn/sh2_native_math_sim_audit_contract_v2.txt" -Algorithm SHA256).Hash.ToLowerInvariant()
  $newV2Digest
  ```

  With `apply_patch`, replace the single
  `SIM_AUDIT_CONTRACT_V2_SHA256` value with that exact printed digest. Do not
  edit either value again. Run the focused suites and both ordinary v2 audits
  before committing:

  ```powershell
  Push-Location $evidenceWorktree
  & $saturnPython tools/saturn/test_verify_sh2_native_math.py
  & $saturnPython tools/saturn/test_compare_sh2_native_math_audit_reports.py
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute0Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  & $saturnPython tools/saturn/verify_sh2_native_math.py $auditRoute1Elf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  Pop-Location
  ```

  Commit the historical re-pin source before generating its final report. The
  tree at this commit already contains the corrected parser, comparator,
  their tests, all reviewed observations, proposal/review, deliberately
  re-pinned v2 contract, and the verifier bytes carrying that contract pin.
  No Task 3 transport or Makefile path is staged:

  ```powershell
  Push-Location $evidenceWorktree
  git -c "safe.directory=$evidenceWorktree" add -- tools/saturn/verify_sh2_native_math.py tools/saturn/sh2_native_math_sim_audit_contract_v2.txt docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json
  git -c "safe.directory=$evidenceWorktree" commit -m "test: repin corrected SH native math source"
  $repinSourceCommit = (git -c "safe.directory=$evidenceWorktree" rev-parse HEAD).Trim()
  $expectedRepinSourceFiles = @(
    "docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json",
    "docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json",
    "tools/saturn/sh2_native_math_sim_audit_contract_v2.txt",
    "tools/saturn/verify_sh2_native_math.py"
  )
  $actualRepinSourceFiles = @(git -c "safe.directory=$evidenceWorktree" diff --name-only $parserCommit $repinSourceCommit | Sort-Object)
  if (Compare-Object ($expectedRepinSourceFiles | Sort-Object) $actualRepinSourceFiles) { throw "re-pin source commit owns unexpected files" }
  if (git -c "safe.directory=$evidenceWorktree" status --porcelain --untracked-files=no) { throw "tracked evidence worktree is dirty at re-pin source commit" }
  Pop-Location
  ```

  A commit cannot embed its own SHA, so only now run `finalize` with the exact
  `repin_source_commit`. It rejects a missing/unclean review, proposal hash
  drift, a new contract total unequal to the corrected total, unchanged
  old/new contract digests, a verifier pin unequal to the new file digest,
  any changed observation, or a source commit that is not the current HEAD.
  It reads and hashes historical bytes with
  `git show <repin_source_commit>:<path>` rather than trusting working-tree
  substitutes. It writes this durable schema:

  ```text
  schema_version: 1
  status: "reviewed-repin-final"
  repin_source_commit
  legacy: {route0_total, route1_total}
  corrected: {helper_total, closure_count, direct_call_facts_sha256,
              helper_call_facts_sha256}
  delta: {route0: {added, removed}, route1: {added, removed}}
  contract: {old_expected_total, old_sha256,
             new_expected_total, new_sha256}
  review: {parser_base_commit, parser_commit, reviewed_range,
           reviewed_files, proposal_sha256, approval_record_sha256, verdict}
  inputs:
    [{path, sha256} for all four observations, proposal, and review record]
  historical:
    parser_verifier: {path, sha256}
    comparator: {path, sha256}
    tests: [{path, sha256}, ...]
    contract: {path, sha256}
    references: [{path, sha256}, ...]
  artifacts:
    {route0_elf_relative_path, route0_elf_sha256,
     route1_elf_relative_path, route1_elf_sha256,
     analysis_parser_sha256}
  ```

  Historical paths are exactly
  `tools/saturn/verify_sh2_native_math.py`,
  `tools/saturn/compare_sh2_native_math_audit_reports.py`,
  `tools/saturn/test_verify_sh2_native_math.py`,
  `tools/saturn/test_compare_sh2_native_math_audit_reports.py`, and
  `tools/saturn/sh2_native_math_sim_audit_contract_v2.txt`; historical
  reference paths are
  `tools/saturn/sh2_native_math_baseline_v1.txt`,
  `tools/saturn/sh2_native_math_route_oracle_v1.txt`, and
  `tools/saturn/sh2_native_math_sim_route_oracle_v1.txt`. ELF paths are
  relative to the retained evidence worktree and cannot escape it.

  ```powershell
  Push-Location $evidenceWorktree
  & $saturnPython tools/saturn/compare_sh2_native_math_audit_reports.py finalize --repin-source-commit $repinSourceCommit --proposal $proposal --review $reviewRecord --legacy-route0 $legacyRoute0 --legacy-route1 $legacyRoute1 --corrected-route0 $correctedRoute0 --corrected-route1 $correctedRoute1 --contract-after tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --verifier tools/saturn/verify_sh2_native_math.py --output $finalRepin
  Pop-Location
  ```

  Commit only the finalized report in the required subsequent commit:

  ```powershell
  Push-Location $evidenceWorktree
  git -c "safe.directory=$evidenceWorktree" add -- docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json
  git -c "safe.directory=$evidenceWorktree" commit -m "test: record reviewed SH native math re-pin"
  $repinReportCommit = (git -c "safe.directory=$evidenceWorktree" rev-parse HEAD).Trim()
  $reportCommitFiles = @(git -c "safe.directory=$evidenceWorktree" diff --name-only $repinSourceCommit $repinReportCommit)
  if ($reportCommitFiles.Count -ne 1 -or $reportCommitFiles[0] -ne "docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json") { throw "final report commit owns unexpected files" }
  if (git -c "safe.directory=$evidenceWorktree" status --porcelain --untracked-files=no) { throw "tracked evidence worktree state is dirty after commit" }
  Pop-Location
  ```

  `verify-final` now requires `repin_source_commit` to be a full 40-hex
  ancestor of both the report commit and current HEAD. It derives the report
  commit as the commit that introduced the report path, requires its
  `repin_source_commit..report_commit` diff to contain only that report, and
  hashes every historical parser/verifier/test/comparator/contract/reference
  path with `git show <repin_source_commit>:<path>`. It reloads every
  observation, proposal, and review from both the source and current commits,
  requires identical blob hashes matching the report, and requires the
  current v2 contract, comparator, comparator tests, baseline, and oracle
  blobs to equal their historical hashes. It also requires the current report
  blob to equal the introduced report blob. It validates the exact reviewed
  inventory and rejects dirty tracked evidence, dirty current
  verifier/contract/reference paths, substituted files, or ELF hash drift.

  Separately, it executes the current verifier twice in ordinary v2 mode and
  twice in code-only observation mode against the retained route ELFs. It
  compares the current observations' complete closure, normalized
  direct-call facts, helper facts, helper total, and empty unresolved lists
  with the frozen corrected observations. Current verifier byte equality to
  the historical Task 3 SHA is deliberately not required. The comparator
  tests cover: a post-v3 current-verifier byte change with preserved v2 facts
  passes; historical `git show` hash mismatch fails; a non-ancestor source
  commit fails; and current v2 behavioral drift fails. The
  `verify-final` subcommand, comparator helper, comparator tests, and report
  schema are the stable compatibility interface: their current hashes must
  equal their `repin_source_commit` hashes through Task 15. Task 8 extends the
  current verifier, not this helper or its tests.

  Run the historical and current compatibility gate after the report commit:

  ```powershell
  Push-Location $evidenceWorktree
  & $saturnPython tools/saturn/test_compare_sh2_native_math_audit_reports.py
  & $saturnPython tools/saturn/compare_sh2_native_math_audit_reports.py verify-final --report $finalRepin --current-verifier tools/saturn/verify_sh2_native_math.py --current-v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --baseline tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --route0-elf $auditRoute0Elf --route1-elf $auditRoute1Elf --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  Pop-Location
  ```

  Fast-forward the control branch only if no other commit has moved it. This
  updates parser/contract/evidence paths but leaves all pre-existing unstaged
  Task 3 transport files untouched:

  ```powershell
  if ((git -C $controlWorktree -c "safe.directory=$controlWorktree" rev-parse HEAD).Trim() -ne $parserBase) { throw "control branch moved during isolated parser work" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" merge --ff-only $repinReportCommit
  if ((git -C $controlWorktree -c "safe.directory=$controlWorktree" rev-parse HEAD).Trim() -ne $repinReportCommit) { throw "control branch did not fast-forward to final report commit" }
  ```

  Now, and only now, add the sourceboot integration to the already-dirty
  control-worktree Makefile with `apply_patch`:

  ```make
  SOURCEBOOT_SH_READELF := $(YAUL_INSTALL_ROOT)/bin/$(YAUL_PROG_SH_PREFIX)-readelf
  ```

  Pass `--readelf "$(SOURCEBOOT_SH_READELF)"` to every ordinary linked-ELF
  v2/v3 verification or generation call. Do not pass it to
  `--object-reference-only`; its exact argument contract omits `--readelf`
  and tests accept omission while rejecting its presence. Add
  `test_verify_sh2_native_math.py` and
  `test_compare_sh2_native_math_audit_reports.py` to the serial host
  prerequisites of `verify`. This Makefile hunk is committed with the existing
  transport slice in Step 9, not retroactively included in either clean
  parser/evidence commit.

  Remove the clean authoring worktree after the fast-forward:

  ```powershell
  $authorStatus = @(git -C $parserAuthoring -c "safe.directory=$parserAuthoring" status --porcelain)
  if ($authorStatus.Count -ne 0) { throw "refusing to remove dirty parser authoring worktree" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" worktree remove $parserAuthoring
  ```

  Retain `$evidenceWorktree`, its ignored build outputs, and its registered
  worktree metadata unchanged through final evidence review in Task 15. Do
  not rebuild either layout probe. Cleanup is defined in Task 15 only after
  every committed hash is revalidated.

- [ ] **Step 1: Write failing transport and symbol-resolution tests**

  In `test_capture_camera_idle.py`, use a fake `sh-elf-nm` output and fake
  Ymir client to require:

  - both `sourceboot_camera_idle_capture` and
    `sourceboot_route_checkpoint` are resolved from the sibling ELF, never
    supplied as hand-entered stale addresses;
  - `g_sm64_saturn_source_cart_probe` resolves from that same ELF and its
    exact 28 raw bytes are read from the same paused target instance;
  - reads are requested in chunks no larger than 65,536 bytes and concatenated
    in order;
  - the final chunk is bounded to the exact remaining byte count;
  - short, missing, repeated, or oversized chunks fail;
  - the resulting raw byte array has exactly 194,496 bytes;
  - the independent raw SBR4 window is exactly 160 bytes, decodes to replay
    tick 2,000, and agrees with the SCC header's route
    magic/version/tick anchor;
  - the sibling ELF absolute camera/route markers match the declared role and
    manifest;
  - the `sh-elf-nm` subprocess environment prepends
    `C:\msys64\usr\bin` without changing the parent environment;
  - ELF, CUE, and ISO identities are recorded before decoding;
  - the SCAR probe is READY with status OK, expected/copied sizes equal the
    exact sibling `SOURCE.DAT` byte length, and the report records that file's
    SHA-256;
  - `frame_serial`, all eight SBR4 timing fields, host wall-clock seconds,
    observed emulated VBlank rate, and emulation-speed ratio are preserved as
    provenance but excluded from same-role SCC byte equality.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_capture_camera_idle.py
  ```

  Expected failure: `capture_camera_idle` is absent.

- [ ] **Step 2: Add the semantic snapshot ABI and baseline packer**

  Add the probe structs from the interface map to
  `saturn_camera_probe.h`. In `camera.c`, implement
  `sm64_saturn_camera_probe_read` under `TARGET_SATURN`. Use explicit
  helpers:

  ```c
  static u32 saturn_camera_probe_f32_bits(f32 value)
  {
      union {
          f32 value;
          u32 bits;
      } raw;
      raw.value = value;
      return raw.bits;
  }

  static u32 saturn_camera_probe_pack_s16(s16 high, s16 low)
  {
      return ((u32)(u16)high << 16) | (u32)(u16)low;
  }
  ```

  Pack every approved field explicitly into snapshot indices 0 through 76.
  Indices must follow SCC words 4 through 80, not physical struct order.
  Baseline diagnostics and generation are zero. Camera flags contribute only
  `gCamera != NULL`, no cutscene, and no active transition; sourceboot adds
  replay, neutral-input, and Mario-quiescent flags. Initialize the out-of-band
  `source_dispatch` to zero in Task 3 and prove the SCC packer never includes
  it; Task 9 sets it to Mario only after that source tick has completed a
  valid Q Mario seam and otherwise leaves it `NONE`. SQT1 therefore observes
  Q-active dispatch, not merely the pre-dispatch selection.

  Add an offset-by-offset host test that extracts the packer mapping and fails
  if an index is missing or assigned twice. Rerun
  `test_camera_idle_contract.py`.

- [ ] **Step 3: Implement fixed and discovery SCC1 recording**

  In `source_camera_idle_probe.c`, own one exact raw block:

  ```c
  #define SOURCEBOOT_SCC1_HEADER_WORDS 24U
  #define SOURCEBOOT_SCC1_SAMPLE_WORDS 81U
  #define SOURCEBOOT_SCC1_SAMPLE_COUNT 600U
  #define SOURCEBOOT_SCC1_TOTAL_WORDS \
      (SOURCEBOOT_SCC1_HEADER_WORDS + \
       SOURCEBOOT_SCC1_SAMPLE_WORDS * SOURCEBOOT_SCC1_SAMPLE_COUNT)

  typedef struct sourceboot_camera_idle_capture {
      uint32_t words[SOURCEBOOT_SCC1_TOTAL_WORDS];
  } sourceboot_camera_idle_capture_t;

  _Static_assert(sizeof(sourceboot_camera_idle_capture_t) == 194496U,
                 "SCC1 raw size drift");
  ```

  Place it in `.lwram_camera_capture`, align it to 32 bytes, explicitly clear
  it through the cache-through alias, and write header magic only after all
  600 samples and counters are complete.

  Fixed mode waits for the committed `SATURN_CAMERA_IDLE_START_TICK`.
  Discovery mode compares the exact Mario tuple and all 77 camera state words
  each source tick. At the first combined 60-tick stable run, it sets
  `idle_start_tick = source_tick - 2000 - 59`, backfills those
  60 identical semantic samples with their consecutive source ticks, and then
  records the remaining 540 naturally. Both modes keep sample word 3 zero.
  Fixed mode also buffers and verifies its first 60 ticks before backfilling
  them with the Mario-quiescent flag; any change in that committed window is a
  failed capture, not a search for a later start.

- [ ] **Step 4: Add exact input, flag, and header packing**

  Pack the applied controller state as:

  ```c
  static uint32_t sourceboot_pack_applied_input(
      const sm64_saturn_source_runtime_state_t *runtime)
  {
      return ((uint32_t)runtime->last_applied_buttons << 16) |
             ((uint32_t)(uint8_t)runtime->last_applied_stick_x << 8) |
             (uint32_t)(uint8_t)runtime->last_applied_stick_y;
  }
  ```

  Header word 23 is `2`. Header word 11 is the first absolute
  replay-relative source tick. Sample ticks must be consecutive. Latch the
  exact final diagnostics after sample 599; do not overwrite the completed
  capture during later frames.

- [ ] **Step 5: Reserve LWRAM and reclaim a measured HWRAM budget**

  Add:

  ```ld
  .lwram_camera_capture (NOLOAD) :
  {
    . = ALIGN (32);
    __lwram_camera_capture_start = .;
    KEEP(*(.lwram_camera_capture))
    __lwram_camera_capture_end = .;
  } > lwram

  ASSERT (SIZEOF(.lwram_camera_capture) == 0 ||
          SIZEOF(.lwram_camera_capture) == 0x2F7C0,
          "SCC1 capture must be absent or exactly 0x2F7C0 bytes")
  ASSERT (SIZEOF(.lwram_camera_capture) == 0 ||
          ORIGIN(lwram) + LENGTH(lwram) -
              __lwram_camera_capture_end >= 0x4000,
          "SCC1 leaves less than the measured LWRAM margin")
  ```

  Include the recorder object only when replay and camera route 1 are both
  selected. Add a map verifier that requires section size `0` for route 0 and
  `0x2F7C0` for route 1 and rejects overlap with `.lwram_cmdts` or
  `.lwram_bss`. Require `sh-elf-readelf -SW` to report the section as NOBITS
  and `sh-elf-nm -S` to report the capture symbol at size `0002f7c0`. The
  verifier runs those tools with the explicit MSYS DLL path and records the
  measured current LWRAM end `0x002CBB20`, final SCC end `0x002FB2E0`,
  remaining `0x4D20`, and discovery allowance `0x2D20`.

  In `source_cart.c`, replace the literal 16-sector definition with:

  ```c
  #ifndef SATURN_SOURCE_CART_STAGE_SECTORS
  #define SATURN_SOURCE_CART_STAGE_SECTORS 16U
  #endif
  #define SOURCE_CART_STAGE_BYTES \
      (SATURN_SOURCE_CART_STAGE_SECTORS * CDFS_SECTOR_SIZE)
  _Static_assert(SATURN_SOURCE_CART_STAGE_SECTORS == 4U ||
                 SATURN_SOURCE_CART_STAGE_SECTORS == 8U ||
                 SATURN_SOURCE_CART_STAGE_SECTORS == 16U,
                 "unsupported source-cart staging size");
  ```

  First build the complete route-1 transport with 8 sectors. If its measured
  HWRAM margin is below `0x5B00`, rebuild with 4; otherwise 4 is forbidden.
  Write the selected value, before/after `___end`, cart-stage symbol size,
  HWRAM deltas, SCC boundaries, and ELF hash to
  `bob_camera_memory_v1.json`. Pass
  `--defsym=__sourceboot_required_hwram_margin=0x1B00` for every later camera
  artifact and make the linker assertion use that symbol, defaulting to the
  existing `0x1000` outside the camera evidence profile.

  Add unit mutations for a missing `___end`, wrong stage size, SCC overlap,
  a total HWRAM margin below `0x1B00`, a post-transport margin below `0x5B00`,
  and a phase whose `___end` regresses without a recorded delta. Do not lower
  the TLSF or `0x0B00` safety floors.

  Give `verify_sourceboot_memory_map.py` two exact subcommands:

  ```text
  select-transport
    --baseline-elf --stage8-elf --stage4-elf --baseline-end
    --required-post-transport-margin --required-final-margin
    --fixture-output --output

  check-phase
    --elf --phase --stage-sectors --previous-report
    --required-final-margin --output
  ```

  `select-transport` verifies all three role/tag/section layouts, chooses stage
  8 whenever it leaves at least `0x5B00`, chooses stage 4 only otherwise, and
  writes both the selected fixture and hash-bound transport report.
  `check-phase` requires the prior report's selected stage, records the
  current ELF hash/`___end`/sections/symbol sizes, computes the signed
  incremental delta from the prior report, and refuses a stale phase name,
  wrong stage, missing predecessor, non-forward report chain, or final margin
  below `0x1B00`. The code-owned predecessor graph is exact:

  ```text
  transport -> fixed-baseline
  fixed-baseline -> candidate-q12 | candidate-q16 | final-baseline
  candidate-q12 -> frozen-numeric
  candidate-q16 -> frozen-numeric
  frozen-numeric -> shadow
  shadow -> lakitu
  lakitu -> default-core
  default-core -> bridges
  bridges -> complete-island
  complete-island -> final-q
  ```

  A Q12/Q16 branch exists only when its candidate fixture entry is
  range-qualified. `final-baseline` and `final-q` are terminal role branches.

- [ ] **Step 6: Hook recording at the authoritative source-tick boundary**

  In `sourceboot_run_source_tick`, call the recorder only after the profile's
  simulation accumulator/count fields are updated:

  ```c
  sourceboot_fast3d.profile.sim_frt_ticks_accum =
      sourceboot_sim_ticks_accum;
  sourceboot_fast3d.profile.sim_tick_count = sourceboot_sim_tick_count;
  #if SATURN_SOURCEBOOT_CAMERA_ROUTE == 1
      sm64_saturn_sourceboot_camera_idle_probe_record(
          sm64_saturn_source_runtime_state(),
          sourceboot_sim_tick_count);
  #endif
  ```

  Do not add another recorder call in the render loop. Add a source-order test
  that rejects a hook before `game_loop_one_iteration()` or after route
  checkpoint publication. Call the recorder reset in the route-configuration
  block before `thread5_game_loop(NULL)` because no `.lwram_*` section is
  cleared by crt0.

- [ ] **Step 7: Implement bounded target capture**

  `capture_camera_idle.py` must accept:

  ```text
  --ymir --ipl --game --route-manifest --capture-role
  --expected-idle-start-tick [--discovery]
  [--source-data --cart-proof-output] --output
  ```

  It resolves `sourceboot_camera_idle_capture`,
  `sourceboot_route_checkpoint`, `sm64_saturn_camera_variant_marker`, and
  `sm64_saturn_camera_route_marker` with
  `D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-nm.exe`,
  using a child environment whose `PATH` starts with
  `C:\msys64\usr\bin`. It runs the
  existing sourceboot BIOS input macro, waits for SCC1 magic, pauses, reads
  194,496 bytes in exactly three bounded reads of 65,536, 65,536, and 63,424
  bytes, independently reads exactly 160 bytes of raw SBR4 from the same
  paused instance, calls `decode_scc1`, and writes both raw windows plus
  decoded data, absolute marker values, artifact identities,
  `frame_serial`, `sim_frt_ticks_accum`, `render_frt_ticks_accum`,
  `render_frt_ticks_last`, `master_wait_ticks`, `slave_busy_ticks`,
  `slave_jobs_completed`, `slave_timeouts`, host wall-clock seconds, observed
  emulated VBlank rate, and emulation-speed ratio. It must abort Ymir on every
  exception. When `--cart-proof-output` is present, require
  `--source-data`, resolve `g_sm64_saturn_source_cart_probe`, read its exact
  28 raw bytes from the same pause, and write a raw-first READY/size/hash
  proof to that path.

  Rerun:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_capture_camera_idle.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  ```

- [ ] **Step 8: Build, select, and boot-prove the additive transport**

  Build route 0 at the unchanged 16-sector default and route-1 discovery
  images at both candidate staging sizes. These are the exact commands:

  ```powershell
  $transportBuildBase = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  $route0TransportCommand = "$transportBuildBase SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_SOURCE_CART_STAGE_SECTORS=16 verify"
  $stage8TransportCommand = "$transportBuildBase SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=1 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=8 verify"
  $stage4TransportCommand = "$transportBuildBase SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=1 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=4 verify"
  C:/msys64/usr/bin/bash.exe -lc $route0TransportCommand
  C:/msys64/usr/bin/bash.exe -lc $stage8TransportCommand
  C:/msys64/usr/bin/bash.exe -lc $stage4TransportCommand

  $route0TransportOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute0-atan2v2-camv1-stage16-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $stage8TransportOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle0-disc1-range0-stage8-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $stage4TransportOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle0-disc1-range0-stage4-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $route0TransportElf = (Resolve-Path "$route0TransportOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $stage8TransportElf = (Resolve-Path "$stage8TransportOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $stage4TransportElf = (Resolve-Path "$stage4TransportOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py select-transport --baseline-elf $route0TransportElf --stage8-elf $stage8TransportElf --stage4-elf $stage4TransportElf --baseline-end 0x060FDCB0 --required-post-transport-margin 0x5B00 --required-final-margin 0x1B00 --fixture-output tools/saturn/fixtures/bob_camera_memory_v1.json --output docs/saturn/evidence/reports/task3-camera-memory-transport-2026-07-29.json
  ```

  Require route 0 to contain no SCC section or SCC symbol. Require the selector
  to choose 8 sectors whenever its margin passes and 4 only when 8 fails.
  Then boot the selected image and capture SCC1/SBR4/SCAR from one pause:

  ```powershell
  $cameraMemory = Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json
  $selectedTransportOutput = if ($cameraMemory.stage_sectors -eq 8) { $stage8TransportOutput } elseif ($cameraMemory.stage_sectors -eq 4) { $stage4TransportOutput } else { throw "invalid selected camera stage" }
  $selectedTransportCue = (Resolve-Path "$selectedTransportOutput/sm64-saturn-sourceboot-e2.cue").Path
  $selectedSourceData = (Resolve-Path "$selectedTransportOutput/SOURCE.DAT").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $selectedTransportCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --capture-role camera-baseline --expected-idle-start-tick 0 --discovery --source-data $selectedSourceData --cart-proof-output docs/saturn/evidence/reports/task3-camera-transport-capture-2026-07-29.json --output docs/saturn/evidence/reports/task3-camera-transport-scc1-2026-07-29.json
  ```

  Require READY/OK, copied size equal to exact `SOURCE.DAT` size, the recorded
  raw/file SHA-256, exact route completion, and no renderer/output regression.
  Delete neither candidate build. Stop before Q implementation if the selected
  size fails any cart-load/hash gate.

- [ ] **Step 9: Commit and review the evidence transport**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/game/camera.c src/port/saturn/runtime/saturn_camera_probe.h src/port/saturn/sourceboot/source_camera_idle_probe.h src/port/saturn/sourceboot/source_camera_idle_probe.c src/port/saturn/sourceboot/main.c src/port/saturn/sourceboot/source_cart.c src/port/saturn/sourceboot/Makefile src/port/saturn/sourceboot/sourceboot-cart.x tools/saturn/capture_camera_idle.py tools/saturn/test_capture_camera_idle.py tools/saturn/verify_sourceboot_memory_map.py tools/saturn/test_verify_sourceboot_memory_map.py tools/saturn/test_camera_idle_contract.py tools/saturn/fixtures/bob_camera_memory_v1.json docs/saturn/evidence/reports/task3-camera-memory-transport-2026-07-29.json docs/saturn/evidence/reports/task3-camera-transport-capture-2026-07-29.json docs/saturn/evidence/reports/task3-camera-transport-scc1-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: add raw camera idle target capture"
  ```

  Review the SCC order, source-tick hook, map, magic-last sequence, and the
  absence of target behavior changes before continuing.

---

### Task 4: Inventory the Complete Bounded Camera Writer Closure

**Files:**

- Create: `tools/saturn/camera_q_writer_contract_v1.json`
- Create: `tools/saturn/verify_camera_q_writers.py`
- Create: `tools/saturn/test_verify_camera_q_writers.py`
- Modify: `src/game/camera.c`

**Interfaces:**

- Consumes: Task 3's semantic SCC snapshot and fixed/discovery recorder.
- Produces:
  the exact writer-ownership JSON, a generated transitive direct-call closure,
  and a reviewed manifest of indirect/function-pointer edges.

- [ ] **Step 1: Write failing writer-inventory mutation tests**

  Define the bounded roots as:

  ```python
  ROOTS = (
      "update_camera",
      "update_mario_camera",
      "mode_mario_camera",
      "mode_lakitu_camera",
      "mode_default_camera",
      "update_default_camera",
      "next_lakitu_state",
      "update_lakitu",
  )
  OWNERS = ("q_owned", "float_api_import", "public_mirror", "invalidation")
  ```

  Parse the translation unit's definitions and direct calls with a
  comment/string-aware brace scanner. Generate the transitive closure from
  every root, including helpers such as `handle_c_button_movement`,
  `collide_with_walls`, and `approach_camera_height`. The contract lists every
  reviewed indirect/function-pointer edge and its possible callees; an
  unreviewed indirect edge is a hard failure. In the complete closure, find
  assignments and mutating vec3 calls for every approved Camera, Lakitu,
  `sOld*`, transition, pan/zoom, yaw/distance/pitch field, and require exactly
  one manifest entry per write. Tests inject an unlisted
  `sZoomAmount = value`, hide a write in a newly reachable helper, add an
  unreviewed indirect call, duplicate an owner, remove a publish write, and
  move a collision import to `q_owned`; each mutation must fail.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  ```

  Expected failure: the verifier/manifest are absent.

- [ ] **Step 2: Populate and review the baseline writer inventory**

  Populate each manifest row with source function, field, operation, owner,
  ordered data-flow stage 0 through 7, direct root path, and source line
  digest. Require
  `verify_camera_q_writers.py src/game/camera.c
  tools/saturn/camera_q_writer_contract_v1.json` to report no omitted or
  multiply owned writes, no missing closure function, and no unresolved
  indirect edge. Review the inventory manually against
  `camera.c:2065-2385`, `2392-2413`, `2913-3011`, `3018-3210`, and
  `5405-5491`.

- [ ] **Step 3: Commit and independently review the writer closure**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/game/camera.c tools/saturn/camera_q_writer_contract_v1.json tools/saturn/verify_camera_q_writers.py tools/saturn/test_verify_camera_q_writers.py
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: inventory Saturn camera writer closure"
  ```

  Review the generated closure and every indirect-edge declaration before
  adding telemetry. Any missed reachable writer reopens this task.

---

### Task 5: Capture Quiescence and Nominate Range-Qualified Q Candidates

**Files:**

- Create: `src/port/saturn/runtime/saturn_camera_range_probe.h`
- Create: `src/port/saturn/sourceboot/source_camera_range_probe.c`
- Create: `tools/saturn/camera_range_contract.py`
- Create: `tools/saturn/test_camera_range_contract.py`
- Create: `tools/saturn/select_camera_q_candidates.py`
- Create: `tools/saturn/test_camera_q_candidates.py`
- Create after capture:
  `tools/saturn/fixtures/bob_default_camera_v1_idle.json`
- Create after capture:
  `tools/saturn/fixtures/bob_camera_q_candidates_v1.json`
- Create after capture:
  `docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json`
- Create after capture:
  `docs/saturn/evidence/reports/task3-camera-idle-discovery-2026-07-29.json`
- Create after fixed-pin proof:
  `docs/saturn/evidence/reports/task3-camera-idle-fixed-baseline-2026-07-29.json`
- Create after fixed-pin proof:
  `docs/saturn/evidence/reports/task3-camera-memory-fixed-baseline-2026-07-29.json`
- Create after fixed-pin proof:
  `docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json`
- Create after analysis:
  `docs/saturn/evidence/reports/task3-camera-range-candidates-2026-07-29.json`
- Modify: `src/game/camera.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/source_camera_idle_probe.c`
- Modify: `src/port/saturn/sourceboot/sourceboot-cart.x`
- Modify: `tools/saturn/capture_camera_idle.py`
- Modify: `tools/saturn/test_capture_camera_idle.py`
- Modify: `tools/saturn/verify_camera_idle_capture.py`
- Modify: `tools/saturn/test_verify_camera_idle_capture.py`

**Interfaces:**

- Consumes: Task 3's raw transport and Task 4's complete writer/field IDs.
- Produces:
  `decode_camera_range(raw: bytes)`,
  `verify_and_emit_idle_fixture(discovery, range_capture, fixed, route)`,
  `select_camera_q_candidates(range_capture, idle_fixture)`,
  `bob_default_camera_v1_idle.json`, and a provisional candidate fixture that
  may qualify Q16, Q12, or both. It does not create the production Q config.

- [ ] **Step 1: Write failing range-contract and candidate-selection tests**

  Define range categories:

  ```python
  RANGE_KINDS = (
      "state_coordinate",
      "delta",
      "distance",
      "approach_residual",
      "transition_numerator",
      "transition_divisor",
      "square_operand",
      "square_sum",
  )
  ```

  Synthetic cases must qualify Q16, qualify Q12 when Q16 violates half-range,
  qualify both when both range proofs pass, reject both formats, reject a
  missing category, reject a non-finite operand, and reject a square/sum
  overflow proof. For Q12 division, separately prove that the sign-restored
  Q16 DIVU quotient fits signed 32-bit before truncation by 16 and that the
  narrowed Q12 result fits its configured envelope; a direct-Q12-only proof
  is insufficient.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_range_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_q_candidates.py
  ```

  Expected failure: both production modules are absent.

- [ ] **Step 2: Define and implement the exact SCR1 range ABI**

  Add `sm64_saturn_camera_range_observe(kind, field_id, f32_bits)` calls at
  every numeric input and derived-result boundary in the bounded float path.
  The implementation compares absolute IEEE-754 bit magnitudes without target
  float arithmetic, keeps a maximum per field/category, and records a bounded
  256-entry corpus whenever a new extremum or angle/zero-crossing boundary is
  seen.

  Compile these calls and conditionally add `source_camera_range_probe.c` to
  `SH_SRCS` only when route 1 and `SATURN_CAMERA_RANGE_CAPTURE=1` are both
  selected. The additional symbol
  `sourceboot_camera_range_capture` is exactly 2,048 big-endian words
  (`0x2000` bytes), aligned to 32 bytes in `.lwram_camera_capture`:

  ```text
  SCR1 header, words 0..31:
    0 magic 0x53435231 ("SCR1"), published last
    1 version 1
    2 total words 2048
    3 header words 32
    4 maxima record words 3
    5 maxima capacity 256
    6 maxima count
    7 corpus record words 4
    8 corpus capacity 256
    9 corpus count
   10 SBR4 magic
   11 SBR4 version
   12 replay ticks 2000
   13 route ID 2
   14 first observed source tick
   15 final observed source tick
   16 discovered idle_start_tick
   17..24 coverage count for each RANGE_KINDS entry in declared order
   25 non-finite observation count
   26 dropped/overflow record count
   27 camera variant 1
   28 atan2 variant 2
   29..30 reserved zero
   31 payload words 2016

  maxima records, words 32..799, 256 records of:
    kind in bits 31:24 plus field_id in bits 23:0
    maximum absolute f32 bits
    source tick of that maximum

  corpus records, words 800..1823, 256 records of:
    kind in bits 31:24 plus field_id in bits 23:0
    source tick
    observed f32 bits
    event flags in bits 31:24 plus sequence in bits 23:0

  words 1824..2047: reserved zero
  ```

  Explicitly clear the NOLOAD block, reject a field ID absent from the writer
  contract, saturate counts without writing out of bounds, and publish magic
  only after SCC1 and SCR1 are complete. Unit tests independently construct
  raw SCR1 bytes and mutate every header field, a maxima key, a corpus value,
  reserved data, non-finite/dropped counters, ordering, and decoded/raw
  agreement.

  Update the linker size assertion to allow exact `0x317C0` only when this
  object is linked. Assert the discovery build leaves at least `0x2D20`
  LWRAM. Final fixed SCC builds must omit the symbol, return to exact
  `0x2F7C0`, and retain at least `0x4000`.

- [ ] **Step 3: Capture SCR1 and the earliest quiescence pin**

  Read the selected staging value:

  ```powershell
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  ```

  Build:

  ```powershell
  $discoveryBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=1 SATURN_CAMERA_RANGE_CAPTURE=1 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify"
  C:/msys64/usr/bin/bash.exe -lc $discoveryBuildCommand
  ```

  Extend `capture_camera_idle.py` with `--range-output`. When present, resolve
  `sourceboot_camera_range_capture` from the sibling ELF, read exactly 8,192
  bytes from the same paused instance after SCC1/SBR4, independently decode
  SCR1, and reject raw/decoded or route/tick disagreement. Test missing,
  short, oversized, stale-magic, and different-pause range windows.

  Capture with the role-tagged discovery CUE:

  ```powershell
  $discoveryOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle0-disc1-range1-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $discoveryCue = (Resolve-Path "$discoveryOutput/sm64-saturn-sourceboot-e2.cue").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $discoveryCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --capture-role camera-baseline --expected-idle-start-tick 0 --discovery --range-output docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json --output docs/saturn/evidence/reports/task3-camera-idle-discovery-2026-07-29.json
  ```

  Do not write the idle fixture yet. Stop if no combined Mario/camera window
  exists; Step 4 derives the candidate tick from these raw reports, rebuilds
  it, and only then emits the authoritative fixture.

- [ ] **Step 4: Rebuild the raw-derived pin and seal the idle fixture**

  Build route 1 with `SATURN_CAMERA_IDLE_DISCOVERY=0`,
  `SATURN_CAMERA_RANGE_CAPTURE=0`, and the tick printed by the raw-first
  discovery verifier, retaining the selected stage-sector value. Extend
  `verify_camera_idle_capture.py` with a mutually exclusive idle-pin mode:

  ```text
  --discovery-run --range-run --route --print-discovered-idle-tick
  --discovery-run --range-run --fixed-run --route
      --emit-idle-fixture --output
  ```

  Both forms independently decode the stored raw SCC1/SCR1 bytes and reject
  decoded-JSON disagreement. The print form writes exactly one unsigned
  decimal tick and no file. The emit form additionally raw-decodes the fixed
  SCC1, requires matching route/artifact identities and its first 60 semantic
  samples to equal the discovery proof, then atomically writes the fixture and
  verification report. Add mutations for a transcribed later tick, one changed
  discovery sample, one changed fixed sample, a mismatched SCR1 digest, and an
  output file that pre-exists after a failing gate.

  ```powershell
  $cameraIdleTickLines = @(& .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_camera_idle_capture.py --discovery-run docs/saturn/evidence/reports/task3-camera-idle-discovery-2026-07-29.json --range-run docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json --route tools/saturn/routes/bob_default_camera_v1.json --print-discovered-idle-tick)
  if ($LASTEXITCODE -ne 0 -or $cameraIdleTickLines.Count -ne 1 -or $cameraIdleTickLines[0] -notmatch '^[0-9]+$') { throw "raw discovery did not yield one idle tick" }
  $cameraIdleTick = [uint32]$cameraIdleTickLines[0]
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $fixedBaselineBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify"
  C:/msys64/usr/bin/bash.exe -lc $fixedBaselineBuildCommand
  $fixedBaselineOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $fixedBaselineElf = (Resolve-Path "$fixedBaselineOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $fixedBaselineCue = (Resolve-Path "$fixedBaselineOutput/sm64-saturn-sourceboot-e2.cue").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $fixedBaselineElf --phase fixed-baseline --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-transport-2026-07-29.json --required-final-margin 0x5B00 --output docs/saturn/evidence/reports/task3-camera-memory-fixed-baseline-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $fixedBaselineCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --capture-role camera-baseline --expected-idle-start-tick $cameraIdleTick --output docs/saturn/evidence/reports/task3-camera-idle-fixed-baseline-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_camera_idle_capture.py --discovery-run docs/saturn/evidence/reports/task3-camera-idle-discovery-2026-07-29.json --range-run docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json --fixed-run docs/saturn/evidence/reports/task3-camera-idle-fixed-baseline-2026-07-29.json --route tools/saturn/routes/bob_default_camera_v1.json --emit-idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --output docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json
  ```

  Require all 600 state samples to be exactly stable beginning at the
  committed tick, the discovery-only SCR1 symbol to be absent, SCC exact size
  `0x2F7C0`, final LWRAM at least `0x4000`, and the chained HWRAM report at
  least `0x5B00`. The fixture's 60 raw discovery samples must equal the first
  60 semantic samples of this fixed capture; a later substituted pin fails.
  The emitted fixture records the raw-derived `idle_start_tick`, SCC/SCR1
  SHA-256 values, route-manifest SHA-256, both ELF/ISO identities, and the
  exact 60-sample proof. Candidate selection rejects an absent verification
  report or mismatched fixture/report hash.

- [ ] **Step 5: Nominate candidates without freezing production config**

  Run only after Step 4 emitted and sealed the idle fixture:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/select_camera_q_candidates.py --range-capture docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --output-fixture tools/saturn/fixtures/bob_camera_q_candidates_v1.json --output-report docs/saturn/evidence/reports/task3-camera-range-candidates-2026-07-29.json
  ```

  The candidate fixture records, separately for Q16 and Q12, eligibility,
  signed raw limits, measured maxima, half-range headroom, square/sum proofs,
  Q12 intermediate-Q16 DIVU bounds, and derived comparison bounds. It exits
  nonzero and writes neither output if both formats fail. It must not create
  `saturn_camera_q_config.h` or declare a winner. Its exact Boolean paths are
  `candidates.q16.range_qualified` and
  `candidates.q12.range_qualified`, alongside the shared evidence hashes;
  Task 7 freezes the winner only after the production differential.

- [ ] **Step 6: Run focused and top-level host gates**

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_range_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_q_candidates.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_capture_camera_idle.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_idle_capture.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sourceboot_memory_map.py
  make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe
  ```

- [ ] **Step 7: Commit the raw range evidence and provisional candidates**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/game/camera.c src/port/saturn/runtime/saturn_camera_range_probe.h src/port/saturn/sourceboot/source_camera_range_probe.c src/port/saturn/sourceboot/source_camera_idle_probe.c src/port/saturn/sourceboot/sourceboot-cart.x src/port/saturn/sourceboot/Makefile tools/saturn/camera_range_contract.py tools/saturn/test_camera_range_contract.py tools/saturn/select_camera_q_candidates.py tools/saturn/test_camera_q_candidates.py tools/saturn/capture_camera_idle.py tools/saturn/test_capture_camera_idle.py tools/saturn/verify_camera_idle_capture.py tools/saturn/test_verify_camera_idle_capture.py tools/saturn/fixtures/bob_default_camera_v1_idle.json tools/saturn/fixtures/bob_camera_q_candidates_v1.json docs/saturn/evidence/reports/task3-camera-idle-discovery-2026-07-29.json docs/saturn/evidence/reports/task3-camera-idle-fixed-baseline-2026-07-29.json docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json docs/saturn/evidence/reports/task3-camera-memory-fixed-baseline-2026-07-29.json docs/saturn/evidence/reports/task3-camera-range-discovery-2026-07-29.json docs/saturn/evidence/reports/task3-camera-range-candidates-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: capture camera range candidates"
  ```

---

### Task 6: Implement Candidate Raw Bit Bridges, Multiply, and DIVU

**Files:**

- Create: `src/port/saturn/runtime/saturn_camera_q_math.h`
- Create: `src/port/saturn/runtime/saturn_camera_q_math.c`
- Create: `tools/saturn/camera_q_diff_fixture.c`
- Create: `tools/saturn/test_camera_q.py`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Create when Q16 is range-qualified:
  `docs/saturn/evidence/reports/task3-camera-memory-candidate-q16-2026-07-29.json`
- Create when Q12 is range-qualified:
  `docs/saturn/evidence/reports/task3-camera-memory-candidate-q12-2026-07-29.json`
- Modify if Q12 is range-qualified: `docs/saturn/PROVENANCE.md`
- Modify if Q12 is range-qualified: `THIRD_PARTY_LICENSES.md`
- Read before implementation:
  `D:/Code/RetroDev/sm64-saturn-port/work/upstream/joengine/jo_engine/math.c`
- Read before implementation:
  `D:/Code/RetroDev/sm64-saturn-port/sm64-port/work/upstream/slavedriver-engine/WALLASM.S`
- Reuse:
  `src/port/saturn/gfx/saturn_q16_sh2.h`
- Reuse:
  `src/port/saturn/gpl/slavedriver_projection.h/.sx`

**Interfaces:**

- Consumes: Task 5's range-qualified candidates and captured operand corpus.
  Until Task 7 freezes the winner, host/target candidate builds pass exactly
  one test-only `SATURN_CAMERA_Q_CANDIDATE_BITS=12|16`; ordinary production
  variant-2 builds remain rejected without the generated final config.
- Produces:
  `saturn_camera_q_t`, `saturn_camera_q_vec3_t`,
  `saturn_camera_u64_t`,
  `sm64_saturn_camera_q_from_f32_bits_checked`,
  `sm64_saturn_camera_q_to_f32_bits_checked`,
  `sm64_saturn_camera_q_mul_checked`,
  `sm64_saturn_camera_q_mul_trig_checked`,
  `sm64_saturn_camera_q_divu_start`, and
  `sm64_saturn_camera_q_divu_collect`.

- [ ] **Step 1: Reconfirm reference pins, licenses, and reuse mode**

  Run:

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/work/upstream/joengine -C D:/Code/RetroDev/sm64-saturn-port/work/upstream/joengine rev-parse HEAD
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/work/upstream/slavedriver-engine -C D:/Code/RetroDev/sm64-saturn-port/sm64-port/work/upstream/slavedriver-engine rev-parse HEAD
  Get-Content D:/Code/RetroDev/sm64-saturn-port/work/upstream/joengine/LICENSE
  Get-Content D:/Code/RetroDev/sm64-saturn-port/sm64-port/work/upstream/slavedriver-engine/LICENSE.txt
  ```

  Require the exact pins in Global Constraints. Record the inspected source
  ranges and destination mode in the implementation report. If Q12 remains a
  range-qualified candidate, add a Jo Engine close-adaptation entry retaining
  the upstream notice and documenting `MACH:MACL` reconstruction plus the
  12-bit shift. For Q16, record direct reuse of the already attributed in-tree
  primitive with no new copy.

- [ ] **Step 2: Define selected-format scalar types and checked APIs**

  Add:

  ```c
  typedef int32_t saturn_camera_q_t;

  typedef struct saturn_camera_q_vec3 {
      saturn_camera_q_t x;
      saturn_camera_q_t y;
      saturn_camera_q_t z;
  } saturn_camera_q_vec3_t;

  typedef struct saturn_camera_u64 {
      uint32_t hi;
      uint32_t lo;
  } saturn_camera_u64_t;

  bool sm64_saturn_camera_q_from_f32_bits_checked(
      uint32_t bits, saturn_camera_q_t *out);
  bool sm64_saturn_camera_q_to_f32_bits_checked(
      saturn_camera_q_t value, uint32_t *out_bits);
  bool sm64_saturn_camera_q_mul_checked(
      saturn_camera_q_t left, saturn_camera_q_t right,
      saturn_camera_q_t *out);
  bool sm64_saturn_camera_q_mul_trig_checked(
      saturn_camera_q_t value, int32_t trig_q16,
      saturn_camera_q_t *out);
  ```

  Add compile-time checks for 32-bit `int32_t` and fraction bits 12 or 16.
  Candidate test builds take the fraction bits from
  `SATURN_CAMERA_Q_CANDIDATE_BITS`; after Task 7, ordinary builds must take
  them only from `saturn_camera_q_config.h`.

- [ ] **Step 3: Write failing raw IEEE-754 bridge tests**

  Add literal bit cases for positive/negative zero, subnormals, exact
  integers, half-LSB ties of both parities, the selected positive/negative
  envelope, one value outside each edge, infinity, and NaN. The accepted
  conversion rule is round-to-nearest, ties-to-even; non-finite and
  out-of-envelope inputs return `false` without producing a usable value.
  Converting a valid Q value back to f32 bits uses the same tie rule and does
  not execute floating arithmetic.

  Run:

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py RawBridgeTests"
  ```

  Expected failure: the Q math header/source are absent.

- [ ] **Step 4: Implement helper-free float/Q bit conversion**

  Decode sign, exponent, and mantissa with integer masks. Handle subnormal
  input as a normal integer-shift case rather than by a C cast. Apply a
  guard/sticky/tie-even decision before narrowing. Construct output f32 bits by
  locating the highest set magnitude bit and rounding the discarded tail.

  Add `saturn_camera_q_math.c` explicitly to sourceboot `SH_SRCS` for camera
  variant 2; the Makefile does not glob runtime sources. Add a provisional
  `-qbits$(SATURN_CAMERA_Q_CANDIDATE_BITS)` object/output tag and reject that
  variable outside 12/16 candidate builds.

  Read the candidate fixture, idle pin, and staging value. For each qualified
  bit count, build route 1 variant 2 with
  `SATURN_CAMERA_Q_CANDIDATE_BITS=$candidateBits` and an exact output tag
  `-demo-replay-camroute1-atan2v2-camv2-qbits$candidateBits-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2`;
  do not run `verify` before the final format exists:

  ```powershell
  $cameraCandidates = Get-Content tools/saturn/fixtures/bob_camera_q_candidates_v1.json -Raw | ConvertFrom-Json
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $candidateOutputs = @()
  foreach ($candidateBits in @(12, 16)) {
      if (-not $cameraCandidates.candidates."q$candidateBits".range_qualified) { continue }
      $candidateBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_CAMERA_Q_CANDIDATE_BITS=$candidateBits SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
      C:/msys64/usr/bin/bash.exe -lc $candidateBuildCommand
      $candidateOutputs += [pscustomobject]@{
          Bits = $candidateBits
          Path = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-qbits$candidateBits-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
      }
  }
  ```

  Inside that loop, inspect the one target object with a scoped DLL path:

  ```powershell
  foreach ($candidateOutput in $candidateOutputs) {
      $cameraQMathObjects = @(Get-ChildItem -LiteralPath "$($candidateOutput.Path)/obj" -Recurse -Filter saturn_camera_q_math.o -File)
      if ($cameraQMathObjects.Count -ne 1) { throw "expected exactly one camera Q math object" }
      $savedPath = $env:PATH
      try {
          $env:PATH = "C:\msys64\usr\bin;$savedPath"
          & "D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin\sh-elf-objdump.exe" -dr $cameraQMathObjects[0].FullName
      } finally {
          $env:PATH = $savedPath
      }
  }
  ```

  Expected result: the two bridge functions have no calls to `__floatsisf`,
  `__floatunsisf`, `__fixsfsi`, `__fixunssfsi`, or any other soft-float helper.

- [ ] **Step 5: Write failing selected-format multiply tests**

  Test zero, signs, one, half, the captured extrema, exact shift boundaries,
  and all overflow edges. Add a mutation build that shifts by
  `SM64_SATURN_CAMERA_Q_FRACTION_BITS - 1`; require at least one literal and
  one captured case to fail. Separately test mixed state-Q by trig-Q16
  multiplication because its shift is always 16 even when state uses Q12.

- [ ] **Step 6: Implement Q16 and Q12 target multiply paths**

  For Q16, call the existing attributed
  `sm64_saturn_q16_mul_sh2` after the measured checked-envelope guard. For Q12,
  add one attributed SH-2 primitive:

  ```c
  static inline saturn_camera_u64_t
  saturn_camera_dmuls_words(int32_t left, int32_t right)
  {
      saturn_camera_u64_t product;
  #if defined(__sh__)
      __asm__ volatile(
          "dmuls.l %2,%3\n\t"
          "sts mach,%0\n\t"
          "sts macl,%1"
          : "=r"(product.hi), "=r"(product.lo)
          : "r"(left), "r"(right)
          : "mach", "macl");
  #else
      const int64_t host_product = (int64_t)left * (int64_t)right;
      product.hi = (uint32_t)((uint64_t)host_product >> 32);
      product.lo = (uint32_t)host_product;
  #endif
      return product;
  }
  ```

  Reconstruct the signed shift using the two words, perform checked narrowing,
  and keep host `int64_t` strictly under `!__sh__`. Mixed trig multiplication
  uses the same product-word helper and a 16-bit shift.

- [ ] **Step 7: Write failing signed DIVU/Q12-narrowing tests**

  Test every sign quadrant, zero divisor, `INT32_MIN / -1`, captured
  transition divisions, quotient overflow, and values immediately around a
  Q12 truncation boundary. For every captured and boundary pair, assert:

  ```python
  direct_q12 = trunc_toward_zero((numerator << 12) / divisor)
  via_q16 = trunc_toward_zero(q16_divu(numerator, divisor) / 16)
  self.assertEqual(direct_q12, via_q16)
  ```

  Add a mutation that arithmetic-shifts a negative Q16 quotient by four;
  require it to fail because that rounds toward negative infinity rather than
  zero. Also mutate a case whose direct Q12 answer fits but whose intermediate
  sign-restored Q16 quotient exceeds signed 32-bit; candidate selection and
  the runtime guard must both reject it.

- [ ] **Step 8: Implement checked start/collect division**

  Wrap the existing `sm64_saturn_divu_q16_start` and
  `sm64_saturn_divu_q16_collect` in:

  ```c
  typedef struct saturn_camera_q_divu {
      sm64_saturn_divu_q16_t q16;
      bool started;
  } saturn_camera_q_divu_t;

  bool sm64_saturn_camera_q_divu_start(
      saturn_camera_q_divu_t *op,
      saturn_camera_q_t numerator,
      saturn_camera_q_t divisor);
  bool sm64_saturn_camera_q_divu_collect(
      saturn_camera_q_divu_t *op,
      saturn_camera_q_t *quotient);
  ```

  For Q12, narrow the collected signed Q16 quotient with an unsigned-magnitude
  shift and restored sign, avoiding both `/ 16` and implementation-defined
  negative right-shift behavior. Check the intermediate Q16 magnitude before
  sign restoration and narrowing. The camera state machine schedules
  independent work between start and collect.

- [ ] **Step 9: Run differential, mutation, and target disassembly gates**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py RawBridgeTests"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py MultiplyTests"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py DivisionTests"
  ```

  Build the SH object and require `dmuls.l` to appear. Reject target references
  to `__muldi3`, `__divdi3`, `__moddi3`, soft-float conversion helpers, and
  plain C 64-bit division/modulo. Run the exact chained phase gate for each
  candidate ELF built in Step 4:

  ```powershell
  foreach ($candidateOutput in $candidateOutputs) {
      $candidateElf = (Resolve-Path "$($candidateOutput.Path)/obj/sm64-saturn-sourceboot-e2.elf").Path
      $candidateMemoryReport = "docs/saturn/evidence/reports/task3-camera-memory-candidate-q$($candidateOutput.Bits)-2026-07-29.json"
      .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $candidateElf --phase "candidate-q$($candidateOutput.Bits)" --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-fixed-baseline-2026-07-29.json --required-final-margin 0x1B00 --output $candidateMemoryReport
  }
  ```

  Each report records the exact `___end` delta from the fixed baseline. A
  report exists if and only if that candidate is range-qualified.

- [ ] **Step 10: Commit the arithmetic foundation**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q_math.h src/port/saturn/runtime/saturn_camera_q_math.c src/port/saturn/sourceboot/Makefile tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py docs/saturn/PROVENANCE.md THIRD_PARTY_LICENSES.md
  if (Test-Path docs/saturn/evidence/reports/task3-camera-memory-candidate-q16-2026-07-29.json) {
      git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add docs/saturn/evidence/reports/task3-camera-memory-candidate-q16-2026-07-29.json
  }
  if (Test-Path docs/saturn/evidence/reports/task3-camera-memory-candidate-q12-2026-07-29.json) {
      git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add docs/saturn/evidence/reports/task3-camera-memory-candidate-q12-2026-07-29.json
  }
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: add candidate camera Q arithmetic"
  ```

  If Q16 required no provenance-file edit, omit unchanged paths from `git
  add`.

---

### Task 7: Complete the Numeric Differential and Freeze the Q Format

**Files:**

- Modify: `src/port/saturn/runtime/saturn_camera_q_math.h`
- Modify: `src/port/saturn/runtime/saturn_camera_q_math.c`
- Modify: `src/port/saturn/runtime/saturn_engine_math_q16.h`
- Modify: `src/engine/math_util.c`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Create: `tools/saturn/freeze_camera_q_format.py`
- Create: `tools/saturn/test_freeze_camera_q_format.py`
- Create: `tools/saturn/camera_q_object_contract.py`
- Create: `tools/saturn/test_camera_q_object_contract.py`
- Create after differential:
  `tools/saturn/fixtures/bob_camera_q_format_v1.json`
- Create after differential:
  `src/port/saturn/runtime/saturn_camera_q_config.h`
- Create when Q16 is range-qualified:
  `docs/saturn/evidence/reports/task3-camera-q16-differential-2026-07-29.json`
- Create when Q12 is qualified:
  `docs/saturn/evidence/reports/task3-camera-q12-differential-2026-07-29.json`
- Create after selection:
  `docs/saturn/evidence/reports/task3-camera-range-format-2026-07-29.json`
- Create after selected-format link:
  `docs/saturn/evidence/reports/task3-camera-memory-numeric-2026-07-29.json`
- Create after selected-format link:
  `docs/saturn/evidence/reports/task3-camera-q-object-equivalence-2026-07-29.json`
- Reuse: `src/port/saturn/gfx/saturn_trig_q16.inc.c`

**Interfaces:**

- Consumes: Task 5's candidate/range fixture and Task 6's candidate word
  arithmetic and DIVU transport.
- Produces:
  `sm64_saturn_atan2s_i32`,
  `sm64_saturn_camera_q_hor_distance`,
  `sm64_saturn_camera_q_abs_distance`,
  `sm64_saturn_camera_q_polar_from_points`,
  `sm64_saturn_camera_q_polar_to_point`,
  `sm64_saturn_camera_q_approach_symmetric`,
  `sm64_saturn_camera_q_approach_asymmetric`, and
  `sm64_saturn_camera_q_vec3_asymptotic`, plus
  `canonicalize_sh_objdump(text: str) -> bytes` and the selected-candidate
  object-equivalence CLI.

- [ ] **Step 1: Write failing two-word square/sum/isqrt tests**

  Test zero, one, perfect-square neighbors, maximum captured horizontal and
  absolute vectors, a carry from `lo` into `hi`, and the measured largest
  square sum. Compare against Python integer arithmetic. Add mutations that
  drop the carry and start the restoring-square-root bit two positions low;
  both must fail.

- [ ] **Step 2: Implement square accumulation and integer square root**

  Use `saturn_camera_dmuls_words` for each signed component square and add
  magnitudes with explicit carry. Implement a restoring integer square root on
  `saturn_camera_u64_t` using only two-word shifts, compares, add/subtract, and
  a 32-bit result. Do not reuse the render helper's target C `int64_t`
  implementation. Expose:

  ```c
  bool sm64_saturn_camera_q_hor_distance(
      const saturn_camera_q_vec3_t *left,
      const saturn_camera_q_vec3_t *right,
      saturn_camera_q_t *distance);
  bool sm64_saturn_camera_q_abs_distance(
      const saturn_camera_q_vec3_t *left,
      const saturn_camera_q_vec3_t *right,
      saturn_camera_q_t *distance);
  ```

- [ ] **Step 3: Factor the existing integer atan2 core without changing its float ABI**

  Write a failing Task 2 regression test that feeds the existing 128-record
  atan2 corpus through both the float wrapper and a new integer entry point.
  Add:

  ```c
  s16 sm64_saturn_atan2s_i32(s32 y, s32 x);
  ```

  Move only the already-reviewed integer quadrant/table logic into that
  function. The existing `atan2s(f32, f32)` target branch still owns raw-route
  capture and calls the integer function after its existing conversion.
  Require the old Task 2 fixture and mutation tests to remain byte-exact.

- [ ] **Step 4: Write failing trig and polar tests**

  Cover all axes, every quadrant, angle wrap at `0x7FFF/0x8000`, near-zero
  deltas, maximum captured distances, and round-trip reconstruction. Compare
  against the in-tree float formulas and the selected-Q bound. Mutate sine and
  cosine indices independently and require failures.

- [ ] **Step 5: Implement trig lookup and polar operations**

  Read `gSaturnSineTableQ16` directly through small runtime lookup functions
  matching the engine's `(u16)angle >> 4` indexing. Use
  `sm64_saturn_camera_q_mul_trig_checked`, the distance helpers, and
  `sm64_saturn_atan2s_i32`. Expose checked equivalents of calculate
  pitch/yaw/angles, `rotate_in_xz`, `rotate_in_yz`, polar extraction, and polar
  reconstruction. Every failure returns `false` to the state machine; no
  helper silently clamps.

- [ ] **Step 6: Write failing approach and exact-target tests**

  Cover positive and negative motion, asymmetric increments/decrements, zero
  crossing, both tie parities, one-LSB residuals, step larger than delta, and
  exact target. Require round-to-nearest ties-to-even for the computed step and
  exact snap when the remaining absolute delta is no larger than one step.
  Mutate `<=` to `<` in the snap and ties-even to ties-away; each mutation must
  fail.

- [ ] **Step 7: Implement scalar/vector approach functions**

  Add checked symmetric, asymmetric, and vec3 operations. Keep all current,
  target, and step values in selected Q. Preserve source update order and
  return whether the exact target was reached where the float caller depends
  on that boolean.

- [ ] **Step 8: Run the full production differential for every candidate**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py --candidate-fixture tools\saturn\fixtures\bob_camera_q_candidates_v1.json --emit-differential-dir docs\saturn\evidence\reports"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_engine_atan2_q16.py"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_engine_atan2_mutation.py"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_q_object_contract.py
  ```

  For each range-qualified candidate, compile the actual
  `saturn_camera_q_math.c` with that candidate bit count and compare every
  public numeric API against the in-tree float formulas over:

  - all literal boundary/tie/sign/overflow/zero cases;
  - all 256 raw SCR1 corpus entries;
  - every per-field measured extremum;
  - every transition numerator/divisor pair;
  - a deterministic seed-expanded neighborhood around each captured value.

  The report includes source/config/candidate-fixture hashes, case counts,
  maximum absolute/ULP error per operation, and the first mismatch. A code,
  mutation, overflow, or unexplained mismatch is a hard failure, not grounds
  to choose the other format. Only a reviewed representational-bound failure
  may disqualify Q16 and allow an already-passing Q12 candidate.
  Emit a report only for a range-qualified candidate; the candidate fixture
  itself is the authoritative proof that an omitted Q16 or Q12 report was
  ineligible rather than silently skipped.

  Build both qualified target objects and reject soft-float/libm/64-div
  references from all new numeric functions. Run the memory-map phase gate
  for each and record exact HWRAM deltas.

- [ ] **Step 9: Freeze the deterministic winner and production config**

  Test `freeze_camera_q_format.py` with passing Q16/Q12, Q12-only when Q16 is
  range-ineligible, Q16 representational failure plus passing Q12, a
  code-failure report, mismatched hashes, a missing report for an eligible
  candidate, a supplied report for an ineligible candidate, missing Q12
  intermediate proof, and both candidates failing. Selection order is Q16
  first, then Q12 when Q16 was range-ineligible or under the reviewed
  representational-bound exception above.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_freeze_camera_q_format.py
  $candidateFixture = Get-Content tools/saturn/fixtures/bob_camera_q_candidates_v1.json -Raw | ConvertFrom-Json
  $freezeArgs = @(
      "--candidates", "tools/saturn/fixtures/bob_camera_q_candidates_v1.json",
      "--output-fixture", "tools/saturn/fixtures/bob_camera_q_format_v1.json",
      "--output-header", "src/port/saturn/runtime/saturn_camera_q_config.h",
      "--output-report", "docs/saturn/evidence/reports/task3-camera-range-format-2026-07-29.json"
  )
  if ($candidateFixture.candidates.q16.range_qualified) {
      $freezeArgs += @("--q16-report", "docs/saturn/evidence/reports/task3-camera-q16-differential-2026-07-29.json")
  }
  if ($candidateFixture.candidates.q12.range_qualified) {
      $freezeArgs += @("--q12-report", "docs/saturn/evidence/reports/task3-camera-q12-differential-2026-07-29.json")
  }
  & .venv-saturn-tools/Scripts/python.exe tools/saturn/freeze_camera_q_format.py @freezeArgs
  ```

  Omit each report argument only when that exact candidate is not
  range-qualified. The script independently checks this presence matrix
  against the fixture before selecting. The generated header defines
  `SM64_SATURN_CAMERA_Q_FRACTION_BITS`, every signed operand envelope, Q12
  intermediate-Q16 bounds when selected, and the derived comparison bound.
  The generated format fixture records the selected integer under the exact
  top-level key `fraction_bits`.
  Regenerate ordinary variant-2 objects without the provisional
  `SATURN_CAMERA_Q_CANDIDATE_BITS` flag, require identical numeric object code
  to the selected candidate build, and chain the selected linked image from
  that candidate's memory report:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $cameraFormat = Get-Content tools/saturn/fixtures/bob_camera_q_format_v1.json -Raw | ConvertFrom-Json
  $selectedBits = [int]$cameraFormat.fraction_bits
  if ($selectedBits -notin @(12, 16)) { throw "invalid frozen camera Q format" }
  $selectedCandidateMemory = "docs/saturn/evidence/reports/task3-camera-memory-candidate-q$selectedBits-2026-07-29.json"
  if (-not (Test-Path $selectedCandidateMemory)) { throw "selected candidate memory report is missing" }
  $task7QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task7QBuildCommand
  $task7QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $task7QElf = (Resolve-Path "$task7QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $task7QElf --phase frozen-numeric --stage-sectors $cameraStageSectors --previous-report $selectedCandidateMemory --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-numeric-2026-07-29.json
  $selectedCandidateOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-qbits$selectedBits-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $candidateQMathObjects = @(Get-ChildItem -LiteralPath "$selectedCandidateOutput/obj" -Recurse -Filter saturn_camera_q_math.o -File)
  $productionQMathObjects = @(Get-ChildItem -LiteralPath "$task7QOutput/obj" -Recurse -Filter saturn_camera_q_math.o -File)
  if ($candidateQMathObjects.Count -ne 1 -or $productionQMathObjects.Count -ne 1) { throw "camera Q math object resolution is ambiguous" }
  .venv-saturn-tools/Scripts/python.exe tools/saturn/camera_q_object_contract.py --candidate $candidateQMathObjects[0].FullName --production $productionQMathObjects[0].FullName --objdump D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe --expected-fraction-bits $selectedBits --output docs/saturn/evidence/reports/task3-camera-q-object-equivalence-2026-07-29.json
  ```

  The object-contract tool prepends `C:\msys64\usr\bin` only in its child
  environment, canonicalizes both `sh-elf-objdump -dr` streams by stripping
  only their input banner and normalizing CRLF to LF, and requires exact
  symbol/instruction/relocation equality. It emits raw and canonical hashes
  only after equality. Tests mutate one opcode, relocation, symbol, and
  section size independently.

- [ ] **Step 10: Commit the complete numeric kernel and frozen format**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q_math.h src/port/saturn/runtime/saturn_camera_q_math.c src/port/saturn/runtime/saturn_camera_q_config.h src/port/saturn/runtime/saturn_engine_math_q16.h src/engine/math_util.c tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/freeze_camera_q_format.py tools/saturn/test_freeze_camera_q_format.py tools/saturn/camera_q_object_contract.py tools/saturn/test_camera_q_object_contract.py tools/saturn/fixtures/bob_camera_q_format_v1.json docs/saturn/evidence/reports/task3-camera-range-format-2026-07-29.json docs/saturn/evidence/reports/task3-camera-memory-numeric-2026-07-29.json docs/saturn/evidence/reports/task3-camera-q-object-equivalence-2026-07-29.json
  if (Test-Path docs/saturn/evidence/reports/task3-camera-q16-differential-2026-07-29.json) {
      git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add docs/saturn/evidence/reports/task3-camera-q16-differential-2026-07-29.json
  }
  if (Test-Path docs/saturn/evidence/reports/task3-camera-q12-differential-2026-07-29.json) {
      git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add docs/saturn/evidence/reports/task3-camera-q12-differential-2026-07-29.json
  }
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: freeze camera Q numeric kernel"
  ```

---

### Task 8: Extend the Static Audit Contract Before Camera Integration

**Files:**

- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Preserve byte-for-byte:
  `tools/saturn/compare_sh2_native_math_audit_reports.py`
- Preserve byte-for-byte:
  `tools/saturn/test_compare_sh2_native_math_audit_reports.py`
- Create: `tools/saturn/generate_camera_q_audit_contract.py`
- Create: `tools/saturn/test_generate_camera_q_audit_contract.py`
- Create: `tools/saturn/verify_camera_q_mutation.py`
- Create: `tools/saturn/test_verify_camera_q_mutation.py`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Reuse: `tools/saturn/camera_q_object_contract.py`
- Preserve unchanged:
  `tools/saturn/sh2_native_math_sim_audit_contract_v2.txt`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json`
- Preserve unchanged:
  `docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json`

**Interfaces:**

- Consumes: Task 3's corrected, independently reviewed, re-pinned v2
  parser/contract and executable-code-derived direct-call graph.
- Produces: a version-dispatched v3 parser supporting repeated
  `EXPECTED_ROOT`, `EXPECTED_CALLER`, and `STOP_BRIDGE` records; an exact
  closure report; deterministic raw-object and canonical-disassembly
  `Q_OBJECT` records; a deterministic JSON report; a candidate-contract
  generator; an object-reference-only mode; a code-owned zero-helper ceiling
  for every stop bridge; and seven audit mutations.

- [ ] **Step 1: Freeze the corrected v2 parser and re-pinned result in regression tests**

  Assert the post-Task-3 v2 contract digest, one-root schema, measured
  expected total, forbidden callers, code-only parser version, SH
  delay-slot/switch/internal-offset behavior, zero unresolved transfers,
  failure text, and successful report shape. Revalidate the report's
  historical parser/verifier/test/contract bytes from its exact ancestor
  `repin_source_commit`, then independently run the current verifier against
  the retained route ELFs and require complete v2 fact equality with the
  frozen corrected observations. Current verifier SHA equality to the
  historical Task 3 SHA is not a gate. Copy the corrected v2 contract into a
  temporary directory, mutate each directive, and require the frozen
  post-re-pin failures before adding v3. Reject
  `analysis-mode=legacy-linear` in every acceptance path.

  The comparator tests require a harmless post-v3 current-verifier byte
  change with identical v2 facts to pass, but historical-byte hash mismatch,
  non-ancestor `repin_source_commit`, dirty/substituted evidence, and current
  v2 fact drift to fail.

  Run:

  ```powershell
  $repoRoot = "D:/Code/RetroDev/sm64-saturn-port/sm64-port"
  $repinPath = "docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json"
  $repin = Get-Content $repinPath -Raw | ConvertFrom-Json
  $parserCommit = [string]$repin.review.parser_commit
  $evidenceWorktree = "$repoRoot/.worktrees/audit-parser-evidence-$($parserCommit.Substring(0, 7))"
  $route0Elf = (Resolve-Path (Join-Path $evidenceWorktree ([string]$repin.artifacts.route0_elf_relative_path))).Path
  $route1Elf = (Resolve-Path (Join-Path $evidenceWorktree ([string]$repin.artifacts.route1_elf_relative_path))).Path
  $auditObjdump = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe"
  $auditReadelf = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe"
  $auditAddr2line = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-addr2line.exe"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_sh2_native_math_audit_reports.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/compare_sh2_native_math_audit_reports.py verify-final --report $repinPath --current-verifier tools/saturn/verify_sh2_native_math.py --current-v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --baseline tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --route0-elf $route0Elf --route1-elf $route1Elf --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  ```

  Expected result: all existing tests and the new v2-freeze cases pass.

- [ ] **Step 2: Write failing v3 schema tests**

  Define the v3 grammar through tests:

  ```text
  AUDIT_CONTRACT_VERSION 3
  ELF_SHA256 <64-lowercase-hex-linked-elf-sha256>
  V2_CONTRACT_SHA256 <64-lowercase-hex-v2-contract-sha256>
  OBJECT_DISASSEMBLY_CANONICALIZATION 1
  EXPECTED_HELPER_TOTAL 500
  EXPECTED_ROOT _saturn_camera_q_default_seam_tick
  EXPECTED_ROOT _saturn_camera_q_lakitu_seam_tick
  EXPECTED_CALLER _saturn_camera_q_default_tick
  EXPECTED_CALLER _saturn_camera_q_publish_bridge
  STOP_BRIDGE _saturn_camera_bridge_find_floor floor 0
  STOP_BRIDGE _saturn_camera_cold_float_fallback_tick cold-fallback 0
  Q_OBJECT camera <64-lowercase-hex-raw-sha256> <64-lowercase-hex-disassembly-sha256>
  FORBIDDEN_CALLER _atan2_lookup
  FORBIDDEN_CALLER _atan2s
  ```

  Require exactly one `ELF_SHA256`, `V2_CONTRACT_SHA256`,
  `OBJECT_DISASSEMBLY_CANONICALIZATION`, and `EXPECTED_HELPER_TOTAL`; require
  canonicalization version `1`; require any number of `EXPECTED_CALLER` rows;
  require unique symbols/logical object names; exactly four Q-object rows for
  `camera`, `math_util`, `saturn_camera_q`, and `saturn_camera_q_math`;
  lowercase SHA-256 fields; a nonnegative exact helper count per stop; a
  generated transitive closure from all roots; zero helper edges for every
  non-stop closure caller; exact helper counts at stops; and a global exact
  total lower than the supplied v2 comparison total.

  Ordinary camera-v3 verification has these code-owned invariants, independent
  of contract contents:

  ```text
  roots:
    _saturn_camera_q_default_seam_tick
    _saturn_camera_q_lakitu_seam_tick
    _saturn_camera_q_default_tick
    _saturn_camera_q_lakitu_tick
    _saturn_camera_q_next_lakitu_state
    _saturn_camera_q_publish_bridge
  stops:
    _saturn_camera_bridge_find_floor             floor
    _saturn_camera_bridge_find_ceil              ceil
    _saturn_camera_bridge_find_wall_collision    wall-collision
    _saturn_camera_bridge_rotate_around_walls    rotate-walls
    _saturn_camera_bridge_collide_with_walls     collide-walls
    _saturn_camera_bridge_is_range_behind_surface range-surface
    _saturn_camera_bridge_find_water_level       water-level
    _saturn_camera_bridge_find_poison_gas_level  poison-gas
    _saturn_camera_cold_float_fallback_tick      cold-fallback
  forbidden:
    _atan2_lookup
    _atan2s
  maximum direct helper edges at each stop:
    _saturn_camera_bridge_find_floor              0
    _saturn_camera_bridge_find_ceil               0
    _saturn_camera_bridge_find_wall_collision     0
    _saturn_camera_bridge_rotate_around_walls     0
    _saturn_camera_bridge_collide_with_walls      0
    _saturn_camera_bridge_is_range_behind_surface 0
    _saturn_camera_bridge_find_water_level        0
    _saturn_camera_bridge_find_poison_gas_level   0
    _saturn_camera_cold_float_fallback_tick       0
  ```

  The contract must contain exactly those six root directives, nine
  symbol/reason stop pairs, and two forbidden directives: no missing, extra,
  renamed, duplicated, or reordered semantic substitute is accepted.
  The verifier owns the exact zero-helper stop-ceiling map above in code; it
  is not read from or learned from the generated contract. A bridge may call
  its named unchanged environmental/legacy API, but raw-bit marshalling keeps
  the bridge wrapper itself free of direct soft-float, libm, and 64-bit
  division/modulo helper edges. Both generation and verification reject an
  observed stop count above the code-owned ceiling before accepting or
  writing contract text.
  `--inspection-only` alone may request a strict subset of the code-owned roots
  and stops; it always applies both forbidden callers. Ordinary v3
  verification hashes the input ELF and v2 contract bytes and requires exact
  equality with the two embedded directives before graph analysis.

  Expected failure: the current parser rejects contract version 3.

- [ ] **Step 3: Implement v3 without changing corrected v2**

  Add a version-dispatched parser and typed records. Use the existing
  corrected executable-code/call-edge model; do not add a linear objdump
  scanner or exact-target filter. Walk all direct edges from every root, stop
  before traversing a named bridge, and compare the generated caller set
  exactly with `EXPECTED_CALLER` rows. Report roots, closure callers, stops,
  stop counts, global total, v2 delta, ELF digest, contract digest, and both
  digests for every Q object separately.

  Add optional `--v2-contract PATH`, `--q-object-manifest PATH`,
  `--reference-q-object-manifest PATH`,
  `--reference-q-object-manifest-sha256 HEX`, and `--json-output PATH` CLI
  arguments. Ordinary linked-ELF verification and generation retain the
  required Task 3 `--readelf PATH`; object-reference-only neither accepts nor
  needs it. The generated build manifest grammar is exactly one
  `logical_name<TAB>absolute_object_path` row for each of the four names
  above, sorted by logical name, with no comments or duplicate paths. For
  every object, hash the raw bytes and call Task 7's
  `canonicalize_sh_objdump` on `sh-elf-objdump -dr OBJECT`; do not implement a
  second banner/line-ending normalizer. The JSON output uses sorted keys,
  records the canonicalization version, and is written only after every gate
  passes.

  Add a mutually exclusive `--object-reference-only` mode for route-0 Q.
  Both v3 modes require `--v2-contract`, hash its exact bytes, and compare it
  with `V2_CONTRACT_SHA256`. Object-reference-only also requires both
  object-manifest arguments plus the expected reference-manifest SHA-256,
  rejects identical resolved manifest paths, first proves the reference
  manifest file hash and route-1 objects match all four `Q_OBJECT` records,
  then proves the route-0 candidate objects match the same records.

  Preserve the existing positional CLI for ordinary v1/v2/v3 audits by
  changing its `elf` and `baseline` positionals to `nargs="?"` and enforcing
  them manually outside object-reference-only mode. Object-reference-only
  accepts exactly `--audit-contract`, `--v2-contract`,
  `--q-object-manifest`, `--reference-q-object-manifest`,
  `--reference-q-object-manifest-sha256`, and `--objdump`; it rejects either
  positional, `--route-oracle`, `--audit-route-oracle`, `--addr2line`, or
  `--json-output`, or `--readelf`. Tests prove the exact object-only set
  succeeds without readelf and fails when readelf is supplied. Ordinary mode
  rejects either reference-manifest argument and performs no artifact-hash
  relaxation.

  Reject:

  - a missing root or expected caller;
  - an extra reachable caller omitted from the contract;
  - a helper edge in a generated non-stop caller;
  - a stop-bridge helper count mismatch or an observed stop count above the
    independent code-owned zero ceiling;
  - a global total equal to or greater than v2;
  - a forbidden `_atan2_lookup` or `_atan2s` caller;
  - an ELF/v2-contract digest mismatch or unsupported canonicalization
    version;
  - a missing, extra, renamed, duplicated, raw-mismatched, or
    disassembly-mismatched Q object;
  - duplicate or unrecognized directives.

- [ ] **Step 4: Add seven independent audit mutations**

  `verify_camera_q_mutation.py` operates on copied synthetic objdump/call-graph
  fixtures and proves that the v3 gate rejects:

  1. one restored soft-float helper edge in a Q closure caller;
  2. one expected caller removed from the contract rather than the binary;
  3. one extra helper edge at a stopped bridge;
  4. one required root directive removed;
  5. one required forbidden directive removed;
  6. one required stop removed; and
  7. one injected extra stop immediately above a helper-bearing caller.

  The mutation CLI returns nonzero if a mutated fixture unexpectedly passes.

- [ ] **Step 5: Add the deterministic candidate-contract generator**

  Write synthetic graph tests requiring stable sorted roots/callers/stops,
  exact per-stop helper counts, embedded ELF/v2-contract SHA-256 values, the
  four stable sorted `Q_OBJECT` rows, and refusal when the candidate total is
  not lower than v2. Feed a synthetic stop with one direct helper edge and
  require ordinary generation and inspection-only generation both to fail
  before writing output, even when the requested contract count is also one.
  Require `--q-object-manifest PATH`; mutate one raw byte, one disassembly
  instruction, one logical name, and one path and prove each is rejected by
  verification. Implement the generator by calling the v3
  graph/attribution/object functions from `verify_sh2_native_math.py`; those
  object functions import Task 7's canonicalizer. Do not add a second
  disassembly parser or canonicalizer.

  Add a tested `--inspection-only` generator mode for pre-contract phase
  gates. It accepts the same ELF, v2, object-manifest, root, stop, objdump, and
  output arguments, but writes deterministic JSON with
  `"acceptance_contract": false` instead of contract text. It still rejects a
  missing root, a helper edge before a named stop, a forbidden atan2 caller,
  malformed object evidence, or an unrecognized argument; it reports but does
  not reject a non-lower global total. Ordinary generation remains the only
  mode that writes v3 text and must reject a non-lower total.

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  ```

  Expected result: all synthetic generation and refusal cases pass.

- [ ] **Step 6: Wire pure audit tests and the dormant object manifest into sourceboot**

  Add the three camera-audit Python test scripts to the sourceboot `verify`
  prerequisites.
  Do not switch `SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT` to v3 yet; final
  measured v3 does not exist until Task 14.
  Add `SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_V2_CONTRACT` bound to the corrected,
  re-pinned v2 path. When Task 14 selects v3, the Makefile must pass that path
  as `--v2-contract`; v2 verification keeps Task 3's code-only CLI and
  behavior, including `--readelf`.

  Add a generated
  `$(SH_BUILD_PATH)/camera-q-objects.tsv` target whose four rows are produced
  from Yaul's `macro-convert-build-path` for:

  ```text
  camera                 src/game/camera.c
  math_util              src/engine/math_util.c
  saturn_camera_q        src/port/saturn/runtime/saturn_camera_q.c
  saturn_camera_q_math   src/port/saturn/runtime/saturn_camera_q_math.c
  ```

  Declare the rule in Task 8 but leave it dormant only until Task 9 adds
  `saturn_camera_q.c`, completing the four-object source set. Task 9 makes it a
  post-object prerequisite of every variant-2 link thereafter, including the
  Task 10/13 dry runs, Task 14's pre-v3 link, and the route-0 reference gate.
  It must not depend on an existing v3 contract. Add synthetic tests for exact
  row ordering and path resolution. Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_mutation.py
  ```

- [ ] **Step 7: Commit the backward-compatible audit extension**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add tools/saturn/verify_sh2_native_math.py tools/saturn/test_verify_sh2_native_math.py tools/saturn/generate_camera_q_audit_contract.py tools/saturn/test_generate_camera_q_audit_contract.py tools/saturn/verify_camera_q_mutation.py tools/saturn/test_verify_camera_q_mutation.py src/port/saturn/sourceboot/Makefile
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: extend native math audit for camera closures"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_sh2_native_math_audit_reports.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/compare_sh2_native_math_audit_reports.py verify-final --report $repinPath --current-verifier tools/saturn/verify_sh2_native_math.py --current-v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --baseline tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --route0-elf $route0Elf --route1-elf $route1Elf --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  ```

  The post-commit call is the actual compatibility proof for Task 8's changed
  verifier bytes. It must pass on frozen v2 facts before Task 9 begins.

---

### Task 9: Implement the Persistent Shadow Lifecycle and Public Mirror

**Files:**

- Create: `src/port/saturn/runtime/saturn_camera_q.h`
- Create: `src/port/saturn/runtime/saturn_camera_q.c`
- Modify: `src/game/camera.c`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/source_camera_idle_probe.h`
- Modify: `src/port/saturn/sourceboot/source_camera_idle_probe.c`
- Modify: `src/port/saturn/runtime/saturn_camera_probe.h`
- Modify: `tools/saturn/camera_idle_contract.py`
- Modify: `tools/saturn/test_camera_idle_contract.py`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Modify: `tools/saturn/verify_camera_q_writers.py`
- Create after target link:
  `docs/saturn/evidence/reports/task3-camera-memory-shadow-2026-07-29.json`

**Interfaces:**

- Consumes: Task 4's writer contract, Task 7's frozen config/numeric kernel,
  Task 3's probe ABI, and Task 8's dormant Q-object-manifest rule.
- Produces:
  `saturn_camera_q_state_t`,
  `saturn_camera_q_import_t`,
  `saturn_camera_q_mirror_t`,
  `sm64_saturn_camera_q_seed`,
  `sm64_saturn_camera_q_invalidate`,
  `sm64_saturn_camera_q_make_mirror`, and private camera adapters
  `saturn_camera_q_prepare_tick`,
  `saturn_camera_q_publish_bridge`, and
  `saturn_camera_cold_float_fallback_tick`, plus an active
  `$(SH_BUILD_PATH)/camera-q-objects.tsv` prerequisite for every variant-2
  ELF and the raw `sourceboot_camera_q_seed_trace` (`SQT1`) symbol.

- [ ] **Step 1: Define an explicit shadow with a hard size budget**

  Add named Q fields for Camera position/focus/area center, all Lakitu
  goal/current/render vectors and speed/scalar state, old position/focus,
  both transition endpoints and live transition polar values, pan/zoom
  globals, yaw/distance/pitch globals, integer angles/modes/frames, area
  identity, source dispatch, validity, ever-seeded state, generation, and the
  eight diagnostic counts.

  Use these nested named records rather than an untyped scalar array:

  ```c
  typedef enum saturn_camera_q_dispatch {
      SATURN_CAMERA_Q_DISPATCH_NONE = 0,
      SATURN_CAMERA_Q_DISPATCH_DEFAULT = 1,
      SATURN_CAMERA_Q_DISPATCH_LAKITU = 2,
      SATURN_CAMERA_Q_DISPATCH_MARIO = 3,
  } saturn_camera_q_dispatch_t;

  typedef struct saturn_camera_q_polar {
      saturn_camera_q_t distance;
      int16_t pitch;
      int16_t yaw;
  } saturn_camera_q_polar_t;

  typedef struct saturn_camera_q_transition_endpoint {
      saturn_camera_q_vec3_t focus;
      saturn_camera_q_vec3_t position;
      saturn_camera_q_polar_t polar;
  } saturn_camera_q_transition_endpoint_t;

  typedef struct saturn_camera_q_transition {
      saturn_camera_q_transition_endpoint_t start;
      saturn_camera_q_transition_endpoint_t end;
      saturn_camera_q_vec3_t mario_position;
      saturn_camera_q_t position_distance;
      saturn_camera_q_t focus_distance;
      int16_t position_pitch;
      int16_t position_yaw;
      int16_t focus_pitch;
      int16_t focus_yaw;
      int32_t frames_left;
      int16_t new_mode;
      int16_t last_mode;
      int16_t max_frames;
      int16_t frame;
  } saturn_camera_q_transition_t;

  typedef struct saturn_camera_q_lakitu {
      saturn_camera_q_vec3_t current_position;
      saturn_camera_q_vec3_t current_focus;
      saturn_camera_q_vec3_t goal_position;
      saturn_camera_q_vec3_t goal_focus;
      saturn_camera_q_vec3_t render_position;
      saturn_camera_q_vec3_t render_focus;
      saturn_camera_q_t focus_h_speed;
      saturn_camera_q_t focus_v_speed;
      saturn_camera_q_t position_h_speed;
      saturn_camera_q_t position_v_speed;
      saturn_camera_q_t focus_distance;
      int16_t yaw;
      int16_t next_yaw;
      int16_t roll;
      int16_t old_pitch;
      int16_t old_yaw;
      int16_t old_roll;
      uint8_t mode;
      uint8_t default_mode;
  } saturn_camera_q_lakitu_t;

  typedef struct saturn_camera_q_state {
      saturn_camera_q_vec3_t camera_position;
      saturn_camera_q_vec3_t camera_focus;
      saturn_camera_q_vec3_t area_center;
      saturn_camera_q_vec3_t old_position;
      saturn_camera_q_vec3_t old_focus;
      saturn_camera_q_lakitu_t lakitu;
      saturn_camera_q_transition_t transition;
      saturn_camera_q_t camera_zoom_distance;
      saturn_camera_q_t zoom_amount;
      saturn_camera_q_t pan_distance;
      saturn_camera_q_t zero_zoom_distance;
      int16_t camera_yaw;
      int16_t camera_next_yaw;
      int16_t yaw_speed;
      int16_t lakitu_distance;
      int16_t lakitu_pitch;
      int16_t mode_offset_yaw;
      int16_t area_yaw;
      uint16_t level_number;
      uint16_t area_index;
      uint8_t camera_mode;
      uint8_t camera_default_mode;
      uint8_t camera_cutscene;
      uint8_t camera_door_status;
      saturn_camera_q_dispatch_t source_dispatch;
      bool valid;
      bool ever_seeded;
      sm64_saturn_camera_q_diagnostics_t diagnostics;
  } saturn_camera_q_state_t;

  _Static_assert(sizeof(saturn_camera_q_state_t) <= 2048U,
                 "camera Q shadow exceeds its HWRAM budget");
  ```

  Add the one private guarded `sSaturnCameraQState` after the existing camera
  globals and before transition-function declarations. Do not interleave it
  with the legacy BSS declarations.

- [ ] **Step 2: Define integer-only seed and mirror records**

  Define `saturn_camera_q_import_t` and `saturn_camera_q_mirror_t` with the
  same named semantic fields as the shadow, but represent every float input or
  output as raw `uint32_t` bits. Define:

  ```c
  bool sm64_saturn_camera_q_seed(
      saturn_camera_q_state_t *state,
      const saturn_camera_q_import_t *source);
  void sm64_saturn_camera_q_invalidate(
      saturn_camera_q_state_t *state,
      uint32_t reason,
      bool unexpected);
  bool sm64_saturn_camera_q_make_mirror(
      const saturn_camera_q_state_t *state,
      saturn_camera_q_mirror_t *mirror);
  ```

- [ ] **Step 3: Write failing seed completeness and range-guard tests**

  Build a distinct bit pattern for every import field and require a seed then
  mirror to preserve each one within the selected-Q bound. Remove each field
  in turn from the seed fixture and require failure. Test non-finite and
  out-of-envelope inputs, area/mode mismatch, unsupported dispatch, active
  cutscene, and a transition divisor outside the proved envelope.

  Eligibility must be based on selected dispatch. A Mario dispatch with stored
  radial mode is accepted; a radial dispatch without Mario selection is not.

- [ ] **Step 4: Implement seed, validation, and armed-counter semantics**

  Convert every raw field with the checked bit bridge before setting valid.
  On any failure, leave the shadow invalid. A pre-seed unsupported bootstrap
  does not increment acceptance counters. After the first successful seed,
  unsupported/range failure increments `range_fallback_count`; an
  unclassified reseed increments `unexpected_reseed_count`. Generation
  increments at every successful seed and every post-seed external
  invalidation. Pre-seed init/warp/area invalidations keep generation zero;
  `ever_seeded` arms later invalidation accounting only after the first
  complete seed.

- [ ] **Step 5: Write failing persistence, invalidation, and seed-trace tests**

  Prove that 600 no-op ticks retain exact Q bits and one generation; ordinary
  publish never reseeds; init, warp, area change, mode change, cutscene
  entry/exit, classified float-API correction, and later external writer each
  invalidate once; and a partial public-state change never merges into a valid
  shadow. Mutate validity, generation, and one invalidation hook separately.

  Add a pure raw decoder for an exact 16-byte, four-big-endian-word `SQT1`
  record:

  ```text
  word 0: 0x53515431 ("SQT1"), published last
  word 1: version 1 in bits 31:16, source dispatch 3 (Mario) in bits 15:0
  word 2: route ID 2
  word 3: first successful Q-seed source tick
  ```

  Baseline variant 1 requires all four words zero. Variant 2 requires the exact
  values above and word 3 equal to
  `first_mario_dispatch_tick(bob_default_camera_v1.json)`. Test short/long
  data, bad magic/version/dispatch/route, a zero or second seed tick, and a
  target tick that disagrees with the manifest-derived one. In the target
  recorder test, initialize/invalidate before the R-trigger, prove generation
  remains zero, then require payload words 1-3 to be written before magic and
  the tick-121 successful seed to publish exactly once.

- [ ] **Step 6: Implement camera-side adapters and the raw seed trace**

  `saturn_camera_q_prepare_tick` runs after legacy pre-dispatch writers. It
  determines the selected dispatch, validates range/state, performs a complete
  seed only when invalid, and marks the current tick Q-active. The probe
  exports `source_dispatch=MARIO` only when that Q-active Mario tick completed
  without fallback; it exports `NONE` for selection-only, invalid, bootstrap,
  and fallback ticks.

  `saturn_camera_q_publish_bridge` calls `make_mirror`, writes public fields in
  the writer-inventory source order through bit-copy unions, and is the only
  normal Q-tick writer of those float fields. It is noinline and called exactly
  once. Populate `sm64_saturn_camera_probe_read` diagnostics/generation from
  the shadow for variant 2.

  In `source_camera_idle_probe.c`, place
  `sourceboot_camera_q_seed_trace[4]` in ordinary HWRAM BSS, not the SCC LWRAM
  section. On each existing post-game-loop recorder call, observe the semantic
  snapshot and the existing sourceboot-owned source tick. When the trace is
  unpublished, generation is nonzero, and the probe reports a successfully
  completed Q-active Mario dispatch, write words 1-3 through the cache-through
  alias, then publish word 0 last. This explicit success predicate does not
  depend on observing a zero-to-nonzero transition. Never rewrite a published
  trace. Leave all four words zero for variant 1 and do not add another tick
  counter.

- [ ] **Step 7: Add the named cold fallback and prove counter behavior**

  Add noinline `saturn_camera_cold_float_fallback_tick`. It is called only
  after an armed eligibility/range failure, never during the pre-seed
  bootstrap. Its first operations invalidate and increment the visible
  fallback counter before dispatching the unchanged float behavior.

  Add a host seam fixture proving one call on an injected range violation and
  zero calls on the captured route corpus. The final v3 audit will stop at this
  symbol and pin its helper count.

- [ ] **Step 8: Re-run the writer inventory and measure HWRAM**

  Now that all four Q-object sources exist, activate Task 8's manifest target
  as a prerequisite of every `SATURN_CAMERA_VARIANT=2` ELF. Require the
  manifest timestamp to be no older than any listed object, require all four
  paths to resolve, and regenerate after any listed object rebuild. This
  target depends on the four objects, never on v3.

  Require every new write to be classified and every old normal-tick public
  write to be excluded from the active Q path. Build the post-lifecycle image
  and chain its exact map:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $task9QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task9QBuildCommand
  $task9QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $task9QElf = (Resolve-Path "$task9QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $task9QElf --phase shadow --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-numeric-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-shadow-2026-07-29.json
  ```

  Require at least `0x1B00` total HWRAM after the Q shadow. Record the exact
  `.text`, `.bss`, shadow-symbol, seed-trace symbol, and `___end` deltas. If
  that margin fails, first reduce shadow duplication; if still needed, rerun
  the Task 3 deterministic cart-stage selector from 8 to 4 sectors and repeat
  its cart-load/hash proof plus every report from Task 5 onward. Do not weaken
  the 4 KiB TLSF or `0x0B00` safety assertions.
  The report must attribute the seed trace's exact 16-byte HWRAM cost
  separately; it is not folded into the fixed `0x2F7C0` SCC LWRAM section.

- [ ] **Step 9: Run lifecycle, numeric, and probe tests**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py ShadowLifecycleTests"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_acceptance_route.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  ```

- [ ] **Step 10: Commit the lifecycle slice**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q.h src/port/saturn/runtime/saturn_camera_q.c src/port/saturn/runtime/saturn_camera_probe.h src/game/camera.c src/port/saturn/sourceboot/Makefile src/port/saturn/sourceboot/source_camera_idle_probe.h src/port/saturn/sourceboot/source_camera_idle_probe.c tools/saturn/camera_idle_contract.py tools/saturn/test_camera_idle_contract.py tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/verify_camera_q_writers.py docs/saturn/evidence/reports/task3-camera-memory-shadow-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: add persistent Saturn camera Q shadow"
  ```

---

### Task 10: Convert Lakitu Transition and Smoothing as One State-Machine Slice

**Files:**

- Modify: `src/port/saturn/runtime/saturn_camera_q.h`
- Modify: `src/port/saturn/runtime/saturn_camera_q.c`
- Modify: `src/game/camera.c`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Modify: `tools/saturn/camera_q_writer_contract_v1.json`
- Create after target link:
  `docs/saturn/evidence/reports/task3-camera-memory-lakitu-2026-07-29.json`

**Interfaces:**

- Consumes: Task 9's persistent shadow/lifecycle and Task 7's distance, polar,
  and approach operations.
- Produces:
  `saturn_camera_q_next_lakitu_state`,
  `saturn_camera_q_lakitu_tick`,
  `saturn_camera_q_lakitu_seam_tick`,
  `saturn_camera_bridge_find_floor`, and
  `saturn_camera_bridge_find_wall_collision`.

- [ ] **Step 1: Define direct, audit-visible environmental bridge APIs**

  Forward-declare `struct Surface` only in `camera.c`; pass it through the Q
  module as `uintptr_t`. Add direct external interfaces rather than indirect
  callbacks so the audit can see every edge:

  ```c
  bool saturn_camera_bridge_find_floor(
      const saturn_camera_q_vec3_t *query,
      saturn_camera_q_t y_offset,
      saturn_camera_q_t *height,
      uintptr_t *surface_token,
      bool *found);
  bool saturn_camera_bridge_find_wall_collision(
      saturn_camera_q_vec3_t *position,
      saturn_camera_q_t offset_y,
      saturn_camera_q_t radius,
      bool *collided);
  ```

  Implement each bridge noinline in `camera.c`: export one temporary vector via
  the bit bridge, call the unchanged float API, import only the returned
  height/corrected vector plus semantic found/collision booleans, increment one
  export and one import count, and return `false` only on conversion/range
  failure.

- [ ] **Step 2: Write failing `next_lakitu_state` differential cases**

  Cover:

  - no transition: exact current position/focus copy and unchanged yaw;
  - `CAM_FLAG_START_TRANSITION`: Mario displacement applied to old vectors,
    start polar fields updated, and flag cleared in source order;
  - positive `framesLeft`: distance/pitch/yaw velocities, start/collect DIVU
    scheduling, approach, polar reconstruction, floor/wall correction,
    decrement, and final yaw;
  - final transition tick and the zero-frame reset path;
  - angle wrap, negative angle velocity, zero distance, bridge miss, and bridge
    failure.

  Use literal boundaries plus every transition record captured in Task 5.
  Mutate the `framesLeft--` order, one polar from/to pair, and the second
  distance divisor; each mutation must fail.

- [ ] **Step 3: Implement pure Q transition logic**

  Add:

  ```c
  bool saturn_camera_q_next_lakitu_state(
      saturn_camera_q_state_t *state,
      saturn_camera_q_vec3_t *new_position,
      saturn_camera_q_vec3_t *new_focus,
      int16_t input_yaw,
      int16_t *output_yaw);
  ```

  Preserve the exact legacy update order from `camera.c:5405-5491`. Start DIVU
  operations before independent angle/vector work and collect only when the
  quotient is needed. Propagate any arithmetic or bridge failure to the seam;
  do not publish a partial transition.

- [ ] **Step 4: Write failing Lakitu smoothing differential cases**

  Exercise exact old-state copy, goal mirrors, asymptotic current position and
  focus, all four speed returns, yaw approach, render position/focus,
  focus-distance and old-angle extraction, roll zero, transition Mario
  position, pitch clamp, and final mode/defMode. Include one-LSB residuals and
  the captured 600-tick stable state.

  Add eligibility/fallback cases for nonzero cutscene/player-2 offsets,
  scheduled shake state, dive-entry shake, handheld/key-dance roll, C-up mode,
  and any post-adjustment not represented in Q. These must select the cold
  fallback before a Q public write, not silently drop the modifier.

- [ ] **Step 5: Implement the pure Q Lakitu tick**

  Add:

  ```c
  bool saturn_camera_q_lakitu_tick(
      saturn_camera_q_state_t *state,
      int16_t yaw_speed,
      uint32_t movement_flags,
      uint32_t status_flags);
  ```

  Call the Q transition, update old/goal/current/render state in legacy order,
  apply the four speed approaches, compute polar focus state, set roll zero,
  perform the named floor correction, copy transition Mario position, and
  clamp pitch. Keep status-flag integer updates in `camera.c` and pass their
  resulting values explicitly.

- [ ] **Step 6: Add and activate the camera-side Lakitu seam**

  Implement the noinline private wrapper
  `saturn_camera_q_lakitu_seam_tick`. At the existing sole
  `update_lakitu(c)` call site:

  ```c
  #if defined(TARGET_SATURN) && SATURN_CAMERA_VARIANT == 2
      if (sSaturnCameraQTickActive) {
          saturn_camera_q_lakitu_seam_tick(c);
      } else {
          update_lakitu(c);
      }
  #else
      update_lakitu(c);
  #endif
  ```

  On a Q failure before publish, invoke the cold fallback once from the
  pre-tick public snapshot. Never run the legacy Lakitu update on partially
  published Q state.

- [ ] **Step 7: Update writer ownership and run focused tests**

  Reclassify the converted transition/Lakitu writes as Q-owned or public
  mirror. Keep floor/wall calls as float-API imports. Run:

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py LakituTransitionTests"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py LakituSmoothingTests"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  ```

- [ ] **Step 8: Inspect the target Q closure**

  Build the exact route-1 variant-2 ELF without `verify` because measured v3
  does not exist yet:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $task10QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task10QBuildCommand
  $task10QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $qElf = (Resolve-Path "$task10QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $qObjectManifest = (Resolve-Path "$task10QOutput/obj/camera-q-objects.tsv").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $qElf --phase lakitu --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-shadow-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-lakitu-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/generate_camera_q_audit_contract.py --inspection-only --elf $qElf --q-object-manifest $qObjectManifest --objdump D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe --readelf D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe --v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --root _saturn_camera_q_lakitu_seam_tick --root _saturn_camera_q_next_lakitu_state --root _saturn_camera_q_lakitu_tick --root _saturn_camera_q_publish_bridge --stop _saturn_camera_bridge_find_floor:floor --stop _saturn_camera_bridge_find_wall_collision:wall-collision --stop _saturn_camera_cold_float_fallback_tick:cold-fallback --output build/saturn/sourceboot/task3-camera-q-lakitu-inspection.json
  ```

  Use that inspection output to inspect
  `saturn_camera_q_lakitu_seam_tick`,
  `saturn_camera_q_next_lakitu_state`, and
  `saturn_camera_q_lakitu_tick`. Require no soft-float, libm, or 64-div
  edges before stopped floor/wall bridges. The named memory report must record
  the exact incremental delta from Task 9's shadow image and retain at least
  `0x1B00` total margin.

- [ ] **Step 9: Commit the Lakitu slice**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q.h src/port/saturn/runtime/saturn_camera_q.c src/game/camera.c tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/camera_q_writer_contract_v1.json docs/saturn/evidence/reports/task3-camera-memory-lakitu-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: convert Saturn Lakitu Q smoothing"
  ```

---

### Task 11: Implement Mario Dispatch and the Pure Default-Goal Core

**Files:**

- Modify: `src/port/saturn/runtime/saturn_camera_q.h`
- Modify: `src/port/saturn/runtime/saturn_camera_q.c`
- Modify: `src/game/camera.c`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Modify: `tools/saturn/camera_q_writer_contract_v1.json`
- Create after target link:
  `docs/saturn/evidence/reports/task3-camera-memory-default-core-2026-07-29.json`

**Interfaces:**

- Consumes: Task 10's Lakitu seam, Task 9's lifecycle/publish boundary, and
  Task 7's selected-Q kernel.
- Produces:
  `saturn_camera_q_default_inputs_t`,
  `saturn_camera_q_default_outputs_t`,
  `saturn_camera_q_default_phase_t`,
  `saturn_camera_q_default_status_t`,
  `saturn_camera_q_default_request_t`,
  `saturn_camera_q_default_result_t`, and the bridge-free phase transition
  `saturn_camera_q_default_tick`.

- [ ] **Step 1: Write failing dispatch and prelude tests**

  Prove these exact cases:

  - variant 1 always calls unchanged float functions;
  - variant 2 plus `CAM_MODE_MARIO_ACTIVE` plus default switch arm selects the
    Q Mario dispatch even while `Camera.mode == CAMERA_MODE_RADIAL`;
  - the one-tick R trigger changes the selection at route tick 121;
  - Mario dispatch sets Q zoom to exactly 350 world units;
  - Lakitu CLOSE/FREE_ROAM dispatch sets Q zoom to 800 world units;
  - ordinary radial dispatch remains float and is not counted as a post-seed
    fallback before the first Q seed;
  - unsupported post-seed dispatch invalidates and increments fallback once.

  Keep `update_mario_camera` identified as a transition-table helper; it is not
  the Mario-mode dispatch.

- [ ] **Step 2: Implement guarded dispatch and integer-owned prelude**

  Call `saturn_camera_q_prepare_tick` after `sYawSpeed = 0x400`. In
  `mode_mario_camera` and `mode_lakitu_camera`, set zoom through
  `saturn_camera_q_set_zoom_world` only when the Q tick is active; otherwise
  retain the exact float assignment. Keep FOV selection and integer flags in
  their legacy order.

  Stage-0 eligibility must reject active cutscene, initialization/warp, fixed
  camera modifiers, player-2 offsets, unsupported shake state, unclassified
  post-mode writers, and operands outside the committed range before any Q
  public write.

  Pack the mutable integer/control perimeter explicitly:

  ```c
  typedef struct saturn_camera_q_default_inputs {
      saturn_camera_q_vec3_t mario_position;
      saturn_camera_q_t mario_forward_velocity;
      saturn_camera_q_t controller_stick_x;
      saturn_camera_q_t controller_stick_y;
      saturn_camera_q_t current_floor_height;
      saturn_camera_q_t used_object_y;
      uintptr_t current_floor_token;
      uint32_t mario_action;
      uint32_t camera_movement_flags;
      uint32_t camera_status_flags;
      uint32_t camera_selection_flags;
      int16_t mario_face_yaw;
      int16_t c_side_button_yaw;
      int16_t avoid_yaw_velocity;
      int16_t camera_yaw_after_door;
      int16_t current_floor_type;
      int16_t dialog_id;
      uint16_t level_number;
      uint16_t area_index;
      uint8_t selected_camera_angle;
      bool used_object_valid;
  } saturn_camera_q_default_inputs_t;

  typedef struct saturn_camera_q_default_outputs {
      uint32_t camera_movement_flags;
      uint32_t camera_status_flags;
      int16_t c_side_button_yaw;
      int16_t avoid_yaw_velocity;
      int16_t free_roam_wall_yaw;
      int16_t next_yaw;
  } saturn_camera_q_default_outputs_t;
  ```

- [ ] **Step 3: Write failing default-goal core differential tests**

  Split the captured/default corpus into:

  1. focus/position prelude and Mario-relative deltas;
  2. polar extraction/reconstruction and yaw ownership;
  3. pan, zoom, yaw-speed, Lakitu distance/pitch, mode offset, and area yaw;
  4. lag/zoom caps and approach operations.

  Compare every named output and intermediate range category to the in-tree
  float path under the derived bound. Mutate one vector operand order, one
  angle sign, one zoom constant, and one approach coefficient. Each mutation
  must fail on a captured case.

- [ ] **Step 4: Implement the pure default-goal state transition**

  Add:

  ```c
  typedef enum saturn_camera_q_default_phase {
      SATURN_CAMERA_Q_DEFAULT_PRELUDE = 0,
      SATURN_CAMERA_Q_DEFAULT_FIND_FLOOR,
      SATURN_CAMERA_Q_DEFAULT_FIND_CEIL,
      SATURN_CAMERA_Q_DEFAULT_FIND_WALL_COLLISION,
      SATURN_CAMERA_Q_DEFAULT_ROTATE_AROUND_WALLS,
      SATURN_CAMERA_Q_DEFAULT_COLLIDE_WITH_WALLS,
      SATURN_CAMERA_Q_DEFAULT_RANGE_BEHIND_SURFACE,
      SATURN_CAMERA_Q_DEFAULT_FIND_WATER_LEVEL,
      SATURN_CAMERA_Q_DEFAULT_FIND_POISON_GAS_LEVEL,
      SATURN_CAMERA_Q_DEFAULT_COMPLETE,
      SATURN_CAMERA_Q_DEFAULT_FAULT,
  } saturn_camera_q_default_phase_t;

  typedef struct saturn_camera_q_height_request {
      saturn_camera_q_vec3_t position;
      saturn_camera_q_t y_offset;
  } saturn_camera_q_height_request_t;

  typedef struct saturn_camera_q_wall_request {
      saturn_camera_q_vec3_t position;
      saturn_camera_q_vec3_t secondary_position;
      saturn_camera_q_t offset_y;
      saturn_camera_q_t radius;
      int16_t yaw_range;
  } saturn_camera_q_wall_request_t;

  typedef struct saturn_camera_q_range_request {
      saturn_camera_q_vec3_t from;
      saturn_camera_q_vec3_t to;
      uintptr_t surface_token;
      int16_t range;
      int16_t surface_type;
  } saturn_camera_q_range_request_t;

  typedef struct saturn_camera_q_level_request {
      saturn_camera_q_t x;
      saturn_camera_q_t z;
  } saturn_camera_q_level_request_t;

  typedef struct saturn_camera_q_default_request {
      saturn_camera_q_default_phase_t phase;
      union {
          saturn_camera_q_height_request_t height;
          saturn_camera_q_wall_request_t wall;
          saturn_camera_q_range_request_t range;
          saturn_camera_q_level_request_t level;
      } value;
  } saturn_camera_q_default_request_t;

  typedef struct saturn_camera_q_default_result {
      saturn_camera_q_default_phase_t phase;
      bool valid;
      bool predicate;
      saturn_camera_q_vec3_t position;
      saturn_camera_q_vec3_t secondary_position;
      saturn_camera_q_t scalar;
      uintptr_t surface_token;
      uint32_t status_flags;
      int32_t integer;
      int16_t angle;
  } saturn_camera_q_default_result_t;

  typedef enum saturn_camera_q_default_status {
      SATURN_CAMERA_Q_DEFAULT_STATUS_REQUEST = 0,
      SATURN_CAMERA_Q_DEFAULT_STATUS_COMPLETE,
      SATURN_CAMERA_Q_DEFAULT_STATUS_FAULT,
  } saturn_camera_q_default_status_t;

  saturn_camera_q_default_status_t saturn_camera_q_default_tick(
      saturn_camera_q_state_t *state,
      const saturn_camera_q_default_inputs_t *inputs,
      const saturn_camera_q_default_result_t *result,
      saturn_camera_q_default_request_t *request,
      saturn_camera_q_default_outputs_t *outputs);
  ```

  These exact layouts are part of Task 11, because the pure transition writes
  requests and consumes results even though `camera.c` does not execute those
  bridges until Task 12. The first call requires nonnull `inputs`, a null
  `result`, and
  `state->default_phase == SATURN_CAMERA_Q_DEFAULT_PRELUDE`. A resume call
  requires null `inputs` and a nonnull result whose phase exactly matches the
  outstanding request. Copy every immutable input needed after the first
  request into named private state fields during the prelude; never retain the
  caller's pointer. `REQUEST` writes only `request`; `COMPLETE` writes only
  `outputs`; `FAULT` writes neither and leaves the state invalid.

  Port the source order from `update_default_camera` and
  `pan_ahead_of_player` into named selected-Q operations. Keep all persistent
  pan/zoom/yaw/distance/pitch values in the shadow. Use the integer trig/polar,
  distance, and approach APIs; never round-trip persistent state through
  public floats. Stop at each environmental dependency by returning a typed
  phase request; Task 12 resumes the transition with a typed result.

- [ ] **Step 5: Commit and review dispatch plus bridge-free goal math**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py DefaultDispatchTests"
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py DefaultGoalCoreTests"
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $task11QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task11QBuildCommand
  $task11QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $task11QElf = (Resolve-Path "$task11QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $task11QElf --phase default-core --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-lakitu-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-default-core-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q.h src/port/saturn/runtime/saturn_camera_q.c src/game/camera.c tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/camera_q_writer_contract_v1.json docs/saturn/evidence/reports/task3-camera-memory-default-core-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: add Saturn default camera Q core"
  ```

  Run the writer closure before the build. The named map must chain from the
  just-committed Lakitu phase, record the post-Task-11 ELF, and retain at least
  `0x1B00`; do not reuse Task 10's ELF. Review the Q intermediate order
  against the float source before adding any environmental bridge.

---

### Task 12: Implement the Typed Eight-Bridge Phase Machine

**Files:**

- Modify: `src/port/saturn/runtime/saturn_camera_q.h`
- Modify: `src/port/saturn/runtime/saturn_camera_q.c`
- Modify: `src/game/camera.c`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Modify: `tools/saturn/camera_q_writer_contract_v1.json`
- Create after target link:
  `docs/saturn/evidence/reports/task3-camera-memory-bridges-2026-07-29.json`

**Interfaces:**

- Consumes: Task 11's bridge-free default transition and typed phase
  protocol, plus Task 10's
  `saturn_camera_bridge_find_floor` and
  `saturn_camera_bridge_find_wall_collision`.
- Produces:
  `saturn_camera_q_default_begin`,
  `saturn_camera_q_default_resume`, and six additional named direct bridges:
  `saturn_camera_bridge_find_ceil`,
  `saturn_camera_bridge_rotate_around_walls`,
  `saturn_camera_bridge_collide_with_walls`,
  `saturn_camera_bridge_is_range_behind_surface`,
  `saturn_camera_bridge_find_water_level`, and
  `saturn_camera_bridge_find_poison_gas_level`.

- [ ] **Step 1: Write failing tests for every default environmental bridge**

  Exercise all eight noinline direct bridges in source order. Reuse the floor
  and wall-collision bridges from Task 10; add the other six in `camera.c`:

  ```text
  saturn_camera_bridge_find_floor
  saturn_camera_bridge_find_ceil
  saturn_camera_bridge_find_wall_collision
  saturn_camera_bridge_rotate_around_walls
  saturn_camera_bridge_collide_with_walls
  saturn_camera_bridge_is_range_behind_surface
  saturn_camera_bridge_find_water_level
  saturn_camera_bridge_find_poison_gas_level
  ```

  Consume the exact production phase/request/result types declared in Task 11;
  do not redeclare them and do not use callbacks. Add these six bridge
  interfaces alongside Task 10's floor and wall-collision interfaces:

  ```c
  bool saturn_camera_bridge_find_ceil(
      const saturn_camera_q_vec3_t *query,
      saturn_camera_q_t y_offset,
      saturn_camera_q_t *height,
      uintptr_t *surface_token,
      bool *found);
  bool saturn_camera_bridge_rotate_around_walls(
      struct Camera *camera,
      const saturn_camera_q_vec3_t *camera_position,
      const saturn_camera_q_vec3_t *mario_position,
      int16_t yaw_range,
      int16_t *avoid_yaw,
      int32_t *avoid_status,
      uint32_t *status_flags);
  bool saturn_camera_bridge_collide_with_walls(
      saturn_camera_q_vec3_t *position,
      saturn_camera_q_t offset_y,
      saturn_camera_q_t radius,
      int32_t *collision_count);
  bool saturn_camera_bridge_is_range_behind_surface(
      const saturn_camera_q_vec3_t *from,
      const saturn_camera_q_vec3_t *to,
      uintptr_t surface_token,
      int16_t range,
      int16_t surface_type,
      bool *behind);
  bool saturn_camera_bridge_find_water_level(
      saturn_camera_q_t x,
      saturn_camera_q_t z,
      saturn_camera_q_t *height,
      bool *present);
  bool saturn_camera_bridge_find_poison_gas_level(
      saturn_camera_q_t x,
      saturn_camera_q_t z,
      saturn_camera_q_t *height,
      bool *present);

  saturn_camera_q_default_status_t saturn_camera_q_default_begin(
      saturn_camera_q_state_t *state,
      const saturn_camera_q_default_inputs_t *inputs,
      saturn_camera_q_default_request_t *request,
      saturn_camera_q_default_outputs_t *outputs);
  saturn_camera_q_default_status_t saturn_camera_q_default_resume(
      saturn_camera_q_state_t *state,
      const saturn_camera_q_default_result_t *result,
      saturn_camera_q_default_request_t *next_request,
      saturn_camera_q_default_outputs_t *outputs);
  ```

  `saturn_camera_q_default_begin` and `saturn_camera_q_default_resume` are thin
  wrappers only:

  ```c
  return saturn_camera_q_default_tick(state, inputs, NULL, request, outputs);
  return saturn_camera_q_default_tick(
      state, NULL, result, next_request, outputs);
  ```

  Their exact phase-to-bridge marshalling contract is:

  | Phase | Request fields consumed | Bridge semantic outputs copied into result |
  | --- | --- | --- |
  | `FIND_FLOOR` | `height.position`, `height.y_offset` | `predicate=found`, `scalar=height`, `surface_token` |
  | `FIND_CEIL` | `height.position`, `height.y_offset` | `predicate=found`, `scalar=height`, `surface_token` |
  | `FIND_WALL_COLLISION` | `wall.position`, `wall.offset_y`, `wall.radius` | corrected `position`, `predicate=collided` |
  | `ROTATE_AROUND_WALLS` | `wall.position`, `wall.secondary_position`, `wall.yaw_range`, plus the seam's current `struct Camera *` | `angle=avoid_yaw`, `integer=avoid_status`, `status_flags` |
  | `COLLIDE_WITH_WALLS` | `wall.position`, `wall.offset_y`, `wall.radius` | corrected `position`, `integer=collision_count` |
  | `RANGE_BEHIND_SURFACE` | `range.from`, `range.to`, `range.surface_token`, `range.range`, `range.surface_type` | `predicate=behind` |
  | `FIND_WATER_LEVEL` | `level.x`, `level.z` | `predicate=present`, `scalar=height` |
  | `FIND_POISON_GAS_LEVEL` | `level.x`, `level.z` | `predicate=present`, `scalar=height` |

  Every result copies the request phase and sets `valid` from the wrapper's
  transport/conversion return. A missing floor/ceiling or water/gas level is
  `valid=true`, `predicate=false`, with the exact legacy sentinel converted
  into `scalar` and a zero surface token where applicable. Initialize every
  result field not listed in its row to zero; tests poison those fields and
  prove the resume transition never consumes them. The rotate bridge validates
  that the exported Mario position matches the public mirror before it calls
  the unchanged float API; it never stores `struct Camera *` in Q state.

  Tests require one export and one import per bridge invocation, exact surface
  token preservation between floor/ceil and range tests, import of only the
  phase-relevant returned scalar, primary position, secondary position, or
  predicate, and no whole-shadow reseed. Require each result phase to match
  the outstanding request, complete/fault to issue no further request, and
  every source-order branch to emit the correct next phase.
  Mutate each bridge to import an unrelated public component, resume with a
  stale phase, and swap two bridge phases; each mutation must fail.

- [ ] **Step 2: Implement the environmental bridge stages**

  Split the pure default operation around bridge calls so `camera.c` can:

  1. request Q query operands;
  2. export via raw-bit conversion;
  3. call the unchanged float API;
  4. import checked returned values;
  5. resume the Q transition.

  Preserve the exact call order and sentinel handling for
  `FLOOR_LOWER_LIMIT`, `CELL_HEIGHT_LIMIT`, water/gas absence, wall results,
  and `is_range_behind_surface`. Every bridge failure selects cold fallback
  before publish and increments the appropriate visible diagnostic.

- [ ] **Step 3: Commit and review the bridge phase machine**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py DefaultBridgePhaseTests"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $task12QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task12QBuildCommand
  $task12QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $task12QElf = (Resolve-Path "$task12QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $task12QElf --phase bridges --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-default-core-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-bridges-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q.h src/port/saturn/runtime/saturn_camera_q.c src/game/camera.c tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/camera_q_writer_contract_v1.json docs/saturn/evidence/reports/task3-camera-memory-bridges-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: add Saturn camera bridge phase machine"
  ```

  The named map must chain from Task 11's post-default-core ELF, record the
  post-Task-12 ELF, and retain at least `0x1B00`; do not reuse either prior
  build. Review all eight direct bridge edges and the request/result conversion
  before integrating dispatch.

---

### Task 13: Integrate Default Dispatch, Publish Once, and Prove Feasibility

**Files:**

- Modify: `src/port/saturn/runtime/saturn_camera_q.h`
- Modify: `src/port/saturn/runtime/saturn_camera_q.c`
- Modify: `src/game/camera.c`
- Modify: `tools/saturn/camera_q_diff_fixture.c`
- Modify: `tools/saturn/test_camera_q.py`
- Modify: `tools/saturn/camera_q_writer_contract_v1.json`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Create after target link:
  `docs/saturn/evidence/reports/task3-camera-memory-complete-2026-07-29.json`

**Interfaces:**

- Consumes: Task 12's typed phase machine, Task 10's Lakitu seam, and Task 9's
  one-publish lifecycle.
- Produces: `saturn_camera_q_default_seam_tick` and the complete linked
  variant-2 camera island ready for measured v3 generation.

- [ ] **Step 1: Implement post-adjustment and one publish**

  Apply the bounded path's final floor correction and pitch clamp through Q or
  the named bridge. Prove all route modifiers are inactive before skipping
  shakes/player-2 behavior. Call `saturn_camera_q_publish_bridge` once after
  goal, transition, smoothing, and post-adjustment succeed, before
  `gLakituState.lastFrameAction` is written.

  Add a source-order test that rejects a publish inside default goal, a second
  publish in Lakitu, or any normal Q public float write outside the bridge.

- [ ] **Step 2: Activate the default seam without changing legacy functions**

  In `mode_default_camera`:

  ```c
  set_fov_function(CAM_FOV_DEFAULT);
  #if defined(TARGET_SATURN) && SATURN_CAMERA_VARIANT == 2
      if (sSaturnCameraQTickActive) {
          c->nextYaw = saturn_camera_q_default_seam_tick(c);
      } else {
          c->nextYaw = update_default_camera(c);
          pan_ahead_of_player(c);
      }
  #else
      c->nextYaw = update_default_camera(c);
      pan_ahead_of_player(c);
  #endif
  ```

  The Q wrapper is noinline and contains only explicit integer/Q calls before
  stopped bridges.

- [ ] **Step 3: Run the full host differential and ownership suite**

  ```powershell
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  ```

- [ ] **Step 4: Run an early link/audit and memory feasibility gate**

  Rebuild the exact route-1 variant-2 ELF without `verify`, then generate an
  uncommitted candidate v3 from that just-resolved artifact:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $task13QBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=2 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe"
  C:/msys64/usr/bin/bash.exe -lc $task13QBuildCommand
  $task13QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $qElf = (Resolve-Path "$task13QOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $qObjectManifest = (Resolve-Path "$task13QOutput/obj/camera-q-objects.tsv").Path
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $qElf --phase complete-island --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-bridges-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-complete-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/generate_camera_q_audit_contract.py --elf $qElf --q-object-manifest $qObjectManifest --objdump D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe --readelf D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe --v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --root _saturn_camera_q_default_seam_tick --root _saturn_camera_q_lakitu_seam_tick --root _saturn_camera_q_default_tick --root _saturn_camera_q_lakitu_tick --root _saturn_camera_q_next_lakitu_state --root _saturn_camera_q_publish_bridge --stop _saturn_camera_bridge_find_floor:floor --stop _saturn_camera_bridge_find_ceil:ceil --stop _saturn_camera_bridge_find_wall_collision:wall-collision --stop _saturn_camera_bridge_rotate_around_walls:rotate-walls --stop _saturn_camera_bridge_collide_with_walls:collide-walls --stop _saturn_camera_bridge_is_range_behind_surface:range-surface --stop _saturn_camera_bridge_find_water_level:water-level --stop _saturn_camera_bridge_find_poison_gas_level:poison-gas --stop _saturn_camera_cold_float_fallback_tick:cold-fallback --output build/saturn/sourceboot/task3-camera-q-audit-candidate-v3.txt
  ```

  Do not stage that build-tree candidate. Require:

  - every planned Q root and caller is present;
  - helper edges exist only at the eight environmental stops and the cold
    fallback stop;
  - `_atan2_lookup` and `_atan2s` are absent from the Q closure;
  - the measured global helper total is lower than v2;
  - the cold fallback remains linked.

  If retaining the cold fallback prevents a lower global total, stop here and
  amend/re-review the design. Do not defer that contradiction to final
  evidence or exclude the helper-bearing body without an approved contract
  change. The named memory report must chain from Task 12's post-bridge ELF,
  record the just-built complete-island ELF, and retain at least `0x1B00`.
  A target-affecting change after this point invalidates the generated v3
  contract and every later capture.

- [ ] **Step 5: Commit the complete converted island**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/runtime/saturn_camera_q.h src/port/saturn/runtime/saturn_camera_q.c src/game/camera.c tools/saturn/camera_q_diff_fixture.c tools/saturn/test_camera_q.py tools/saturn/camera_q_writer_contract_v1.json src/port/saturn/sourceboot/Makefile docs/saturn/evidence/reports/task3-camera-memory-complete-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "feat: convert Saturn default camera Q seam"
  ```

---

### Task 14: Pin Audit V3, Non-Vacuity, and Two Runs per Camera Role

**Files:**

- Create: `tools/saturn/fixtures/bob_camera_q_acceptance_v1.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-bridge-calibration-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-baseline-scc1-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-baseline-scc1-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-scc1-ab-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-scc1-ab-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-sbr4-ab-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-sbr4-ab-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-memory-final-baseline-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-memory-final-q-2026-07-29.json`
- Create: `tools/saturn/sh2_native_math_sim_audit_contract_v3.txt`
- Modify: `tools/saturn/camera_idle_contract.py`
- Modify: `tools/saturn/test_camera_idle_contract.py`
- Modify: `tools/saturn/capture_camera_idle.py`
- Modify: `tools/saturn/test_capture_camera_idle.py`
- Modify: `tools/saturn/verify_camera_idle_capture.py`
- Modify: `tools/saturn/test_verify_camera_idle_capture.py`
- Modify: `tools/saturn/camera_q_writer_contract_v1.json`
- Modify: `src/port/saturn/sourceboot/Makefile`

**Interfaces:**

- Consumes: Task 13's complete route-1 Q target, fixed idle/format/memory
  fixtures, writer contract, Task 8's v3 generator, and SCC1 tools.
- Produces: the measured v3 contract committed before capture, exact
  bridge-count/first-seed acceptance fixture, two raw-derived SCC1/SBR4/SQT1
  captures per role, two same-role determinism results, two cross-role
  comparison reports, and two strict simulation-performance pair results.

- [ ] **Step 1: Add every camera gate to sourceboot `make verify`**

  Route-1 verification must run:

  ```text
  test_camera_acceptance_route.py
  test_camera_idle_contract.py
  test_capture_camera_idle.py
  test_verify_camera_idle_capture.py
  test_compare_camera_route_reports.py
  test_camera_range_contract.py
  test_camera_q_candidates.py
  test_freeze_camera_q_format.py
  test_camera_q.py
  test_verify_camera_q_writers.py
  test_verify_sourceboot_memory_map.py
  test_compare_sh2_native_math_audit_reports.py
  test_generate_camera_q_audit_contract.py
  test_verify_sh2_native_math.py
  test_verify_camera_q_mutation.py
  verify_camera_idle_layout.py
  ```

  Keep the existing route/native-math/coherency verifiers. Wire the Makefile
  so variant 1 still uses the corrected, re-pinned v2, while route-1 variant
  2 requires the measured v3 contract and passes the frozen corrected v2 path separately through
  `--v2-contract` plus its generated
  `$(SH_BUILD_PATH)/camera-q-objects.tsv` through `--q-object-manifest`.
  Route 0 must not require SCC storage but still runs pure host tests. Do not
  invoke route-1 variant-2 `make verify` until Step 3 has generated and
  selected v3 from its completed ELF.

- [ ] **Step 2: Build baseline with v2 and link Q without verification**

  Read the committed idle tick and staging value:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  ```

  Build variant 1 with the full v2-backed `verify` target. Build variant 2
  through the ordinary default build target only so a nonexistent v3 cannot
  create a circular gate:

  ```powershell
  $cameraBaselineBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=1 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 SATURN_CAMERA_IDLE_START_TICK=$cameraIdleTick SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify"
  C:/msys64/usr/bin/bash.exe -lc $cameraBaselineBuildCommand
  $cameraQBuildCommand = $cameraBaselineBuildCommand.Replace(
      "SATURN_CAMERA_VARIANT=1",
      "SATURN_CAMERA_VARIANT=2"
  ).Replace(" verify", "")
  C:/msys64/usr/bin/bash.exe -lc $cameraQBuildCommand
  ```

  Resolve the role-tagged output trees and exact Q ELF:

  ```powershell
  $baselineOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv1-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $qOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $baselineElf = (Resolve-Path "$baselineOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $qElf = (Resolve-Path "$qOutput/obj/sm64-saturn-sourceboot-e2.elf").Path
  $qObjectManifest = (Resolve-Path "$qOutput/obj/camera-q-objects.tsv").Path
  $baselineCue = (Resolve-Path "$baselineOutput/sm64-saturn-sourceboot-e2.cue").Path
  $qCue = (Resolve-Path "$qOutput/sm64-saturn-sourceboot-e2.cue").Path
  $qElfHashBeforeAudit = (Get-FileHash $qElf -Algorithm SHA256).Hash.ToLowerInvariant()
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $baselineElf --phase final-baseline --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-fixed-baseline-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-final-baseline-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sourceboot_memory_map.py check-phase --elf $qElf --phase final-q --stage-sectors $cameraStageSectors --previous-report docs/saturn/evidence/reports/task3-camera-memory-complete-2026-07-29.json --required-final-margin 0x1B00 --output docs/saturn/evidence/reports/task3-camera-memory-final-q-2026-07-29.json
  ```

  Both named final reports must retain at least `0x1B00` HWRAM and bind the
  exact role ELF. Do not launch Ymir yet.

- [ ] **Step 3: Generate, review, select, and commit measured v3**

  Generate from the exact `$qElf`:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/generate_camera_q_audit_contract.py --elf $qElf --q-object-manifest $qObjectManifest --objdump D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe --readelf D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe --v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --root _saturn_camera_q_default_seam_tick --root _saturn_camera_q_lakitu_seam_tick --root _saturn_camera_q_default_tick --root _saturn_camera_q_lakitu_tick --root _saturn_camera_q_next_lakitu_state --root _saturn_camera_q_publish_bridge --stop _saturn_camera_bridge_find_floor:floor --stop _saturn_camera_bridge_find_ceil:ceil --stop _saturn_camera_bridge_find_wall_collision:wall-collision --stop _saturn_camera_bridge_rotate_around_walls:rotate-walls --stop _saturn_camera_bridge_collide_with_walls:collide-walls --stop _saturn_camera_bridge_is_range_behind_surface:range-surface --stop _saturn_camera_bridge_find_water_level:water-level --stop _saturn_camera_bridge_find_poison_gas_level:poison-gas --stop _saturn_camera_cold_float_fallback_tick:cold-fallback --output tools/saturn/sh2_native_math_sim_audit_contract_v3.txt
  ```

  The generator itself launches objdump with `C:\msys64\usr\bin` prepended.
  Review every root, generated caller, direct edge, stop, per-stop helper
  count, canonicalization version, v2 digest, Q ELF digest, all four
  raw-object/canonical-disassembly digest pairs, exact global total, and
  negative delta. Reject a missing/extra caller or object, an object digest
  mismatch, or any helper edge before a stop.

  Select v3 in the Makefile only for route-1 camera variant 2. Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_mutation.py
  $cameraQVerifyCommand = "$cameraQBuildCommand verify"
  C:/msys64/usr/bin/bash.exe -lc $cameraQVerifyCommand
  $qElfHashAfterAudit = (Get-FileHash $qElf -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($qElfHashAfterAudit -ne $qElfHashBeforeAudit) { throw "Q ELF changed while selecting v3; regenerate before capture" }
  ```

  If any target-affecting file changes or `make verify` changes the ELF,
  regenerate/re-review v3 and repeat the unchanged-hash check. Commit the
  stable contract and Makefile before any calibration or acceptance capture:

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add tools/saturn/sh2_native_math_sim_audit_contract_v3.txt src/port/saturn/sourceboot/Makefile
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: pin Saturn camera Q audit v3"
  ```

- [ ] **Step 4: Calibrate bridge counts once, then review and pin them**

  Run one non-acceptance Q calibration capture at the fixed idle pin. Decode
  raw SCC1 without applying expected bridge counts. Compare export/import
  totals to a static count derived from the exact Q bridge call sites over
  `[first_successful_seed_tick, 2599 + idle_start_tick]`, inclusive. Derive
  the first successful seed from the same-pause raw SQT1 record and require it
  to equal
  `first_mario_dispatch_tick(bob_default_camera_v1.json)`; do not hardcode
  2,600 or assume seed begins at tick 1. Investigate any mismatch; do not
  choose a fixture value merely because the target emitted it.

  Add a tested `capture_camera_idle.py --bridge-calibration` mode valid only
  with capture role `camera-q`. It bypasses only the not-yet-created expected
  bridge-count fixture; raw layout, route/role markers, neutral input,
  generation, zero fault/fallback/reseed counters, 600-tick stability, sibling
  ELF markers, SCC/SBR4 same-pause binding, and artifact hashes remain
  mandatory. Capture:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $qCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --capture-role camera-q --expected-idle-start-tick $cameraIdleTick --bridge-calibration --output docs/saturn/evidence/reports/task3-camera-bridge-calibration-2026-07-29.json
  ```

  Add `--audit-contract PATH`, `--idle-fixture PATH`, and
  `--idle-pin-verification PATH` to fixed capture mode and record each
  path/hash/size. Raw-decode the pinned fixture evidence again and require its
  digest in the pin-verification report plus its tick to match the capture
  argument before launching Ymir. Require v2 for baseline and v3 for Q by
  exact contract version plus build markers. Tests reject bridge calibration
  for baseline, discovery, range, or ordinary acceptance captures and prove
  that every non-count, fixture-hash, and pin-verification gate fails
  independently.

  Extend the writer contract with the reviewed per-dispatch bridge schedule.
  Pin `first_successful_seed_tick` from the manifest-derived helper only after
  raw SQT1 agrees; preserve the calibration report's exact 16 raw SQT1 bytes
  and digest.
  Generate `bob_camera_q_acceptance_v1.json` only after the static schedule and
  calibration agree:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_camera_idle_capture.py --calibration docs/saturn/evidence/reports/task3-camera-bridge-calibration-2026-07-29.json --writer-contract tools/saturn/camera_q_writer_contract_v1.json --route tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --emit-acceptance tools/saturn/fixtures/bob_camera_q_acceptance_v1.json
  ```

  The emitted fixture contains route ID 2, camera variant 2, atan2 variant 2,
  zoom bits `1135542272`, the idle-fixture digest, the manifest-derived first
  successful seed tick, and the exact reviewed export/import counts. Add a
  test that requires both counts to be nonzero and rejects any subsequent raw
  SQT1 or count deviation.

- [ ] **Step 5: Capture two fresh baseline SCC1 runs**

  Use:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $baselineCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --capture-role camera-baseline --expected-idle-start-tick $cameraIdleTick --output docs/saturn/evidence/reports/task3-camera-baseline-scc1-run1-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $baselineCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --capture-role camera-baseline --expected-idle-start-tick $cameraIdleTick --output docs/saturn/evidence/reports/task3-camera-baseline-scc1-run2-2026-07-29.json
  ```

  Run those two commands without rebuilding between them. Require
  byte-identical SCC1 windows, baseline generation zero, bridge counts zero,
  route ID 2, zoom 350, exact neutral input, exact 600-tick stability, an
  all-zero 16-byte SQT1 window, and valid artifact identities. Each baseline
  report records
  `artifacts.audit_contract.{path,sha256,size}` for the corrected, re-pinned
  v2.

- [ ] **Step 6: Capture two fresh Q SCC1 runs**

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $qCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --capture-role camera-q --expected-idle-start-tick $cameraIdleTick --output docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe" --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" --game $qCue --route-manifest tools/saturn/routes/bob_default_camera_v1.json --idle-fixture tools/saturn/fixtures/bob_default_camera_v1_idle.json --idle-pin-verification docs/saturn/evidence/reports/task3-camera-idle-pin-verification-2026-07-29.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --capture-role camera-q --expected-idle-start-tick $cameraIdleTick --output docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json
  ```

  Run those two commands without rebuilding between them. Require
  byte-identical SCC1 windows, nonzero generation, the
  pinned nonzero bridge counts, zero overflow/saturation/divide/reseed/fallback
  counts, route ID 2, zoom 350, exact 600-tick stability, and byte-identical
  valid SQT1 windows whose seed tick equals the pinned writer-contract value.
  Bind each report through
  `artifacts.audit_contract.{path,sha256,size}` for committed v3 and
  `artifacts.q_object_manifest.{path,sha256,size}` for
  `camera-q-objects.tsv`, alongside the existing ELF artifact record, so Task
  15 can resolve and revalidate the exact audited inputs.

- [ ] **Step 7: Compare every baseline/Q sample in two independent pairs**

  Run:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_camera_idle_capture.py --baseline-run1 docs/saturn/evidence/reports/task3-camera-baseline-scc1-run1-2026-07-29.json --baseline-run2 docs/saturn/evidence/reports/task3-camera-baseline-scc1-run2-2026-07-29.json --q-run1 docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json --q-run2 docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json --route tools/saturn/routes/bob_default_camera_v1.json --format tools/saturn/fixtures/bob_camera_q_format_v1.json --acceptance tools/saturn/fixtures/bob_camera_q_acceptance_v1.json --pair1-output docs/saturn/evidence/reports/task3-camera-scc1-ab-run1-2026-07-29.json --pair2-output docs/saturn/evidence/reports/task3-camera-scc1-ab-run2-2026-07-29.json
  .venv-saturn-tools/Scripts/python.exe tools/saturn/compare_camera_route_reports.py --route tools/saturn/routes/bob_default_camera_v1.json --baseline-run1 docs/saturn/evidence/reports/task3-camera-baseline-scc1-run1-2026-07-29.json --baseline-run2 docs/saturn/evidence/reports/task3-camera-baseline-scc1-run2-2026-07-29.json --q-run1 docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json --q-run2 docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json --require-sim-improvement --pair1-output docs/saturn/evidence/reports/task3-camera-sbr4-ab-run1-2026-07-29.json --pair2-output docs/saturn/evidence/reports/task3-camera-sbr4-ab-run2-2026-07-29.json
  ```

  Require distinct baseline/Q ELF and ISO hashes, correct raw variants, exact
  packed integer fields, all float fields within the derived bound, and every
  position/focus component below one world unit at all 600 ticks. Independently
  decode all four 160-byte SBR4 windows; require same-role equality for
  non-timing behavior/output fields and strict Q
  `sim_frt_ticks_accum` improvement in run1/run1 and run2/run2. Record all
  timing/provenance values even when same-role timing differs. Require the
  acceptance fixture's idle-fixture and idle-pin-verification digests to match
  the corresponding artifact records in all four captures before comparing
  any samples.

- [ ] **Step 8: Commit the reviewed non-vacuity fixture and SCC evidence**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add tools/saturn/fixtures/bob_camera_q_acceptance_v1.json tools/saturn/camera_idle_contract.py tools/saturn/test_camera_idle_contract.py tools/saturn/capture_camera_idle.py tools/saturn/test_capture_camera_idle.py tools/saturn/verify_camera_idle_capture.py tools/saturn/test_verify_camera_idle_capture.py tools/saturn/camera_q_writer_contract_v1.json src/port/saturn/sourceboot/Makefile docs/saturn/evidence/reports/task3-camera-memory-final-baseline-2026-07-29.json docs/saturn/evidence/reports/task3-camera-memory-final-q-2026-07-29.json docs/saturn/evidence/reports/task3-camera-bridge-calibration-2026-07-29.json docs/saturn/evidence/reports/task3-camera-baseline-scc1-run1-2026-07-29.json docs/saturn/evidence/reports/task3-camera-baseline-scc1-run2-2026-07-29.json docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json docs/saturn/evidence/reports/task3-camera-scc1-ab-run1-2026-07-29.json docs/saturn/evidence/reports/task3-camera-scc1-ab-run2-2026-07-29.json docs/saturn/evidence/reports/task3-camera-sbr4-ab-run1-2026-07-29.json docs/saturn/evidence/reports/task3-camera-sbr4-ab-run2-2026-07-29.json
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "test: bind Saturn camera Q target evidence"
  ```

---

### Task 15: Regress the Original Route and Close Task 3

**Files:**

- Create:
  `docs/saturn/evidence/reports/task3-camera-q-audit-v3-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route0-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-legacy-route1-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route0-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-corrected-route1-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-pre-repin-proposal-2026-07-29.json`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-parser-review-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-q-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-q-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run1-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run2-2026-07-29.json`
- Create:
  `docs/saturn/evidence/reports/task3-camera-q-seam-2026-07-29.md`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `tools/saturn/capture_camera_idle.py`
- Modify: `tools/saturn/test_capture_camera_idle.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Revalidate unchanged:
  `tools/saturn/test_compare_sh2_native_math_audit_reports.py`
- Revalidate unchanged:
  `tools/saturn/compare_sh2_native_math_audit_reports.py`
- Revalidate unchanged:
  `docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json`
- Modify: `docs/superpowers/plans/2026-07-29-sh2-native-math-purge.md`
- Modify: `docs/superpowers/specs/2026-07-29-saturn-camera-q-seam-design.md`
- Modify: `docs/saturn/HANDOFF_2026-07-29-native-math-sprint.md`
- Modify:
  `.superpowers/sdd/2026-07-29-sh2-native-math-purge/progress.md`
  (local ignored ledger; never stage it)

**Interfaces:**

- Consumes: Task 14's reviewed captures, stable v3 contract, and exact final Q
  ELF.
- Produces:
  the hash-revalidated mutation-proof audit report, four raw
  `bob-parity-v1` reports, two original-route A/B reports, the final Task 3
  report, and updated sprint control documents.

- [ ] **Step 1: Revalidate v3 against the exact captured Q artifact**

  Resolve the Q ELF from each of Task 14's two Q reports and require the same
  ELF hash, matching the hash embedded in the committed v3 contract. Resolve
  `artifacts.q_object_manifest.path` from both reports and require identical
  manifest hashes matching the committed v3 object records. Re-run the
  historical-source and current-v2 compatibility gate with the retained Task
  3 evidence ELFs:

  ```powershell
  $repoRoot = "D:/Code/RetroDev/sm64-saturn-port/sm64-port"
  $repinPath = "docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json"
  $repin = Get-Content $repinPath -Raw | ConvertFrom-Json
  $parserCommit = [string]$repin.review.parser_commit
  $evidenceWorktree = "$repoRoot/.worktrees/audit-parser-evidence-$($parserCommit.Substring(0, 7))"
  $route0Elf = (Resolve-Path (Join-Path $evidenceWorktree ([string]$repin.artifacts.route0_elf_relative_path))).Path
  $route1Elf = (Resolve-Path (Join-Path $evidenceWorktree ([string]$repin.artifacts.route1_elf_relative_path))).Path
  $auditObjdump = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe"
  $auditReadelf = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe"
  $auditAddr2line = "D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-addr2line.exe"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_sh2_native_math_audit_reports.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/compare_sh2_native_math_audit_reports.py verify-final --report $repinPath --current-verifier tools/saturn/verify_sh2_native_math.py --current-v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --baseline tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --route0-elf $route0Elf --route1-elf $route1Elf --objdump $auditObjdump --readelf $auditReadelf --addr2line $auditAddr2line
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_mutation.py
  ```

  Run `verify_sh2_native_math.py` against that exact Q ELF, object manifest,
  and committed v3:

  ```powershell
  $qRun1 = Get-Content docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json -Raw | ConvertFrom-Json
  $qRun2 = Get-Content docs/saturn/evidence/reports/task3-camera-q-scc1-run2-2026-07-29.json -Raw | ConvertFrom-Json
  if ($qRun1.artifacts.elf.sha256 -ne $qRun2.artifacts.elf.sha256) { throw "Q capture ELF hashes differ" }
  if ($qRun1.artifacts.q_object_manifest.sha256 -ne $qRun2.artifacts.q_object_manifest.sha256) { throw "Q capture object-manifest hashes differ" }
  if ($qRun1.artifacts.audit_contract.sha256 -ne $qRun2.artifacts.audit_contract.sha256) { throw "Q capture audit-contract hashes differ" }
  $qElf = (Resolve-Path $qRun1.artifacts.elf.path).Path
  $qObjectManifest = (Resolve-Path $qRun1.artifacts.q_object_manifest.path).Path
  $qAuditContract = (Resolve-Path tools/saturn/sh2_native_math_sim_audit_contract_v3.txt).Path
  if ((Get-FileHash $qElf -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qRun1.artifacts.elf.sha256) { throw "captured Q ELF changed" }
  if ((Get-FileHash $qObjectManifest -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qRun1.artifacts.q_object_manifest.sha256) { throw "captured Q object manifest changed" }
  if ((Get-FileHash $qAuditContract -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qRun1.artifacts.audit_contract.sha256) { throw "committed v3 differs from captured contract" }
  .venv-saturn-tools/Scripts/python.exe tools/saturn/verify_sh2_native_math.py $qElf tools/saturn/sh2_native_math_baseline_v1.txt --route-oracle tools/saturn/sh2_native_math_route_oracle_v1.txt --audit-route-oracle tools/saturn/sh2_native_math_sim_route_oracle_v1.txt --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --v2-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --q-object-manifest $qObjectManifest --objdump D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-objdump.exe --readelf D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-readelf.exe --addr2line D:/Code/RetroDev/sm64-saturn-port/work/yaul-install/bin/sh-elf-addr2line.exe --json-output docs/saturn/evidence/reports/task3-camera-q-audit-v3-2026-07-29.json
  ```

  Require the same lower exact global total, zero pre-stop Q-closure helper
  edges, exact stopped counts, forbidden atan2 callers absent, and unchanged
  per-Q-object digests. Do not regenerate or silently update the contract
  here.

- [ ] **Step 2: Rerun the immutable `bob-parity-v1` route twice per role**

  Build camera variants 1 and 2 with
  `SATURN_SOURCEBOOT_CAMERA_ROUTE=0`, all other Task 3 renderer flags fixed,
  and distinct output tags. Extend the tested camera capture tool with an
  `--sbr4-only` mode that resolves and reads only the raw SBR4 checkpoint when
  SCC is intentionally absent. Capture two SBR4 runs per role. Require the old
  manifest hash to remain
  `f62861516c708ef0bda82c0a32bf10706a93b0948160f42b50c1e6ec9ddd44ee`,
  raw-first SBR4 decoding, same-role determinism, zero faults, and the hardened
  output/reject contract.

  Build and resolve the two CUEs:

  ```powershell
  $cameraIdleTick = (Get-Content tools/saturn/fixtures/bob_default_camera_v1_idle.json -Raw | ConvertFrom-Json).idle_start_tick
  $cameraStageSectors = (Get-Content tools/saturn/fixtures/bob_camera_memory_v1.json -Raw | ConvertFrom-Json).stage_sectors
  $route1QCapture = Get-Content docs/saturn/evidence/reports/task3-camera-q-scc1-run1-2026-07-29.json -Raw | ConvertFrom-Json
  $route1QOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute1-atan2v2-camv2-idle$cameraIdleTick-disc0-range0-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $route1QObjectManifest = (Resolve-Path "$route1QOutput/obj/camera-q-objects.tsv").Path.Replace('\', '/')
  $route1QObjectManifestSha256 = (Get-FileHash $route1QObjectManifest -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($route1QObjectManifestSha256 -ne $route1QCapture.artifacts.q_object_manifest.sha256) { throw "route-1 Q reference manifest no longer matches captured evidence" }
  $parityBaselineBuildCommand = "cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge && source /d/Code/RetroDev/sm64-saturn-port/sm64-port/.yaul.env && cd src/port/saturn/sourceboot && make -j2 SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 SATURN_SOURCE_CART_STAGE_SECTORS=$cameraStageSectors SATURN_ATAN2_VARIANT=2 SATURN_CAMERA_VARIANT=1 SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=0 SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_RENDERER_PIPELINE=2 HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify"
  C:/msys64/usr/bin/bash.exe -lc $parityBaselineBuildCommand
  $parityQBuildCommand = $parityBaselineBuildCommand.Replace(
      "SATURN_CAMERA_VARIANT=1",
      "SATURN_CAMERA_VARIANT=2"
  ).Replace(
      " HOST_CC=C:/msys64/mingw64/bin/gcc.exe verify",
      " HOST_CC=C:/msys64/mingw64/bin/gcc.exe SATURN_CAMERA_Q_REFERENCE_MANIFEST=$route1QObjectManifest SATURN_CAMERA_Q_REFERENCE_MANIFEST_SHA256=$route1QObjectManifestSha256 verify"
  )
  C:/msys64/usr/bin/bash.exe -lc $parityQBuildCommand
  $parityBaselineOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute0-atan2v2-camv1-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $parityQOutput = "build/saturn/sourceboot/e2-bob-demo-replay-camroute0-atan2v2-camv2-stage$cameraStageSectors-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2"
  $parityBaselineCue = (Resolve-Path "$parityBaselineOutput/sm64-saturn-sourceboot-e2.cue").Path
  $parityQCue = (Resolve-Path "$parityQOutput/sm64-saturn-sourceboot-e2.cue").Path
  ```

  Invoke `capture_camera_idle.py --sbr4-only` twice for each CUE with the
  immutable route manifest and exact role/audit binding. Do not rebuild
  between the two runs of one role:

  ```powershell
  $ymirExe = "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe"
  $saturnIpl = "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin"
  $parityBaselineRun1 = "docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run1-2026-07-29.json"
  $parityBaselineRun2 = "docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run2-2026-07-29.json"
  $parityQRun1 = "docs/saturn/evidence/reports/task3-bob-parity-camera-q-run1-2026-07-29.json"
  $parityQRun2 = "docs/saturn/evidence/reports/task3-bob-parity-camera-q-run2-2026-07-29.json"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir $ymirExe --ipl $saturnIpl --game $parityBaselineCue --route-manifest tools/saturn/routes/bob_parity_v1.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --capture-role camera-baseline --sbr4-only --output $parityBaselineRun1
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir $ymirExe --ipl $saturnIpl --game $parityBaselineCue --route-manifest tools/saturn/routes/bob_parity_v1.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v2.txt --capture-role camera-baseline --sbr4-only --output $parityBaselineRun2
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir $ymirExe --ipl $saturnIpl --game $parityQCue --route-manifest tools/saturn/routes/bob_parity_v1.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --capture-role camera-q --q-object-manifest "$parityQOutput/obj/camera-q-objects.tsv" --reference-q-object-manifest $route1QObjectManifest --reference-q-object-manifest-sha256 $route1QObjectManifestSha256 --sbr4-only --output $parityQRun1
  .venv-saturn-tools/Scripts/python.exe tools/saturn/capture_camera_idle.py --ymir $ymirExe --ipl $saturnIpl --game $parityQCue --route-manifest tools/saturn/routes/bob_parity_v1.json --audit-contract tools/saturn/sh2_native_math_sim_audit_contract_v3.txt --capture-role camera-q --q-object-manifest "$parityQOutput/obj/camera-q-objects.tsv" --reference-q-object-manifest $route1QObjectManifest --reference-q-object-manifest-sha256 $route1QObjectManifestSha256 --sbr4-only --output $parityQRun2
  ```

  `--sbr4-only` rejects SCC/discovery/range/calibration options. For camera-Q
  it requires both object-manifest paths, records their hashes, and requires
  the reference hash to equal Task 14's captured route-1 manifest before
  launch.

  Compare all four reports with the camera-role comparator:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/compare_camera_route_reports.py --route tools/saturn/routes/bob_parity_v1.json --baseline-run1 $parityBaselineRun1 --baseline-run2 $parityBaselineRun2 --q-run1 $parityQRun1 --q-run2 $parityQRun2 --pair1-output docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run1-2026-07-29.json --pair2-output docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run2-2026-07-29.json
  ```

  Require final Mario positional divergence below one world unit and exact
  timer/action/face-angle/camera/output gates, same-role non-timing
  determinism, correct atan2=2/camera marker roles, and unchanged manifest
  digest. Do not request a simulation-improvement gate for this radial-only
  regression. The Q shadow must remain unseeded.

  For route-0 variant 2, `make verify` cannot apply the route-1
  artifact-hash-bound v3 contract to a different ELF. Add a tested
  `verify-camera-q-reference` prerequisite that requires the route-0 Q
  closure object digests and disassembly to match the corresponding audited
  route-1 Q objects by invoking `verify_sh2_native_math.py
  --object-reference-only` with the route-0 generated manifest, the committed
  v3 contract, `SATURN_CAMERA_Q_REFERENCE_MANIFEST`, and its captured
  `SATURN_CAMERA_Q_REFERENCE_MANIFEST_SHA256`; then run every
  non-artifact-specific verifier. Reject a missing reference manifest,
  self-reference, any logical-name/path/digest mismatch, or a route-1 manifest
  whose digest differs from Task 14's captured manifest. Do not skip or reuse
  v2 for this build.

  The Make prerequisite invokes exactly:

  ```make
  "$(SOURCEBOOT_PYTHON)" "$(ROOT)/tools/saturn/verify_sh2_native_math.py" \
    --object-reference-only \
    --audit-contract "$(ROOT)/tools/saturn/sh2_native_math_sim_audit_contract_v3.txt" \
    --v2-contract "$(SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_V2_CONTRACT)" \
    --q-object-manifest "$(SH_BUILD_PATH)/camera-q-objects.tsv" \
    --reference-q-object-manifest "$(SATURN_CAMERA_Q_REFERENCE_MANIFEST)" \
    --reference-q-object-manifest-sha256 "$(SATURN_CAMERA_Q_REFERENCE_MANIFEST_SHA256)" \
    --objdump "$(SOURCEBOOT_SH_OBJDUMP)"
  ```

  Makefile tests require this exact argument set and prove that each missing
  object-mode argument, either legacy positional, a legacy route/addr2line
  option, or an extra/renamed object fails.

- [ ] **Step 3: Run the complete host and target verification matrix**

  Host:

  ```powershell
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_acceptance_route.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_idle_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_capture_camera_idle.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_idle_capture.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_camera_route_reports.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_range_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_camera_q_candidates.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_freeze_camera_q_format.py
  cmd.exe /d /c "set COMPILER_PATH=&& .venv-saturn-tools\Scripts\python.exe tools\saturn\test_camera_q.py"
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_writers.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sourceboot_memory_map.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_compare_sh2_native_math_audit_reports.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_generate_camera_q_audit_contract.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_sh2_native_math.py
  .venv-saturn-tools/Scripts/python.exe tools/saturn/test_verify_camera_q_mutation.py
  make -f Makefile.saturn.mk OS=Windows_NT verify-runtime-contracts SATURN_TOOLS_PYTHON=$PWD/.venv-saturn-tools/Scripts/python.exe
  ```

  Target: rerun `make verify` for route 1 camera variants 1 and 2 and route 0
  camera variants 1 and 2 from fresh role-tagged output trees. Route-1 Q uses
  exact v3; route-0 Q uses `verify-camera-q-reference` as described above.
  Preserve exact commands, output paths, ELF hashes, contract mode, and memory
  reports.

- [ ] **Step 4: Write the final evidence report**

  `task3-camera-q-seam-2026-07-29.md` must state:

  - selected Q format, measured ranges, headroom, and error derivation;
  - exact bounded functions converted and explicit non-goals;
  - route digest/ID, idle pin, raw SQT1 first-seed tick, bridge counts, and
    compound non-vacuity proof;
  - two-run SCC determinism and all-sample baseline/Q bounds;
  - original-route regression results;
  - v2/v3 exact helper totals and closure stops;
  - both `sim_frt_ticks_accum` deltas;
  - selected cart-stage sectors and every HWRAM/LWRAM phase delta;
  - zero fault/fallback/reseed counters;
  - ELF/ISO/raw hashes;
  - Jo/SlaveDriver pins, licenses, files inspected, and reuse modes;
  - wall-time/emulator-speed caveat and rollback via camera variant 1.

- [ ] **Step 5: Update sprint control documents**

  Mark the design status implemented only after every gate passes. Replace the
  Task 3 summary in the sprint plan with the exact delivered seam and evidence
  links. Update the handoff's next action to Task 4 planning. Append each Task
  3 implementation/review commit and the final clean-review status to the SDD
  ledger. `.superpowers/sdd/.gitignore` intentionally ignores that local
  ledger, so update it on disk but do not force-add or include it in a commit.

- [ ] **Step 6: Obtain an independent final code/evidence review**

  Review scope includes target diff, raw evidence decoder, four SCC captures,
  four original-route captures, both comparison pairs, audit contract,
  all four native-math observations, the proposal, parser review record,
  finalized re-pin report, provenance, and build maps. Resolve every P1/P2
  finding with the standard fix/re-review loop and refresh any artifact
  invalidated by a target code change.

  After the final review is clean and `verify-final` has revalidated every
  historical byte/input hash plus the current verifier's v2 behavior, remove
  the retained evidence worktree. Derive its exact registered path from the
  committed parser SHA, require it to remain under the repository's
  `.worktrees` directory, require no tracked changes, require the recorded
  `repin_source_commit` to be an ancestor of both the finalized-report commit
  and current control HEAD, and require the retained worktree's HEAD to be the
  commit that added only the finalized report:

  ```powershell
  $repoRoot = "D:/Code/RetroDev/sm64-saturn-port/sm64-port"
  $controlWorktree = "$repoRoot/.worktrees/sh2-native-math-purge"
  $repinReport = Get-Content "$controlWorktree/docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json" -Raw | ConvertFrom-Json
  $parserCommit = [string]$repinReport.review.parser_commit
  $repinSourceCommit = [string]$repinReport.repin_source_commit
  if ($parserCommit -notmatch '^[0-9a-f]{40}$') { throw "invalid parser commit in re-pin report" }
  if ($repinSourceCommit -notmatch '^[0-9a-f]{40}$') { throw "invalid source commit in re-pin report" }
  $evidenceWorktree = "$repoRoot/.worktrees/audit-parser-evidence-$($parserCommit.Substring(0, 7))"
  $expectedPrefix = [System.IO.Path]::GetFullPath("$repoRoot/.worktrees") + [System.IO.Path]::DirectorySeparatorChar
  $resolvedEvidence = [System.IO.Path]::GetFullPath($evidenceWorktree)
  if (-not $resolvedEvidence.StartsWith($expectedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) { throw "evidence cleanup path escaped .worktrees" }
  if (-not (Test-Path $resolvedEvidence)) { throw "retained evidence worktree is missing" }
  if (git -C $resolvedEvidence -c "safe.directory=$resolvedEvidence" status --porcelain --untracked-files=no) { throw "refusing to remove evidence worktree with tracked changes" }
  $repinEvidenceCommit = (git -C $controlWorktree -c "safe.directory=$controlWorktree" log -1 --format=%H -- docs/saturn/evidence/reports/task3-native-math-audit-repin-2026-07-29.json).Trim()
  git -C $controlWorktree -c "safe.directory=$controlWorktree" merge-base --is-ancestor $repinSourceCommit $repinEvidenceCommit
  if ($LASTEXITCODE -ne 0) { throw "re-pin source is not an ancestor of report commit" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" merge-base --is-ancestor $repinSourceCommit HEAD
  if ($LASTEXITCODE -ne 0) { throw "re-pin source is not an ancestor of current commit" }
  if ((git -C $resolvedEvidence -c "safe.directory=$resolvedEvidence" rev-parse HEAD).Trim() -ne $repinEvidenceCommit) { throw "evidence worktree HEAD no longer matches re-pin evidence commit" }
  git -C $controlWorktree -c "safe.directory=$controlWorktree" worktree remove --force $resolvedEvidence
  git -C $controlWorktree -c "safe.directory=$controlWorktree" worktree prune
  ```

  `--force` is limited to the exact validated retained evidence worktree and
  removes only ignored build outputs already represented by committed hashes.

- [ ] **Step 7: Commit Task 3 closure**

  ```powershell
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge add src/port/saturn/sourceboot/Makefile tools/saturn/capture_camera_idle.py tools/saturn/test_capture_camera_idle.py tools/saturn/test_verify_sh2_native_math.py docs/saturn/evidence/reports/task3-camera-q-audit-v3-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run1-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-baseline-run2-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-q-run1-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-q-run2-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run1-2026-07-29.json docs/saturn/evidence/reports/task3-bob-parity-camera-ab-run2-2026-07-29.json docs/saturn/evidence/reports/task3-camera-q-seam-2026-07-29.md docs/superpowers/plans/2026-07-29-sh2-native-math-purge.md docs/superpowers/specs/2026-07-29-saturn-camera-q-seam-design.md docs/saturn/HANDOFF_2026-07-29-native-math-sprint.md
  git -c safe.directory=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge commit -m "docs: close Saturn camera Q seam"
  ```

## Final Acceptance Matrix

| Gate | Required result |
| --- | --- |
| Route identity | Raw SCC1 route ID 2, immutable new-manifest digest, valid same-pause SQT1 with manifest-derived first Mario seed tick, Mario dispatch witness, zoom bits `0x43AF0000` |
| Idle stability | 600 consecutive neutral samples; every state word bit-identical within each run; two byte-identical runs per role |
| Cross-role behavior | Every packed integer exact; every float within derived Q/f32 bound; all position/focus components below 1 world unit |
| Q health | Nonzero generation and pinned nonzero bridge counts; zero overflow, saturation, divide, reseed, and range-fallback counts |
| ABI/scope | Public Camera/Lakitu layout and non-Saturn behavior unchanged; radial/cutscene/rare modes remain explicit non-goals |
| Original route | `bob-parity-v1` hash unchanged; two A/B pairs pass the hardened SBR4 output contract |
| Static audit | Corrected route-0/route-1 v2 closure/call/helper facts identical; reviewed report validates its ancestor historical re-pin bytes and the current verifier's frozen v2 facts; v3 global total lower than corrected v2; complete generated Q closure; zero helper edges before exact named stops |
| Performance | Q `sim_frt_ticks_accum` strictly lower in both independent default-camera A/B pairs |
| Memory | SCC exact `0x2F7C0` replay-only NOBITS section; at least `0x4000` LWRAM remains; selected cart stage is hash-proven; final HWRAM margin is at least `0x1B00` (`0x1000` TLSF + `0x0B00` safety) |
| Provenance | Pinned source, commit, license, inspected ranges, reuse mode, notices, and material changes recorded |
| Rollback | `SATURN_CAMERA_VARIANT=1` remains unchanged and buildable |

Only after every row is proven may Task 3 be marked complete or variant 2 be
considered for a default switch.
