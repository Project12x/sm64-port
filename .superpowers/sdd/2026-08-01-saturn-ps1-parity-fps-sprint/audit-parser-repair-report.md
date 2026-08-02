# Wave 1 audit parser/null-proof repair report

## Status

Complete for the scoped host-only parser repair. The fresh sourceboot ELF now
recovers all 17 statically direct sites as direct facts (16 audited math-helper
calls plus `_random_float`) and suppresses the exact configured-null
`_exec_display_list` callback block. The four genuine data-driven dispatchers
remain unresolved. The audit gate intentionally remains closed until a
separate task derives and checks their dispatcher manifests.

No route oracle, BOB route manifest, audit contract, target source, generated
artifact, or baseline was changed. `EXPECTED_TOTAL 582` remains unchanged.

## Files changed

- `tools/saturn/verify_sh2_native_math.py`
- `tools/saturn/test_verify_sh2_native_math.py`
- `.superpowers/sdd/2026-08-01-saturn-ps1-parity-fps-sprint/audit-parser-repair-report.md`

## Reference-code-first record

This change is an architecture-specific extension of the repository's existing
SH-2 abstract interpreter, not an implementation of a reusable external
algorithm or library. Before editing, I inspected the local audit brief and
delta disposition, the current verifier at producer commit
`95052e0345258697319b767b17602951f55efa36`, and the fresh sourceboot assembly
and ELF shapes produced by the pinned Wave 1 lineage (`afb1d37`; ELF SHA-256
`c827ae034df782f007cc464d20c1752ef9f3459b04f5f70eaf5ef60d59c50730`).
No compatible external upstream implementation was named or applicable.
Reuse mode: close extension of local analyzer patterns and test-fixture shapes.

## TDD evidence

### Static-call recovery red

The fresh-shaped spill/reload fixture was first added against the unchanged
analyzer. Its focused run failed because the expected direct-call facts were
absent: the analyzer poisoned exact literal spills after stores through output
pointers. The fixture was then tightened to the exact 17 fresh sites, including
the non-helper `_random_float` call.

Green assertion: the fixture now requires the exact caller/offset/target set
for all 17 sites and requires zero unresolved transfers.

### Configured-null proof red

The null fixture was updated to the fresh `_main` / configure /
`_exec_display_list` shape and given a decoded-line seed inside the guarded
callback block. Before the repair, the focused test failed with an
`UnresolvedTransfer` for `_exec_display_list` from the `decodedline` seed even
though `prove_sourceboot_null_task_submit()` had proved the submit slot null.

Green assertion: only the exact load/test/branch-proven dead block is excluded
from disconnected decoded-component seeds. The paired non-null/unknown fixture
still reports the callback, and an unrelated disconnected seed receives
fail-closed argument-alias state.

## Implementation

### Non-escaped frame provenance

The interpreter now tracks a small stack-alias property independently from
the existing abstract register value. Incoming `r4`-`r7` values at a real
function entry may point into caller storage, but cannot name the callee's
fresh local frame until a local stack address escapes. Disconnected decoded
seeds keep conservative may-alias state.

Stack spills and reloads retain this property; joins use logical OR; calls and
unknown operations conservatively restore may-alias state. Stores through a
proven symbol/external pointer therefore no longer poison unrelated exact
literal slots in a fresh, non-escaped frame. Stores through unknown pointers,
possible stack pointers, escaped frame addresses, conflicting joins, and
unknown overwrites continue to poison the affected provenance. The missing
SH-2 extension forms used by the fresh code (`extu.w`, `exts.b`, and `exts.w`)
were modeled alongside `extu.b` without inventing symbols.

### Exact configured-null dead block

After the existing whole-image proof establishes the exact single-store,
zero-configured `_sTaskSubmit` contract, the analyzer recognizes only the
matching `_exec_display_list` slot-load, callback-load, `tst`, taken-null
branch, indirect transfer, and delay-slot shape. Those dead nodes are removed
only from decoded-component acceptance seeds. Unknown/non-null configuration
does not produce dead nodes and remains unresolved.

## Verification

### Focused host tests

Command (run from `tools/saturn`):

```text
..\..\.venv-saturn-tools\Scripts\python.exe -m unittest \
  test_verify_sh2_native_math.NativeMathCensusTests.test_sourceboot_null_task_submit_proof_clears_only_the_guarded_transfer \
  test_verify_sh2_native_math.NativeMathCensusTests.test_sourceboot_nonnull_or_unknown_task_submit_remains_unresolved \
  test_verify_sh2_native_math.CodeOnlyAnalysisTests.test_fresh_wave1_non_escaped_frame_spills_recover_seventeen_direct_calls \
  test_verify_sh2_native_math.CodeOnlyAnalysisTests.test_fresh_wave1_data_driven_dispatchers_stay_unresolved \
  test_verify_sh2_native_math.CodeOnlyAnalysisTests.test_disconnected_seed_does_not_assume_entry_argument_alias_provenance
```

Result: `Ran 5 tests in 0.014s` / `OK`.

### Complete host suite

```text
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
```

Result: `Ran 156 tests in 0.142s` / `OK`.

The complete suite includes the pre-existing fail-closed controls for unknown
stores, indexed stack overwrites, literal/unknown merges, conflicting stack
targets, escaped stack addresses, and dereferenced callbacks.

### Fresh full-ELF observation-only census

The existing sourceboot ELF was analyzed host-only with the repository's local
SH tools, `--audit-observation-only`, and `--analysis-mode code-only`. No build,
CUE generation, target execution, or emulator launch was performed.

Structured result (`C:\tmp\wave1-parser-after-null.json`):

- Parser SHA-256: `fda1b5daa1b51e2af70dbc62b807dddf5b8a11b24eb2b82c03fa4a4caaf8135e`
- ELF SHA-256: `c827ae034df782f007cc464d20c1752ef9f3459b04f5f70eaf5ef60d59c50730`
- Helper total observed: `881`
- Contract-before expected total: `582`
- Contract SHA-256: `87dabb51adc1c1cb6b646a826977658de305df086d1cfb21fc2c97a0bd6127e2`
- Unresolved indirect transfers: exactly `4`

The command exits nonzero only because these genuine dispatchers are not yet
listed in a checked oracle:

| Caller | Offset | Provenance |
| --- | ---: | --- |
| `_geo_call_global_function_nodes_helper` | `+40` | dynamic callback field |
| `_level_cmd_call` | `+18` | reached script callback |
| `_level_cmd_call_loop` | `+18` | reached loop-script callback |
| `_process_geo_layout` | `+90` | geo command jump table |

The previous `_exec_display_list +30` false positive is absent, as are all 17
static-call misses. The observed `881` is evidence from this repaired parser,
not authorization to repin the immutable `582` contract.

## Scope and remaining work

- The scoped parser/null-proof repair is complete.
- The gate remains closed by design because the four genuine dispatchers need
  separately derived, checked, closure-contributing manifests.
- This task did not add suppressions or fabricated direct facts and did not
  alter any oracle or route file.
- No target build, Ymir session, or CUE generation was run.
