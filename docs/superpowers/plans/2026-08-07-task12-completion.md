# Task 12 Completion Plan (real seq00, closure-selected audio bundles, S64P bindings)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Unblock governing-plan Task 12 (`docs/superpowers/plans/2026-08-05-saturn-full-game-completeness-parallel-optimization.md:681-717`, currently blocked/source-incomplete): generate the real expanded sequence-00 payload without requiring a PC game build, replace the packager's heuristic m64 scan with a real decode-walk validator, derive BOB's resident audio bundle from Task 3's authoritative closure instead of the hardcoded music-only selection, and emit the S64P `AUDIO_DEPENDENCIES` payload hashes/byte counts/scratch limits that Task 22's linker step consumes.

**Architecture:** Everything ships through the existing, review-hardened `tools/saturn/saturn_audio_package.py` (440 lines; CLI `compile_saturn_audio.py`; make target `Makefile.saturn.mk:274-276` with a GREEN-twice determinism gate at :278-288). This plan adds four bounded capabilities to that pipeline, in dependency order: seq00 generation (removes the fail-closed inventory block), decode-walk validation (replaces the byte-scan heuristic that the last review rejected), closure ingestion (joins `collect_scene_closure.py`'s per-record SFX/music declarations to banks/instruments/samples), and programmatic S64P dependency emission (the compiler-owned path `compile_scene_package.py` already enforces). Strictly data + acceptance ABI — no VM semantics (Task 15 owns interpretation), no SCSP (Task 17), no sourceboot loading (Task 21).

**Tech Stack:** Python 3 (stdlib only, matching every `tools/saturn/*.py`), GNU as/objcopy for sequence assembly (already toolchain-present), unittest with committed synthetic fixtures (never Nintendo bytes).

**Sequencing vs. sibling plans:** Fully independent of `2026-08-07-task14-completion.md` and `2026-08-07-task16-completion.md` — host-tooling only, can run in parallel with either. Task 22 consumes this plan's outputs (plan:1604: "Task 22 invokes the generic linker only after final ... Task 12 audio payload hashes/byte counts/scratch limits exist"; :1607: RED rejects pre-Task-12 hashes).

**Current ground truth (2026-08-07 research pass, all verified):**
- Fail-closed block: `source_inventory` (`saturn_audio_package.py:135-148`) raises "missing expanded sequence-00 payload… (the 338-byte sound_data.c wrapper is not package data)" when `sound/sequences.bin.inc.c` is absent or ≤1024 bytes (:139-142; second guard :194-195). That file is a PC-build product (`Makefile:761-763`, `assemble_sound.py --sequences`; seq00 itself assembled from committed `sound/sequences/00_sound_player.s` via as+objcopy at `Makefile:350-356/771-773`) that never lands in `sound/` — a missing generation step, not broken source data. Plan:699 forbids requiring the PC build.
- Text/binary seam: the real `sequences.bin.inc.c` is comma-separated C text, but `_load_sequences` reads raw file bytes (:171) and the tests model binary (`test_compile_saturn_audio.py:26` uses a synthetic 2048-byte blob). Resolving this seam is part of "done."
- All raw inputs exist on disk: 34 US `.m64`s + 219 `.aiff`s (ROM-extracted via `assets.json`), 38 `sound_banks/*.json`, committed `00_sound_player.s`, `sequences.json`. Verdict: tooling gap, not source-data gap.
- Closure gap: `collect_scene_closure.py` already emits authoritative per-record `sfx_ids`/`sfx_banks`/`music_sequence_ids` (`:791-819` `_behavior_sounds`, called :1012; `_sound_declarations` :656-663 parsing `include/sounds.h`) — verified in `build/saturn/packages/bob/1/closure.json`: 86 records, 54 distinct SFX IDs, 8 banks, music `SEQ_LEVEL_GRASS`. But `saturn_audio_package.py` never reads it: closures are hardcoded `_closure("bob"/"wf", [3], …)` at :358-359, producing a music-only `bob_audio_closure.json` (bank `'22'`, 16 samples, 6 sfx_mappings). Do not conflate the two same-named "closure" artifacts.
- m64 heuristic: `_load_sequences` (:174-195) does a raw byte scan for 0xFB/0xFC after offset 128, rejecting only literal `FF FF` targets; its own comment (:188-193) concedes it avoids real decoding. Unhandled: 0xFF end reachability, 0xFD/0xFE delays, 0xF5/0xF9/0xFA conditionals, 0xF8/0xF7 loops, 0xF2-0xF4 relative branches, 0x90-family channel-pointer tables, in-range target validation. Task 15's C VM (`src/port/saturn/audio68k/sequence_vm.c`, 567 lines, :138-344/:363-490, US + EU/SH formats, rereview PASS at `b004fe7b`) is the porting reference for a host decoder.
- S64P path: `compile_scene_package.py` accepts dependency sections only programmatically ("dependency sections are compiler-owned", :206; `compile_package` :196-199); the provisional recipe passes none (`Makefile.saturn.mk:1312-1320`). `validate_audio_dependency` (:416-426) and manifest `s64p_audio_dependencies` (:401-407) exist but carry music-only hashes. Task 22's dependency-view fields are `byte_count`/`maximum_scratch`/`content_sha256`/`generation` (`src/port/saturn/runtime/saturn_scene_package.h:51-62`, kind 7 `AUDIO_DEPENDENCIES` at :25).
- Stale-artifact warning: the on-disk `build/saturn/audio/generated/AUDIO.DAT` (Aug 5) predates the `b06382c8` seq00 guard and will NOT regenerate under the current tool — treat it as invalid, regenerate everything in Task 5.
- Commit trail context for reviewers: `b2da692d` → repairs → `ac3b91b2`, follow-up `b937456e` (ledger `progress.md:199-200`).

---

### Task 1: Standalone seq00 generation (removes the fail-closed block)

**Files:**
- Create: `tools/saturn/gen_sequence_bank.py`
- Create: `tools/saturn/test_gen_sequence_bank.py`
- Modify: `tools/saturn/saturn_audio_package.py` (inventory/consumption seam only)
- Modify: `Makefile.saturn.mk` (new `compile-audio-sequences` step wired before `compile-saturn-audio`)

- [ ] **Step 1: Read the real assembly path first**

Read `Makefile:350-356` and `:761-773` (how the PC build assembles `00_sound_player.s` with as+objcopy and how `assemble_sound.py --sequences` concatenates seq00 + the 34 `.m64`s into `sequences.bin` with its index table), and `assemble_sound.py` itself for the exact layout (entry count, offset table format, alignment). The generator must reproduce those exact bytes standalone.

- [ ] **Step 2: RED tests**

`test_gen_sequence_bank.py` (unittest, synthetic fixtures only — construct a tiny fake `.s`-assembled blob and two fake `.m64`s in a temp dir, never real Nintendo bytes in the repo): output byte-stability across two runs; index table entries match input count/order/offsets/alignment per the layout read in Step 1; missing input file fails closed with a named error; output size > 1024 for any real-shaped input set (the packager's guard threshold). Run; RED (module doesn't exist).

- [ ] **Step 3: Implement the generator**

`gen_sequence_bank.py`: assemble `sound/sequences/00_sound_player.s` via the pinned toolchain's `as` + `objcopy` (discover the binaries the same way `Makefile.saturn.mk`'s existing toolchain checks do — do not hardcode paths), concatenate with the US `.m64` set per `sequences.json` order, emit `build/saturn/audio/generated/sequences.bin` (raw binary — this resolves the text/binary seam in favor of raw bytes) plus a JSON manifest (per-sequence offsets/sizes/SHA-256). No PC-build dependency.

