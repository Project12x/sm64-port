# Sprint 2: Cadence Recovery — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development. Tasks T2.2+ are elaborated only after T2.1's capture lands (constitution: measure before design). AGENTS.md binds all work; FPS gates are owner-observed.

**Goal:** Recover cadence from ~1.1 FPS toward the 4-6 FPS band by returning
per-frame-hot data to 32-bit HWRAM and cutting per-frame algorithmic waste,
without regressing the accepted visuals/audio (tags `sprint1-r1-accepted`,
`sprint1-r2-audio-clean`).

**Measured basis (Sprint 2 T1, `sprint2-t1-hwram-attribution.md`):**
- Net HWRAM growth vs A9A is **+44 B** — the problem is composition, not bloat.
  The 131,072 B VDP1 cmdt double-buffer was repatriated to HWRAM, paid for by
  evicting **102,056 B** of hot data to 16-bit LWRAM: the 54,080 B three-way
  split **plus `_sourceboot_fast3d` (44,616 B, per-frame hot, previously
  unsuspected)**.
- True HWRAM slack today: 504 B. Cart free: **419,024 B** (corrected — not
  148 KB). LWRAM remaining: 0x96C0.
- libyaul silently owns ~55 KB of HWRAM `.bss` (`_private_pool` alone 40 KB).
- Cadence profile: 53 VBlanks/frame — scene construction ~24.5, master
  finalization ~5.8.

**Reclamation packages (from T1's ranked table):**
- (a) return the 43,776 B workarea: VDP1 cmdt capacity 2048→1664 (24,576 B)
  + libyaul `_private_pool` 0xA000→0x4000 (24,576 B, **external patch — the
  dependency is read-only; use a patch file/fork per global rules**) = 49,152 B.
- (b) full un-split + `_sourceboot_fast3d` home requires adding
  `GFX_POOL_SIZE` 6400→4096 (18,432 B) → 63,488 B, SMPC pool as reserve.
- ALL capacity shrinks are gated on T2.1's instrumented capture of observed
  peaks (VDP1 command_count max, TLSF high-water, display-list entries).

**Owner directives in force:** consult SlaveDriver/Z-Treme before designing
(T2.0); use the 4 MB cart wisely — cold tenants may move to cart (419 KB free),
hot tenants never (A-bus latency).

---

### Task T2.0: Reference sweep — SlaveDriver & Z-Treme (read-only)

`work/upstream/` clones. Extract, with file:line citations: (1) memory-tier
placement policy — what each engine keeps in HWRAM vs LWRAM vs cart-mapped
space, especially VDP1 command lists and per-frame scratch; (2) draw-order
machinery — how Z-Treme orders VDP1 commands (bucket/radix/counting sort?)
for the T2.4 counting-sort design; (3) command-buffer sizing — how large their
per-frame VDP1 lists are and whether they double-buffer in work RAM or build
in place. Deliverable: `docs/saturn/evidence/reports/sprint2-t2_0-reference-sweep.md`
with a "lessons applied to T2.2-T2.4" section. No code.

### Task T2.1: Instrumented peak capture (gates every capacity shrink)

Discover what the existing profile/trace rails already publish (render
snapshot counters, cadence trace, VDP1 frame-bank stats) before adding
anything. Required peaks over a full scripted-route run (ROUTE_REPLAY=1
exercises movement — do NOT rely on an idle boot; input-less-run lesson):
max per-frame VDP1 command_count; libyaul TLSF/`_private_pool` high-water;
max `gGfxPool` display-list usage; SMPC pool usage. Missing counters get
NOLOAD diagnostic fields gated behind `SATURN_DIAGNOSTIC_MODE=1` — the
product build stays untouched. Deliverable: measured peaks + verdict per
shrink candidate (safe / unsafe / needs-margin), committed as evidence.

### Task T2.2: Reclamation package + un-split — **complete (memory), cadence NEGATIVE**

Apply the verdict-approved shrinks; libyaul change via patch file under
`tools/patches/` (or the project's established mechanism — investigate; never
edit `third_party/` in place). Return the workarea + actor scratch (and
`_sourceboot_fast3d` if package (b) clears) to HWRAM. Gates: link +
`verify-memory-map` OK with ≥0x1F00; `verify-audio-loop-contracts` +
`verify-pcm68k-model` green; FPS capture vs the 1.1 baseline; owner
look-and-listen (no visual/audio regression).

**Status 2026-08-15 — evidence
`docs/saturn/evidence/reports/sprint2-t2_2-reclaim-unsplit.md`, candidate
`id-6b7c7e5d5f71e809`, commits `971f8f93`, `26ae9c38`, `98dab715`,
`03c697f5`:**

- Memory objective MET. 67,584 B recovered (cmdt 2048→1664,
  `GFX_POOL_SIZE` 6400→4096, libyaul `_private_pool` 0xA000→0x4000 via a
  build-time-staged patched copy of the one MIT translation unit, linked
  ahead of `-lyaul`; SMPC 14→4 skipped — it needs a full driver-TU
  supersede and the arithmetic closes without it). Full 54,080 B hot set
  returned to HWRAM. `verify-memory-map` RESULT OK; `hwram_remaining`
  0x20D8 → **0x5578**, true slack over the floor **472 B → 13,944 B**.
- Gates green: `verify-memory-map` OK, `verify-audio-loop-contracts` 24,
  `verify-pcm68k-model` 18, work-storage contract 4 (retargeted to the
  all-HWRAM policy + mutation-verified), staging-relocation contract
  repaired (it was already failing at base HEAD).
- **Cadence objective NOT met: 1.068 FPS sustained vs the R1 baseline's
  comparable 1.071 (−0.24%, no material change).** All phases unchanged
  (construction 24.72 VBlanks/frame, master finalization 5.80). No
  regression either: queue clean, zero SH-2 exceptions.
- Confound recorded: `_sourceboot_fast3d` (44,616 B, per-frame hot) is
  still in LWRAM — T1 measured full A9A hot-set residency at ~98,192 B, so
  the LWRAM hypothesis is half-tested, not refuted. Next rung is T2.0 L3's
  build-in-VRAM staging window, not more tier shuffling.
- **Owner gate still open:** look-and-listen on `id-6b7c7e5d5f71e809` for
  any visual/audio regression from the capacity cuts.
- Design correction for T2.3+: the evidence now points at the algorithmic
  levers (construction = 44% of a 56-VBlank frame), which is exactly
  T2.3's counting sort. Cadence recovery should not be expected from
  further memory-tier work.

### Task T2.3: Painter relink counting-sort (existing board task)

O(64×~1,800)/frame → one counting pass. Design informed by T2.0's Z-Treme
findings. Host test pins chain equivalence (same far-to-near order, stable
within bins) before/after. FPS capture isolates its contribution.

### Task T2.4+: next levers, planned on T2.2/T2.3 results

Candidates from evidence, not yet committed to: Mario command-count diet
(638/882 items), scene-construction hot-path profiling (24.5 VBlanks),
master-finalization (5.8). One change → one CUE → one measurement each.

**Sprint gate:** owner-observed cadence materially above 1.1 FPS with accepted
visuals/audio intact. The 4 FPS floor re-binds on the sprint's accepted result.
