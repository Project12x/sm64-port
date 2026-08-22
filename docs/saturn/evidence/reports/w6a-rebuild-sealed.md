# W6a rebuild — sealed closure, same binary, new identity tag

- Date: 2026-08-22, 06:59 → 07:18 local. Worktree `.worktrees/saturn-recovery`,
  branch `saturn/recovery`, HEAD **`996487b9`**, tree clean (`git diff` empty;
  only `.msys-home/` and `releases/` untracked).
- Task: rebuild the W6a product candidate from **committed** source so it
  carries a clean provenance closure, and confirm its cadence reproduces.
  No product source was changed; no emulator GUI was launched.

---

## 0. The one-paragraph version

**`provenance.closure_clean` is now `true`, the cadence reproduced to the last
digit, and the identity tag changed.** The rebuild seals
**`id-57fb7312058b7f2e`**, not W6a's `id-586944bb637a920f` — but the product is
the same binary: the two ELFs are **9,983,384 bytes each and differ in exactly
64 bytes**, one contiguous run at file offset `0x00083c04`, which is the
identity blob's own `source_hash` followed by its `effective_config_hash`.
`SOURCE.DAT` and the CUE are byte-identical. Cadence at 30 presentation events,
caches off, is **7.044534412955466 mean / 6.6666666666666670 1% low** — the same
IEEE double as W6a's, with the same 8.5172 mean VBlanks/frame and the same 0
cadence-unstable retries. The tag moved because `Makefile.saturn.mk` — a
`linker/build-recipe` **source-closure row** — was last written **17 minutes
after** W6a's product build finished and before the W6a commit, so W6a sealed a
closure that corresponds to no committed state. That is precisely what
`closure_clean: false` recorded, and section 3 eliminates every other candidate
by measurement.

---

## 1. Build

