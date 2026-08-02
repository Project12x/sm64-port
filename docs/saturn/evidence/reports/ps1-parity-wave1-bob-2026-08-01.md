# PS1-parity Wave 1 BOB comparison

## Decision

**BLOCKED — neither accepted nor rejected.** The initial Wave 1 head did not
compile. Its reviewed successors then cleared the LWRAM and HWRAM link gates,
and `afb1d37` produced a fresh ELF, but the truthful native-math audit now
fails: 866 observed helpers versus the fixed 582 contract, with 22 unlisted
unresolved transfers. No fresh CUE could be built. The predecessor image, Ymir
capture, route-state comparison, pixels, counters, and timing comparison were
therefore not run.
Commit `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` remains the next comparison
baseline; none of `3fef49c45514f2dcdd10d62c44d14c17780d0e37`,
`5244b7bb9c64d54de20ae0b01c5bc63a2e98cb31`,
`ade86025aa16122c525a338015f49f7fbb3c36ed`, or
`afb1d378fb6a5024cc6ee58e49ca299e6bf5a044` has Task 4 target acceptance
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

## Compact-template retry

The independently approved compact-template fix at
`ade86025aa16122c525a338015f49f7fbb3c36ed` (`fix: guard compact terrain
templates`) was built with the same guarded, single-shell `-B -j1` profile and
the worktree-local Task 4 `TMPDIR`. It cleared the prior `lwram` overflow but
the final linker now rejects the HWRAM safety margin:

```text
ld: HWRAM margin below libyaul's TLSF control-block floor: the heap libyaul
  builds at ___end would overrun the top of HWRAM and mirror into low memory.
  Shrink a static HWRAM consumer.
collect2: error: ld returned 1 exit status
TASK4_RETRY3_GATE_EXIT=2
```

The failure is before ELF output and before the native-math audit. At
`2026-08-02T04:57:13Z`, the ELF path was absent; the CUE and ISO still had the
old hashes listed below and were not launched.

## Cache-shrink retry and truthful audit block

The independently approved cache shrink at
`afb1d378fb6a5024cc6ee58e49ca299e6bf5a044` (`perf: shrink terrain template
cache`) was built in the same guarded one-shell serial profile with the
worktree-local Task 4 `TMPDIR`. It cleared both prior linker failures and
produced a fresh ELF:

| Artifact | Last write UTC | SHA-256 |
| --- | --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `2026-08-02T05:15:34.3943791Z` | `c827ae034df782f007cc464d20c1752ef9f3459b04f5f70eaf5ef60d59c50730` |

The audit then ran and failed after about 471.7 seconds:

```text
SH-2 native-math census FAILED:
  audit total differs from fixed post-conversion baseline 582, found 866
  ... 22 unlisted unresolved indirect transfers ...
SH-2 native-math route audit: total 866
TASK4_RETRY4_GATE_EXIT=2
```

The unresolved transfers were `_create_skybox_facing_camera +132`; five in
`_envfx_update_snow_blizzard` (`+324`, `+334`, `+376`, `+384`, `+474`); three
in `_envfx_update_snow_normal` (`+332`, `+358`, `+464`); five in
`_envfx_update_snow_water` (`+156`, `+164`, `+190`, `+198`, `+208`);
`_exec_display_list +30`; `_geo_call_global_function_nodes_helper +40`;
`_level_cmd_call +18`; `_level_cmd_call_loop +18`;
`_orbit_from_positions +128`; `_pos_from_orbit +128` and `+162`; and
`_process_geo_layout +90`.

The contract remains 582 and was not changed. The fresh ELF is not sufficient
to authorize a CUE build or Ymir capture while this audit fails. The CUE and
ISO below remain stale and were not launched.

## Source and artifact identities

| Role | Identity | Result |
| --- | --- | --- |
| predecessor | `23c3cdda4bcb1e749b05cde9ae1910e02ed5eff6` (`docs: design PS1-parity optimization sprint`) | not checked out or built after the Wave 1 gate failed |
| initial Wave 1 head | `3fef49c45514f2dcdd10d62c44d14c17780d0e37` (`perf: prebuild terrain command state`) | source compilation failed before fresh ELF |
| retry head | `5244b7bb9c64d54de20ae0b01c5bc63a2e98cb31` (`fix: compile standalone terrain templates on SH2`) | source compilation completed; link failed, `lwram` overflow 15840 bytes |
| compact-template retry head | `ade86025aa16122c525a338015f49f7fbb3c36ed` (`fix: guard compact terrain templates`) | prior LWRAM overflow cleared; link failed at libyaul HWRAM TLSF heap-safety floor |
| cache-shrink retry head | `afb1d378fb6a5024cc6ee58e49ca299e6bf5a044` (`perf: shrink terrain template cache`) | linked fresh ELF; audit failed 866 vs fixed 582 with 22 unresolved transfers |
| output profile | `e2-bob-demo-replay-camroute1-atan2v2-camv3-idle0-disc0-range0-stage8-r6000-slave1-poly0-hot1-clip1-bsp1-frag0-pipe2` | partial object rebuild only |

The output directory already contained older artifacts. They were inspected
only to reject them as stale; none was launched or treated as Wave 1 evidence.
The first freshness check was made at `2026-08-02T03:49:54Z`. After the first
link retry, at `2026-08-02T04:04:24Z`, no ELF remained at the output path; the
failed linker had removed it. The compact-template retry confirmed that state
again at `2026-08-02T04:57:13Z`. The cache-shrink retry created the fresh ELF
above, but the CUE and ISO retained their earlier timestamps and hashes and
were rejected as stale.

| Existing artifact | Last write UTC | SHA-256 | Disposition |
| --- | --- | --- | --- |
| `obj/sm64-saturn-sourceboot-e2.elf` | `2026-08-02T05:15:34.3943791Z` | `c827ae034df782f007cc464d20c1752ef9f3459b04f5f70eaf5ef60d59c50730` | fresh `afb1d37` ELF, audit-rejected and not launched |
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

- The link gates now pass, but the truthful audit has worsened from the prior
  recorded 656 to 866 helpers versus 582 and reports 22 unresolved transfers.
- The CUE and ISO in the profile directory are stale; any future capture must
  build and verify new disc identities instead of using them.
- After the audit discrepancy and unresolved transfers are addressed outside
  Task 4, rerun the same
  truthful native-math gate first. Only a fresh artifact path permitted by that
  gate may proceed to the serial predecessor/head CUE and Ymir comparison.
