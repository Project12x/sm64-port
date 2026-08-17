# Sprint 2 Task T2.18 — parallel make is identity-safe

- Date: 2026-08-16 23:12 → 2026-08-17 00:37 local. Worktree
  `.worktrees/saturn-recovery`, branch `saturn/recovery`.
- Task: determine whether the `-j1` mandate at `AGENTS.md:60` and `HOWTO.md:21`
  can be relaxed without changing the sealed artifact identity, and relax it
  only if the hashes prove it safe.
- Host: 12 logical cores (`NUMBER_OF_PROCESSORS=12`), MSYS2 GNU Make 4.4.1.
- **Verdict: `-j1` is pure cost. Relaxed.** Three builds of the same
  27-variable tuple — `-j12`, `-j12`, `-j1` — all sealed the same identity and
  produced **byte-identical ELF, ISO, CUE, linker map, and all 280 object
  files**, with the canonical manifest comparison reporting `identical: true`
  and zero differing fields for all three pairs. A fourth, independent
  `-j12`/`-j1` pair at a different source state agrees. No order-dependent
  artifact exists in this build.
- **Wall clock: `-j1` 1039 s vs `-j12` 713 s / 708 s — a 31% saving (~5.5 min),
  not the 12 minutes the core count suggests.** The reason is the second
  finding below.
- **The larger finding is not about parallelism.** Every build in this project
  — serial and parallel alike — links the ELF **four times** and runs
  `objdump -S` **four times**, because a generated-source rule rewrites three
  files with fresh mtimes on every make invocation. Three of the four passes
  are pure waste and cost more than parallelism saves. Section 6.

---

## 1. The three-way test

All three builds ran back to back, sequentially, through
`tools/saturn/with-msys-toolchain.ps1` with the full 27-variable tuple
(`SATURN_OBJECT_POOL_CAPACITY=208`, `SATURN_DIAGNOSTIC_MODE=0`), each into a
fresh output tree so all 280 objects were compiled from scratch every time.

| Run | Jobs | Exit | Wall clock | Sealed identity | Source closure |
| --- | --- | --- | --- | --- | --- |
| B | `-j1` | 0 | **1039 s** | `id-0fade22f26a95c0c` | `3fa695c5…2470a` |
| C | `-j12` | 0 | **713 s** | `id-0fade22f26a95c0c` | `3fa695c5…2470a` |
| D | `-j12` | 0 | **708 s** | `id-0fade22f26a95c0c` | `3fa695c5…2470a` |

Equal sealed identity tags and equal closure hashes are what make the
comparison admissible — see section 3.

### The three-way hash comparison

| Artifact | B (`-j1`) | C (`-j12`) | D (`-j12`) | Three-way |
| --- | --- | --- | --- | --- |
| ELF | `23937dcc5e8fb03a7b8d76b73f72cf65c508406d899703b600908c4657baf931` | same | same | **identical** |
| ISO | `134c074ffd28605805534b416e93076fc96743a2bc9c333fffcbf07f676f65d7` | same | same | **identical** |
| CUE | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | same | same | identical (88 B, not identity-bearing) |
| linker `.map` | `a62f0695369739e42f9b86db8945df6751e82fe60b9ac3d04dfee458b4a81a36` | same | same | **identical** |
| all 280 `.o`, sorted digest | `ed5e5fbb988027a0b936a62410b50121afb50e7d723f7ec40c96bb8052999589` | same | same | **identical** |
| release manifest, raw bytes | `4b69157f90ea…619e7` | `045759c4be11…b0641` | `045759c4be11…b0641` | C ≡ D; B differs |
| release manifest, canonical compare | — | — | — | **`identical: true`, 0 differing fields, all three pairs** |

The object-set digest is the SHA-256 of the sorted `sha256sum` listing of every
`.o` in the tree, so it pins all 280 compilation units individually, not just
the link product.

The only raw-byte difference anywhere is run B's manifest, and it is not a
divergence: the manifest records Git provenance, and other agents committed to
this worktree between B and C. `BUILDING.md` states the sanctioned comparison
ignores manifest location, output relative layout, and Git provenance. The
project's own tool:

