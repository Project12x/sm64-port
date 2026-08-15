# Sprint 1 (R0+R1): Recovery, Baseline, and Audible Audio — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve every unlanded piece of the donor worktree on a new `saturn/recovery` branch, then produce one owner-accepted CUE at A9A feature parity with real audio: non-regressed Mario/BOB visuals, looping BOB music through the proven MC68000/SCSP driver (hardware loop bit, no sequence VM), one game-triggered SFX, ≥4 FPS.

**Architecture:** Branch from donor HEAD `b2447f67` (keeps the A9A renderer lineage and the DIVU fix), transplant the dirty state via patch + explicit copy list, then make five bounded audio changes (driver diet, looped-sample music, boot resilience) and one renderer revert (hot state back to HWRAM). Feature flags: `COMPLETE_MARIO_ANIMATION=0`, `DYNAMIC_ACTOR_CLOSURE=0`, `SEMANTIC_AUDIO=1`.

**Tech Stack:** SH-2 GCC 14.3.0 (Yaul), MC68000 gcc (poneSound toolchain), Python 3 (`.venv-saturn-tools`), GNU Make ≥4.3 via `tools/saturn/with-msys-toolchain.ps1`, Ymir (desktop + headless).

**Governing docs:** program charter `2026-08-14-saturn-shaped-port-program.md`; constitution rules in `docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md` §"Operating contract". **Stop rule: two implementation attempts or two hours without a new live result → revert, bypass, or smaller transplant. Never a new abstraction.**

**Owner inputs required before Task 10 (Tasks 1–9 do not need it):** a WAV render of the BOB theme (SM64 seq 3, "SEQ_LEVEL_GRASS"), any sample rate, from the owner's own ROM-derived audio (emulator recording is fine). Place at `<recovery-worktree>/bob_theme.us.wav`. It is gitignored like `baserom.us.z64` and never committed. Target: a SHORT loopable motif — the SCSP's 16-bit loop-end addressing caps a looped PCM8 sample at 65,535 bytes, i.e. **~8.2 s at 8 kHz** (~10.9 s at 6 kHz). Supply any length; `wav_to_pcm8.py` trims to the cap automatically — but a section composed to loop at that length will sound far better than an arbitrary cut. Full-length music arrives with CD-DA in Phase S3.

**Paths used throughout:**
- `DONOR` = `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge`
- `REC` = `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery`
- All git commands against the donor are read-only (`diff`, `status`, `show`). Never `add`/`checkout`/`clean` in `DONOR`.

---

### Task 1: Create the recovery worktree and land the plan documents

**Files:**
- Create: git worktree `REC` on new branch `saturn/recovery` at `b2447f67`
- Create: `REC/docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md` (copy from DONOR)
- Create: `REC/docs/superpowers/plans/2026-08-14-sprint1-recovery-baseline-audio.md` (copy from DONOR)

- [ ] **Step 1: Create the worktree** (from the repo root, NOT from inside DONOR)

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port
git worktree add .worktrees/saturn-recovery -b saturn/recovery b2447f6715bf64d34be4391298f3746db919aa6f
```

Expected: `Preparing worktree (new branch 'saturn/recovery')`. Path contains `sm64-port` (charter D10) — do not relocate it.

- [ ] **Step 2: Verify the worktree is clean and at the right commit**

```bash
cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/saturn-recovery
git status --porcelain | wc -l   # expected: 0
git rev-parse HEAD                # expected: b2447f6715bf64d34be4391298f3746db919aa6f
```

- [ ] **Step 3: Copy the two plan docs from DONOR and commit**

```bash
cp "$DONOR/docs/superpowers/plans/2026-08-14-saturn-shaped-port-program.md" docs/superpowers/plans/
cp "$DONOR/docs/superpowers/plans/2026-08-14-sprint1-recovery-baseline-audio.md" docs/superpowers/plans/
git add docs/superpowers/plans/2026-08-14-*.md
git commit -m "docs: add saturn-shaped port program charter and sprint 1 plan"
```

---

### Task 2: Transplant the donor's dirty state, curated

**Files:**
- Create: `C:\Users\estee\AppData\Local\Temp\claude\...\scratchpad\donor-tracked.patch` (scratch, not committed)
- Modify (in REC, via patch): all 117 tracked-modified donor files
- Create (in REC, via copy): the untracked keep-list below

- [ ] **Step 1: Export the donor's tracked modifications as one patch** (read-only against DONOR)

```bash
cd "$DONOR"
git diff > /c/Users/estee/AppData/Local/Temp/claude/donor-tracked-2026-08-14.patch
wc -l /c/Users/estee/AppData/Local/Temp/claude/donor-tracked-2026-08-14.patch
```

Expected: several thousand lines. Do not stage anything in DONOR.

- [ ] **Step 2: Apply to REC and verify**

```bash
cd "$REC"
git apply --check /c/Users/estee/AppData/Local/Temp/claude/donor-tracked-2026-08-14.patch   # expected: silence
git apply /c/Users/estee/AppData/Local/Temp/claude/donor-tracked-2026-08-14.patch
git status --porcelain | wc -l   # expected: ~117 modified
```

Both trees are at the same commit, so the patch must apply cleanly. If it does not, STOP — do not force; report the reject hunks.

- [ ] **Step 3: Copy the untracked keep-list** (every file below has ZERO commits in git — this is the preservation payload)

```bash
cd "$REC"
for f in \
  src/port/saturn/sourceboot/source_audio_live.c \
  src/port/saturn/sourceboot/source_audio_live.h \
  src/port/saturn/sourceboot/source_audio_semantics.h \
  src/port/saturn/platform/saturn_cart_code.h \
  tools/saturn/compile_sourceboot_sfx_bundle.py \
  tools/saturn/test_compile_sourceboot_sfx_bundle.py \
  tools/saturn/source_audio_live_test.c \
  tools/saturn/vdp1_painter_chain_test.c \
  tools/saturn/test_actor_live_memory_contract.py \
  tools/saturn/test_sourceboot_cold_stage_return.py \
  tools/saturn/test_sourceboot_stack_contract.py \
  docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md \
  docs/superpowers/plans/2026-08-01-role3-lakitu-publication.md \
  docs/superpowers/plans/2026-08-01-role3-visual-capture.md \
  docs/superpowers/plans/2026-08-01-sh2-indirect-transfer-audit-gate.md \
  docs/superpowers/plans/2026-08-06-saturn-hud-vdp2.md \
  docs/superpowers/plans/2026-08-07-saturn-vdp2-clut.md \
  docs/superpowers/plans/2026-08-07-task12-completion.md \
  task-11-generic-actor-report.md \
