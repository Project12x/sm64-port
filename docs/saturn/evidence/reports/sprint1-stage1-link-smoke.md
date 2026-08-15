# Sprint 1 / Task 10 Stage 1: Link Smoke + Margin Readback

- Date: 2026-08-14 (22:33–23:26 local)
- Worktree: `.worktrees/saturn-recovery`, branch `saturn/recovery`
- HEAD: `70ddf31c7f4a3da8f7a33717504a95c1b61ed324` (clean tracked tree at start)
- Scope: first full SH-2 build of the recovery branch. Target compile/link
  and margin readback only — no emulator launch, no owner gate, no music
  (music-less SFX bundle by design; SEQ_START finds index 0 and stays silent).

## Outcome

**LINK FAILED — HWRAM overflow.** Both fallback-ladder rungs sanctioned for
this stage were exercised; per the ladder the next rung (partial re-eviction)
is a reviewed code change, so the stop rule was invoked. Exact margins below.
No ELF, ISO, or CUE was produced; there are no artifact hashes to record.
Compilation itself is healthy: all ~230 SH-2 objects compiled, identity
sealed, and the link was reached on both attempts.

| Attempt | Pool | Sealed identity | Linker result |
| --- | --- | --- | --- |
| 1 | `SATURN_OBJECT_POOL_CAPACITY=240` (A9A value) | `id-925904aa56588949` | `region 'ram' overflowed by 61168 bytes` |
| 2 | `SATURN_OBJECT_POOL_CAPACITY=208` (fallback a) | `id-26f79a882b63d52d` | `region 'ram' overflowed by 41712 bytes` |

Linker messages (identical shape both attempts):

```
sh-elf-ld: ... section `.bss' will not fit in region `ram'
sh-elf-ld: HWRAM sections extend past the physical top of work RAM. Move bulk
           CPU-only state to LWRAM before evaluating the required heap margin.
sh-elf-ld: region `ram' overflowed by <N> bytes
```

## Margin arithmetic (from the linker maps)

HWRAM region `ram`: origin `0x06004000`, length `0x000FC000` → top `0x06100000`.

Pool 208 map (`id-26f79a882b63d52d`, map SHA-256
`89b635a3486ffc55c7a80fa4ef8e2583438a2eec65600f42e99337e1497cdff0`):

| Section | VMA | Size |
| --- | --- | --- |
| `.text` | `0x06004000` | `0x83D08` (539,912) |
| `.rodata` | `0x06087F90` | `0x1DFD` (7,677) |
| `.data` | `0x06089D90` | `0x857C` (34,172) |
| `.bss` | `0x06092320` | `0x77FD0` (491,472) |
| `.uncached` | load `0x0610A2F0` | `0xF78` (3,960) |

End of `.bss` = `0x0610A2F0`, i.e. `0xA2F0` (41,712) past the region top —
exactly the linker's overflow figure. Pool 240 map (`id-925904aa56588949`,
map SHA-256 `368bef4b422ab80f2576ed4c0bafdedbf972c9dc5d8c62d18576f29a643fa96c`)
differs only in `.bss` = `0x7CBD0`; the 240→208 cut recovered `0x4C00`
(19,456) bytes = 32 objects x 608 bytes.

**Relief still required at pool 208 for stage-1 success** (overflow plus the
`0x1F00` heap-margin gate): `41,712 + 7,936 = 49,648 bytes (0xC1F0)`.
At pool 240: `61,168 + 7,936 = 69,104 bytes (0x10DF0)`.

For the sprint's decision matrix, the remaining rungs from the Task 10 plan
are (b) return only the per-primitive scratch arrays to HWRAM (leave
`s_bob_hot_workarea` in LWRAM) and (c) keep audio service `.text` in the
cart. Both are code changes needing review; neither was attempted.

## Margin gate output (verbatim)

`verify-memory-map` (auto-picks newest ELF; none exists because the link
failed):

