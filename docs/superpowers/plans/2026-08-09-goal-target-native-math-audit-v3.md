# Goal-Target Native-Math Audit V3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the historical v2 audit, bind the corrected 700-helper census to the exact fully integrated Task 5 ELF, and produce the build, package, 20,000-frame smoke, and visual evidence required for a new owner manual test.

**Architecture:** Audit-contract v3 adds an immutable expected ELF SHA-256 and fails before disassembly when the supplied artifact differs. The existing source-derived simulation route oracle remains shared by v2 and v3. The existing object-pool capture is extended with read-only cart, exception, cadence, and camera-yaw samples so one artifact-bound 20,100-frame run can enforce the campaign's nonvisual smoke gates; the existing HUD capture supplies the screenshot.

**Tech Stack:** Python 3 `unittest`, GNU Make/MSYS2, SH-2 binutils, Yaul sourceboot, headless and desktop Ymir, Markdown/JSON/PNG evidence.

## Global Constraints

- Historical `tools/saturn/sh2_native_math_sim_audit_contract_v2.txt` and `SIM_AUDIT_CONTRACT_V2_SHA256` remain byte-for-byte unchanged.
- V3 accepts only ELF SHA-256 `562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a`, embedded build identity `id-735756402029c2f4`, root `_game_loop_one_iteration`, total 700, and forbidden callers `_atan2_lookup`/`_atan2s`.
- V3 is explicitly selected for the Task 5 build; the Makefile's v2 default is unchanged.
- No runtime source, renderer behavior, object-pool capacity, target identity schema, or route-oracle version changes.
- Preserve all unrelated modified and untracked files. Stage only paths named by each task.
- Every behavior-changing commit updates `CHANGELOG.md` in the same commit.
- Do not mark target gates complete from host tests. Keep failed or unexecuted gates open in both this plan and `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`.
- Starting point: design commit `418161fa`; the campaign ledger, verifier, tests, and both v1 route oracles already contain uncommitted Task 5 reconciliation work.

---

### Task 1: Exact-artifact v3 contract and corrected route audit

**Files:**
- Create: `tools/saturn/sh2_native_math_goal_audit_contract_v3.txt`
- Modify: `tools/saturn/verify_sh2_native_math.py`
- Modify: `tools/saturn/test_verify_sh2_native_math.py`
- Modify: `tools/saturn/sh2_native_math_route_oracle_v1.txt`
- Modify: `tools/saturn/sh2_native_math_sim_route_oracle_v1.txt`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md`

**Interfaces:**
- Consumes: `file_digest(path: Path) -> str`, `baseline_digest(text: str) -> str`, existing v1 route oracles, and the sealed Task 5 ELF hash.
- Produces: `AuditContract.expected_elf_sha256: str | None`, `verify_audit_contract_target(contract: AuditContract, elf: Path) -> None`, checked-in v3 contract digest `80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5`, and a passing exact-artifact target audit.

**Execution ledger (2026-08-10):** **complete (Task 1 scope).** TDD RED
was observed when the new tests could not import the absent v3 constant; the
minimal versioned parser, integrity selector, exact ELF binder, and sealed
fixture then passed the 13-test v3/source-derived focused invocation. The
full verifier suite ran 229 tests: 228 pass and the documented unrelated
`test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites`
failure remains open. The direct DLL-safe audit of
`id-735756402029c2f4` took 391.7 s and exited 0 with audit total 700, no
unlisted unresolved transfer/effect failure, and neither forbidden caller.
Self-review checked parser fail-closed behavior, v2 compatibility, fixture
digest/ELF binding, and the reconciled source-derived owner/candidate changes.
No package, ISO, smoke, visual, or manual-acceptance gate is claimed here.
Commit: `fix(saturn): bind goal native math audit to sealed target` (this
Task 1 commit).

- [x] **Step 1: Add v3 parser and target-binding tests before implementation**

Add imports for `GOAL_AUDIT_CONTRACT_V3_SHA256` and
`verify_audit_contract_target`, then add these tests to
`NativeMathCensusTests`:

```python
def _v3_contract_text(self, elf_sha256: str = "a" * 64) -> str:
    return (
        "AUDIT_CONTRACT_VERSION 3\n"
        "EXPECTED_ROOT _game_loop_one_iteration\n"
        "EXPECTED_TOTAL 700\n"
        f"EXPECTED_ELF_SHA256 {elf_sha256}\n"
        "FORBIDDEN_CALLER _atan2_lookup\n"
        "FORBIDDEN_CALLER _atan2s\n"
    )

def test_v3_audit_contract_requires_one_lowercase_exact_elf_sha256(self) -> None:
    contract = parse_audit_contract(self._v3_contract_text())
    self.assertEqual(contract.version, 3)
    self.assertEqual(contract.expected_elf_sha256, "a" * 64)
    for replacement in ("", "A" * 64, "a" * 63, "g" * 64):
        text = self._v3_contract_text(replacement)
        with self.subTest(replacement=replacement):
            with self.assertRaises(ValueError):
                parse_audit_contract(text)
    duplicate = self._v3_contract_text() + f"EXPECTED_ELF_SHA256 {'b' * 64}\n"
    with self.assertRaisesRegex(ValueError, "duplicate expected ELF"):
        parse_audit_contract(duplicate)