; do mkdir -p "$(dirname "$f")"; cp "$DONOR/$f" "$f"; done
cp -r "$DONOR/docs/saturn/evidence/reports/." docs/saturn/evidence/reports/ 2>/dev/null || true
cp -r "$DONOR/audits" audits
cp "$DONOR/.tmp-audio-probe-current.py" tools/saturn/probe_audio_mailbox.py
```

Explicitly EXCLUDED (generated/scratch, ~105 MB — never copy): `.tmp-*`, `review-task*.diff`, `tools/saturn/current-*-video*/`, loose `tools/saturn/*.json|*.png|*.raw` captures, `.ymir-profile/`, `build/`, `.tmp-upstream-sm64-psx/`.

- [ ] **Step 4: Verify the zero-history files arrived**

```bash
ls -la src/port/saturn/sourceboot/source_audio_live.c src/port/saturn/platform/saturn_cart_code.h tools/saturn/compile_sourceboot_sfx_bundle.py
```

Expected: all present, non-zero size.

- [ ] **Step 5: Commit in curated clusters** (order matters for later revertability; each commit message notes provenance)

```bash
git add src/port/saturn/audio src/port/saturn/audio68k src/port/saturn/soundtest \
        src/port/saturn/sourceboot/source_audio_live.c src/port/saturn/sourceboot/source_audio_live.h \
        src/port/saturn/sourceboot/source_audio_semantics.c src/port/saturn/sourceboot/source_audio_semantics.h \
        tools/saturn/pcm68k_model_test.c tools/saturn/test_full_game_audio_source.py \
        tools/saturn/compile_sourceboot_sfx_bundle.py tools/saturn/test_compile_sourceboot_sfx_bundle.py \
        tools/saturn/source_audio_live_test.c tools/saturn/soundtest_boot_contract_test.c \
        tools/saturn/probe_audio_mailbox.py docs/saturn/audio/PCM68K_PROVENANCE.md
git commit -m "feat(audio): transplant unlanded audio integration from donor worktree

Donor: sh2/native-math-purge working tree @ b2447f67 (2026-08-14).
Includes the SCSP write-only-latch readback fix (saturn_sound_cpu.c),
the SFXB bundle ABI + packager, the source_audio_live bridge, and the
semantics workspace refactor. Probationary: contains the known-broken
VM/fallback paths, dieted in the next commits."

git add src/port/saturn/sourceboot/sourceboot-cart.x src/port/saturn/platform/saturn_cart_code.h \
        src/game/camera.c src/engine/surface_load.c src/game/area.c src/game/game_init.c \
        src/game/level_update.c src/game/mario.c src/game/object_list_processor.c src/game/save_file.c \
        tools/saturn/test_actor_live_memory_contract.py tools/saturn/test_sourceboot_cold_stage_return.py \
        tools/saturn/test_sourceboot_stack_contract.py
git commit -m "feat(memory): transplant cart-cold/LWRAM memory-relief refactor from donor"

git add src/port/saturn/gfx src/port/saturn/runtime src/game/rendering_graph_node.c \
        tools/saturn/vdp1_painter_chain_test.c tools/saturn/test_render_snapshot_source.py \
        tools/saturn/test_vdp1_frame_bank_source.py tools/saturn/saturn_hud_layout_test.c
git commit -m "feat(render): transplant renderer/painter-chain/HUD/actor-runtime dirty state from donor

HAZARD (for Sprint 2): rendering_graph_node.c now hard-fails
saturn_actor_identity_registry_apply — unregistered actors are silently
dropped. Reconcile with regenerated packages before enabling actors."

git add src/port/saturn/sourceboot/main.c src/port/saturn/sourceboot/Makefile Makefile.saturn.mk
git commit -m "feat(build): transplant sourceboot main + build wiring from donor

main.c carries three overlapping efforts (memory relief, HUD publish
change, audio live wiring) that cannot be split file-wise."

git add -A
git commit -m "chore: transplant remaining donor state (actor-bank v2 tools, release tooling, plans, ledgers, evidence, audits)"
```

- [ ] **Step 6: Verify complete transfer**

```bash
git status --porcelain | wc -l    # expected: 0
git log --oneline b2447f67..HEAD  # expected: 6-7 commits
```

Then confirm the transplant matches the donor byte-for-byte for tracked files:

```bash
cd "$DONOR" && git diff > /tmp/a.patch
cd "$REC"  && git diff b2447f6715bf64d34be4391298f3746db919aa6f -- $(cd "$DONOR" && git diff --name-only) > /tmp/b.patch
diff <(grep '^[+-]' /tmp/a.patch) <(grep '^[+-]' /tmp/b.patch) | head -5
```

Expected: no output (identical change content).

---

### Task 3: Restore the constitution (`AGENTS.md`)

**Files:**
- Create: `REC/AGENTS.md`

- [ ] **Step 1: Read the condensed copy** at `REC/HOWTO.md` (39 lines) and the operating contract in `docs/saturn/PRODUCT_RECOVERY_HANDOFF_2026-08-14.md` lines 458-492.

- [ ] **Step 2: Write `AGENTS.md`** containing, verbatim from those two sources: the product-gate definition (owner-observed CUE is the only milestone), the 7-step loop (regression test → bounded change → unique build → hash+profile binding → observe after gameplay renders → record facts → keep/revert), the two-attempt/two-hour stop rule with the three allowed responses, the CUE-hash warning (identify by identity/ELF/ISO tuple), the testing budget, and the fail-open rules (audio failure mutes audio only; unsupported content skips at the smallest object). Add one new line: "The 4 FPS floor blocks any retained change; measure with capture_sourceboot_throughput.py."

- [ ] **Step 3: Commit**

```bash
git add AGENTS.md
git commit -m "docs: restore AGENTS.md product-gate constitution (was referenced by the handoff but missing from every tree)"
```

---

### Task 4: 68K driver diet — remove the VM from the image, fix the stack

The proven driver was 2,918 B with a ~150 B state struct on a 1,020 B stack. The dirty tree links the VM/allocator and puts a ~2 KB state struct on that stack — the root cause of failure `0x0340`. This task removes the VM objects and moves the state to `.bss`.

**Files:**
- Modify: `REC/src/port/saturn/audio68k/Makefile`
- Modify: `REC/src/port/saturn/audio68k/pcm_voice.h`
- Modify: `REC/src/port/saturn/audio68k/pcm_voice.c`
- Modify: `REC/src/port/saturn/audio68k/main.c`
- Test: `REC/tools/saturn/pcm68k_model_test.c`

**Read first (identifier verification is mandatory, plan-code was drafted from audit notes):** `pcm_voice.h` (state struct), `pcm_voice.c` lines ~39-60 (defines), ~300-530 (music_start/music_service/fallback), ~842+ (SEQ_START case), `main.c` (`pcm68k_main`), `Makefile` (`OBJECTS :=` line).

- [ ] **Step 1: Write the failing host test** — append to `tools/saturn/pcm68k_model_test.c`:

```c
static void test_voice_state_fits_reserved_stack(void) {
    /* linker.ld reserves 0x3C00..0x3FFC (1,020 bytes). The state no longer
       lives on the stack, but keep it small enough that moving it back
       could never overflow again. */
    assert(sizeof(sm64_saturn_pcm_voice_state_t) <= 768);
}
```

Register it in the test `main()` alongside the existing tests.

- [ ] **Step 2: Run it to verify it fails** (host build target already exists)

```bash
cd "$REC" && powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-pcm68k-model
```

Expected: FAIL (struct currently ~2 KB with the embedded `sequence_vm_t` + `audio_engine_t`).

- [ ] **Step 3: Diet the header.** In `pcm_voice.h`: delete the `sequence_vm_t` and `audio_engine_t` members and every `music_fallback_*` / VM-tick counter; keep `music_active`, `music_starts`, `music_slot`, `music_reject_mask`, the SFXB generation field, and the four PCM voices. Delete the includes of `sequence_vm.h` / `audio_engine.h` that the removed members needed.

- [ ] **Step 4: Diet the source.** In `pcm_voice.c`: delete `#define SM64_SATURN_PCM_MUSIC_DIRECT_FALLBACK`, the `music_start`/`music_service` retrigger implementation, and every call into `sequence_vm_*` / `audio_engine_*`. Keep untouched: `sfx_bundle_view`, `sfx_sample_descriptor`, the semantic volume/pan quantizers, `play_semantic`, the `PLAY_REFRESH` case, and the diagnostic-mailbox publisher. Create a small helper `pcm_music_key_off(state)` (keys off slot 0, clears `music_active`) and leave the `SEQ_START` case temporarily as `pcm_music_key_off(state); break;` (Task 6 rewrites it; same helper name there).