```
python tools/saturn/release_manifest.py compare \
  --first  build/saturn/sourceboot/t2_18-runB/saturn-release-manifest-v1.json \
  --second build/saturn/sourceboot/t2_18-runC/saturn-release-manifest-v1.json
```

```json
{"differing_fields":[],"identical":true,"schema":"sm64-saturn-release-comparison-v1",
 "first_manifest_sha256":"4b69157f90ea41dfca5370212f08e944996ed26d11214dcdf0aca593176619e7",
 "second_manifest_sha256":"045759c4be110061b7da9c80c7feed2814b7129f60b998de4b5705dc57bb0641"}
```

B vs C, B vs D, and C vs D all return `"identical":true` with
`"differing_fields":[]`. C and D — the two parallel builds — produce manifests
that are identical even at the raw byte level, because HEAD did not move
between them.

## 2. Independent corroboration at a second source state

Before the tree started churning, one earlier `-j12` build (run 1, exit 0,
**707 s**, sealed identity `id-c0352f297034f653`) was compared against the
**historical T2.17 `-j1` product build** already on disk at the same identity:

| Artifact | `-j1` (T2.17) | `-j12` (run 1) | Match |
| --- | --- | --- | --- |
| ELF | `2933c5d5d6d1399243d5b838b63c16f1b3e1a6ba2a79587edd9c3f7cb8c2fecd` | same | **identical** |
| ISO | `49b68a07144b2b58ad6781dadb35404e038e0f9d75eaf7c354f8fa88e1d4c9cc` | same | **identical** |
| manifest, canonical compare | `1d73590985…b35f3` | `777f8a657c…28a8c` | **`identical: true`, 0 differing fields** |

So parallel-vs-serial byte equality reproduces across **two independent source
states**, one of them a build made by a different task on a different day.

## 3. Method: identity-tag equality is the admission test

Four other agents were committing product source to this worktree throughout
the test window — six commits in six minutes at the peak. The test does not
rely on `git status` agreement, which cannot survive that. It relies on the
build's own sealing:

- Identity v2 is composed from the resolved profile and package manifests, the
  **source closure**, and the toolchain attestation. A change to any compiled
  input changes the closure, which changes the sealed tag, which changes the
  output directory name.
- **Two builds that seal the same tag provably compiled the same inputs.** Runs
  B, C and D all sealed `id-0fade22f26a95c0c` with closure hash
  `3fa695c5…2470a`, so their inputs were identical regardless of what HEAD was
  doing.
- `verify-sealed-inputs` independently rehashes every closure row *after* link,
  so an exit-0 build additionally proves its inputs did not move mid-flight.

This machinery was not merely assumed — it fired. Two earlier builds were
**voided by it and discarded rather than reported**:

- One `-j12` build reached the link and then failed:
  `ValueError: source closure input changed after discovery: Makefile.saturn.mk`
  (`make[1]: *** [Makefile:1484: verify-sealed-inputs] Error 1`). Another agent
  had committed a change to a closure input mid-build.
- A second `-j12` build died at 257 s compiling `src/goddard/dynlist_proc.c`
  while HEAD moved `f5b664f9` → `48a0f207` during those 257 seconds.

Neither void was a parallelism failure. Both are the reproducibility machinery
working as designed, and both are the reason the reported table comes only from
runs that sealed a common identity.

## 4. Verification of each removability claim

