# SM64 Saturn product-recovery handoff — 2026-08-14

## Read this first

This handoff is intentionally candid. The recent development process did not
serve the product goal well enough.

The product is a playable Super Mario 64 port for Sega Saturn. Over the last
two weeks, the branch accumulated **866 reachable commits** while the best
owner-accepted result remained the August 5 A9A BOB slice. The commit work was
not fictitious, and portions of it may be useful, but it did not produce a
current artifact that improves on that slice. The owner instead observed worse
Mario rendering, incorrect actor presentation, no audible game audio, and
roughly 1 FPS in at least one current run.

That is a process failure, not merely a collection of difficult bugs. We used
serious engineering effort to close proxy goals—formats, manifests, reviews,
mutation suites, provenance, and generalized interfaces—while repeatedly
deferring the only evidence that mattered: a current CUE showing the game.

**Historical statement as of 2026-08-14:** There was **no owner-accepted
current development CUE**. Do not read that time-scoped finding as current
status: the later W0 artifact `id-e8720d58595d9a62` is owner-accepted, while
the two-level presentation/product gate remains open. This handoff's warning
against protecting architecture because of sunk cost remains applicable.

## Owner goal that must not be reinterpreted

The next accepted artifact must be a normal game-path build, not a bespoke
showcase. It must provide:

- normal BOB selected by the source game;
- correctly scaled and animated Mario with source textures, correct fixed
  Gouraud, and correct front/back and painter ordering;
- a normally spawned, correctly placed and textured Bob-omb through a shared
  actor path;
- audible real music and at least one game-triggered SFX;
- working input, camera, collision, HUD, and simulation;
- no scene-wide failure for one unsupported object;
- at least the 4 FPS integration floor, then 6–10 FPS for presentation; and
- a minimum Whomp's Fortress proof in the same executable and through the same
  game, renderer, scene, and audio paths.

The owner stated this narrower correction repeatedly: make one Task 8 repair,
wire a normally spawned BOB enemy rather than an injected Cannon, boot Ymir
immediately, expand through the same generic path, integrate music and SFX,
and do no new formats or infrastructure sprints before the live gate. That
instruction was sound. The execution violated it by allowing prerequisites,
reviews, and generalized completeness to expand faster than live integration.

## Quantified development pattern

Current repository facts at this handoff:

- branch: `sh2/native-math-purge`;
- HEAD: `b2447f6715bf64d34be4391298f3746db919aa6f`;
- reachable commits dated since 2026-07-31: **866**;
- commit prefixes: **388 docs**, **289 fix**, **89 feat**, **44 test**,
  **33 perf**, and 23 other;
- commits dated 2026-08-06 through 2026-08-14, after the accepted baseline
  date: **474**;
- current status after this handoff update: **408 changed paths**—117 tracked
  modifications and 291 untracked paths;
- index: empty; nothing is staged.

Subject matches are non-exclusive, but they illustrate the allocation of
effort: 159 recent commits mention release/identity/manifest/reproduction/
closure work, 169 mention actor/bundle/registry/residency/scene-publication
work, 112 mention rendering/VDP/Gouraud/Mario/camera/frame work, and 68 mention
audio/sound/SCSP/PCM/sequence work.

The dense commit rate is itself evidence of a broken feedback loop. From
August 3 through August 6 there were 489 commits; August 10 alone had 121. A
human could not reasonably verify the assembled game at that cadence. The
process optimized for closing internal tasks and review findings rather than
maintaining a comprehensible, playable product lineage.

## What happened

### 1. A useful but constrained playable slice existed

The A9A BOB artifact was owner-accepted as a useful visual/control/performance
baseline. It ran at an owner-observed 4–6 FPS and a measured 5.294 mean FPS,
with acceptable Mario and BOB presentation for that slice. It was not a full
port: it lacked normal enemies, goals, and integrated audio, and it used too
much memory for straightforward expansion.

The correct response was to preserve that playable behavior while replacing
one constraint at a time. Instead, development moved to broad architecture
without keeping an accepted playable branch continuously alive.

### 2. Release and hermetic machinery ran ahead of the product