- [ ] **Step 5: Move the state off the stack.** In `main.c`, change the local

```c
sm64_saturn_pcm_voice_state_t voice_state;
```

to a file-scope

```c
static sm64_saturn_pcm_voice_state_t s_voice_state;
```

and pass `&s_voice_state` where `&voice_state` was passed. Add next to it:

```c
_Static_assert(sizeof(sm64_saturn_pcm_voice_state_t) <= 768,
               "voice state must stay far below the 1020-byte reserved stack");
```

- [ ] **Step 6: Remove the VM objects from the image.** In `audio68k/Makefile`, change the `OBJECTS :=` list to exactly `start main pcm_voice scsp_pcm8` plus `audio_freestanding` **only if** the link then fails on missing helper symbols (division/modulo helpers). Do not remove `-DSM64_SATURN_PCM_MAPPED_ZERO=1` or other flags. `sequence_vm.c`, `audio_engine.c`, `voice_allocator.c`, `desired_voice.c`, `slot_shadow.c`, `scsp_timer.c` stay in the tree (banked), out of the image.

- [ ] **Step 7: Rebuild driver + run host tests**

```bash
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk pcm68k-image verify-pcm68k-model
```

Expected: PASS; the printed image size drops well below the dirty tree's 13,520 B. The existing `verify_pcm68k_image.py` static check (`__driver_end <= __stack_bottom`) must pass with margin.

