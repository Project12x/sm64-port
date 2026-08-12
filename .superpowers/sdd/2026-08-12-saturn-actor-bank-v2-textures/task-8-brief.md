# Task 8 brief — canonical actor scene-bundle owner

## Scope and base

- Reconciled approved Task 7 base: `dfa8b286`.
- Implement the minimum canonical S64P/S64F CART owner, generic bundle
  publication/resolution, fixed two-lane workspace, texture activation, and
  sourceboot boot owner. Do not implement production actor job/emitter cutover,
  the remaining unsupported BOB source states, Ymir, release, or manual gates.
- Preserve unrelated dirty/untracked work and stage only explicit Task 8 paths.

## Owner convergence and anti-debt rules

- There is one immutable package owner: root Make produces S64P/S64F and the
  relocation-neutral assembly input; sourceboot consumes and seals it.
- Do not create a second CD/cart streamer. Runtime aliases the final CART bytes.
- Do not add a persistent HWRAM upload array. The cold 2,560-byte transfer span
  is phase-borrowed from the idle VDP1 command bank during boot, every checked
  DMA retires before reuse, and the bank is returned before its first frame.
- Boot validation scratch and runtime actor workspace have disjoint lifetimes
  and must share one typed LWRAM union. No heap or pointer-bearing publication.
- Scene validation/source initialization must be at most 256 bytes of exact
  SH-2 stack per call. Bind persistent bytes and lifecycle in the plan before
  accepting source completion.

## Required behavior

- Package generation 14 contains one S64P root and exactly one CART
  `ACTOR_DEPENDENCIES` S64F-v3 payload with exact nonzero generation, path,
  content hash, and stable ID. Root scratch is zero and no terrain/collision/sky
  payload is duplicated.
- Package compilation, validation, and header emission require an explicit
  payload root. Absolute, escaping, missing, or mismatched payload paths fail
  before publication.
- The fixed 64-byte scalar bundle publication validates the exact S64F, records
  immutable package identity plus a nonzero residency generation, and commits
  last. Two lane claims use bundle-wide stride/capacity; stale, duplicate,
  half-range, mismatched, or malformed state fails closed.
- Resolution accepts ordinary snapshots, resolves any validated family/model
  variant, validates S64B v1/v2, binds the fixed workspace lane, and prepares
  generic pose/meshlet output. Production code must contain no Cannon, model,
  behavior, or family special case.
- Sourceboot validates CART package bytes, activates Task 7 texture residency,
  publishes the bundle only after package/texture generations agree, exposes a
  scalar probe, and invalidates on every failed transition.
- Scene residency stores dependency byte count/destination class in its owned
  staging/active slots rather than reconstructing large local views.

## Evidence and remaining gates

- RED: missing payload-root/runtime/source-owner interfaces plus measured
  3,920/3,420-byte scene begin/commit stack debt.
- GREEN: package schema/determinism; bundle runtime/source owner/scene/texture
  residency; S64B-v2, mixed S64F, pose, meshlet, queue, batch, neutrality, and
  feature-off gates; exact installed SH-2 syntax/object/stack and assembly.
- Full linked target is not substitutable by object evidence. Independent
  review must pass before Task 9. Task 9 must broaden the 20 currently
  unsupported drawable BOB selections and cut all 34 through the production
  job/emitter before Ymir.

## Reference/reuse record

- Pinned libyaul gitlink
  `6012f79f237773378c8014e70d8998ad95a38d98`, MIT: inspected VDP1
  `vram.h`, `cmdt.h`, and Yaul build definitions for partition layout, command
  bank lifetime, and SH tool invocation. Reuse is dependency/API adaptation;
  no external code is copied.
- In-tree source-cart, scene residency, Task 7 actor texture residency, actor
  bundle/bank/pose/meshlet, and sourceboot generated-assembly rules were
  inspected and extended by same-repository close-port/shared-core reuse.