- [ ] **Step 4: Repoint the packager's seam**

Change `saturn_audio_package.py`'s inventory (:135-148) and `_load_sequences` seq00 substitution (:166-171) to consume the generated raw `sequences.bin` (path passed via a new `--sequences-bin` argument; keep the old `sound/sequences.bin.inc.c` acceptance as a fallback so existing tests stay meaningful, but the guard message now names the generator as the fix). Update the packager's tests for the new argument. Wire `compile-audio-sequences` into `Makefile.saturn.mk` as a prerequisite of `compile-saturn-audio`.

- [ ] **Step 5: Verify, commit, review**

`python -m unittest test_gen_sequence_bank test_compile_saturn_audio -v` all green; run the real end-to-end `compile-saturn-audio` make target and confirm the inventory guard passes with real repo inputs. Commit `feat(saturn): generate expanded sequence bank standalone for audio packaging` (CHANGELOG in same commit — the repo's changelog-guard hook enforces this). Two-stage review.

---

### Task 2: Real m64 decode-walk validation

**Files:**
- Create: `tools/saturn/m64_decode_walk.py`
- Create: `tools/saturn/test_m64_decode_walk.py`
- Modify: `tools/saturn/saturn_audio_package.py` (:174-195, replace the heuristic's role)

- [ ] **Step 1: Port the traversal, not the semantics**

Read `sequence_vm.c:138-344` (sequence/channel control flow) and `:363-490` (layer + US vs EU/SH format selection). Write `m64_decode_walk.py` as a validation-only reachability walker: decode from offset 0, follow branches/loops/calls with a visited-set and bounded work budget, validate every branch/call/pointer-table target lands in-range and on a decoded boundary, flag unknown opcodes, verify 0xFF end reachability on every reachable path. It emits (ok, findings[]) — it does NOT interpret timing/voices (Task 15's job).

- [ ] **Step 2: RED fixtures per defect class**

Synthetic fixtures: out-of-range branch target; infinite loop without budget exhaustion handling (walker must terminate and report); truncated mid-opcode; 0x90-family channel pointer past EOF; valid minimal sequence (must pass). Plus the existing `\xfc\xff\xff` fixture must still be caught. Run; RED.

- [ ] **Step 3: Integrate**

`_load_sequences` runs the walker on every sequence payload (including generated seq00); any finding fails packaging closed with the sequence name and offset. Keep the cheap prefilter if it's free, but the walker is now the authority. Full packager suite green.

- [ ] **Step 4: Commit + review**

`feat(saturn): validate m64 sequences by decode walk in audio packaging` + CHANGELOG. Two-stage review — reviewer must independently spot-check the walker against `sequence_vm.c`'s opcode tables, not trust the port.

---

### Task 3: Closure-derived resident bundles

**Files:**
- Modify: `tools/saturn/saturn_audio_package.py` (`_closure` :358-359, `_sfx_mappings` :242-254, bundle assembly)
- Modify: `tools/saturn/test_compile_saturn_audio.py`
- Modify: `Makefile.saturn.mk` (pass `--closure build/saturn/packages/bob/1/closure.json`)

- [ ] **Step 1: RED**

New tests with a synthetic closure JSON (matching `collect_scene_closure.py`'s real output schema — read it first): the resident bundle must include exactly the union of (a) every closure-declared SFX ID's bank/instrument/sample chain resolved through `_sfx_mappings`, and (b) the closure's `music_sequence_ids` sequences — nothing more (a bank absent from the closure must be absent from the bundle), deterministically ordered. A closure SFX ID with no resolvable mapping fails closed with the ID named. Run; RED.

- [ ] **Step 2: Implement `--closure` ingestion**

Replace the hardcoded `_closure("bob", [3], …)` selection with closure-file-driven selection when `--closure` is passed (keep the hardcoded music-only path only as the no-argument fallback for WF until its closure exists — and record that asymmetry in the manifest). Join: closure `sfx_ids` → `include/sounds.h` declarations (already parsed by the closure generator — reuse its bank/ID split convention) → `sound_banks/*.json` instruments → sample records.

- [ ] **Step 3: Verify against the real BOB closure**

Run end-to-end with the real `build/saturn/packages/bob/1/closure.json` (86 records / 54 SFX / 8 banks): the generated `bob_audio_closure.json` must now cover all 8 closure banks (vs. today's music-only single bank), and GREEN-twice determinism (`Makefile.saturn.mk:278-288`) must pass. Commit `feat(saturn): derive BOB resident audio bundle from scene closure` + CHANGELOG; review.

---

### Task 4: S64P AUDIO_DEPENDENCIES emission

**Files:**
- Modify: `tools/saturn/saturn_audio_package.py` (dependency-payload emission; `validate_audio_dependency` :416-426)
- Modify: `tools/saturn/compile_scene_package.py` (programmatic dependency intake — compiler-owned per its :206 policy)
- Modify: `Makefile.saturn.mk:1312-1320` (provisional recipe gains the audio dependency input)
- Tests: extend both tools' suites

- [ ] **Step 1: Read the binding contract**

`saturn_scene_package.h:25` (kind 7) and `:51-62` (`byte_count`/`maximum_scratch`/`content_sha256`/`generation`) — the exact four fields Task 22's RED gate checks (plan:1607). The Python sealer's dependency descriptor format (96 bytes/descriptor per the package header constants) must be emitted to match the C validator bit-for-bit — the existing C/Python ABI parity tests from the `ac3b91b2` wave are the pattern.

- [ ] **Step 2: RED, implement, verify**

RED: the packager emits an AUDIO_DEPENDENCIES payload whose per-chunk hashes/byte counts/scratch limits round-trip through `compile_scene_package.py` → `scene_package_validate()`'s host test harness; mutations (changed byte count, stale generation, reordered chunks) fail closed. Implement the emission from Task 3's closure-selected bundle (full-coverage hashes, replacing the music-only ones at :401-407); wire the provisional recipe. GREEN both suites + determinism gate.

- [ ] **Step 3: Commit + review**

`feat(saturn): bind closure-selected audio dependencies into S64P sealing` + CHANGELOG. Two-stage review with the C-side validator test in the review loop.

---

### Task 5: Fresh artifacts, determinism, reconciliation

- [ ] **Step 1:** Delete/ignore the stale pre-guard `build/saturn/audio/generated/*` outputs; regenerate everything end-to-end via the make targets; GREEN-twice determinism gate passes; record the four output files' hashes (plan:704-708's list) in a short evidence report under `docs/saturn/evidence/reports/`.
- [ ] **Step 2:** Reconcile: governing-plan Task 12 checklist items that the evidence actually proves (:698-717), ledger entry lifting the blocked status to source-complete with the explicitly-open gates named (no target/Ymir/audible claim — those are Tasks 21/23), CHANGELOG.
- [ ] **Step 3:** Final whole-diff independent review, then:

```bash
git add docs/ .superpowers/ CHANGELOG.md
git commit -m "docs(saturn): reconcile task 12 completion evidence and ledger"
```