- [ ] **Step 8: Commit**

```bash
git add src/port/saturn/audio68k tools/saturn/pcm68k_model_test.c
git commit -m "fix(audio): remove sequence VM from 68K image and move voice state off the 1KB stack

Root cause of failure 0x0340: ~2KB of state (20-voice allocator alone
1,760B) as a stack local against a 1,020B reserved stack, corrupting
note bindings. Secondary defect (channel scripts decoded as layer
scripts) is mooted by removing the VM from the image; VM sources remain
in-tree, banked. CHANGELOG updated."
```

Include the CHANGELOG.md entry (root cause + consumer impact) in this same commit.

---

### Task 5: Music as a looped SFXB sample (packager + validator + WAV tool)

**Files:**
- Create: `REC/tools/saturn/wav_to_pcm8.py`
- Modify: `REC/tools/saturn/compile_sourceboot_sfx_bundle.py`
- Modify: `REC/src/port/saturn/audio68k/pcm_voice.c` (validator relax only)
- Modify: `REC/.gitignore`
- Test: `REC/tools/saturn/test_compile_sourceboot_sfx_bundle.py`

- [ ] **Step 1: Write `tools/saturn/wav_to_pcm8.py`** (complete file):

```python
#!/usr/bin/env python3
"""Convert a WAV file to raw signed 8-bit mono PCM for the SCSP.

Usage: wav_to_pcm8.py IN.wav OUT.pcm8 --rate 8000 [--max-seconds 28]
Owner-provided WAVs are ROM-derived and must never be committed.
"""
import argparse, struct, sys, wave

def load_wav_mono_float(path):
    with wave.open(path, "rb") as w:
        ch, width, rate, n = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
        raw = w.readframes(n)
    if width == 2:
        samples = struct.unpack("<%dh" % (n * ch), raw)
        scale = 32768.0
    elif width == 1:
        samples = [b - 128 for b in raw]
        scale = 128.0
    else:
        raise SystemExit("unsupported sample width: %d" % width)
    mono = [sum(samples[i * ch:(i + 1) * ch]) / (ch * scale) for i in range(n)]
    return mono, rate

def resample_linear(mono, src_rate, dst_rate):
    if src_rate == dst_rate:
        return list(mono)
    out_n = int(len(mono) * dst_rate / src_rate)
    out = []
    for i in range(out_n):
        pos = i * src_rate / dst_rate
        j = int(pos)
        frac = pos - j
        a = mono[j]
        b = mono[j + 1] if j + 1 < len(mono) else a
        out.append(a + (b - a) * frac)
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input"); ap.add_argument("output")
    ap.add_argument("--rate", type=int, default=8000)
    ap.add_argument("--max-seconds", type=float, default=28.0)
    args = ap.parse_args()
    mono, src_rate = load_wav_mono_float(args.input)
    out = resample_linear(mono, src_rate, args.rate)
    limit = int(args.rate * args.max_seconds)
    if len(out) > limit:
        out = out[:limit]
        print("note: trimmed to %.1f s" % args.max_seconds, file=sys.stderr)
    data = bytes((max(-128, min(127, round(s * 127.0))) & 0xFF) for s in out)
    with open(args.output, "wb") as f:
        f.write(data)
    print("%s: %d bytes @ %d Hz (%.1f s)" % (args.output, len(data), args.rate, len(data) / args.rate))

if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Write the failing packager test** — add to `test_compile_sourceboot_sfx_bundle.py` a test that builds a bundle with `--music-pcm <tmpfile> --music-rate 8000` and asserts: the music sample row's `flags` field equals `1` (loop), the trailer's `music_sample_index` points at it, and `music_sequence_bytes == 0` (no m64 payload).

- [ ] **Step 3: Run to verify it fails**

```bash
"$REC/.venv-saturn-tools/Scripts/python.exe" -m pytest tools/saturn/test_compile_sourceboot_sfx_bundle.py -v
```

Expected: FAIL (`--music-pcm` unknown / flags currently 0 / m64 trailer appended).

- [ ] **Step 4: Modify the packager.** In `compile_sourceboot_sfx_bundle.py`: add `--music-pcm` (raw PCM8 path) and `--music-rate` args; when given, append the PCM as the final sample row with `flags = 1` (`SM64_SATURN_PCM_SAMPLE_LOOP`), set trailer `music_sample_index` to that row, and set `music_sequence_offset = music_sequence_bytes = 0` (delete the m64-append branch). Also delete the dead `music_sample_index = len(sample_rows) if "sample_rows" in locals() else 0` line (audit-flagged dead code). Enforce the budget: total metadata+PCM must fit the driver reserve and bank (`0x5000` metadata base, `0x8000` PCM base, 491,520 B sound RAM) — the packager already validates sizes; extend the error message to state the music trim option.

- [ ] **Step 5: Relax the 68K validator by one bit.** In `pcm_voice.c`'s bundle validation (audit: line ~178), change

```c
if (flags != 0U) { return reject; }
```

to

```c
if ((flags & ~SM64_SATURN_PCM_SAMPLE_LOOP) != 0U) { return reject; }
```

(verify the actual macro name in `saturn_pcm_protocol.h`; it is the same flag `scsp_pcm8.c:132` already consumes to set `SCSP_LOOP_NORMAL`).

- [ ] **Step 6: Run packager tests + host model**

```bash
"$REC/.venv-saturn-tools/Scripts/python.exe" -m pytest tools/saturn/test_compile_sourceboot_sfx_bundle.py -v
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-pcm68k-model
```

Expected: PASS.

- [ ] **Step 7: Gitignore the owner asset + commit**

```bash
printf '\n# owner-provided ROM-derived audio, never committed\nbob_theme.us.wav\n*.pcm8\n' >> .gitignore
git add tools/saturn/wav_to_pcm8.py tools/saturn/compile_sourceboot_sfx_bundle.py \
        tools/saturn/test_compile_sourceboot_sfx_bundle.py src/port/saturn/audio68k/pcm_voice.c .gitignore
