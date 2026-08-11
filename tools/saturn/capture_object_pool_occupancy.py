#!/usr/bin/env python3
"""Measure gObjectPool occupancy on the canonical flags-on geo-walk config.

Task 2 of the memory-residency campaign (docs/superpowers/plans/
2026-08-09-memory-residency-campaign.md): sample
``g_sm64_saturn_object_pool_probe`` (src/port/saturn/runtime/
saturn_object_pool_probe.h) through headless Ymir every
``--sample-interval`` emulated frames out to at least
``--post-bios-frames`` post-BIOS-handoff frames, and record the real
observed ``current_allocated`` / ``peak_allocated`` / ``alloc_failures``
time series so the owner's Task 3 capacity gate is decided from measured
data, not an estimate.

This runs the route encoded by the supplied sealed identity after BIOS
handoff. The report records its replay/live-input modes from that identity;
it makes no movement or coverage claim without the matching target evidence.

Pattern-copied from capture_sourceboot_boot_trace.py (BIOS handoff macro,
sh-elf-nm symbol resolution through the DLL-safe MSYS wrapper, artifact
identity binding, protocol/diagnostics evidence shape) and
capture_route_views.py's YmirClient transport.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import time
from pathlib import Path
from typing import Any

from capture_hwtest import artifact_identity
from capture_route_views import YmirClient
from capture_sourceboot_throughput import (
    BOOT_TRACE_BYTES,
    CADENCE_TRACE_BYTES,
    IDENTITY_MISMATCH_MESSAGE,
    build_elf_build_identity_probe,
    build_elf_identity_probe,
    decode_boot_trace,
    decode_cadence_trace,
    prove_loaded_build_identity,
    prove_target_identity,
    validate_startup_vblanks,
    validate_release_identity_probe,
)
from gen_build_identity import build_identity
from release_manifest import verify_release_manifest
from capture_sourceboot_boot_trace import (
    NM,
    bind_capture_artifacts,
    cpu_cache_through_alias,
    protocol_and_diagnostics,
    resolve_probe_symbol,
    run_bios_handoff,
    wrapped_nm_command,
)

ROOT = Path(__file__).resolve().parents[2]

PROBE_SYMBOL = "g_sm64_saturn_object_pool_probe"
PROBE_MAGIC = 0x4F504F4C
PROBE_WORD_COUNT = 5
PROBE_BYTES = PROBE_WORD_COUNT * 4
SMOKE_SYMBOLS = {
    "sAreaYaw": 2,
    "sourceboot_exception_record": 4,
    "g_sm64_saturn_source_cart_probe": 28,
    "sourceboot_boot_trace": BOOT_TRACE_BYTES,
    "sourceboot_cadence_trace": CADENCE_TRACE_BYTES,
}

# Session environment rule: chunk every exec.run_for call at <=600 frames.
# 300 keeps every chunk boundary aligned with the plan's own 300-frame
# sample cadence regardless of the caller's --sample-interval.
RUN_FOR_CHUNK_FRAMES = 300
DEFAULT_SAMPLE_INTERVAL_FRAMES = 300
# 300 * 67 = 20,100 -- an exact multiple of the default sample interval
# that clears the plan's >=20,000-frame measurement floor.
DEFAULT_POST_BIOS_FRAMES = 20100
MIN_POST_BIOS_FRAMES = 20000
STARTUP_IDENTITY_VBLANKS = 3600

def pool_capacity_from_sealed_artifact(identity_spec: Path, elf: Path) -> int:
    """Return capacity only after the matching sealed identity is found in ELF.

    The capacity is a compiler-config scalar rather than a standalone ABI
    field.  Rebuilding the canonical identity from its spec then requiring the
    resulting tuple bytes in the selected ELF binds the report to that exact
    compilation, instead of accidentally reporting the header's fallback
    value for an overridden build.
    """
    spec = json.loads(identity_spec.read_text(encoding="utf-8"))
    expected = build_identity(spec).raw
    if elf.read_bytes().find(expected) < 0:
        raise ValueError(
            "sealed build identity from identity spec is not present in capture ELF"
        )
    capacity = spec.get("object_pool_capacity")
    if type(capacity) is not int:
        raise ValueError("sealed identity spec has no integer object_pool_capacity")
    return capacity


def resolve_release_binding(
    manifest: Path, game: Path, elf: Path, identity_spec: Path | None
) -> dict[str, Any]:
    """Resolve capacity and identity only from a fully verified release."""
    verified = verify_release_manifest(manifest)
    if game.resolve() != verified.outputs["cue"]:
        getattr(verified, "close", lambda: None)()
        raise ValueError("game CUE differs from verified release manifest")
    if elf.resolve() != verified.outputs["elf"]:
        getattr(verified, "close", lambda: None)()
        raise ValueError("ELF differs from verified release manifest")
    snapshot = getattr(verified, "snapshot_outputs", verified.outputs)
    capture_elf = snapshot["elf"]
    capture_cue = snapshot["cue"]
    version = verified.document["identity_version"]
    if version == 1 and identity_spec is None:
        getattr(verified, "close", lambda: None)()
        raise ValueError("identity spec is required for identity v1 compatibility (--identity-spec)")
    probe = build_elf_build_identity_probe(capture_elf)
    validate_release_identity_probe(verified, probe)
    if version == 2:
        values = verified.document["effective_config"]
        capacity = values.get("object_pool_capacity")
        if type(capacity) is not int:
            raise ValueError("release manifest has no integer object_pool_capacity")
    else:
        assert identity_spec is not None
        spec = json.loads(identity_spec.read_text(encoding="utf-8"))
        built = build_identity(spec)
        expected = built.raw
        if expected != bytes(probe["expected_bytes"]):
            raise ValueError("identity spec differs from release ELF identity symbol")
        capacity = spec.get("object_pool_capacity")
        if type(capacity) is not int:
            raise ValueError("identity v1 spec has no integer object_pool_capacity")
        values = spec
    return {
        "verified": verified,
        "probe": probe,
        "sealed_identity": bytes(probe["expected_bytes"]),
        "identity_values": values,
        "pool_capacity": capacity,
        "cue": capture_cue,
        "elf": capture_elf,
        "release_manifest_sha256": verified.manifest_sha256,
    }


def resolve_smoke_addresses(
    elf: Path, *, nm: Path = NM, run: Any = subprocess.run
) -> dict[str, int]:
    """Resolve every sampled ABI from one DLL-safe symbol-table listing."""
    completed = run(
        wrapped_nm_command(elf, nm=nm), check=False, capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm wrapper failed for {elf}: {completed.stderr.strip()}"
        )
    return {
        symbol: resolve_probe_symbol(completed.stdout, symbol)
        for symbol in (PROBE_SYMBOL, *SMOKE_SYMBOLS)
    }


def resolve_probe_address(elf: Path, *, nm: Path = NM, run: Any = subprocess.run) -> int:
    """Backward-compatible single-probe resolver for existing callers."""
    completed = run(
        wrapped_nm_command(elf, nm=nm), check=False, capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise ValueError(
            f"DLL-safe sh-elf-nm wrapper failed for {elf}: {completed.stderr.strip()}"
        )
    return resolve_probe_symbol(completed.stdout, PROBE_SYMBOL)


def decode_probe(data: list[int]) -> dict[str, Any]:
    """Decode a raw probe read leniently -- never raises.

    Like capture_sourceboot_boot_trace.py's capture_trace_checkpoint(), early
    checkpoints (before the BIOS has actually finished the CD-boot sequence
    and loaded the ELF's .data image into RAM) legitimately read back
    whatever was in that RAM before the game's own .data copy landed there
    -- zero, or leftover BIOS/loader garbage. That is not a capture failure;
    it just means the sample predates the probe's real value, so it is
    reported as ``magic_valid: False`` with the raw words preserved for
    evidence rather than treated as an error.
    """
    if len(data) != PROBE_BYTES:
        raise ValueError(
            f"object-pool probe read returned {len(data)} bytes, expected {PROBE_BYTES}"
        )
    words = [
        int.from_bytes(bytes(data[index:index + 4]), byteorder="big")
        for index in range(0, PROBE_BYTES, 4)
    ]
    magic_valid = words[0] == PROBE_MAGIC
    return {
        "magic": words[0],
        "magic_valid": magic_valid,
        "current_allocated": words[1] if magic_valid else None,
        "peak_allocated": words[2] if magic_valid else None,
        "alloc_failures": words[3] if magic_valid else None,
        "frames_sampled": words[4] if magic_valid else None,
        "raw_words": words,
    }


def read_probe(client: YmirClient, address: int) -> dict[str, Any]:
    result = client.call("mem.peek", {"address": address, "count": PROBE_BYTES})
    data = result.get("data")
    if not isinstance(data, list):
        raise ValueError("Ymir object-pool probe read has no byte data")
    return decode_probe(data)


def read_smoke_bytes(client: YmirClient, address: int, count: int) -> bytes:
    """Read one fixed target ABI window through the SH-2 cache-through alias."""
    result = client.call("mem.peek", {"address": address, "count": count})
    data = result.get("data")
    if not isinstance(data, list) or len(data) != count:
        raise ValueError(
            f"Ymir smoke read at 0x{address:08x} returned "
            f"{len(data) if isinstance(data, list) else 'no'} bytes, expected {count}"
        )
    if any(not isinstance(byte, int) or not 0 <= byte <= 0xFF for byte in data):
        raise ValueError("Ymir smoke read contains a non-byte value")
    return bytes(data)


def decode_s16_be(data: list[int] | bytes) -> int:
    raw = int.from_bytes(bytes(data), "big")
    return raw - 0x10000 if raw & 0x8000 else raw


def decode_cart_probe(raw: bytes) -> dict[str, Any]:
    if len(raw) != 28:
        raise ValueError("source cart probe has wrong size")
    words = [int.from_bytes(raw[index:index + 4], "big") for index in range(0, 28, 4)]
    result = dict(zip(
        ("magic", "stage", "expected_size", "copied_size", "cart_id", "cart_size", "status"),
        words,
    ))
    result["ready_complete_ok"] = (
        result["magic"] == 0x53434152
        and result["stage"] == 5
        and result["expected_size"] == result["copied_size"]
        and result["status"] == 0
    )
    return result


def read_smoke_sample(client: YmirClient, addresses: dict[str, int]) -> dict[str, Any]:
    """Read pool and nonvisual gates from one paused target instant."""
    raw = {
        symbol: read_smoke_bytes(
            client, cpu_cache_through_alias(addresses[symbol]), size
        )
        for symbol, size in ((PROBE_SYMBOL, PROBE_BYTES), *SMOKE_SYMBOLS.items())
    }
    probe = decode_probe(list(raw[PROBE_SYMBOL]))
    if not probe["magic_valid"]:
        return {
            **probe,
            "area_yaw": None,
            "exception_magic": None,
            "cart": None,
            "boot": None,
            "cadence": None,
        }
    return {
        **probe,
        "area_yaw": decode_s16_be(raw["sAreaYaw"]),
        "exception_magic": int.from_bytes(
            raw["sourceboot_exception_record"][:4], "big"
        ),
        "cart": decode_cart_probe(raw["g_sm64_saturn_source_cart_probe"]),
        "boot": decode_boot_trace(raw["sourceboot_boot_trace"]),
        "cadence": decode_cadence_trace(raw["sourceboot_cadence_trace"]),
    }


def prove_sealed_target_identity(
    client: YmirClient,
    target_probe: dict[str, Any],
    build_probe: dict[str, Any],
    sealed_identity: bytes,
) -> dict[str, Any]:
    """Require target code and its embedded identity to match the sealed ELF."""
    if bytes(build_probe["expected_bytes"]) != sealed_identity:
        raise ValueError("sealed build identity does not match capture ELF")
    target = prove_target_identity(client, target_probe)
    loaded = prove_loaded_build_identity(client, build_probe)
    return {"match": bool(target["match"] and loaded["match"]), "code": target, "build": loaded}


def wait_for_sealed_target_identity(
    client: YmirClient,
    target_probe: dict[str, Any],
    build_probe: dict[str, Any],
    sealed_identity: bytes,
    *,
    startup_vblanks: int,
    run_for: Any | None = None,
) -> dict[str, Any]:
    """Wait until exact code and initialized build identity match one target."""
    if bytes(build_probe["expected_bytes"]) != sealed_identity:
        raise ValueError("sealed build identity does not match capture ELF")
    startup_vblanks = validate_startup_vblanks(startup_vblanks)
    advance = run_for or (
        lambda frames: client.call("exec.run_for", {"frames": frames})
    )
    last_build_error: ValueError | None = None
    for attempt in range(1, startup_vblanks + 1):
        advance(1)
        try:
            target = prove_target_identity(client, target_probe)
        except ValueError as error:
            if str(error) != IDENTITY_MISMATCH_MESSAGE:
                raise
            continue
        try:
            loaded = prove_loaded_build_identity(client, build_probe)
        except ValueError as error:
            last_build_error = error
            continue
        return {
            "match": bool(target["match"] and loaded["match"]),
            "code": target,
            "build": loaded,
            "startup_vblanks_waited": attempt,
            "startup_identity_attempts": attempt,
        }
    detail = f": {last_build_error}" if last_build_error is not None else ""
    raise ValueError(
        "sealed target identity did not match after "
        f"{startup_vblanks} one-VBlank startup attempts{detail}"
    )


def smoke_acceptance(samples: list[dict[str, Any]]) -> dict[str, bool]:
    valid = [sample for sample in samples if sample.get("magic_valid")]
    yaw_window = [
        sample for sample in valid
        if sample["label"] in ("post-bios-9600", "post-bios-9900")
    ]
    vdp = [sample["boot"]["vdp2_presentation_generation"] for sample in valid]
    checks = {
        "pool_alloc_failures_zero": bool(valid) and all(
            sample["alloc_failures"] == 0 for sample in valid
        ),
        "cart_ready_complete_ok": bool(valid) and all(
            sample["cart"]["ready_complete_ok"] for sample in valid[-2:]
        ),
        "exception_record_clear": bool(valid) and all(
            sample["exception_magic"] == 0 for sample in valid
        ),
        "vdp_generations_climbing": len(vdp) >= 2 and vdp[-1] > vdp[0],
        "area_yaw_changes_9500_10000": (
            len(yaw_window) == 2
            and len({sample["area_yaw"] for sample in yaw_window}) == 2
        ),
    }
    return {**checks, "pass": all(checks.values())}


def validate_post_bios_frames(frames: int) -> int:
    if frames < MIN_POST_BIOS_FRAMES:
        raise ValueError(
            f"--post-bios-frames must be >= {MIN_POST_BIOS_FRAMES} "
            "(Task 2's measurement floor)"
        )
    return frames


def validate_sample_interval(interval: int) -> int:
    if interval <= 0 or interval > 600:
        raise ValueError(
            "--sample-interval must be a positive value <= 600 "
            "(exec.run_for chunk cap)"
        )
    return interval


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ymir", type=Path, required=True, help="ymir-headless executable")
    parser.add_argument("--ipl", type=Path, required=True, help="Saturn BIOS image")
    parser.add_argument("--game", type=Path, required=True, help="built sourceboot .cue path")
    parser.add_argument("--elf", type=Path, required=True, help="matching sourceboot ELF")
    parser.add_argument("--release-manifest", type=Path, required=True)
    parser.add_argument(
        "--identity-spec",
        type=Path,
        required=False,
        help="sealed build identity spec generated for the matching ELF",
    )
    parser.add_argument("--output", type=Path, required=True, help="JSON evidence report")
    parser.add_argument(
        "--post-bios-frames", type=int, default=DEFAULT_POST_BIOS_FRAMES,
        help=f"bounded frames to sample after BIOS handoff (default: {DEFAULT_POST_BIOS_FRAMES})",
    )
    parser.add_argument(
        "--sample-interval", type=int, default=DEFAULT_SAMPLE_INTERVAL_FRAMES,
        help=f"emulated-frame interval between probe samples (default: {DEFAULT_SAMPLE_INTERVAL_FRAMES})",
    )
    parser.add_argument("--timeout", type=float, default=1800.0)
    args = parser.parse_args(argv)

    for label, path in (
        ("Ymir", args.ymir),
        ("IPL", args.ipl),
        ("game", args.game),
        ("ELF", args.elf),
        ("release manifest", args.release_manifest),
    ):
        if not path.is_file():
            parser.error(f"{label} is not a file: {path}")
    try:
        args.post_bios_frames = validate_post_bios_frames(args.post_bios_frames)
        args.sample_interval = validate_sample_interval(args.sample_interval)
    except ValueError as error:
        parser.error(str(error))
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.post_bios_frames % args.sample_interval != 0:
        parser.error("--post-bios-frames must be an exact multiple of --sample-interval")

    args.ymir = args.ymir.resolve()
    args.ipl = args.ipl.resolve()
    args.game = args.game.resolve()
    args.elf = args.elf.resolve()
    args.release_manifest = args.release_manifest.resolve()
    if args.identity_spec is not None:
        if not args.identity_spec.is_file():
            parser.error(f"identity spec is not a file: {args.identity_spec}")
        args.identity_spec = args.identity_spec.resolve()
    args.output = args.output.resolve()
    try:
        release_binding = resolve_release_binding(
            args.release_manifest, args.game, args.elf, args.identity_spec
        )
        capture_cue = release_binding.get("cue", args.game)
        capture_elf = release_binding.get("elf", args.elf)
        artifacts = bind_capture_artifacts(
            capture_cue, capture_elf
        )
    except (OSError, ValueError) as error:
        parser.error(str(error))

    try:
        identity_values = release_binding["identity_values"]
        sealed_identity = release_binding["sealed_identity"]
        pool_capacity = release_binding["pool_capacity"]
        target_identity_probe = build_elf_identity_probe(capture_elf)
        build_identity_probe = release_binding["probe"]
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(str(error))
    smoke_addresses = resolve_smoke_addresses(capture_elf)
    probe_address = smoke_addresses[PROBE_SYMBOL]

    wall_start = time.perf_counter()
    emulated_frames = 0
    client: YmirClient | None = None
    samples: list[dict[str, Any]] = []
    final_probe: dict[str, Any] | None = None
    target_identity: dict[str, Any] | None = None
    failure: BaseException | None = None
    try:
        client = YmirClient(
            args.ymir, args.ipl, capture_cue, args.timeout
        )

        def run_for(frames: int) -> None:
            nonlocal emulated_frames
            remaining = frames
            while remaining > 0:
                chunk = min(remaining, RUN_FOR_CHUNK_FRAMES)
                client.call("exec.run_for", {"frames": chunk})
                emulated_frames += chunk
                remaining -= chunk

        def sample(label: str) -> None:
            smoke = read_smoke_sample(client, smoke_addresses)
            samples.append({"label": label, "emulated_frames": emulated_frames, **smoke})

        run_bios_handoff(client, run_for, sample)
        target_identity = wait_for_sealed_target_identity(
            client,
            target_identity_probe,
            build_identity_probe,
            sealed_identity,
            startup_vblanks=STARTUP_IDENTITY_VBLANKS,
            run_for=run_for,
        )

        elapsed = 0
        while elapsed < args.post_bios_frames:
            run_for(args.sample_interval)
            elapsed += args.sample_interval
            sample(f"post-bios-{elapsed}")

        final_probe = read_smoke_sample(client, smoke_addresses)
        client.shutdown()
    except BaseException as error:
        failure = error
        if client is not None:
            client.abort()

    post_bios_samples = [s for s in samples if s["label"].startswith("post-bios-")]
    valid_samples = [s for s in samples if s["magic_valid"]]
    valid_post_bios_samples = [s for s in post_bios_samples if s["magic_valid"]]
    first_valid_sample = valid_samples[0] if valid_samples else None
    peak_allocated_observed = max(
        (s["peak_allocated"] for s in valid_samples), default=None
    )
    alloc_failures_observed = max(
        (s["alloc_failures"] for s in valid_samples), default=None
    )
    current_allocated_min = min(
        (s["current_allocated"] for s in valid_post_bios_samples), default=None
    )
    current_allocated_max = max(
        (s["current_allocated"] for s in valid_post_bios_samples), default=None
    )
    acceptance = smoke_acceptance(samples)
    route_note = (
        "sealed identity route modes: "
        f"route_replay_mode={identity_values['route_replay_mode']}, "
        f"live_input_mode={identity_values['live_input_mode']}; "
        "the matched target build determines capture movement."
    )

    report: dict[str, Any] = {
        "evidence_kind": "ymir-object-pool-occupancy-capture",
        "diagnostic_only": True,
        "manual_gui_launch": False,
        "target_build": False,
        "performance_measurement": False,
        "route_note": route_note,
        "ymir": str(args.ymir),
        "ipl": str(args.ipl),
        "game": artifact_identity(capture_cue),
        "elf": artifact_identity(capture_elf),
        "artifacts": artifacts,
        "release_manifest_sha256": release_binding["release_manifest_sha256"],
        "build_identity_spec": (
            artifact_identity(args.identity_spec) if args.identity_spec is not None else None
        ),
        "target_identity": target_identity,
        "pool_capacity_binding": (
            "release-manifest-effective-config-v2"
            if release_binding["verified"].document["identity_version"] == 2
            else "explicit-v1-identity-spec"
        ),
        "probe_symbol": PROBE_SYMBOL,
        "probe_address": probe_address,
        "probe_cache_through_address": cpu_cache_through_alias(probe_address),
        "smoke_symbols": SMOKE_SYMBOLS,
        "smoke_addresses": smoke_addresses,
        "smoke_cache_through_addresses": {
            symbol: cpu_cache_through_alias(address)
            for symbol, address in smoke_addresses.items()
        },
        "pool_capacity": pool_capacity,
        "sample_interval_frames": args.sample_interval,
        "requested_post_bios_frames": args.post_bios_frames,
        "emulated_frames": emulated_frames,
        "post_bios_sample_count": len(post_bios_samples),
        "valid_sample_count": len(valid_samples),
        "first_valid_sample": first_valid_sample,
        "samples": samples,
        "final_probe": final_probe,
        "peak_allocated_observed": peak_allocated_observed,
        "alloc_failures_observed": alloc_failures_observed,
        "current_allocated_min_post_bios": current_allocated_min,
        "current_allocated_max_post_bios": current_allocated_max,
        "smoke_acceptance": acceptance,
        "wall_seconds": time.perf_counter() - wall_start,
    }
    if client is not None:
        report.update(protocol_and_diagnostics(client))
    if failure is not None:
        report["failure"] = {"message": str(failure), "type": type(failure).__name__}

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2, sort_keys=True))
    return 1 if failure is not None or not acceptance["pass"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
