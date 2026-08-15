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
