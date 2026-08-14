# Mario Port Product Recovery Implementation Plan

**Status:** active — Task 1 evidence review passed; baseline-first hybrid
selected. Task 2 has produced a donor-derived diagnostic CUE and a Ymir frame,
but the owner visual/audio acceptance gate remains unresolved. No current
candidate is accepted.

**Activation commit:** `1f07485a` (`docs(saturn): make playable port the product gate`)

> **For agentic workers:** Execute this plan serially. Do not dispatch parallel
> implementation lanes. Every task ends with a new live CUE or an explicit
> rollback/bypass decision. A component test or review cannot advance a task.

**Goal:** Deliver one Sega Saturn CUE that plays normal BOB and a minimum
Whomp’s Fortress proof through the same SM64 game/runtime path, with correct
controllable Mario, a correctly textured normally spawned Bob-omb, audible
music and game-triggered SFX, and at least 6.0 mean presentation FPS.

**Architecture:** Use the immutable A9A artifact as the visual/control/cadence
oracle and the current dirty tree as a donor. Transplant the smallest useful
recent components onto a working live path, one causal change at a time. Keep
candidate package/actor/audio infrastructure only when it produces a new live
result within two attempts or two hours; otherwise bypass it without inventing
a replacement format.

**Tech stack:** Original SM64 source game, SH-2 C, MC68000, Yaul/VDP1/VDP2/
SCSP, 4 MiB DRAM cartridge, repository MSYS wrapper, Ymir headless/desktop,
existing capture and profile tools.

## Global constraints

- [`../../saturn/PRODUCT_GOAL.md`](../../saturn/PRODUCT_GOAL.md) is the
  acceptance authority.
- Preserve unrelated dirty work. Never reset, clean, or broadly stage the
  shared worktree.
- Never overwrite `build/saturn/baselines/a9a-2026-08-05/`.
- Use one exact profile and print source state, profile hash, CUE/ELF hashes,
  build time, and launch path before every Ymir run.
- One behavior hypothesis per build. No batching rendering, actor, audio, or WF
  changes into one causal experiment.
- Run only the focused regression exposed by the observed failure plus target
  compile/link/memory checks before the CUE. Broad mutation, release,
  reproducibility, and audit waves are deferred.
- Any regression in boot, Mario, controls, camera, collision, accepted actors,
  audible output, or the 4.0 FPS integration floor blocks advancement.
- After two causal attempts or two hours without a new live observation, use
  the documented fallback. Do not open a repair round or new architecture task.
- Unsupported feature coverage omits one object/draw/event; it must not stall
  simulation, blank the scene, or suppress unrelated audio.
- Every kept behavior change updates `CHANGELOG.md`, this plan, and the single
  ledger in the same transition. Documentation records live facts once; it does
  not create a second review/evidence task.

## Artifact evidence contract

Each candidate record in
`docs/saturn/evidence/reports/current-product-gate.json` contains:

- source HEAD and full `git status --short` digest;
- profile path and SHA-256;
- ELF, ISO, and CUE paths, sizes, timestamps, and SHA-256;
- exact Ymir executable and BIOS hashes;
- capture start/end VBlank and first visible presentation generation;
- Mario animation/model/material/Gouraud and control/camera status;
- normal actor behavior/model/material/ground status;
- music and SFX event plus actual captured audio-output status;
- exception, allocation, stale-generation, quarantine, and dropped-event counts;
- mean, median, and 1% low presentation FPS; and
- owner verdict (`unreviewed`, `rejected`, or `accepted`).

Writing this record does not require a new general evidence framework. Extend
the existing capture/report path only if it cannot record one required scalar.

---

### Task 1: Freeze the accepted baseline and current donor

**Product result:** We can prove which artifact is accepted, which candidate is
current, and that no later launch can silently use the wrong CUE/profile.

**Files:**

- Verify: `build/saturn/baselines/a9a-2026-08-05/*`
- Read: `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-repaired-2026-08-05.json`
- Read: `tools/saturn/baseline-a9a.png`
- Create: `docs/saturn/evidence/reports/current-product-gate.json`
- Update: `STATE.md`
- Update: `.superpowers/sdd/2026-08-13-mario-port-product-recovery/progress.md`

**Interfaces:**

- Consumes the immutable A9A hashes in `PRODUCT_GOAL.md`.
- Produces one current candidate identity record used by every later task.

- [x] **Step 1: Hash-check the baseline without rebuilding**

