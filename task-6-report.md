# Task 6 — Compile-once VDP1 command state

## Result

- Resolved terrain templates now retain the complete 16-word VDP1 command
  image and an exact per-word patch mask.
- Static texture source, draw-mode, colour, size, and reserved words resolve
  during `sm64_saturn_demo_render_init()` after VDP1 partitions are set.
  The frame path no longer resolves templates.
- Worker lanes now publish only dynamic XY patch payloads plus the compact
  result identity.  The master remains the only owner that copies a static
  template, patches link/end/XY/Gouraud fields, allocates Gouraud storage, and
  emits the final VDP1 command.
- BOB has load-resolved base, recovery-material, and texture-suppressed
  variants.  Those supported states no longer select the legacy reconstruction
  path.  The sole `demo_bob_terrain_legacy_fallbacks` increment is retained
  for malformed/rejected templates or genuinely dynamic material state.

## Verification

Passed with Qt MinGW host GCC (`C:\Qt\Tools\mingw1310_64\bin\gcc.exe`):

- `terrain_command_template_test.c` — includes immutable-word poisoning and
  clipped, recovery-material, textured-flat, textured-Gouraud, and
  texture-suppressed patch fixtures.
- `terrain_depth_bins_test.c` — regression check for the compact result stream
  consumed by the master emitter.
- `git diff --check` and source checks confirm template resolution is invoked
  from the load hook only, workers write only XY patch bytes, and the legacy
  counter has one narrow fallback site.

`mingw32-make -f Makefile.saturn.mk` could not run its host-tool prerequisite
under PowerShell (`! was unexpected at this time`), so the two C fixtures were
compiled directly with the same Qt native compiler.  No MSYS, bash,
`sh-elf-*`, target build, or Ymir command was run.

Target-route verification is pending: the accepted BOB DRAM-cart capture must
confirm `demo_bob_terrain_legacy_fallbacks == 0` together with the existing
zero fault/overflow invariants.

## Reference and reuse record

Read before implementation:

- `docs/saturn/RENDERER_PRIOR_ART.md` — command-template lifecycle and the
  master/slave ownership boundary.
- `docs/saturn/UPSTREAM_CODE_LEDGER.md` — PS1 source remains pattern-only;
  SlaveDriver and Sonic Z-Treme are GPL-compatible references under the
  repository policy.
- The exact Task 6 plan at
  `docs/superpowers/plans/2026-08-02-saturn-dual-sh2-vdp-pipeline-sprint.md`.

Reuse mode: project-native implementation.  No upstream renderer code was
copied for this task; the existing recorded prior-art constraints informed the
template ownership and fixed-patch design.
