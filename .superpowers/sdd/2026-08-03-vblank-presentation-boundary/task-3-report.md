# Task 3 report — persistent post-BIOS boundary trace

Date: 2026-08-03
Status: **source-complete; independent review and target evidence pending**

## Outcome

`30123c1b` (`diag(saturn): trace post-BIOS presentation boundaries`) adds the
non-static volatile `sourceboot_boot_trace` target-RAM global. Its fixed
eight-word layout is magic, version, monotonic write sequence (`stage`), named
last boundary (`stage_id`), observed VBlank generation, scheduler credit, and
VDP1/VDP2 presentation generations. The host resolves that exact global from
the matching ELF, then reports both a decoded last stage and the raw words.

## TDD evidence

Before the source edit:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_boot_trace.py
```

RED: exit 1; both tests failed with `boot trace must publish a magic word`.

Before the reader implementation:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py
```

RED: exit 1; explicit `AssertionError: sourceboot boot-trace reader is
missing`.

Focused GREEN and retained contract checks:

```powershell
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_boot_trace.py
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_sourceboot_boot_trace.py
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_sourceboot_presentation_boundary.py
& .\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_boot_trace.py --help
& .\.venv-saturn-tools\Scripts\python.exe -m py_compile tools\saturn\capture_sourceboot_boot_trace.py tools\saturn\test_sourceboot_boot_trace.py tools\saturn\test_capture_sourceboot_boot_trace.py
```

GREEN: exit 0. The trace and reader tests each ran two tests; the retained
presentation-boundary mutation gate ran six. `git diff --check` over the
behavior files also exited 0 before the implementation commit.

## Contract mapping

| Requirement | Evidence |
| --- | --- |
| Stable, volatile, symbol-resolvable target RAM record | The non-static `sourceboot_boot_trace` has a fixed eight-word uint32 layout. The source mutation gate rejects a static/non-volatile replacement; the reader resolves its exact global `nm` symbol. |
| Monotonic stage plus scheduler/presentation state | The scalar writer increments `stage`, names `stage_id`, and publishes observed VBlank, current credit, and independent VDP1/VDP2 generations. Its mutation test rejects a non-monotonic assignment. |
| Unambiguous boundaries with no side effects | Trace stages bracket bootstrap retirement, thread5, stale waits, source ticks, VDP1 render/sync, and VDP2 commit. The writer test rejects a VDP sync and requires no allocator or BOB branch. |
| Bounded post-BIOS capture | The headless reader runs the proven BIOS input macro, accepts at most 3,600 post-BIOS frames, performs one `mem.peek`, and emits stage plus raw words. It is marked diagnostic-only, not a GUI launch, target build, or performance measurement. |

## Remaining gates

1. Independent specification and quality review of `30123c1b`.
2. One serial trace CUE build through the audited MSYS wrapper.
3. One bounded headless Ymir capture using the project profile/32-Mbit DRAM
   cart and matching CUE/ELF; record its exact final stage and raw words before
   any repair decision.

No target build, CUE construction, Ymir launch, or capture was performed in
this task. The trace establishes diagnostic observability only; it does not
claim the cause of the post-BIOS failure.

## Fix round 1/5 — zero-frame reader validation

Specification review rejected `30123c1b..4d064bb7` because the parser accepted
`--post-bios-frames 0` even though it unconditionally sent that value to
Ymir's 1..3600-frame `exec.run_for` RPC. `2c1acca8`
(`fix(saturn): reject empty post-BIOS trace runs`) adds the small reusable
`validate_post_bios_frames()` guard and invokes it before any headless client
is constructed. Zero now fails locally; 1 is the valid lower bound.

TDD RED: the new reader test failed because the validation function was
absent. GREEN: the reader test now runs three tests and passes; the trace
source gate (2) and retained presentation gate (6) also pass, as does Python
compilation. No target build, CUE construction, Ymir launch, or capture was
performed. The review verdict remains REJECT until an independent rereview
records that this Important finding is addressed; the trace-CUE and capture
gates remain open.

## Fix round 2/5 — cache-through WRAM publication and fixed ABI

Quality review rejected the trace because volatile P1 writes can remain in the
master SH-2 cache while Ymir `mem.peek` and hardware debuggers read backing
WRAM. `b317a2e5` (`fix(saturn): publish boot trace through WRAM`) retains the
ELF-resolvable global but writes every trace word through
`CPU_CACHE_THROUGH | (uintptr_t)&sourceboot_boot_trace`, following the
project-owned `src/port/saturn/hwtest/main.c` telemetry publication precedent.
It also adds `_Static_assert(sizeof(sm64_saturn_sourceboot_boot_trace_t) ==
32U, ...)`.

TDD RED: the strengthened two-test source contract failed against the prior
writer because its 32-byte ABI assertion was absent. GREEN: it passes after
requiring the exact ordered eight-field layout, the exact static assertion,
and the cache-through writer. In-memory mutations independently reject an
alias removal, a cached writer, and a resized ABI. Reader (3) and retained
presentation (6) gates pass, as does Python compilation. No target build, CUE
construction, Ymir launch, or capture was performed. Quality rereview and the
existing serial trace-CUE/capture gates remain open.
