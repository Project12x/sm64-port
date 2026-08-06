# Saturn Iterative Geo-Walk Design

**Date:** 2026-08-06  
**Status:** owner-approved direction; implementation plan follows  
**Scope:** source Saturn runtime scene-graph traversal  
**First qualification scene:** Bob-omb Battlefield  
**End goal:** every linked SM64 level, actor geo tree, callback, and held-object path

## Decision

Replace the production Saturn source path's recursive
`geo_process_node_and_siblings()` descent with an explicit bounded continuation
stack in LWRAM. Preserve the inherited source traversal semantics; change only
where continuation state lives. Keep the recursive implementation available in
separate baseline/oracle builds for differential comparison.

The master SH-2 remains the sole owner of source graph traversal and mutable
source graph state. The slave SH-2 continues to consume immutable render/actor
jobs after the source snapshot is complete. No single-SH2 fallback is part of
this design.

## Why the stack must be removed from the call path

The current linked ELF allocates 0x1A4 bytes of locals plus 32 bytes of saved
state in each `geo_process_node_and_siblings()` invocation. The exception dump
from the textured BOB image recorded a master `SP=0x060026DC` against
`___master_stack=0x06004000`, an underrun of 0x19324 bytes, with a corrupted
PC/PR. This is approximately 228 live recursive walker frames. Enlarging or
moving the master call stack would hide the unbounded source of the failure and
consume memory needed by the full-game runtime.

## Semantic contract

The iterative walker must preserve all observable source behavior:

1. Sibling visitation order remains the linked-list order, with the same
   switch-case parent rule that limits a selected branch to its selected child.
2. `GRAPH_RENDER_CHILDREN_FIRST` runs the child traversal before the node's
   ordinary dispatch, exactly as the source path does.
3. Every `GraphNodeFunc` callback runs at the same logical enter point, with the
   same `GEO_CONTEXT_*` value and matrix pointer/allocator argument.
4. Matrix push/pop and Q16/float mirror refresh occur at the same enter/leave
   boundaries. Matrix-stack depth is tracked separately from traversal depth.
5. Camera, frustum, master-list, object, and held-object globals are restored
   after their subtrees exactly where recursive return currently restores them.
6. Animation globals and the held-object temporary animation state are saved and
   restored across nested subtrees.
7. Shared-child parent assignment and clearing occur at the same boundaries.
8. LOD admission, culling, generated/background display-list production, and
   callback-produced lists retain their current source decisions.

## Runtime architecture

`GeoWalkContext` owns a fixed array of `GeoWalkFrame` records in a dedicated
`.lwram_geo_traversal` NOLOAD section. A frame contains the current node or
continuation, sibling cursor, phase, matrix-depth token, and compact indices for
the saved graph/animation context. It contains no VDP1 command storage and is
CPU-only; SCU DMA never reads it.

The dispatcher operates as an enter/leave state machine:

```text
push sibling continuation
enter node and execute callback/pre-child work
if a child is admitted: push leave token, push child siblings
otherwise: execute leave token immediately
repeat until the root continuation is retired
```

Node-specific code is split only at existing child calls. Enter code performs
the current pre-child work; leave code performs the current post-child cleanup.
No handler may call `geo_process_node_and_siblings()` in the production path.

Overflow is fail-closed: latch a named diagnostic reason, capture the current
scene/level/area/generation and traversal depth, stop source display submission,
and leave the target in the existing diagnostic/reset path. Silent truncation or
continuing with a partially restored matrix/global context is forbidden.

## Capacity and placement

Traversal capacity is not a BOB constant. A build-time manifest computes the
maximum nested source geo depth across all linked level layouts, actor geo trees,
shared children, held-object paths, and generated callback declarations. The
manifest emits the capacity consumed by the sourceboot linker and rejects any
scene whose proven bound exceeds it. A fixed safety margin is included and is
covered by a mutation test.

The linker places the arena between tracked LWRAM owners and the reserved slave
stack, with alignment and non-overlap assertions. The map verifier reports the
arena start/end, capacity, high-water mark, and remaining LWRAM margin. The
arena is never placed in VDP1/SCU-visible storage, HWRAM command banks, or the
DRAM cartridge.

## Baseline and rollout

- Baseline build: inherited recursive source walk, used only for replay/oracle
  comparison and never as the production candidate.
- Candidate build: iterative walk enabled and source recursion statically
  rejected in the linked source path.
- Production build: candidate path is the default once the full-game depth,
  linked map, target exception, and manual BOB gates are green.

## Verification requirements

Host tests compare recursive and iterative walkers on synthetic graphs covering
all node kinds, siblings, children-first nodes, switches, LOD, camera/frustum,
master lists, objects, held objects, callbacks, animation state, shared-child
parenting, and overflow. The comparison checks event order, callback arguments,
matrix depth, global restoration, culling/admission decisions, and generated
display-list identity.

Source-policy tests reject direct recursive calls from production handlers and
verify the generated all-game depth manifest. The linked target gate checks the
LWRAM arena assertions, both SH-2 vector tables, and the source identity. Ymir
headless runs bracket the prior green transition; the manual gate uses the
profile-managed 32-Mbit DRAM cartridge and the exact new CUE. The existing
camera, texture, actor, audio, dual-SH2, VDP1, VDP2, and FPS gates remain
independent and are not declared green by host traversal tests.

## Explicit non-goals

- Do not disable the source geo walk.
- Do not select a smaller BOB-only arena.
- Do not move the whole master stack as the production fix.
- Do not run the source graph walker on the slave SH-2.
- Do not change camera semantics, texture lowering, actor content, or audio
  behavior in this traversal transition.