Run `Get-FileHash -Algorithm SHA256` on the archived ELF, ISO, and CUE. Require
the exact three hashes recorded in `PRODUCT_GOAL.md`. A mismatch stops the plan;
recover from the existing archive/evidence rather than regenerating it.

Completed: archived ELF/ISO/CUE hashes exactly match `PRODUCT_GOAL.md`; the
archive was not rebuilt or changed.

- [x] **Step 2: Record the donor state**

Capture `git rev-parse HEAD`, `git status --short`, and a SHA-256 of that status
text. List current generated CUE/ELF/profile candidates with timestamps and
hashes. Do not modify or stage production files.

Completed: recorded HEAD `10a2c507fa3503656c3a61930a87acae070d24d8`,
the 384-line dirty-status digest, newest artifact tuple, and parent profile,
USA BIOS, and headless Ymir identities in `current-product-gate.json`.

- [ ] **Step 3: Visible baseline capture remains open**

Use the profile-backed Ymir path documented by the A9A evidence. Wait through
the recorded startup window and capture only after BOB and Mario are visible.
Record Mario appearance, controls/camera, and presentation cadence. Do not call
the baseline complete SM64; it is only the rollback oracle.

Attempted diagnostic only: the immutable archive layout failed the diagnostic
preflight because it intentionally lacks `obj/<cue>.elf`; the exact same-hash
original sibling-layout tuple then ran 1,680 emulated frames using explicit USA
BIOS and headless-Ymir arguments. The parent `Ymir.toml` hash is historical
comparator evidence, not a headless launch input. Its boot trace failed decode
(`magic 0x08a20417`) and the diagnostic has no video capture. No
visual/control/cadence claim is made; the visible capture remains open.

- [ ] **Step 4: Visible current-candidate capture remains open**

Launch the exact newest candidate through the same Ymir/BIOS conditions. Record
the owner-observed visual/audio/FPS failures already named in `STATE.md`. If no
candidate can be bound unambiguously, rebuild once before making any behavior
change and use that artifact as the donor record.

Attempted diagnostic only: the selected candidate's linked-code and embedded
build-identity probes matched after 683 startup VBlanks, then its cadence
capture failed with wrong magic after 600 observation VBlanks and zero
presentation events. This is a failed headless diagnostic, not visual/audio/FPS
evidence or acceptance; the visible capture remains open.

- [x] **Step 5: Select the integration base**

Choose baseline-first hybrid unless the current candidate already preserves all
baseline Mario/control/camera/FPS gates. The current candidate’s actor/audio
code remains available as donor patches either way. Record the decision; do not
debate or redesign architecture.

Decision: **baseline-first hybrid.** The current candidate did not preserve the
baseline visual/control/camera/FPS gates: it produced zero measured presentation
events in the bounded capture and no video evidence. The immutable A9A path is
the integration base; current actor/audio code remains donor-only.

**Gate:** baseline and donor are exact; diagnostic attempts ran but did not
establish visible gameplay. The visible baseline/current comparison remains
unmet and blocks any acceptance claim.

---

### Task 2: Restore Mario on the live BOB path

**Product result:** The new CUE preserves the accepted baseline’s controllable,
animated Mario with correct scale, texture colors, fixed Gouraud lighting,
front/back occlusion, and painter order.

**Status:** active — the current Make graph still unconditionally fails
actor-package validation even with dynamic actors disabled. The isolated
historical donor is therefore the live diagnostic route. It now builds from
`d7b04d61` plus the reviewed donor-only Windows extractor compatibility commit
`5808cdbf`; Ymir has captured its exact CUE. That frame proves boot and terrain
presentation only. Mario material, depth order, animation, controls/camera,
and audio are still owner-acceptance gates.

**Primary files (change only those implicated by the live diff):**

- `src/port/saturn/gfx/saturn_demo_render.c`
- `src/port/saturn/gfx/saturn_actor_bridge.c`
- `src/port/saturn/gfx/saturn_vdp1_backend.h`
- `src/port/saturn/gfx/saturn_vdp1_frame_bank.c`
- `src/port/saturn/sourceboot/main.c`
- the one focused existing renderer/source-policy test that reproduces the
  selected defect

**Interfaces:**

- Consumes the accepted A9A Mario image/commands and current donor render path.
- Produces a BOB CUE with Mario accepted before actor/audio changes.

- [ ] **Step 1: Compare current and accepted Mario command/material paths**

Inspect the current generated VDP1 command chain and the accepted-source donor
for scale, RGB1555 source, color-calculation mode, Gouraud address, local
coordinates, polygon order, and animation bank/frame. Name the first divergence
that explains an owner-observed defect.