def test_v2_audit_contract_rejects_exact_elf_directive(self) -> None:
    text = (
        "AUDIT_CONTRACT_VERSION 2\n"
        "EXPECTED_ROOT _game_loop_one_iteration\n"
        "EXPECTED_TOTAL 582\n"
        f"EXPECTED_ELF_SHA256 {'a' * 64}\n"
        "FORBIDDEN_CALLER _atan2_lookup\n"
    )
    with self.assertRaisesRegex(ValueError, "v2.*EXPECTED_ELF_SHA256"):
        parse_audit_contract(text)

def test_v3_target_binding_accepts_only_the_exact_file(self) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        elf = Path(temporary) / "target.elf"
        elf.write_bytes(b"sealed target")
        digest = hashlib.sha256(elf.read_bytes()).hexdigest()
        contract = parse_audit_contract(self._v3_contract_text(digest))
        verify_audit_contract_target(contract, elf)
        elf.write_bytes(b"sealed target!")
        with self.assertRaisesRegex(ValueError, "target ELF SHA-256 mismatch"):
            verify_audit_contract_target(contract, elf)

def test_v2_target_binding_remains_artifact_agnostic(self) -> None:
    contract = parse_audit_contract(
        "AUDIT_CONTRACT_VERSION 2\n"
        "EXPECTED_ROOT _game_loop_one_iteration\n"
        "EXPECTED_TOTAL 582\n"
        "FORBIDDEN_CALLER _atan2_lookup\n"
    )
    with tempfile.TemporaryDirectory() as temporary:
        elf = Path(temporary) / "any.elf"
        elf.write_bytes(b"any historical artifact")
        verify_audit_contract_target(contract, elf)

def test_main_rejects_wrong_v3_elf_before_invoking_sh_tools(self) -> None:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        elf = root / "wrong.elf"
        baseline = root / "baseline.txt"
        route_oracle = root / "route.txt"
        audit_oracle = root / "audit-route.txt"
        audit_contract = root / "audit-v3.txt"
        elf.write_bytes(b"wrong target")
        baseline.write_text(
            "BASELINE_VERSION 1\nHOT_CEILING 0\n", encoding="utf-8"
        )
        route_oracle.write_text(
            "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
            encoding="utf-8",
        )
        audit_oracle.write_text(
            "ROUTE_ORACLE_VERSION 1\nROOT _game_loop_one_iteration\n",
            encoding="utf-8",
        )
        audit_contract.write_text(
            self._v3_contract_text("a" * 64), encoding="utf-8"
        )
        with patch.object(
            bounded_verifier, "verify_baseline_integrity"
        ), patch.object(
            bounded_verifier, "verify_route_oracle_integrity"
        ), patch.object(
            bounded_verifier, "verify_audit_contract_integrity"
        ), patch.object(
            bounded_verifier, "run_command"
        ) as run_command:
            self.assertEqual(bounded_verifier.main([
                str(elf), str(baseline),
                "--route-oracle", str(route_oracle),
                "--audit-route-oracle", str(audit_oracle),
                "--audit-contract", str(audit_contract),
                "--objdump", "objdump",
                "--readelf", "readelf",
                "--addr2line", "addr2line",
            ]), 2)
            run_command.assert_not_called()
```

- [x] **Step 2: Run the new tests and confirm RED**

Run:

```powershell
$env:PYTHONPATH='tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe -m unittest `
  test_verify_sh2_native_math.NativeMathCensusTests.test_v3_audit_contract_requires_one_lowercase_exact_elf_sha256 `
  test_verify_sh2_native_math.NativeMathCensusTests.test_v2_audit_contract_rejects_exact_elf_directive `
  test_verify_sh2_native_math.NativeMathCensusTests.test_v3_target_binding_accepts_only_the_exact_file `
  test_verify_sh2_native_math.NativeMathCensusTests.test_v2_target_binding_remains_artifact_agnostic `
  test_verify_sh2_native_math.NativeMathCensusTests.test_main_rejects_wrong_v3_elf_before_invoking_sh_tools
```

Expected: FAIL because the v3 constant/helper/field do not exist or because
the parser still rejects version 3.

- [x] **Step 3: Implement the minimal versioned contract model**

Change the dataclass and add the exact validator:

```python
@dataclass(frozen=True)
class AuditContract:
    version: int
    expected_root: str
    expected_total: int
    forbidden_callers: frozenset[str]
    expected_elf_sha256: str | None = None


def verify_audit_contract_target(contract: AuditContract, elf: Path) -> None:
    if contract.version == 2:
        return
    assert contract.expected_elf_sha256 is not None
    actual = file_digest(elf)
    if actual != contract.expected_elf_sha256:
        raise ValueError(
            "audit contract target ELF SHA-256 mismatch: "
            f"expected {contract.expected_elf_sha256}, found {actual}"
        )