Full 27-variable product tuple, `SATURN_DIAGNOSTIC_MODE=0`,
`SATURN_OBJECT_POOL_CAPACITY=208`, via `tools/saturn/with-msys-toolchain.ps1`
→ MSYS `sh --noprofile --norc`, sourcing `../../.yaul.env` then
`unset COMPILER_PATH`, invoked from PowerShell (GNU Make under the Bash tool
resolves `TMP` to `C:\WINDOWS\` and host compiles fail):

```sh
make -f Makefile.saturn.mk -j12 sourceboot \
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
  SATURN_OBJECT_POOL_CAPACITY=208 \
  SATURN_CAMERA_IDLE_START_TICK=0 SATURN_CAMERA_IDLE_DISCOVERY=0 SATURN_CAMERA_RANGE_CAPTURE=0 \
  SATURN_FAST3D_Q16_TRACE=0
```

**07:01:10 → 07:09:17, wall clock 487 s, exit 0**, one attempt, no repairs
needed. `-j12` per AGENTS.md as relaxed by T2.18; T2.22's post-fix `-j12`
figure is 417 s, and this run's extra ~70 s is the from-scratch object tree
(the previous build's output directory was moved aside first, so all objects
were compiled fresh).

None of the three known repairs was required: the tracked profile
`tools/saturn/profiles/sourceboot-bob-demo-v1.json`
(`a562c98760a893a474092799ba3d52b6feb9312afadb28c967271bd8da1c8b9c`) already
carries `diagnostic_mode: 0` and `object_pool_capacity: 208`, matching the make
tuple, so no profile flip was made and none had to be reverted; generation 15
did not report stale, so no delete-and-republish was needed; and the
`releases/` copies preserve the `obj/` subdirectory the `--release-manifest`
binding requires. All tracked source was frozen before `sourceboot` was
invoked.

## 2. Identity and hashes

| | W6a candidate | this rebuild |
| :--- | :--- | :--- |
| identity tag | `id-586944bb637a920f` | **`id-57fb7312058b7f2e`** |
| identity label | …`cfg586944bb637a` | `feat001-pipe4-l9-a1-route0-replay1-live1-boot600-cam0v3-diag0-cart32-stage8-hot1-clip1-bsp1-poly2-frag0-cfg57fb7312058b` |
| `identity_sha256` | `c765fb1d5044e42c…aa419` | `d45cbc253c7a9dbf54943a30991a9c135448a64cc8eef7a809cd572dabf7b3c5` |
| `effective_config_sha256` | `586944bb637a920f…9f557` | `57fb7312058b7f2eea43d2584a3c59687de621d92945533612b001199135bb95` |
| `source_closure_sha256` | `d2f8abcb2945a1ff…fde5f` | `d150814458dc1274578d6a0422d88574429536499620d77c05837dd3eef00097` |
| `package_set_sha256` | `94014e46b8b08a4a…b6f05` | **identical** |
| `target_profile_sha256` | `0835691224115234…54db0` | **identical** |
| `toolchain_attestation_sha256` | `df57017415eb1a50…f749f50` | **identical** |
| ELF SHA-256 | `03e6f55e2667e7559c8c68e0862b099448a6bd41e9f46fe4f4c9308c93eca8d7` | `4a287b769d94978007840ca08d2a1ae594aa1e3a287e5cad2c3574891779b66b` |
| ELF size | 9,983,384 | **9,983,384** |
| ISO SHA-256 | `d956f7278e1fc84162d2fc70388694bb9ffa9c2f7ed978d650e7a941c9f3dce0` | `fc007092b2429ff9844dd34fb1b4874c4ddac97b6bccdb1250a62a38b1d68f6e` |
| ISO size | 5,177,344 | **5,177,344** |
| CUE SHA-256 (88 B, never identity-bearing) | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | **identical** |
| `SOURCE.DAT` SHA-256 | `531dbda73dd5e81b95808eff175e37c1cca5c1825e9008517f2d965fc1870daf` | **identical** |
| linker `.map` SHA-256 | `b93e37132afd0dc67288038ede550954404612cb56cbe8ded2096d6a765b8328` | `7f3ce24cac62a0816da9df78d17a22a0ab38a3b51f0247aaf65c2e5771671c81` |
| manifest SHA-256 | `e94d56837101ad616ca4b278727f36f7fdc0455dcf1b1ee9fc9119d2cf405ac0` | `2d66f95fd871abc3b2a8e74e2d39559cca50ff39cec9aa85fbb52d4ce51b2c57` |
| `provenance.git_revision` | `a583e9a9` (pre-W6a-commit) | **`996487b9`** |
| **`provenance.closure_clean`** | **`false`** | **`true`** |

**`closure_clean: true` is the point of this task and it is met.** All ten
package-class hashes, the target profile hash and the toolchain attestation
hash are unchanged; `source_closure_sha256` is the **only** identity input that
moved.

### The ELF difference is 64 bytes, and they are the identity's own hashes

A byte-for-byte comparison of the two ELFs:

```
sizes 9983384 9983384  (equal)
differing runs: 1
total differing bytes: 64
  offset 0x00083c04 len 64
```

```
old: d2f8abcb…fde5f  586944bb…9f557
new: d1508144…f00097  57fb7312…5bb95
```

That run is exactly `source_hash` (32 B) followed by `effective_config_hash`
(32 B) inside the 500-byte build-identity blob that
`src/port/saturn/platform/saturn_build_identity.c` compiles into the ELF.
**9,983,320 of 9,983,384 bytes are identical.** No compiled code or data
changed; only the self-referential closure bookkeeping did. This is the same
signature T2.22 documented when a closure-bearing makefile changes.

## 3. Why the tag differs — every other candidate eliminated

T2.22 established that makefiles and generator scripts are themselves
source-closure rows and that the closure hash is compiled into the ELF, so any
closure-row change necessarily reseals a new tag. The question is which row.

1. **The four commits after W6a's source commit are not the cause.**
   `git diff --name-only 68ce477e HEAD` is `CHANGELOG.md`, fifteen files under
   `docs/`, and `tools/saturn/capture_master_wait_cause.py`. Of the closure's
   **4,303** rows, **zero** are under `docs/` and **zero** end in `.md`; and
   `tools/saturn/capture_master_wait_cause.py` is not among the 65 `tools/`
   rows. None of those commits touches the closure.
2. **Exactly three closure rows were written between W6a's build and this
   one.** Scanning all 4,303 rows for an on-disk mtime in the window
   2026-08-21 21:01 (W6a's build finished) → 2026-08-22 07:01 (this build
   started) returns three files and no others:

   | row | class | mtime |
   | :--- | :--- | :--- |
   | `build/saturn/sourceboot/generated/saturn_quad_map.c` | generated-input | 2026-08-21 21:07:34 |
   | `build/saturn/sourceboot/generated/saturn_quad_map.h` | generated-input | 2026-08-21 21:07:34 |
   | `Makefile.saturn.mk` | linker/build-recipe | **2026-08-21 21:18:29** |

3. **The two quad-map rows are eliminated by measurement.**
   `tools/saturn/quad_map.py` writes unconditionally (it is not one of the
   eight generators T2.22 converted to write-if-changed), so `compile-quad-map`
   stamps a fresh mtime whether or not the bytes move — and W6a ran
   `verify-runtime-contracts`, which depends on `compile-quad-map`, after its
   product build. Running the same target again here at 07:17:06 rewrote both
   files and left them **byte-identical**
   (`.c` `55f5492129351f250cfebf34406268bccc5a74a41fcc28902723562a8e5550fe`,
   `.h` `1d549c17b3f6d130dc38d770249f80ecc2f2e1a6b2b4f0daffbd4a381ea005b3`,
   before and after). Fresh mtime, unchanged content: not a closure change.
4. **That leaves `Makefile.saturn.mk`,** whose last write is **17 minutes after
   W6a's product build finished** (build 20:54–21:01; file 21:18:29) and before
   the W6a commit. Its entire W6a diff (`a583e9a9` → `68ce477e`, three hunks)
   lives inside the `verify-softfp-bitexact` **host-gate** recipe — the
   `SOFTFP_FAST_DIR`/`SOFTFP_FAST_FUNCS` pair, the MSYS `mkdir -p` path fix and
   its comment block, and the per-routine source/include selection loop. None
   of it compiles anything the target links, which is exactly why the ELF can
   differ in nothing but its identity blob while the closure hash moves.
   By contrast `src/port/saturn/sourceboot/Makefile` — the other makefile W6a
   changed, and the one that actually wires `SOFTFP_FAST_OBJS` into the link —
   has mtime 20:51:50, *before* the build, and needed no post-build edit.
5. **The exact sealed bytes are not recoverable.** Substituting the pristine
   `a583e9a9` `Makefile.saturn.mk` hash into the current closure text yields
   `f744a18b…`, not W6a's `d2f8abcb…`, so W6a's build did not use the
   unmodified file either: it sealed an **intermediate** edit that exists in no
   commit. W6a's `saturn-source-closure-v2.json` was overwritten by this
   rebuild and only its SHA-256 survives (inside the W6a ELF), so the
   intermediate cannot be reconstructed row by row. It does not need to be —
   the ELF comparison already proves the difference is confined to bookkeeping.

**Conclusion.** `id-586944bb637a920f` was sealed against a working tree that no
commit reproduces. `id-57fb7312058b7f2e` is the same product built from
committed source with an honest closure, and it is the artifact any promotion
should use.

## 4. Cadence — reproduced exactly

`capture_sourceboot_throughput.py`, 30 presentation events (29 intervals),
`--warmup-ticks 30`, `--startup-vblanks 4096 --max-vblanks 3600
--timeout 1800`, **caches off** (`sh2_cache_emulation: false`), release-manifest
bound, absolute CUE. Ymir headless `build-agent2`
(`3156bea3f416ff98d061a2cdc81461dfbeb26da6674dbba211868d0d1677f98b`), BIOS
`sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`. On-target
identity probe **MATCH**
(`d88b17a9fd1c42e57e3a0c76cd790a506b6a7e778f0a37dfc63f1850850d23b5` — the same
executable-window digest as the baseline and W6a). Route span 30 → 59, status
`complete`.

| | accepted baseline `id-e8720d58595d9a62` | W6a `id-586944bb637a920f` | **rebuild `id-57fb7312058b7f2e`** |
| :--- | ---: | ---: | ---: |
| FPS mean | 6.6923 | 7.044534412955466 | **7.044534412955466** |
| FPS 1% low | 6.0 | 6.666666666666667 | **6.666666666666667** |
| FPS median | 6.6667 | 6.666666666666667 | **6.666666666666667** |
| mean VBlanks/frame | 8.9655 | 8.5172 | **8.5172413793103450** |
| VBlank delta range | 8–9 | 8–9 | **8–9** |
| cadence-unstable retries | 1 | 0 | **0** |

**The rebuild reproduces W6a's figure to the full IEEE double** — not to within
a tolerance, but as the same value, which is what a byte-identical instruction
stream on a deterministic emulator should produce. Against the accepted
baseline that is **+5.26% mean and +11.1% on the 1% low**, unchanged from W6a's
claim. One run, one basis, no averaging and no re-runs.

Evidence: `docs/saturn/evidence/reports/w6a-rebuild-throughput-30events-57fb7312.json`.

## 5. Gates

`verify-memory-map`, verbatim, auto-selecting this build's ELF:

```
verify-memory-map: checking .../build/saturn/sourceboot/e2-bob-identity-id-57fb7312058b7f2e/obj/sm64-saturn-sourceboot-e2.elf
  ___end          = 0x060FAA68
  hwram_remaining = 0x5598 bytes (required >= 0x1F00)
  lwram_end       = 0x002E95E0
  lwram_remaining = 0x16A20 bytes (floor >= 0x4000)
  RESULT          = OK
```

`hwram_remaining 0x5598` and `lwram 0x16A20` match W6a exactly, as they must
given an identical `.text`/`.data` layout.

| Gate | Result |
| :--- | :--- |
| `verify-memory-map` | **RESULT OK** |
| `verify-render-native-math` | PASS |
| `verify-render-native-math-mutation` | PASS (mutation caught) |
| `verify-shadow-trig` | PASS — 65,536 s16 inputs, 0 divergences |
| `verify-shadow-trig-mutation` | PASS (3 mutations caught) |
| `verify-runtime-contracts` | PASS |
| `verify-terrain-clip` | PASS |
| `verify-ztreme-frustum` | PASS |
| `verify-frame-pipeline` | PASS (mutation caught) |
| `verify-vdp1-frame-bank` | OK — 4 tests |
| `verify-vdp1-user-clip` | PASS (positive control: 80,813 writes removed) |
| `verify-vdp1-user-clip-mutation` | PASS (mutation caught) |
| `verify-gouraud-transfer` | PASS |
| `verify-ir-transform` | PASS |
| `verify-hot-promotion` | PASS (mutation caught) |
| `verify-saturn-object-pool-probe-contract` | OK — 12 tests |
| `verify-softfp-bitexact` (smoke, `SOFTFP_TEST_PAIRS=200000 SOFTFP_TEST_STRIDE=4096`) | **PASS — 16,917,285 checks, 0 failures** |

The soft-float gate was run at W6a's documented smoke configuration, not the
full 43.3 G-check sweep: the ELF comparison in section 2 proves the linked
library is byte-identical to the one W6a passed at full volume
(`w6a-softfp-bitexact-full.txt`), so a second full run would re-prove a
settled fact. The smoke matters for a different reason — it exercises the
**committed** gate recipe, which is the file that changed after W6a's build,
and it ran end-to-end from a single `make` invocation with no manual repair.
Its 16,917,285/0 figure is identical to W6a's smoke.

Not run, per the standing known-failure list:
`verify-render-clusters`, `verify-sourceboot-presentation-boundary`,
`verify-vdp1-transfer-pipeline`, `verify-render-snapshot-bank`,
`verify-graph-q16-contract`, `test_gen_actor_identity_registry.py`,
`verify-tools`, `verify-bob-bsp-header`.

## 6. Artifacts

- **Product:** `releases/2026-08-22_w6a-rebuild/id-57fb7312058b7f2e/`
  — `sm64-saturn-sourceboot-e2.{iso,cue}`, `saturn-release-manifest-v1.json`,
  `obj/sm64-saturn-sourceboot-e2.{elf,map}`, `obj/SOURCE.DAT`. The `obj/`
  layout is preserved because `--release-manifest` binds the ELF at `obj/…`
  relative to the manifest and rejects a flattened copy.
- **Pre-build preservation:** `releases/2026-08-22_0659_w6a-rebuild-pre-build/id-586944bb637a920f/`
  — the W6a candidate, hashes re-verified against
  `w6a-softfloat-implementation.md` before the build ran.
- **Build tree:** `build/saturn/sourceboot/e2-bob-identity-id-57fb7312058b7f2e/`.
  W6a's tree was moved to
  `build/saturn/sourceboot/w6a-prior-id-586944bb637a920f/` before the build so
  every object was compiled from scratch and so
  `verify-memory-map`'s `e2-bob-identity-*` glob could not select it.

`git diff` is empty; no product source, profile or configuration was modified.

## 7. What remains

- **`id-57fb7312058b7f2e` still needs an owner-observed CUE** before it can
  replace `id-e8720d58595d9a62` as the accepted baseline. This task ends at a
  sealed, measured artifact; no emulator GUI was opened.
- **The promotion candidate is now this tag, not W6a's.** `id-586944bb637a920f`
  cannot be reproduced from its own closure and should not be promoted;
  `w6a-softfloat-implementation.md` §11 already anticipated that a
  clean-closure rebuild would be what is promoted.
- **`tools/saturn/quad_map.py` writes unconditionally.** It is harmless today
  because its output is byte-stable, but it is the same class of defect T2.22
  fixed in eight other generators, and it re-stales a closure row on every
  `verify-runtime-contracts` run. Converting it to `write_if_changed` would
  remove a standing source of spurious mtime churn on a `generated-input`
  closure row.
- **A closure row edited after a build is invisible until the next rebuild.**
  W6a's `closure_clean: false` did flag it, and that flag was the reason this
  task existed — the mechanism works. The cheap habit that would have avoided
  the divergence entirely is to commit before building the artifact that will
  be measured.
