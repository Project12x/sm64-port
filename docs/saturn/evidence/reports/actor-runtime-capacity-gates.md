# Two unenforced runtime capacities, not a VDP1 partition gap

**Date:** 2026-09-03 · Supersedes the first version of this file
(`actor-texture-partition-gap.md`, commit `f1161698`), whose central claim was
**wrong**. See "What the first version got wrong" below; it is kept here
because the way it went wrong is the more useful lesson.

## The symptom

Admitting MODEL_WOODEN_SIGNPOST, MODEL_KOOPA_SHELL or MODEL_GOOMBA produced a
build that compiled, booted, matched its sealed identity, reported **zero faults
from either CPU**, and then presented **0 frames across 3,600 vblanks**.
MODEL_GOOMBA was retested in isolation and still failed, so it was not
cumulative load.

## The mechanism

`sm64_saturn_source_scene_bundle_init()` returning false is not soft. At
`src/port/saturn/sourceboot/main.c:2279-2285` sourceboot prints a line and then
spins:

```c
if (!sm64_saturn_source_scene_bundle_init(...)) {
    dbgio_puts("sourceboot: generic BOB scene bundle failed\n");
    dbgio_flush();
    for (;;) {}
}
```

A **clean validation failure therefore looks exactly like a hardware fault that
isn't there**: identity matches, no CPU fault, no frames, forever. Every
capacity check in the bake passes, because the values being violated are on the
runtime side and the bake never saw them.

## The two gates, measured

Both live in `src/port/saturn/sourceboot/source_scene_bundle.h`, both were
hand-maintained, and nothing in the C build or the bake compared either against
what a bundle actually needs.

| gate | was | needed | checked by |
|---|---:|---:|---|
| `..._WORKSPACE_BYTES` | 3,072 | 3,267 (MODEL_GOOMBA) | `init_from()` vs `bundle.maximum_scratch` |
| `..._UPLOAD_STAGE_BYTES` | 2,560 | 3,584 (MODEL_KOOPA_SHELL) | `preflight_span()`, **per bank** |

The upload stage is the one that hid longest, because it is a **per-bank** limit
rather than a total. Boot stages every bank's texture and CLUT through one fixed
buffer, so the binding number is the largest single bank. That is why each of
the three failed *alone*, and why "intrinsic, not cumulative" was a correct
observation pointing at the wrong conclusion.

Correlating both gates against every generation on disk separates all eight
observations with no anomaly:

| gen | set | verdict | scratch | largest bank | gate violated |
|---|---|---|---:|---:|---|
| g26 | star only | works | 2,915 | 2,560 | — |
| g85 | + buddy | works | 2,915 | 2,560 | — |
| g91 | final 5 | works | 2,915 | 2,560 | — |
| g92 | final 5 | works | 2,915 | 2,560 | — |
| g84 | + signpost | **black** | 2,915 | **2,688** | stage |
| g90 | + goomba | **black** | **3,267** | **3,072** | both |
| g75 | all 8 | **black** | **3,267** | **3,584** | both |
| g76 | all 8 | **black** | **3,267** | **3,584** | both |

## The fix

1. `WORKSPACE_BYTES` 3,072 -> 3,328. Free: the buffer is a union overlaid on
   `sm64_saturn_scene_package_view_t`, so anything up to 3,348 B costs nothing.
2. `UPLOAD_STAGE_BYTES` 2,560 -> 4,096. Also free: the stage borrows the idle
   command bank, which is 53,248 B.
3. `compile_actor_family_bundle.py` now reads **both** constants out of the
   header and fails the bake with the byte count and the offending model when a
   bundle exceeds either -- the check runs against measured bank rows and the
   real `bundle.maximum_scratch`, not against the hand-maintained estimate.

4. `source_scene_bundle_test.c`'s fake VDP1 was itself under-sized -- 20,000 B
   of texture against a real actor arena of 64,960 B -- so the roster that fits
   hardware with 26,176 B to spare still failed the host test. The fixture now
   mirrors the partition arithmetic (49,152 + 15,808 = 64,960, static-asserted),
   which makes it fail exactly when a bundle would not fit hardware either.
   Third instance today of the same shape.

## Verified

Generation g93 bakes the three previously-blocked actors: 26 banks, 32,256 B of
texture, 6,528 B of CLUT, 3,267 B of scratch, largest bank 3,584 B. Both gates
clear.

Build `dfb8a95b7f8f7258`, headless `capture_sourceboot_throughput.py`:

| | |
|---|---|
| status | `complete` |
| presentation events | 30 (29 intervals) |
| guest FPS mean / median | **6.0000** / 6.0000 |
| guest FPS 1% low | 5.4545 |
| vblanks per frame | 10 (60/10 exactly) |

Cost of the three actors: **6.3043 -> 6.0000 FPS, -4.8%**, most of it
MODEL_GOOMBA's 403 texture commands per instance. Evidence:
`actor-runtime-capacity-gates-g93-throughput.json`.

`verify-source-scene-bundle` passes. `verify-actor-texture-residency` fails to
compile on a `struct vdp1_vram_partitions` redefinition between
`tools/saturn/host_stubs/yaul.h` and the real header -- **pre-existing and
unrelated**: nothing in that translation unit
(`actor_texture_residency_test.c`, `saturn_actor_bundle.c`,
`saturn_actor_bank.c`, `saturn_sha256.c`) reaches any file changed here.

MODEL_BOBOMB_BUDDY remains out. It was never blocked -- it presents frames --
but costs 14% (6.3043 -> 5.4206 FPS). That is a framerate choice, not a defect.

## What the first version got wrong

It claimed VDP1 was never partitioned for actor textures, and that the largest
residency that presents frames is 25,600 B "exactly", matching
`SOURCEBOOT_MARIO_TEXTURE_BYTES` to the byte.

Every part of that is false:

- **Actors are partitioned.** `source_scene_bundle.c:288-299` carves the actor
  texture and CLUT regions out of `partitions.remaining_base` with an explicit
  bounds check. They were never meant to be inside `SOURCEBOOT_TEXTURE_BYTES`.
- **The pool is not tight.** The real `remaining_size` is
  524,256 - (53,248 + 359,296 + 12,288 + 34,464) = **64,960 B**. The largest
  failing roster wanted 40,352 B. VDP1 had room throughout, and in fact more
  than `inventory_actor_family_bundles.py` models (52,672 B), so the budget was
  conservative rather than overdrawn.
- **The 25,600 "boundary" was a mislabelled row.** g84 was recorded as WORKS. It
  is the signpost generation, and the signpost's bank is 2,688 B against the
  2,560 B stage -- it black-screened. One wrong cell produced a boundary that
  then matched a real constant by coincidence, and the coincidence was
  convincing enough to be committed.

The failure mode worth naming: **a single number matching a known constant to
the byte felt like proof and was not.** It was one arithmetic identity resting
on one unverified label, published without checking the code path that would
have to be true for it to mean anything -- `source_scene_bundle.c` was never
opened. Reading the allocator first would have cost minutes and refuted it
immediately.

## Method note

What actually worked was correlating *every* boot-time gate against *every*
generation on disk at once, rather than testing one hypothesis per build. That
is a few minutes of reading bundle JSON against the constants the runtime
enforces, and it produced a separator with no exceptions on the first pass.

The failure is silent by construction, so the durable fix is not either constant
-- it is that the bake now refuses to produce a bundle the runtime cannot
accept. Both known instances of this defect class were found the expensive way,
by bisecting builds, within three days of each other.