```

In `parse_audit_contract`, accept versions 2 and 3. Add a dedicated
`EXPECTED_ELF_SHA256` branch that rejects every token count except two,
recognizes exactly one value, validates it with
`re.fullmatch(r"[0-9a-f]{64}", value)`, forbids it in v2, and requires it in
v3. Preserve the v2 constructor result with `expected_elf_sha256=None`.

- [x] **Step 4: Add and pin the checked-in v3 contract**

Create the file with exactly these bytes and a trailing newline:

```text
# Immutable v3 goal-target source-simulation audit.
# Exact artifact: id-735756402029c2f4; ELF SHA-256 is pinned below.
# Historical audit contract v2 remains unchanged and is not superseded.
AUDIT_CONTRACT_VERSION 3
EXPECTED_ROOT _game_loop_one_iteration
EXPECTED_TOTAL 700
EXPECTED_ELF_SHA256 562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a
FORBIDDEN_CALLER _atan2_lookup
FORBIDDEN_CALLER _atan2s
```

Add:

```python
GOAL_AUDIT_CONTRACT_V3_SHA256 = (
    "80f662863f6af8c8d905717cc06504677eedf144e2f00eff7b254ee7e099cba5"
)
```

Change `verify_audit_contract_integrity` to use
`expected_digest: str | None = None`. When it is `None`, select
`SIM_AUDIT_CONTRACT_V2_SHA256` for v2 or `GOAL_AUDIT_CONTRACT_V3_SHA256` for
v3; reject every other version. Keep the explicit `expected_digest=` test
seam unchanged.

- [x] **Step 5: Prove the checked-in v3 fixture and mutation behavior**

Add:

```python
def test_checked_in_goal_audit_contract_v3_is_pinned(self) -> None:
    path = Path(__file__).parent / "sh2_native_math_goal_audit_contract_v3.txt"
    text = path.read_text(encoding="utf-8")
    contract = parse_audit_contract(text)
    verify_audit_contract_integrity(text, contract)
    self.assertEqual(contract.expected_total, 700)
    self.assertEqual(
        contract.expected_elf_sha256,
        "562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a",
    )
    with self.assertRaisesRegex(ValueError, "immutable audit contract digest mismatch"):
        verify_audit_contract_integrity(text.replace("700", "701"), contract)
```

- [x] **Step 6: Enforce the artifact check before SH tool invocation**

In `main`, after `args.elf.is_file()` and `elf_path = args.elf.resolve()`, add:

```python
if audit_contract is not None:
    verify_audit_contract_target(audit_contract, elf_path)
```

This call must precede the first `run_command([args.objdump, ...])`. The
behavioral `test_main_rejects_wrong_v3_elf_before_invoking_sh_tools` from
Step 1 is the regression guard: it requires exit 2 and proves `run_command`
was never reached. Do not add a source-text ordering assertion; it would
couple the test to implementation spelling rather than the fail-fast contract.

- [x] **Step 7: Run the v3 and source-derived oracle tests GREEN**

Run the new tests plus:

```powershell
$env:PYTHONPATH='tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe -m unittest `
  test_verify_sh2_native_math.NativeMathCensusTests.test_analysis_candidates_seed_each_declared_indirect_edge_endpoint `
  test_verify_sh2_native_math.NativeMathCensusTests.test_checked_in_sim_oracle_declares_complete_bob_callback_sets `
  test_verify_sh2_native_math.NativeMathCensusTests.test_checked_in_sim_oracle_matches_all_source_derived_dispatcher_sets `
  test_verify_sh2_native_math.NativeMathCensusTests.test_source_manifest_comparison_rejects_omitted_and_underived_callbacks `
  test_verify_sh2_native_math.NativeMathCensusTests.test_post_manifest_dispatchers_consume_only_matching_exact_dynamic_sites `
  test_verify_sh2_native_math.NativeMathCensusTests.test_static_near_match_remains_unlisted_in_each_post_manifest_family `
  test_verify_sh2_native_math.NativeMathCensusTests.test_checked_source_group_allows_shared_camera_callback_contribution
```

Expected: all PASS. Then run:

```powershell
$env:PYTHONPATH='tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_verify_sh2_native_math.py
```

Expected: PASS. If the known unrelated
`test_pinned_bob_null_camera_trigger_proof_removes_only_exact_two_sites`
failure reproduces unchanged at `418161fa`, record both invocations and keep
the broad-suite gate explicitly open; do not call it green.

- [x] **Step 8: Run the current exact ELF once with v3**

Run the verifier directly through the DLL-safe wrapper:

```powershell
$task5Elf = 'build\saturn\sourceboot\e2-bob-identity-id-735756402029c2f4\obj\sm64-saturn-sourceboot-e2.elf'
$task5Toolchain = 'D:\Code\RetroDev\sm64-saturn-port\work\yaul-install\bin'
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 `
  .\.venv-saturn-tools\Scripts\python.exe `
  tools\saturn\verify_sh2_native_math.py `
  $task5Elf `
  tools\saturn\sh2_native_math_baseline_v1.txt `
  --route-oracle tools\saturn\sh2_native_math_route_oracle_v1.txt `
  --audit-route-oracle tools\saturn\sh2_native_math_sim_route_oracle_v1.txt `
  --audit-contract tools\saturn\sh2_native_math_goal_audit_contract_v3.txt `
  --objdump "$task5Toolchain\sh-elf-objdump.exe" `
  --readelf "$task5Toolchain\sh-elf-readelf.exe" `
  --addr2line "$task5Toolchain\sh-elf-addr2line.exe"
