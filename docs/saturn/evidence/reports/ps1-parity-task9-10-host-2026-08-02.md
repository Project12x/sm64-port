# PS1-parity Tasks 9–10: compact/fused terrain host evidence

Date: 2026-08-02  
Scope: host contracts only; no target build, CUE generation, hardware run, or
Ymir run was performed for this task.

## Result

Terrain classification now writes a 12-byte visible descriptor and a private
32-byte VDP1 command image into the owning master/slave lane. The lane seals
its span with the current nonzero frame sequence only after its descriptor and
command writes. The master validates both sequences, counts, descriptor shape,
and command indices before publishing compact sort references.

For valid immutable templates, classification patches CTRL/PMOD/COLR/SRCA,
SIZE, and final screen coordinates once. Emission copies that prepared command
and performs only the master-owned Gouraud allocation/address patch when the
material truly needs it. Dynamic LOD/recovery state and invalid templates keep
the per-entry legacy material-binding fallback.

## Exact static footprint change

The old result record was 56 bytes. The replacement descriptor is exactly 12
bytes; its paired private command image is 32 bytes. Therefore each allocated
lane slot changes from 56 bytes to 44 bytes, a net reduction of 12 bytes.

BOB defines 867 primitives and each lane has `867 * 2 = 1,734` clipped-result
slots. Across the two lane banks, `3,468 * 12` removes **41,616 bytes** of
LWRAM staging capacity. The two merge-reference banks remain two 8-byte fields
(record pointer plus sort key); command ownership is recovered from the record
range rather than adding a command pointer to every reference.

For every published result, the descriptor no longer transports four
projected vertices (32 bytes), four Gouraud colors (8 bytes), material ID (2
bytes), texture slot (2 bytes), or copied flags (2 bytes). It adds a 2-byte
private-command index, for a net 44-byte descriptor reduction before the
32-byte command image is counted.

The valid-template emitter no longer performs the second projected-area cross,
four-corner repack, metadata rebuild, template match, or template repatch.
Specifically, that removes four coordinate-record reads/eight scalar repack
writes, four subtracts/two multiplies/one subtract for the cross product, and
the per-result immutable material comparison/reconstruction. Classification
still performs the one authoritative area test and gathers final coordinates;
the final 32-byte copy into the master VDP1 arena remains.

## Host contracts

The deterministic fixture sends the same synthetic primitive set through the
existing split command patcher and the fused publication/merge API. It compares
primitive identity, BSP leaf, clip class, corner count, final coordinates and
command bytes, far-to-near painter order, and equal-depth source-ID tie order.
It includes both a template-patched record and the explicit fallback image.

Mutation coverage rejects:

- a stale slave publication sequence;
- a command index outside its owning lane;
- a merged count larger than the destination capacity;
- malformed two-corner descriptors; and
- clipped-fan reservations that would cross lane headroom.

Observed TDD sequence:

1. RED: the host runtime-contract compile failed on the intentionally missing
   visible descriptor, fused publication, sequence seal, and merge APIs.
2. GREEN: `make -f Makefile.saturn.mk ... verify-runtime-contracts` passed.
3. Compatibility: `verify-terrain-command-template` passed.

Both passing commands ran with MSYS2's `/usr/bin` and `/mingw64/bin` on PATH
and a workspace-local TMPDIR, so the compiler runtime DLLs remained on the
launch path.

## Ownership and provenance

The implementation retains the existing SlaveDriver close-port boundary from
`Lobotomy-Software/SlaveDriver-Engine` commit
`a8986591557b6e680550d3c23970284d3b38ff8f`, GPL-3.0-or-later,
`WALLS.C:1240-1408,1803-1950`: bounded disjoint result spans, one coarse
master/slave handoff, and one join. Reuse mode remains close-port/pattern
adaptation as recorded in `docs/saturn/UPSTREAM_CODE_LEDGER.md`; no additional
upstream source was copied. The command word layout continues to use libyaul
`6012f79f237773378c8014e70d8998ad95a38d98` through its public VDP1 API and the
existing compact-template adapter.

## Remaining target evidence

This host-only task does not prove SH-2 compilation, final linker headroom,
cache behavior under Ymir/hardware, pixel parity, or frame-time improvement.
Those remain intentionally deferred to the one serial target build and one BOB
comparison for the completed optimization wave. The stable merge sort also
remains; bounded ordering is a later task.