Large efforts built source closures, toolchain attestations, release identity,
manifest sealing, reproducible A/B builds, native-math audit contracts,
capacity reports, and guarded staging. These found real defects and produced
some robust tools. They were nevertheless scheduled before the executable had
normal actors, audio, a second level, or a stable current visual gate.

Reviews then found additional tool-binding, path-safety, CRLF, cleanup, and
TOCTOU defects. Each finding triggered another repair, rebuild, reseal, and
evidence cycle. The work improved the reliability of releasing an artifact
that was not yet the desired game. This was good engineering applied at the
wrong product stage.

### 3. Actor architecture expanded before one ordinary actor worked

The actor effort introduced or revised source closure discovery, identity
registries, S64B/S64F/S64P formats, family bundles, variant compilers, material
lowering, target parsers, queue/storage APIs, workspace ownership, texture
residency, generation publication, scene bundles, and render scheduling.

Many of these changes passed focused tests and independent reviews. They also
entered repeated repair rounds for parser semantics, closure provenance,
alignment, type ownership, address spans, Make prerequisites, stale sidecars,
memory regions, DMA staging, publication validation, and path safety. At one
point the supposedly supported real BOB family set compiled to zero usable
drawable variants; Cannon became the convenient textured proof even though it
did not prove the required ordinary Bob-omb path.

The decisive failure is simple: after all that work, no current artifact has
an owner-accepted normally spawned, correctly textured and placed Bob-omb.
The modules are therefore probationary, not completed architecture.

### 4. Rendering work lost the accepted visual result

The branch gained SH-2 math work, multiple rendering pipelines, overlap,
double buffering, worker queues, fixed Gouraud work, actor lowering, depth and
painter policies, and extensive diagnostics. Yet owner observation regressed:

- Mario became enormous in one build;
- Mario colors/material association became wrong or shared with Cannon;
- Mario read as flat colored despite claimed Gouraud work;
- front/back visibility and painter/occlusion order were incorrect;
- Bob-omb textures were wrong or absent;
- Bob-omb ground placement was wrong; and
- one current path ran around 1 FPS rather than the accepted 4–6 FPS band.

The process often answered these reports with source-policy tests, command
audits, or another architecture correction before creating and observing one
causal CUE. Some screenshots were captured before gameplay had fully rendered;
older or wrongly profiled builds were also shown as if current. This damaged
artifact trust and made visual debugging more expensive.

### 5. Audio infrastructure replaced audible audio as the goal

The project already had an owner-proven standalone MC68000/SCSP playback
driver. Later work added semantic audio APIs, event transport, scene audio
closures, SFXB metadata/PCM packaging, sequence packaging, a runtime sequence
VM, allocator/voice policy, telemetry, and multiple target variants.

But the source game still produced no audible music or SFX. Four direct
sourceboot-audio CUE experiments left the target driver's active guard at zero.
The latest semantic-audio candidate reached the sound CPU and ran its VM, but
started no notes. We spent hours debugging the unfinished target sequence VM
instead of first connecting the source game's semantic calls to the already
proven SCSP playback route.

The PS1 port comparison clarified the mistake: that port uses prerecorded XA
tracks for music and fixed SPU samples for SFX; it does not execute the N64
sequence VM on target. Saturn does not have to copy that exact storage method,
but it demonstrates the right product boundary. Preserve `play_music` and
`play_sound` semantics while choosing the smallest proven target playback
implementation. The semantic API, driver, and packaged PCM can be reused;
the runtime M64 VM is not entitled to block audible audio.

### 6. Recovery itself repeated the pattern

On August 13 the documentation correctly reset authority around the playable
product. Even then, recovery drifted into donor reconstruction, extractor and
toolchain compatibility, repeated current-tree package generations, source
policy proofs, and several audio diagnostics without reaching a new accepted
artifact.

An isolated historical donor at `d7b04d61` plus extractor compatibility work
did boot a BOB frame, but it was diagnostic only and linked no-op audio. The
current tree later built and booted, but owner-observed visual and audio gates
remained failed. The recovery rules were correct; enforcement was too weak.

## Concrete failure inventory