| Claim | Verdict | Evidence |
| --- | --- | --- |
| No `.NOTPARALLEL` anywhere | **holds** | `grep -rn NOTPARALLEL --include=*.mk --include=Makefile .` → zero matches repo-wide, including `third_party/libyaul/libyaul/build/*.mk`. |
| No `MAKEFLAGS` forcing serial | **holds** | Only two uses: `Makefile:953` (`MAKEFLAGS += --no-builtin-rules` in the N64/PC makefile — not a jobs flag) and `src/port/saturn/sourceboot/Makefile:1392`, which reads `$(firstword $(MAKEFLAGS))` solely to detect `-n`. Verified empirically that under `-j12` the first word is `-j12`, contains no `n`, so the path-list writer is not suppressed. |
| Stages are ordered structurally | **holds, and is stronger than described** | `Makefile.saturn.mk:261-288` is *three* recipe lines carrying *five* sub-make invocations: assets → discover → seal (`print-identity-tag`) → build → `verify-sealed-inputs seal-release`. GNU Make never parallelizes recipe lines within one target regardless of `-j`, and the last line chains its three sub-makes with shell `&&`. Ordering does not depend on `-j` at all. |
| Nothing in a later stage feeds an earlier one | **holds at stage granularity; violated *within* the build stage** | No backward edge across the five stages; the assets stage carries an explicit comment forbidding it (`Makefile:967-969`). But inside the build stage the `pre-build-iso` hook re-enters make and re-runs the *asset* generator — section 6. `verify-sealed-inputs` rehashes every closure row after link and passes, so the regenerated bytes are stable. |
| Real `.d` dependency tracking exists | **holds** | `build.post.bin.mk:60` defines `SH_DEPS`; `:161` does `-include $(SH_DEPS)`. Discovery independently regenerates depfiles every build via the phony `sourceboot-force-discovery-scan`. |
| Every build is clean, so incomplete header deps cannot bite | **holds** | Each sealed identity gets its own output directory, and each run's tree was moved aside afterwards, so all 280 objects were compiled from scratch in every run. |
| `-j` reaches the compile stage through `$(MAKE)` | **holds, confirmed in the real build** | Logs contain 14 × `make[3]: warning: -j1 forced in submake: resetting jobserver mode` — a warning GNU Make emits only when a jobserver is live in the parent — proving `--jobserver-auth` propagated through the recursive chain. One benign `jobserver unavailable` appears in the root "Building tools" sub-make, which is a no-op here. |

## 5. Order-dependent artifacts: none found

Every candidate hazard was checked; each resolves at parse time or by
construction, i.e. independently of `-j`:

- **Link order** — `SH_OBJS_UNIQ` (`build.post.bin.mk:45`) is a `foreach` over
  `SH_SRCS_C/CXX/S/OTHER`, evaluated at parse time. The link line consumes that
  variable, not a directory scan or a completion order.
- **`$(wildcard)` / `$(shell find)` source lists** — parse-time, before any
  recipe runs; identical under any `-j`.
- **The soft-float archive** — `rm -f` then a single
  `ar rcs "$@" $(SOFTFP_OBJS)` with a fixed variable order; never built
  incrementally.
- **The closure path-lists** — written by `$(file >)` in one recipe from
  `$(sort …)`: single writer, deduplicated, byte-sorted.
- **Generated headers consumed by many TUs** — already guarded by an explicit
  order-only barrier at `Makefile:1532`, whose comment states the rationale
  outright: *"under -j make would otherwise happily race a consumer against the
  generator."* Someone had already thought about `-j` safety here.
- **ISO timestamps** — pinned by `SOURCE_DATE_EPOCH` plus
  `tools/saturn/xorrisofs-reproducible`.

Prior art agrees: a from-scratch clean `-j8` build is recorded in
`.superpowers/sdd/2026-08-05-saturn-full-game-completeness-parallel-optimization/progress.md:497`
(2026-08-09), though without a hash comparison against a serial build. This task
supplies that comparison.

## 6. The real cost: four links and four `objdump -S` passes per build

Order-independent, and it applies to **every build this project has ever done,
serial and parallel alike.** This is why `-j12` saves 31% rather than 90%.

A single build's log contains **4** link commands, **4** `objdump -S`
invocations, and **6** runs of `compile_scene_package.py`. Phase windows
reconstructed from artifact mtimes (run 1, `-j12`, 707 s total):