```

Expected after approximately 7.5 minutes: exit 0, audit total 700, no
unlisted unresolved transfer/effect, and both forbidden callers absent.

- [x] **Step 9: Update behavior documentation and commit**

Under `CHANGELOG.md` `[Unreleased] / Fixed`, explain the stale callback owners,
under-approximating bootstrap candidates, preserved v2, and exact v3 binding.
Update both plans with tests, target-audit result, and remaining package/smoke
gates. Commit only the paths listed by this task:

```powershell
git add -- CHANGELOG.md `
  docs/superpowers/plans/2026-08-09-memory-residency-campaign.md `
  docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md `
  tools/saturn/verify_sh2_native_math.py `
  tools/saturn/test_verify_sh2_native_math.py `
  tools/saturn/sh2_native_math_route_oracle_v1.txt `
  tools/saturn/sh2_native_math_sim_route_oracle_v1.txt `
  tools/saturn/sh2_native_math_goal_audit_contract_v3.txt
git diff --cached --check
git commit -m "fix(saturn): bind goal native math audit to sealed target"
```

- [x] **Step 10: Self-review specification and code quality**

Self-review against `418161fa..HEAD` checked every v3 design requirement,
parser fail-closed behavior, v2 compatibility, exact digest/ELF binding,
source-derived indirect owners, and candidate selection. No repair was
required. Independent review remains a later integration gate; this task
does not claim any package, smoke, or owner gate.

---

### Task 2: Artifact-bound combined smoke capture

**Files:**
- Modify: `tools/saturn/capture_object_pool_occupancy.py`
- Modify: `tools/saturn/test_capture_object_pool_occupancy.py`
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md`

**Interfaces:**
- Consumes: the existing pool probe, `capture_sourceboot_throughput.decode_boot_trace`, `capture_sourceboot_throughput.decode_cadence_trace`, and one DLL-safe `sh-elf-nm` listing.
- Produces: every 300-frame sample's pool, signed `sAreaYaw`, cart probe, exception magic, boot trace, and stable cadence trace; final `smoke_acceptance` booleans.

- [ ] **Step 1: Write decoder and acceptance tests RED**

Add tests for these pure functions:

```python
def test_decode_signed_camera_yaw(self) -> None:
    self.assertEqual(capture.decode_s16_be([0x80, 0x00]), -32768)
    self.assertEqual(capture.decode_s16_be([0x7F, 0xFF]), 32767)

def test_decode_cart_probe_requires_ready_complete_ok(self) -> None:
    words = [0x53434152, 5, 3565776, 3565776, 0x5A, 4 << 20, 0]
    raw = b"".join(x.to_bytes(4, "big") for x in words)
    self.assertTrue(capture.decode_cart_probe(raw)["ready_complete_ok"])
    words[3] -= 1
    raw = b"".join(x.to_bytes(4, "big") for x in words)
    self.assertFalse(capture.decode_cart_probe(raw)["ready_complete_ok"])

def test_smoke_acceptance_requires_every_nonvisual_gate(self) -> None:
    samples = [
        {"label": "post-bios-9600", "magic_valid": True,
         "alloc_failures": 0, "area_yaw": 10, "exception_magic": 0,
         "cart": {"ready_complete_ok": True},
         "boot": {"vdp2_presentation_generation": 20}},
        {"label": "post-bios-9900", "magic_valid": True,
         "alloc_failures": 0, "area_yaw": 11, "exception_magic": 0,
         "cart": {"ready_complete_ok": True},
         "boot": {"vdp2_presentation_generation": 21}},
    ]
    self.assertEqual(capture.smoke_acceptance(samples), {
        "pool_alloc_failures_zero": True,
        "cart_ready_complete_ok": True,
        "exception_record_clear": True,
        "vdp_generations_climbing": True,
        "area_yaw_changes_9500_10000": True,
        "pass": True,
    })
```

Add negative subtests that independently freeze yaw, hold generations, set
exception magic `0x53484258`, set cart failure, and set allocation failures.

- [ ] **Step 2: Run the focused tests and confirm RED**

```powershell
$env:PYTHONPATH='tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe -m unittest `
  test_capture_object_pool_occupancy.ObjectPoolOccupancyTests.test_decode_signed_camera_yaw `
  test_capture_object_pool_occupancy.ObjectPoolOccupancyTests.test_decode_cart_probe_requires_ready_complete_ok `
  test_capture_object_pool_occupancy.ObjectPoolOccupancyTests.test_smoke_acceptance_requires_every_nonvisual_gate
```

Expected: FAIL because the decoders and `smoke_acceptance` do not exist.

- [ ] **Step 3: Implement read-only smoke decoding**

Import the existing stable boot/cadence decoders and add fixed ABI sizes:

```python
from capture_sourceboot_throughput import (
    BOOT_TRACE_BYTES, CADENCE_TRACE_BYTES,
    decode_boot_trace, decode_cadence_trace,
)

