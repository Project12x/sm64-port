# Task 5 implementation report — scene-package residency

Status: implementation source-complete; independent review and all target,
Ymir, FPS, final-root, broad native-math, and manual gates remain open.

## Result

- Added a bounded bytewise S64P v1 reader. It never casts big-endian media to
  host structs and validates root SHA-256, section hashes/ranges/alignment,
  zero-filled gaps, closed section ordering (plus the required zero-section
  fixture), section and payload dependency graphs, canonical dependency-set
  SHA-256, descriptor metadata, stable IDs, and payload ordering.
- Added master-owned two-generation residency state. Every declared external
  payload is hash/size/generation validated, including feature-inactive ones;
  only the feature-active transitive payload closure consumes capacity.
  Section/payload dependency order, aligned byte plus scratch budgets, partial
  commit rollback, duplicate generations/commits, and capacity exhaustion fail
  closed. A new generation does not expose or evict the old generation.
- Render, bank, and (when present) voice retirement are exact-generation bits.
  An active generation cannot be unloaded; a replaced generation cannot be
  unloaded until every applicable consumer retires it.
- Immutable render snapshots now carry a scalar package ID, active-feature
  mask, root/dependency-set hashes, and actor/animation/audio bank identity
  hashes. No package or payload pointer crosses the peer-visible snapshot.
- Sourceboot has a strict future scene-root boundary that validates S64P and
  rejects provisional roots. The Task 4 provisional root is not activated.
- CART capacity is caller-supplied available capacity. The runtime does not
  assume that the whole 4 MiB cart is free and does not write over the existing
  native-pointer `SOURCE.DAT` prefix. Task 5 stages validated identities and
  capacity claims only; concrete final destination movement remains owned by
  the later actor/animation/audio producers and Task 22 final residency plan.

## Test evidence

Development failures caught before final GREEN:

- First focused compile stopped under `-Werror` on a signedness mismatch in a
  dependency bound. The comparison was made explicitly unsigned.
- The first residency execution stopped on a fixture mutation applied after
  the payload registry had copied its generation value. The test now mutates
  the bound registry and proves the intended generation rejection.
- Expanding the fixture to the closed eight-section ABI initially exposed an
  out-of-range fixture dependency index for `RESIDENCY_PLAN`; the fixture was
  corrected to create dependency records only for sections 5--7.

Final prescribed GREEN (serialized through DLL preflight, exit 0):

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-scene-package-runtime verify-scene-residency
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_scene_runtime_neutrality.py
```

The two C executables passed; the neutrality/sourceboot boundary suite passed
5/5. Coverage includes root/dependency/section hash failures, SHA-256 known
vector, section and payload dependency order/cycles, partial rollback with old
active identity retained, actor/audio generation and payload mismatch, aligned
HWRAM/CART/SOUND-RAM capacity, stale/exact retirement, active eviction refusal,
double commit, inactive-feature validation without residency, pointer-free
snapshot publication, and zero-section commit.

Snapshot compatibility check:

```powershell
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 mingw32-make -f Makefile.saturn.mk -j1 verify-render-snapshot-bank
```

Compilation succeeded. The existing quoted-executable MSYS recipe then failed
with `unexpected EOF while looking for matching '"'`; the produced executable
and `tools/saturn/test_render_snapshot_source.py` were run directly and both
passed. This wrapper defect is not recorded as a green Make gate.

## Provenance and open gates

No upstream source was copied. The bounded SHA-256 implementation is a
project-owned implementation of the standardized algorithm because the repo
has no suitable target C hash utility and adding a hosted dependency would not
fit the freestanding SH-2 runtime.

Open: independent review; linked SH-2/sourceboot build and memory inspection;
real final BOB/WF roots and destination placement; Ymir/FPS/manual evidence;
package transitions; broad native-math evidence.
