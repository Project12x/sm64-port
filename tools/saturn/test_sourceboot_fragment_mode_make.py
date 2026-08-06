"""Isolated parser contract for sourceboot's fragment-mode compatibility alias."""
from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = ROOT / "src/port/saturn/sourceboot/Makefile"


def fragment_block(source: str) -> str:
    start = source.index("SATURN_DEMO_BSP_ORDER ?= 1")
    end = source.index("SATURN_DEMO_BSP_FRAGMENT_FLAT ?= 0")
    return source[start:end]


def invoke(block: str, *variables: str) -> subprocess.CompletedProcess[str]:
    make = os.environ.get("MAKE", "make")
    content = block + "\nall:\n\t@printf '%s:%s\\n' '$(SATURN_DEMO_BSP_FRAGMENTS)' '$(SATURN_DEMO_FRAGMENT_MODE)'\n"
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "fragment-contract.mk"
        path.write_text(content, encoding="utf-8")
        return subprocess.run(
            [make, "--no-print-directory", "-f", str(path), *variables, "all"],
            check=False,
            text=True,
            capture_output=True,
        )


def expect_values(block: str, expected: str, *variables: str) -> None:
    result = invoke(block, *variables)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == expected


def test_fragment_alias_defaults_and_rejects_recursive_mutation() -> None:
    source = MAKEFILE.read_text(encoding="utf-8")
    block = fragment_block(source)
    assert "ifeq ($(origin SATURN_DEMO_BSP_FRAGMENTS),undefined)" in block
    assert "SATURN_DEMO_BSP_FRAGMENTS := 0" in block
    assert "SATURN_DEMO_FRAGMENT_MODE ?= $(SATURN_DEMO_BSP_FRAGMENTS)" in block

    expect_values(block, "0:0")
    expect_values(block, "1:1", "SATURN_DEMO_FRAGMENT_MODE=1")
    expect_values(block, "1:1", "SATURN_DEMO_BSP_FRAGMENTS=1")
    expect_values(block, "1:1", "SATURN_DEMO_BSP_FRAGMENTS=1", "SATURN_DEMO_FRAGMENT_MODE=1")

    mismatch = invoke(block, "SATURN_DEMO_BSP_FRAGMENTS=1", "SATURN_DEMO_FRAGMENT_MODE=0")
    assert mismatch.returncode != 0
    assert "must match" in mismatch.stderr

    mutation = block.replace("SATURN_DEMO_BSP_FRAGMENTS := 0",
                             "SATURN_DEMO_BSP_FRAGMENTS ?= $(SATURN_DEMO_FRAGMENT_MODE)", 1)
    assert mutation != block
    recursive = invoke(mutation)
    assert recursive.returncode != 0
    assert "Recursive variable 'SATURN_DEMO_BSP_FRAGMENTS'" in recursive.stderr


if __name__ == "__main__":
    test_fragment_alias_defaults_and_rejects_recursive_mutation()
