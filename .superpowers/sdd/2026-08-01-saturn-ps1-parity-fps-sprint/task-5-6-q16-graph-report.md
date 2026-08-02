# Tasks 5–6 implementation report — Q16 graph math wave

## Scope

- Replaced Q16 vector normalization's three generic signed 64-bit divides
  with one reciprocal through `sm64_saturn_div_s64_s32()` and three Q16
  multiplies.
- Routed the Saturn graph's perspective and orthographic nodes through new
  Q16 constructors and raw-Q16 wire writes. The source/PC branches still call
  the original `guPerspective()` and `guOrtho()` paths.
- Kept the existing Q16 camera/object constructors, and removed redundant
  `Vec3s -> Vec3f -> Q16` work from translation, translation/rotation, and
  billboard nodes. Graph scale now lowers its source float once.
- Replaced Q16 boundary casts with integer IEEE-754 decode/pack operations,
  eliminating `_fixsfsi` and `_floatsisf` from these helpers.
- Replaced the five remaining C signed-64 divisions in the Q16 look-at
  constructor with the same explicit libyaul-backed DIVU seam.

## Source-level target call-edge delta

Removed from the selected `TARGET_SATURN` graph branch:

- `geo_process_ortho_projection -> guOrtho`
- `geo_process_perspective -> guPerspective`
- `geo_process_translation_rotation -> vec3s_to_vec3f`
- `geo_process_translation -> vec3s_to_vec3f`
- `geo_process_billboard -> vec3s_to_vec3f`
- `geo_process_scale -> vec3f_set`
- three `sm64_saturn_q16_vec3_normalize -> __divdi3`-shaped C divisions
- five `sm64_saturn_mtxq_lookat -> __divdi3`-shaped C divisions
- `sm64_saturn_float_to_q16 -> _fixsfsi` conversion
- `sm64_saturn_q16_to_float -> _floatsisf` conversion

Added target seams:

- `geo_process_ortho_projection -> sm64_saturn_mtxq_ortho`
- `geo_process_perspective -> sm64_saturn_mtxq_perspective`
- normalization/look-at/projection/ortho division through
  `sm64_saturn_div_s64_s32` (libyaul DIVU on SH-2; signed host oracle in tests)

These are host/preprocessor-proven source-level deltas. Exact linked SH-2
symbol edges remain intentionally deferred to the parent's single guarded
target build/audit; this task did not launch `sh-elf-*`, build a CUE, or run an
emulator.

## TDD evidence

- Normalization mutation gate first failed because the one-bit reciprocal
  mutant was ignored; after implementation it is rejected by the differential
  corpus.
- Projection fixture first failed to compile because the Q16 perspective and
  ortho constructors did not exist.
- Graph branch contract first failed because the Saturn preprocessed branch
  still selected `guOrtho`; it later caught the redundant `vec3s_to_vec3f`
  bridge and the look-at C divisions before each was removed.
- Conversion assembly gate first failed on host `cvttss2sil`; after integer
  IEEE decode/pack it reports integer/bit operations only.
- Review round 1 poisoned the perspective normalization output with `0xA55A`;
  all three singular fixtures initially returned identity while preserving the
  poison. The constructor now establishes `UINT16_MAX` before validation, the
  graph caller initializes the same deterministic fallback, and the poisoned
  regressions pass.

## Fresh host verification

The combined serial command completed with exit code 0:

- `verify-graph-q16-contract`
- `verify-mtxq-ctors`
- `verify-mtxq-ctors-mutation`
- `verify-mtxq-conversion-assembly`
- `verify-runtime-contracts`
- `verify-render-native-math`

## Remaining caveats

- Q16 perspective uses the existing 4096-entry SM64 trig table, so arbitrary
  dynamic FOV values are quantized; tested 45/60/90-degree cases stay within
  the explicit 160-Q16-ulp matrix tolerance. BOB's normal FOV is covered.
- Singular perspective/ortho inputs deterministically produce identity and
  return false. Singular perspective also writes `UINT16_MAX` to a non-null
  normalization output. The graph's source-derived dimensions and near/far
  values are non-singular; the caller intentionally does not re-enter the
  float fallback.
- Host assembly proves the boundary conversion implementation contains no
  native float conversion/arithmetic. The linked SH-2 absence of
  `___divdi3`, `___fixsfsi`, and `___floatsisf` at the exact accepted callers
  still requires the deferred target audit.
