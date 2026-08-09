# Task 14 real headless boot capture — first target-hardware-emulation evidence, and the real blocker it found — 2026-08-09

This is the first automated headless-Ymir boot capture ever run against a
genuinely green-linked Task 14 canonical acceptance build. It does **not**
close the question it set out to answer. It found a real, independently
confirmed, pre-existing build-packaging defect that halts this build before
it reaches anywhere near the code path the original crash lived in. The
capture is honest, cross-verified evidence of that blocker, not of the
crash fix itself either way.

## 1. Build under test

Canonical acceptance configuration (per Task 14's own definition):
`SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1
SATURN_FEATURE_SEMANTIC_AUDIO=0 SATURN_RENDERER_PIPELINE=4
SATURN_DIAGNOSTIC_MODE=0`, identity `e2-bob-identity-id-fdc1ac9ba25a4779`,
produced earlier this session by a prior task in this same effort (two
clean builds, byte-identical ELF/ISO/CUE). Re-verified present and
byte-identical against that prior task's own cited hashes before use:

- ELF `build/saturn/sourceboot/e2-bob-identity-id-fdc1ac9ba25a4779/obj/sm64-saturn-sourceboot-e2.elf`
  (7,905,360 bytes) — SHA-256 `51d54745e9f79c9a9ad2d7a89f4ab0aae9e1de3ab18e7e775d5f211ef67571fa` (matches).
- ISO `build/saturn/sourceboot/e2-bob-identity-id-fdc1ac9ba25a4779/sm64-saturn-sourceboot-e2.iso`
  (4,335,616 bytes) — SHA-256 `d78ff30f6d7cde55b0452568590341b83b9ca135275041b5213d634b17e29d22` (matches).
- CUE (88 bytes) — SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` (matches;
  boilerplate text, not itself load-bearing evidence, per the prior task's own note).
- HWRAM/LWRAM symbol values (`___end=0x060fb3bc`, `__lwram_camera_capture_end=0x0027b2d0`)
  re-read from this ELF via `sh-elf-nm -S` and confirmed identical to the prior
  task's cited figures — this is the same build, not a re-link.

## 2. Real infrastructure used

Found via `docs/saturn/evidence/reports/*.md` citations and verified present on disk
before use (not guessed):

- Ymir headless: `D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe`
  (3,255,296 bytes, SHA-256 `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`) —
  cited by `tools/saturn/current-stackfix-throughput-20260806.json`'s own recorded
  `artifacts.ymir` field from a real prior capture.
- USA IPL BIOS: `D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin`
  (524,288 bytes, SHA-256 `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f`) —
  the path cited in `docs/saturn/HANDOFF_2026-07-20.md:195` no longer exists on
  disk (it pointed at a since-cleaned `%TEMP%` file); the real, currently-resident
  copy was located under the project's own `.ymir-profile/roms/ipl/` profile
  directory and confirmed to be the exact 524,288-byte USA image the prior
  session's own memory-margin report also cites.

`tools/saturn/capture_sourceboot_throughput.py` — the tool this task
originally pointed at — turned out to be **the wrong tool for this build**:
its required ELF symbols `s_runtime`/`s_render_job_queue` belong to the
overlapped-render-pipeline (task 5/9) telemetry ABI, which this
`SATURN_RENDERER_PIPELINE=4` canonical-acceptance identity does not export
(confirmed by `sh-elf-nm`: absent from this ELF's symbol table entirely).
Using it would have failed at symbol resolution before reaching the target at
all. A custom capture harness was written instead (not committed — throwaway,
same precedent as the wave 3/4/closure reports' own probes), reusing the
project's real building blocks: `YmirClient`, `run_bios_handoff`,
`decode_boot_trace`, and artifact-identity helpers imported directly from
`tools/saturn/capture_route_views.py` / `capture_sourceboot_boot_trace.py` /
`capture_hwtest.py`, rather than reimplementing the JSON-RPC protocol.
Harness preserved at
`C:\Users\estee\AppData\Local\Temp\claude\...\scratchpad\task14_boot_capture\sourceboot_crash_evidence_capture.py`.

Every `exec.run_for` call requested **≤600 frames** (well under Ymir's own
3600-frame hard cap, confirmed in `ymir-agent`'s `debug_service.cpp`:
`"frames must be between 1 and 3600"` — the cap errors closed, it does not
silently no-op, contrary to this project's unrelated *prior* `capture_hwtest.py`
lesson about a different call site). Depth was built entirely by chunking
multiple in-process calls against one long-lived `YmirClient`, never by
requesting more than the cap in one call.

## 3. What the capture actually did

1. BIOS handoff: the project's own proven USA-BIOS input sequence
   (`run_bios_handoff` — 120+30+1200+5×30 = 1,500 frames).
2. Identity wait: read `main`'s own linked code bytes back from target RAM
   one VBlank at a time, up to 600 attempts. Matched at **attempt 562**
   (frame 2,062 since BIOS handoff) — confirms the exact linked ELF is what
   is actually executing, not a stale or mismatched image.
3. Depth push: 9 × 600-frame chunks (each its own bounded `exec.run_for`
   call) to **7,462 total frames since BIOS-handoff start** — 5,400 frames
   past identity confirmation, well past both the task's requested 4,000–5,000
   post-BIOS depth and the ~228-live-frame window where the original crash
   used to occur.
4. At every one of 20 checkpoints: read `sourceboot_boot_trace` (32 bytes,
   `0x0608eeac`), `sourceboot_exception_record` (100 bytes, `0x0608ef80` —
   magic(4) + exception_name ptr(4) + full `cpu_registers_t`(92), decoded by
   hand since no existing tool reads this record), `g_sm64_saturn_source_cart_probe`
   (28 bytes, `0x0026a290` — added mid-session once its purpose became clear,
   see §4), and both SH-2s' registers via `regs.read`.
5. Final-frame screenshot via `video.capture`.

## 4. The real finding

**No SH-2 exception ever fired.** `sourceboot_exception_record.magic`
read `0x00000000` (never written) at all 20 checkpoints across the full
7,462-frame depth. This is the record the project's committed exception
trampolines (`source_exception_trampolines.sx` / `source_exception_record.c`)
write on any real CPU fault — a nonzero `0x53484258` ("SHBX") magic would
mean a fault happened; it never did.

**But VDP1/VDP2 presentation never began either.** `sourceboot_boot_trace`'s
own `stage` field was stuck at `3` (`main-entry`) from frame 2,662 onward,
all the way to frame 7,462 — `vdp1_presentation_generation` and
`vdp2_presentation_generation` stayed `0` the entire time. The target CPU
was genuinely, continuously alive and executing the whole run (Ymir's own
`instance.stopped` `frame_limit` notifications confirm real frame advancement
at the host level, and both SH-2s answered `regs.read` throughout) — it just
never got past the very first few hundred instructions of `main()`.

**Root cause, found and independently cross-verified, not guessed:**
`g_sm64_saturn_source_cart_probe` — a diagnostic the project's own
`source_cart.h` documents as existing precisely "so Ymir's debugger can
prove early CD -> cart progress" — reads:

```
stage  = failed (6)
status = size-mismatch (4)   [SM64_SATURN_SOURCE_CART_SIZE_MISMATCH]
cart_id = 92 (DRAM_CART_ID_4MIB match — the 4 MiB DRAM cart itself was
          detected fine, this is not a missing-cart failure)
cart_size = 4,194,304 (4 MiB, as expected)
expected_size = 2,343,984
copied_size = 0   (the copy loop never started)
```

`main.c`'s own logic (`sm64_saturn_source_cart_load()` in
`src/port/saturn/sourceboot/source_cart.c:145-146`) checks the CD directory
entry's recorded file size against the ELF's own linked `.cart_rodata`
section size and refuses to copy on any mismatch — then `main()`
(`src/port/saturn/sourceboot/main.c:1610-1613`) deliberately halts in
`for (;;) {}` rather than proceed. **This is a real, working safety gate
doing exactly what it was written to do, not a crash.**

Independently confirmed against the artifacts directly, not just trusted
from the target's own self-report:

- The ELF's linked `.cart_rodata` size (`___sourceboot_cart_rodata_end −
  ___sourceboot_cart_rodata_start`, both resolved via `sh-elf-nm`):
  `0x2263c430 − 0x22400000 = 2,343,984` bytes — matches the probe's
  `expected_size` exactly.
- The ISO's own ISO9660 root-directory record for `SOURCE.DAT;1` (parsed
  directly from the built `.iso`'s Primary Volume Descriptor, not read
  through any project tool): **`size = 2,940,880` bytes** — matching the
  staged `build/saturn/sourceboot/e2-bob-identity-id-fdc1ac9ba25a4779/cd/SOURCE.DAT`
  file on disk, and **596,896 bytes larger than what this exact ELF expects.**

So: this specific green-linked build's own packaged `SOURCE.DAT` disagrees
with its own linked ELF's expectation, by a margin far too large to be
sector-rounding. The link succeeding twice, byte-identically, does not mean
the disc image it produced is internally self-consistent — nothing in the
build chain currently cross-checks that `.cart_rodata`'s linked size matches
the CD-staged asset's size. This is a **new, distinct finding**, unrelated
to the geo-walk recursion work, worth flagging to whoever owns Task 14's
next increment.

**Corroborating register evidence.** At the final checkpoint, `sh2.master`'s
PC/PR sit two bytes apart (`0x0607714c` / `0x0607714a`) with
`is_delay_slot=true` — the exact signature of a tight two-instruction
branch-to-self loop — and both addresses fall inside `main()`'s own linked
range (`0x060770bc`–`0x06078280`). This matches the deliberate
`for (;;) {}` halt in `main.c` precisely, not a fault vector, an unmapped
address, or a random stack-smash landing spot. `sh2.slave` responded to
`regs.read` throughout (it dynamically enabled itself during the run, per
Ymir's own capability tracking) and sits at `0x06080aa4`, outside `main()`'s
range entirely — consistent with the slave core idling in its own
libyaul-managed dispatcher, unaffected and unfaulted.

The final-frame screenshot (320×224, SHA-256
`3d4e2da752fb01b326f6dda568b208bde2a7ac79d70f10987c4e8c8be91c7e5a`) is solid
black — consistent with (not independent proof of) the trace data: `main()`
halts before `BOOTSTRAP_BEFORE`'s `vdp2_frame_commit`/`vdp2_sync_wait`, the
first point any frame actually gets presented, so a black screen is exactly
what this finding predicts.

## 5. Independent cross-check (ruling out a harness bug)

Before trusting a custom, uncommitted harness's finding, the same artifacts
were re-run through the project's own already-reviewed
`tools/saturn/capture_sourceboot_boot_trace.py` (unmodified, no changes to
it) with `--post-bios-frames 2400 --post-bios-checkpoint-interval 600`:

```
post-bios-600   frames=2100  raw_words=[0x53394254,1,0,0,0,0,0,0]   (stage 0 -- pre-trace)
post-bios-1200  frames=2700  raw_words=[0x53394254,1,3,3,0,0,0,0]   (stage 3 -- main-entry)
post-bios-1800  frames=3300  raw_words=[0x53394254,1,3,3,0,0,0,0]   (stage 3 -- main-entry, unchanged)
post-bios-2400  frames=3900  raw_words=[0x53394254,1,3,3,0,0,0,0]   (stage 3 -- main-entry, unchanged)
master pc=0x0607714a pr=0x0607714a  (same halt-loop address, confirmed independently)
```

Identical result from an independent, previously-vetted tool. This is a real
target-content defect, not an artifact of the throwaway harness.

## 6. Bottom line — status and honest assessment

**Real captured frame depth reached: 7,462 frames since BIOS handoff**
(5,400 past target-identity confirmation), independently corroborated to
3,900 frames by the project's own vetted tool.

**Exception-record status: absent.** `sourceboot_exception_record.magic`
never left `0x00000000` across the full depth. No SH-2 exception occurred
in this run.

**VDP1/VDP2 activity: none.** Both presentation-generation counters stayed
at `0` for the entire run; `main()` never reached `BOOTSTRAP_BEFORE`, let
alone any real render code.

**Does this constitute evidence the original crash is fixed? No — and it
cannot, as captured.** The original "Mario holding something" master-stack
overrun crash lived in the geo-walk/render code path deep inside the game
loop (`GRAPH_NODE_TYPE_START`/`HELD_OBJECT`, `saturn_geo_walk_runtime.c`,
`rendering_graph_node.c`). This build never got anywhere near that code:
execution halts at the pre-cart-load gate in `main()`, before
`main_pool_init`, before the actor/geo-walk runtime is even initialized,
before a single VDP2 frame is presented. **This capture is inconclusive
about the original crash, in either direction** — it neither reproduces nor
refutes it, because the code path in question never runs. What it *does*
demonstrate is: (a) no SH-2 exception occurs from BIOS handoff through the
cart-load gate under this exact ELF/IPL/CD combination, and (b) a real,
previously-undiscovered, independently-confirmed build-packaging defect
(`SOURCE.DAT` size mismatch) blocks any further headless verification of
this identity until it is fixed and the CUE/ISO regenerated with a
`SOURCE.DAT` that matches the linked ELF's own expectation.

**What this does not, and cannot, close out — even after the packaging
defect is fixed:** this project's own established convention distinguishes
automated headless evidence from manual acceptance, and that boundary is
real here. Even a future capture that gets past the cart-load gate and
observes VDP1/VDP2 frames advancing through real gameplay would still only
be reading fixed memory-mapped telemetry (boot-trace stage, presentation
generation counters, register snapshots) — it has no semantic awareness of
game state such as "Mario is currently moving and holding an object," the
exact scenario the original crash needed. Confirming that specific scenario
plays out safely requires a human at a live desktop Ymir window actually
walking Mario into a Bob-omb Battlefield and picking something up — this
capture cannot substitute for that, and no automated headless capture can,
by the nature of what it observes. The immediate next step for whoever picks
this up is not a bigger headless capture; it is fixing the `SOURCE.DAT`/
`.cart_rodata` size mismatch this report found, regenerating a
self-consistent CUE/ISO, and only then either re-running this same headless
harness to actually reach the geo-walk code, or going straight to a manual
desktop-Ymir session once that headless check is clean.
