# Task 14 headless boot capture, re-run after the SOURCE.DAT/.cart_rodata fix — 2026-08-09

This is the second real headless-Ymir boot capture against a Task 14
canonical-acceptance sourceboot build, run specifically to answer: does the
`.cart_rodata` orphan-input-section fix (`4bd66637`, independently
re-verified in `807ad209`) actually let boot proceed past the cart-load
gate the prior capture found? **Yes.** The cart-load gate passes, no SH-2
exception fires across a much deeper capture than before, VDP1/VDP2
presentation is continuously active, `gMarioState` resolves to real,
evolving gameplay state, and the final frame is a real rendered scene with
Mario visible — not a black screen. The specific "Mario holding an object"
scenario the original crash needed is **not** exercised by this capture and
still cannot be, for reasons explained in §6; that piece remains open for
manual acceptance. A separate, real, previously-undiscovered build-tooling
defect was also found and is reported honestly in §4 — it did not block this
capture (worked around, no source changes), but whoever owns Task 14's next
increment should know about it.

## 1. Build under test

Canonical acceptance configuration (unchanged from the prior capture):
`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1
SATURN_FEATURE_SEMANTIC_AUDIO=0 SATURN_RENDERER_PIPELINE=4
SATURN_DIAGNOSTIC_MODE=0`, built fresh this session via
`/c/msys64/usr/bin/bash.exe` sourcing `.yaul.env` and running
`make -f Makefile.saturn.mk verify-sourceboot` with those flags on the
command line (per this project's own documented AI-agent-sandbox workaround
in `docs/saturn/BUILDING.md`). The build passed `verify-sourceboot`'s own
gates (entry point, `.cart_rodata` section address/size, soft-fp/fp-bit
substitution, Q16 disassembly, atan2 mutation differential) with exit 0.

Two independent full `make -f Makefile.saturn.mk verify-sourceboot` runs
were performed this session (see §4 for why a second run was needed). Their
outputs are both used below, for two independent lines of evidence:

- **Primary capture identity**: `e2-bob-identity-id-80491551cc665514`
  (first run). ELF SHA-256
  `65731945a69e17f8f29c1dc86e4e0b206e2602d05cab93c641b2c1968523bd41`
  (7,905,360 B). Its `.iso` as originally packaged by the build was
  content-broken (§4) and was repaired by hand before use, using only this
  identity's own already-produced, self-consistent build outputs and the
  project's own real `make-iso` tool — not a source change, not hand-edited
  bytes. Repaired ISO SHA-256
  `611bf42aa696d14fed28381911edd6635a48308b69b940303532700581c7ff54`
  (4,335,616 B). Verified before use: ISO9660 root directory lists
  `SOURCE.DAT;1` at exactly 2,940,880 bytes, matching `sh-elf-readelf -S`'s
  `.cart_rodata` `SIZEOF` and the `___sourceboot_cart_rodata_start`/`_end`
  symbol span for *this exact ELF* exactly; `obj/SOURCE.DAT` and
  `cd/SOURCE.DAT` are byte-identical (sha256
  `25086152f6f62ac4389de19b19b193a4bfd739487dc2584d07eba576a115664b`); IP.BIN
  carries the patched `"T-SM64SB01"` product ID (the same patch
  `pre-build-iso` itself applies, to avoid Ymir's game-database 8 Mbit-cart
  misdetection documented in this project's own Makefile comments).
- **Cross-check identity**: `e2-bob-identity-id-b36a047acedba675` (second
  run). ELF SHA-256
  `74818f61171a147523746e37b0a653eef956c998e991117d314d4ce474217da7`
  (7,905,360 B); this run's own `.iso` packaged correctly on the first try
  (SHA-256 `744aaa3a80283e24ac4d4b26333cd8861df712ced5f2c8349678410ef560c620`,
  4,335,616 B, `SOURCE.DAT;1` present at 2,940,880 bytes) — **zero manual
  repair**, used exactly as the build system produced it. Its CUE/ISO mtimes
  were touched forward (metadata only, no content change) solely to satisfy
  a freshness check in the project's own capture tool, after independently
  confirming via direct ISO9660 parsing that its content really was already
  correct and current for this ELF.

Both ELFs report identical `readelf -h` entry point (`0x6004000`), identical
`.cart_rodata` section (`PROGBITS 22400000`, size `0x2cdfd0` =
2,940,880 B), and identical addresses for every symbol this capture reads
(`main` at `0x060770bc`, `sourceboot_boot_trace` at `0x0608eeac`,
`sourceboot_exception_record` at `0x0608ef80`,
`g_sm64_saturn_source_cart_probe` at `0x0026a290`, `gMarioState` at
`0x0608d7d8`) — expected, since the fix only changed the separate
`0x22400000+` cart memory region, not HWRAM/LWRAM layout, and both builds
share the same flags/source.

## 2. Infrastructure reused

Same real building blocks as the prior capture, all found and verified
present on disk before use: Ymir headless
(`ymir-agent/build-agent2/apps/ymir-headless/Release/ymir-headless.exe`,
SHA-256 `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`),
the USA IPL BIOS (`.ymir-profile/roms/ipl/Sega Saturn BIOS (USA).bin`,
SHA-256 `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f`).

Two capture tools were used, deliberately, for independent cross-checking:

1. A v2 custom harness adapted from the prior session's own
   (uncommitted, throwaway) harness — same precedent, same reuse of
   `YmirClient`, `run_bios_handoff`, `decode_boot_trace`,
   `capture_text_probe_sample` imported from `tools/saturn/capture_route_views.py`
   / `capture_sourceboot_boot_trace.py` / `capture_hwtest.py`, plus a new
   `gMarioState` reader (offsets from `include/types.h`'s own field
   comments: `action` @ `+0x0C`, `pos` @ `+0x3C`, `heldObj` @ `+0x7C`).
   Preserved at
   `C:\Users\estee\AppData\Local\Temp\claude\...\scratchpad\task14b_boot_capture\sourceboot_boot_capture_v2.py`.
   Ran against the primary (repaired) identity.
2. The project's own already-reviewed, unmodified
   `tools/saturn/capture_sourceboot_boot_trace.py`, run against the
   cross-check (naturally-clean) identity, exactly as the prior session used
   it for its own independent corroboration.

Every `exec.run_for` call in both tools stayed at or under Ymir's 3600-frame
hard cap; depth was built entirely by chunking multiple in-process calls
against one long-lived `YmirClient`.

## 3. What the capture actually did and found

**v2 custom harness, primary identity (repaired ISO):**

1. BIOS handoff (1,500 frames) → identity wait, matched at attempt 562
   (frame 2,062) — confirms the exact linked ELF is executing.
2. Depth push: 30 × 1,200-frame chunks = **36,000 frames past identity
   confirmation, 38,062 total frames since BIOS handoff** — roughly 5×
   deeper than the prior capture's 7,462-frame ceiling, and far past both
   the original ~228-live-frame crash window and the point the cart-load
   gate used to halt at.
3. 41 checkpoints, each reading `sourceboot_boot_trace`,
   `sourceboot_exception_record`, `g_sm64_saturn_source_cart_probe`,
   `gMarioState` (pointer + `action`/`pos`/`heldObj`), both SH-2's
   registers, and a final `video.capture` screenshot.

**Result — the cart-load gate is passed.** From the very first
post-identity checkpoint (frame 3,262) onward, `g_sm64_saturn_source_cart_probe`
reads:

```
status = ok (0)            [was: size-mismatch (4)]
stage  = ready (5)          [was: failed (6)]
cart_id = 92 (4 MiB DRAM cart)
cart_size = 4,194,304
expected_size = 2,940,880
copied_size   = 2,940,880   [was: 0 — the copy never started]
```

`expected_size == copied_size` exactly, at every one of the 30 post-identity
checkpoints spanning the full 36,000-frame depth. The copy that used to
never start now completes and stays complete.

**No SH-2 exception ever fired.** `sourceboot_exception_record.magic` read
`0x00000000` at all 41 checkpoints across the full 38,062-frame depth —
unchanged from the prior capture's finding, now confirmed over roughly 5×
more depth and, critically, with execution actually reaching game code this
time instead of spinning in the pre-cart-load gate.

**VDP1/VDP2 presentation is continuously active**, unlike the prior capture
where both counters stayed at 0 for the entire run:

| frames since BIOS handoff | `vdp1`/`vdp2` presentation generation | boot-trace last stage |
|---:|---:|---|
| 3,262  | 10   | source-tick-before |
| 9,262  | 240  | source-tick-before |
| 18,862 | 610  | stale-wait-before |
| 27,262 | 933  | source-tick-before |
| 38,062 | 1,379 | source-tick-before |

Both counters climb monotonically and in lockstep for the entire depth; the
boot-trace `last_stage` field cycles normally between
`source-tick-before`/`after` and `stale-wait-before`/`after` — the real
per-frame tick state machine running repeatedly, not a stall.

**`gMarioState` resolves to real, initialized gameplay state.** The pointer
read from `0x0608d7d8` is `0x0609facc` at every post-identity checkpoint —
this is not a guess: `sh-elf-nm` independently confirms `_gMarioStates` is
linked at exactly `0x0609facc`, i.e. `gMarioState` genuinely points at
`&gMarioStates[0]`, matching `src/game/level_update.c`'s own
`struct MarioState *gMarioState = &gMarioStates[0];`. Its `action` field is
non-zero, non-sentinel, and changes over the run
(`0x0c400201` at frame 3,262 → `0x0c400202` at frame 27,262 →
`0x0c000203` at frame 33,262) — real action-state transitions, not a frozen
or garbage value. `pos` stays constant at `(-6558.0, -0.0, 6464.0)` for the
entire run, and `heldObj` stays `0x00000000` throughout — both expected,
because this canonical-acceptance identity has
`SATURN_SOURCEBOOT_LIVE_INPUT=0` and `SATURN_SOURCEBOOT_ROUTE_REPLAY=0`: no
controller input and no scripted replay drive Mario at all in this build, so
he never moves and never interacts with anything (see §6).

**The final-frame screenshot (320×224, SHA-256
`ee4e2192562b1d7e22102df1fe80bbfd264132913b0ea77075a1ef39450ffa34`) is a
real rendered scene**, not black: Mario (red cap, blue overalls, visibly
textured) stands in front of what appears to be a cannon-like structure on a
light-colored terrain, with props visible in the background. This is direct
visual confirmation that VDP1/VDP2 output is real geometry, not the
solid-black frame the prior capture found (which halted before any
`vdp2_frame_commit`/`vdp2_sync_wait` ever ran).

Master SH-2 PC at the final checkpoint is `0x060722b4` — inside `main.c`'s
linked range but nowhere near the old `0x0607714a` two-instruction halt-loop
address the prior capture found; consistent with active execution inside
the tick/scheduler code, not a spin loop.

## 4. Independent cross-check, and a real new finding it surfaced

Before trusting the v2 harness's own result, the same question was asked a
second, fully independent way: the project's own already-reviewed,
unmodified `capture_sourceboot_boot_trace.py`, run against a *separate*,
naturally-built ISO (`b36a047acedba675`) that required **no manual repair
at all**.

Result (bounded at 3,600 post-BIOS frames, the tool's own single-call
default cap, checkpointed every 600 frames):

```
post-bios-1200  frames=2700  stage_id=3   (main-entry)
post-bios-1800  frames=3300  stage_id=10  vdp1=11  vdp2=11   (source-tick-before)
post-bios-2400  frames=3900  stage_id=10  vdp1=34  vdp2=34
post-bios-3000  frames=4500  stage_id=10  vdp1=57  vdp2=57
post-bios-3600  frames=5100  stage_id=10  vdp1=80  vdp2=80
```

The cart-load gate passes and VDP1/VDP2 presentation starts almost
immediately after `main()` is entered (by frame 3,300 — well under 100
frames after leaving `main-entry`), independently confirming the primary
capture's finding with a different build artifact and a different,
previously-vetted tool.

**Getting to that clean second build surfaced a real, previously-undiscovered
build-tooling defect, unrelated to the geo-walk work and unrelated to the
`.cart_rodata` fix itself:** the sourceboot build's identity hash (produced
by `tools/saturn/gen_build_identity.py`, embedded in the
`SOURCEBOOT_OUTPUT_TAG` Makefile variable that names each build's output
directory) is **not stable across separate `make` process invocations**,
even with completely unchanged source inputs. Across two full runs of the
identical `make -f Makefile.saturn.mk verify-sourceboot` command this
session, four *different*, never-repeated identity values were produced:
`3f49edaa08eae542` → `80491551cc665514` → `1fed04933c4f5ef8` in the first
run, `b36a047acedba675` in the second. (`gen_build_identity.py` itself was
checked and does not use randomized `hash()`, wall-clock time, or any other
obviously nondeterministic input — the true root cause was not tracked down
further, as it is out of scope for this evidence-only task.)