```
verify-memory-map: no built ELF under build/saturn/sourceboot/e2-bob-identity-*/obj/; build sourceboot first or set SOURCEBOOT_CANDIDATE_ELF=<path>
make: *** [Makefile.saturn.mk:311: verify-memory-map] Error 1
```

## Build invocation

Via `tools/saturn/with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`,
sourcing the machine-local `../../.yaul.env` then `unset COMPILER_PATH`
(BUILDING.md host-native gotcha), then:

```
make -f Makefile.saturn.mk -j1 sourceboot \
  SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=0 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=0 \
  SATURN_FEATURE_SEMANTIC_AUDIO=1 SATURN_RENDERER_PIPELINE=4 \
  SATURN_SOURCEBOOT_LEVEL_ID=9 SATURN_SOURCEBOOT_AREA_ID=1 SATURN_SOURCEBOOT_ROUTE_ID=0 \
  SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1 SATURN_SOURCEBOOT_LIVE_INPUT=1 \
  SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 SATURN_SOURCEBOOT_CAMERA_ROUTE=0 \
  SATURN_CAMERA_VARIANT=3 SATURN_ATAN2_VARIANT=2 SATURN_DIAGNOSTIC_MODE=0 \
  SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0 SATURN_CART_MBIT=32 SATURN_SOURCE_CART_STAGE_SECTORS=8 \
  SATURN_DEMO_VIEW_RADIUS=6000 SATURN_SLAVE_RENDER=1 SATURN_DEMO_POLY_TIER=2 \
  SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1 SATURN_DEMO_BSP_ORDER=1 \
  SATURN_DEMO_BSP_FRAGMENTS=0 SATURN_DEMO_FRAGMENT_MODE=0 SATURN_DEMO_BSP_FRAGMENT_FLAT=0 \
  SATURN_OBJECT_POOL_CAPACITY=240 \
  SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 \
  SATURN_FAST3D_Q16_TRACE=0
```

(second attempt identical except `SATURN_OBJECT_POOL_CAPACITY=208`).
Link-reaching build durations: pool 240 23:12:01–23:18:11 (6m10s, warm);
pool 208 23:18:49–23:26:09 (7m20s). Total session including fresh-tree
provisioning: ~53 minutes.

## Fresh-tree provisioning performed (first build of this worktree)

The recovery worktree had never met the toolchain; the donor's build state
masked several fresh-tree gaps. Everything below is untracked build state
except item 6.

1. `baserom.us.z64` copied read-only from the main checkout (gitignored).
2. `git submodule update --init third_party/libyaul` → pinned `6012f79f`.
3. Host tools: MSYS2 now ships g++ 16.1.0, which no longer compiles the
   bundled `tools/armips.cpp` (pre-C++17 code; missing `<cstdint>`). The
   donor's gitignored prebuilt `tools/*.exe` (built 2026-08-01 under the
   older compiler) were copied in rather than patching tracked source.
   The four extraction tools and `audiofile/libaudiofile.a` compiled fine.
4. Checkout-level US asset extraction (`extract_assets.py us`) — the
   sourceboot recursive root make runs `NOEXTRACT=1` and assumes the
   checkout PNGs already exist (they are gitignored; the donor had them).
5. SFX-bundle inputs generated per the sanctioned prerequisite path:
   root `pcm68k-image compile-saturn-audio` with
   `SATURN_AUDIO_SCENE_CLOSURE=build/saturn/packages/bob/1/closure.json`,
   then `compile_sourceboot_sfx_bundle.py` (music-less: no `--music-pcm`),
   yielding `build/saturn/audio/generated/sourceboot-sfx/{bob_sfx_manifest.json,
   bob_sfx_metadata.bin (1,220 B), bob_sfx_pcm.bin (208,056 B)}`.
