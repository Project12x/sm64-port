# Task 10 — terrain flat/material fast paths

## Result and ownership

- The classify/compact owner now selects `FLAT_REPLACE`,
  `TEXTURED_REPLACE`, or `GOURAUD` before publishing its compact terrain
  record. Two otherwise-unused `clip_class` bits carry that decision without
  increasing the twelve-byte cross-SH2 descriptor; the existing low two clip
  bits and recovery/LOD bits retain their meanings.
- The master consumes that compact decision but remains the exclusive owner of
  the Gouraud allocator, staging upload, VDP1 command arena, and final command
  order. It no longer recomputes the terrain shade path after the worker join.
- Equal post-light colors use `FLAT_REPLACE`; near-equal and clipped
  interpolated colors retain `GOURAUD`; textured material uses
  `TEXTURED_REPLACE`. A texture-suppressed LOD result intentionally selects
  flat replacement before it can reserve a Gouraud table.
- The texture-suppressed resolved-template variant is now RGB1555/REPLACE,
  matching its compact path. Recovery remains conservative Gouraud because
  its recovery template owns a dynamic GRDA patch.
- `sm64_saturn_gouraud_bank_t` records per-frame `saved_tables` and
  `saved_bytes`. They are diagnostics only, reset at frame begin, and do not
  influence scheduling or promotion. A `GOURAUD` classification remains the
  only terrain path that calls the allocator.

## Test-first record and verification

1. Added the command-template fixture before changing the policy. It covers
   equal colors, one-step distinct colors, clipped interpolated colors,
   textured-flat material, and an LOD-suppressed gradient.
2. The initial red test exposed the missing `TEXTURED_REPLACE` spelling and,
   once that compatibility spelling existed, exited with code 1 for the
   texture-suppressed gradient. Qt MinGW opens a blocking assertion UI for a
   failing `assert`, so this newly added red path deliberately returns a
   nonzero exit code rather than leaving a modal process. A bounded probe
   confirmed an empty executable exits normally while `assert(0)` blocks;
   no process was left running.
3. The green command uses only native Windows Qt MinGW GCC:

   ```powershell
   C:\Qt\Tools\mingw1310_64\bin\gcc.exe -std=c11 -Wall -Wextra -Werror \
     -Isrc\port\saturn\gfx -Isrc\port\saturn\gpl \
     tools\saturn\terrain_command_template_test.c \
     src\port\saturn\gfx\saturn_terrain_command_template.c \
     -o build\saturn\host-tests\terrain-command-template-task10.exe
   ```

   It passes, including compact-tag round trips and bank saved-table/byte
   accounting. The independent native `terrain_depth_bins_test.c` regression
   also passes. `git diff --check` passes.

No MSYS, bash, `sh-elf-*`, target build, or Ymir command was run. Target
compilation and hardware/emulator capture remain intentionally deferred to the
serial integration gate.

## Reference and reuse record

- Plan/spec reviewed: `docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md` Task 10 and
  `docs/superpowers/specs/2026-08-01-saturn-ps1-parity-performance-and-audio-design.md`.
- Upstream inspected: `yaul/yaul` at
  `6012f79f237773378c8014e70d8998ad95a38d98`, MIT;
  `libyaul/scu/bus/b/vdp/vdp1_cmdt.c` and `vdp1/cmdt.h`.
- Reuse mode: pattern-only. No Yaul or other upstream renderer source was
  copied. The compact tag, classifier, diagnostics, and sourceboot ownership
  wiring are project-native.
