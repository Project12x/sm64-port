# Task 7 implementation report — actor texture residency

## Status

Source-complete-pending-review from reconciled base `ce28a7d9`. Behavior commit
`7cbd06ed` is the Task 7
`feat(saturn): publish actor texture residency generations` transition. This
is host and freestanding SH-2 module evidence, not target
runtime evidence. Task 8/runtime/renderer/Ymir/release gates remain closed.

## Reconciliation and design corrections

- Plan, ledger, and HEAD agreed before edits; unrelated dirty/untracked work was
  inventoried and remains outside the explicit Task 7 stage.
- The legacy implementation lives in `saturn_ir_texture.c`, so the authorized
  correction adds `init_region` there and keeps the partition initializer a
  thin delegate. Task 6 IR binders are byte/behavior unchanged.
- Pinned libyaul's raw SCU-DMA calls return void. Activation instead uses the
  already-initialized, master-owned checked queue submit/wait boundary. No queue
  source or initialization path changed.
- Task 6's unchanged 16-byte scalar mapping typedef moved from the Yaul-facing
  material header to the lightweight publication header. The latter forward-
  declares Yaul's exact partition tag; `saturn_scene_residency.h` compiles
  without Yaul and owns one fixed 2,064-byte publication.
- Residency and immutable S64F package generations are distinct. The caller's
  nonzero/new residency generation is published while the revalidated bundle
  retains its own package generation for Task 8 reconciliation.
- S64F already rejects zero/repeated 32-bit bank IDs after full-hash/bank
  validation. Residency therefore never deduplicates: canonical mapping count
  equals selected v2 variant count, and a scalar collision fails before DMA.

## Reference/reuse record

- libyaul gitlink `6012f79f237773378c8014e70d8998ad95a38d98`, MIT:
  `libyaul/scu/bus/b/vdp/vdp1/vram.h` and `libyaul/scu/scu/dma.h` inspected for
  the exact partition tag, sizes, and void DMA API. Reuse is dependency/API
  adaptation.
- In-tree `slavedriver_dma_queue.*`, GPL-3.0-or-later close-port of SlaveDriver
  Engine `a8986591557b6e680550d3c23970284d3b38ff8f`: checked submit/wait API reused
  unchanged.
- In-tree actor bundle/bank/material, IR residency, scene residency, frame-bank,
  Gouraud, render-snapshot, and publication-fence patterns inspected at the
  reconciled base. Reuse is same-repository close-port/shared-core extension;
  no new external code was copied.

## RED

- `make -f Makefile.saturn.mk verify-actor-texture-residency` exited 1 because
  `saturn_actor_texture_residency.c` did not exist.
- The exact combined `make -k -f Makefile.saturn.mk
  verify-actor-texture-residency verify-scene-residency` also exited 1: the
  actor target lacked its implementation and scene tests lacked the planned
  publication type/accessors. Production files were still untouched.

## Implementation

- Activation first invalidates old publication, revalidates the exact supplied
  S64F view from bytes, resolves every embedded bank in canonical order, and
  computes all texture/CLUT counts and offsets without heap or pointer plan
  arrays. Every source/destination span is proved through its inclusive final
  byte before the first submit; transfer addresses are cast only after checked
  `uintptr_t` addition. Independent 8-byte texture and 32-byte CLUT alignment,
  capacity, nonoverlap, uint16 address-unit/index, and 128-mapping bounds are
  fail-closed.
- Upload submits and waits for each nonempty selected v2 texture/CLUT span via
  the checked SCU queue. Submit/wait failure may leave dirty VRAM but rebuilds
  no reachable table. After all transfers, scalar mappings/counts are rebuilt,
  fenced, the residency generation is written, fenced, and committed is
  written last. Lookup clears output and copies only an exact committed
  generation/bank match.
- The real BOB artifact proves 14 canonical v2 mappings, 16,640 texture bytes,
  2,816 CLUT bytes, and family 29/model `0x0080` Cannon. A mixed fixture proves
  v1 remains opaque/unmapped. Collision, view/hash/bank mutation, separate
  short regions, lifecycle flags, null/misaligned/wrapping/overlapping spans,
  malformed current state, zero/stale/reused generations, queue failures, and
  mid-transfer nonpublication are covered.
- Scene reset clears the owner. Failed staging clears a matching staged actor
  generation; a commit retains only the exact matching committed publication
  and clears stale/partial state; inactive unload clears a matching stale
  publication. Existing scene lifecycle return values and renderer state are
  unchanged.

## Verification

- Focused GREEN:
  `make -f Makefile.saturn.mk verify-actor-texture-residency
  verify-scene-residency verify-ir-texture verify-actor-material` — PASS.
