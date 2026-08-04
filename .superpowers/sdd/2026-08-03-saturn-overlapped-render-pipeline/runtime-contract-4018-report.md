# Runtime-contract line-4018 diagnosis

Status: resolved host-contract mismatch and clear-flag shade-payload leak; no
snapshot or master VDP1 behavior change.

## Reproduction

The focused explicit-host command was:

```text
C:\msys64\usr\bin\make.exe -B -j1 -f Makefile.saturn.mk \
  SATURN_REPO_ROOT=D:/Code/RetroDev/sm64-saturn-port/sm64-port/.worktrees/sh2-native-math-purge \
  HOST_CC=C:/Qt/Tools/mingw1310_64/bin/gcc.exe verify-runtime-contracts
```

Before the repair, compilation completed and the executable stopped at
`tools/saturn/runtime_contract_test.c:4018`, comparing the sorted primitive-1
worker command to `split_primitive_1`.

## Root cause

`d80020cab` introduced the original fused path and its full-command assertion.
Its worker writer called
`sm64_saturn_terrain_template_patch_resolved_record()`, so the result span
contained a complete VDP1 command. `e50fc478` deliberately replaced that write
with a private dynamic payload. In its final layout, bytes 0..7 are either
zero or four post-light RGB1555 shades, bytes 8..11 are reserved zero, bytes
12..27 carry projected coordinates, and bytes 28..31 are reserved zero. The
master path resolves immutable material by primitive ID and calls
`sm64_saturn_terrain_template_patch_resolved_record_ex()` from
`demo_emit_terrain_result()` after the cross-CPU join.

The fixture's resolved template starts with control `0x0004`, PMOD `0x0488`,
COLR `0xC210`, SRCA `0x0123`, and SIZE `0x0820`; those immutable words never
appear in a worker payload. The mismatch is therefore expected for the current
two-stage contract, not a command-index, sort, or template-corruption defect.

## Repair

Commit `533471e5` (`test(saturn): align terrain worker command contract`)
renames the test to describe the private dynamic-command boundary and replaces
the obsolete full-command comparisons with assertions that:

- no-shades worker entries carry zero bytes 0..11 and 28..31;
- live post-light worker entries retain their four RGB1555 shade words at
  bytes 0..7 while bytes 8..11 and 28..31 remain zero;
- the original coordinate bytes survive result sorting; and
- resolved-template patching creates the expected complete master-side image.

The independent review found that the live producer passes a shades buffer
even when `POST_LIGHT_SHADES` is clear. The new clear-flag fixture was RED:
the prior writer copied those ignored values into bytes 0..7. `90fc3c76`
(`fix(saturn): gate post-light terrain shades`) now copies shades only when the
post-light flag is set. The fixture also takes the flagged path through the
same sorted spans and verifies all four shade words survive.

The checks remain behavior assertions; none were suppressed or weakened. The
old assertion could only pass by restoring duplicate worker material assembly,
which would contradict the compile-once-template architecture and duplicate
the master-owned work.

## Verification and remaining gates

- Red reproduced: explicit-host `verify-runtime-contracts` failed at line
  4018 before the test-contract repair.
- Red: the test-first clear-flag fixture passed non-null shades without the
  post-light flag and failed at the zero-prefix assertion against the prior
  writer. A shade-copy-to-zero mutation also failed at the live-shade `memcmp`.
- Green: the same `-B -j1` command completed with exit code 0 after the
  `90fc3c76` live-shades correction.
- Hygiene: `git diff --check` exited 0 before the test commit.
- Independent review verdict: `runtime-contract-4018-review.md` is NO-GO for
  the former no-shades-only assertion. This correction awaits a fresh
  independent rereview and does not claim approval.
- No target build, CUE, or Ymir run was performed. A2 still needs fresh
  independent reviews and target cache/coherency evidence.