| Phase | Window | Duration |
| --- | --- | --- |
| assets + discovery + seal | 23:25:17 → 23:26:57 | ~100 s |
| parallel compile, 280 objects | 23:26:57 → ~23:28:30 | ~95 s |
| **four link + nm + `objdump -S` cycles** | ~23:28:30 → 23:36:59 | **~510 s (72%)** |
| ISO + `verify-sealed-inputs` + `seal-release` | 23:36:59 → 23:37:04 | ~5 s |

Each `objdump -S` writes a 52,289,956-byte listing. Successive passes produced
**byte-identical** output — the work is not merely repeated, it is provably
redundant. Only ~95 s of the 707 s is parallelizable compilation, which is
exactly why `-jN` cannot reclaim the 15 minutes on its own.

### Mechanism

1. `src/port/saturn/sourceboot/Makefile:1142` —
   `$(SOURCEBOOT_ACTOR_SCENE_ASM): source-actor-scene-package …`. The
   prerequisite is **PHONY**, so the target is always out of date and its
   recipe runs on every make invocation.
2. That recipe runs `compile_scene_package.py`, which rewrites
   `generated/actor_scene_bundle_g15.sx`, `scene_package.h`, and
   `saturn_scene_package_abi.h` — **identical bytes, fresh mtimes**.
3. Fresh header mtimes restale every TU that includes them (observed:
   `src/game/rendering_graph_node.c`,
   `src/port/saturn/gfx/saturn_demo_render.c`), forcing a recompile, a relink,
   and a fresh `nm` + `objdump -S`.
4. `src/port/saturn/sourceboot/Makefile:1557` —
   `pre-build-iso: $(SOURCEBOOT_CART_IMAGE)`. The ISO rule
   (`build.post.iso-cue.mk`) re-enters make for `pre-build-iso`, which
   re-evaluates the same always-stale chain and triggers another round.

### Scoped fix — deliberately NOT implemented here

It is a separate change, and this worktree currently has four other agents
editing product source.

**Preferred — write-if-changed in the generators.** Have
`compile_scene_package.py` (and the sibling actor-bank / quad-map generators)
write to a temporary path and replace the output only when the bytes differ,
leaving the mtime untouched otherwise. These tools are already proven
deterministic — `test_scene_package_determinism` passes in-build, and this
task's own evidence is that four successive regenerations produced identical
bytes — so a content comparison is sound. This removes all three redundant
passes at the root, and helps `-j1` builds by exactly the same amount.

**Alternative — break the PHONY→file edge.** Give
`$(SOURCEBOOT_ACTOR_SCENE_ASM)` its real file prerequisites (the closure, the
family report, the generator sources) instead of the phony
`source-actor-scene-package`, so it regenerates only when an input actually
changes. Narrower, but leaves other phony-driven generators untouched.

An order-only prerequisite is **not** sufficient: it would stop the phony target
forcing the `.sx` rebuild, but would not stop the generator rewriting mtimes
when it does run.

**Secondary, independent lever:** `.sym` and `.asm` are debug listings emitted
unconditionally by yaul's bundled `.elf` rule. Even one `objdump -S` pass costs
85–180 s and writes 52 MB. Making the listing an opt-in target would remove most
of the remainder, at the cost of overriding a bundled upstream recipe.

**Expected effect.** Removing three of four passes should cut roughly 380 s
(~6.3 min) from *every* build, serial or parallel — larger than the entire
parallelism saving measured here, and available without touching `-j` at all.
This is an **estimate**, not a measurement: no fix was implemented or timed.

## 7. Wall clock and the recommended `-j`

| Configuration | Wall clock |
| --- | --- |
| `-j1` (run B, this host, same closure) | **1039 s** (17 m 19 s) |
| `-j12` (run C) | **713 s** (11 m 53 s) |
| `-j12` (run D) | **708 s** (11 m 48 s) |
| `-j12` (run 1, different closure) | 707 s |
| `-j1` (2026-08-10, `hermetic-sourceboot-release-2026-08-10.md:30`) | 924.0 s / 937.2 s |

Same-host, same-closure saving: **1039 → 710.5 s mean = 328.5 s (31.6%)**.

