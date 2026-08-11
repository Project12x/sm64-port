# Task 10 execution report

Date: 2026-08-11

## Status

Task 10 is `blocked-on-prerequisite`, not complete. Task 9 remains the
historically reviewed release-sealing baseline at controller base `bb9d3c9f`.
The canonical staged integrated BOB candidate verified before emulator I/O at
manifest SHA-256
`9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99`
and identity `id-a40f992c085da2f0`. No rebuild or restage occurred.

The exact 20,100-frame smoke ran to its requested depth and failed the
gameplay/presentation gates because the sealed target enables
`SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1` while the production generic actor
cutover remains the intentional fail-closed stub documented by
`docs/superpowers/plans/2026-08-07-task16-completion.md`. Task 16 completion is
the prerequisite. It must not be replaced by disabling the feature. Because
that prerequisite changes target bytes, Task 9 build/reproducibility/v4/
staging and reviews must reopen before Task 10 resumes.

## Reconciliation and staged release

- Reconciled HEAD, active plan, SDD ledger, Task 9 report, `STATE.md`, and
  `ROADMAP.md` at `bb9d3c9f`; all named Task 9 complete and Task 10 active.
- Verified the canonical five-file stage before each evidence chain with:

  ```powershell
  .\.venv-saturn-tools\Scripts\python.exe tools\saturn\release_manifest.py verify `
    --manifest build\saturn\releases\sourceboot-bob-demo-v2-manual-candidate\saturn-release-manifest-v1.json
  ```

- Manifest SHA-256:
  `9110b40da0e890b7b03dc5748e9ead4a47865ea4f9e3df21869b47de33679b99`.
- Exact staged ELF SHA-256:
  `dcf4123f66ffff5e5d4efd4ac2cd5efdc4298a2c8c62c2a73af69ce936013010`.
- The manifest-bound capture's copied CUE/ISO/ELF all matched their release
  rows; exact immutable code and initialized 500-byte identity both matched.

## Environment

The linked worktree has no local `.ymir-profile`. The established canonical
project profile was used from
`D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile`.

- BIOS: `Sega Saturn BIOS (USA).bin`, 524,288 bytes, SHA-256
  `96e106f740ab448cf89f0dd49dfbac7fe5391cb6bd6e14ad5e3061c13330266f`.
- Headless Ymir:
  `D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe`,
  SHA-256
  `fcc88d82b2ea7afdf400bcf67d45139d02354379388f7f9dba731b63a38d3943`.
- Ymir was invoked by the existing `YmirClient` with the exact staged CUE and
  `--dram-cart`; the prescribed BIOS pulse macro was unchanged.

## Repair round 1: bounded loaded-identity wait

The first exact command was:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_object_pool_occupancy.py `
  --ymir $ymirHeadless --ipl $ipl `
  --game $manualCue --elf $manualElf --release-manifest $manualManifest `
  --post-bios-frames 20100 --sample-interval 300 --timeout 1800 `
  --output docs\saturn\evidence\reports\hermetic-sourceboot-combined-smoke-2026-08-10.json
```

It exited 1 after 12.8 seconds and 1,500 BIOS-macro frames. Failed-report
SHA-256 was
`9261cc61b624a69e565f81d62a90f08d4c7166e92f0d5d098a1ae8369af8acc2`;
there were zero valid samples because immutable target bytes were not loaded
yet. Exact-candidate diagnostics measured immutable code first matching at
+540 VBlanks and the initialized sealed identity at +577.

Reference-first inspection used
`tools/saturn/capture_sourceboot_throughput.py::wait_for_target_identity` at
`bb9d3c9f`; reuse mode is same-repository close-port. TDD added a regression
for waiting until both code and sealed build identity are loaded, then added a
bounded 3,600-VBlank wait. Commit:

- `fc036e6e` — `fix(saturn): wait for loaded smoke identity`.

The behavior commit includes `CHANGELOG.md`, the tool/test, active plan,
ledger, and this report. Focused RED was the missing API. Focused GREEN was
2/2 for the regression/CLI pair, then 13/13 for the full capture suite with
normal Windows ancestor-handle access. The sandboxed full-suite setup run had
two established `C:\Users\estee` ancestor-handle errors and was discarded as
environmental. Syntax and scoped diff checks passed. Target bytes and release
binding inputs did not change in this repair.

## Exact smoke rerun

After re-verifying the same manifest, the same exact command completed all
requested execution and exited 1 only on acceptance:

- Wall time: 172.985 seconds.
- Total emulated frames: 22,177 = 1,500 BIOS macro + 577 identity wait +
  20,100 exact post-BIOS frames.
- Samples: 67/67 valid at 300-frame intervals.
- Report SHA-256:
  `75e4ffc666de54f81808c6b98beb59edb499aff15d623063f9499eedb24d2ac6`.
- Release manifest, CUE, ISO, ELF, immutable code, and identity: exact match.
- Cart: stage 5, expected/copied 3,565,696/3,565,696, status 0.
- Exception record: clear (0).
- Object pool: capacity 208, peak 138, allocation failures 0, final allocated
  range observed 0–137.
