# Memory Residency Campaign: 208-Slot Object-Pool Cut

**Date:** 2026-08-09
**Task:** Memory-residency campaign Task 4
**Verdict:** PASS for the canonical idle-boot remeasurement; this is not
evidence for pickup/hold or action-particle pressure.

## Owner decision and implementation

G1 approved the recommended **208-slot** object-pool capacity. The selection
keeps 208 / 138 = 1.507 times the measured idle-boot peak, clears the known
12,408 B HWRAM link deficit by a projected 7,048 B, and remains subject to
the overflow-latch safety gate.

OBJECT_POOL_CAPACITY now defaults to 240 unless the sourceboot-only
SATURN_OBJECT_POOL_CAPACITY Make variable supplies a non-empty override. The
effective capacity is a typed compiler-config identity parameter. The
occupancy harness requires the corresponding sealed identity spec and rejects
an ELF that does not contain its exact identity tuple, so a future overridden
build cannot be reported as the header fallback of 240.

## Map evidence

The old 240-slot flags-on map and the fresh 208-slot map contain:

| Build | gObjectPool size | Result |
|---|---:|---|
| id-f60aaf60fe7b5d06 (240) | 0x23a00 = 145,920 B | recorded baseline |
| id-2db3d6487ae1bb4d (208) | 0x1ee00 = 126,464 B | fresh target build |
| Delta | 0x4c00 = 19,456 B | exactly 32 times 608 B |

The fresh 208-slot target passed verify-sourceboot. Its identity has
effective-config hash
2db3d6487ae1bb4de25a302656247818b42a7062137f4912736d44bdce2c46db;
its ELF SHA-256 is
75d8f3141abccc83cad4676b641a6764e0c62675b14616854a1d086db786d134.

A final review rerun also passed verify-sourceboot with the same flags and
capacity. Its source-hash-sensitive identity is id-999bd5f4943c0267; the
sealed generated spec again records capacity 208, the map again reports
gObjectPool at 0x1ee00, and its ELF SHA-256 is
5a4315a0205b786eca06ecbca9b9b4e13b6e7c8b44f13bb5227c6e16f84602b6.

## Passthrough proof

The override-unset canonical build used 240 slots (id-e49afeb0d4f053ab,
map size 0x23a00). After forcing only object_list_processor.o to rebuild
with explicit SATURN_OBJECT_POOL_CAPACITY=240, the two full target ELFs
were byte-identical:

    unset default:  EB1FC628EF2CF523C59D3789645561D044463109CDE41176F4348B52A7A582D7
    explicit 240:   EB1FC628EF2CF523C59D3789645561D044463109CDE41176F4348B52A7A582D7

## 208-slot occupancy remeasurement

The artifact-bound capture is
memcamp-object-pool-occupancy-capacity-208-2026-08-09.json
(SHA-256 6deb606c0093de611253892ab34c19561da071346f4b7f0366275bd70f556ba6).
It used the sealed 208 identity spec (SHA-256
b794b6e6ecc43be43651d7727895fb1e05c351344f8baca468160dea6826a1ef) and
the ELF above.

- 20,100 requested post-BIOS frames; 21,600 emulated frames total.
- 67 post-BIOS samples, 66 valid; first valid sample at post-BIOS frame 600.
- Observed peak: **138** allocated.
- Observed allocation failures / overflow latch: **0**.
- Post-BIOS current occupancy: 0--137.

The canonical geo-walk configuration leaves Mario at BOB's default spawn. It
therefore still does not exercise pickup/hold, macro-object-dense traversal,
or action-particle bursts. A nonzero alloc_failures value in a later gate is a
fail-closed result and returns capacity selection to G1.

## Remaining gates

Task 4's specification and differential code-quality reviews passed after
correcting two stale target-header comments. Task 4 was committed as
`2b765df6`. Task 5 remains blocked on its broader interaction/particle-pressure
evidence, not merely on this idle-boot result.