- [ ] **Step 2: Add one focused RED regression**

Encode only that divergence—for example incorrect material ownership, missing
`CC_GOURAUD`/`CMDGRDA`, or cross-stream painter order. Do not add a broad format
or renderer test suite.

- [ ] **Step 3: Transplant the smallest accepted behavior**

Prefer the accepted A9A Mario path and fixed Gouraud table. Do not route Mario
through actor-v2 material state merely for architectural uniformity. Preserve
source animation and controls.

- [ ] **Step 4: Build and boot immediately**

Run the focused test, target compile/link/memory checks, then build one uniquely
identified CUE through the repository MSYS wrapper with serial `-j1` Make.
Print artifact/profile hashes before launch. Capture after Mario is visible.

- [ ] **Step 5: Keep or revert**

Keep only if Mario is visibly correct and controls/camera/collision remain
normal at at least 4.0 mean FPS. Otherwise revert the attempt. After two failed
attempts, restore the accepted Mario renderer wholesale as the donor boundary
and stop adapting the probationary replacement.

**Gate:** owner accepts Mario in the exact CUE. No actor or audio expansion may
start earlier.

**Blocker record (2026-08-14):**

- Current serial build stopped before SH-2 compilation at
  `compile_actor_family_bundle.py`: `family report does not match
  closure-derived semantics`. `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0` does not
  bypass its unconditional package-generation/sealing dependencies.
- Isolated donor `d7b04d61` was prepared from only the bound ROM, `build/us_pc`,
  tool environment, and an exact pinned libyaul gitlink. The apparent missing `./tools/mio0` failure was a
  diagnostic-command error: top-level `make -n` propagated its dry-run flag to
  the extractor's child `make -C tools`, printing tool commands without creating
  helpers. A real extraction regenerated the donor BOB PNG and host helpers,
  then reached the sound branch. It now stops because that branch hard-codes
  `python3`, which resolves to the unavailable Windows App Execution Alias rather
  than the approved interpreter. The reviewed donor-only `5808cdbf` change uses
  `sys.executable` for that branch; it has a focused RED/GREEN regression and
  does not alter game/package behavior. Extraction then regenerated the donor's
  own source assets and the Pipe-4 serial build produced its own CUE/ELF/ISO.
  No current generated Saturn output was borrowed.