git commit -m "feat(audio): music as looped SFXB sample — wav_to_pcm8 tool, packager --music-pcm, loop-bit validator

Replaces the m64 trailer + 240Hz retrigger fallback (which assumed a
timer that does not exist) with the SCSP hardware loop bit the driver
already supports. CHANGELOG updated."
```

---

### Task 6: SEQ_START/SEQ_STOP drive the looped sample; SFX slots pinned

**Files:**
- Modify: `REC/src/port/saturn/audio68k/pcm_voice.c`
- Test: `REC/tools/saturn/pcm68k_model_test.c`

- [ ] **Step 1: Write the failing host tests** — replace the dirty tree's `test_semantic_sequence_start_reaches_scsp` (it asserted the deleted fallback) with:

```c
static void test_seq_start_starts_looped_music_voice(void) {
    /* publish a bundle whose trailer names a looped music row, send
       SEQ_START with seq id 3, assert: music_active==1, the music slot's
       SCSP words carry the loop mode, music_starts==1. */
}
static void test_seq_stop_keys_off_music(void) {
    /* after the above, send SEQ_STOP; assert music_active==0 and the
       slot was keyed off. */
}
static void test_sfx_uses_slots_1_to_3_while_music_holds_slot_0(void) {
    /* start music, then three PLAY_REFRESH SFX; assert none of them
       touched slot 0. */
}
```

Write the bodies against the existing test harness patterns in the same file (the harness already publishes bundles and inspects SCSP register writes — mirror `test_semantic_sfx_uses_validated_sound_ram_bundle`).

- [ ] **Step 2: Run to verify they fail** (`verify-pcm68k-model`): FAIL — SEQ_START currently stops music only (Task 4 stub).

- [ ] **Step 3: Implement the SEQ_START/SEQ_STOP handlers** in `pcm_voice.c` (verify identifier names against the file; behavior is normative):

```c
#define SM64_SATURN_PCM_MUSIC_SLOT 0U