SMOKE_SYMBOLS = {
    "sAreaYaw": 2,
    "sourceboot_exception_record": 4,
    "g_sm64_saturn_source_cart_probe": 28,
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "sourceboot_cadence_trace": CADENCE_TRACE_BYTES,
}

def decode_s16_be(data: list[int] | bytes) -> int:
    raw = int.from_bytes(bytes(data), "big")
    return raw - 0x10000 if raw & 0x8000 else raw

def decode_cart_probe(raw: bytes) -> dict[str, Any]:
    if len(raw) != 28:
        raise ValueError("source cart probe has wrong size")
    words = [int.from_bytes(raw[i:i + 4], "big") for i in range(0, 28, 4)]
    result = dict(zip(
        ("magic", "stage", "expected_size", "copied_size", "cart_id",
         "cart_size", "status"), words,
    ))
    result["ready_complete_ok"] = (
        result["magic"] == 0x53434152
        and result["stage"] == 5
        and result["expected_size"] == result["copied_size"]
        and result["status"] == 0
    )
    return result
```

Resolve all symbols from one `wrapped_nm_command` result. At each existing
sample, read the cache-through address for each symbol while paused. Decode
the exception record's first word only; zero means no target exception was
recorded. Store the decoded fields beside the pool fields.

- [ ] **Step 4: Implement strict summary acceptance**

```python
def smoke_acceptance(samples: list[dict[str, Any]]) -> dict[str, bool]:
    valid = [sample for sample in samples if sample.get("magic_valid")]
    yaw_window = [
        sample for sample in valid
        if sample["label"] in ("post-bios-9600", "post-bios-9900")
    ]
    vdp = [sample["boot"]["vdp2_presentation_generation"] for sample in valid]
    checks = {
        "pool_alloc_failures_zero": bool(valid) and all(sample["alloc_failures"] == 0 for sample in valid),
        "cart_ready_complete_ok": bool(valid) and all(sample["cart"]["ready_complete_ok"] for sample in valid[-2:]),
        "exception_record_clear": bool(valid) and all(sample["exception_magic"] == 0 for sample in valid),
        "vdp_generations_climbing": len(vdp) >= 2 and vdp[-1] > vdp[0],
        "area_yaw_changes_9500_10000": len(yaw_window) == 2 and len({sample["area_yaw"] for sample in yaw_window}) == 2,
    }
    return {**checks, "pass": all(checks.values())}
```

Derive `route_note` from the sealed identity spec's `route_replay_mode` and
`live_input_mode`; remove the current hardcoded disabled-route claim. Include
`smoke_acceptance` in the JSON and return exit 1 when it is false.

- [ ] **Step 5: Run the complete capture suite GREEN**

```powershell
$env:PYTHONPATH='tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\test_capture_object_pool_occupancy.py
```

Expected: PASS, including all prior capacity/ELF identity tests.

- [ ] **Step 6: Update docs, commit, and review**

Update the changelog and both ledgers with the capture correction and open
target gates. Stage only the files listed by Task 2 and commit:

```powershell
git add -- CHANGELOG.md `
  docs/superpowers/plans/2026-08-09-memory-residency-campaign.md `
  docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md `
  tools/saturn/capture_object_pool_occupancy.py `
  tools/saturn/test_capture_object_pool_occupancy.py
