# Mario Port Product Recovery — execution ledger

Authority: `docs/saturn/PRODUCT_GOAL.md`

Active plan: `docs/superpowers/plans/2026-08-13-mario-port-product-recovery.md`

Status: **active — Task 2 donor-derived diagnostic CUE boots; baseline-first
hybrid selected; no current candidate is accepted and owner visual/audio
comparison remains unresolved**

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
- [x] Produce one donor-derived diagnostic CUE and immediately perform an identity-bound Ymir observation.
- [ ] Owner-accept Mario materials/depth/animation/controls and audio before any actor or level expansion.

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

## 2026-08-14 — Task 1 review pass; Task 2 active

- Same-reviewer rereview of correction `d8858728` returned **PASS**: the parent
  TOML is comparator-only, visible Task 1 steps remain open, and the record has
  resolved commands/output paths plus `YmirClient`'s injected `--dram-cart`.
- Task 1 therefore establishes only baseline/current identities, the diagnostic
  rejection, and the baseline-first hybrid decision. It does not close the
  visual capture requirement or any product gate.
- Task 2 is active: identify the smallest accepted A9A Mario render-path
  divergence, make one causal restoration, then build one new CUE and observe
  it before any Bob-omb, audio, performance, or Whomp's Fortress work.

## 2026-08-14 — Task 2 donor CUE and diagnostic capture

- Exact accepted source donor is `d7b04d61`; its current-dirty comparison found
  one Mario-only contract delta. The focused source policy test is GREEN 4/4
  after restoring material-RGB-times-light Gouraud entries, neutral polygon base,
  and direct textured `CC_REPLACE`. This is **host-contract-passed only** and
  remains uncommitted because no target build reached a CUE.
- Current normal-BOB build failed before SH-2 compile at unmodified
  `compile_actor_family_bundle.py` closure semantics. Disabling dynamic actors
  does not bypass unconditional actor package generation/sealing.
- Isolated donor worktree `d7b04d61` bound canonical ROM (SHA-256
  `17ce0773…21d91`), `build/us_pc` (1,977 files / 42,663,369 bytes), and Python
  environment, plus an exact pinned libyaul gitlink. The apparent `./tools/mio0` absence was caused by `make -n`
  propagating dry-run into the extractor's child tool build. A real extraction
  regenerated the donor BOB PNG and helpers, then stopped at its sound branch's
  literal `python3`, which selected the unavailable Windows App Execution Alias.
