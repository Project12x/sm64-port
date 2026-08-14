# Mario Port Product Recovery — execution ledger

Authority: `docs/saturn/PRODUCT_GOAL.md`

Active plan: `docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md`

Status: **active — Task 1 evidence recorded; baseline-first hybrid selected;
no current candidate accepted and visual comparison remains unresolved**

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
- Governance, product goal, active plan, and this forced/scoped ledger were committed as `1f07485a` (`docs(saturn): make playable port the product gate`).
- Documentation verification: archived A9A hashes recomputed exact; authoritative relative links passed; placeholder scan passed; staged `git diff --check` and post-commit `git show --check` passed.
- Persistent workspace constitution: `D:/Code/RetroDev/sm64-saturn-port/AGENTS.md`, SHA-256 `e0edf890da1c2d1c977c82da69c8e1851bb9b87a60125648cd2fab670a27ce58`.
- No code, build, emulator, or owner-acceptance gate ran during this documentation transition.

## Current execution boundary

- [x] Record the current HEAD, dirty-worktree ownership, profile/BIOS/Ymir identity, and current artifact hashes.
- [x] Inventory the accepted A9A donor at file/config granularity before changing runtime behavior.
- [x] Create `docs/saturn/evidence/reports/current-product-gate.json` from measured facts; do not predeclare pass fields.
- [ ] Begin Task 2 with one causal Mario restoration change and an immediate identity-bound Ymir observation.

## Task 1 — baseline and donor evidence

- [x] Archived A9A ELF/ISO/CUE hashes were recomputed exact against
  `PRODUCT_GOAL.md`; no rebuild or archive mutation occurred.
- [x] Recorded source HEAD, dirty-worktree status digest, historical A9A launch
  record, image-file identity, and newest current artifact tuple in
  `docs/saturn/evidence/reports/current-product-gate.json`.
- [ ] Baseline/current visual observation: the parent profile config is pinned
  and hash-matched as historical desktop comparator evidence only. Bounded
  headless observations used explicit BIOS/CUE/`--dram-cart` arguments; neither
  consumed `Ymir.toml` or included video capture. The same-hash A9A sibling tuple
  failed its boot-trace diagnostic after 1,680 frames. The selected current
  candidate matched linked/build identity, then failed cadence decode before a
  presentation event. No result advances the product gate.

## 2026-08-14 — Task 1 evidence review correction

- Independent read-only review of `10a2c507..2fc1f72c` initially returned
  **NEEDS FIXES**: the headless client never reads `Ymir.toml`, Steps 3–4 were
  checked despite lacking video, and the record used placeholder commands.
- Verified correction: `YmirClient` injects explicit `--ipl`, `--game`, and
  `--dram-cart`; the pinned parent TOML remains historical desktop-comparator
  evidence only. The record now contains resolved capture commands and output
  paths, and the plan leaves both visible-capture steps unchecked.
- Verification after the correction: `current-product-gate.json` parses and the
  scoped diff passes `git diff --check`. The failed A9A boot-trace and current
  zero-presentation diagnostics are retained unchanged. Task 2 is still the
  next live behavior task; no product acceptance is claimed.

## Non-negotiable stop rules

- Two failed live attempts or two hours on one regression triggers donor replacement or removal, not more abstraction.
- A host test, source review, release seal, or `source-complete` status does not advance this ledger.
- Do not open another architecture, wire-format, release, capacity, or generalized-level sprint before the two-level presentation gate.
- A launched artifact must be named and hashed before Ymir starts; stale or wrong-profile launches are invalid evidence.
