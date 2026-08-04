# Runtime-contract line-4018 diagnosis

Status: resolved host-contract mismatch; no production renderer or snapshot
logic change.

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
with a private dynamic payload: words 0..5 and 14..15 are zeroed, while words
6..13 carry the projected coordinates. That commit's master path resolves the
immutable material by primitive ID and calls
`sm64_saturn_terrain_template_patch_resolved_record_ex()` from
`demo_emit_terrain_result()` after the cross-CPU join.

The fixture's resolved template starts with control `0x0004`, PMOD `0x0488`,
COLR `0xC210`, SRCA `0x0123`, and SIZE `0x0820`; the current worker payload
correctly starts with zero material bytes. The mismatch is therefore expected
for the current two-stage contract, not a command-index, sort, or template
corruption defect.

## Repair

Commit `533471e5` (`test(saturn): align terrain worker command contract`)
renames the test to describe the private dynamic-command boundary and replaces
the obsolete full-command comparisons with assertions that:

- material/header and tail words in resolved worker entries are zero;
- the original coordinate bytes survive result sorting; and
- resolved-template patching creates the expected complete master-side image.

The checks remain behavior assertions; none were suppressed or weakened. The
old assertion could only pass by restoring duplicate worker material assembly,
which would contradict the compile-once-template architecture and duplicate
the master-owned work.

## Verification and remaining gates

- Red reproduced: explicit-host `verify-runtime-contracts` failed at line
  4018 before the test-contract repair.
- Green: the same `-B -j1` command completed with exit code 0 after commit
  `533471e5`.
- Hygiene: `git diff --check` exited 0 before the test commit.
- Independent review verdict: not yet obtained; this report does not claim
  independent approval.
- No target build, CUE, or Ymir run was performed. A2 still needs fresh
  independent reviews and target cache/coherency evidence.