- Donor-only compatibility commit `5808cdbf` changes that extractor invocation
  to `sys.executable`, backed by a focused RED/GREEN test. It changes neither
  game, package, actor, nor renderer behavior. The donor then regenerated its
  own assets and Pipe-4 serial build produced a CUE/ELF/ISO:
  - CUE `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
  - ELF `60c978973e682f8a1cdb3c060d58b8038ce021f7718d8f9c5a430b5cfbaff8d5`
  - ISO `0e6eb40e868016df8b321187a27cd35738edcbe34d81938b01ef09a2484b3270`
- In-repo Ymir headless, with explicit BIOS/CUE/DRAM-cart (no inferred TOML),
  completed the USA-BIOS macro plus 1,800 game frames, for 3,300 emulated
  VBlanks. It wrote a 320x224 frame, SHA-256
  `401737ae49bb65bfe91f0488ff0ee7b4b7aed53701cb5cbcadd711df27696375`.
  It proves CUE boot and visible BOB terrain only. It does not prove correct
  Mario materials/depth/animation/controls, audible music/SFX, or FPS.
- A separate run held logical D-pad Up (`0x1000`) for 120 frames after the same
  boot and captured VBlank 3,420. Its frame SHA-256 is
  `f9dd7a3f133438aed8ab9d6950385b12b04e5690ee0a06ebeb4892d36be002b5` and
  visibly shows red/blue Mario on BOB ground. It is narrow evidence that the
  donor path reacts to input and renders Mario; it does not close any owner
  acceptance or audio/cadence requirement.
- A visible Ymir session was launched with that exact CUE and the desktop
  profile. Owner review is now the next acceptance boundary. Tasks 3–6 remain
  closed; do not repair the actor stack or Make graph before that result.
- Read-only donor preflight establishes that `source_audio_stub.c` is linked by
  this CUE and intentionally implements `play_sound`, `play_music`, and
  `audio_init` as no-ops. Silence is therefore expected by source, not an
  emulator configuration failure. This Mario recovery artifact is not an audio
  candidate; real audio remains a later causal live change after owner Mario
  acceptance.

## 2026-08-14 — recovery audit: stop duplicate Mario/audio attempts

- The focused A9A Mario source-policy contract is present in both the isolated
  donor and the current dirty renderer: material RGB times source light in the
  Gouraud table, neutral polygon base, and RGB1555 detail with `CC_REPLACE`.
  The test is a code guardrail only; it does not identify a new live behavior
  change.  A rebuild from that condition would be duplicate work and is not
  authorized.
- Four direct sourceboot audio CUEs were built as negative evidence.  Their
  final target reads left rebuilt `_s_active` as eight zero bytes after 1,200
  post-BIOS frames, including the final soundtest-style volatile driver/PCM
  copy route (`fdeeca232b1d028d25d5dbc0dd7c79ac661b53ebb3b1f04ce162ab428c2fa60e`).
  The same custom PCM68K binary reaches READY in standalone soundtest.  This
  sourceboot experiment exceeded the two-attempt budget and is terminal; no
  fifth variant or audible-success claim is permitted.
- The current generic BOB route remains rejected (zero presentation events in
  its bounded diagnostic; normal build may fail actor-family closure validation).
  The historical donor CUE is diagnostic-only and still awaits an owner visual
  verdict.  No Bob-omb, audio, cadence, or Whomp’s Fortress gate advances.
- Next action: capture/inspect the actual scene-ready donor VDP1 Mario records
  (order/link, PMOD, GRDA, base color, and texture source).  Only an observed
  mismatch may justify one local Mario patch and a new CUE.  If no mismatch is
  found, request the owner’s verdict on the named donor artifact rather than
  opening new implementation work.

## 2026-08-14 — scene-ready donor VDP1 command observation

- The first bounded probe stopped at 2,100 VBlanks and captured only the Sega
  license screen; it is invalid scene evidence.  The corrected explicit BIOS,
  DRAM-cart, and Up-input run reached VBlank 3,420 and reproduced the existing
  post-startup PNG SHA-256 `f9dd7a3f...002b5`.
- Both LWRAM command staging banks are populated and source-consistent:
  `0x00200000` has 894 nonzero commands/SHA-256 `aa46f9e2...60a1d`; and
  `0x00210000` has 920/SHA-256 `7624d479...b0d3`.  Mario-screen commands have
  RGB1555 Gouraud `CMDPMOD`, neutral `CMDCOLR=0xC210`, nonzero `CMDGRDA`, and
  RGB1555 `CC_REPLACE` detail sprites.  No color-mode/GRDA discrepancy exists
  for the donor; no Mario renderer patch or new CUE follows.
- The frame still visibly has bad terrain presentation and does not prove a
  normal Bob-omb, sound, cadence, or owner acceptance.  The next allowed
  action is the owner verdict on this exact CUE or a new probe that explains a
  specific remaining visual defect; Tasks 3–6 stay closed.
- A visible Ymir window was then launched for that manual verdict using only
  `ymir-agent/build-agent2/.../ymir-sdl3.exe --profile
  sm64-port/.ymir-profile --disc <named donor CUE>`; its local launch report is
  `build/saturn/task2-a9a-donor-vdp1-manual-launch-20260814.json` in the donor
  worktree.  It binds CUE `cdbf0bfa...f46dba7` and ISO
  `0e6eb40e...4b3270`.  No owner verdict has been recorded and other preexisting
  Ymir windows were left untouched.

## 2026-08-14 — sourceboot direct-SFX route retired

- The semantic-audio tree still cannot be used as a music donor because its
  linked MC68000 PCM image drops source sequence commands.  The isolated donor
  tested the real grass-jump event `0x04018080` through the soundtest-style
  cold-boot/mailbox/voice route, but four target CUE attempts left `_s_active`
  zero after 1,200 post-BIOS frames.  The standalone sound test reaches READY
  with the same custom PCM68K binary.
- This disproves the tested sourceboot integration variants; it does not prove
  an audible event, a music sequence, or an emulator configuration defect.  No
  more sourceboot-audio changes, manual launches, or architecture work are
  authorized from this preflight.  The next audio change requires telemetry
  that identifies the distinct game-entry-to-driver activation boundary.

## Non-negotiable stop rules

- Two failed live attempts or two hours on one regression triggers donor replacement or removal, not more abstraction.
- A host test, source review, release seal, or `source-complete` status does not advance this ledger.
- Do not open another architecture, wire-format, release, capacity, or generalized-level sprint before the two-level presentation gate.
- A launched artifact must be named and hashed before Ymir starts; stale or wrong-profile launches are invalid evidence.
