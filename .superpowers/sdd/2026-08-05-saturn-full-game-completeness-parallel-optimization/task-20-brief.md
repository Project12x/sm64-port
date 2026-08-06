### Task 20: Close BOB billboard, translucent, shadow, and effect capabilities — bounded source slice

**Lane:** actor capabilities. **Depends on:** Tasks 13, 16, 18, and the accepted Task 19 admission slice. **Produces:** source-derived BOB effect-class inventory and pointer-free descriptor/lowering infrastructure with explicit fail-closed handling for unsupported geo constructs. This slice must not claim production observer capture, final closure, target/Ymir replay, manual playability, or FPS.

**Files:**

- Extend actor compiler/bank/batch modules from Tasks 11/16 only where the generated ABI remains source-hashed and backward-compatible.
- Create `src/port/saturn/gfx/saturn_actor_effect.h` and `.c` for bounded descriptor admission/lowering helpers.
- Create `tools/saturn/actor_capability_effect_test.c` and `tools/saturn/actor_effect_order_test.c`.
- Extend `tools/saturn/test_bob_actor_capabilities.py` with exact effect-class oracle/mutations.
- Modify `Makefile.saturn.mk`, `CHANGELOG.md`.

- [x] RED report binds the exact BILLBOARD, ALPHA, TRANSLUCENT, SHADOW, PARTICLE, DECAL, projectile, reward, and effect arrays to the named digest-pinned `tools/saturn/fixtures/bob_actor_effect_oracle_v1.json`; preserve the nine current unsupported records and their `GEO_CULLING_RADIUS`/`GEO_BRANCH_AND_LINK` reasons. Do not infer child roles from runtime family names.
- [ ] Reuse `sm64_saturn_mtxq_billboard`, the existing actor meshlet near-plane/depth admission, stable master batching, and explicit VDP1 color-calculation modes. Do not add a second camera-facing math path, worker-side timers, collision queries, or mutable source globals.
- [ ] Add bounded pointer-free effect/shadow descriptors, explicit alpha/cutout versus true-translucent mapping, stable far-to-near bins with equal-depth source-order stability, zero-budget/overflow/stale/unknown-bit fail-closed behavior, and capacity telemetry. Production observer fields remain unresolved and must stay zero/fail-closed until a later source-capture task proves them.
- [ ] Run GREEN serially with DLL/MSYS preflight; include the new effect C/order tests, exact Python effect oracle, `verify-actor-batches`, and the existing actor capability/family/queue regressions. Record any inherited target/Make/Ymir/manual/FPS gates as open.
- [ ] Commit as `feat(saturn): add bounded BOB actor effect infrastructure`; review VDP1 painter order, VDP2 ownership, effect lifetimes, source authority, and no runtime family-name branches.
