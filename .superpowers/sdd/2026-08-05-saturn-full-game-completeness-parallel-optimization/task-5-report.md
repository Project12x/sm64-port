# Task 5 implementation report — scene-package residency

Status: review repair source-complete; independent rereview and all target,
Ymir, FPS, final-root, broad native-math, and manual gates remain open.

## Result

- Added a bounded bytewise S64P v1 reader. It never casts big-endian media to
  host structs and validates root SHA-256, section hashes/ranges/alignment,
  zero-filled gaps, closed section ordering (plus the required zero-section
  fixture), section and payload dependency graphs, canonical dependency-set
  SHA-256, descriptor metadata, stable IDs, and payload ordering.
- Added master-owned two-generation residency state backed by explicit,
  non-overlapping caller-owned root/HWRAM/LWRAM/CART/VRAM/SOUND-RAM spans.
  Root and feature-active payload bytes are copied into owned storage and
  rehashed at commit. Every declared external payload is hash/size/generation
  validated, including feature-inactive ones;
  only the feature-active transitive payload closure consumes capacity.
  Section/payload dependency order, aligned byte plus scratch budgets, partial
  commit rollback, duplicate generations/commits, and capacity exhaustion fail
  closed. A new generation does not expose or evict the old generation.
- Render snapshot, VDP1 frame-bank, actor/animation bank, and (when present)
  audio voice ownership use exact-generation acquisition/release adapters with
  bounded per-consumer lease-token sets. Snapshot and VDP1 adapters derive the
  token from the concrete handle; actor/audio callers provide a nonzero token
  that is unique among their live leases. Once a replacement commits, the old
  generation is closed to later acquisition and cannot unload until existing
  references retire. Duplicate acquire, duplicate/stale release, and bounded
  token-table exhaustion fail closed without changing another lease.
  VDP1 handles and explicit actor-bank tokens use disjoint high/low-bit token
  namespaces because they share the bank-consumer set.
- Immutable render snapshots now carry a scalar package ID, active-feature
  mask, root/dependency-set hashes, and actor/animation/audio bank identity
  hashes. No package or payload pointer crosses the peer-visible snapshot.
- Sourceboot has a strict optional linked-root boot caller that validates S64P
  and rejects provisional roots before entering the game loop. Failure leaves
  the output view zeroed. The Task 4 provisional root is not activated.
- CART capacity is caller-supplied available capacity. The runtime does not
  assume that the whole 4 MiB cart is free and does not write over the existing
  native-pointer `SOURCE.DAT` prefix. Its exported residency span begins at the
  aligned linked-source high-water. Later producer tasks still own final
  package content and Task 22 owns the final root seal.

## Independent-review repair

The first review returned SPEC/QUALITY FAIL. The focused repair adds:

- early `offset > byte_count` rejection before subtraction, gap scan, or hash,
  covered by a resealed `UINT32_MAX` descriptor regression;
- owned root/payload copies with post-copy and commit-time hashes, rollback
  clearing, immutable accessors, and source-mutation/corrupt-owned-byte tests;
- absolute two-generation placement including alignment and scratch;
- token-bound render/VDP1/actor/audio lifecycle adapters, including two-live-
  handle regressions proving duplicate release of one lease cannot retire the
  other;
- an aligned post-`SOURCE.DAT` CART span and a real optional-root boot caller;
- UTF-8 stable-ID validation and ABI-legal generation-zero/zero-byte payloads;
- defined SHA behavior: `NULL, 0` is empty, while `NULL` with nonzero length is
  rejected; and
- an explicit `<string.h>` declaration for sourceboot's output-view clearing,
  avoiding an implicit freestanding target prototype.

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

The two C executables passed; the repaired neutrality/sourceboot boundary suite
passed 6/6. Coverage includes root/dependency/section hash failures, SHA-256 known
vector, section and payload dependency order/cycles, partial rollback with old
active identity retained, actor/audio generation and payload mismatch, aligned
HWRAM/CART/SOUND-RAM capacity, stale/exact retirement, active eviction refusal,
double commit, inactive-feature validation without residency, pointer-free
snapshot publication, zero-section commit, malicious root offsets, owned-byte
mutation isolation, commit-time corruption detection, storage-span overlap,
root capacity, SOUND-RAM/scratch exact fit, cross-generation absolute
alignment, UTF-8 stable IDs, generation zero, and zero-byte payloads.
The retirement regression holds two render handles and two actor-bank tokens
simultaneously, rejects duplicate acquire/release, proves token A cannot retire
token B, rejects a stale-generation handle after unload, and permits unload
only after every legitimate release succeeds.

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

Open: independent rereview; linked SH-2/sourceboot build and memory inspection
(both runtime sources are now in `SH_SRCS`, but no target build is claimed);
the inherited snapshot Make quoted-executable defect;
real final BOB/WF roots and destination placement; Ymir/FPS/manual evidence;
package transitions; broad native-math evidence.
