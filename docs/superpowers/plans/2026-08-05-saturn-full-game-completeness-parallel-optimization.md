# Saturn Full-Game Completeness and Parallel Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a representative Saturn-only SM64 runtime that exposes all 209 source-selected Mario animations, renders the complete generated Bob-omb Battlefield dynamic-family/effect closure, preserves the original music and SFX semantics through the MC68000/SCSP, and continues reducing renderer/runtime overhead. The final all-features BOB build must measure at least 4.0 mean presentation FPS in the pinned setup before the next 12--15 FPS optimization sprint.

**Architecture:** The inherited SM64 source remains authoritative for gameplay, camera, animation selection, object behavior, and audio policy. Offline tools generate deterministic scene, animation, actor, and audio packages. The master SH-2 publishes immutable snapshots and remains the sole simulation, package-residency, final-order, VDP1-lowering, and presentation owner; either SH-2 may claim bounded scene/actor render jobs. The MC68000 independently schedules sequences, envelopes, and SCSP voices from bounded semantic commands. BOB is the first complete manifest and manual proof, not a runtime special case; Whomp's Fortress area 1 is the second-level generation/load proof.

**Tech Stack:** C11, freestanding MC68000 C/assembly, SH-2 Q16.16, VDP1, VDP2, SCSP, CPU-DMAC, SCU DMA, Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, Python 3 generators/tests, MSYS2 SH/68K toolchains through `tools/saturn/with-msys-toolchain.ps1`, and Ymir with the repository profile and 32-Mbit DRAM cart.

## Governing documents and inherited evidence

- Approved design: `docs/superpowers/specs/2026-08-05-saturn-full-game-completeness-parallel-optimization-design.md`
- Superseded active plan whose completed evidence remains valid: `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`
- Inherited execution evidence: `docs/saturn/evidence/reports/overlapped-render-pipeline-2026-08-03.md`
- Engine ownership: `docs/saturn/ENGINE_PORT_ARCHITECTURE.md`
- Prior-art/provenance: `docs/saturn/UPSTREAM_CODE_LEDGER.md`, `docs/saturn/SLAVEDRIVER_ADAPTATION.md`, `docs/saturn/SGL_REFERENCE_NOTES.md`, and `docs/saturn/audio/PCM68K_PROVENANCE.md`
- Accepted rollback baseline: A9A BOB/live-input/Q16-camera/DRAM-cart/Pipeline-4 CUE, manually accepted with normal controls/camera and 4--6 FPS.
- Preserve commits `27cebc7e`, `d5f70887`, and `d7b04d61`; this sprint consumes their Task 10 selector/source-contract/blocker evidence and does not relabel it.

## Live design amendments

- 2026-08-05 audio-loader audit: keep Yaul pinned and unmodified unless a concrete Yaul-core defect is target-proven. Use its low-level SMPC/memory-map boundary through a project-owned, tested sound-CPU boot wrapper; do not make the warned convenience SNDON/SNDOFF wrappers part of the game audio contract.
- 2026-08-05 audio-scheduling audit: retain semantic, pointer-free SH-2 commands. Sequence/layer/note timing, ADSR, allocation, desired-voice construction, slot-shadow comparison, and SCSP register writes all remain MC68000 work; a native cross-CPU `SaturnVoiceState` table is explicitly rejected.
- 2026-08-05 residency audit: a complete sound-RAM clear is legal only during cold boot or explicit driver recovery. Scene/package preparation may write only validated unowned spans, must retain the active generation through commit, and may retire it only after the replacement generation is live.
- 2026-08-05 transfer audit: CPU copy is the correctness baseline. SCU DMA to sound RAM is an optional later optimization and may be enabled only after target evidence proves the exact source/destination legality, cache/barrier ordering, and non-interference with renderer DMA ownership.
- 2026-08-05 native-math review repair: BOB null-camera suppression is authorized only after the verifier hashes the supplied linked ELF and every reviewed source input, confirms the unchanged BOB route to the terminal return, and rejects every extra `levelNum` rewrite/use. Renderer callback facts come only from the table returned by the factory that the renderer initialization actually activates; lifecycle ownership is proven inside the extracted roots. This is source-complete under `f349fe9f..52c75422`; the prescribed Qt wrapper failure and the capped broad exact-ELF run remain open evidence, not green gates.
- 2026-08-05 identity review correction: `effective_config_hash` and the compiled identity must include every compiler-affecting sourceboot wrapper value, not merely the release feature tuple. Archive validation must bind the accepted A9A artifact trio to the exact recorded capture cadence, target identity/size, real profile content, and successful launch evidence before copying anything; a conflicting manifest must fail before archive state changes.
- 2026-08-05 closure review correction: deterministic output is insufficient. The scene collector must enumerate and verify every reachable native computed spawn/effect edge or fail closed; `LOAD_MODEL_FROM_GEO`, geo provenance, cycles, act-scoped instance maxima, schema-complete records, source-derived audio declarations, and byte-stable checkout-independent serialization are part of the closure contract.
- 2026-08-05 closure review round-two correction: native closure must conservatively traverse reachable helper/action/respawner/particle dispatch and recognized computed behavior tables, then reject unknown dynamic forms. Every rules-file path and edge/count/capacity claim must be source-attested and repository-hashed. Inline LevelScript parsing is area-scoped, while audio facts are function/behavior-scoped and bind the declared `SOUND_ARG_LOAD` bank rather than token prefixes.
- 2026-08-05 closure review round-three correction: native indexing/traversal is repository-wide over the bounded source closure, not behavior-directory-local. Every resolved model/geo/animation root requires hashed provenance; nested area links and area-local music are fail-closed. Schema validates referential/aggregate integrity. A `maximum_live_instances` value is a source-attested live bound (spawn cadence plus child lifetime/explicit cap), never a one-time spawn factor; any unprovable continuous emitter fails generation.
- 2026-08-05 closure review round-four correction: the bounded source index includes every repository source location reachable from a generated callback, and absent definitions are errors. One behavior may carry several concrete model/geo variants with provenance for every variant. Audio follows call-site arguments/semantic parameter flow, never all tokens inside a generic helper. Schema validates provenance symbols and complete typed classification. BehaviorScript bounds are evaluated at each spawn site; exception bounds require source-attested deletion/state proofs. Entry-script `JUMP_LINK` expansion and comment stripping precede area/music parsing.
- 2026-08-05 closure review round-five correction: every reachable symbol reference—direct call, data, or function-pointer/table dispatch—must resolve uniquely or reject the closure. A `SOUND_*` token becomes an audio fact only where bounded call-site dataflow proves the invoked callee is an audio sink or forwarding wrapper; arbitrary non-audio calls never contribute SFX.
- 2026-08-05 S64P review correction: dependency references are stable-ID based at the compiler boundary and lower only after global canonical ordering; the packed mask is limited to 32 canonical payload ordinals and unknown/duplicate/cyclic/absent-bit references fail closed. Header emission accepts an explicit payload manifest for dependency-bearing roots. ABI structs/enums are emitted once in a shared guarded header; per-scene headers contain constants only. Provisional inline sections use `NONE` destinations and zero residency accounting by design, and their reports must label those budgets as non-residency evidence until Tasks 5/7/11/12/22 provide final placement.
- 2026-08-05 residency review correction: a validated S64P view is never a residency lease. Task 5 copies the root and feature-active payload bytes into bounded owned spans, rehashes those spans at commit, and stages exactly two generations using absolute aligned high-water accounting after the immutable SOURCE.DAT prefix. Render, VDP1, actor/animation, and audio consumers hold one-shot generation-scoped lease tokens; duplicate or stale release fails closed. Sourceboot rejects provisional roots through the linked-root caller while preserving the caller's output view on failure. The snapshot Make quoting defect remains an explicit open gate.
- 2026-08-05 PCM v2 correction: the sound mailbox keeps byte-addressed big-endian records and separates an eight-entry control ring from a 24-entry SFX ring. Two-lap cursors make every physical slot usable while preserving empty/full distinction; producer bytes/telemetry publish before ownership cursors, and the MC68000 validates both ring snapshots then drains control before SFX within its bounded poll budget. The v1 wire proof remains historical and is never silently reinterpreted as v2.
- 2026-08-05 animation-bank correction: the compact 209-ID bank is source-complete and differentially proven, but its mesh tables intentionally remain the legacy normal-cap/front-eye/open-hand compatibility selection. Executable C validation binds the repository-pinned 193-file inventory, exact GEO1 relationships, global gap-free tier-0 primitive ownership, and scratch bounds; the advisory JSON schema never substitutes for that validator. Full switch-variant geometry and runtime branch/state cutover remain Task 10 responsibilities.
- 2026-08-05 source-geo seam correction: the state-only seam is not proven. The normal graph walk remains authoritative because the old root skip changes graph-owned state; Task 8's checked evidence is only a static source audit plus an illustrative digest model. No runtime caller or suppression is permitted until a future task executes the real normal/suppressed `geo_process_root()` paths and proves every required domain.
- 2026-08-05 audio-policy review correction: Task 9's source adapter retains untouched requests until tick-time admission, refreshes moving sources by exact `soundBits + source-token + package-generation` identity, and keeps pending-token lifetimes alive across same-frame stop/control calls. The SH jingle guard is decremented before completion consumption, preserving the source's tick-two one-shot completion. This is source-complete only; transport binding, MC68000 playback, package residency, target/Ymir, and manual audio remain later-task gates.
- 2026-08-05 pose-runtime review correction: Task 10's enabled complete-animation path uses checked int64 translation sums, honors matrix-multiply failure, and publishes a two-slot pose handoff selected by `pose_slot` so frame-overlap workers never read a single mutable global. Legacy `walking_bank`/neutral fallback remains feature-off-only compatibility state; the complete path does not assign it. The selector ABI grows with the shared header and must not be treated as a frozen byte-size contract.
- 2026-08-05 actor-family review correction: Task 11 keeps compiler and C ABI capability ranking identical (total mask bits, then bounded multiplicity), canonicalizes family representatives/order independent of closure input order, sums shared-family live multiplicity, and binds every S64F payload to a nonzero expected SHA-256 in the C validator. Zero/unknown flags and tampered payloads fail closed. The current BOB evidence intentionally reports 13 unsupported geo nodes, so the complete-closure gate remains open.
- 2026-08-05 audio-catalog review correction: Task 12's hardened ABI/SHA/signed-PCM/metadata/residency slices are retained, but the task is blocked rather than promoted. The repository lacks the real expanded `00_sound_player` payload, BOB/WF closures are still music-only, S64P `AUDIO_DEPENDENCIES` and closure-selectable S64A chunks are not emitted, and the 68K active-plan validation plus general m64 control-flow parser remain incomplete. Synthetic fixtures must not be presented as full-game audio evidence.
- 2026-08-05 scene-admission integration correction: Task 13's production sourceboot path now links `saturn_scene_admission.c` and consumes regenerated BOB admission metadata rather than a hand-assembled runtime table. The queued bounded worklist handles the real 1183-node BOB graph; immutable view orientation takes precedence, zero lateral rows use a conservative depth-only fallback, and validation rejects reserved fields, non-endpoint portal ownership, missing global cluster coverage, and node-containment violations while retaining mandatory clusters. The generic path is source-complete, but target/Ymir/manual/FPS evidence and whole-game closure remain open.
- 2026-08-06 actor-snapshot review correction: Task 14's production seam now brackets the authoritative geo walk and publishes per-bank generation tickets with P2/fence handoff, but it deliberately rejects unresolved family/scene-package/bank identities rather than fabricating actors. Full-pool identity/capacity, typed visibility/switch/range/held/effect fields, package-bound budget, wrap/overlap cleanup, and target sourceboot compilation remain open; host fixtures are not source-complete evidence.
- 2026-08-06 actor-queue review correction: Task 16's `0549f7af` queue is a useful generic exact-once infrastructure seam, but not the production dual-SH2 actor renderer. Its repair must account for the fixed 65,536-byte actor arena (including aligned banks, observer, queue, batches, and output records), use P2-safe metadata reads, test output overflow/exact-fit boundaries, and keep actor-meshlet/production drain, target, Ymir, manual, and FPS gates open.
- 2026-08-06 actor-queue repair acceptance: `d4efe0e9` closes the infrastructure-slice review findings. One Task 14 bank container (both snapshot generations), observer, queue, batches, alignment, and 2,806 eight-byte output records fit exactly in the fixed 65,536-byte actor arena; 2,807 and output-count overflow fail closed, valid instance 64 is covered, and batch count uses a generation-checked P2 accessor. This does not promote Task 16: actor-meshlet preparation, production drain/cutover, manifest drawable bound, target retirement race, and target/Ymir/manual/FPS remain open.
- 2026-08-06 sequence-VM scope correction: Task 15 reuses the pinned Project12x sequence/layer timing and control-flow semantics by close-port at the semantic boundary, while replacing N64 pointer/RSP assumptions with bounded offsets and scalar events. The repository contains disassembled source shape but no expanded seq00 binary, so the VM scaffold proves malformed/control-flow behavior and source timing only; full 35-sequence coverage, S64P linkage, MC68000 image, SCSP transport, target, Ymir, and audible evidence remain blocked.
- 2026-08-06 sequence-VM parity repair (superseded by `b004fe7b`): the first repair added persistent small-layer state, reserve operands, branch polarity, channel state, and bounded-flow tests; review then found the large-note/d7/format-scope defects recorded and closed by the acceptance amendment below.
- 2026-08-06 sequence-VM repair acceptance: `b004fe7b` closes the bounded-slice findings. Large-layer note0→note1 resets local/persistent duration, d7 ORs selected active bits and clears only selected finished bits, EU/SH layer f4 uses bounded signed-relative flow, and EU/SH sequence da/dc fail closed immediately after opcode fetch before incompatible operand consumption. Rereview is SPEC/QUALITY PASS, C0/I0/M0 for the bounded host VM slice; full catalog/S64P/MC68000/SCSP/target/Ymir/manual gates remain open.
- 2026-08-06 voice-scheduler scope correction: Task 17 is source-complete only for bounded MC68000-shaped allocator/timer/desired-voice/slot-shadow infrastructure. It preserves Project12x priority/lifetime/ADSR/tuning/pan semantics and uses PoneSound/Yaul only at the documented hardware/order boundary, but remains outside the heartbeat image until Task 12 supplies real sequence/sample/residency data and a later task wires the production timer/drain path.
- 2026-08-06 voice-scheduler review correction: Task 17's first independent rereview is SPEC/QUALITY FAIL, C1/I2/M0. Stale-generation slot recovery can clear software state while hardware remains keyed, the freestanding m68k gate can pass from cached objects without pinned tool discovery, and the report overstates source-faithful ADSR while software TL and SCSP EG both participate. A repair must make stale recovery transactional, force a reproducible toolchain rebuild, and narrow/trace envelope ownership before the bounded slice is accepted.
- 2026-08-06 voice-scheduler repair: `144b4aa6` makes stale-generation recovery transactional (successful key-off clears software state; zero-capacity retains/retries with capacity telemetry), forces a pinned PoneSound/GCC rebuild with artifact hash and empty `nm -u`, and makes software total-level the sole envelope owner with neutral SCSP EG words. The actual Project12x release range and simplified/non-source-faithful envelope boundary are documented; independent rereview is pending.
- 2026-08-06 voice-scheduler repair acceptance: independent rereview of `144b4aa6` is SPEC/QUALITY PASS, C0/I0/M0. Real `scsp_pcm8` caller-path tests prove transactional stale key-off/retry; forced `-B` m68k verification attests the pinned PoneSound/GCC/artifact hashes and empty `nm -u`; software TL is the sole deliberately simplified envelope owner. Production heartbeat/package/target/Ymir/manual/FPS remain open.
- 2026-08-06 actor-capability scope correction: Task 18's generic slice derives class bits from closure records and runtime masks from fields actually present in the actor ABI. It can validate rigid/opaque/static-transform/platform/collectible class requirements without a family whitelist, but surface/collectible source fields remain unresolved and fail closed; BOB still reports 13 unsupported records and `complete_closure=false`.
- 2026-08-06 actor-capability review correction: Task 18's first rereview is SPEC/QUALITY FAIL, C0/I2/M1. The 52→56-byte S64F layout needs a version/record-size compatibility boundary and trusted external hash; the runtime-mask mutation must reseal its payload to exercise the intended unknown-bit validator; and reports must distinguish 13 unsupported family representatives/reasons from 14 closure actor records. Capability hints are derived facts while requirements validate them; schema-v1 typed-field gaps remain open.
- 2026-08-06 actor-capability second review correction: S64F version/schema is still `v1` despite the incompatible 56-byte record; platform/collectible bits come from an optional unowned `capability_hints` dictionary rather than generated schema-validated closure evidence and therefore remain unavailable/fail-closed; and the diagnostic test/report needs an independent exact-family oracle (including UNKNOWN_CAPABILITY and stable ID `0x9322461f`) rather than self-derived unresolved lists. The ABI/hash/mutation/terminology repair remains active.