This is not exhaustive at the line-of-code level, but it records the recurring
failure classes that caused the repeated repair sprints.

| Area | Concrete failures encountered | Product consequence |
| --- | --- | --- |
| Hermetic/release | Windows command-line overflow from full-closure Git status; submodule CRLF normalization mismatch; schema owner rejected its own slash-bearing class; profile output names disagreed with `SH_PROGRAM`; provenance repeated the same broad status call; historical audit total blocked the required new measurement; absolute `.incbin` paths broke relocation; debug paths made A/B ELF differ; `sh-elf-gcc-nm` attestation omitted its delegated `sh-elf-nm`; cleanup had path/ancestor race concerns | Many long rebuild/reseal cycles; no new playable feature |
| Actor source/format | Model-binding arity, reached display-list provenance, tail-branch termination, `MODEL_NONE` poisoning, recursive/unknown macro handling, alignment/headroom, output type ownership, and scratch ownership repeatedly changed | Parser and format maturity increased, but no accepted ordinary Bob-omb appeared |
| Actor package/admission | Initial real family arithmetic exceeded fixed output/command/Gouraud budgets; actor shares and terrain ownership were ambiguous; stale sidecars and incomplete Make import prerequisites reused mismatched outputs; package/profile paths needed containment; generation publication and DMA staging required several safety repairs | Repeated design corrections and generation rebuilds before one live consumer |
| Actor runtime | Feature-on generic actor wrapper intentionally failed because production cutover was incomplete; one actor job failed and quarantined the render generation; later review found publication validation, stack-copy, HWRAM alias, and scene ownership defects | Normal game advanced only two source ticks in one smoke; live gate blocked |
| Rendering | Mario material/Gouraud policies changed across paths; actor and Mario resource association was not preserved in owner observation; painter/depth order and scale regressed; command-bank/scene-publication work repeatedly changed frame ownership | Current presentation became visually worse and slower than A9A |
| Audio | Historical donor linked explicit no-op stubs; four direct sourceboot variants left `_s_active` zero; semantic path later started MC68000/VM but no notes; current request validation reports target-only field corruption; experimental direct fallback has no CUE | Weeks of audio infrastructure without owner-audible game sound |
| Build/tooling | Windows app-alias Python failure; MSYS `/d/...` versus native path transport; stale generated packages; no-clobber publishers; historical donor missing generated PNG; historical extractor expected `python3` and extensionless `./tools/mio0` | Builds were slow, fragile, and often stopped before the live hypothesis could be tested |
| Evidence | Premature captures before the gameplay stage, stale/wrong-profile launches, and a stable tiny CUE whose hash did not distinguish ISO contents | Owner could not trust that a shown image represented the claimed source |

Several individual fixes above were correct. The repeated mistake was allowing
their existence to justify another repair cycle before the subsystem had
changed the game.

## Root process failures

These are the failures the next maintainer must treat as seriously as code
defects.

### Proxy-goal substitution

`source-complete`, review PASS, format validation, manifest equality, and clean
host tests repeatedly became de facto milestones. None demonstrated the game.
We measured what the development process could close, rather than what the
owner needed to present.

### Late integration

The first normal game consumer came after extensive design and validation.
The correct order was minimum consumer first, emulator immediately, then only
the infrastructure proven necessary by that consumer.

### Sunk-cost protection

Once a subsystem had plans, reports, tests, and reviews, further repairs were
treated as cheaper than bypassing it. This prolonged failing actor and audio
paths. Prior token and commit spend cannot be an acceptance criterion.

### Review-driven scope expansion

Independent review correctly found real correctness issues. The process then
allowed each issue to trigger another broad repair cycle before the subsystem
had product value. Safety findings should either receive one narrow fix or
cause the unproven subsystem to be removed from the immediate demo path.

### Too many simultaneous variables

Rendering, actor packaging, residency, scheduling, memory layout, audio,
release identity, and build tooling changed across overlapping periods. When a
CUE regressed, causal attribution was poor. This is the opposite of the
required one-change/one-CUE/one-observation loop.

### Tests for abstractions rather than observed failures

