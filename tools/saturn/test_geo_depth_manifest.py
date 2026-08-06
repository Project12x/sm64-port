#!/usr/bin/env python3
"""RED/GREEN tests for the full-game geo-depth manifest generator."""
from __future__ import annotations

import json
import hashlib
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools" / "saturn" / "geo_depth_manifest.py"


def _run(*args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(TOOL), *args],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def _write_source(path: Path, nested: int = 2) -> None:
    opens = "\n".join("   GEO_OPEN_NODE()," for _ in range(nested))
    closes = "\n".join("   GEO_CLOSE_NODE()," for _ in range(nested))
    path.write_text(
        "const GeoLayout fixture[] = {\n"
        f"{opens}\n"
        "   GEO_BRANCH_AND_LINK(shared_child),\n"
        "   GEO_HELD_OBJECT(0, 0, 0, 0, callback),\n"
        "   GEO_ASM(0, callback),\n"
        f"{closes}\n"
        "   GEO_END(),\n};\n",
        encoding="utf-8",
    )


def test_source_depth_and_determinism() -> None:
    with tempfile.TemporaryDirectory() as raw:
        temp = Path(raw)
        first = temp / "z_fixture.c"
        second = temp / "a_fixture.c"
        _write_source(first)
        _write_source(second, nested=1)
        out_a = temp / "a.h"
        report_a = temp / "a.json"
        out_b = temp / "b.h"
        report_b = temp / "b.json"
        link_a = temp / "a.ld"
        link_b = temp / "b.ld"
        args = (
            "--source", str(first),
            "--source", str(second),
            "--safety-margin", "2",
        )
        result_a = _run(*args, "--output-header", str(out_a), "--output-linker", str(link_a), "--output-json", str(report_a))
        assert result_a.returncode == 0, result_a.stderr
        result_b = _run(
            "--source", str(second), "--source", str(first),
            "--safety-margin", "2",
            "--output-header", str(out_b), "--output-json", str(report_b),
            "--output-linker", str(link_b),
        )
        assert result_b.returncode == 0, result_b.stderr
        assert out_a.read_bytes() == out_b.read_bytes()
        assert report_a.read_bytes() == report_b.read_bytes()
        assert link_a.read_bytes() == link_b.read_bytes()
        report = json.loads(report_a.read_text(encoding="utf-8"))
        assert report["max_proven_depth"] == 5
        assert report["capacity"] == 8
        assert "expected_size = 0x80" in link_a.read_text(encoding="utf-8")
        assert report["inputs"] == sorted(report["inputs"], key=lambda item: item["identity"])


def test_missing_input_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as raw:
        result = _run("--source", str(Path(raw) / "missing.c"))
        assert result.returncode != 0
        assert "missing" in result.stderr.lower()


def test_duplicate_identity_fails_closed() -> None:
    with tempfile.TemporaryDirectory() as raw:
        temp = Path(raw)
        one = temp / "one.json"
        two = temp / "two.json"
        payload = {
            "schema": "sm64-saturn-geo-depth-input-v1",
            "identity": "actors/shared/geo.inc.c",
            "structural_depth": 2,
            "shared_child_edges": 0,
            "held_object_edges": 0,
            "callback_edges": 0,
            "max_depth": 2,
        }
        one.write_text(json.dumps(payload), encoding="utf-8")
        two.write_text(json.dumps(payload), encoding="utf-8")
        result = _run("--input", str(one), "--input", str(two))
        assert result.returncode != 0
        assert "duplicate" in result.stderr.lower()


def test_undercount_and_capacity_mutations_fail_closed() -> None:
    with tempfile.TemporaryDirectory() as raw:
        temp = Path(raw)
        source = temp / "fixture.c"
        _write_source(source)
        under = temp / "under.json"
        under.write_text(
            json.dumps({
                "schema": "sm64-saturn-geo-depth-input-v1",
                "identity": "actors/under/geo.inc.c",
                "structural_depth": 2,
                "shared_child_edges": 1,
                "held_object_edges": 1,
                "callback_edges": 1,
                "max_depth": 4,
            }),
            encoding="utf-8",
        )
        under_result = _run("--input", str(under))
        assert under_result.returncode != 0
        assert "undercount" in under_result.stderr.lower()

        out = temp / "manifest.json"
        result = _run(
            "--source", str(source), "--safety-margin", "2",
            "--output-json", str(out),
        )
        assert result.returncode == 0, result.stderr
        mutation = json.loads(out.read_text(encoding="utf-8"))
        mutation["capacity"] = mutation["max_proven_depth"]
        canonical = dict(mutation)
        canonical.pop("input_sha256", None)
        mutation["input_sha256"] = hashlib.sha256(
            json.dumps(canonical, sort_keys=True, separators=(",", ":")).encode("utf-8")
        ).hexdigest()
        out.write_text(json.dumps(mutation), encoding="utf-8")
        verify = _run("--verify-json", str(out))
        assert verify.returncode != 0
        assert "capacity" in verify.stderr.lower()


def test_repository_source_dirs_cover_full_game_geo_inputs() -> None:
    with tempfile.TemporaryDirectory() as raw:
        temp = Path(raw)
        report_path = temp / "repo.json"
        result = _run(
            "--root", str(ROOT),
            "--source-dir", str(ROOT / "actors"),
            "--source-dir", str(ROOT / "levels"),
            "--output-json", str(report_path),
        )
        assert result.returncode == 0, result.stderr
        report = json.loads(report_path.read_text(encoding="utf-8"))
        assert len(report["inputs"]) >= 400
        assert report["max_proven_depth"] >= 1
        assert report["capacity"] >= report["max_proven_depth"]


def main() -> None:
    test_source_depth_and_determinism()
    test_missing_input_fails_closed()
    test_duplicate_identity_fails_closed()
    test_undercount_and_capacity_mutations_fail_closed()
    test_repository_source_dirs_cover_full_game_geo_inputs()
    print("geo depth manifest: PASS")


if __name__ == "__main__":
    main()
