# PS1-parity Wave 1 BOB comparison

## Decision

**BLOCKED — neither accepted nor rejected.** The initial Wave 1 head did not
compile. Its reviewed successor, `5244b7b`, fixes that translation-unit error
but still fails at link because the `lwram` region overflows. No retry reached
a fresh ELF, so the native-math audit did not run and no fresh CUE could be
built. The predecessor image, Ymir capture, route-state comparison, pixels,
counters, and timing comparison were therefore not run.
Commit `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` remains the next comparison
baseline; neither `3fef49c45514f2dcdd10d62c44d14c17780d0e37` nor
`5244b7bb9c64d54de20ae0b01c5bc63a2e98cb31` has Task 4 target acceptance
evidence.

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

## Retry after the reviewed SH2 compile fix

The retry used source commit
`5244b7bb9c64d54de20ae0b01c5bc63a2e98cb31` (`fix: compile standalone terrain
templates on SH2`). The original Task 1 `/tmp/sm64-saturn-MSYS` temporary
directory caused the Windows-hosted compiler to fail creating a temporary file
despite Bash reporting the directory writable. That environmental attempt
exited 2 before link. A fresh worktree-local temporary directory was then used
with the identical source profile, `-B -j1`, one MSYS2 Bash process, and no
other target activity.

The second retry compiled the standalone template successfully, then failed at
the final link:

```text
ld: ...sm64-saturn-sourceboot-e2.elf section `.lwram_camera_capture'
  will not fit in region `lwram'
ld: region `lwram' overflowed by 15840 bytes
collect2: error: ld returned 1 exit status
TASK4_RETRY2_GATE_EXIT=2
```

This is a distinct failure from the first source include error and is also
before `verify_sh2_native_math.py`; it does not permit a performance
comparison or change the fixed 582 audit contract.

## Source and artifact identities

| Role | Identity | Result |
| --- | --- | --- |
| predecessor | `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` (`docs: design PS1-parity optimization sprint`) | not checked out or built after the Wave 1 gate failed |
| initial Wave 1 head | `3fef49c45514f2dcdd10d62c44d14c17780d0e37` (`perf: prebuild terrain command state`) | source compilation failed before fresh ELF |
| retry head | `5244b7bb9c64d54de20ae0b01c5bc63a2e98cb31` (`fix: compile standalone terrain templates on SH2`) | source compilation completed; link failed, `lwram` overflow 15840 bytes |
| output profile | `e2-bob-demo-replay-camroute1-atan2v2-camv3-idle0-disc0-range0-stage8-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2` | partial object rebuild only |

The output directory already contained older artifacts. They were inspected
only to reject them as stale; none was launched or treated as Wave 1 evidence.
The first freshness check was made at `2026-08-02T03:49:54Z`. After the retry,
at `2026-08-02T04:04:24Z`, no ELF remained at the output path; the failed
linker had removed it. The CUE and ISO retained their earlier timestamps and
hashes and were rejected as stale.

| Existing artifact | Last write UTC | SHA-256 | Disposition |
| --- | --- | --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | missing after retry | previous hash: `e1877d8a836b0b0a951a7028407f5a3ddd7dd28e170114e5ed01db8a3a47f27d` | no retry ELF; the prior stale Task 1 ELF was removed by the failed link |
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

- The standalone command-template include problem is fixed at `5244b7b`, but
  the resulting profile now exceeds the linker `lwram` region by 15840 bytes.
- The CUE and ISO in the profile directory are stale and there is now no ELF;
  any future capture must repeat the source/hash freshness preflight.
- After the target link budget is addressed outside Task 4, rerun the same
  truthful native-math gate first. Only a fresh artifact path permitted by that
  gate may proceed to the serial predecessor/head CUE and Ymir comparison.
