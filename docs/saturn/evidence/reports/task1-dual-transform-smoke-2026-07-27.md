# Task 1 dual-transform slave smoke

The standalone `dualtransform` Yaul target was built and verified, then run
under Ymir with `--dram-cart`, BIOS input, and the handoff-yield sequence. The
fresh ELF symbol `_saturn_dual_smoke_result` resolved to `0x06015818`; the
16-byte probe decoded as:

| Field | Value |
| --- | ---: |
| magic | `0x4453` (`DS`) |
| version | 1 |
| size | 16 |
| status | 1 (pass) |
| fixture_count | 16 |
| mismatch_index | `0xFFFF` (none) |
| master_cpu_seen | 1 |
| slave_cpu_seen | 1 |

The master computed the expected output from the shared immutable job record;
the slave ran `sm64_saturn_ir_transform_batch()` over the same fixture bank
and wrote disjoint output arrays. The master then compared every view and
projected record byte-for-byte. This is the Task 1 CPU-agnostic smoke gate,
not a performance claim.

Evidence JSON: `task1-dual-transform-smoke-2026-07-27.json`.