case SM64_SATURN_PCM_CONTROL_SEQ_START: {
    /* words[1] = source sequence id from play_music(). Music is the
       bundle trailer's looped sample; unknown ids just stop music
       (silence is legal, a hang or reject storm is not). */
    pcm_music_key_off(state);
    if (words[1] == 3U /* SEQ_LEVEL_GRASS */) {
        const sample_descriptor_t *music = music_trailer_descriptor(state);
        if (music != NULL) {
            scsp_pcm8_start(SM64_SATURN_PCM_MUSIC_SLOT, music); /* loop bit in flags */
            state->music_active = 1U;
            state->music_starts += 1U;
        } else {
            state->music_reject_mask |= 1U;
        }
    }
    break;
}
case SM64_SATURN_PCM_CONTROL_SEQ_STOP:
    pcm_music_key_off(state);
    break;
```

`pcm_music_key_off` keys off slot 0 and clears `music_active`. Change the SFX round-robin from `slot = next_slot++ % 4` to `slot = 1U + (next_slot++ % 3U)` so music owns slot 0. Keep publishing `music_starts` / `music_active` / `music_reject_mask` to the diagnostic mailbox.

- [ ] **Step 4: Run host tests** (`verify-pcm68k-model`): PASS. Also update `test_full_game_audio_source.py`: delete the two assertions on the removed `MUSIC_DIRECT_FALLBACK` token; keep the `play_music(SEQ_PLAYER_LEVEL, SEQUENCE_ARGS(4, SEQ_LEVEL_GRASS), 0U)` source-presence assertion.

- [ ] **Step 5: Commit**

```bash
git add src/port/saturn/audio68k/pcm_voice.c tools/saturn/pcm68k_model_test.c tools/saturn/test_full_game_audio_source.py
git commit -m "feat(audio): SEQ_START/SEQ_STOP drive looped music on pinned slot 0, SFX on slots 1-3. CHANGELOG updated."
```

---

### Task 7: Audio boot failure must not hang the game

**Files:**
- Modify: `REC/src/port/saturn/sourceboot/main.c` (audit: ~lines 1993-1997 and 2013-2016)
- Test: `REC/tools/saturn/test_full_game_audio_source.py`

- [ ] **Step 1: Write the failing source-policy test** — add to `test_full_game_audio_source.py`:

```python
def test_audio_init_failure_does_not_hang():
    text = MAIN_C.read_text(encoding="utf-8")
    live_init = text[text.index("sourceboot_audio_init"):]
    assert "for(;;)" not in live_init.split("thread5_game_loop")[0], \
        "audio init failure must fall through to the silent stub, not spin"