Mutation tests and source-spelling assertions were often thorough, but many
protected interfaces that had no accepted target consumer. They increased
maintenance and confidence without reducing product risk. A bad product goal
with excellent tests is still a bad development loop.

### Artifact identity and launch discipline failed

Stale artifacts, wrong profiles, and premature captures were used. The CUE
file itself is only 88 bytes and has the same SHA-256 in the A9A baseline and
many current builds, so a CUE hash alone does not identify the build. Every
launch must bind identity, ELF hash, ISO hash, CUE path, build time, BIOS, cart,
and profile before Ymir opens.

### Unsupported content failed too broadly

Incomplete actor coverage sometimes failed package construction or quarantined
the render generation/scene. For a port in progress, unsupported content must
skip at the smallest safe object or draw while Mario, terrain, simulation,
input, and audio continue.

### No enforced time or spend budget

The owner repeatedly asked for a runnable artifact, yet work continued for
hours or days without a live improvement. The current constitution's two
attempt/two-hour stop rule was added late and then not enforced consistently.

### Documentation became work rather than control

The branch has 388 documentation commits in two weeks. Some were necessary,
but the volume did not prevent stale claims, conflicting plans, or product
regression. Documentation must constrain the next live action; it must not
become a second implementation stream.

## Accountability

It would be dishonest to call the owner's time and money an uncomplicated
investment in necessary infrastructure. The opportunity cost is real. The
process consumed roughly the owner's reported 40 hours, about $250, and a
presentation/collaboration opportunity while failing to provide the promised
artifact.

It is also inaccurate to say all 866 commits are worthless. Some contain
target-proven math, memory, DMA, packaging, parser, and driver work. Their value
is conditional: each must be cherry-picked individually into a live product
lineage and retained only after a target observation. The current branch as a
unit has not earned adoption.

## What is safe to retain

### Proven or strong donors

- The immutable A9A artifact as the visual/control/performance oracle.
- Original SM64 gameplay, Mario state, objects, collision, camera, animation,
  level scripts, audio calls, and progression ownership.
- SH-2 native-math and cadence fixes with target evidence.
- Cartridge/linker placement and bounded address/DMA primitives with target
  evidence.
- The standalone owner-accepted MC68000/SCSP playback path.
- Source asset extraction and identity work where it feeds a live consumer.

### Useful only by narrow cherry-pick

- semantic `play_music`/`play_sound` integration;
- scene audio closure and SFXB PCM/metadata packaging;
- actor-bank parsing and material lowering for one actually used actor;
- family lookup needed by a normal source-spawned object;
- queue/storage code needed by that object's current frame;
- texture residency needed by its exact tiles/CLUT;
- generation publication needed to keep that live frame safe; and
- overlap/scheduling changes only after a current capture proves a bottleneck.

For each item, cherry-pick the smallest commit or manually transplant the
smallest code. Do not cherry-pick whole task ranges. Do not import their release
reports, broad mutation matrices, or unused abstractions merely to preserve
history.

### Deferred regardless of prior spend

- new actor or audio wire-format versions;
- all-actor BOB enumeration;
- generalized level/package frameworks beyond the minimum WF proof;
- runtime M64 sequence-VM completeness;
- hermetic A/B release reproduction and resealing;
- broad capacity/audit campaigns; and
- speculative renderer or scheduler redesign.

## Exact state at handoff

### Accepted rollback oracle

Path: `build/saturn/baselines/a9a-2026-08-05/`

- ELF SHA-256:
  `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
- ISO SHA-256:
  `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
- CUE SHA-256:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
- measured presentation cadence: 5.294 FPS
- status: `owner-accepted`, constrained historical baseline only

Do not rebuild, overwrite, or relabel it.

### Latest built current diagnostic

Path:
`build/saturn/sourceboot/e2-bob-identity-id-7deb747eb230b595/`

- effective-config identity: `id-7deb747eb230b595`
- features: complete Mario animation, dynamic actor closure, and semantic
  audio all enabled
- build time: approximately 2026-08-14 16:51–16:56 EDT
- ELF SHA-256:
  `3c83af8b49fd8f7f26ffa8ae2e4de5e2fe840cdda97bbe2272401aa51bbc7407`