git diff --cached --check
git commit -m "test(saturn): bind combined smoke to flags-on target"
```

Use `superpowers:requesting-code-review` on this commit. Require signed-yaw,
cache-through, cadence-seqlock, cart/exception, artifact-binding, route-note,
and 20,000-frame-floor review. Repair and rerun Step 5 before target use.

---

### Task 3: Reproduce, audit, and package the exact goal target

**Files:**
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/obj/sm64-saturn-sourceboot-e2.elf`
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/obj/sm64-saturn-sourceboot-e2.sym`
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/obj/SOURCE.DAT`
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/cd/SOURCE.DAT`
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/sm64-saturn-sourceboot-e2.iso`
- Verify/reproduce: `build/saturn/sourceboot/e2-bob-identity-id-735756402029c2f4/sm64-saturn-sourceboot-e2.cue`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md`

- [ ] **Step 1: Reconcile the implementation state before target work**

```powershell
git status --short
git log -5 --oneline
git diff --check
```

Confirm Task 1 and Task 2 commits and both independent-review verdicts are in
the two ledgers. Confirm no implementation file owned by those tasks remains
uncommitted. Preserve all unrelated dirty and untracked paths.

- [ ] **Step 2: Rebuild and run every ordinary target gate plus v3**

```powershell
$task5Contract = (Resolve-Path 'tools\saturn\sh2_native_math_goal_audit_contract_v3.txt').Path
powershell -ExecutionPolicy Bypass -File tools\saturn\with-msys-toolchain.ps1 `
  mingw32-make -f Makefile.saturn.mk -j1 verify-sourceboot `
  SATURN_DEMO_PATH=1 `
  SATURN_SOURCEBOOT_ROUTE_REPLAY=1 `
  SATURN_SOURCEBOOT_LIVE_INPUT=1 `
  SATURN_SOURCEBOOT_LIVE_INPUT_BOOTSTRAP_TICKS=600 `
  SATURN_SOURCEBOOT_LEVEL_ID=9 `
  SATURN_SOURCEBOOT_AREA_ID=1 `
  SATURN_SOURCEBOOT_ROUTE_ID=0 `
  SATURN_SOURCEBOOT_CAMERA_ROUTE=0 `
  SATURN_CAMERA_VARIANT=3 `
  SATURN_CAMERA_IDLE_START_TICK=0 `
  SATURN_CAMERA_IDLE_DISCOVERY=0 `
  SATURN_CAMERA_RANGE_CAPTURE=0 `
  SATURN_CART_MBIT=32 `
  SATURN_SOURCE_CART_STAGE_SECTORS=8 `
  SATURN_DEMO_HOT_PROMOTION=1 `
  SATURN_DEMO_NEAR_CLIP=1 `
  SATURN_DEMO_BSP_ORDER=1 `
  SATURN_DEMO_POLY_TIER=2 `
  SATURN_DEMO_BSP_FRAGMENTS=0 `
  SATURN_DEMO_FRAGMENT_MODE=0 `
  SATURN_DEMO_BSP_FRAGMENT_FLAT=0 `
  SATURN_RENDERER_PIPELINE=4 `
  SATURN_SLAVE_RENDER=1 `
  SATURN_ATAN2_VARIANT=2 `
  SATURN_DEMO_VIEW_RADIUS=6000 `
  SATURN_DIAGNOSTIC_MODE=0 `
  SATURN_FAST3D_Q16_TRACE=0 `
  SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0 `
  SATURN_FEATURE_COMPLETE_MARIO_ANIMATION=1 `
  SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE=1 `
  SATURN_FEATURE_SEMANTIC_AUDIO=0 `
  SATURN_OBJECT_POOL_CAPACITY=208 `
  SOURCEBOOT_NATIVE_MATH_SIM_AUDIT_CONTRACT=$task5Contract
```

Expected: identity directory `e2-bob-identity-id-735756402029c2f4`, ordinary
memory/native-math/coherency/package gates pass, and the v3 audit reports 700.
If a target gate fails, stop and diagnose it; do not package stale output as a
manual candidate.

- [ ] **Step 3: Reconfirm exact ELF identity and immutable hash**

```powershell
$task5Dir = 'build\saturn\sourceboot\e2-bob-identity-id-735756402029c2f4'
$task5Elf = "$task5Dir\obj\sm64-saturn-sourceboot-e2.elf"
$task5Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $task5Elf).Hash.ToLowerInvariant()
if ($task5Hash -ne '562fd6e47dd489f55f3c9d131ea2bca1fa417b8b3ce2c2ed90369db7d145978a') {
    throw "sealed Task 5 ELF drifted: $task5Hash"
}
$task5IdentityPath = 'build\saturn\sourceboot\generated\saturn_build_identity.json'
$task5SpecPath = 'build\saturn\sourceboot\generated\saturn_build_identity_spec.json'
$task5Identity = Get-Content -Raw $task5IdentityPath | ConvertFrom-Json
$task5ConfigHash = $task5Identity.identity.effective_config_hash
$task5IdentityId = 'id-' + $task5ConfigHash.Substring(0, 16)
if ($task5ConfigHash -ne '735756402029c2f43b4b9792077ca7f4393de9330a37ad12914c4c9ffb68ed59') {
    throw "effective config drifted: $task5ConfigHash"
}
if ($task5IdentityId -ne 'id-735756402029c2f4' -or $task5Dir -notlike "*identity-$task5IdentityId") {
    throw "identity directory does not match sealed config: $task5IdentityId"
}
$env:PYTHONPATH = 'tools/saturn'
.\.venv-saturn-tools\Scripts\python.exe -c `
  "from pathlib import Path; from capture_object_pool_occupancy import pool_capacity_from_sealed_artifact; assert pool_capacity_from_sealed_artifact(Path(r'$task5SpecPath'), Path(r'$task5Elf')) == 208; print('sealed identity tuple present in exact ELF')"
```

Require embedded identity tuple `id-735756402029c2f4` and effective-config digest
`735756402029c2f43b4b9792077ca7f4393de9330a37ad12914c4c9ffb68ed59`.
Any hash drift invalidates v3 and requires a newly reviewed contract/design;
do not edit the pinned hash opportunistically.

- [ ] **Step 4: Compute physical and usable low-RAM margins from the map**

```powershell
$task5Sym = "$task5Dir\obj\sm64-saturn-sourceboot-e2.sym"
$endLine = Select-String -LiteralPath $task5Sym -Pattern '^([0-9a-fA-F]+)\s+B\s+___end$'
if ($endLine.Count -ne 1) { throw "expected one ___end symbol" }
$endAddress = [Convert]::ToUInt32($endLine.Matches[0].Groups[1].Value, 16)
$physicalMargin = 0x06100000 - $endAddress
$usableMargin = $physicalMargin - 0x1B00
if ($physicalMargin -lt 0 -or $usableMargin -lt 0) { throw "low-RAM margin is negative" }
[pscustomobject]@{
    end_address = ('0x{0:x8}' -f $endAddress)
    physical_margin_bytes = $physicalMargin
    usable_margin_bytes = $usableMargin
}
```

