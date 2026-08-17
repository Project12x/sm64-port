# SM64 Saturn product goal

**Status:** authoritative

**Owner decision:** 2026-08-13
**Replaces as work authority:** every earlier Saturn roadmap, task plan, sprint,
and “source-complete” ledger. Historical documents remain evidence only.

## Product

The goal is a playable port of Super Mario 64 to Sega Saturn with the required
4 MiB DRAM cartridge. The original SM64 game loop, level scripts, Mario state,
objects, collision, camera, animation, audio semantics, and progression remain
authoritative. Saturn code supplies bounded platform services, asset lowering,
rendering, audio playback, and scheduling beneath that game.

Progress is usefulness of the assembled executable. A component is not progress
because it compiles, rejects malformed data, reproduces a hash, passes review,
or has a generic name. It is progress only when a newly built, uniquely
identified CUE advances or preserves the playable port.

## Immutable historical baseline

The accepted A9A BOB artifact is the visual/control/performance rollback oracle:

- archived path: `build/saturn/baselines/a9a-2026-08-05/`;
- ELF SHA-256:
  `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`;
- ISO SHA-256:
  `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`;
- CUE SHA-256:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`;
- measured mean presentation cadence: 5.294 FPS;
- owner-observed band: 4–6 FPS with normal controls/camera and acceptable
  Mario/BOB presentation for that slice.

Do not rebuild or replace this baseline. It is constrained and incomplete—it
lacks the required normal enemies, goals, and integrated audio—but every new
candidate must be compared with it.

## Current truth

W0 candidate `id-e8720d58595d9a62` is the current owner-accepted development
artifact: manifest SHA-256
`fd9e1ffbf2b2e23e2f14706a6173b1e72cf207f36b4328c02636cb109cc01990`, CUE
SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`,
and ELF SHA-256
`021f5fee2e0d5a24a1077bc7c41728453e9dc6fc4f4d1cc4c595bdf108b96c50`. It
passed W0's overwrite-fence regression gate for boot, play, presentation,
audio, and owner-observed 6–7 FPS. This does **not** pass the still-open
two-level presentation/product gate below: it is not proof that both BOB and
Whomp's Fortress meet that shared-executable gate.

Historical pre-W0 candidates reached the normal BOB source loop and generic
actor records, but owner observation found incorrect Mario color/material
association, flat or incorrect Gouraud, incorrect painter/occlusion order,
incorrect Bob-omb textures/placement, no audible game audio, and approximately
1 FPS in at least one run. Stale or wrongly profiled artifacts were also
launched. Those historical failures remain evidence; host and target-component
tests did not override them.

The current actor-bank, family-bundle, texture-residency, scene-publication,
and audio-package stack is **probationary**. It may be reused, transplanted, or
bypassed. It is not protected architecture until the product gates below pass.

## Presentation gate

The next deliverable is one newly built CUE, launched with one recorded profile,
that demonstrates both levels through the same executable.

### BOB acceptance

- The normal SM64 level script selects BOB; no injected model or forced object.
- Mario is controllable with correct scale, source animation, textures, fixed
  Gouraud lighting, front/back occlusion, and painter order.
- Terrain, collision, camera, and HUD remain functional.
- At least one ordinarily spawned `bhvBobomb` / `MODEL_BLACK_BOBOMB` is visible
  with recognizable source textures and correct ground placement.
- One real source music sequence is audible.
- At least one game-triggered source SFX is audible.
- Audio failure may mute audio but may not block simulation or rendering.
- No exception, allocation failure, stale generation, or whole-scene
  quarantine occurs.
- Integration work never falls below 4.0 mean presentation FPS. Presentation
  acceptance requires at least 6.0 mean FPS; 6–10 FPS is the immediate target.

### Whomp’s Fortress acceptance

- The same executable can select or transition to Whomp’s Fortress without a
  `wf`-specific frame loop, renderer, Mario path, or audio backend.
- WF terrain, collision, Mario, camera, input, and the shared music/SFX backend
  run through the same product path.
- During the first proof, unsupported WF objects may be skipped individually
  with visible telemetry. They may not stall the scene or require an injected
  substitute.
- The integration floor is 4.0 mean FPS; the presentation target is 6–10 FPS.

The presentation is not complete when only BOB passes. Both level proofs are
part of the approved end-of-week artifact.

## Port milestones after the presentation

1. Restore the retail title, menu, and file-selection path.
2. Enter BOB or WF from that path without rebuilding the executable.
3. Preserve gameplay, camera, collision, animation, HUD, music, and SFX.
4. Expand ordinary actor and effect coverage through the shared runtime.
5. Collect a star, exit the course, and return through the source level flow.
6. Add levels and content incrementally while preserving every accepted gate.
7. Optimize toward stable playable performance on emulator and then hardware.

## Reuse policy

### Proven and retained

- SH-2 native-math corrections and measured cadence fixes;
- cartridge/linker placement and immutable accepted A9A artifact;
- bounded memory, address, and DMA primitives that have target evidence;
- original source gameplay/camera/input/collision/animation ownership;
- source asset identity/extraction where it feeds a live consumer; and
- the owner-accepted standalone MC68000/SCSP sound path as the audio donor.

### Probationary

- S64P/S64F/S64B actor formats and family/variant compilers;
- generic actor queues, meshlet workspaces, texture residency, and scene
  publication;
- scene-derived audio bundles and semantic event transport; and
- overlapped rendering/publication machinery not preserved by a current live
  artifact.

A probationary component gets at most two causal integration attempts or two
hours without a new live observation. Then it is bypassed or replaced by the
smallest proven donor path. Sunk implementation cost is not a reason to keep it.

### Deferred until the presentation passes

- new wire formats or version generations;
- generalized multi-level frameworks beyond the minimum WF proof;
- complete BOB actor enumeration;
- release sealing, hermetic A/B reproduction, capacity campaigns, and broad
  audit campaigns; and
- speculative optimization without a current capture.

## Development loop

For each causal change:

1. State the visible or audible hypothesis and the exact current artifact.
2. Add only the smallest test that would have caught the observed defect.
3. Make the smallest implementation change.
4. Build one new uniquely identified CUE.
5. Verify its source/profile/CUE hashes before launch.
6. Run Ymir long enough for gameplay to render; capture visual, audio, input,
   failure, memory, and cadence evidence.
7. Keep the change only if it improves the requested behavior and preserves the
   preceding accepted gates. Otherwise revert or bypass it.

Do not accumulate several behavior changes before the emulator observation.
Do not perform a broad review or release rebuild before the live result. A
review may reject unsafe code after the result, but it may not expand the scope
without a new owner decision.

## Status vocabulary

- `historical`: useful prior evidence; not the current executable.
- `candidate`: compiled or host-tested but not observed in the current CUE.
- `live-observed`: exact current CUE produced the recorded behavior.
- `owner-accepted`: the owner manually accepted the exact artifact.
- `blocked`: a named external or safety prerequisite prevents the next live
  observation.

`source-complete` and `host-contract-passed` are component descriptions only.
They never advance this product goal.