- ISO SHA-256:
  `11028c300afd05e639801f53c8667373648452048c2a1ed5b2cdb96f0e87fb31`
- CUE SHA-256:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
- status: diagnostic only; not owner-accepted

The duplicate CUE hash is expected because the descriptor text is stable. Use
the ISO/ELF/identity tuple, not the CUE hash alone.

The latest working-session audio telemetry for this exact candidate was not
published as a standalone JSON report and must therefore be treated as
operator-session evidence, not release evidence. It recorded SCSP/MC68000
startup, `music_starts=2`, `vm_ticks=193`, `active=1`, but `notes=0`, with
`malformed=2`, `consume_fail=3`, `scsp_fail=0`, and final failure `0x0340`.
Current diagnostic code interprets `0x0340` as an invalid voice request with
velocity and zero-envelope fields corrupted or misread on target. This points
to a target ABI/layout/dataflow defect between the sequence VM event and voice
request, not failure to start the SCSP CPU.

### Uncommitted audio experiment after that build

The following dirty paths contain an experimental direct-music fallback and a
larger uncommitted audio rewrite:

- `src/port/saturn/audio68k/pcm_voice.c`
- `src/port/saturn/audio68k/pcm_voice.h`
- `tools/saturn/test_full_game_audio_source.py`
- `tools/saturn/pcm68k_model_test.c`

The fallback loops a packaged PCM sample through the existing driver instead
of waiting for the unfinished runtime sequence VM. Focused Python tests passed,
the host model passed, and MC68000 compilation/linking succeeded after removing
a compiler-helper division. **No sourceboot CUE was built with this fallback,
and no Ymir or owner audio observation exists.** It is a candidate diagnostic,
not working audio. The four files must be reviewed against the surrounding
uncommitted audio changes before any transplant; the diff from HEAD is not a
small isolated patch.

### Worktree warning

The worktree is a forensic donor with 408 changed paths and an empty index.
Do not run `git reset --hard`, broad checkout, broad `git add`, or cleanup.
Create a separate recovery worktree or make an explicit inventory before
moving anything. Generated package generations and superseded stages exist;
never assume they correspond to current source.

## Required recovery strategy

### Phase A — establish one trusted playable lineage

1. Preserve A9A unchanged.
2. Use the already-built A9A, historical-donor, and current diagnostic
   artifacts to choose the source lineage with the fewest known visual
   regressions. The historical donor `d7b04d61` plus donor-only extractor
   compatibility `5808cdbf` is diagnostic evidence, not yet accepted.
3. Do not spend a new build merely re-proving silent rendering. The **next new
   product build must also contain the BOB audio gate below**.
4. Manually compare Mario, terrain, input, camera, animation, and depth only in
   that audio-enabled build after gameplay is visibly rendered.
5. If it cannot meet the accepted baseline within two changes or two hours,
   stop and choose a smaller transplant. Do not repair its package architecture.

### Phase B — the next product build must contain real audio

The next behavior build must not be another silent renderer or another host-only
audio milestone.

1. Preserve the source game's semantic `play_music`/`play_sound` calls.
2. Reuse the proven MC68000/SCSP driver and current packaged PCM/metadata where
   their bytes are valid.
3. Bypass the unfinished runtime M64 VM for the first live music gate. Map the
   BOB source music selection to a predecoded/packaged PCM stream or loop, in
   the same spirit as the PS1 port's target-appropriate music backend. Playback
   must start from the game call, loop without periodic silence, and stop or
   replace when the semantic music state changes; an always-on diagnostic tone
   or compile-time direct fallback does not pass.
4. Map one ordinary game-triggered SFX to its packaged PCM sample through the
   same semantic boundary and prove it can sound while music remains serviced.
5. Build one uniquely identified CUE. Require target telemetry showing an
   active voice, then require the owner to hear music and the triggered SFX.
6. Audio failure must mute audio only; it may not stall simulation, blank the
   scene, or change Mario/terrain rendering.

This is not permission to create another audio format, sequencing engine, or
catalog pipeline. It is a bounded transplant of existing semantic and driver
work into the game.

