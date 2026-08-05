# A5.9 desktop-Ymir native performance capture — 2026-08-04

## Result

The new bounded desktop capture command successfully launched the exact A5.9
Route-0/live-input/Pipe4 CUE with the project profile and recorded ten native
Ymir running-counter snapshots. VDP2 median was 60 FPS. VDP1 median was 4 FPS,
with a 3--4 FPS range. This independently confirms the owner's visual 3--4 FPS
report; the A5.8/A5.9 queue cutover is still not a performance improvement.

Ymir remained alive and responsive after capture (PID 39916 at the evidence
check). No target rebuild occurred.

## Exact artifacts and command boundary

- Desktop executable:
  `D:/Code/RetroDev/sm64-saturn-port/ymir-agent/build-agent/apps/ymir-sdl3/Release/ymir-sdl3.exe`
- Profile:
  `D:/Code/RetroDev/sm64-saturn-port/sm64-port/.ymir-profile`
- CUE SHA-256:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
- ISO SHA-256:
  `d21138b2fa759543de21cf70a8521a9193ad4741683aebb688e8dac423d84f3f`
- Raw generated JSON:
  `build/saturn/ymir-desktop-launches/a59-native-performance-20260804.json`
- Raw JSON SHA-256:
  `5fa80f7a5d61bbedc0c609e979e9e29b15bf5cf5a09bc3f68c6f3f27c7a34691`
- Sampling: 35-second boot warmup, then 10 samples at 1.1-second intervals.

All ten snapshots contained running counters. Ymir reported VDP2 values of
59--61 FPS and VDP1 values of 3--4 FPS. The saved JSON retains each complete native title, speed,
target speed, VDP2 FPS, VDP1 FPS, VDP1 completed draws, and GUI FPS.

## Tool verification

- Watched RED: the capture module, exact-process collector, proven executable,
  and long-form launch contract were absent or wrong.
- GREEN after review repair: `14/14` focused Python unit tests pass.
- `py_compile` passes for both desktop launch/capture modules.
- The live command exited 0 after 46.4 seconds and emitted the summary above.
- First independent review was NO-GO: it rejected fresh-interval wording,
  truncated even-sample medians, missing structured failure reports, and
  implicit Win32 ABI declarations. The repair calls the observations
  snapshots, preserves fractional medians, writes running/success/failure
  JSON, timestamps snapshots, checks that the exact process remains live, and
  declares the Win32 prototypes. Fresh rereview is GO. A final non-blocking
  hardening adds capture stage, PID, and adjacent log paths to failed reports.

## Upstream provenance

- Repository: `https://github.com/Project12x/Ymir.git`
- Pinned commit: `bf3e4a4a58031663d404ecc999348d72473135ba`
- License: GPL-3.0
- Files inspected:
  `apps/ymir-sdl3/src/app/app.cpp` and
  `apps/ymir-sdl3/src/app/shared_context.hpp`
- Reuse mode: protocol/API observation only. No Ymir source or headers were
  copied. The tool parses the public native desktop title produced by Ymir's
  one-second counter rollover.

## Remaining gate

Independent rereview verdict: GO, with no critical or important findings.
After the final hardening, the repaired collector attached read-only to the
same still-running PID and captured five timestamped snapshots: VDP2 median
60 FPS; VDP1 median 3 FPS, range 3--4. This did not launch another emulator or
rebuild the target. Together the two bounded windows confirm sustained 3--4
VDP1 FPS rather than a single manual title reading.

`QM/QS/QN/QR/QW/QF/QQ` are not part of Ymir's title and remain uncaptured.
They must not be inferred from the host delayed-slave fixture. A separate
target-memory path can collect them without a target rebuild, but it needs a
valid emulator memory-observation boundary before it can close A5.9.
