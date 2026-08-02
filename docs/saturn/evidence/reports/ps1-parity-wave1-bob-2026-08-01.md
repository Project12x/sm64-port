# PS1-parity Wave 1 BOB comparison

## Decision

**BLOCKED — neither accepted nor rejected.** The Wave 1 head did not compile
far enough to produce a fresh ELF, so the native-math audit did not run and no
fresh CUE could be built. The predecessor image, Ymir capture, route-state
comparison, pixels, counters, and timing comparison were therefore not run.
Commit `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` remains the next comparison
baseline; `3fef49c45514f2dcdd10d62c44d14c17780d0e37` has no Task 4 target
acceptance evidence.

The fixed native-math contract remains 582. It was not changed or bypassed.

## Guarded serial command

The only target command used one MSYS2 Bash process, prepended `/usr/bin`,
created and selected the Task 1 scratch `TMPDIR`, sourced the parent checkout's
`.yaul.env`, and used the required full rebuild and serial flags:

```text
C:\msys64\usr\bin\bash.exe -lc '
  set -o pipefail
  cd /d/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge
  export PATH="/usr/bin:$PATH"
  export TMPDIR="/tmp/sm64-saturn-$MSYSTEM"
  mkdir -p "$TMPDIR"
  source ../../.yaul.env
  make -C src/port/saturn/sourceboot -B -j1 \
    SATURN_DEMO_PATH=1 \
    SATURN_SOURCEBOOT_ROUTE_REPLAY=1 \
    SATURN_SOURCEBOOT_CAMERA_ROUTE=1 \
    SATURN_CAMERA_VARIANT=3 \
    SATURN_SOURCE_CART_STAGE_SECTORS=8 \
    SATURN_DEMO_HOT_PROMOTION=1 \
    SATURN_DEMO_NEAR_CLIP=1 \
    SATURN_DEMO_BSP_ORDER=1 \
    SATURN_RENDERER_PIPELINE=2 \
    verify-sim-math-route
'
```

Result: make exited 2 after about 117.5 seconds. The compile of
`src/port/saturn/gfx/saturn_terrain_command_template.c` included
`src/port/saturn/gpl/slavedriver_terrain_result.h` through
`saturn_terrain_emit_policy.h`. In the SH build, line 88 of the result header
uses `CPU_ADDRESS_PARTITION_MASK`, but that declaration is not visible in this
translation unit:

```text
slavedriver_terrain_result.h:88:44: error:
  'CPU_ADDRESS_PARTITION_MASK' undeclared (first use in this function)
make: *** [...saturn_terrain_command_template.o] Error 1
TASK4_GATE_EXIT=2
```

This failure occurred before link and before `verify_sh2_native_math.py`.
Consequently it is not a new 582-contract audit result: the truthful audit was
not reached.

## Source and artifact identities

| Role | Identity | Result |
| --- | --- | --- |
| predecessor | `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` (`docs: design PS1-parity optimization sprint`) | not checked out or built after the Wave 1 gate failed |
| Wave 1 head | `3fef49c45514f2dcdd10d62c44d14c17780d0e37` (`perf: prebuild terrain command state`) | compilation failed before fresh ELF |
| output profile | `e2-bob-demo-replay-camroute1-atan2v2-camv3-idle0-disc0-range0-stage8-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2` | partial object rebuild only |

The output directory already contained older artifacts. They were inspected
only to reject them as stale; none was launched or treated as Wave 1 evidence.
The freshness check was made at `2026-08-02T03:49:54Z`.

| Existing artifact | Last write UTC | SHA-256 | Disposition |
| --- | --- | --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `2026-08-02T02:26:19.9308640Z` | `e1877d8a836b0b0a951a7028407f5a3ddd7dd28e170114e5ed01db8a3a47f27d` | stale Task 1 ELF, matching the recorded pre-sprint identity |
| `sm64-saturn-sourceboot-e2.cue` | `2026-08-01T23:51:55.1270563Z` | `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7` | stale, not launched |
| `sm64-saturn-sourceboot-e2.iso` | `2026-08-01T23:51:53.8310566Z` | `de639f7e9defdb98c80c1cdb79dffe09f1932731a06c4fddec44b051ecd18240` | stale, not launched |

The fresh build's output directory timestamp advanced to
`2026-08-02T03:49:17Z` from partial object compilation, while its old ELF was
not replaced. This is direct evidence that there is no fresh Wave 1 artifact
identity to compare.

## Capture and counter evidence

No Ymir process was launched and no named BOB checkpoint was captured. Thus
fresh terrain/Mario visibility, route tick, game state, pixel parity, overflow
flags, and fault flags are unavailable rather than assumed.

The requested measurement fields are all **not measured**:

| Field | Value |
| --- | --- |
| command count | unavailable — no valid image/capture |
| intentional flat count | unavailable — no valid image/capture |
| true Gouraud count | unavailable — no valid image/capture |
| Gouraud allocation failures | unavailable — no valid image/capture |
| command build ticks | unavailable — no valid image/capture |
| command upload ticks | unavailable — no valid image/capture |
| VDP wait ticks | unavailable — no valid image/capture |
| total-frame ticks | unavailable — no valid image/capture |

## Concerns and next gate

- The new standalone command-template translation unit exposes an SH-only
  include-contract problem that host tests did not exercise.
- An older ELF and disc image remain in the same profile directory. Any future
  capture must repeat the freshness/source-identity preflight and must not use
  these hashes as Wave 1 products.
- After the target compile issue is addressed outside Task 4, rerun the same
  truthful native-math gate first. Only a fresh artifact path permitted by that
  gate may proceed to the serial predecessor/head CUE and Ymir comparison.