6. **Transplant gap (needs owner decision):**
   `tools/saturn/manifests/sourceboot-bob-demo/audio-sourceboot-sfx-v1.json`
   is referenced by the tracked profile but was never committed in the donor
   (untracked there). Copied from the donor; it sits untracked here and must
   be committed to make the branch self-building.
7. Package generation 15 (pinned `SCENE_PACKAGE_GENERATION ?= 15`):
   donor's published `actors-v3-g15`/`scene-v3-g15` were tried first but
   failed validation as **stale for current inputs** (donor code has moved
   on to g35). Sanctioned response applied: stale generation dirs deleted
   under this worktree's `build/`, then generation 15 was published locally
   from current recovery inputs (`compile-actor-family-bundle`, and the
   pipeline's own `compile-actor-scene-package` publish-or-verify).
   The locally generated provisional `scene.s64p` was compared against the
   donor's provisional copy during provisioning.

## Tracked-file deviation (uncommitted, owner review required)

`tools/saturn/profiles/sourceboot-bob-demo-v1.json` — the identity bootstrap
(`bootstrap_sourceboot_identity_spec.py`) unconditionally requires the
Make-provided config to equal the profile's `release_config`. The
transplanted profile still pinned the donor's all-features crunch tuple
(`object_pool_capacity 208, features.complete_mario_animation 1,
features.dynamic_actor_closure 1`), which makes the sprint plan's mandated
stage-1 variable set unbuildable as-committed. The profile was reconciled to
the plan's tuple (features off; pool 240, then 208 for fallback a). The edit
is deliberately left uncommitted; it currently reads the last-attempted
tuple (208/0/0, semantic audio 1). Either commit a chosen tuple or
`git checkout --` the file when deciding the next step. Sprint tasks 1–9
never owned this reconciliation — the plan overlooked the equality gate.

## Failure-mode responses used (in order)

- Windows/env: `unset COMPILER_PATH` after sourcing `.yaul.env`
  (BUILDING.md documented gotcha; host gcc was otherwise handed the SH-2
  assembler and died on x86-64 SEH directives).
- SFX-bundle rule inputs missing → ran the prerequisite chain explicitly
  (sanctioned).
- Stale scene-package generation → deleted offending generation dirs under
  this worktree's `build/` and republished locally (sanctioned).
- HWRAM overflow at link → fallback (a) pool 208; still short; **stop rule
  invoked** before rung (b).

## What this stage proved / did not prove

Proved: the transplanted tree compiles end-to-end under the real SH-2
toolchain; identity sealing, package publication, semantic-audio bundle
generation, and the music-less SFX path all function; the failure is
isolated to HWRAM `.bss` capacity with exact numbers recorded above.

Not proved: link success, margin gate, boot, visuals, audio, input, FPS —
no claims are made about any of these.

## Stage 1b: HWRAM relief + linking margin-passing build (2026-08-14)

- Date: 2026-08-14 (23:38–23:56 local); build 23:38:26–23:55:45 (17m19s).
- Same invocation as stage 1's pool-208 attempt (identical 27-variable set,
  `SATURN_OBJECT_POOL_CAPACITY=208`), on top of two new commits:
  - `1b8b8239` `fix(build)`: profile reconciled to the R1 tuple
    (features.complete_mario_animation/dynamic_actor_closure 1→0; pool 208
    and semantic audio 1 were already committed) + the missing
    `audio-sourceboot-sfx-v1.json` manifest committed (descriptor metadata
    only, not gitignored — purely a missing commit).
  - `49370e31` `perf(render)`: three-way work-storage split (fallback
    rung b, refined). `s_bob_hot_workarea` (43,776 B) back to
    `.lwram_bss` (the 91f02ffd placement, aligned(16) kept); five
    actor-path-only scratch arrays evicted via the new
    `DEMO_ACTOR_WORK_CACHE` macro (`s_actor_queue_merge_ids` 2,576 B,
    `s_actor_slots` 1,288 B, `s_actor_texture_slots` 1,288 B,
    `s_actor_gouraud` 2,576 B, `s_actor_gouraud_addresses` 2,576 B =
    10,304 B). Each was verified actor-path-only by reading every use
    (only `demo_actor_queue_assemble_done`, `demo_reserve_mario_gouraud`,
    `demo_emit_mario`, `demo_emit_mario_range`; never the terrain path).
    The 18 terrain/primitive scratch arrays stay in HWRAM.
    `test_dual_sh2_work_storage_contract.py` retargeted to pin the split
    (4 tests OK).

