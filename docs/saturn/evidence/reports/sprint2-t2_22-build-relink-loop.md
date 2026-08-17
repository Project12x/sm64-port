# Sprint 2 Task T2.22 — the four links are gone

- Date: 2026-08-17 00:45 → 01:50 local. Worktree `.worktrees/saturn-recovery`,
  branch `saturn/recovery`.
- Task: stop every build relinking the ELF and re-running `objdump -S` four
  times, as scoped by
  [`sprint2-t2_18-parallel-build-identity.md`](sprint2-t2_18-parallel-build-identity.md)
  section 6.
- Host: 12 logical cores (`NUMBER_OF_PROCESSORS=12`), MSYS2 GNU Make 4.4.1.
  Same host and same 27-variable tuple as T2.18.

---

## 0. Result

| | before | after |
| --- | --- | --- |
| ELF links per build | **4** | **1** |
| `objdump -S` passes per build (52,289,956 B each) | **4** | **1** |
| `-j12` wall clock, from scratch | **694.9 s** | **417.0 s** (**−277.9 s, −40.0%**) |
| `-j1` wall clock, from scratch | **1039 s** (T2.18) | **797.1 s** (**−241.9 s, −23.3%**) |
| re-running `make sourceboot` with nothing changed | 4 links, 4 `objdump` | **0 compiles, 0 links, 0 `objdump`** |

Every artifact byte outside the sealed identity blob is unchanged: **279 of
280 object files are byte-identical**, `SOURCE.DAT` and the CUE are
byte-identical, and the two ELFs differ in **exactly one 64-byte run**, which
lies wholly inside the 500-byte build-identity blob. Section 5.

**Two things in the brief turned out to be wrong, and both are reported rather
than worked around:**

1. T2.18 named `compile_scene_package.py` and three files it rewrites. It does
   not rewrite them — it has been write-if-changed since before this task.
   The real churn is in eight *other* generators. Section 1.
2. The admission test "all runs must land in the same `e2-bob-identity-id-*`
   directory" is **structurally unreachable *across* a fix to this defect**,
   and so is `release_manifest.py compare` returning `identical: true` across
   it. The makefiles and the generator scripts are themselves source-closure
   inputs, and the closure hash is compiled into the ELF. Section 5 replaces
   the across-the-fix comparison with a per-object and per-byte one, and still
   demands the original test *within* the fix: the from-scratch `-j1` and
   `-j12` post-fix builds do seal one tag and do return `identical: true` with
   zero differing fields. This is a property of the build's identity design,
   not a defect and not a regression.

## 1. T2.18 named the wrong generator

T2.18 section 6 identified `src/port/saturn/sourceboot/Makefile:1142` and
concluded that `compile_scene_package.py` "rewrites
`generated/actor_scene_bundle_g15.sx`, `scene_package.h`, and
`saturn_scene_package_abi.h` — identical bytes, fresh mtimes."

It does not. `compile_scene_package.py` publishes every output through
`publish_or_verify_set` (`tools/saturn/compile_scene_package.py:396-479`),
which locks each target, compares the existing bytes, and `continue`s without
writing when they already match. The mtimes prove it: immediately after
T2.18's own run D finished at 00:36:40 on 2026-08-17, those three files were
dated **2026-08-14 23:04**, **2026-08-15 12:51** and **2026-08-14 22:56**.
They had not been written for two days.

The `$(SOURCEBOOT_ACTOR_SCENE_ASM): source-actor-scene-package` PHONY edge is
real and it does re-run the generator on every parse — six times per build —
but the recipe is `@test -f "$@"` and the sub-make's writes are idempotent, so
it costs process time, not a relink.

Implementing the brief literally would therefore have changed nothing.

## 2. What actually churns, measured live

The baseline build was run with a timestamped log
(`releases/2026-08-17_t2_22-build-relink-loop/before-j12/build.log`) and the
generated tree was sampled while it ran. The mechanism is visible directly.