## Prior art and reuse mode

- SlaveDriver Engine `a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later: inspected `WALLS.C:288-500,1240-1408,1803-1950,2062-2285`, `WALLASM.S:253-353`, `DMA.C`, `DMA.H`, `SCL_FUNC.C`, `V_BLANK.C:94-145`, `INITMAIN.C`, `MEMCPY.S`, and `LINK.S`. Retain the existing attributed close ports in `src/port/saturn/gpl/`; new work defaults to pattern-only unless its task records an exact compatible close-port.
- Sonic Z-Treme `cff75451c1616aac1236fc2b44223902b55c706b`, GPL-3.0: inspected `ZT_FRUSTUM.c:126-161`, `ZT_RENDERING.c:406-505,718-786`, `ZT_LOADING.c:118-176,299-355`, `workarea.c:14-20`, and `ZTE_DEF.H`. Task 13 may extend the existing attributed frustum close-port; other work is pattern-only unless separately recorded.
- Jo Engine `556d081146211b6a1cfa6591d70f9487d406758b`, MIT repository/BSD-3-Clause per-file: inspected `jo_engine/math.c:57-70`, `jo_engine/vdp1_command_pipeline.c`, `jo_engine/3d.c`, and `jo_engine/jo/sega_saturn.h`. Retain the documented fixed-math adaptation and pattern study; do not adopt its SGL allocator/runtime.
- Yaul `6012f79f237773378c8014e70d8998ad95a38d98`, MIT: inspected public VDP1/VDP2/DMA/CD/CDFS/DRAM-cart APIs, `sh-elf/lib/ldscripts/yaul.x`, `libmic3d/render.c`, `libmic3d/sort.c/.h`, `libmic3d/light.c`, and `libyaul/scu/bus/cpu/cpu_dmac.c`. It remains the pinned dependency/API and memory-map authority; close-port only when a task records exact files and notices.
- Yaul examples `66b648eb059bb8bb7392eac70821605a68205b85`, repository license file absent at the pin: inspected `scsp-ponesound-pcm8/Makefile`, `ponesound.c`, `ponesound.h`, and `scsp-ponesound-pcm8.c`. Pattern-only evidence confirms embedded `sdrv.bin` loading, 512-KiB SCSP RAM selection, generic SMPC sound-CPU commands, shared sound-RAM communication, and VBlank publication of PoneSound's `start` field. Copy no example source or binary; reject its full-RAM clear for post-boot transitions and its VBlank-triggered scheduler for SM64 cadence.
- PoneSound `31782e4c61337327f23eb9aa45ecd37fe0944ea0`, MIT: inspected `LICENSE`, `README.md`, `documentation.md`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, and `jo_demo/pcmsys.c/.h`. Retain only the documented vector/linker and SCSP slot/pitch close-port. The semantic protocol, sequence VM, and source-policy bridge are project work.
- Inherited Project12x SM64 audio at repository pin `36d015fb`: inspect `src/audio/external.c`, `seqplayer.c`, `playback.c`, `synthesis.c`, `heap.c`, `load.c`, `data.c`, and their headers before Tasks 9, 12, 15, or 17 write code. Reuse mode is in-tree semantic close-port: retain public IDs, policy, sequence control flow, layer/note rules, ADSR/release, priority, and tuning while replacing N64 task/RSP/ABI and pointer layouts with bounded big-endian Saturn records and MC68000/SCSP execution. The inherited Project12x baseline has no root license file, so do not export these routines into a newly licensed component or claim a new license; preserve existing source notices and record exact ranges/material changes. A literal port of the N64 synthesis/task backend is rejected for architecture/runtime fit: the freestanding MC68000 has a different ABI, no RSP audio microcode, bounded sound RAM, and must emit SCSP slot events rather than N64 command lists.
- `malucard/sm64-psx` `3073845688ea273da78d539b20c45110d8a868c3`, no repository-wide license established: inspected `README.md`, `src/game/game_init.c`, `src/port/gfx/gfx_rsp_jit.c`, `src/port/psx/gfx_dl_exec_psx.c`, `gfx_tessellation_psx.c`, `gfx_texture_psx.c`, `cd_psx.c`, `controller_psx.c`, `src/game/hud.c`, `tools/preprocess_graphics.py`, `convert_image_psx.py`, `pack_textures.py`, and `compress_mario_anims.c`. Behavior/architecture study only: compact generated IR/assets, target-native lowering, whole-loop profiling, and residency inform this plan; copy no PS1 code, GTE math, ordering tables, VRAM layout, or packets.
- Sega SGL 3.02j user-supplied archive, proprietary: only the documentation listed in `docs/saturn/SGL_REFERENCE_NOTES.md` was studied. No SGL code, binary, header, or sample is copied, linked, or redistributed.
- Eyepatch Entertainment's private 3DO/Saturn project is feasibility evidence only. No dependency or reuse is permitted without a later license/provenance review.

## Global constraints

1. This is a Saturn-exclusive repository. Do not preserve PC/N64 buildability at the expense of the Saturn architecture.
2. Source gameplay, collision, camera, animation selection/frame advancement, object behavior, and audio policy remain authoritative. Saturn presentation code consumes immutable state; it does not reimplement those systems.
3. Shared runtime modules may not branch on `bob`, `wf`, an enemy name, or a hand-authored level path. BOB/WF names are permitted only in generators, fixtures, package selection, and evidence.
4. BOB scope is the generated transitive closure of LevelScript objects, macro presets, model/geo roots, behavior-spawned children/rewards/projectiles/effects, act variants, animation/material features, sequences, and sound banks. A prose list is never authoritative.
5. Every source-selected `MARIO_ANIM_*` table entry must resolve through the complete animation bank. No idle/walk whitelist and no private Castle animation clock may remain on the enabled path.
6. Audio keeps the exact public signatures in `src/audio/external.h`. SH-2 commands are pointer-free and semantic; the MC68000 timer, not the 4--6 FPS frame loop, owns sequence/note/envelope cadence.
7. Full audio content compiles, but only closure-selected bundles are resident. The package compiler must prove each resident bundle fits sound RAM; generated Nintendo-derived sequence/PCM package bytes remain untracked.
8. The master alone owns source simulation, package loading/eviction, final painter order, Gouraud reservation, VDP1 lowering, VRAM, and publication. Neither audio nor renderer work may borrow the other's queues, DMA ownership, timers, or fences.
9. VDP1 owns world/actor polygon commands and VDP2 owns the sky/background, HUD, windows, color-offset/fade, and final composition duties suited to its planes. A task may not duplicate the same visual workload on both processors or infer that VDP2's 60 Hz composition means VDP1 construction is healthy.
10. One active render generation retains its scene/actor/animation package identities until retirement. Loading `N+1` may not invalidate `N`; partial/stale/corrupt data fails closed with telemetry and never triggers full-frame replay.
11. Zero admitted world clusters or actor instances is valid. Unknown families/features, capacity overflow, stale generations, missing assets, audio stalls, and invalid package hashes are explicit failures, never silent substitution.
12. Intermediate feature builds may regress. Record attributable simulation/source-graph/render/command/transfer/audio counters, but impose no arbitrary intermediate FPS threshold. The one numeric sprint gate is mean presentation cadence **at least 4.0 FPS** in the pinned comparative Ymir setup; "4--6 FPS" names the accepted observed band, not a second or subjective threshold. Only Task 29 applies this gate.
13. All builds and Ymir runs are serialized. Use `-j1`, the DLL-preflight wrapper, a worktree-local MSYS `HOME/TMPDIR/TMP`, and never run concurrent cross-toolchain jobs on the user's busy CPU.
14. Every behavior-changing commit updates `CHANGELOG.md` in the same commit. Before the next task, the controller updates this plan and its SDD ledger with status, commits, tests actually run, reviews, design corrections, and every open gate.
15. Host evidence never closes target/Ymir/manual gates. Failed or unexecuted gates stay unchecked with a reason.
16. Full sound-RAM clear outside cold boot/explicit recovery, VBlank-driven audio scheduling, and a native C voice table shared between SH-2 and MC68000 are forbidden on the enabled semantic-audio path. Yaul remains an upstream dependency unless an exact core defect is recorded and target-proven.

## Subagent-driven execution protocol

This plan has four logically parallel lanes—animation, actors/scenes, audio, and optimization—but implementation is deliberately interleaved through integration waves. The subagent-driven-development protocol allows only one implementation subagent at a time in the shared worktree; independent read-only research may run concurrently. This avoids central-file conflicts and protects the busy CPU while still advancing every lane before the next integration wave.

Before Task 1:

- [ ] Verify the current worktree/branch and preserve unrelated dirty files.
- [ ] Run the SDD skill's `scripts/sdd-workspace` for this exact plan.
- [ ] Create `.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md` with this plan path on line 1.
- [ ] Preflight the whole plan for contradictions and create one todo per task.

For every task:

1. The controller records `BASE=$(git rev-parse HEAD)`, extracts the task brief, and dispatches one fresh implementer with only the brief, required earlier interfaces, report path, binding constraints, and an explicit model/reasoning tier. Use the strongest available coding model with high reasoning for interface/ownership/runtime tasks and the normal coding model with high reasoning for bounded tool/test tasks; never leave tier selection implicit.
2. The implementer writes the failing test first, runs the named RED command and records the expected failure, implements the smallest complete slice, runs focused GREEN plus the named regression gate, updates `CHANGELOG.md`, commits, self-reviews, and writes the report file.
3. The controller generates a `BASE..HEAD` review package and dispatches an independent strongest-available reviewer at high reasoning. The reviewer reports two ordered verdicts—first specification compliance, then code quality—and both must approve.
4. Critical/Important findings enter the five-round skill fix loop. Rounds 1--3 resume the implementer; rounds 4--5 use a fresh stronger implementer. Every fix receives a scoped re-review. Load-bearing findings still open after round 5 block the sprint.
5. The controller immediately updates this plan and the new ledger, preserving unchecked target/manual gates, before releasing the next task. No progress is held for a later documentation batch.
6. A final most-capable whole-branch reviewer receives the complete review package and all deferred/parked findings before Task 29 can close.

## File ownership and conflict barriers

- Native-math owner: `tools/saturn/verify_sh2_native_math.py` and `tools/saturn/test_verify_sh2_native_math.py` until Task 1 lands.
- Animation owner: Mario/actor animation generators, generated animation-bank format, and pose evaluator.
- Actor/scene owner: closure/schema/package tools, actor-family manifests, immutable instance snapshots, and actor capability tests.
- Audio owner: `src/port/saturn/audio/**`, `src/port/saturn/audio68k/**`, audio package tools/tests, and the sole implementation of public source audio symbols.
- Optimization owner: scene admission, batching, shared render graph/runtime, draw merge/lowering, and performance telemetry; it consumes animation/actor/audio ABIs without redefining them.
- Integration-spine owner only: `Makefile.saturn.mk`, `src/port/saturn/sourceboot/Makefile`, `src/port/saturn/sourceboot/main.c`, `src/port/saturn/gfx/saturn_demo_render.c`, `src/port/saturn/gfx/saturn_render_snapshot.h`, `src/port/saturn/gfx/saturn_fast3d_frontend.h`, ISO contents, build identity, `CHANGELOG.md`, and public status/architecture docs.
- No task may increase the current eight render descriptors or widen the eight-bit dependency mask until the BOB closure proves the existing three actor-batch pairs plus world pair cannot fit. Any ABI expansion is a separately reviewed correction with target memory-map evidence.

## Integration-wave dependency map

| Wave | Tasks | Parallel lane intent | Release condition |
|---|---|---|---|
| 1 | 1--5 | truthfulness, identity, closure, package foundation | native-math blocker reconciled; S64P schema/provisional fixtures and bounded loader green; no final BOB-root claim |
| 2 | 6--10 | audio control, full Mario bank, geo evidence, source audio policy | protocol v2, all animation IDs, differential state seam, source-policy host models green |
| 3 | 11--17 | generic actor banks/snapshots, audio content/VM/voices, scene admission | representative actor/audio packages and generic queue/SCSP execution green |
| 4 | 18--23 | complete BOB actors/effects and audible semantic closure | final BOB root resealed from completed payloads; visual/audio closure has zero unresolved required capabilities |
| 5 | 24--27 | render reductions, better SH-2 granularity/overlap, WF proof | reviewed gains/counters and second-level package load proof green |
| 6 | 28--29 | four-build attribution and all-features acceptance | exact identities, final >=4.0 mean FPS recovery, manual semantics, final review |

## Live task status — update on every transition

Unchecked entries must append one of `pending`, `active`, `source-complete`, or
`blocked` during execution. `source-complete` remains unchecked until every
required target/Ymir/manual gate for that task is complete. Checked means the
task's implementation, reviews, evidence, and transition documentation are
all complete.

- [ ] Task 1 — source-complete — commits `f349fe9f`, `52c75422`; independent rereview PASS. Focused 222/222, mutation, and equivalent MSYS normal host gate are green. The prescribed Qt wrapper quote defect and capped broad exact-ELF verifier remain open; no target/Ymir/manual evidence is claimed.
- [ ] Task 2 — source-complete — commits `f5a0248d`, `8c97fd4e` (reports `264f3b4b`, `5008759e`); independent rereview PASS. Identity 9/9, sourceboot identity 5/5, capture 35/35, archive 12/12, and no-build baseline revalidation are green. Linked SH-2 symbol/layout, Ymir/manual, and Task 1 broad native-math gates remain open.
- [ ] Task 3 — source-complete — final rereview SPEC/QUALITY PASS. Generic 19/19, real BOB 1/1, inventory 1/1, and serial closure compilation are green. BOB is 86 records, 133 source hashes, 54 proven SFX IDs/8 banks; target/Ymir/FPS/package/native-math/manual gates remain open.
- [x] Task 4 — source-complete — implementation `ba40126c`, repair `a3e22842`; independent rereview SPEC/QUALITY PASS with no findings. Schema 12/12, determinism 3/3, `py_compile`, and serial DLL-preflight provisional-package gate are green; provisional SHA was stable (`84A1716C32A1563443EF00DECEF2BEEBC3C7581F113D56298F9D862522BF1F6E`). Final BOB/WF reseal, target boot, Ymir/FPS, native-math, and manual gates remain deferred to their owning tasks.
- [x] Task 5 — source-complete — implementation `422096a1`, ownership/source-cart repair `c7d6e148`, declaration follow-up `eb99c47c`, lease-token repair `26986d94`; independent rereview SPEC/QUALITY PASS, C0/I0/M0. Serial C runtime/residency gates and neutrality/sourceboot 6/6 are green. Snapshot Make quoting, target link/memory inspection, final BOB/WF roots, Ymir/FPS, native-math, and manual gates remain deferred.
- [x] Task 6 — source-complete — implementation `7bb8f87f`; independent review SPEC/QUALITY PASS, C0/I0/M0. V1/v2 ABI, transport/publication, 68K control-first model, heartbeat, soundtest migration, linked image/zero-map, and PCM mutations 12/12 are green. The exact Qt wrapper EOF and broad test-tools timeout remain explicitly open; no target/Ymir/audible claim.
- [x] Task 7 — source-complete — implementation `902ada8a`, repairs `68f9dd10`, `9ae757d5`, `b573b6bb`, final review record `8929b2c9`; independent rereview SPEC/QUALITY PASS, C0/I0. Actor source 8/8, actor bank 8/8, Mario pose 17/17; native-path DLL-preflight C11/Werror and actor-pose fixture pass. Make path-conversion caveat, switch-variant/runtime cutover, target/Ymir/manual/FPS/final-package gates remain deferred.
- [ ] Task 8 — blocked/deferred — evidence-only commits `a9c3f9c5`, `3110c3de`; independent rereview SPEC/QUALITY PASS for claim qualification, but the real graph differential remains open and no seam is enabled.
- [x] Task 9 — source-complete — implementation `9a904766`; repairs `84fedde2`, `a5d14609`, `003a7637`, `6da2aa0b`, `9a0bc8fa`, `1f71656f`, `80672126`, `f12e2ee6`; independent final rereview SPEC/QUALITY PASS, C0/I0/M0. Serial DLL-preflight `verify-audio-policy verify-audio-spatial`, Python source/ABI 4/4, strict feature-on C11/Werror syntax, and scoped diff checks pass. Source semantics are complete through the pointer-free SH-2 policy boundary; sequence VM, MC68000 voice service, package/transport, target/Ymir/manual/FPS gates remain open.
- [x] Task 10 — source-complete — implementation `ea6a1236`, hardening `a4d00a61`; independent rereview SPEC/QUALITY PASS, C0/I0/M1. Focused actor-pose bank, feature identity 5/5, variant identity 2/2, sweep validator 2/2, strict feature 0/1 bridge syntax, and scoped diff checks pass. The complete path is source-selected and two-slot immutable across overlap; linked variant/Ymir/manual gates remain open because the dry-run stops at missing `YAUL_INSTALL_ROOT`.
- [x] Task 11 — source-complete — implementation `52999c35`, repairs `acff11a8`, `7f0caa62`; independent final rereview SPEC/QUALITY PASS, C0/I0/M0. Generic suite 4/4, full-game source contract 4/4, serial `compile-actor-banks SCENE_LEVEL=bob SCENE_AREA=1`, strict actor-bank C11/Werror syntax, executable C tamper/hash and unequal-capability ranking checks, and scoped diff pass. BOB emits 47 deterministic S64F families (99,105 bytes, SHA `97dc231b…`) from 86 closure records/133 source hashes; 13 unsupported geo nodes keep `complete_closure=false`. S64P linkage, runtime cutover, target/Ymir/manual/FPS gates remain open.
- [ ] Task 12 — blocked — hardened commits `48d6401c`, `628c8324`, `724aaa84`, `7c0bbb72`, `7b8aa224`, `cc092c69`, `fa0f1040`, `78d1f967`, `b06382c8`, `2da45926`, `90b152e6`, `583c3840`, `ac3b91b2`, `b937456e`; independent rereview remains SPEC/QUALITY FAIL for the broader task. ABI/SHA/signed-PCM/metadata/work-span slices are preserved, and the MC68000 active+replacement plan check is now implemented, but the official package gate is blocked by the absent real seq00 asset, music-only BOB/WF closures, missing S64P/closure-selectable payload linkage, and incomplete general m64 control-flow parsing. Task 13 may proceed independently; Task 12 must not claim a full catalog or target/Ymir/manual evidence.
- [x] Task 13 — source-complete — implementation `33e06fb8`; repairs `280e1804`, `f6e0aa03`, `0d6707c8`, `d2e5b604`, `a888ff00`; independent rereview SPEC/QUALITY PASS, C0/I0/M0. Generic scene admission, regenerated BOB metadata, sourceboot linkage, queued 1183-node worklist, containment/coverage/reserved-field/portal checks, and depth-only orientation fallback are green in the focused serial gates. Target/Ymir/manual/FPS evidence remains open.
- [ ] Task 14 — active/source-incomplete — implementation `56c33d76`; repairs `7cb28651`, `508a8da1`; independent rereviews remain SPEC/QUALITY FAIL (latest C2/I3). Authoritative observation ordering, pool-slot overflow telemetry, per-bank tickets, wrap-aware handoff, P2/fence metadata, and target-LWRAM assertion are hardened, but unresolved family/scene/bank registry, full-pool identity, typed source fields, payload cache visibility, overlap cleanup, package-bound budget, and sourceboot target gates remain open. No nonzero production actor claim.
- [ ] Task 15 — active/source-incomplete — commits `df95a107`, `52d45d5e`, `211158ea`, `b004fe7b`; bounded pointer-free VM scaffold and `verify-sequence-vm` are green, with Project12x ranges/provenance and source-parity repairs recorded. Bounded-slice rereview is SPEC/QUALITY PASS, C0/I0/M0. Full seq00/35-sequence/S64P/MC68000 image/SCSP/target/Ymir/manual audio gates remain blocked by Task 12 assets and later integration.
- [ ] Task 16 — active/source-incomplete — infrastructure `0549f7af`; independent rereview SPEC/QUALITY FAIL. Exact-once queue/batching, stale-generation quarantine, and host mutation gates pass, but the 64-instance arena/accounting/P2/output-boundary repair is in progress and actor-meshlet/production renderer cutover remains open.
- [ ] Task 17 — active/source-incomplete — infrastructure commits `5b74081c`, `144b4aa6`; serial allocator/slot-shadow/timer/SCSP-PCM8/sequence-VM and forced m68k provenance gates pass. Repair rereview is SPEC/QUALITY PASS, C0/I0/M0. Full envelope/package data, heartbeat/MC68000 drain, target image, Ymir/tempo/manual/FPS evidence remain open.
- [ ] Task 18 — active/source-incomplete — generic capability slice `a319583c`; serial actor-capability-bank/C mutation, closure-derived Python 2/2, and generic actor-bank 4/4 gates pass. BOB payload is 47 families/86 closure records (SHA `53f541e6…`); 13 unsupported family representatives/reasons cover 14 closure actor records. Default opaque query fails closed on eight exact representatives; rigid/static adds five existing unsupported geo families. Independent rereview is SPEC/QUALITY FAIL, C0/I2/M1; ABI/hash/mutation/terminology repair is active. No full BOB/target/Ymir/manual/FPS claim.
- [ ] Task 19 — close BOB articulated/enemy/boss capabilities
- [ ] Task 20 — close BOB billboard/translucent/shadow/effect capabilities
- [ ] Task 21 — integrate bounded audio package boot/residency in sourceboot
- [ ] Task 22 — prove complete BOB visual/dynamic closure
- [ ] Task 23 — prove audible BOB music/SFX semantic closure
- [ ] Task 24 — reduce command, Gouraud, sort, and repeated-memory work
- [ ] Task 25 — improve opportunistic dual-SH2 job granularity
- [ ] Task 26 — extend useful work across transfer/presentation fences
- [ ] Task 27 — prove Whomp's Fortress generation and package transition
- [ ] Task 28 — build and capture the four-feature diagnostic matrix
- [ ] Task 29 — recover all-features BOB to >=4.0 mean FPS and publish sprint evidence

---

### Task 1: Reconcile the inherited native-math publication blocker

**Lane:** integration spine. **Depends on:** inherited Task 10 evidence. **Produces:** trustworthy broad host/linked-image gate.

**Files:**

- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify: `tools/saturn/sh2_native_math_route_oracle_v1.txt`
- Modify: `CHANGELOG.md`
- Reconcile after review: this plan, new SDD ledger, old Task 10 plan/evidence status only; preserve unrelated dirty ledger work.

**Contract:** The verifier may remove only the two exact unreachable BOB null-camera-trigger indirect calls proven by the pinned route. It must fail closed for a non-BOB route, non-null BOB camera table, changed guard/branch shape, a third reachable indirect call, or a missing owned disassembly block. It may not fabricate calls or static facts.

**Design correction (2026-08-05):** The inherited route oracle still names
pre-descriptor-queue worker symbols that the exact ELF no longer contains.
Task 1 updates the checked route-oracle text and any directly coupled tracked
digest fixture as source-of-truth inputs; it must not conceal those stale names
by filtering them only in Python.

- [x] Add/confirm all five mutations in `test_verify_sh2_native_math.py` and run RED:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
  ```

  Expected pre-fix result: the checked-in renderer oracle test errors with `expected one terrain_worker definition, got 0`; mutations must not be accidentally green through permissive parsing.

- [x] Repair the parser/oracle against the actual descriptor-queue source shape; keep the proof pinned to the source and ELF identities.
- [ ] Run focused GREEN and the two mutation gates serially — source suite (`222/222`), prescribed mutation gate, and equivalent MSYS normal host gate passed; the exact Qt `mingw32-make` wrapper command remains unchecked because `/usr/bin/sh` ends with the documented unmatched-quote error:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-render-native-math
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-render-native-math-mutation
  ```

- [x] Commit independently as `fix(saturn): reconcile descriptor native-math oracle` with changelog reasoning; do not mix scene/audio/renderer features. Follow-up identity hardening is `52c75422`.
- [x] Obtain clean task review, then record whether the full Task 10 host gate is now runnable; do not mark linked target evidence green from host tests. Rereview PASS: the broad gate is runnable, but its existing exact-ELF run timed out at `600.7s` and is still unchecked.

### Task 2: Add authoritative feature and package identity

**Lane:** integration spine. **Depends on:** Task 1 for broad gate, but may develop against focused tests. **Produces:** immutable identity for every diagnostic build/capture.

**Files:**

- Create: `src/port/saturn/platform/saturn_build_identity.h`
- Create: `src/port/saturn/platform/saturn_build_identity.c`
- Create: `tools/saturn/gen_build_identity.py`
- Create: `tools/saturn/test_gen_build_identity.py`
- Create: `tools/saturn/test_sourceboot_feature_identity.py`
- Create: `tools/saturn/archive_a9a_baseline.py`
- Create: `tools/saturn/test_archive_a9a_baseline.py`
- Create: `docs/saturn/evidence/reports/completeness-sprint-a9a-baseline-2026-08-05.json`
- Modify: `src/port/saturn/sourceboot/Makefile`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/capture_sourceboot_throughput.py`
- Modify: `tools/saturn/test_capture_sourceboot_throughput.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Interface:**

```c
enum sm64_saturn_feature_bits {
    SM64_SATURN_FEATURE_COMPLETE_MARIO_ANIMATION = 1U << 0,
    SM64_SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE    = 1U << 1,
    SM64_SATURN_FEATURE_SEMANTIC_AUDIO           = 1U << 2,
};

typedef struct sm64_saturn_build_identity {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t feature_bits;
    uint16_t renderer_pipeline, level_id, area_id, route_id;
    uint16_t route_replay_mode, live_input_mode;
    uint16_t camera_route, camera_variant, diagnostic_mode, reserved0;
    uint32_t bootstrap_ticks;
    uint16_t cart_mbit, cart_stage_sectors, hot_promotion, near_clip;
    uint16_t bsp_order, polygon_tier, fragment_mode, reserved1;
    uint8_t source_hash[32], effective_config_hash[32];
    uint8_t route_artifact_hash[32], input_artifact_hash[32];
    uint8_t camera_artifact_hash[32], cart_profile_hash[32];
    uint8_t scene_package_hash[32], scene_dependency_set_hash[32];
    uint8_t actor_package_hash[32], animation_package_hash[32];
    uint8_t audio_package_hash[32];
} sm64_saturn_build_identity_t;

extern const sm64_saturn_build_identity_t saturn_build_identity;
```

Add validated `0|1` make variables `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION`, `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE`, and `SATURN_FEATURE_SEMANTIC_AUDIO`. `SATURN_RENDERER_PIPELINE` remains validated as `2|3|4`. Replay and live-input are separate serialized booleans because the accepted route uses both. `diagnostic_mode` is `0` for promotable builds and a versioned nonzero enum only for the all-animation and scene-transition target probes; diagnostic artifacts can never satisfy Task 29. The generator canonicalizes every behavior-affecting value from the target wrapper—including replay mode, live-input mode, bootstrap ticks, route, camera route/variant, cart size/staging/profile, hot promotion, clipping, BSP ordering, polygon/LOD tier, fragmentation, and feature tuple—into `effective_config_hash`. Route, input, camera, and cart-profile artifacts also retain their individual SHA-256 values. Output labels derive from the compiled identity, never vice versa.

Before generating any replacement, archive the already accepted A9A artifacts without rebuilding them. Source the exact paths from `docs/saturn/evidence/reports/a9a-step11-overlap-throughput-repaired-2026-08-05.json`, copy the matching trio into `build/saturn/baselines/a9a-2026-08-05/`, and refuse any input whose hashes differ from ELF `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`, ISO `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`, or CUE `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`. The tracked baseline manifest records original/archive paths and hashes, capture/profile/config evidence, measured 5.294 FPS mean, and ancestry checks for `27cebc7e`, `d5f70887`, and `d7b04d61`. This is the immutable historical rollback; later feature-off rebuilds are comparative descendants, not replacements.

- [x] Write archive RED tests for a wrong ELF/CUE/ISO hash, a CUE naming a different ISO, missing capture/config/profile evidence, overwrite of a different archive, and failed preserved-commit ancestry. Run the RED command before implementation:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py
  ```

- [x] Archive and hash-verify the accepted A9A trio, write the tracked manifest, and run `git merge-base --is-ancestor` for all three preserved commits. Never regenerate an artifact to satisfy this step. The revalidation binds exact recorded cadence/target/profile/launch evidence and fails before copies on a manifest conflict.
- [x] Write identity tests rejecting invalid values, missing/stale package hashes, absent ELF identity, label/compiled-feature drift, and capture/loaded-ELF tuple mismatch. The mutation table is exhaustive: independently mutate magic, version, size, every reserved-zero field, renderer pipeline, level ID, area ID, route ID, replay flag, live-input flag, bootstrap count, camera route, camera variant, diagnostic mode, cart size, cart staging count, hot-promotion flag, near-clip flag, BSP flag, polygon/LOD tier, fragmentation mode, each individual feature bit, source hash, effective-config hash, route artifact hash, input artifact hash, camera artifact hash, cart-profile hash, S64P root hash, S64P dependency-set hash, aggregate actor payload hash, animation payload hash, and audio payload hash. Also test every valid replay/live-input combination and reject non-boolean encodings. Every mutation must make generation or capture validation fail; compiler-affecting ATAN2/demo/view/slave/camera-probe/BSP-flat/trace controls are now individually represented and mutated. Run RED:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
  ```

- [x] Generate a fixed-width versioned identity and teach the capture path to resolve and hash-check it before accepting telemetry.
- [x] Run GREEN plus capture regressions:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_gen_build_identity.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_throughput.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_archive_a9a_baseline.py
  ```

- [x] Commit as `feat(saturn): bind feature tuple to target identity`; review identity/version/endianness and all label mutations. Repair rereview PASS; required linked-target and Ymir/manual evidence remain unchecked.

### Task 3: Generate the transitive BOB closure

**Lane:** actors/scenes. **Depends on:** none for isolated tooling. **Produces:** authoritative required family/effect/audio set consumed by Tasks 4, 11, 12, 18--23.

**Files:**

- Create: `tools/saturn/scene_package_schema.py`
- Create: `tools/saturn/collect_scene_closure.py`
- Create: `tools/saturn/behavior_spawn_rules.json`
- Create: `tools/saturn/test_scene_closure.py`
- Create: `tools/saturn/test_bob_scene_closure.py`
- Replace the narrow role of: `tools/saturn/test_actor_generalization_inventory.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Output:** `build/saturn/packages/bob/1/closure.json`, schema `sm64-saturn-scene-closure-v1`. Every record names source path/hash and declares level/area, act mask, object/model/behavior/geo roots, recursively spawned children/rewards/projectiles/effects, animation table, material feature bits, maximum live instances, music sequence IDs, and SFX banks/IDs.

- [x] Build fixtures for LevelScript `OBJECT*`, macro presets, `LOAD_MODEL_FROM_GEO`, behavior `spawn_object*`/reward edges, acts, model-less controllers, and cycles. Add mutations for an undeclared child, missing model/geo root, stale source hash, duplicate stable ID, and a hand-written BOB-only schema field; run RED:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
  ```

- [x] Implement deterministic parsing plus explicit reviewed spawn rules for computed spawn sites that static syntax cannot resolve. Every manual rule names the exact behavior source and reason; unknown edges fail closed.
- [x] Generate BOB twice and prove byte-identical JSON and complete source-hash coverage. The former Goomba 11-instance assertion becomes one generated closure fact, not the scope boundary.
- [x] Run GREEN and target creation:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_closure.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_scene_closure.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure SCENE_LEVEL=bob SCENE_AREA=1
  ```

- [x] Commit as `feat(saturn): derive transitive scene dependency closure`; final fifth-round rereview PASS; target/Ymir/FPS/package/native-math/manual evidence remains unchecked.

### Task 4: Compile the generic S64P schema and provisional fixtures

**Lane:** actors/scenes. **Depends on:** Task 3. **Produces:** the generic S64P schema/compiler/validator plus deterministic fixture roots; it does **not** claim the final BOB root before payload producers finish.

**Files:**

- Create: `tools/saturn/compile_scene_package.py`
- Create: `tools/saturn/validate_scene_package.py`
- Create: `tools/saturn/emit_scene_package_header.py`
- Create: `tools/saturn/test_scene_package_schema.py`
- Create: `tools/saturn/test_scene_package_determinism.py`
- Refactor with compatibility wrappers: `tools/saturn/extract_bob_area.py`, `tools/saturn/emit_bob_scene.py`, `tools/saturn/compile_bob_bsp.py`, `tools/saturn/bake_bob_bsp_fragments.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Binary ABI:** big-endian `S64P`, version 1, with:

```c
typedef struct sm64_saturn_scene_package_header {
    uint32_t magic;
    uint16_t version, header_size;
    uint32_t package_size;
    uint16_t level_id, area_id, section_count, flags;
    uint8_t package_sha256[32];
    uint8_t dependency_set_sha256[32];
} sm64_saturn_scene_package_header_t;
```

`S64P` is the single atomic root of a scene generation, not a promise that every multi-megabyte asset is embedded in one resident blob. Its closed section-kind enum is: `WORLD_STATIC`, `COLLISION`, `SKY_BACKGROUND`, `BSP_PORTAL`, `ACTOR_DEPENDENCIES`, `ANIMATION_DEPENDENCIES`, `AUDIO_DEPENDENCIES`, and `RESIDENCY_PLAN`. Inline world/collision/sky/BSP records live in the root. Each dependency section contains sorted content-addressed descriptors for external actor/animation/audio payloads: payload kind/stable ID, byte count, SHA-256, destination class, lifetime, dependency mask, and maximum scratch. The root hash covers every descriptor, and the dependency-set hash covers the sorted payload hashes, so a root plus the wrong payload cannot validate. Each ordinary section descriptor records kind, offset, byte size, power-of-two alignment, destination class, lifetime, dependency mask, content hash, and maximum runtime scratch. Task 4 outputs schema fixtures plus a provisional world-only BOB root under `build/saturn/packages/bob/1/provisional/`. Payload-producing Tasks 7, 11, and 12 emit content-addressed dependencies; Task 22 is the sole final BOB link/reseal owner and writes `build/saturn/packages/bob/1/final/`. Task 27 does the same for WF. A provisional root cannot enter a target artifact or satisfy any closure gate.

- [x] Add RED tests for wrong magic/version/root/dependency-set/payload hash, overlapping/out-of-order sections, bad alignment, dependency cycles, missing/extra/wrong-generation actor/animation/audio payload, unknown section/lifetime/destination, budget overflow, nondeterministic descriptor ordering, and BOB-specific binary fields. Run them before implementation and record their expected missing-schema/compiler failures. Repair coverage additionally mutates payload cycles, absent dependency-mask bits, descriptor metadata, cross-kind IDs, and nonzero-mask shuffled inputs. Focused schema 12/12 and determinism 3/3 are green:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_package_schema.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_package_determinism.py
  ```
- [x] Implement schema/packing and retain the existing BOB tools as thin callers of generic functions until their consumers migrate. Link deterministic synthetic payload fixtures and a clearly tagged provisional BOB world root; do not invent future actor/animation/audio hashes. Stable-ID lowering, global uniqueness, payload-manifest validation, and one shared generated ABI header were added in repair `a3e22842`.
- [x] Run twice and compare hashes. The serial DLL-preflight `compile-provisional-scene-package -j1` gate passed three times; two-run whole-file SHA matched `84A1716C32A1563443EF00DECEF2BEEBC3C7581F113D56298F9D862522BF1F6E`:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_package_schema.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_package_determinism.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-provisional-scene-package SCENE_LEVEL=bob SCENE_AREA=1
  ```

- [x] Commit as `feat(saturn): compile versioned scene packages`; implementation `ba40126c`, repair `a3e22842`; independent review and rereview both examined packing overflow, endianness, deterministic hashes, payload manifests, shared ABI emission, and compatibility-wrapper equivalence. Optional broad BobMeshIR coverage timed out under the CPU guard after two passing tests and remains non-gating.

### Task 5: Validate and retain scene-package residency

**Lane:** integration spine. **Depends on:** Task 4. **Produces:** master-owned atomic root-plus-payload load/commit/unload with exact-generation retention, proven against synthetic fixture payloads until the final BOB root is resealed in Task 22.

**Files:**

- Create: `src/port/saturn/runtime/saturn_scene_package.h/.c`
- Create: `src/port/saturn/runtime/saturn_scene_residency.h/.c`
- Create: `tools/saturn/scene_package_runtime_test.c`
- Create: `tools/saturn/scene_residency_test.c`
- Create: `tools/saturn/test_scene_runtime_neutrality.py`
- Modify: `src/port/saturn/sourceboot/source_cart.h/.c`
- Modify: `src/port/saturn/gfx/saturn_render_snapshot.h`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Interfaces:**

```c
bool sm64_saturn_scene_package_validate(const void *bytes, uint32_t byte_count,
                                        sm64_saturn_scene_package_view_t *view);
bool sm64_saturn_scene_residency_begin(sm64_saturn_scene_residency_t *state,
                                       const sm64_saturn_scene_package_view_t *view,
                                       uint32_t generation);
bool sm64_saturn_scene_residency_load_section(sm64_saturn_scene_residency_t *state,
                                              uint16_t section_index);
bool sm64_saturn_scene_residency_commit(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
bool sm64_saturn_scene_residency_unload(sm64_saturn_scene_residency_t *state,
                                        uint32_t generation);
```

- [x] Write RED lifecycle tests: dependency order, partial root/payload load quarantine, stale generation, wrong root/dependency-set/payload hash, actor/audio payload generation mismatch, WRAM/cart/SOUND_RAM/scratch capacity exhaustion, exact-fit and cross-generation alignment, active-generation eviction refusal, failed atomic commit/rollback, double commit, zero-section package, owned-span mutation, malformed offset, provisional-root rejection, and duplicate/stale lease release.
- [x] Implement bounded views with no package pointers in peer-visible queue descriptors. Commit a root and its complete **feature-active** payload set under one generation ticket or not at all; inactive-feature descriptors remain hash-validated but are not made resident, preserving diagnostic attribution. Add `scene_package_id`, root/dependency-set hashes, active-feature mask, and resolved actor/animation/audio bank identities to immutable snapshots; copy and rehash owned bytes; retain every resident payload of generation `N` until one-shot render, VDP1, actor/animation, and audio lease tokens retire it. Provisional roots are rejected by the linked sourceboot caller, and SOURCE.DAT prefix/high-water placement remains explicit.
- [x] Run GREEN: serial DLL-preflight runtime/residency C gates pass; neutrality/sourceboot policy is 6/6. The broader Snapshot Make target remains open solely for the inherited quoted-executable EOF; its direct produced binary/source-policy check is recorded but does not substitute for that Make gate.

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-scene-package-runtime verify-scene-residency
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_runtime_neutrality.py
  ```

- [x] Commit as `feat(saturn): retain scene packages by render generation`; implementation `422096a1`, repairs `c7d6e148`, `eb99c47c`, `26986d94`; independent review/rereview pass. Source-cart bounds, owned-byte rollback, lease-token lifetime, and generic no-`bob|wf` neutrality are recorded; target link/Ymir/manual evidence remains open.

### Task 6: Evolve PCM protocol v2 with protected control capacity

**Lane:** audio. **Depends on:** existing audible PCM68K prototype. **Produces:** pointer-free control/SFX transport that cannot lose music control to SFX pressure.

**Files:**

- Modify: `src/port/saturn/audio/saturn_pcm_protocol.h`
- Modify: `src/port/saturn/audio/saturn_pcm_transport.h/.c`
- Modify: `src/port/saturn/audio68k/main.c`, `src/port/saturn/audio68k/pcm_voice.h/.c`
- Create: `tools/saturn/audio_protocol_v2_test.c`
- Modify: `tools/saturn/pcm_protocol_test.c`, `tools/saturn/pcm_transport_test.c`
- Modify: `src/port/saturn/audio68k/Makefile`, `tools/saturn/verify_pcm68k_image.py`, `Makefile.saturn.mk`, `CHANGELOG.md`

**Wire ABI:** Keep the mailbox big-endian, byte-addressed, pointer-free, and fixed at `0x04000--0x04FFF`. Protocol v2 divides the existing 512-byte command region into control ring `8 x 16` at `0x04040` and SFX ring `24 x 16` at `0x040C0`; both end at `0x04240`. Driver/work/sample regions remain `0x00000--0x03FFF`, `0x05000--0x07FFF`, and `0x08000--0x7FFFF`.

Control opcodes: RESET, MUTE, SET_MASTER, PACKAGE_PREPARE, PACKAGE_COMMIT, SEQ_START, SEQ_STOP, SEQ_FADE, SEQ_CHANNEL_FADE, BANK_MASK, SOUND_MODE. SFX opcodes: PLAY_REFRESH, STOP_HANDLE, STOP_SOURCE, STOP_BANK.

```c
bool sm64_saturn_audio_control_enqueue(sm64_saturn_pcm_transport_t *transport,
                                       sm64_saturn_audio_opcode_t opcode,
                                       const uint16_t words[7]);
bool sm64_saturn_audio_sfx_enqueue(sm64_saturn_pcm_transport_t *transport,
                                   sm64_saturn_audio_opcode_t opcode,
                                   const uint16_t words[7]);
```

- [x] Add RED tests for both ring layouts/wrap, big-endian words, producer-last publication, corrupt indices, v1/v2 mismatch, SFX saturation with successful control enqueue, control saturation telemetry, and control-first consumption. The final model/image mutation class is 12/12.
- [x] Implement v2 without weakening the historical v1 proof. Migrate soundtest only after v2 host gates pass; preserve its owner-confirmed evidence as history. The MC68000 consumes control before SFX and reports separate saturation/consumption counters.
- [x] Run GREEN serially: DLL-preflight/MSYS make equivalents and linked-image gates pass. The exact Qt `mingw32-make` spelling remains open only for the inherited unmatched-quote `.exe` launch; broad `test_tools.py` timed out after early/BOB passes and was not substituted by the focused rerun.

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-protocol-v2 verify-pcm-transport verify-pcm68k-model verify-pcm68k-image
  ```

- [x] Commit as `feat(saturn): reserve semantic audio control transport`; `7bb8f87f`; independent review pass covered offsets, publication order, starvation/control priority, v1 preservation, and 68K image/stack/work bounds.

### Task 7: Generate the complete compact Mario animation bank

**Lane:** animation. **Depends on:** Task 3's package IDs but not actor runtime. **Produces:** the content-addressed animation payload referenced by the S64P root, containing all 209 `MARIO_ANIM_*` records without pre-baking every frame.

**Files:**

- Create: `tools/saturn/actor_source.py`
- Create: `tools/saturn/compile_actor_bank.py`
- Create: `tools/saturn/manifests/actors/mario.json`
- Create: `tools/saturn/schemas/saturn-actor-bank-v1.schema.json`
- Create: `tools/saturn/test_actor_source.py`
- Create: `tools/saturn/test_actor_bank.py`
- Create: `src/port/saturn/gfx/saturn_actor_bank.h/.c`
- Create: `tools/saturn/actor_pose_bank_test.c`
- Modify narrowly: `tools/saturn/extract_mario_actor.py`, `tools/mario_anims_converter.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Format:** Compact source animation channels plus joint-local vertices, joint ownership, branch/node ordinals, material/meshlet tables, hashes, and maximum scratch. Do not generate full-frame vertex arrays for all animations; the existing 1.32-MiB `saturn_mario_actor_mesh.h` already demonstrates why that shape cannot scale.

```c
typedef struct sm64_saturn_actor_animation_record {
    uint32_t values_offset, indices_offset;
    uint16_t frame_count, joint_count, flags;
    int16_t y_translation_divisor;
} sm64_saturn_actor_animation_record_t;

typedef struct sm64_saturn_actor_bank {
    uint32_t magic;
    uint16_t version, family_id, model_id, joint_count;
    uint16_t animation_count, meshlet_count, primitive_count;
    uint16_t vertex_count, max_instances;
    uint32_t feature_mask, source_hash_words[8];
} sm64_saturn_actor_bank_t;
```

- [x] Extract shared parsing helpers while requiring byte-identical output from the legacy Mario generator. RED mutations covered missing/duplicate IDs, corrupt frame/index spans, bad joint ordinals, missing hashes, unsupported geo nodes, nondeterministic order, in-range relationship corruption, global primitive gaps/duplicates, and distinctness.
- [x] Require exactly 209 records matching `include/mario_animation_ids.h` and the repository-pinned canonical 193-file `assets/anims/*.inc.c` inventory (inventory digest `2d7c66e9…`). Every record is reachable by stable animation ID; no BOB/reachable whitelist.
- [x] Differentially evaluate every legacy neutral/walking frame through compact channels and require exact posed vertices/light inputs before retaining compatibility. Do not delete `saturn_mario_actor_mesh.h`; switch-variant geometry remains explicitly deferred to Task 10.
- [x] Run GREEN: actor source 8/8, actor bank 8/8, MarioActorPoseTests 17/17, native-path DLL-preflight C11/Werror actor-pose fixture pass. The combined Make command remains unclaimed only for path-conversion/inherited meshlet executable launch issues.

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_source.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_bank.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-pose-bank verify-actor-meshlets
  ```

- [x] Commit as `feat(saturn): compile complete compact Mario animations`; implementation/repair commits `902ada8a`, `68f9dd10`, `9ae757d5`, `b573b6bb`, final rereview `8929b2c9`; source ordering, channel semantics, joint mapping, executable bounds, global mesh ownership, and legacy differential were independently reviewed.

### Task 8: Prove a state-only source-geo seam differentially

**Lane:** optimization. **Depends on:** Task 1. **Produces:** evidence-gated option to remove redundant display construction while retaining all source state mutations.

**Files:**

- Create: `src/port/saturn/runtime/saturn_source_geo_state.h/.c`
- Create: `tools/saturn/source_geo_state_diff_test.c`
- Create: `tools/saturn/test_source_geo_state_contract.py`
- Modify only after RED evidence: `src/port/saturn/runtime/saturn_source_runtime.h/.c`
- Modify only after differential pass: `tools/saturn/test_source_render_suppression.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

```c
typedef struct sm64_saturn_source_geo_digest {
    uint32_t generation, object_count, animation_digest;
    uint32_t geo_state_digest, visibility_digest;
} sm64_saturn_source_geo_digest_t;

bool sm64_saturn_source_geo_update_state(uint32_t generation,
                                         sm64_saturn_source_geo_digest_t *digest);
```

- [ ] RED must demonstrate the old `geo_process_root()` suppression changes at least animation, painting/warp, water/moving-texture, camera/matrix-derived object state, or lifecycle digest. A test that only compares draw output is insufficient.
- [x] Record the evidence-only deferral: no bounded seam currently produces exact real-graph digests, so the full geo walk remains authoritative and the reserved API is fail-closed/uncalled. No speculative renderer suppression was added.
- [x] Run static/illustrative GREEN: `test_source_geo_state_contract.py` 8/8 and DLL-preflight serial C11/Werror contract execution pass. The real graph differential and combined render-policy Make gate remain explicitly open.

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-source-geo-state-diff verify-source-render-policy
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_source_geo_state_contract.py
  ```

- [x] Commit the evidence-only deferral as `test(saturn): bound source geo suppression` (`a9c3f9c5`) plus claim-qualification repair (`3110c3de`); independent rereview SPEC/QUALITY PASS, C0/I0/M0. Review confirms no source mutation moved into the renderer. No FPS threshold applies.

### Task 9: Preserve source music/SFX policy and spatial semantics

**Lane:** audio. **Depends on:** Task 6. **Produces:** source-authoritative semantic state/events while keeping public APIs unchanged.

**Files:**

- Create: `src/port/saturn/audio/saturn_audio_policy.h/.c`
- Create: `src/port/saturn/audio/saturn_audio_spatial.h/.c`
- Create: `tools/saturn/audio_policy_test.c`
- Create: `tools/saturn/audio_spatial_diff_test.c`
- Create: `tools/saturn/test_full_game_audio_source.py`
- Stage replacement implementation: `src/port/saturn/sourceboot/source_audio_semantics.c`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Ownership:** Adapt policy from the repository's inherited `src/audio/external.c`: the six-entry background queue, priority/duplicate rules, secondary music, jingles, fades, lower/unlower constraints, bank masks, continuous freshness, source stop, and getters remain SH-2 source policy. Only sequence bytecode timing/notes/voices move to the MC68000.

`PLAY_REFRESH` carries original 32-bit `soundBits`, a bounded 16-bit source token, package generation, quantized volume/pan/pitch, and freshness generation. Never pass `f32 *pos` across CPUs; use a bounded pointer-identity side table owned by the source adapter.

- [ ] Write differential RED fixtures from representative source policy traces: queue insertion/preemption, duplicate sequence, fade, jingle interruption/resume, lower/unlower, bank disable/enable, discrete vs continuous SFX, pointer-token reuse, source stop, and moving-source pan/pitch.
- [ ] Preserve every signature in `src/audio/external.h`. `source_audio_semantics.c` is the only semantic implementation of those symbols; the silent stub remains a feature-off rollback translation unit.
- [ ] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-policy verify-audio-spatial
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_full_game_audio_source.py
  ```

- [ ] Commit as `feat(saturn): preserve source audio policy semantics`; record exact adapted source files/ranges and reuse mode in provenance. Review public ABI equivalence and pointer-free transport.

### Task 10: Activate source-selected complete Mario poses

**Lane:** animation, then integration spine. **Depends on:** Tasks 2 and 7. **Produces:** runtime-selected compact pose for every source animation ID.

**Files:**

- Create: `src/port/saturn/gfx/saturn_actor_pose.h/.c`
- Modify: `src/port/saturn/gfx/saturn_actor_bridge.h/.c`
- Modify: `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`
- Modify: `tools/saturn/actor_pose_bank_test.c`, `tools/saturn/actor_meshlet_test.c`
- Create: `tools/saturn/build_sourceboot_variant.py`
- Create: `tools/saturn/test_build_sourceboot_variant.py`
- Create: `tools/saturn/capture_sourceboot_animation_sweep.py`
- Create: `tools/saturn/test_capture_sourceboot_animation_sweep.py`
- Integration-owner modify: `src/port/saturn/gfx/saturn_render_snapshot.h`, `src/port/saturn/gfx/saturn_demo_render.c`, `src/port/saturn/sourceboot/Makefile`, `Makefile.saturn.mk`
- Modify: `CHANGELOG.md`

```c
typedef struct sm64_saturn_actor_pose_work {
    int16_t (*vertices)[3];
    uint8_t *light_intensity;
    int32_t *joint_matrices_q16;
    uint16_t vertex_capacity, joint_capacity;
} sm64_saturn_actor_pose_work_t;

bool sm64_saturn_actor_pose_evaluate(const sm64_saturn_actor_bank_t *bank,
                                     int16_t animation_id,
                                     int16_t animation_frame,
                                     sm64_saturn_actor_pose_work_t *work,
                                     sm64_saturn_actor_pose_view_t *pose);
```

- [ ] Add RED tests for all 209 IDs, negative/overflow frames, root translation, channel sampling, scratch bounds, corrupt offsets, and feature-off compatibility. Explicitly assert `is_walking_family`, `walking_bank`, and neutral fallback are absent from the enabled complete-animation path. Also RED-test that the target sweep rejects a missing/duplicate ID, a non-production evaluator symbol, a promotable `diagnostic_mode=0` identity, or a mismatched ELF/CUE/hash.
- [ ] Evaluate only the selected pose into bounded scratch after the authoritative source geo tick and immutable snapshot capture. Keep material/switch state source-owned; do not advance animation in Saturn code.
- [ ] Integrate `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1` and bind its bank hash to Task 2 identity. Feature `0` retains the accepted rollback path until Task 28.
- [ ] Add a non-promotable `SATURN_DIAGNOSTIC_MODE=animation-sweep` sourceboot mode that feeds every ID `0..208` through the **same production evaluator and meshlet preparation path**, publishes ID/frame/result/hash telemetry, and terminates with `seen_count=209`, no fallback, no corrupt bounds, and no target fault. It may choose IDs only; it may not own a second evaluator or alter the normal source-selected path.
- [ ] Run GREEN plus current actor/overlap regressions:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-pose-bank verify-actor-meshlets verify-dual-actor-worker verify-demo-render-overlap
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_build_sourceboot_variant.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_animation_sweep.py
  ```

- [ ] After review, run one serialized linked target build and bounded Ymir sweep. The builder writes stable `artifact.json` and exact ELF/CUE/ISO hashes under `build/saturn/variants/animation-sweep/`; the capture writes `docs/saturn/evidence/reports/animation-sweep-target-2026-08-05.json`. Leave the step unchecked if the target does not report all 209 IDs:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label animation-sweep --animation 1 --actors 0 --audio 0 --pipeline 4 --diagnostic-mode animation-sweep --jobs 1 --output build\saturn\variants\animation-sweep\artifact.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_animation_sweep.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --artifact build\saturn\variants\animation-sweep\artifact.json --startup-vblanks 600 --max-vblanks 4096 --output docs\saturn\evidence\reports\animation-sweep-target-2026-08-05.json
  ```

- [ ] Commit as `feat(saturn): evaluate all source-selected Mario poses`; review scratch ownership, Q16 math, source-frame fidelity, and no private animation clock. Retain the legacy generated header until Task 28 proves rollback is no longer needed.

### Task 11: Compile generic actor-family banks

**Lane:** actors/scenes. **Depends on:** Tasks 3, 4, and 7's shared actor parser/format. **Produces:** content-addressed immutable family-bank payloads referenced by the S64P root for every closure record, with explicit unsupported capability reports.

**Files:**

- Extend: `tools/saturn/actor_source.py`, `tools/saturn/compile_actor_bank.py`
- Create: `tools/saturn/test_generic_actor_bank.py`
- Create: `tools/saturn/test_full_game_actor_source.py`
- Extend: `src/port/saturn/gfx/saturn_actor_bank.h/.c`
- Generated build output: `build/saturn/packages/bob/1/actors/*`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Feature mask:** ANIMATED, SWITCH, BILLBOARD, ALPHA, TRANSLUCENT, SHADOW, PARENTED, HELD, MODEL_MUTATION, SURFACE, plus declared LOD/render-range and particle/effect records. Vertices stay joint-local and name `joint_id`; records use offsets, never published pointers.

- [ ] Add RED fixtures for rigid opaque, animated joint hierarchy, switch cases, billboard alpha, translucent, shadow, held/parented, model mutation, surface object, unsupported node, and source-hash drift.
- [ ] Compile every BOB closure family deterministically. Unsupported features remain named in the report and fail the complete-closure gate, but do not prevent supported family banks from being inspected.
- [ ] Prove the selected first runtime family is chosen by smallest supported closure capability/multiplicity, not a hard-coded Goomba branch. Preserve the Goomba spike as evidence only.
- [ ] Run GREEN:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_generic_actor_bank.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_full_game_actor_source.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-actor-banks SCENE_LEVEL=bob SCENE_AREA=1
  ```

- [ ] Commit as `feat(saturn): compile generic actor family banks`; review every unsupported classification, exact source provenance, and deterministic package hashes.

### Task 12: Compile the full audio catalog and scene bundles

**Lane:** audio. **Depends on:** Tasks 3, 4, and 6. **Produces:** versioned untracked global catalog `AUDIO.DAT`, full manifest, and bounded BOB/WF content-addressed residency payloads referenced by each S64P root.

**Files:**

- Create: `src/port/saturn/audio/saturn_audio_package.h`
- Create: `src/port/saturn/audio68k/audio_package.h/.c`
- Create: `tools/saturn/saturn_audio_package.py`
- Create: `tools/saturn/compile_saturn_audio.py`
- Create: `tools/saturn/test_compile_saturn_audio.py`
- Create: `tools/saturn/audio_residency_test.c`
- Create: `docs/saturn/evidence/reports/full-game-audio-package-2026-08-05.md`
- Modify: `Makefile.saturn.mk`, `.gitignore`, `CHANGELOG.md`

**Format:** big-endian `S64A`, version 1, 2,048-byte-aligned chunks, source/package SHA-256, and tables for all 35 sequence IDs, 38 banks/bank sets, instruments/percussion/key splits/tuning/ADSR/release, all 219 extracted AIFF sample records, stable SFX mappings, and per-scene resident bundles. `AUDIO.DAT` is a global CD catalog; an S64P `AUDIO_DEPENDENCIES` section binds the exact closure-selected chunk hashes and generation committed for that scene. Full input is about 7.54 MiB and cannot be resident in less than 480 KiB; CD package and scene residency are mandatory.

- [ ] Add RED tests for AIFF parsing/PCM8 conversion, loop/tuning/envelope preservation, duplicate/missing IDs, stale/missing assets, invalid sequence control flow, alignment/hash drift, S64P audio-dependency/global-catalog mismatch, wrong scene generation, exact resident-bundle overflow, active-generation eviction, and any post-boot request to clear the complete sound-RAM address space.
- [ ] Consume `sound/sequences.json`, `sound/sound_banks/*.json`, built/extracted `.m64`, and user-extracted AIFFs directly. Do not require a PC game build. Normalize only pointer/endianness hazards; preserve branch behavior.
- [ ] Emit explicit driver/mailbox/work/sample ownership spans and a replacement-generation load plan. Preparation may reuse only unowned or retired sample spans; it must prove that the currently committed generation remains readable until the MC68000 acknowledges the replacement commit. If both generations cannot coexist, preparation fails closed rather than clearing or overwriting active data.
- [ ] Generate untracked outputs:

  ```text
  build/saturn/audio/generated/AUDIO.DAT
  build/saturn/audio/generated/audio_manifest.json
  build/saturn/audio/generated/bob_audio_closure.json
  build/saturn/audio/generated/wf_audio_closure.json
  ```

- [ ] Run GREEN twice and prove identical hashes:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_compile_saturn_audio.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-saturn-audio verify-audio-residency
  ```

- [ ] Commit source/tools/manifests/evidence only as `feat(saturn): compile scene-resident SM64 audio`; review asset legality, untracked outputs, hashes, and fit calculations.

### Task 13: Generalize BSP/frustum/portal-window admission

**Lane:** optimization. **Depends on:** Tasks 4 and 5. **Produces:** scene-neutral rejection before transform/classify/lower.

**Files:**

- Create: `src/port/saturn/gfx/saturn_scene_admission.h/.c`
- Create: `tools/saturn/scene_admission_test.c`
- Create: `tools/saturn/portal_window_test.c`
- Generalize: `src/port/saturn/gfx/saturn_render_cluster.h`
- Generalize: `tools/saturn/static_bsp.py`, `tools/saturn/emit_bob_scene.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Interface:** Runtime accepts only a validated package section and immutable `sm64_saturn_render_view_t`; it returns ordered cluster references and telemetry. Package metadata carries bounds, BSP nodes, adjacency, conservative portal windows, mandatory flags, and LOD tiers.

```c
bool sm64_saturn_scene_admit(const sm64_saturn_scene_admission_view_t *scene,
                             const sm64_saturn_render_view_t *view,
                             sm64_saturn_scene_admission_output_t *output,
                             sm64_saturn_scene_admission_stats_t *stats);
```

- [x] RED cases: wholly outside frustum, behind camera, near-plane intersection, camera inside bounds, mandatory cluster, open/closed portal, cyclic adjacency, invalid node/ref/window, zero clusters, capacity exhaustion, and yaw/pitch views. Focused scene-admission, portal-window, render-cluster, and Z-Treme frustum suites cover these mutations; mandatory/coverage/containment/reserved-field cases are included in the final integration set.
- [x] Adapt the pinned Z-Treme frustum and existing BOB BSP/cluster paths through generic package views; exact prior-art ranges are recorded in the provenance section. Portal windows remain conservative—false-positive drawing is permitted, false-negative visible rejection is not.
- [x] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-scene-admission verify-portal-windows verify-render-clusters verify-ztreme-frustum
  ```

- [x] Commit as `perf(saturn): admit packaged scenes before transform`; review near-plane conservatism, traversal cycles, zero admission, and no level symbols in runtime. Final implementation/repair range is `33e06fb8..a888ff00`; independent rereview is SPEC PASS / QUALITY PASS, C0/I0/M0. Target/Ymir/manual/FPS evidence is intentionally not claimed here.

### Task 14: Publish generic immutable actor-instance snapshots

**Lane:** actors/scenes. **Depends on:** Tasks 3, 5, 8 (implemented or explicitly deferred), and 11. **Produces:** stable object identities and source-resolved render state after each authoritative tick.

**Files:**

- Create: `src/port/saturn/gfx/saturn_actor_instance.h/.c`
- Create: `src/port/saturn/gfx/saturn_geo_state_observer.h/.c`
- Create: `tools/saturn/actor_instance_snapshot_test.c`
- Create: `tools/saturn/test_actor_snapshot_source.py`
- Narrowly instrument: `src/game/rendering_graph_node.c`
- Modify: `src/port/saturn/gfx/saturn_render_snapshot.h/.c`
- Modify: `src/port/saturn/sourceboot/main.c`, `src/port/saturn/sourceboot/Makefile`, `Makefile.saturn.mk`, `CHANGELOG.md`

```c
typedef struct sm64_saturn_actor_instance_snapshot {
    uint32_t generation, scene_package_generation, instance_key;
    uint32_t actor_bank_id, actor_bank_hash_words[8];
    uint16_t family_id, model_id, parent_index, parent_node_ordinal;
    uint32_t feature_state;
    int32_t position_q16[3], scale_q16[3];
    int32_t held_offset_q16[3], draw_distance_q16;
    int32_t render_range_min_q16, render_range_max_q16;
    int16_t angle[3], animation_id, animation_frame;
    int32_t animation_accel, anim_state;
    int32_t effect_params_q16[4];
    uint32_t effect_lifetime;
    uint16_t opacity, render_range_state, billboard_state;
    uint16_t shadow_type, shadow_scale, shadow_solidity;
    uint16_t effect_kind, effect_flags;
    int8_t area_index;
    uint8_t active, render_active, switch_count, reserved;
    uint16_t switch_state[SM64_SATURN_ACTOR_MAX_SWITCHES];
} sm64_saturn_actor_instance_snapshot_t;

bool sm64_saturn_actor_instances_capture(sm64_saturn_actor_instance_snapshot_t *out,
                                         uint16_t capacity, uint32_t generation,
                                         uint16_t *count,
                                         sm64_saturn_actor_capture_telemetry_t *stats);
```

The original geo walk records already selected switch/render-range/billboard/shadow state by generated node ordinal. Resolve family from `(sharedChild, behavior)` against the generated registry and `gLoadedGraphNodes[]` on the master. Publish no source pointers. `instance_key` is object-pool slot plus a Saturn incarnation sidecar.

- [ ] RED cases: Mario, active ordinary object, `MODEL_NONE` controller, parent/held child and offset, switch state, opacity, render-active and render-range-min/max/draw-distance rejection, billboard, shadow, effect kind/params/lifetime, package/bank/hash identity, pool-slot reuse/incarnation, despawn between captures, stale generation, unknown family, and capacity overflow. Each typed field receives a source-observer fixture and a mutation proving the snapshot changes or fails closed; generic `feature_state` bits never substitute for source-owned values consumed at runtime. Assert the exact ABI size/alignment and that two maximum-live snapshot banks plus observer state fit the package-declared LWRAM budget.
- [ ] Hook `observer_begin_object/record_switch/end_object` only around already authoritative geo evaluation. Capture after `sourceboot_run_source_tick()` completes and before render-snapshot publication; observer code must not select cases or mutate gameplay.
- [ ] Run GREEN and source-neutrality gate:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-instance-snapshot verify-render-snapshot-bank
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_snapshot_source.py
  ```

- [ ] Commit as `feat(saturn): snapshot source-resolved actor instances`; review lifecycle identity, instrumentation side effects, overflow policy, and pointer-free publication.

### Task 15: Implement the bounded MC68000 sequence VM

**Lane:** audio. **Depends on:** Tasks 6 and 12. **Produces:** deterministic execution of original assembled M64 sequence semantics independent of SH-2 frame rate.

**Files:**

- Create: `src/port/saturn/audio68k/sequence_vm.h/.c`
- Create: `src/port/saturn/audio68k/sequence_player.h/.c`
- Create: `tools/saturn/audio_sequence_vm_test.c`
- Modify: `src/port/saturn/audio68k/audio_package.h/.c`
- Modify: `src/port/saturn/audio68k/Makefile`, `Makefile.saturn.mk`, `CHANGELOG.md`

**Contract:** Support the actual full package's player/channel/layer control flow, tempo/delay/variation state, instrument and percussion selection, note start/release, volume/pan/pitch, loops, calls/returns, and termination. Offline normalization may replace absolute/pointer/endian hazards with package offsets, but may not change branches or timing.

**Reference-code-first boundary:** Before implementation, inspect the pinned in-tree Project12x files named in Prior Art, record exact functions/ranges and material changes in provenance, and close-port their sequence/layer/note semantics. The interpreter itself is a Saturn-shaped rewrite only where the N64 pointer ABI, audio-task/RSP command stream, unbounded host assumptions, or MC68000/SCSP event boundary makes direct adaptation impossible; the task report must name which reason applies to each from-scratch module.

- [ ] Generate full opcode coverage from all 35 sequences and BOB/WF traces — blocked because the expanded seq00/full package bytes are absent. The available source-shape audit covers sequence/layer control flow and malformed offset, stack, unknown, non-progress, and budget cases only.
- [x] Implement the bounded interpreter slice: pointer-free sequence/layer state, source timing/tempo accumulation, bounded loops/calls/returns, and scalar note/control events with no SCSP writes. Full-content unsupported-opcode closure remains open.
- [ ] Differentially compare all package traces with a host reference interpreter — blocked until real package bytes exist; bounded fixture traces are covered by `verify-sequence-vm`.
- [x] Run GREEN for the bounded slice:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-sequence-vm verify-pcm68k-image
  ```

- [x] Commit bounded scaffold as `df95a107` with edge-semantics repairs `52d45d5e`, `211158ea`, `b004fe7b`; bounded-slice independent rereview is SPEC/QUALITY PASS, C0/I0/M0. No MC68000 image, SCSP, target, Ymir, manual, or full-content claim is made.

### Task 16: Batch generic actor jobs through a dedicated shared queue

**Lane:** actors/runtime. **Depends on:** Tasks 11, 13, and 14. **Produces:** one descriptor-owned job per admitted instance without destabilizing the proven eight-entry world graph.

**Design correction:** The existing world/phase graph has eight descriptors and one `uint8_t` dependency mask per descriptor. Complete BOB cannot honestly give every admitted instance a job through that graph. Preserve it unchanged and create a protocol-compatible actor-instance queue unless measured closure/capacity evidence later justifies a separately planned global graph ABI change.

**Files:**

- Create: `src/port/saturn/gfx/saturn_actor_instance_queue.h/.c`
- Create: `src/port/saturn/gfx/saturn_actor_batch.h/.c`
- Create: `tools/saturn/actor_instance_queue_test.c`
- Create: `tools/saturn/actor_batch_test.c`
- Create: `tools/saturn/test_actor_runtime_neutrality.py`
- Generalize: `src/port/saturn/gfx/saturn_actor_meshlets.h/.c`
- Integration-owner modify: `src/port/saturn/gfx/saturn_demo_render.c`, `src/port/saturn/gfx/saturn_render_snapshot.h`, `Makefile.saturn.mk`, `CHANGELOG.md`

```c
bool sm64_saturn_actor_meshlets_prepare_bank(
    const sm64_saturn_actor_bank_t *bank,
    const sm64_saturn_actor_instance_snapshot_t *instance,
    const sm64_saturn_render_view_t *view,
    sm64_saturn_actor_pose_work_t *pose_work,
    sm64_saturn_actor_meshlet_output_t *output,
    sm64_saturn_fast3d_profile_t *stats);
```

Each queue descriptor owns exactly one admitted instance and a disjoint claimant-lane output span; it performs early bounds, selected-pose evaluation, meshlet admission/classification, and terminal publication. Either SH-2 may claim it. Master final merge preserves source/painter order. Queue capacity and output storage derive from the validated scene manifest maximum, with a compile-time global ceiling and exact memory report.

- [x] RED cases for the infrastructure slice: zero instances, one/many instances, master/slave claiming, same-family batching, different materials, stale instance/bank/generation, duplicate claim, output overlap, output overflow, claimant failure, pool-incarnation mismatch, and quarantining only the failed instance. Exact 64-instance and 2,806/2,807 output-arena boundaries are covered. Concurrent target claiming remains open.
- [ ] Keep existing Mario functions as feature-off wrappers. `ACTOR_ADMIT/ACTOR_LOWER` world graph behavior remains unchanged until the integration cutover replaces the single Mario pair deliberately.
- [x] Run GREEN for the infrastructure slice:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-instance-queue verify-actor-batches verify-actor-meshlets verify-dual-actor-worker verify-render-overlap-integration
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_actor_runtime_neutrality.py
  ```

- [x] Commit infrastructure as `0549f7af` (`feat(saturn): add generic actor instance queue`) with repair `d4efe0e9` (`fix(saturn): bound actor runtime storage`). Repair rereview is SPEC PASS / QUALITY PASS, C0/I0/M0. The broader task remains unchecked: generic actor-meshlet preparation, production publication/drain/cutover, manifest drawable-count proof, target retirement race, target/Ymir/manual/FPS evidence, and final master merge remain open.

### Task 17: Implement timer-driven SCSP voices and allocation

**Lane:** audio. **Depends on:** Tasks 6, 12, and 15. **Produces:** frame-rate-independent notes, envelopes, priority stealing, and SCSP register control.

**Files:**

- Create: `src/port/saturn/audio68k/audio_engine.h/.c`
- Create: `src/port/saturn/audio68k/voice_allocator.h/.c`
- Create: `src/port/saturn/audio68k/desired_voice.h/.c`
- Create: `src/port/saturn/audio68k/slot_shadow.h/.c`
- Create: `src/port/saturn/audio68k/scsp_timer.h/.c`
- Create: `tools/saturn/audio_voice_allocator_test.c`
- Create: `tools/saturn/audio_slot_shadow_test.c`
- Create: `tools/saturn/audio_scsp_timer_test.c`
- Modify: `src/port/saturn/audio68k/scsp_pcm8.h/.c`, `pcm_voice.h/.c`, `main.c`, `Makefile`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Contract:** A bounded 20-note semantic allocator backed by the SCSP's 32 slots protects music-control/note classes from ordinary SFX. Timer IRQ/service cadence drives sequence ticks and ADSR independent of SH-2/game FPS. The MC68000 builds a compact internal desired-voice array from sequence/note/allocator state, compares it with an MC68000-owned slot shadow, and emits only required SCSP parameter/key transitions in deterministic order. This table is never a shared-memory ABI and is never constructed or published by an SH-2. Lowest-priority eligible SFX drops first with telemetry; protected music is never displaced by ordinary SFX.

**Reference-code-first boundary:** Close-port priority, note lifetime, ADSR/release, pitch/tuning, pan, and layer ownership semantics from the exact pinned in-tree Project12x functions inspected for this task. Adapt only the hardware execution boundary: N64 synthesis/task/RSP command production cannot drive SCSP slots, so register programming, timer service, slot allocation, and sound-RAM residency are Saturn-native modules informed by the pinned Yaul and PoneSound files. Record every inspected range, retained semantic, rewritten boundary, notice, and material change.

- [x] RED/GREEN infrastructure cases: timer cadence, start/release/key-off order, pitch/pan/volume/loop words, ADSR phases, protected music, priority/age stealing, slot exhaustion/drop telemetry, stalled timer, invalid residency, zero/minimal shadow writes, reassignment ordering, stale generation, and deterministic repeated traces. Full source envelope-table/package cases remain blocked.
- [x] Expand the four-voice proof into bounded 20-note allocator/32-slot shadow infrastructure without breaking the slot/pitch regression. Desired voices, shadows, and register ownership remain MC68000-local; SH-2 does not publish a native voice-state structure. `scsp_pcm8` remains the sole explicit MMIO executor.
- [x] Run GREEN for the bounded infrastructure and freestanding module:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-voice-allocator verify-audio-scsp-timer verify-scsp-pcm8 verify-pcm68k-image
  ```

- [x] Commit bounded infrastructure as `5b74081c` with repair `144b4aa6`; independent rereview is SPEC/QUALITY PASS, C0/I0/M0. Production heartbeat/MC68000 wiring, full package/residency, target image, tempo/Ymir/manual/FPS evidence remain open; tempo correctness remains a later target gate.

### Task 18: Close BOB rigid, opaque, platform, and collectible capabilities

**Lane:** actor capabilities. **Depends on:** Tasks 11, 14, and 16. **Produces:** zero unsupported records for the closure's rigid/static-transform/opaque/collectible classes.

**Files:**

- Extend: `tools/saturn/compile_actor_bank.py`
- Extend: `src/port/saturn/gfx/saturn_actor_bank.h/.c`, `saturn_actor_meshlets.c`, `saturn_actor_batch.c`
- Create: `tools/saturn/actor_capability_opaque_test.c`
- Create: `tools/saturn/test_bob_actor_capabilities.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

- [x] Query `closure.json` for required rigid/opaque/static-transform/platform/surface/collectible feature sets and make the test fail with exact unresolved family IDs. No source-level family whitelist is used; default opaque fails on eight exact IDs and rigid/static adds five existing unsupported geo families.
- [x] Implement generic capability class/runtime masks for transform, scale, material, surface, and lifecycle fields present in the actor ABI. Unknown runtime bits and missing required masks fail closed; source-owned surface/collectible fields remain unresolved rather than fabricated.
- [ ] Require multiplicity/capacity from generated maximum-live counts and prove each visible instance gets its own queue descriptor/output span — blocked by Task 14 registry/typed-field gaps and Task 16 production drain.
- [x] Run GREEN for the generic slice:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-capability-opaque verify-actor-instance-queue
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_actor_capabilities.py --class opaque
  ```

- [x] Commit generic slice as `a319583c`; independent review pending. BOB `complete_closure=false`, 13 unsupported records, and no target/Ymir/manual/FPS claim remain explicit.

### Task 19: Close BOB articulated, enemy, and boss capabilities

**Lane:** actor capabilities. **Depends on:** Tasks 10, 11, 14, 16, and 18. **Produces:** zero unsupported animated/switch/held/parented/model-mutation records for BOB.

**Files:**

- Extend: `tools/saturn/compile_actor_bank.py`, `src/port/saturn/gfx/saturn_actor_pose.c`, `saturn_actor_batch.c`
- Create: `tools/saturn/actor_capability_articulated_test.c`
- Extend: `tools/saturn/test_bob_actor_capabilities.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

- [ ] RED report must name every still-unsupported ANIMATED, SWITCH, PARENTED, HELD, MODEL_MUTATION, and LOD/render-range closure record, including behavior-spawned children and boss rewards.
- [ ] Reuse the same compact joint/channel evaluator as Mario. Consume source-selected animation/switch/parent state from immutable snapshots; do not port enemy behavior or advance animation in the renderer.
- [ ] Prove multiple same-family actors on both SH-2 claimant lanes, different animation frames, parent-before-child merge identity, held-object transforms, model changes, despawn/reward transition, zero visible, and stale parent failure.
- [ ] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-capability-articulated verify-actor-pose-bank verify-actor-instance-queue
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_actor_capabilities.py --class articulated
  ```

- [ ] Commit as `feat(saturn): close BOB articulated actor capabilities`; review source authority, parent/child ordering, scratch reuse, and no Mario/enemy duplicate evaluators.

### Task 20: Close BOB billboard, translucent, shadow, and effect capabilities

**Lane:** actor capabilities. **Depends on:** Tasks 13, 16, 18, and 19. **Produces:** zero unsupported visual-effect records in the generated BOB closure.

**Files:**

- Extend: actor compiler/bank/batch modules from Tasks 11/16
- Create: `src/port/saturn/gfx/saturn_actor_effect.h/.c`
- Create: `tools/saturn/actor_capability_effect_test.c`
- Create: `tools/saturn/actor_effect_order_test.c`
- Extend: `tools/saturn/test_bob_actor_capabilities.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

- [ ] RED report must name BILLBOARD, ALPHA, TRANSLUCENT, SHADOW, PARTICLE, PROJECTILE, DECAL, and reward/effect records still unsupported. Include capacity overflow and unknown feature-bit mutations.
- [ ] Implement generic camera-facing basis, alpha/translucent material classes, stable far-to-near bins, bounded shadow descriptors, and closure-declared particle/effect lifetimes. Preserve VDP2/VDP1 layer ownership and master painter merge.
- [ ] Test camera yaw/pitch, near-plane billboard, opaque/translucent interleave, equal-depth stability, shadow receiver/depth, effect expiration, projectile multiplicity, zero effect budget, and overflow fail-closed telemetry.
- [ ] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-actor-capability-effects verify-actor-effect-order verify-actor-batches
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_actor_capabilities.py --class effects
  ```

- [ ] Commit as `feat(saturn): close BOB actor effect capabilities`; review painter order, effect budgets, source lifetimes, and no family names in runtime.

### Task 21: Integrate bounded audio package boot and residency in sourceboot

**Lane:** audio plus integration spine. **Depends on:** Tasks 2, 5, 6, 9, 12, 15, and 17. **Produces:** feature-selectable semantic audio from source calls to the live MC68000/SCSP.

**Files:**

- Create: `src/port/saturn/sourceboot/source_audio_service.h/.c`
- Create: `src/port/saturn/sourceboot/source_audio_loader.h/.c`
- Create: `src/port/saturn/audio/saturn_sound_cpu.h/.c`
- Complete: `src/port/saturn/sourceboot/source_audio_semantics.c`
- Create: `tools/saturn/audio_sourceboot_contract_test.c`
- Create: `tools/saturn/audio_sound_cpu_boot_test.c`
- Modify: `src/port/saturn/sourceboot/main.c`, `source_cart.h/.c`, `Makefile`
- Modify: `src/port/saturn/soundtest/main.c`, `soundtest_boot.h/.c`
- Modify: `src/port/saturn/audio68k/main.c`, `Makefile.saturn.mk`, `CHANGELOG.md`

```c
bool sm64_saturn_audio_service_boot(const sm64_saturn_audio_boot_config_t *config);
bool sm64_saturn_audio_scene_prepare(uint16_t scene_audio_id);
bool sm64_saturn_audio_scene_commit(uint16_t scene_audio_id);
void sm64_saturn_audio_service_poll(void);
const sm64_saturn_audio_stats_t *sm64_saturn_audio_service_stats(void);
```

Use the canonical Task 2 `SATURN_FEATURE_SEMANTIC_AUDIO` switch; do not add a second alias that can drift. `0` links the silent rollback implementation. `1` links the semantic adapter/service/loader, protocol-v2 driver, and `AUDIO.DAT`, and records the package hash in target identity. Boot occurs after successful source-cart setup and before `thread5_game_loop(NULL)`.

- [ ] RED cases: feature-label drift, direct use of Yaul's warned `smpc_smc_sndoff_call`/`smpc_smc_sndon_call` outside the project wrapper, wrong SNDOFF/copy/SNDON ordering, missing barrier or bounded wait, READY without heartbeat advance, feature-on boot without 512-KiB mode, feature-on scene transition attempting a complete sound-RAM clear, writes into driver/mailbox/active-generation spans, missing/short CD read, package/hash/version mismatch, oversized BOB bundle, stale prepare/commit generation, active-block eviction, driver not READY, heartbeat stall, queue saturation, and duplicate public symbol definitions.
- [ ] Implement one project-owned sound-CPU wrapper around the pinned Yaul generic SMPC command boundary. Pre-stage CD data in SH-2-visible memory before SNDOFF; on cold boot or explicit recovery only, stop and wait, select 512-KiB mode, clear validated owned regions, copy/verify driver and initial package, initialize the mailbox, publish with an explicit compiler/bus barrier, restart, then wait within a fixed budget for READY plus heartbeat advance. No other production file may issue sound-CPU commands directly.
- [ ] Implement bounded scene-transition loading without stopping the sound CPU or clearing all sound RAM. Ordinary gameplay frames may enqueue/poll but may not issue unbounded CD reads. Prepare writes only package-plan-approved unowned spans; commit atomically switches the package generation; retirement occurs only after the MC68000 acknowledgement. Audio failure mutes, increments a named fault, and returns without blocking simulation/rendering.
- [ ] Require audio package residency and scene package generation to commit atomically from the master; do not use render queue/DMA/timer ownership. CPU copy remains mandatory correctness behavior. Any SCU-DMA path is independently switchable and stays disabled until a target test proves sound-RAM destination support, cache/barrier correctness, bounded transfer time, and no collision with renderer DMA ownership.
- [ ] Run GREEN host/linked-image model checks only; no target or audible claim is made here. Task 23 owns the first exact audio target build/capture after Task 22 reseals the final BOB root:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-sound-cpu-boot verify-audio-sourceboot verify-audio-residency verify-pcm68k-image
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_feature_identity.py
  ```

- [ ] Commit as `feat(saturn): integrate semantic audio service`; review CDFS ownership, sound-CPU stop-window ordering, barriers, forbidden post-boot clears, bounded waits, feature-off rollback, package generation, DMA isolation, and fault isolation.

### Task 22: Prove complete BOB visual/dynamic closure

**Lane:** integration spine. **Depends on:** Tasks 2--5, 7, 10--21. **Produces:** the final resealed BOB S64P root and all closure-required dynamic families/effects enabled through the generic actor path.

**Files:**

- Create: `tools/saturn/test_bob_dynamic_closure.py`
- Create: `tools/saturn/gen_bob_replay_manifest.py`
- Create: `tools/saturn/test_bob_replay_manifest.py`
- Create: `tools/saturn/test_seal_scene_package.py`
- Create: `tools/saturn/test_no_level_specific_runtime.py`
- Create: `tools/saturn/launch_sourceboot_variant.py`
- Create: `tools/saturn/test_launch_sourceboot_variant.py`
- Create: `docs/saturn/evidence/reports/bob-dynamic-closure-v1.md`
- Integration-owner modify: `src/port/saturn/gfx/saturn_demo_render.c`, `saturn_render_snapshot.h`, `src/port/saturn/sourceboot/main.c`, `Makefile`, `Makefile.saturn.mk`
- Modify: `tools/saturn/capture_sourceboot_throughput.py`, `CHANGELOG.md`

**Acceptance:** Task 22 invokes the generic linker only after final Task 7 animation, Task 11/18--20 actor, and Task 12 audio payload hashes/byte counts/scratch limits exist. It writes `build/saturn/packages/bob/1/final/scene-package.{bin,json,h}`, rejects every provisional/missing/stale payload, and binds the root/dependency-set hashes into Task 2 identity. With `SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1`, `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1`, and semantic audio still `0`, the generated closure must report `unresolved_edge_count == 0` and `unsupported_required_capability_count == 0`. A closure-derived replay manifest spans BOB acts and trigger routes; it is regenerated from stable family/effect activation records rather than a prose enemy list. Every admitted visible instance has one actor-queue descriptor, exact-generation package/family/instance identity, and a terminal result or named quarantine. Model-less controllers remain source objects but emit no draw job.

- [ ] Add RED closure/runtime tests by deleting one spawned child, actor bank, feature implementation, maximum-instance record, output span, replay activation record, and source hash. Add source mutations introducing `bob|goomba|king_bobomb|chain_chomp` branches in shared runtime.
- [ ] Link/reseal the final BOB root from exact world plus final actor/animation/audio payload manifests. RED rejects a provisional tag, pre-Task-7/11/12 hash, missing payload, changed byte count/scratch limit, and nondeterministic dependency ordering. Replace the enabled path's single Mario job pair with the generated instance list/queue while preserving terrain world jobs, final master merge, current controls/camera, and feature-off rollback.
- [ ] Run the complete actor/scene host gate and one serial linked candidate build. Inspect identity, package hashes, HWRAM/LWRAM/cart capacity, P2 queue records, output spans, and native-math gate before Ymir. Stable output is `build/saturn/variants/bob-visual-closure/artifact.json`:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-bob-dynamic-closure verify-scene-package-runtime verify-scene-residency verify-actor-instance-snapshot verify-actor-instance-queue verify-actor-capability-opaque verify-actor-capability-articulated verify-actor-capability-effects verify-render-native-math verify-render-native-math-mutation
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_seal_scene_package.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 seal-scene-package SCENE_LEVEL=bob SCENE_AREA=1 SCENE_OUTPUT_VARIANT=final
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label bob-visual-closure --animation 1 --actors 1 --audio 0 --pipeline 4 --diagnostic-mode none --jobs 1 --output build\saturn\variants\bob-visual-closure\artifact.json
  ```

- [ ] Run the bounded closure-derived BOB replay set. The aggregate capture must account for every required drawable family/effect in the generated closure as either an observed exact-generation terminal actor job or an explicit model-less/source-inactive record whose generic capability was target-linked and host-proven; no ordinary enemy, boss, hazard, platform, collectible, spawned reward/projectile, or required effect may disappear into an unreported "representative" subset. Require non-sky terrain, Mario, zero ordinary quarantine/stale generation, and stable publication. Then launch the same artifact through the profile-managed desktop path for a short manual visual/controls check; record FPS but do not gate on it:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_throughput.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --game build\saturn\variants\bob-visual-closure\sm64-saturn-sourceboot-e2.cue --elf build\saturn\variants\bob-visual-closure\sm64-saturn-sourceboot-e2.elf --replay-manifest build\saturn\packages\bob\1\bob-replay-manifest.json --startup-vblanks 600 --max-vblanks 1200 --presentation-events 8 --output docs\saturn\evidence\reports\bob-visual-closure-target-2026-08-05.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\launch_sourceboot_variant.py --artifact build\saturn\variants\bob-visual-closure\artifact.json --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-sdl3\Release\ymir-sdl3.exe --profile D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile --launch --monitor-seconds 20 --output build\saturn\ymir-desktop-launches\bob-visual-closure-20260805.json
  ```
- [ ] Commit as `feat(saturn): activate complete generated BOB actors`; review the whole integration diff, exact closure report, no runtime whitelist, and manual limitations.

### Task 23: Prove audible BOB music/SFX semantic closure

**Lane:** audio plus integration spine. **Depends on:** Tasks 3, 6, 9, 12, 15, 17, 21, and 22. **Produces:** full BOB music/SFX behavior from original calls with independent 68K timing.

**Files:**

- Create: `tools/saturn/capture_sourceboot_audio.py`
- Create: `tools/saturn/test_capture_sourceboot_audio.py`
- Create: `tools/saturn/test_bob_audio_closure.py`
- Create: `docs/saturn/evidence/reports/bob-audio-semantics-2026-08-05.md`
- Modify integration only as evidence requires: source audio service/semantics, sourceboot Makefile/main, `Makefile.saturn.mk`, `CHANGELOG.md`

**Closure:** Level-grass music, Mario action/voice calls, every generated BOB enemy/object/hazard SFX, star/puzzle/race jingles used by BOB, fades, secondary interruption/resume, continuous fuse/environment sounds, stop-by-handle/source/bank, and bank masks. The generated audio closure, not this prose summary, is authoritative.

- [ ] RED rejects unresolved BOB sound IDs, unavailable sample/instrument/sequence dependencies, control-ring loss under SFX flood, incorrect fade/interruption trace, missing stop/refresh, stale audio generation, and capture identity mismatch.
- [ ] Enable `SATURN_FEATURE_SEMANTIC_AUDIO=1`. Automated capture must prove exact ELF/audio hashes, advancing heartbeat and 68K timer independent of presentation FPS, sequence/player state, voice activity, reserved control service, bounded SFX drops, zero protocol faults, and no render/simulation blocking.
- [ ] Build only after full audio host review. Run Ymir with the same DRAM/disc/profile path. Manual acceptance must confirm BOB music, movement/action/voice SFX, several ordinary enemy/object sounds, continuous sounds, a jingle, fade/interruption/resume, stereo pan, and that controls/camera remain normal. Perceived tempo must remain stable while VDP1 FPS varies. Stable artifact/report paths are `build/saturn/variants/bob-audio-closure/artifact.json` and `docs/saturn/evidence/reports/bob-audio-target-2026-08-05.json`.
- [ ] Run:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_bob_audio_closure.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_audio.py
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-audio-protocol-v2 verify-audio-policy verify-audio-sequence-vm verify-audio-voice-allocator verify-audio-residency verify-audio-sourceboot verify-pcm68k-image
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label bob-audio-closure --animation 1 --actors 1 --audio 1 --pipeline 4 --diagnostic-mode none --jobs 1 --output build\saturn\variants\bob-audio-closure\artifact.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_audio.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --artifact build\saturn\variants\bob-audio-closure\artifact.json --startup-vblanks 600 --max-vblanks 1800 --output docs\saturn\evidence\reports\bob-audio-target-2026-08-05.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\launch_sourceboot_variant.py --artifact build\saturn\variants\bob-audio-closure\artifact.json --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-sdl3\Release\ymir-sdl3.exe --profile D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile --launch --monitor-seconds 20 --output build\saturn\ymir-desktop-launches\bob-audio-closure-20260805.json
  ```

- [ ] Commit as `feat(saturn): close BOB music and SFX semantics`; independent review must separate automated facts from owner-heard evidence and keep retail-hardware audio open.

### Task 24: Reduce command, Gouraud, sort, and repeated-memory work

**Lane:** optimization. **Depends on:** Tasks 13, 16, and 22. **Produces:** smaller/faster master finalization for the representative all-actor workload.

**Files:**

- Create: `src/port/saturn/gfx/saturn_material_batch.h/.c`
- Create: `src/port/saturn/gfx/saturn_draw_merge.h/.c`
- Create: `tools/saturn/material_batch_test.c`
- Create: `tools/saturn/draw_merge_test.c`
- Modify: `src/port/saturn/gfx/saturn_terrain_command_template.c`, `saturn_demo_render.c`, `saturn_fast3d_frontend.h`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

**Requirements:** Reuse resolved immutable command templates; share neutral/identical Gouraud tables only when all four corner colors match; stable-merge descriptor-local streams without insertion sort; batch compatible opaque family/material records; preserve translucent far-to-near/source-stable order; avoid copying records already resident in their claimant output lane.

- [ ] RED fixtures cover incompatible material/texture/CLUT/blend/cull state, identical/different Gouraud tables, opaque stable merge, translucent equal-depth stability, capacity overflow, and stale command identity.
- [ ] Add telemetry for input/output commands, Gouraud tables/bytes, records copied, sort comparisons, and finalization VBlanks/ticks. The representative fixture must show at least one strictly reduced work counter with identical command semantics; no percentage/FPS gate.
- [ ] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-material-batches verify-draw-merge verify-terrain-command-template verify-terrain-command-stream verify-demo-render-overlap
  ```

- [ ] Commit as `perf(saturn): reduce final draw construction work`; review exact VDP1 state equivalence, painter order, telemetry truthfulness, and memory bounds.

### Task 25: Improve opportunistic dual-SH2 job granularity

**Lane:** optimization. **Depends on:** Tasks 16 and 24. **Produces:** finer bounded world/actor work without fixed family-to-CPU ownership or full-frame joins.

**Files:**

- Create: `tools/saturn/render_job_balance_test.c`
- Modify: `src/port/saturn/gfx/saturn_render_job_graph.h/.c`, `saturn_render_job_runtime.h/.c`, `saturn_actor_instance_queue.h/.c`, `saturn_demo_render.c`
- Modify: `tools/saturn/render_overlap_integration_test.c`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

- [ ] RED models: master runs first, slave runs first, delayed slave, uneven clusters, many small actors, one heavy articulated actor, zero actor jobs, one failed descriptor, queue capacity, and generation wrap. Assert exact-once work and terminal merge, not a fixed CPU split.
- [ ] Split only when package-derived cluster/actor batch bounds justify it. Either SH-2 may claim any compatible READY world or actor work; one failure quarantines only its dependent/result. Master remains sole final ordering/lowering/publication owner.
- [ ] Record per-lane jobs, input items, output records, estimated/observed work, imbalance, idle polls, overlap window, and master finalization. No arbitrary uplift or 50/50 requirement.
- [ ] Run GREEN:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-render-job-balance verify-render-job-runtime verify-actor-instance-queue verify-render-overlap-integration verify-demo-render-overlap
  ```

- [ ] Commit as `perf(saturn): refine opportunistic SH2 render work`; review P2 coherency, scheduler progress, failure localization, and no family/CPU affinity.

### Task 26: Extend useful work across transfer and presentation fences

**Lane:** optimization/integration. **Depends on:** Tasks 5, 21, 24, and 25. **Produces:** useful source/package preparation while VDP1 command/Gouraud transfers are pending, without exposing partial banks.

**Files:**

- Modify: `src/port/saturn/gfx/saturn_vdp1_frame_bank.h/.c`, `saturn_gouraud_transfer.h/.c`
- Modify: `src/port/saturn/runtime/saturn_frame_pipeline.h/.c`
- Modify: `src/port/saturn/sourceboot/main.c`
- Modify: `tools/saturn/vdp1_transfer_pipeline_test.c`, `tools/saturn/frame_pipeline_test.c`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

- [ ] RED lifecycle cases: command completes before Gouraud and vice versa, delayed IRQ, transfer failure, stale late completion, bank reuse attempt, package transition during transfer, source tick during transfer, audio poll during transfer, and partial publication.
- [ ] Keep exact bank/generation tickets. Queue CPU-DMAC commands and SCU-DMA Gouraud work without immediate waits; check terminal fences only before reuse/publication. Permit bounded source tick, audio poll, or package preparation while pending, but never evict active package data or show BUILDING/partial VRAM.
- [ ] Run GREEN and one post-review serial target candidate/capture. Record transfer/terminal waits and useful overlapping actions; no FPS threshold.

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-vdp1-frame-bank verify-vdp1-transfer-pipeline verify-gouraud-transfer verify-frame-pipeline verify-render-overlap-integration
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label transfer-overlap --animation 1 --actors 1 --audio 1 --pipeline 4 --diagnostic-mode none --jobs 1 --output build\saturn\variants\transfer-overlap\artifact.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_throughput.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --game build\saturn\variants\transfer-overlap\sm64-saturn-sourceboot-e2.cue --elf build\saturn\variants\transfer-overlap\sm64-saturn-sourceboot-e2.elf --startup-vblanks 600 --max-vblanks 1200 --presentation-events 8 --output docs\saturn\evidence\reports\transfer-overlap-target-2026-08-05.json
  ```

- [ ] Commit as `perf(saturn): overlap useful work with VDP1 transfer`; review VRAM bank lifetime, package/audio ownership, late completion, and exact publication acknowledgement.

### Task 27: Prove Whomp's Fortress generation and package transition

**Lane:** portability/integration. **Depends on:** Tasks 3--5, 11--17, and 21--26. **Produces:** second-level closure, validate, load/unload, and generic-runtime READY proof.

**Files:**

- Create: `tools/saturn/test_wf_scene_closure.py`
- Create: `tools/saturn/test_wf_package_load.py`
- Create: `tools/saturn/capture_scene_transition.py`
- Create: `tools/saturn/test_capture_scene_transition.py`
- Extend: `tools/saturn/test_no_level_specific_runtime.py`
- Modify: `Makefile.saturn.mk`, `CHANGELOG.md`

Use `levels/wf/areas/1` through the identical CLI/schema/runtime. Its moving platforms, Thwomps, Piranha Plants, small Whomps/Whomp King, tower, projectiles/effects, macro collectibles, act variants, and different music/banks are inputs—not permission for level-specific branches.

- [ ] RED on missing WF spawn edge, generic unsupported capability, stale/oversized section, BOB-to-WF active-package eviction, audio voice still referencing an evicted block, and `bob|wf|whomp` appearing in shared runtime/render/audio modules. Capture RED also rejects a host-only transition, wrong diagnostic identity, mismatched root/payload hash, skipped READY, stale render/voice generation, fault/quarantine, or absent rollback.
- [ ] Generate deterministic closure and all WF payloads, then link/reseal `build/saturn/packages/wf/1/final/` through the same generic command used for BOB. Resolve only generic missing feature classes. Exercise BOB prepare/commit, render retirement, audio fade/voice retirement, unload, WF prepare/commit, generic runtime READY, then rollback.
- [ ] Add and run:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 compile-scene-closure compile-actor-banks compile-saturn-audio seal-scene-package verify-wf-scene-package verify-scene-package-portability SCENE_LEVEL=wf SCENE_AREA=1 SCENE_OUTPUT_VARIANT=final
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_no_level_specific_runtime.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_scene_transition.py
  ```

- [ ] After review, build a linked non-promotable `scene-transition` probe and capture the target sequence `BOB READY -> render/voice retirement -> WF PREPARE -> WF READY -> BOB PREPARE -> BOB READY`. Bind exact BOB/WF root and actor/animation/audio payload hashes and require zero stale generation, partial commit, quarantine, or runtime/audio fault:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label wf-transition --animation 1 --actors 1 --audio 1 --pipeline 4 --diagnostic-mode scene-transition --jobs 1 --output build\saturn\variants\wf-transition\artifact.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_scene_transition.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --artifact build\saturn\variants\wf-transition\artifact.json --startup-vblanks 600 --max-vblanks 4096 --output docs\saturn\evidence\reports\wf-scene-transition-target-2026-08-05.json
  ```

- [ ] Commit as `feat(saturn): prove second-level package portability`; review deterministic hashes, transition generations, named unresolved capabilities, and no alternate frame loop/renderer/evaluator/audio backend. Full WF playability is not required.

### Task 28: Build and capture the four-feature diagnostic matrix

**Lane:** integration/evidence. **Depends on:** Tasks 1--27 and clean per-task reviews. **Produces:** exact attribution of feature costs on identical BOB inputs.

**Authoritative matrix:**

| Build | Complete animation | Dynamic actors/effects | Semantic audio | Pipeline |
|---|---:|---:|---:|---:|
| Renderer rollback | 0 | 0 | 0 | 4 |
| Complete Mario | 1 | 0 | 0 | 4 |
| Mario + BOB closure | 1 | 1 | 0 | 4 |
| All features | 1 | 1 | 1 | 4 |

Pipeline 2/3 remain renderer diagnostics and are not completeness-matrix members.
The renderer-only row is the current source tree with completeness features
disabled, not a claim of byte identity with the archived A9A artifact. Task 28
also cites the exact accepted A9A hashes as the historical 4--6 FPS rollback
evidence; source hashes keep those two baselines distinct.

**Files:**

- Create: `tools/saturn/build_feature_matrix.py`
- Create: `tools/saturn/test_build_feature_matrix.py`
- Create: `tools/saturn/inspect_feature_matrix.py`
- Create: `tools/saturn/test_inspect_feature_matrix.py`
- Create: `tools/saturn/capture_feature_matrix.py`
- Create: `tools/saturn/test_capture_feature_matrix.py`
- Create: `docs/saturn/evidence/reports/full-game-feature-matrix-2026-08-05.md`
- Modify: `Makefile.saturn.mk`; build/capture tools only as reviewed defects require; update `CHANGELOG.md` for behavior changes.

- [ ] Add `verify-full-game-completeness-host` as the single aggregate of tools, runtime contracts, scene/package/residency, complete animation, actor snapshot/queue/capabilities, audio protocol/package/policy/VM/voices/residency, render graph/overlap/transfers, coherency, memory map, native-math, and every associated mutation target. A test deletes one constituent prerequisite and proves the aggregate fails. Run the complete host suite serially and record its stable log; one narrow green gate never substitutes for this command:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-full-game-completeness-host 2>&1 | Tee-Object docs\saturn\evidence\reports\full-game-completeness-host-2026-08-05.log
  ```
- [ ] Build all four tuples serially through `build_sourceboot_variant.py`, changing only the three feature values. Use identical ROM/source hashes, BOB route, live-input bootstrap, Q16 camera, cart profile, clipping/BSP/LOD, Pipeline 4, and `diagnostic_mode=0`. Never build two variants concurrently. `matrix.json` binds all stable artifact paths and hashes:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_feature_matrix.py --output-dir build\saturn\feature-matrix\2026-08-05 --jobs 1 --matrix-output build\saturn\feature-matrix\2026-08-05\matrix.json
  ```

- [ ] Inspect each ELF/CUE for exact identity/hashes, expected translation units, HWRAM/LWRAM/cart/sound-RAM bounds, no unresolved native math, descriptor/callback ownership, dual-SH2 P2 publication, CPU-DMAC/SCU-DMA paths, audio driver/package placement, and no duplicate audio symbols:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_inspect_feature_matrix.py
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\inspect_feature_matrix.py --matrix build\saturn\feature-matrix\2026-08-05\matrix.json --output docs\saturn\evidence\reports\full-game-feature-matrix-inspection-2026-08-05.json
  ```

- [ ] Automated Ymir capture for each exact artifact must bind target identity before telemetry and report identical source state plus presentation FPS, source ticks, world/actor jobs/items, commands/Gouraud/sort/copies, transfer waits, stale/quarantine/fault counters, audio heartbeat/timer/control/SFX/voices/drops, and package generations:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_feature_matrix.py --matrix build\saturn\feature-matrix\2026-08-05\matrix.json --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --startup-vblanks 600 --max-vblanks 1800 --presentation-events 12 --output docs\saturn\evidence\reports\full-game-feature-matrix-2026-08-05.json
  ```
- [ ] Commit source tools and evidence as `test(saturn): attribute complete feature matrix`; review measurement clock domains, exact artifact hashes, tuple isolation, and honest failed gates.

### Task 29: Recover all-features BOB to >=4.0 mean FPS and publish sprint evidence

**Lane:** final integration. **Depends on:** Task 28. **Produces:** accepted representative full-feature candidate and a clean handoff to the 12--15 FPS sprint.

**Files:**

- Modify: this plan and SDD ledger
- Modify: `STATE.md`, `ROADMAP.md`, `ARCHITECTURE.md`, `HOWTO.md`, `README.md`
- Modify: design decision status and all sprint evidence reports
- Modify: `CHANGELOG.md` if any final behavior fix is required

- [ ] If the all-features capture is below **4.0 mean presentation FPS** in the pinned setup, use Task 28 attribution to append a named evidence-driven recovery subtask under Task 24, 25, or 26 before editing code. It receives the normal implement/review loop. Do not lower the gate, disable required features, or perform an unreviewed grab-bag optimization.
- [ ] Re-run only the affected focused gate, then the complete all-features host/linked/capture gate against a new exact identity. Repeat evidence-driven recovery until the authoritative all-features build is at least 4.0 mean presentation FPS; 4--6 FPS remains the expected accepted band:

  ```powershell
  powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-full-game-completeness-host 2>&1 | Tee-Object docs\saturn\evidence\reports\full-game-completeness-host-final-2026-08-05.log
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\build_sourceboot_variant.py --label all-features-final --animation 1 --actors 1 --audio 1 --pipeline 4 --diagnostic-mode none --jobs 1 --output build\saturn\variants\all-features-final\artifact.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_throughput.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --game build\saturn\variants\all-features-final\sm64-saturn-sourceboot-e2.cue --elf build\saturn\variants\all-features-final\sm64-saturn-sourceboot-e2.elf --replay-manifest build\saturn\packages\bob\1\bob-replay-manifest.json --startup-vblanks 600 --max-vblanks 1800 --presentation-events 12 --output docs\saturn\evidence\reports\all-features-final-throughput-and-dynamic-closure-2026-08-05.json
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_audio.py --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe --ipl "D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin" --artifact build\saturn\variants\all-features-final\artifact.json --startup-vblanks 600 --max-vblanks 1800 --output docs\saturn\evidence\reports\all-features-final-audio-semantics-2026-08-05.json
  ```
- [ ] Require the final throughput/dynamic-closure report and final audio-semantic report to resolve the identical ELF/CUE/ISO, S64P root/dependency set, actor, animation, and audio hashes from `all-features-final/artifact.json`. Reapply every Task 22 accounting invariant and every Task 23 timer/control/voice/fault invariant. A post-Task-28 optimization cannot close on FPS alone.
- [ ] Manual all-features BOB acceptance with the profile-managed 32-Mbit DRAM cart must confirm:
  - controls and camera remain normal;
  - all reachable Mario animation states look coherent and the complete table remains available;
  - all BOB-required enemies, boss, objects, hazards, platforms, collectibles, effects, deaths/rewards, and act variants appear/behave from source logic;
  - BOB music/SFX, fades, jingles, continuous sounds, interruption/resume, and stereo behavior are audible and semantically plausible;
  - geometry/materials/painter order/package transitions remain stable;
  - visible cadence is coherent with the automated at-least-4.0-FPS gate and has no obvious long stalls.
- [ ] Launch exactly the captured all-features artifact through the established profile-managed desktop path; do not pass a bare CUE or rely on the most recent build directory:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\launch_sourceboot_variant.py --artifact build\saturn\variants\all-features-final\artifact.json --ymir D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-sdl3\Release\ymir-sdl3.exe --profile D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile --launch --monitor-seconds 20 --output build\saturn\ymir-desktop-launches\all-features-final-20260805.json
  ```
- [ ] Run the final most-capable whole-branch review over merge-base..HEAD, pointing it at every deferred/parked finding and open target/retail gate. One consolidated fix wave and one scoped re-review are permitted by the SDD skill.
- [ ] Reconcile every task checkbox and individual step immediately. Record commits, reports, review verdicts, target hashes, tests actually run, manual owner statements, design corrections, and every remaining open gate. Keep retail-hardware performance and the next 12--15 FPS sprint explicitly open.
- [ ] Commit final evidence as `docs(saturn): publish completeness sprint evidence`, then use `superpowers:finishing-a-development-branch`.

## Exact serialized target-build wrapper

Use the repository's preflight wrapper for host Make gates. For full sourceboot target builds, use this shape with the Task 28 feature tuple substituted. Never run two instances concurrently:

```powershell
$Worktree = 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\sh2-native-math-purge'
$Command = @'
export PATH=/mingw64/bin:/usr/bin:$PATH
export HOME=/d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge/.msys-home
export TMPDIR=$HOME/tmp
export TMP=$HOME/tmp
mkdir -p "$TMPDIR"
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
source ../../.yaul.env
export SHELL=/usr/bin/sh
make -C src/port/saturn/sourceboot -B -j1 \
  SATURN_DEMO_PATH=1 \
  SATURN_SOURCEBOOT_ROUTE_REPLAY=1 \
  SATURN_SOURCEBOOT_LIVE_INPUT=1 \
  SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 \
  SATURN_SOURCEBOOT_CAMERA_ROUTE=0 \
  SATURN_CAMERA_VARIANT=3 \
  SATURN_CART_MBIT=32 \
  SATURN_SOURCE_CART_STAGE_SECTORS=8 \
  SATURN_DEMO_HOT_PROMOTION=1 \
  SATURN_DEMO_NEAR_CLIP=1 \
  SATURN_DEMO_BSP_ORDER=1 \
  SATURN_DEMO_POLY_TIER=2 \
  SATURN_DEMO_FRAGMENT_MODE=0 \
  SATURN_RENDERER_PIPELINE=4 \
  SATURN_DIAGNOSTIC_MODE=0 \
  SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 \
  SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 \
  SATURN_FEATURE_SEMANTIC_AUDIO=1
'@
& "$Worktree\tools\saturn\with-msys-toolchain.ps1" `
  'C:\msys64\usr\bin\sh.exe' '--noprofile' '--norc' '-lc' $Command
```

`build_sourceboot_variant.py` is the stable front end to this wrapper: it supplies the exact identity values, refuses concurrent builds, copies the ELF/ISO/CUE into `build/saturn/variants/<label>/`, fixes the copied CUE's relative ISO reference, rehashes all three, and emits `artifact.json`. The wrapper must preflight and expose the SH/68K toolchain DLL closure on `PATH` so `msys-2.0.dll`, `msys-gcc_s-seh-1.dll`, `libgmp-10.dll`, `libmpfr-6.dll`, `libisl-23.dll`, and transitive runtime dependencies resolve from the audited toolchain. Do not launch bare `sh-elf-*` tools from a DLL-incomplete environment.

## Sprint completion criteria

The sprint is complete only when all 29 tasks and their individual steps are contemporaneously status-marked; every behavior commit has its changelog reasoning; every task has independent specification/quality review; the full host and linked target gates are green; the four diagnostic artifacts have exact identities; BOB visual and audio closure reports contain no unresolved required records; Whomp's Fortress reaches generic READY and rolls back on target; all 209 Mario IDs complete the production evaluator target sweep; the owner manually accepts controls/camera/visual/audio semantics; and the authoritative all-features BOB build measures at least 4.0 mean presentation FPS in the pinned setup. Retail-hardware performance, complete playability of every later level, and the 12--15 FPS target remain explicit follow-on work.