Both `-j12` runs land within 5 s of each other, so the measurement is stable.
`-j12` on a 12-core host was used for the test; a value at or below the core
count is what the evidence covers. `-j1` remains available and correct for a
loaded machine — this task removes a mandate, it does not impose one.

## 8. Doc edits made

Both sites independently mandated the serial route, so both were changed, each
citing the evidence inline so the constraint is not reinstated later from the
same caution that created it:

- **`AGENTS.md:60`** — "with `-j1`" replaced by parallel-permitted wording
  carrying the three-way hash result and the 1039 s → ~710 s numbers.
- **`HOWTO.md:21`** — "the repository MSYS wrapper and serial `-j1` make route"
  replaced likewise.

Everything else is unchanged: builds still go through
`tools/saturn/with-msys-toolchain.ps1`, still use the full 27-variable set, and
still must not invoke a different Make, Python, or profile implicitly. **Only
the `-j1` part was under test and only it was relaxed.**

## 9. Reproduction

```
# Each run, via tools/saturn/with-msys-toolchain.ps1 -> MSYS sh --noprofile --norc -l,
# sourcing ../../.yaul.env then `unset COMPILER_PATH` (BUILDING.md gotcha):
make -f Makefile.saturn.mk -j<N> sourceboot \
  <the 27-variable tuple from sprint1-stage1-link-smoke.md, with
   SATURN_OBJECT_POOL_CAPACITY=208 and SATURN_DIAGNOSTIC_MODE=0>

# Between runs, move the sealed tree aside so the next build is a full rebuild:
mv build/saturn/sourceboot/e2-bob-identity-<tag> build/saturn/sourceboot/<label>

# Admission test: all runs must report the same <tag>. Then compare:
sha256sum <tree>/obj/sm64-saturn-sourceboot-e2.elf <tree>/sm64-saturn-sourceboot-e2.iso
python tools/saturn/release_manifest.py compare \
  --first <A>/saturn-release-manifest-v1.json --second <B>/saturn-release-manifest-v1.json
```

Preserved trees under `build/saturn/sourceboot/`: `t2_18-runB`, `t2_18-runC`,
`t2_18-runD`, plus `t2_18-run1-j12` and `t2_18-ref-t2_17-j1` for the
second-source-state pair. Artifacts also copied to
`releases/2026-08-17_t2_18-parallel-identity/{runB,runC,runD}/`.

## 10. Honesty

- **The result is a hash match, not a speed argument.** The mandate was relaxed
  because three builds produced byte-identical ELF, ISO, map and objects, with
  the canonical manifest comparison clean on all three pairs. The 31% saving
  alone would not have justified it.
- **Two builds were discarded, not reported.** Both were voided by other
  agents' source changes landing mid-build, caught by the build's own gates.
  They are described in section 3 rather than hidden.
- **The saving is 31%, not the order-of-magnitude the brief hypothesized.**
  Only ~95 s of a ~710 s build is parallelizable compilation. Anyone expecting
  `-jN` to turn 15 minutes into 2 will be disappointed; section 6 is where the
  remaining time actually goes.
- **`-j12` was the only parallel width tested.** No claim is made about `-j24`,
  oversubscription, or behaviour on a loaded machine. The two `-j12` runs
  agreeing to within 5 s is evidence of stability at that width only.
- **Three runs is three runs.** A race that fires rarely could still exist. What
  is established is that two independent `-j12` builds and one `-j1` build
  agree byte for byte at one source state, and that a third `-j12` build agrees
  with a `-j1` build at a second, independent source state.
- **The four-link finding is measured; its fix is not.** The 4 links, 4
  `objdump -S` passes and byte-identical 52,289,956 B listings are observed.
  The ~380 s projected saving is an estimate — nothing was implemented or timed.
- **No emulator was launched and no product source was touched.** No claims are
  made about boot, visuals, audio, input, or FPS.
- `verify-sourceboot-presentation-boundary` remains a known pre-existing
  failure (literal-text drift since `c9476fd6`) and was not touched.