Expected sealed-artifact reference: `___end = 0x060fc9d8`. Record both byte
margins; do not substitute cart capacity for low-RAM capacity.

- [ ] **Step 5: Prove cart payload and ISO agree byte-for-byte**

```powershell
$objSource = "$task5Dir\obj\SOURCE.DAT"
$cdSource = "$task5Dir\cd\SOURCE.DAT"
$iso = "$task5Dir\sm64-saturn-sourceboot-e2.iso"
$objInfo = Get-Item -LiteralPath $objSource
$cdInfo = Get-Item -LiteralPath $cdSource
$objHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $objSource).Hash
$cdHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $cdSource).Hash
if ($objInfo.Length -ne $cdInfo.Length -or $objHash -ne $cdHash) {
    throw 'obj and cd SOURCE.DAT differ'
}
$isoSourceLine = tar -tvf $iso | Select-String 'SOURCE\.DAT$'
if ($isoSourceLine.Count -ne 1) { throw 'ISO must contain exactly one SOURCE.DAT' }
$cartStartLine = Select-String -LiteralPath $task5Sym -Pattern '^([0-9a-fA-F]+)\s+R\s+___sourceboot_cart_rodata_start$'
$cartEndLine = Select-String -LiteralPath $task5Sym -Pattern '^([0-9a-fA-F]+)\s+R\s+___sourceboot_cart_rodata_end$'
if ($cartStartLine.Count -ne 1 -or $cartEndLine.Count -ne 1) {
    throw 'expected one linked cart start and end symbol'
}
$cartStart = [Convert]::ToUInt32($cartStartLine.Matches[0].Groups[1].Value, 16)
$cartEnd = [Convert]::ToUInt32($cartEndLine.Matches[0].Groups[1].Value, 16)
$cartBytes = $cartEnd - $cartStart
if ($objInfo.Length -ne $cartBytes) {
    throw "SOURCE.DAT size $($objInfo.Length) != linked cart span $cartBytes"
}
$isoSourceText = $isoSourceLine.Line.Trim()
if ($isoSourceText -notmatch "(?:^|\s)$cartBytes(?:\s|$)") {
    throw "ISO SOURCE.DAT listing does not contain linked size $cartBytes"
}
[pscustomobject]@{
    source_dat_bytes = $objInfo.Length
    source_dat_sha256 = $objHash.ToLowerInvariant()
    iso_listing = $isoSourceText
}
```

Expected linked cart span and `SOURCE.DAT` size: 3,565,776 bytes. Check the
ISO listing contains that same size before using the CUE.

- [ ] **Step 6: Record source-complete status without closing target smoke**

Update both plans with the exact build command, ELF/config digests, v3 result,
low-RAM margins, cart payload result, and ordinary target gates. Mark Task 5
`source-complete` only. Leave combined headless smoke, screenshot inspection,
manual acceptance, and Task 6 automation explicitly unchecked.

---

### Task 4: Run combined smoke, inspect visuals, and prepare manual handoff

**Files:**
- Create: `docs/saturn/evidence/reports/memcamp-flags-on-combined-smoke-2026-08-09.json`
- Create: `docs/saturn/evidence/reports/memcamp-flags-on-hud-2026-08-09.json`
- Create: `docs/saturn/evidence/reports/memcamp-flags-on-hud-2026-08-09.png`
- Create: `docs/saturn/evidence/reports/memcamp-flags-on-manual-ready-2026-08-09.md`
- Create after owner approval: `docs/saturn/evidence/reports/memcamp-flags-on-manual-launch-2026-08-09.json`
- Modify: `docs/superpowers/plans/2026-08-09-memory-residency-campaign.md`
- Modify: `docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md`

- [ ] **Step 1: Run the artifact-bound 20,100-frame combined smoke**

```powershell
$task5Dir = 'build\saturn\sourceboot\e2-bob-identity-id-735756402029c2f4'
$ymir = 'D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe'
$bios = 'D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin'
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_object_pool_occupancy.py `
  --ymir $ymir `
  --ipl $bios `
  --game "$task5Dir\sm64-saturn-sourceboot-e2.cue" `
  --elf "$task5Dir\obj\sm64-saturn-sourceboot-e2.elf" `
  --identity-spec build\saturn\sourceboot\generated\saturn_build_identity_spec.json `
  --post-bios-frames 20100 `
  --sample-interval 300 `
  --timeout 1800 `
  --output docs\saturn\evidence\reports\memcamp-flags-on-combined-smoke-2026-08-09.json
```

Expected: exit 0, exactly 67 post-BIOS samples, peak occupancy at or below
208, zero allocation failures, cart ready/complete/OK, clear exception record,
rising VDP generations, and differing yaw at post-BIOS 9600 and 9900. Any
failed acceptance boolean fails the combined smoke; keep Task 5 open.

- [ ] **Step 2: Capture the HUD/scene screenshot from the verified target CUE**

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\capture_sourceboot_hud_state.py `
  --ymir $ymir `
  --ipl $bios `
  --game "$task5Dir\sm64-saturn-sourceboot-e2.cue" `
  --startup-frames 3600 `
  --expect-row 0 `
  --expect-col 1 `
  --expect-glyph-index 12 `
  --timeout 1800 `
  --output docs\saturn\evidence\reports\memcamp-flags-on-hud-2026-08-09.json `
  --screenshot-output docs\saturn\evidence\reports\memcamp-flags-on-hud-2026-08-09.png
```