```

(Adapt the slicing to the real function layout after reading the file.)

- [ ] **Step 2: Run it — FAIL** (both failure sites currently `dbgio_puts(...); for(;;){}`).

- [ ] **Step 3: Implement.** Replace both infinite loops with: log via `dbgio_puts`, set a boolean `s_audio_live_failed = true`, and continue boot with the semantic layer left unbound — the semantics module is already fail-closed when unbound (calls become no-ops), which is exactly the handoff's "audio failure mutes audio only." Verify by reading `source_audio_semantics.c` that unbound == silent no-op (the workspace-bind refactor made every entry fail closed).

- [ ] **Step 4: Run the test — PASS. Commit.**

```bash
git add src/port/saturn/sourceboot/main.c tools/saturn/test_full_game_audio_source.py
git commit -m "fix(audio): audio boot failure falls through to silent stub instead of hanging boot (handoff Phase B.6). CHANGELOG updated."
```

---

### Task 8: Renderer hot state back to HWRAM

**Files:**
- Modify: `REC/src/port/saturn/gfx/saturn_demo_render.c`

- [ ] **Step 1: Locate the eviction.** In `saturn_demo_render.c`, find the `DEMO_CPU_WORK_CACHE` macro (added by `91f02ffd`) and the `s_bob_hot_workarea` declaration. Confirm both currently carry `section(".lwram_bss")`.

- [ ] **Step 2: Revert placement.**

```c
/* was: #define DEMO_CPU_WORK_CACHE __attribute__((section(".lwram_bss"), aligned(16))) */
#define DEMO_CPU_WORK_CACHE __attribute__((aligned(16)))
```

and remove the `.lwram_bss` section attribute from `s_bob_hot_workarea` (keep `aligned(16)`). Rationale in a one-line comment: these are per-primitive inner-loop operands; 16-bit LWRAM versus 32-bit HWRAM was the mechanism behind the 5.29→~1 FPS collapse; the HWRAM pressure that forced the move is gone with animation/actors off.

- [ ] **Step 3: This change is validated by the Task 10 link + margin readback** (no host test exists for section placement; the linker asserts are the test). If the link in Task 10 fails on HWRAM with this revert, the fallback order is: (a) return only the per-primitive scratch arrays to HWRAM and leave `s_bob_hot_workarea` in LWRAM; (b) revert this task entirely and record the margin numbers. Never hand-shrink other subsystems to force it.

- [ ] **Step 4: Commit**

```bash
git add src/port/saturn/gfx/saturn_demo_render.c
git commit -m "perf(render): return demo-path hot working set to HWRAM (reverts 91f02ffd placement; mechanism of the ~1 FPS regression). CHANGELOG updated."
```

---

### Task 9: Wire the memory-map margin gate into the build loop + parameterize the audio probe

**Files:**
- Modify: `REC/tools/saturn/verify_sourceboot_memory_map.py`
- Modify: `REC/Makefile.saturn.mk`
- Modify: `REC/tools/saturn/probe_audio_mailbox.py`

**Added scope (Task 2 quality-review finding):** `probe_audio_mailbox.py` was promoted from donor scratch (`.tmp-audio-probe-current.py`) byte-for-byte and is not yet a real tool: it hardcodes absolute paths to `ymir-headless.exe`, the BIOS, and one stale artifact directory (`e2-bob-identity-id-7deb747eb230b595`); it runs everything as import-time side effects with no `main()` guard; and it carries a stale `sys.path.insert`. Before Task 11 depends on it: add argparse (`--ymir`, `--ipl`, `--cue` or `--artifact-dir`, `--output`), wrap execution in `main()` under `if __name__ == "__main__":`, delete the stale path insert, and mirror the CLI conventions of `capture_sourceboot_throughput.py`. Behavior of the mailbox/SCSP peek logic itself must not change.

- [ ] **Step 1: Add a plain `verify` mode.** The tool already contains `inspect_elf()` and `validate_layout()` (welded to a closed sprint's phase-chain CLI). Add an argparse subcommand:

```python
p_verify = subparsers.add_parser("verify", help="check one ELF's memory layout")
p_verify.add_argument("--elf", required=True)
p_verify.add_argument("--required-final-margin", type=lambda s: int(s, 0), default=0x1F00)
```

whose handler runs `inspect_elf` + `validate_layout` and prints `___end`, HWRAM remaining vs required, and LWRAM floor status, exiting nonzero on violation. Replace the hardcoded `YAUL_BIN = Path("D:/...")` with `Path(os.environ["YAUL_INSTALL_ROOT"]) / "bin"` (keep the old value as fallback with a warning).

- [ ] **Step 2: Add the make target** in `Makefile.saturn.mk`:

```make
.PHONY: verify-memory-map
verify-memory-map:
	$(SATURN_TOOLS_PYTHON) tools/saturn/verify_sourceboot_memory_map.py verify \
	  --elf $(SOURCEBOOT_CANDIDATE_ELF) --required-final-margin 0x1F00
```

(resolve `SOURCEBOOT_CANDIDATE_ELF` the same way neighboring verify targets resolve the sealed identity's ELF path — copy their pattern.)

- [ ] **Step 3: Run it against any existing donor-built ELF to prove the tool works** (read-only against `DONOR/build/...`): expect a pass/fail report with real numbers, not a crash.

- [ ] **Step 4: Commit**

```bash
git add tools/saturn/verify_sourceboot_memory_map.py Makefile.saturn.mk
git commit -m "feat(build): plain verify --elf mode for the memory-map gate; margin becomes a build output. CHANGELOG updated."
```

---

### Task 10: Build the R1 candidate CUE

**Files:**
- Owner asset: `REC/bob_theme.us.wav` (prerequisite — see header)
- Output: `REC/build/saturn/sourceboot/e2-bob-identity-id-<tag>/`

- [ ] **Step 1: Produce the music PCM**

```bash
"$REC/.venv-saturn-tools/Scripts/python.exe" tools/saturn/wav_to_pcm8.py bob_theme.us.wav build/saturn/audio/bob_theme_8k.pcm8 --rate 8000
```

Expected: ≤65,535 bytes (the tool's rate-aware default trims to the u16/SCSP loop cap). Wire it into the SFX-bundle make rule's packager invocation (`--music-pcm build/saturn/audio/bob_theme_8k.pcm8 --music-rate 8000`); for a longer loop at lower fidelity, retry at `--rate 6000` (~10.9 s cap).

- [ ] **Step 2: Build** (one command; all variables mandatory — the profile JSON is not read back into Make):

```bash
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 \
  mingw32-make -f Makefile.saturn.mk -j1 sourceboot \
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

