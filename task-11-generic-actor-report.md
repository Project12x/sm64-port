# Task 11 — generic actor-family bank report

`compile_actor_bank.py` now emits a deterministic, content-addressed S64F
family bank from the generated scene closure. Records retain bounded offsets,
source hashes, capability masks, multiplicity, effects, model variants, and
explicit unsupported source facts. The C ABI validates spans and chooses the
smallest supported capability/capacity record without any model-name branch.

BOB evidence: closure 86 records / 133 source hashes; 47 families; payload
99,105 bytes; SHA-256
`97dc231ba1778e815423d05e9c69a13ab7567a83faa1714c085dac3dc69d45f0`.
Unsupported required capability count is 13 (twelve `GEO_CULLING_RADIUS`, one
`GEO_BRANCH_AND_LINK`), so complete closure remains false while supported
families remain inspectable.

Green evidence (all serial/DLL-preflight where applicable): generic suite 4/4
(53.325s), full-game source contract 4/4 (0.001s),
`compile-actor-banks SCENE_LEVEL=bob SCENE_AREA=1` exit 0, and host C11/Werror
syntax compile of `saturn_actor_bank.c` exit 0. No target/Ymir/manual/FPS,
runtime cutover, complete-closure, or final S64P linkage claim is made. No
external code was copied.

Independent rereview of `52999c35..7f0caa62`: SPEC PASS / QUALITY PASS. The
review confirmed total-mask ranking in both selectors, aggregate multiplicity
and closure-order determinism, zero/unknown-record rejection, and C SHA-256
tamper/expected-hash checks. Target/Ymir/manual/package-linkage gates remain
open.