Expected: exit 0 and the report names the exact CUE whose ELF/package binding
was proved in Task 3. Verify the report's resolved `game` path equals that CUE,
then inspect the PNG with the image viewer. Require the HUD at the top, textured
terrain, and a visible Mario/actor. A spawned-but-invisible actor fails the
visual gate. Bind the CUE/ISO/SOURCE.DAT hashes beside this report in Step 3.

- [ ] **Step 3: Write the reproducible manual-ready report**

In `memcamp-flags-on-manual-ready-2026-08-09.md`, record:

- commit and exact ELF/config SHA-256 values;
- the complete effective flag tuple from Task 3 Step 2;
- v3 audit total, low-RAM margins, cart bytes/hash, ISO/CUE paths;
- combined-smoke sample count, peak, zero failures, cart/exception/cadence/yaw verdicts;
- HUD report and screenshot paths plus the observed visible scene;
- remaining manual checklist: pickup/hold interaction, sustained camera
  behavior, textures/HUD, visible actor, stability, and perceived FPS.

Update both ledgers in the same transition. Mark automated Task 5 gates with
their exact evidence but leave owner manual acceptance open. Never mark Task 6
complete from these Task 5 probes.

- [ ] **Step 4: Validate and commit evidence/documentation only**

```powershell
.\.venv-saturn-tools\Scripts\python.exe -m json.tool `
  docs\saturn\evidence\reports\memcamp-flags-on-combined-smoke-2026-08-09.json > $null
.\.venv-saturn-tools\Scripts\python.exe -m json.tool `
  docs\saturn\evidence\reports\memcamp-flags-on-hud-2026-08-09.json > $null
git diff --check
git add -- `
  docs/saturn/evidence/reports/memcamp-flags-on-combined-smoke-2026-08-09.json `
  docs/saturn/evidence/reports/memcamp-flags-on-hud-2026-08-09.json `
  docs/saturn/evidence/reports/memcamp-flags-on-hud-2026-08-09.png `
  docs/saturn/evidence/reports/memcamp-flags-on-manual-ready-2026-08-09.md `
  docs/superpowers/plans/2026-08-09-memory-residency-campaign.md `
  docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md
git diff --cached --check
git commit -m "docs(saturn): qualify flags-on target for manual acceptance"
```

- [ ] **Step 5: Run completion verification, then request owner launch approval**

Use `superpowers:verification-before-completion` to recheck the committed
evidence, exact hash, both ledger states, and clean ownership of Task 5 files.
Report the automated verdict and the still-open manual checks. Only after the
owner explicitly confirms launch, run:

```powershell
.\.venv-saturn-tools\Scripts\python.exe tools\saturn\launch_ymir_desktop.py `
  --cue "$task5Dir\sm64-saturn-sourceboot-e2.cue" `
  --profile .ymir-profile `
  --launch `
  --monitor-seconds 15 `
  --output docs\saturn\evidence\reports\memcamp-flags-on-manual-launch-2026-08-09.json
```

Hand the owner the pickup/hold, camera, textures/HUD, visible-actor, stability,
and perceived-FPS checklist. Record their actual verdict before closing Task 5.
After the owner reports the result, validate and commit the launch record and
ledger transition:

```powershell
.\.venv-saturn-tools\Scripts\python.exe -m json.tool `
  docs\saturn\evidence\reports\memcamp-flags-on-manual-launch-2026-08-09.json > $null
git add -- `
  docs/saturn/evidence/reports/memcamp-flags-on-manual-launch-2026-08-09.json `
  docs/saturn/evidence/reports/memcamp-flags-on-manual-ready-2026-08-09.md `
  docs/superpowers/plans/2026-08-09-memory-residency-campaign.md `
  docs/superpowers/plans/2026-08-09-goal-target-native-math-audit-v3.md
git diff --cached --check
git commit -m "docs(saturn): record flags-on manual acceptance"
```

If the owner finds a defect, record the failed checklist item and leave Task 5
open; do not use the acceptance commit subject.

---

## Final verification checklist

- [ ] V2 contract bytes and pinned digest are unchanged.
- [ ] V3 rejects a wrong ELF before the first SH tool invocation.
- [ ] Exact ELF and effective-config digests match the approved design.
- [ ] Verifier and capture suites have fresh, recorded outcomes.
- [ ] Independent reviews cleared Task 1 and Task 2.
- [ ] Ordinary target gates, v3 total 700, RAM margins, cart/ISO package, and
  all combined-smoke booleans have artifact-bound evidence.
- [ ] Screenshot inspection confirms HUD, terrain, and a visible actor.
- [ ] Both ledgers agree with Git HEAD and leave manual acceptance/Task 6 open
  until their real gates execute.
- [ ] `git diff --check` passes and unrelated dirty work remains unstaged.