`build.post.iso-cue.mk:24` and `:33` re-enter make for the `pre-build-iso` and
`post-build-iso` hooks, and `Makefile.saturn.mk:261-288` adds a separate
`verify-sealed-inputs seal-release` sub-make. **One build is four fresh parses
of the sourceboot makefile**, each of which walks the ELF's prerequisite
graph. `compile-actor-banks` appears in the baseline log exactly four times:
00:52:51, 00:54:59, 00:57:43, 01:00:11.

In each of those parses, generated files that carry PHONY-rooted rules were
rewritten with identical bytes and fresh mtimes. The `.d` files pulled in by
`build.post.bin.mk:161` then make those files ordinary prerequisites of the
objects, so the objects restaled and the ELF relinked:

| rewritten with identical bytes | written by | forced by | restales |
| --- | --- | --- | --- |
| `saturn_geo_depth_manifest.h` / `.ld` / `.json` | `geo_depth_manifest.py` | PHONY `source-geo-depth` (recipe on a phony target) | `rendering_graph_node.o`, and `.ld` is a link input |
| `actor_identity_registry.h` | `gen_actor_identity_registry.py` | PHONY `source-actor-identity-registry-force`, `Makefile:1110-1116` | `rendering_graph_node.o` |
| `bob_sfx_metadata.bin`, `bob_sfx_pcm.bin`, `bob_sfx_manifest.json` | `compile_sourceboot_sfx_bundle.py` | PHONY `sourceboot-audio-sfx-inputs`, `Makefile:1195` | re-emits `sourceboot_sfx_bundle.sx`, then its `.o` |
| `bob_area1_intake.json`, `bob_area1_mesh_ir_v2.json` | `extract_bob_area.py` | PHONY `compile-bob-area`, `Makefile.saturn.mk:2176` | the whole BOB chain below |
| `bob_area1_compiled.json`, `bob_area1_report.json` | `saturn_mesh_ir.py` | same | same |
| `bob_tiles_clut16.bin` / `.pal` / `bob_tiles_manifest.json` | `bake_bob_tiles.py` | PHONY `compile-bob-tiles` | re-emits `bob_texture_bank.sx`, then its `.o` |
| `bob_area1_bsp_report.json`, `bob_bsp.h` | `compile_bob_bsp.py` | PHONY `compile-bob-bsp` | `saturn_demo_render.o` |
| `bob_scene.h` | `emit_bob_scene.py` | PHONY `compile-bob-scene` | `saturn_demo_render.o` |

