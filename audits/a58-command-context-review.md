# Peer Audit — 2026-08-04 18:00

## Context

- Prior agent: Codex subagent
- Task claimed: Bind ordered terrain commands and pointer-free P2 callback contexts for the dormant A5.8 terrain and Mario queue callbacks.
- Verdict: **NO-GO**
- Commit reviewed: `b42fb657bd5a135a2a3224757df9288588302830`
- Files examined: `src/port/saturn/gfx/saturn_demo_render.c`, `saturn_render_callback_context.[ch]`, `saturn_render_job_queue.[ch]`, `saturn_dual_frame_bank.h`, `saturn_terrain_depth_bins.h`, `src/port/saturn/sourceboot/Makefile`, the four new/changed host fixtures, `ARCHITECTURE.md`, `CHANGELOG.md`, `STATE.md`, `ROADMAP.md`, and the active plan/evidence report.
- Commands run: `git status --short`; `git show --stat --oneline b42fb657`; full commit diff; focused `rg`/`Get-Content` inspection; five independent Qt MinGW GCC C11 `-Wall -Wextra -Werror` compile/runs for terrain command stream, callback context, terrain route, actor route, and pre-existing terrain depth bins.
- Test result: all five strict host fixtures passed. No target link, CUE, Ymir, or hardware/cache run was performed or claimed by this review.

## Findings

### Critical

#### 1. The renderer callbacks cannot open a context published by the current frame path

The new renderer publisher is definition-only: `demo_render_queue_context_publish()` is declared `unused` at `src/port/saturn/gfx/saturn_demo_render.c:1820`, and the only occurrence in the renderer is that definition. Consequently every entry in `s_render_callback_contexts`, initialized at line 1343, remains not-ready and all four dormant callbacks fail in `demo_render_queue_context_open()` before doing work.

Terrain has a second disconnect. The callbacks open `s_terrain_queue_compact_context` (lines 1781, 2540, and 2570), but the live frame populates a different stack-local `compact` object at lines 3549-3551 and passes that only to the legacy fixed-range worker. The static queue context therefore retains zero-initialized `classify` and `spans` pointers even if a release record were added. This is not merely deferred CPU-DUAL registration: the supposedly source-complete callback context has no valid publication path to the snapshot it consumes.

The route fixtures do not expose this. They only assert that the callback function bodies contain the text `demo_render_queue_context_open` (`tools/saturn/render_job_terrain_route_source_test.c:91,99` and `render_job_actor_route_source_test.c:67,79`); they never execute a renderer callback through publication/open. The standalone callback fixture publishes a synthetic four-byte stack payload directly, so it cannot prove that the actual terrain or Mario callback can be opened.

### Important

#### 2. Peer aliasing stops at the outer context while callbacks dereference nested cached pointers

`sm64_saturn_render_callback_context_open()` applies `sm64_saturn_dual_frame_read_range()` only to `cached_payload` (`saturn_render_callback_context.c:75`). The returned P2 alias changes the address of the outer context but does not rewrite pointer values stored inside it.

That matters for both dormant routes. `demo_terrain_compact_context_t` stores `classify` and `spans` pointers (lines 1776-1779); the legacy frame's `classify` is stack-local (line 3525) and the terrain callbacks immediately dereference `context->classify` (lines 2548-2555). `demo_mario_transform_context_t` stores `vertex_refs`, which points at the mutable `.lwram_bss` array `s_actor_transform_refs` (lines 333 and 1154), and the queue transform reads it directly at line 2171. A slave claimant can therefore receive an uncached outer context yet follow a cached inner pointer to master-published, frame-varying data. The host helper deliberately collapses P1/P2 aliases, so the current tests cannot detect this target coherency failure.

Before cutover, these callback payloads must either be self-contained, use pointer-free identities resolved to claimant-local/P2 aliases, or explicitly re-alias every mutable nested range according to producer/reader ownership. A target/cache proof is still required afterward.

#### 3. The advertised corruption matrix is incomplete for both phases

The general open routine checks generation, index, phase, sequence, claim, byte count, and lane bounds, but the executable fixture does not mutate all of those fields for both terrain and Mario. The terrain block mutates phase/readiness/sequence and an API index, while the Mario block mutates phase/readiness/sequence/wrong claim (`tools/saturn/render_callback_context_test.c:84-159`). Neither block corrupts `payload_bytes`, `producer_lane`, stored `job_index`, or the stored post-publication generation. Cross-lane success is exercised only for Mario; terrain is opened only by the producing master in `run_claim()`.

This falls short of the requested direct corruption/stale/incomplete/wrong-claim/cross-lane coverage for both dormant phases, and it makes the plan/evidence statement that both phases cover the full matrix too strong.

### Minor

#### 4. The evidence report contradicts the committed target source list

The A5.8 evidence section says `saturn_render_callback_context.c` is “not yet added to the target source list,” but this same commit adds it to `src/port/saturn/sourceboot/Makefile`. The status should say that the source list is wired but target compilation/linking remains unverified.

### Discrepancies between summary and code

- “Terrain and Mario dormant callbacks open their statically bounded contexts” is not demonstrated in the renderer: no renderer publication call exists, and terrain opens an unpopulated static object rather than the populated frame-local context.
- “Cache-through peer selection” is true only for the outer payload address. It does not establish safe peer access to mutable pointer-referenced data inside the actual renderer contexts.
- “Both terrain and Mario cases reject corrupt phases, stale generations/sequences, incomplete publication, wrong claims, out-of-range identities, and cross-lane selection” exceeds the fixture coverage described in Finding 3.
- The evidence report's target-source-list statement is stale relative to the Makefile in the same commit.

## What was done well

- The ordered terrain command design is compact and coherent. The anonymous union preserves the eight-byte SH-2 emit-ref ABI, radix scatter copies the complete ref, the command-stream fixture verifies cross-stream sorting retains exact command identity, and the legacy path remains on `sm64_saturn_terrain_emit_ref_command()` through the explicit mode flag.
- The release record is genuinely pointer-free and 16 bytes, publication writes `ready` last through the cache-through alias, and open validates the current exact claimed queue job before returning access.
- No queue runtime registration or second CPU-DUAL owner was introduced; the default legacy renderer and master-owned final VDP1 emission remain intact.
- The new source is present in both the host target and sourceboot `SH_SRCS`, and the changelog/architecture documents clearly label the feature dormant rather than claiming a new FPS result.

## Recommended next actions

1. Add one renderer-owned snapshot/publication step that fills the exact static terrain and Mario payloads consumed by callbacks, then publishes every descriptor context before either CPU can claim it. Remove the stack-pointer dependency from the terrain queue context.
2. Make the actual callback payload graph cache-safe: eliminate mutable nested pointers or resolve each pointer-free bank identity through reader-lane-aware P1/P2 helpers inside the callbacks.
3. Add executable callback tests that invoke all four renderer callbacks after real publication and cover both producer lanes for both terrain and Mario; mutate generation, sequence, index, phase, payload bytes, producer lane, ready, and claim independently.
4. Correct the evidence report's target-source-list statement. Then run a target compile/link and section-symbol audit before accepting source GO; keep CPU-DUAL cutover gated on the later target/cache/manual proof.