### Outcome

**LINK SUCCEEDED — margin gate OK.** Sealed identity `id-29429bb2a7b03158`
(`build/saturn/sourceboot/e2-bob-identity-id-29429bb2a7b03158/`). No
emulator launch; no claims about boot, visuals, audio, input, or FPS.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `b25c8cf2097bd8fcf205e34f133bdf69db1b0b5c3404dbd7fab64e7b5c8a6b0c` |
| `sm64-saturn-sourceboot-e2.iso` | `33ba93beb92e498ca551ccf26d195cdd76149b64aba236282542ce73d89ff8d1` |
| `obj/sm64-saturn-sourceboot-e2.map` | `245196a6b5b2bf9c0eeee204166c6d2a19b13ecd44a3dbb2a6958b1256320db8` |

### Margin gate output (verbatim, `verify-memory-map` via make; it
auto-selected this build's ELF)

```
verify-memory-map: checking /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery/build/saturn/sourceboot/e2-bob-identity-id-29429bb2a7b03158/obj/sm64-saturn-sourceboot-e2.elf
verify: D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery\build\saturn\sourceboot\e2-bob-identity-id-29429bb2a7b03158\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FDF28
  hwram_remaining = 0x20D8 bytes (required >= 0x1F00)
  lwram_end       = 0x002F5D40
  lwram_remaining = 0xA2C0 bytes (floor >= 0x4000)
  RESULT          = OK
```

### Deltas vs the stage-1 pool-208 map (`id-26f79a882b63d52d`)

| Section | Stage 1 size | Stage 1b size | Delta |
| --- | --- | --- | --- |
| `.text` | `0x83D08` (539,912) | `0x83D08` | 0 |
| `.rodata` | `0x1DFD` (7,677) | `0x1DFD` | 0 |
| `.data` | `0x857C` (34,172) | `0x857C` | 0 |
| `.bss` | `0x77FD0` (491,472) | `0x6AC90` (437,392) | **−54,080** |
| `.uncached` | `0xF78` (3,960), spilled past top | `0xF78` at `0x060FCFB0` | 0 (now fits) |
| `.lwram_bss` | `0xD89D8` (887,256) | `0xE5D38` (941,368) | +54,112 |

The `.bss` shrink is exactly the eviction sum (43,776 + 10,304 = 54,080 B);
`.lwram_bss` grew 54,112 B (32 B of section-placement alignment). The
margin arithmetic closes: stage 1 needed 49,648 B measured at the `.bss`
end, but `___end` also counts the 3,960 B `.uncached` section that now
fits below the top, so the effective requirement was 53,608 B against
54,080 B recovered — hwram slack over the 0x1F00 gate is 0x1D8 (472 B).
This margin is thin: any committed-`.bss` growth ≥ 472 B reopens the
overflow at this configuration.

LWRAM floor: `lwram_remaining` 0xA2C0 (41,664 B) against the 0x4000
(16,384 B) floor — 25,280 B of headroom after absorbing the eviction.

### What stage 1b proved / did not prove

Proved: the recovery branch links at the mandated R1 stage-1 configuration
with the margin gate passing as a build output; the profile equality gate
and SFX manifest are now committed (self-building tree); the three-way
split is pinned by the work-storage contract test.

Not proved: boot, visuals, audio, input, FPS. The 4 FPS floor gate
(Task 11) decides whether the actor/workarea LWRAM placement is retained.