The BOB group is self-perpetuating rather than merely wasteful: the PHONY
`compile-bob-area` rewrites `bob_area1_intake.json`, which is a *prerequisite*
of the tiles rule, which rewrites `bob_tiles_manifest.json`, which is a
prerequisite of the BSP rule, and so on. Once anything drags that chain in,
each parse hands the next parse a fresh reason to run. `Makefile.saturn.mk`
already carries a comment above `compile-bob-bsp` recording an earlier
encounter with this same class of bug ("the file flip on every make pass …
each flip resealed a new identity").

The baseline log shows the consequence exactly. After link 1 at 00:56:19:

```
00:57:42  make ... -f Makefile pre-build-iso        <- second parse begins
00:57:44  geo depth manifest: PASS                  <- manifest .h/.ld/.json rewritten
00:57:47  bob_sfx_{metadata.bin,pcm.bin,manifest}   <- rewritten
00:58:18  bob_area1_{intake,mesh_ir,compiled}.json  <- rewritten
00:58:22  bob_tiles_clut16.{bin,pal}, manifest      <- rewritten
00:58:33  actor_identity_registry.h                 <- rewritten
00:58:33  src/game/rendering_graph_node.c           <- recompiled
00:58:33  generated/sourceboot_sfx_bundle.sx        <- re-emitted and reassembled
00:58:51  src/port/saturn/gfx/saturn_demo_render.c  <- recompiled
00:58:53  sm64-saturn-sourceboot-e2.elf             <- link 2
```

Links landed at 00:56:19, 00:58:53, 01:00:58 and 01:03:04. Each cycle is
125–155 s, essentially all of it `objdump -S` writing a 52 MB listing.

**Phase budget of the 694.9 s baseline:**

| phase | window | duration |
| --- | --- | --- |
| assets + discovery + seal | 00:52:50 → 00:54:58 | 128 s |
| parallel compile, 280 objects | 00:54:58 → 00:56:19 | 81 s |
| **four link + `nm` + `objdump -S` cycles, ISO, verify, seal** | 00:56:19 → 01:04:24 | **485 s (70%)** |

## 3. The fix

New module `tools/saturn/write_if_changed.py` with two functions,
`write_bytes_if_changed` and `write_text_if_changed`. Each compares the bytes
already on disk with the bytes the caller is about to publish and returns
without writing when they match.

`write_text_if_changed` takes `newline` with `open()` semantics and applies the
identical translation before comparing, because these call sites are not
uniform: `gen_actor_identity_registry.py`, `geo_depth_manifest.py` and
`saturn_mesh_ir.py` pinned `newline="\n"` while `extract_bob_area.py`,
`bake_bob_tiles.py`, `compile_bob_bsp.py`, `emit_bob_scene.py` and
`compile_sourceboot_sfx_bundle.py` used the default, which on Windows writes
CRLF. **Comparing untranslated text would have silently rewritten every
CRLF file on every run and fixed nothing**, and comparing while writing
translated bytes would have changed those files' content. The write path
delegates to the caller's original `Path.write_text` / `Path.write_bytes` call,
so the published bytes are provably the same bytes as before.

Eight generators now publish through it — the eight in section 2's table.
Registered as a closure input at
`src/port/saturn/sourceboot/Makefile:1332` so a change to the helper reseals
the identity like any other generator. Bare intra-directory imports
(`from write_if_changed import …`) match the established convention in this
directory — `tools/saturn` has no `__init__.py` and 20+ tools already import
their siblings this way — and every context that can resolve `saturn_mesh_ir`
can resolve `write_if_changed` from the same directory.

Not changed: `compile_scene_package.py` (already idempotent), and the
`printf > "$@"` recipes that emit `bob_texture_bank.sx`,
`bob_sky_bitmap.sx` and `sourceboot_sfx_bundle.sx`. Those truncate
unconditionally but only run when a prerequisite is genuinely newer, and with
the prerequisites no longer churning they stop running. Section 8 records the
one place that still leaves exposed.

## 4. The PHONY prerequisites were kept, deliberately

T2.18 offered "break the PHONY→file edge" as an alternative. It was rejected,
and the phony prerequisites are all still in place.

Each of them buys a real guarantee that a file prerequisite cannot express:

- `source-actor-identity-registry-force` exists because the registry header has
  one stable pathname but **embeds `SOURCEBOOT_SCENE_PACKAGE_GENERATION`**. Its
  own comment states the rule: a prior generation's registry must never survive
  as a newer package's runtime binding. That hazard is a *variable* change, not
  a file change, so no prerequisite list detects it.
- `source-geo-depth` scans `actors/` and `levels/` by `rglob("*.c")`. A file
  prerequisite list cannot express "a geo source was added or renamed".
- `compile-bob-area` and its chain, and `sourceboot-audio-sfx-inputs`, are
  cross-makefile goals invoked as sub-makes; they have no file identity in the
  calling parse at all.

With write-if-changed, keeping them is free: the generator still runs, still
recomputes from live inputs, and now costs one Python process instead of a
relink. Removing them would have traded a 40% build-time defect for a
correctness hole. **This is also why an order-only prerequisite is not the
answer** — as T2.18 already noted, it stops the phony forcing a rebuild but
does nothing about the generator rewriting mtimes when it does run. In fact
`$(SH_OBJS_UNIQ)` at `Makefile:1533` *already* depends on
`$(SOURCEBOOT_ACTOR_IDENTITY_REGISTRY_HEADER)` and `source-geo-depth`
order-only, and the loop happened anyway.

## 5. Byte identity: what could be proven, and what could not

### The admission test in the brief cannot be satisfied by any fix

The sealed identity is composed from the **source closure**, and the closure
contains `Makefile.saturn.mk` and `src/port/saturn/sourceboot/Makefile` as
`linker/build-recipe` rows and all 60-odd generator scripts as `generator`
rows, each with its SHA-256. The closure file's own hash goes into the identity
spec, the identity generator turns that into the label, and
`src/port/saturn/platform/saturn_build_identity.c` compiles the resulting blob
**into the ELF**.

So *any* change that fixes this defect — in a generator or in a makefile —
necessarily changes the closure hash, the sealed tag, the output directory name
and the ELF. Requiring the same `e2-bob-identity-id-*` directory before and
after is requiring that the fix not exist. The same applies to
`release_manifest.py compare` reporting `identical: true`.

This was verified before the fix was written, not discovered afterwards.

### The control: the baseline reproduces T2.18 exactly

Before touching anything, a from-scratch `-j12` build of the unmodified tree
sealed `id-0fade22f26a95c0c` — T2.18's identity — and reproduced its run D
**byte for byte**:

| artifact | verdict |
| --- | --- |
| ELF `23937dcc5e8fb03a7b8d76b73f72cf65c508406d899703b600908c4657baf931` | identical |
| ISO `134c074ffd28605805534b416e93076fc96743a2bc9c333fffcbf07f676f65d7` | identical |
| linker map `a62f0695369739e42f9b86db8945df6751e82fe60b9ac3d04dfee458b4a81a36` | identical |
| `SOURCE.DAT` `0deb02cb52e2c474…` | identical |
| CUE | identical |
| all 280 `.o` | **280/280 identical, 0 differing** |

That is a fifth independent build agreeing at that identity, and it pins the
"before" side of the comparison to a known artifact rather than to a claim.

### The substitute test, which is stronger

After the fix the tuple sealed `id-bed197e0c5e928d3`. Comparing that tree
against the baseline tree:

| artifact | verdict |
| --- | --- |
| all 280 `.o` | **279 identical; 1 differs: `src/port/saturn/platform/saturn_build_identity.o`** |
| `SOURCE.DAT` (the cart image, `.cart_rodata`) | **identical** |
| CUE | identical |
| ELF | same size, 9,992,380 B; **one differing run** |
| ISO, linker map | differ, following the ELF |

The single differing object is precisely the translation unit that compiles the
sealed identity. And the ELF difference is one contiguous 64-byte run at file
offset `0x00084724`, which lies **wholly inside** the 500-byte identity blob
that starts at `0x000846f0`:

```
before: 3fa695c5da37b4d7befcee8c1e80e6471f8f7b65f787a795e564034f81c2470a 0fade22f26a95c0c ...
after:  fc773748a63642e608fa7111a901f5b8cf352b3bfd3e88edb8a0680d10ad9f50 bed197e0c5e928d3 ...
```

The first 32 bytes are the source-closure hash — `3fa695c5…2470a` is the exact
closure hash T2.18 recorded for runs B, C and D — and the next 8 are the
identity tag. **Every one of the other 9,992,316 ELF bytes is unchanged.**

`release_manifest.py compare` reports `identical: false` with exactly six
differing fields, and all six are that same chain:

```
source_closure_sha256, effective_config.artifact_hashes.source_hash,
effective_config_sha256, identity_sha256,
outputs.elf.sha256, outputs.iso.sha256
```

Nothing else moved: not the toolchain attestation, not the resolved profile,
not the package set, not `SOURCE.DAT`.

### `identical: true` where it can be demanded: `-j1` vs `-j12` after the fix

The manifest equality the brief asked for *is* reachable — between two builds
that share a closure. The from-scratch `-j1` build sealed the same
`id-bed197e0c5e928d3` and produced:

| artifact | `-j12` vs `-j1` |
| --- | --- |
| ELF `93fad886b33421c47a6ee3da1bfc6e202f2b08f047061891b66bed55d15b3f3f` | **identical** |
| ISO `47deaa6ab76506b487ff226435fbe1f10267be18cc83f4fe7990e10e11d08763` | **identical** |
| linker map `e8c6a9929accc26b46cc7dbf1a1c979ce1b2ea14a7626d8cc7010cf88850cd76` | **identical** |
| `SOURCE.DAT`, CUE | **identical** |
| all 280 `.o` | **280/280 identical, 0 differing** |
| `release_manifest.py compare` | **`identical: true`, 0 differing fields** — and the two manifests are equal at the raw byte level (`bfd115b024c5…`) |

So T2.18's parallel-identity result reproduces on top of this change, and the
one-link build is as reproducible as the four-link build was.

## 6. The regeneration guarantee, tested three ways

A write-if-changed that never rewrites is a silent-staleness bug, worse than
the waste it replaces. Three tests, two of them at build level:

**(a) Nothing changed → nothing happens.** Re-running `make sourceboot` on the
completed tree with the same tuple: **0 compiles, 0 links, 0 `objdump`
passes**, 261 s of generator and validation work only, same sealed identity,
and byte-identical ELF, ISO and map. Before the fix this same no-op re-run
performed four links and four `objdump -S` passes.

**(b) A generated output on disk is wrong → it is repaired and the link
re-runs.** `build/saturn/sourceboot/generated/actor_identity_registry.h` was
corrupted in place (`SATURN_ACTOR_IDENTITY_REGISTRY_COUNT 25U` → `24U`,
SHA-256 `c5757e77…` → `72e6d219…`) and `make sourceboot` re-run with no other
change:

- the generator detected the difference and rewrote the file — SHA-256 back to
  `c5757e77ff587e8d94c56e31697b5566607cd88d9552d8c691fa397e453c0f95`, mtime
  advanced to 01:29:30;
- **exactly 1 compile** (`src/game/rendering_graph_node.o`), **1 link**, **1
  `objdump -S`**;
- and the resulting ELF, ISO and map hashes are **identical** to the
  pre-corruption build (`93fad886b33421c4…`, `47deaa6ab76506b4…`,
  `e8c6a9929accc26b…`).

That is the exact chain the fix must not break: wrong bytes → rewrite → mtime
advance → recompile → relink → correct product.

**(c) A genuine input change moves the output.**
`tools/saturn/test_write_if_changed.py` runs `geo_depth_manifest.py` as a
subprocess over a fixture source tree, asserts a second run over unchanged
inputs touches nothing (mtime stamped into the past survives), then deepens the
fixture's geo nesting from 3 to 9 and asserts the header and JSON report are
both rewritten and their bytes move, with `max_proven_depth == 9`. The linker
fragment carries only the aligned capacity, which legitimately does not move
for that change, so the test proves it is *correct* rather than stale by
regenerating into a virgin directory and comparing bytes. 11 tests, wired into
`verify-tools`.

**Mutation-checked.** Three mutations of `write_if_changed.py`, each run
against the suite:

| mutation | result |
| --- | --- |
| always skip the write (`if True:`) | **4 failures, 8 errors** |
| always write (the pre-fix behaviour, `if False:`) | **4 failures** |
| drop the platform newline translation in the comparison | **1 failure** |

0% survival.

## 7. Wall clock

| configuration | before | after | delta |
| --- | --- | --- | --- |
| `-j12`, from scratch | **694.9 s** (this task's baseline; T2.18 measured 708 s / 713 s / 707 s at the same identity) | **417.0 s** | **−277.9 s, −40.0%** |
| `-j1`, from scratch | **1039 s** (T2.18 run B) | **797.1 s** | **−241.9 s, −23.3%** |
| re-run with nothing changed, `-j12` | 4 links, 4 `objdump` | 261.2 s, **0 links** | — |
| re-run after one generated header was corrupted, `-j12` | — | 400.6 s, 1 link | — |

Phase budget after the fix, `-j12`:

| phase | window | duration |
| --- | --- | --- |
| assets + discovery + seal | 01:15:30 → 01:17:41 | 131 s |
| parallel compile, 280 objects | 01:17:41 → 01:18:43 | 62 s |
| one link + `nm` + `objdump -S`, ISO, verify, seal, and the three remaining parses | 01:18:43 → 01:22:26 | 223 s |

The `-j1` saving (241.9 s, 23.3%) is smaller in percentage terms than `-j12`'s
because the serial compile of 280 objects is a much larger share of that
build; the absolute seconds removed are comparable, as expected for a fix that
removes three fixed-cost link/listing cycles rather than parallel work.

**The saving is 278 s, not the ~380 s T2.18 projected.** T2.18's estimate
assumed three whole cycles at ~170 s each disappear. Three links, three `nm`
runs and three 52 MB `objdump -S` listings did disappear, but the three extra
make parses remain — they are the `pre-build-iso` / `post-build-iso` hooks and
the verify sub-make, and they still walk the graph and still run every PHONY
generator, now to no effect. That residue is roughly 130 s of the post-fix
build and is section 8's remaining lever.

## 8. What is left

- **The three extra make parses still run every generator.** They no longer
  write anything, but `compile_scene_package.py` still runs five times per
  build and the BOB chain, the audio chain and the geo-depth scan all still
  execute once per parse. That is the ~130 s residue. Removing it means
  restructuring the ISO hook re-entry, which is a much larger change and is
  what the `Makefile.saturn.mk:255-260` comment is protecting.
- **`objdump -S` is still emitted unconditionally**, once instead of four
  times, and still costs 85–180 s and writes 52,289,956 B. T2.18's secondary
  lever — making the `.sym`/`.asm` listings opt-in — is untouched and is now
  the single largest remaining item in the build.
- **The `printf > "$@"` assembly emitters are still unconditional.**
  `bob_texture_bank.sx`, `bob_sky_bitmap.sx` and `sourceboot_sfx_bundle.sx` are
  truncated and rewritten whenever make decides to run them. With their
  prerequisites no longer churning they stop being run, but they are not
  themselves idempotent, so a future generator that starts churning would
  re-open a narrower version of this loop.
- **One-shot catch-up.** The first build after this change still saw
  `bob_bsp_fragments*` and two `.sx` files regenerate once, because the
  *previous* build had left them with churned mtimes. That is the last echo of
  the old behaviour, not new churn; the no-op re-run in section 6(a) confirms
  the steady state is clean.

## 9. Honesty

- **T2.18's named mechanism was wrong and implementing the brief literally
  would have achieved nothing.** `compile_scene_package.py` was already
  write-if-changed. This is stated first in this report because it is the part
  most likely to be assumed correct on a re-read.
- **The identity moved, and it had to.** No fix to this defect can preserve the
  sealed tag, because the build deliberately hashes its own recipe and its own
  generators into the artifact. The requested `identical: true` was replaced by
  a per-object and per-byte comparison, which is a stronger claim, not a
  weaker one — but it is a *different* claim and the substitution is
  deliberate.
- **The saving is 40%, not the projected 55%.** 277.9 s at `-j12`, not ~380 s.
  Section 7 says where the difference went.
- **`-j1` was measured once.** So was `-j12` after the fix. The baseline
  `-j12` figure has four independent measurements behind it (this task's
  694.9 s plus T2.18's 708/713/707 s); the post-fix figures do not.
- **Zero builds were voided.** Two commits landed in this worktree during the
  baseline build's window (`c9adb159` at 00:52 and `bf060ed3` at 01:05, both
  from other agents), but both touched only `tools/saturn/capture_*.py` and
  `render_mario_ab.py` / `decimate_mario_actor.py`, none of which are source
  closure inputs. `verify-sealed-inputs` passed on every build, and the
  baseline reproduced T2.18's run D byte for byte, which is independent proof
  the tree did not move under it.
- **The verification builds ran with the fix uncommitted.** The working tree
  was frozen for the duration of each build and no tracked file was edited
  while a build was in flight. The sealed identity binds file *content*, not
  git state, and the sanctioned manifest comparison ignores git provenance
  (`BUILDING.md`), so this does not affect any hash reported here. The commit
  followed the measurements.
- **A pre-existing host-gate failure was found and not chased.**
  `tools/saturn/test_gen_actor_identity_registry.py:462`
  (`test_sourceboot_registry_does_not_force_rewrite_immutable_bundle_inputs`)
  asserts the registry rule does not depend on `source-actor-family-bundle`.
  It does, at `Makefile:1117`, and has since before this task. This is a sixth
  instance of the brittle-host-gate class STATE.md catalogues. It is reported,
  not worked around; the assertion's *intent* — that the registry rule must not
  force a rewrite of immutable bundle inputs — is exactly what this task
  delivers, so the gate is now asserting the wrong mechanism for the right
  reason.
- **`AGENTS.md` and `HOWTO.md` still quote T2.18's pre-T2.22 wall clocks**
  (1039 s vs 713/708 s). The task brief instructed explicitly not to edit those
  two files, so they were not touched. Their *claim* — that parallel make is
  identity-safe and `-j1` is not required — is unaffected; only the seconds are
  now historical. Both cite T2.18's report, which remains accurate for the
  build as it stood.
- **`ROADMAP.md` was not touched.** It tracks product cadence levers; this is
  build infrastructure and has no cadence effect. `STATE.md` gained one note,
  because the sealed tag for the standard tuple moved and every cadence figure
  recorded there is bound to a pre-T2.22 tag.
- **No emulator was launched and no product source was touched.** No claim is
  made about boot, visuals, audio, input or FPS. `SOURCE.DAT` — the cart image
  the console actually reads — is byte-identical before and after.

## 10. Reproduction

```
# Baseline and after builds, each into a fresh tree, via
# tools/saturn/with-msys-toolchain.ps1 -> MSYS sh --noprofile --norc -l,
# sourcing ../../.yaul.env then `unset COMPILER_PATH`:
make -f Makefile.saturn.mk -j<N> sourceboot <the 27-variable tuple from
  sprint1-stage1-link-smoke.md, SATURN_OBJECT_POOL_CAPACITY=208,
  SATURN_DIAGNOSTIC_MODE=0>

# Count the loop:
grep -c 'sh-elf-gcc.exe -specs=sourceboot' build.log   # links
grep -c 'objdump.exe -S' build.log                     # listings

# Regeneration test:
python -c "import re;from pathlib import Path;p=Path('build/saturn/sourceboot/generated/actor_identity_registry.h');r=p.read_bytes();m=re.search(rb'REGISTRY_COUNT (\d+)U',r);p.write_bytes(r[:m.start(1)]+b'24'+r[m.end(1):])"
# ... re-run the same make; expect exactly one link and the same ELF hash.

# Host tests:
python tools/saturn/test_write_if_changed.py     # 11 tests
python tools/saturn/test_geo_depth_manifest.py
```

Preserved trees under `build/saturn/sourceboot/`: `t2_22-before-j12`,
`t2_22-after-j12`, `t2_22-after-j1`. Artifacts and full timestamped build logs
under `releases/2026-08-17_t2_22-build-relink-loop/`.
