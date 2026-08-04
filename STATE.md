# State

See `docs/superpowers/plans/2026-08-03-saturn-overlapped-render-pipeline.md`.

Current active source slice: Task 5/A5.8 is migrating the accepted BOB
renderer from its fixed terrain/Mario worker to one dependency-aware,
descriptor-owned SH-2 queue. A5.5 ownership bridging and A5.8.1 graph-aware
runtime drains are source-review GO; the current live frame remains legacy
until terrain execution/merge and Mario transform/classify use exact
descriptor-owned payloads and can replace the sole CPU-DUAL callback atomically.

Terrain's dormant WORLD_LOWER callback now uses its exact descriptor input
span, bridge-derived physical output lane, and terminal-DONE reader contract;
the live legacy adapter is still authoritative. Descriptor-indexed WORLD_ADMIT
position publication, per-job result-count retention, and Mario's equivalent
route remain the blockers to a reviewable atomic cutover.

The accepted desktop-Ymir A3+A4 candidate remains the manual rollback baseline
at roughly 3–4 FPS (up from 1–2 FPS). It is qualitative evidence only; no new
target CUE or FPS claim is authorized until the live A5 cutover is reviewed,
built, and manually tested.