## Stage 2: owner music wired into the SFX bundle — R1 candidate CUE (2026-08-15)

- Date: 2026-08-15 (00:03–00:35 local). Worktree `.worktrees/saturn-recovery`,
  branch `saturn/recovery`; base HEAD `602844a8` (stage 1b evidence commit).
- Scope: commit the host M64 renderer, wire the owner's rendered BOB theme
  WAV into the SFX-bundle make rule, rebuild the R1 candidate. Same
  27-variable invocation as stage 1b (pool 208), via
  `with-msys-toolchain.ps1` → MSYS `sh --noprofile --norc -l`, sourcing
  `../../.yaul.env` then `unset COMPILER_PATH`. **No emulator launch** —
  Task 11 owns the owner gate; no claims about boot, visuals, audio,
  input, or FPS.

### Commits under test

| SHA | Subject |
| --- | --- |
| `a2008f1d` | `feat(audio)`: host M64-to-WAV renderer (`tools/saturn/render_m64_wav.py`; format logic only, zero Nintendo bytes — verified by inspection) |
| `55f1a2de` | `feat(audio)`: `SOURCEBOOT_MUSIC_WAV ?= bob_theme.us.wav` → `wav_to_pcm8.py --rate 8000` → `build/saturn/audio/bob_theme_8k.pcm8` → packager `--music-pcm/--music-rate`; parse-time warning + music-less degrade when the WAV is absent; `wav_to_pcm8.py` added to `SOURCEBOOT_GENERATOR_INPUTS` |

### Attempt 1: FAILED at `source-actor-family-bundle` (00:05:31–00:10:00)

`compile_actor_family_bundle.py --verify-publication` raised
"actor family bundle publication is stale for current inputs". Root cause
(exact, single field): the g15 report pins
`resource_inventory/package_class_source_bytes/audio` = 234,312 —
measured against the music-less bundle at stage-1 provisioning time. With
music packaged, the audio class measures 299,690 (+65,378 = PCM +65,169,
metadata +12, manifest +197). A scratch recompile against current inputs
showed only `actor-family-bundle.json` differed; `bob-area1-actors-v3.s64f`,
`bob-area1-actors-v3-dependency.json`, and `actor_bundle_capacity.h` were
byte-identical.

Sanctioned response (the stage-1 "stale scene-package generation"
failure mode): deleted `build/saturn/packages/bob/1/actors-v3-g15/`,
republished generation 15 from current inputs
(`compile_actor_family_bundle.py` publish mode), re-verified with
`--verify-publication` — PASS. `scene-v3-g15` was left untouched: its only
actor reference is the byte-identical `.s64f` payload. The attempt-1
sealed identity `id-f27956ab45266358` produced no ELF and is superseded.

### Attempt 2: BUILD SUCCEEDED (00:14:39–00:33:00, 18m21s)

Sealed identity **`id-b3aceeb28570230b`**
(`build/saturn/sourceboot/e2-bob-identity-id-b3aceeb28570230b/`), config
label `feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfgb3aceeb28570`.
Fresh identity → full ~250-object recompile. Only linker warning: the
usual benign RWX LOAD segment note.