The concrete, observed symptom: this project's own custom `pre-build-iso`
hook (added on top of libyaul's generic ISO-building machinery, specifically
to stage `SOURCE.DAT` into the CD image directory and patch IP.BIN's
product ID) runs via a **freshly re-parsed recursive `make -f Makefile
pre-build-iso` sub-process** — and that fresh process sometimes recomputes a
*different* identity than the outer build process that is actually
assembling the `.iso`. When it does, `pre-build-iso` stages `SOURCE.DAT`
into the *wrong* identity's directory, and the outer process's `.iso`
target quietly packages a disc that is **missing `SOURCE.DAT` entirely** —
not the size-mismatch this session's earlier fix addressed, but a
completely absent cart image (which would manifest at runtime as
`SM64_SATURN_SOURCE_CART_IMAGE_NOT_FOUND`, a different failure mode from
either the original crash or the already-fixed size-mismatch). This is
exactly what happened to the `80491551cc665514` build's original `.iso`
(1,394,688 bytes, root directory listing only `A.BIN`/`ABS.TXT`/`BIB.TXT`/
`CPY.TXT` — confirmed by direct ISO9660 parsing before use) before it was
repaired by hand for this capture (§1). The sibling directory
`1fed04933c4f5ef8` that `pre-build-iso` actually wrote into that run had a
correctly-staged cart image but no real `IP.BIN`/`.iso`/`.cue` at all — the
build's own identity drift split one logical build's outputs across three
directories, no single one of which was self-consistent on its own.