### Phase C — restore one normal Bob-omb

1. Use the source-spawned `bhvBobomb` / `MODEL_BLACK_BOBOMB` record.
2. Cherry-pick only the actor-bank/material/residency/queue pieces needed by
   that record.
3. Skip every unsupported actor individually.
4. Observe texture identity, position/ground height, animation, occlusion, and
   continued Mario/terrain/audio operation in Ymir.
5. Do not accept an injected Cannon or an object-specific renderer branch as
   proof of the generic path.

### Phase D — performance and WF

Measure every retained live candidate. A regression below 4 FPS blocks the
change. Once BOB is visually and audibly accepted, add the smallest WF terrain/
collision entry through the same executable. Unsupported WF objects may skip
individually during the first proof.

## Operating contract for the next maintainer

For every behavior change, write down before editing:

1. the owner-visible or audible defect;
2. the exact baseline and current ISO/ELF/identity;
3. one causal hypothesis;
4. the smallest files allowed to change;
5. the earliest Ymir observation; and
6. the stop time.

Then follow this exact loop:

1. one focused regression test tied to the observed defect;
2. one bounded code change;
3. one unique build;
4. hash and name the build/profile before launch;
5. observe after the gameplay stage renders;
6. record image/audio/input/failure/FPS facts; and
7. keep or revert the change immediately.

Stop after two implementation attempts or two hours without a new live result.
At that stop, the only allowed decisions are revert, bypass, or smaller donor
transplant. A new plan, abstraction, repair round, audit, or generalized format
is not an allowed response.

Testing budget before the live observation:

- one focused defect regression;
- target compile/link; and
- only the safety checks needed to avoid corrupting the console state.

Broader suites and independent review occur after a useful live result and
before that result becomes the new accepted baseline. Do not spend another
night proving unused interfaces while the CUE is silent or visually wrong.

## Acceptance dashboard at handoff

| Product capability | Best evidence | Status |
| --- | --- | --- |
| A9A BOB terrain/Mario/input/camera | archived exact artifact, 5.294 FPS | historical owner-accepted baseline |
| Current Mario visual fidelity | owner observed color/Gouraud/order regressions | failed |
| Current normal Bob-omb | no accepted textured/placed normal actor | failed |
| Current real music | driver/VM telemetry but zero notes/audible output | failed |
| Current game-triggered SFX | no owner-audible current evidence | failed |
| Current performance | approximately 1 FPS in one owner run; no accepted current cadence | failed/unproven |
| Whomp's Fortress | no current generated/live proof | not started |
| Title/menu/file select | sourceboot bypasses retail path | not started |
| Release reproducibility | substantial historical evidence | deferred; not a product gate |

## Files that define truth

- `docs/saturn/PRODUCT_GOAL.md` — canonical product and acceptance goal
- `STATE.md` — current product state
- `ROADMAP.md` — capability milestones
- `docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md` —
  active recovery plan, subordinate to the product goal and this factual state
- `.superpowers/sdd/2026-08-13-mario-port-product-recovery/progress.md` —
  execution ledger
- `docs/saturn/evidence/reports/2026-08-13-bob-convergence-handoff.md` —
  prior donor and rendering/audio negative evidence
- repository-root `AGENTS.md` — durable Saturn product-gate constitution and
  two-attempt/two-hour stop rule

Historical plans and reports are research material. They do not authorize new
work, and their checked boxes do not mean the game works.

The constitution is the persistent memory intended to prevent recurrence, but
text alone is not enforcement. At each two-hour boundary the maintainer must
actually stop the running task, record the absent live result, and choose
revert/bypass/smaller transplant. Editing the plan to extend the task is itself
a violation unless the owner explicitly changes the product goal.

## Final instruction

The next deliverable is not another report, review PASS, source-complete task,
format version, or infrastructure sprint. It is a newly built CUE in which the
owner can see a non-regressed Mario and BOB, hear real music and a triggered
SFX, and identify the normal Bob-omb path. If existing architecture cannot
produce that promptly, stop protecting it and reuse only the smallest proven
pieces.