Note `OBJECT_POOL_CAPACITY=240` (A9A's value — the 208 cut belonged to the all-features crunch; if HWRAM fails by <2 KB, dropping to 208 is allowed fallback (a)).

- [ ] **Step 3: If the link fails, apply the decision matrix — in order, one change per rebuild:**
  (a) `SATURN_OBJECT_POOL_CAPACITY=208`;
  (b) Task 8 fallback (scratch arrays only to HWRAM);
  (c) keep audio service `.text` in the cart (the transplanted linker delta's placement) and record it as an explicit measured-cadence TODO;
  (d) two failed attempts or two hours → stop, record margins from `verify-memory-map`, escalate to owner.

- [ ] **Step 4: Record identity + margins** (this is the launch-discipline binding):

```bash
powershell -ExecutionPolicy Bypass -File tools/saturn/with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk verify-memory-map
sha256sum build/saturn/sourceboot/e2-bob-identity-id-*/obj/*.elf build/saturn/sourceboot/e2-bob-identity-id-*/*.iso
```

Write identity tag, ELF/ISO SHA-256, build time, and margin numbers into `docs/saturn/evidence/reports/sprint1-r1-candidate.md`. Commit that file.

---

### Task 11: Launch, telemetry, owner gate

- [ ] **Step 1: Headless first** — cadence + on-target identity proof + audio telemetry:

```bash
"$REC/.venv-saturn-tools/Scripts/python.exe" tools/saturn/capture_sourceboot_throughput.py \
  --ymir "D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent/apps/ymir-headless/Release/ymir-headless.exe" \
  --ipl "D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin" \
  --release-manifest build/saturn/sourceboot/e2-bob-identity-id-<tag>/saturn-release-manifest-v1.json \
  --game <cue> --elf <elf> --startup-vblanks 600 --max-vblanks 3600 \
  --output docs/saturn/evidence/reports/sprint1-r1-throughput.json
"$REC/.venv-saturn-tools/Scripts/python.exe" tools/saturn/probe_audio_mailbox.py <per its --help, against the same CUE>
```

Telemetry gate before any owner time: FPS mean ≥ 4.0; `MUSIC_STARTS >= 1`, `MUSIC_REJECT_MASK == 0`, `ACTIVE_VOICE_COUNT >= 1` sustained, `PROTOCOL_FAULTS == 0`. Screenshot after the gameplay stage renders via `capture_hwtest.py --screenshot-output --dram-cart`.

- [ ] **Step 2: Desktop launch for the owner** (first recorded launch of the hardened launcher — also its smoke test):

```bash
"$REC/.venv-saturn-tools/Scripts/python.exe" tools/saturn/launch_ymir_desktop.py \
  --release-manifest build/saturn/sourceboot/e2-bob-identity-id-<tag>/saturn-release-manifest-v1.json --launch
```

- [ ] **Step 3: Owner observation checklist** (record answers verbatim in the evidence report):
  1. Mario, terrain, camera, input at least as good as A9A (scale, colors, shading, ordering)?
  2. BOB music playing, looping without periodic silence?
  3. One game-triggered SFX audible over the music (e.g. jump), stopping/starting correctly?
  4. Perceived cadence (recorded, see gate note below).

  **Owner gate change (2026-08-15, owner-stated):** the ≥4 FPS floor is NOT
  a blocking gate for R1 acceptance. FPS is measured (headless capture) and
  recorded, but a sub-4 result does not fail the candidate or trigger a
  revert — the stage-1b LWRAM evictions (workarea + Mario emission scratch,
  forced by 49.6 KB of committed HWRAM growth) are the expected mechanism,
  and cadence recovery via committed-memory reduction is the first named
  objective of the next sprint. Visuals and audio are judged on their own
  merits. The AGENTS.md 4 FPS floor continues to apply to *retained changes*
  in later sprints once a cadence baseline is re-established.

- [ ] **Step 4: Keep or revert immediately.** On owner acceptance: update `STATE.md` + `CHANGELOG.md` + `ROADMAP.md` (R1 closed), commit, and tag:

```bash
git tag -a sprint1-r1-accepted -m "R1: A9A-parity visuals + audible looping music + SFX, owner-accepted"
```

On rejection: record the defect, apply the constitution (one hypothesis, smallest change, second CUE; stop after two attempts/two hours).

---

## Self-review notes

- Spec coverage: preservation (T1–T2), constitution (T3), audio M1–M5 (T4–T7 — M1=T4, M3/M4=T5, M5=T6, M2=T7; the SCSP readback fix arrives inside T2's patch), R5 revert (T8), margin gate (T9), build/observe/gate (T10–T11). Bob-omb, streaming, saves, levels: later sprints per charter.
- The plan's C snippets were drafted from the donor-map audit, not from reading every line; each such task opens with a mandatory read/verify step — this codebase's established pattern (plan-drafted code has repeatedly been corrected against source by implementers, by design).
- Rollback safety: every task is one commit; T8 carries its own fallback ladder; the donor worktree is never touched after T2's read-only export.