- Direct Python history:
  `.venv-saturn-tools/Scripts/python.exe -m unittest
  tools.saturn.test_actor_bank_format tools.saturn.test_actor_bank_v2
  tools.saturn.test_actor_family_bundle -v` — 26/26 PASS.
- Broader PASS: VDP1 frame bank, checked DMA queue, Gouraud transfer, S64B-v2
  86 mutations, actor family report 47 families / 13 unsupported
  representatives / 14 records, family bank, mixed S64F 54 mutations, pose,
  meshlet plus invalid-span mutation, feature-off 6/6, and variant/source
  40/40. The family Make gate required the established explicit forward-slash
  Windows `SATURN_REPO_ROOT`; the default `/d/...` spelling is misread by the
  Windows Python pathlib invocation and is not a product failure.
- Exact sourceboot-equivalent GCC 14.3.0 SH-2 flags passed `-fsyntax-only` and
  real object emission for actor texture residency, IR texture, actor material,
  and scene residency. Final post-lifecycle object sizes are recorded after the
  last fresh run: 35,092, 23,144, 25,184, and 64,528 bytes respectively.
  These are module compiles only.
- Scoped Task 7 `git diff --check`, staged `git diff --check`, and explicit
  stage inventory pass. The stage contains exactly the 16 Task 7 production,
  test, build, CHANGELOG, plan, brief/report, and ledger paths listed above;
  the unrelated dirty/untracked set remains unstaged.

## Unchecked/failed adjacent gates

- `test_a8_deferred_transfer_runtime_contract.py` remains 5/7 in untouched
  sourceboot: it expects a persistent destination-poison state and terminal
  VDP2 camera coupling to the published VDP1 frame-bank object. These are
  pre-existing Task 8-era sourceboot requirements; Task 7 does not modify or
  weaken them. The broader transfer-pipeline umbrella therefore remains
  unchecked despite its C executable and first source checks passing.
- Independent Task 7 spec/quality review is mandatory before Task 8 opens.
- Target link/run, runtime activation, renderer, Ymir, release, smoke, visual,
  desktop, manual, retail, total-game, and Task 8+ gates remain unchecked.

## Independent review and fix round 1/5

Review of `ce28a7d9..97dae9b2` returned C0/I4/M1. The repair is active and no
finding is waived:

1. Staging access must never expose an older active/other-generation
   publication; only canonical empty state or the exact staging generation is
   writable.
2. Activation prestate and lookup must share complete bounded publication
   validation, including unused entries, ordering, duplicates, totals, and
   every mapping invariant.
3. SCU DMA must never consume a CART bundle pointer directly. Activation gains
   a caller-owned bounded HWRAM span stage and uses the queue's single public
   read-only request preflight before any copy/submit; one span is CPU-copied
   then submitted/waited at a time.
4. The residency Make gate must build/verify the Task 5 real bundle on a fresh
   checkout while preserving its no-clobber/stale-generation semantics.
5. Generation ordering uses explicit unsigned half-range serial arithmetic,
   not an implementation-defined unsigned-to-signed cast.

Focused repair RED/GREEN is complete and behavior is committed as `4c4c24a9`;
same-reviewer rereview and every Task 8+ gate remain pending.

## Owner convergence correction

The owner replaced the bespoke Cannon-demo milestone with a working generic BOB
gate targeted for 2026-08-14. Task 7 remains only the safety-critical residency
prerequisite. Task 8 must own the fixed HWRAM stage and canonical scene package;
Task 9 must admit the normal BOB actor set through the generic queue and
renderer. The first short Ymir smoke now precedes exhaustive target-capacity,
release reproduction, reseal, and final manual evidence.

The repaired Make dependency was exercised against an absent isolated output:
it rebuilt and validated the real 47-family/14-variant Task 5 bundle, ran 13
Python publication/inventory tests, and passed the actor residency C gate. The
same canonical generation remained fail-closed because its stale destination
already exists. The isolated generated output was removed after the pass.

Final memory-debt RED/GREEN adds the missing transport-owner proof. Activation
now accepts only a nonempty HWRAM stage (including the P2 alias), proves the
full reserved stage ends at or before `0x06100000`, and rejects CART, LWRAM,
one-byte HWRAM overflow, or overlap with the bundle, 2,064-byte publication,
texture partition, or CLUT partition before queue preflight or DMA. Native host
fixtures preserve ordinary pointer-overlap semantics behind a test-only define;
the freestanding SH-2 build uses only the physical Saturn address rule. The
focused C test and exact SH-2 `-m2 -mb -ffreestanding -Werror` syntax compile
pass. A fresh absent-output generation-1 build then passed in 209.3 seconds:
real bundle C validation was 47 families / 14 variants, the actor residency
fixture passed, and all 13 publication/inventory tests passed. Only the
task-created four-file temporary output directory was removed afterward.