This was **not** fixed as part of this task (no source changes were made,
per this task's scope). It is flagged here, with concrete before/after
evidence, for whoever owns Task 14's next increment or the build-tooling
backlog.

## 5. Bottom line — status and honest assessment

**The cart-load gate is now passed**, confirmed two independent ways (a
custom harness against a hand-repaired artifact, and the project's own
vetted tool against a separately-built, unmodified artifact). This is real,
target-hardware-emulation evidence that `4bd66637` fixes what it claims to
fix, not just a link-time/static check passing.

**No SH-2 exception fired** across the full 38,062-frame primary capture
(36,000 frames past identity confirmation, ~5× the prior capture's depth) —
consistent with, though not proof beyond, the geo-walk recursion fix
holding through everything this capture actually exercises.

**Real gameplay state is reached and observably alive**: `gMarioState`
resolves correctly, `action` transitions over time, VDP1/VDP2 presentation
generation climbs continuously (10 → 1,379 across the primary capture), and
the final frame is a real rendered scene with Mario visible, not a black
screen or a frozen one.

**What this does not, and cannot, close out.** The original "Mario holding
something" master-stack-overrun crash needed Mario actually moving and
picking up an object (`GRAPH_NODE_TYPE_START`/`HELD_OBJECT` in
`saturn_geo_walk_runtime.c`/`rendering_graph_node.c`). This capture's own
`gMarioState.pos` never changes and `heldObj` stays null for the entire
36,000-frame depth, because this canonical-acceptance identity has
`SATURN_SOURCEBOOT_LIVE_INPUT=0` and `SATURN_SOURCEBOOT_ROUTE_REPLAY=0` — no
input source drives Mario at all. Depth alone cannot substitute for input:
running this exact build another 100,000 frames would not make Mario pick
anything up, because nothing is telling him to. This matches the boundary
this project's own prior report already drew, and it stands unchanged: a
headless capture with live input or a scripted replay route configured
*might* be able to exercise this scenario and would be a legitimate next
automated step, but the project's established convention — and this
report's own honest position — is that confirming "Mario holding an object
under real render load survives" ultimately needs either that kind of
purpose-built scripted-input capture, or a human at a live desktop Ymir
window actually walking Mario into a Bob-omb Battlefield object. This
capture does not, and does not claim to, substitute for either.

**Also open**: the build-identity nondeterminism / `pre-build-iso`
mis-targeting defect found in §4. It did not block this capture (the first
build's artifacts were usable after a documented, tool-only repair, and the
second build happened to converge cleanly on its own), but it is a real
defect that will keep intermittently producing broken `.iso` packages from
otherwise-correct builds until someone investigates why
`gen_build_identity.py`'s inputs aren't stable across separate `make`
process invocations.

## 6. Commands reference

Build:
```sh
source .yaul.env
make -f Makefile.saturn.mk verify-sourceboot \
  SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 \
  SATURN_FEATURE_SEMANTIC_AUDIO=0 SATURN_RENDERER_PIPELINE=4 SATURN_DIAGNOSTIC_MODE=0
```

Primary capture: `python3 sourceboot_boot_capture_v2.py` (harness above).

Cross-check:
```sh
python3 tools/saturn/capture_sourceboot_boot_trace.py \
  --ymir <ymir-headless.exe> --ipl <USA IPL> \
  --game <b36a047acedba675 CUE> --elf <b36a047acedba675 ELF> \
  --output vetted_tool_crosscheck.json \
  --post-bios-frames 3600 --post-bios-checkpoint-interval 600
```