- Failed gates: presentation generations did not climb and `sAreaYaw` did not
  change in the prescribed 9,500–10,000-frame window.
- The authoritative source loop advanced exactly two ticks and then remained
  stopped. Peak 138 remains an idle/bootstrap floor without pickup/hold or
  action-particle coverage.

## Systematic root-cause evidence

Diagnostics were bounded and changed their sampled state or execution chunk;
the unchanged acceptance run was not repeated. The final failure-edge report
at `build/saturn/reports/task10-current-render-failure-edge.json` has SHA-256
`6953227593a13e580e09a396189e83bd476a577cdd99cf5e3620bbf1481344b6`.

### Ruled out

- **BIOS/start pulses:** the unchanged proven macro loads exact code/identity,
  completes the 3,565,696-byte cart copy, and reaches the source loop.
- **Emulator/profile invocation:** canonical BIOS, exact CUE, known Ymir
  binary, and `--dram-cart` were used; cart reports ID 92 / 4 MiB and complete.
- **Stale snapshot/probe address:** every sampled symbol was freshly resolved
  from the exact staged ELF and read through P2; live boot/cadence/pool/queue
  transitions agree with control-flow PCs.
- **Target-byte mismatch:** immutable code and the full initialized identity
  match the release-bound ELF; feature bits are 3 (complete Mario animation +
  dynamic actor closure).
- **VBlank/callback loss:** `__vblank_out_callback` at `0x260f9e4c` contains
  handler `0x0607db14` and null work; SCU IMS is `0x0000a17c` (VBlank-out
  unmasked); the counter advances 1427→1517 over three 30-frame intervals.

### Positive cause

- Master stopped PCs are predominantly `0x0607da0a`/`0x0607da0c`, the
  `vdp2_tvmd_vblank_out_wait()` TVSTAT loop inside
  `_sm64_saturn_source_runtime_wait_vblank`; SR is 0 and no exception exists.
- Slave SH-2 is enabled once runtime initialization completes and normally
  stops at `0x06087ce0`, Yaul `__slave_polling_entry`; early reads correctly
  report `target_disabled` before activation.
- The frame pipeline reaches simulation generation 2 and render generation 1.
  Its VBlank observation continues, while the render generation remains
  fail-closed.
- At +1,820 post-BIOS frames, render queue generation 1 contains:

  | Job | Input/output count | Terminal state |
  | --- | ---: | --- |
  | WORLD_ADMIT | 283 / 1,734 | DONE |
  | WORLD_LOWER | 283 / 1,734 | DONE |
  | ACTOR_ADMIT | 260 / 260 | FAILED |
  | ACTOR_LOWER | 644 / 644 | QUARANTINED |

- CPU-DUAL notify sequence and retired sequence are both 1. Slave claims are
  `[1,1,1,0]`; telemetry records one slave failure and one quarantine. The
  failure is not a missing wake/retirement.
- Source policy matches the evidence exactly:
  `src/port/saturn/gfx/saturn_demo_render.c` routes feature-on
  `demo_actor_admit_compat_wrapper` and `demo_actor_lower_compat_wrapper` to
  `return false`. Its adjacent comment says the generic actor-instance queue
  is infrastructure-only until production cutover. The authoritative Task 16
  completion plan identifies these wrappers as the auditable replacement
  point and leaves meshlet preparation, handoff, worker drain, merge, and
  queue-owned retirement unimplemented.

This is a product-completeness prerequisite encoded in the sealed target, not
an emulator or Task 10 harness defect.

## Discarded environmental/setup runs

- Two initial diagnostics supplied a non-profile BIOS path and closed before
  `instance.ready`; they are setup errors, not target evidence.
- One completed diagnostic could not persist because its ignored
  `build/saturn/reports` directory did not yet exist; the directory was created
  and the bounded diagnostic rerun.
- One first-pass diagnostic treated the expected pre-activation slave
  `target_disabled` response as fatal; the corrected run records that state
  and later slave registers.
- The earlier sandboxed test-suite ancestor-handle errors are recorded
  separately above.

## Open gates and handoff

Task 16 production generic actor cutover is the blocking prerequisite. Do not
disable `SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE`; that would contradict the
accepted eventual-full-port profile. After Task 16 changes land:

1. Reopen Task 9 clean A/B build, reproducibility, measurement, exact-v4,
   capacity/package, staging, and independent reviews for a new manifest.
2. Re-run Task 10 manifest verification and exact 20,100-frame smoke.
3. Only after smoke passes, run release-bound visual capture and independent
   PNG inspection.
4. Only after visual acceptance, verify desktop launch and prepare the owner
   checklist.
5. Leave owner manual acceptance unchecked until the owner plays and reports.

No visual JSON/PNG, desktop-launch report, owner checklist handoff, or owner
verdict was produced. `sm64-saturn-full` remains non-releasable; architecture
scales, but content/system inventory and game-wide evidence remain incomplete.
No total-game completion is claimed.