| Artifact | SHA-256 |
| --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `6b33c1cc19c8a50281908e98dea012d81246d63acc12ef4b31fdd0f4210dc091` |
| `sm64-saturn-sourceboot-e2.iso` | `832d039acf48ed2a5f13b628a37594a40ce8865e51542afc69cfd31f61862a4a` |
| `sm64-saturn-sourceboot-e2.cue` (88 B; not identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` |
| `obj/sm64-saturn-sourceboot-e2.map` | `299e3622b54fbcce91017c138792a237f46ffabe95b4abb86e87ab02bdc37057` |
| `bob_sfx_metadata.bin` (1,232 B) | `b13869267e2b7af56cbfed8bb8943405ef501137dcb1378e92d1333fa84279a9` |
| `bob_sfx_pcm.bin` (273,225 B) | `b76e6cbc913aa3aed0600848def97abf7d806d83f5c4096840ed03a049601924` |

ISO grew 5,115,904 → 5,181,440 B (+65,536 = 32 sectors). Artifacts
preserved to `releases/2026-08-15_0033/id-b3aceeb28570230b/` (stage 1b's
were preserved to `releases/2026-08-15_0005/id-29429bb2a7b03158/` before
building, hashes re-verified against this doc).

### Margin gate output (verbatim; auto-selected THIS build's ELF)

```
verify-memory-map: checking /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery/build/saturn/sourceboot/e2-bob-identity-id-b3aceeb28570230b/obj/sm64-saturn-sourceboot-e2.elf
verify: D:\Code\RetroDev\sm64-saturn-port\sm64-port\.worktrees\saturn-recovery\build\saturn\sourceboot\e2-bob-identity-id-b3aceeb28570230b\obj\sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FDF28
  hwram_remaining = 0x20D8 bytes (required >= 0x1F00)
  lwram_end       = 0x002F5D40
  lwram_remaining = 0xA2C0 bytes (floor >= 0x4000)
  RESULT          = OK
```

### Margin delta vs stage 1b (`id-29429bb2a7b03158`): ZERO

Every RAM section is byte-identical in size to stage 1b — `.text`
`0x83D08`, `.rodata` `0x1DFD`, `.data` `0x857C`, `.bss` `0x6AC90`,
`.uncached` `0xF78`, `.lwram_bss` `0xE5D38`, `.lwram_actor_runtime`
`0x10000`, `.lwram_geo_traversal` `0xC00`; `___end`, `hwram_remaining`
(0x20D8), and `lwram_remaining` (0xA2C0) are unchanged. The music payload
landed entirely in `.cart_rodata`: `0x389B40` → `0x3999D0` (+65,168 B,
the `.incbin`-with-alignment growth of the bundle blobs). Well under the
1 KB flag threshold for RAM deltas — nothing to flag. HWRAM slack over
the 0x1F00 gate remains 0x1D8 (472 B), same thin-margin caveat as 1b.

### Bundle music evidence (`bob_sfx_manifest.json`, regenerated in-build)

- `music_sample_index` = **63** (nonzero; sample rows 0–62 are SFX,
  row 63 is the music), `music_sample_id` = `music/looped-pcm8`.
- Music row: `{"stable_id": "music/looped-pcm8", "offset": 240824,
  "sample_count": 65169, "rate": 8000, "default_volume": 15,
  "flags": 1}` — flags bit 0 is `SM64_SATURN_PCM_SAMPLE_LOOP`, the SCSP
  gapless-loop bit.
- Music PCM 65,169 B ≤ the 65,535 B SCSP sample cap; converted in-build
  by the new rule (`bob_theme_8k.pcm8`, 8,000 Hz, 8.1 s, from the
  32 kHz 8.15 s owner WAV).
- Sound RAM: 54 SFX mappings / 63 SFX sample rows = 208,056 B of SFX PCM
  from base 0x8000; music appended at 240,824; PCM ends at 305,993 of
  the 524,288 B sound RAM (491,520 B usable above the driver base) —
  218,295 B spare. `music_sequence_offset`/`bytes` = 0/0 (sequence VM
  retired; SEQ_START keys the looped sample directly).
- Total bundle PCM 273,225 B; metadata 1,232 B (+12 vs music-less: the
  appended sample row).

### What stage 2 proved / did not prove

Proved: the music-wired bundle builds, links, and passes the margin gate
with margins byte-identical to 1b; the packaged bundle carries the looped
owner music row with in-cap PCM; the actor-package staleness interaction
of a changed audio bundle is understood and its sanctioned repair
recorded. Not proved: boot, visuals, audible music, input, FPS — the R1
candidate CUE awaits the Task 11 Ymir observation.