- Exact donor diagnostic artifacts: CUE
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`,
  ELF `60c978973e682f8a1cdb3c060d58b8038ce021f7718d8f9c5a430b5cfbaff8d5`,
  ISO `0e6eb40e868016df8b321187a27cd35738edcbe34d81938b01ef09a2484b3270`.
  In-repo Ymir boot macro completed 3,300 emulated VBlanks and captured a
  320x224 BOB frame (`401737ae49bb65bfe91f0488ff0ee7b4b7aed53701cb5cbcadd711df27696375`).
  This is diagnostic evidence only, not an accepted Mario or audio result. A
  visible Ymir session for this exact CUE was launched for owner review.
- A second independent headless run held logical Saturn D-pad Up (`0x1000`) for
  120 frames after that same startup. Its 3,420-VBlank screenshot contains a
  clearly visible red/blue Mario on BOB ground
  (`f9dd7a3f133438aed8ab9d6950385b12b04e5690ee0a06ebeb4892d36be002b5`).
  This is evidence of rendering after input on the exact CUE; it does not
  establish full controls/camera, animation, audio, cadence, or owner
  acceptance.
- The donor links `source_audio_stub.c`: its `play_sound`, `play_music`, and
  `audio_init` are explicit no-ops. This CUE is therefore expected to be silent
  by source, rather than failing a profile or Ymir audio setting. Its manual
  window is a Mario/control/rendering gate only; Task 4 must add real source
  audio after Mario owner acceptance.
- Keep the bounded source-policy regression and emitter hunk uncommitted in the
  existing dirty renderer file. Do not treat it as an accepted transplant,
  expand into an actor/package repair, or open Tasks 3–6. A new CUE requires an
  explicit decision to make the one-line donor-only interpreter compatibility
  repair (`sys.executable` instead of literal `python3`) or to authorize a
  different build boundary.

---

### Task 3: Render one normal Bob-omb through a shared actor path

**Product result:** An ordinarily spawned `bhvBobomb` /
`MODEL_BLACK_BOBOMB` is recognizable, textured correctly, animated where the
source requires it, and placed on the ground while Mario remains accepted.

**Primary files (use the existing path; do not introduce a new format):**

- `src/port/saturn/gfx/saturn_actor_bridge.c`
- `src/port/saturn/gfx/saturn_actor_bundle_runtime.c`
- `src/port/saturn/gfx/saturn_actor_material.c`
- `src/port/saturn/gfx/saturn_actor_texture_residency.c`
- `src/port/saturn/gfx/saturn_demo_render.c`
- `tools/saturn/actor_variant_bank.py`
- `tools/saturn/actor_material_v2.py`
- one focused Bob-omb regression test

**Interfaces:**

- Consumes the existing normal object registry, package, queue, material, and
  residency donor code.
- Produces one real actor accepted in the same frame as accepted Mario.

- [ ] **Step 1: Prove the actor is normal**

Capture source object behavior/model identity, world position, floor height,
animation/frame, material/tile/CLUT mapping, and emitted command count. Reject
forced records, injected Cannon, first-record substitution, or a demo renderer.

- [ ] **Step 2: Compare source semantics with emitted VDP1 state**

Identify the first incorrect association: texture/CLUT address, palette/color
lane, material state leakage, pose/scale, Y placement, or painter ordering.

- [ ] **Step 3: Add one focused RED regression and minimal repair**

Repair the current shared route. If the actor-v2 path cannot produce a correct
Bob-omb within two attempts, bypass its material lowering with the smallest
source-attested donor material/geometry path while keeping the normal registry,
behavior identity, and shared final renderer. Do not inject the object.

- [ ] **Step 4: Build and boot immediately**

Require the Task 2 Mario gate, visible terrain, the normal Bob-omb, zero
scene-wide quarantine, and at least 4.0 mean FPS in the same CUE.

**Gate:** owner accepts Mario and Bob-omb together. Do not expand to all BOB
actors before audio.

---

### Task 4: Produce audible game music and SFX

**Product result:** The accepted BOB CUE plays one real music sequence and one
game-triggered SFX without regressing rendering or cadence.

**Primary files:**

- `src/port/saturn/sourceboot/source_audio_live.c`
- `src/port/saturn/sourceboot/source_audio_semantics.c`
- `src/port/saturn/audio/saturn_sound_cpu.c`
- `src/port/saturn/audio68k/pcm_voice.c`
- `tools/saturn/compile_sourceboot_sfx_bundle.py`
- the existing focused source-audio/sound-CPU tests

**Interfaces:**

- Consumes the owner-accepted standalone MC68000/SCSP playback path and source
  game audio events.
- Produces actual SCSP output while the same BOB frame loop renders.

- [ ] **Step 1: Select the shortest proven donor path**

Use the standalone soundtest’s proven boot, mailbox, sample, and voice sequence.
Do not add a protocol version, new bundle format, streaming system, positional
audio, or complete catalog.

- [ ] **Step 2: Wire one real music sequence**

Use the existing source music request and one already converted sequence. The
MC68000 owns timing independently of presentation FPS.

- [ ] **Step 3: Wire one game-triggered SFX**

Choose an event naturally produced during normal BOB play. Preserve its source
semantic ID through the existing mailbox to one validated resident PCM sample.
Do not use a proof tone or injected command as acceptance.

- [ ] **Step 4: Add only observed regressions**

Retain the cold-boot ordering and write-only-latch correction already developed.
Test safety bounds, muted failure, and the exact selected event. Do not run the
full audio catalog or release suite.

- [ ] **Step 5: Build, boot, and capture output**

Require visible accepted Mario/Bob-omb plus actual captured audio waveform or
recorded audible output. `SNDON`, heartbeat, mailbox consumption, and voice
allocation without output do not pass. Audio failure must not black-screen or
stall the game.

**Gate:** owner hears music and the game-triggered SFX in the exact accepted
visual CUE.

---

### Task 5: Recover BOB presentation cadence

**Product result:** The correct visual/audio BOB artifact reaches at least 6.0
mean presentation FPS without removing accepted features.

**Files:** change only the dominant path identified by the current capture.
Likely donors live in `src/port/saturn/gfx/`,
`src/port/saturn/runtime/saturn_source_runtime.c`, and the accepted cadence
implementation.

- [ ] **Step 1: Measure the accepted all-feature candidate**

Use `tools/saturn/capture_sourceboot_throughput.py` against the exact CUE/ELF.
Separate source simulation, construction, master finalization, transfer, and
presentation. Do not optimize a historical or feature-off image.

- [ ] **Step 2: Choose one dominant measured cost**

Make one bounded change. Prefer restoring an accepted fast donor over inventing
a scheduler or data format.

- [ ] **Step 3: Build and recapture**

Keep the change only if all visual/audio/input gates remain accepted and mean
FPS improves. Revert a neutral or negative result immediately.

- [ ] **Step 4: Repeat only while each iteration improves the live artifact**

Stop at 6.0 mean FPS for the presentation. Further optimization waits until the
second level is running.

**Gate:** BOB is owner-accepted at ≥6.0 mean FPS.

---

### Task 6: Prove Whomp’s Fortress in the same executable

**Product result:** The accepted CUE can boot/select WF and run terrain,
collision, Mario, camera, input, and the same audio backend without a
level-specific runtime.

**Primary files:**

- `src/port/saturn/sourceboot/source_entry.c`
- `src/port/saturn/sourceboot/level_headers.h`
- `src/port/saturn/sourceboot/Makefile`
- `Makefile.saturn.mk`
- `tools/saturn/collect_scene_closure.py`
- `tools/saturn/compile_scene_package.py`
- existing BOB world extraction/bake tools, parameterized only where WF proves
  a real BOB assumption

**Interfaces:**

- Consumes the accepted BOB executable path.
- Produces a runtime boot selector: default BOB; hold a documented controller
  input at boot to select WF until the retail menu is restored.

- [ ] **Step 1: Run the portability assay before editing**

Feed `levels/wf/areas/1` and its source level entry through each existing BOB
world/package producer. Record exact BOB assumptions. Do not create
`compile-wf-*` clones.

- [ ] **Step 2: Parameterize only the first blocking producer**

Replace a hardcoded BOB input/name with level/area parameters while preserving
the BOB output. Build both content sets and boot BOB immediately. If BOB
regresses, revert.

- [ ] **Step 3: Repeat one producer at a time until WF packages**

Each step must preserve the BOB CUE. Do not generalize beyond BOB and WF source
features.

- [ ] **Step 4: Add the transitional same-executable selector**

Use one boot-time controller choice to select the unmodified BOB or WF source
level entry. Do not add a new frame loop, renderer, Mario path, or audio backend.
The retail title/menu replaces this selector later.

- [ ] **Step 5: Boot WF after each causal runtime change**

Require terrain, collision, Mario, controls, camera, audio service, no exception,
and per-object omission rather than scene failure. The integration floor is
4.0 mean FPS.

- [ ] **Step 6: Optimize the current WF artifact to the presentation target**

Use the same measured one-change/one-capture loop as Task 5. Target 6–10 FPS;
acceptance requires at least 6.0 mean FPS.

**Gate:** one exact CUE runs accepted BOB and minimum WF.

---

### Task 7: Publish and owner-test the two-level presentation artifact

**Product result:** The owner receives one unambiguous CUE that demonstrates
the work and cannot be confused with a baseline or superseded build.

**Files:**

- Update: `docs/saturn/evidence/reports/current-product-gate.json`
- Update: `STATE.md`
- Update: `ROADMAP.md`
- Update: the active ledger

- [ ] **Step 1: Run the focused product regression set**

Run only tests covering defects fixed in Tasks 2–6 plus target link/memory
checks. Defer exhaustive release/reproducibility/audit waves.

- [ ] **Step 2: Capture BOB and WF from the same CUE**

Record exact source/profile/artifact/Ymir/BIOS hashes, post-render screenshots,
audio output, telemetry, and cadence for both selections.

- [ ] **Step 3: Launch visible Ymir with the verified profile**

Before launch, print the CUE path/hash/build time and level-selection control.
Do not launch a release archive, prior candidate, or different worktree.

- [ ] **Step 4: Owner manual gate**

The owner verifies Mario, controls, camera, BOB, Bob-omb, audio, WF, and visible
cadence. Record corrections without calling the artifact accepted.

- [ ] **Step 5: Accept or return to the failing task**

Only an explicit owner acceptance marks the presentation complete. A rejection
returns to exactly one failing product task; it does not reopen architecture.

---

### Task 8: Restore the retail title-to-course loop

**Status:** blocked until Task 7 owner acceptance.

**Product result:** The same port boots through the retail title/menu/file-select
flow, enters BOB or WF, collects a star, and returns through source transitions.

Use the existing source level scripts, `source_demo_data.c`, introface assets,
input, save, and audio donors. Remove the transitional direct-level selector
only after the retail path reaches both accepted courses. This task receives a
separate short live plan after Task 7; no title architecture work is authorized
earlier.

## Completion criteria

This plan completes only when Task 7 is owner-accepted. Component completion,
all-green host tests, a successful link, an identity-bound manifest, a review
PASS, or a single-level demo is insufficient.
