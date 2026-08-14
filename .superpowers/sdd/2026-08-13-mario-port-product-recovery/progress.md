# Mario Port Product Recovery — execution ledger

Authority: `docs/saturn/PRODUCT_GOAL.md`

Active plan: `docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md`

Status: **active — Task 1 baseline/donor inventory; no current candidate accepted**

## 2026-08-13 — owner correction and durable reset

- The owner rejected infrastructure completion as a substitute for a playable port and approved a baseline-first hybrid recovery.
- The end-of-week product gate is one executable containing both a correct, audible BOB and a minimally complete Whomp's Fortress through the same game path.
- The accepted visual/performance donor remains `build/saturn/baselines/a9a-2026-08-05/`:
  - ELF SHA-256 `1905ec8d42ea00ea2c000b5f53dd88f2079ffda8ceb67bcd5879e8e96acfc2e2`
  - ISO SHA-256 `1ccaef4f2a2d379d82879d3e823d84db135fdee1045d69aa8e0a60d150cfaf96`
  - CUE SHA-256 `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
  - measured 5.294 FPS; owner-observed range 4–6 FPS.
- Current builds are not accepted: Mario rendering, normal Bobomb rendering, audible audio, and performance have all regressed in owner observation.
- Repository governance was rewritten around live product evidence. Historical plans and ledgers remain evidence, but they no longer authorize work or define progress.
- No code, build, emulator, or owner-acceptance gate ran during this documentation transition.

## Current execution boundary

- [ ] Record the current HEAD, dirty-worktree ownership, exact build/profile command, and current artifact hashes.
- [ ] Inventory the accepted A9A donor at file/symbol/config granularity before changing runtime behavior.
- [ ] Create `docs/saturn/evidence/reports/current-product-gate.json` from measured facts; do not predeclare pass fields.
- [ ] Begin Task 2 with one causal Mario restoration change and an immediate identity-bound Ymir observation.

## Non-negotiable stop rules

- Two failed live attempts or two hours on one regression triggers donor replacement or removal, not more abstraction.
- A host test, source review, release seal, or `source-complete` status does not advance this ledger.
- Do not open another architecture, wire-format, release, capacity, or generalized-level sprint before the two-level presentation gate.
- A launched artifact must be named and hashed before Ymir starts; stale or wrong-profile launches are invalid evidence.
