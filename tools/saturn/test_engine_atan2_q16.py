#!/usr/bin/env python3
"""Differential gate for the TARGET_SATURN atan2 Q16 seam.

The fixture is emitted from two independent 2,000-tick BOB replay captures.
Each value stays in its captured IEEE-754 representation until the compiled
engine path decodes it, so this test exercises the real guarded source rather
than a reimplementation in Python.
"""

from __future__ import annotations

import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import Mapping


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tools/saturn/fixtures/bob_parity_v1_atan2_smc1_v1.json"
RUNNER = ROOT / "tools/saturn/engine_atan2_q16_fixture_test.c"


def compatible_host_compiler(inherited: Mapping[str, str] | None = None) -> str | None:
    """Select the native compiler that can link an executable under MSYS.

    Sourceboot invokes this Python fixture from an MSYS shell, but it is a
    Windows Python process.  In that combination PATH can resolve ``gcc`` to
    a Windows toolchain whose COFF output the MSYS assembler cannot consume.
    The adjacent MSYS2 MinGW64 toolchain is the project's compatible host
    compiler; everywhere else retain the normal portable PATH lookup.
    """
    environment = os.environ if inherited is None else inherited
    configured = environment.get("HOST_CC")
    if configured:
        return configured
    if os.name == "nt" and environment.get("MSYSTEM") == "MSYS":
        msys_roots: list[Path] = []
        configured_root = environment.get("MSYS2_ROOT")
        if configured_root:
            msys_roots.append(Path(configured_root))
        for path_entry in environment.get("PATH", "").split(os.pathsep):
            path = Path(path_entry)
            if path.name.lower() == "bin" and path.parent.name.lower() == "usr":
                msys_roots.append(path.parent.parent)
        for root in msys_roots:
            compiler = root / "mingw64" / "bin" / "gcc.exe"
            if compiler.is_file():
                return str(compiler)
    return shutil.which("gcc", path=environment.get("PATH"))


def compiler_environment(
    temporary: Path,
    inherited: Mapping[str, str] | None = None,
    compiler: str | Path | None = None,
) -> dict[str, str]:
    """Give GCC writable temporary space and keep its matching tools on PATH."""
    environment = dict(os.environ if inherited is None else inherited)
    writable_temporary = str(temporary)
    environment.update(TEMP=writable_temporary, TMP=writable_temporary, TMPDIR=writable_temporary)
    if compiler is not None:
        compiler_path = Path(compiler)
        if compiler_path.is_absolute():
            compiler_bin = str(compiler_path.parent)
            existing_path = environment.get("PATH", "")
            environment["PATH"] = compiler_bin if not existing_path else compiler_bin + os.pathsep + existing_path
            environment.pop("COMPILER_PATH", None)
    return environment


class EngineAtan2Q16FixtureTests(unittest.TestCase):
    def test_compiler_environment_uses_fixture_temporary_directory(self) -> None:
        temporary = Path("fixture-temporary-directory")
        inherited = {"TEMP": r"C:\\msys64\\tmp", "TMP": r"C:\\msys64\\tmp", "TMPDIR": r"C:\\msys64\\tmp"}

        environment = compiler_environment(temporary, inherited)

        self.assertEqual(environment["TEMP"], str(temporary))
        self.assertEqual(environment["TMP"], str(temporary))
        self.assertEqual(environment["TMPDIR"], str(temporary))

    def test_msys_uses_its_mingw64_gcc_before_path_gcc(self) -> None:
        """The MSYS assembler cannot consume the COFF emitted by a PATH gcc."""
        with tempfile.TemporaryDirectory() as temporary:
            msys_root = Path(temporary) / "msys64"
            mingw_gcc = msys_root / "mingw64" / "bin" / "gcc.exe"
            mingw_gcc.parent.mkdir(parents=True)
            mingw_gcc.touch()

            compiler = compatible_host_compiler({
                "MSYSTEM": "MSYS",
                "PATH": str(msys_root / "usr" / "bin"),
            })

            self.assertEqual(compiler, str(mingw_gcc))

            environment = compiler_environment(
                Path(temporary),
                {
                    "PATH": str(msys_root / "usr" / "bin"),
                    "COMPILER_PATH": r"C:\cross\sh-elf",
                },
                mingw_gcc,
            )

            self.assertEqual(environment["PATH"].split(os.pathsep)[0], str(mingw_gcc.parent))
            self.assertNotIn("COMPILER_PATH", environment)

    def test_target_q16_seam_matches_every_captured_atan2_result(self) -> None:
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))
        self.assertEqual(fixture["capture_contract"]["checkpoint_tick"], 2000)
        samples = fixture["samples"]
        self.assertEqual(len(samples), 128)
        self.assertEqual({sample["function"] for sample in samples}, {"atan2s", "atan2_lookup"})

        compiler = compatible_host_compiler()
        self.assertIsNotNone(compiler, "gcc is required for the engine seam fixture")
        expect_mutation = bool(
            os.environ.get("SM64_SATURN_EXPECT_ATAN2_Q16_MUTATION")
        )
        with tempfile.TemporaryDirectory() as temporary:
            executable = Path(temporary) / ("engine-atan2-q16-fixture.exe" if os.name == "nt" else "engine-atan2-q16-fixture")
            corpus = "".join(
                f"{sample['function']} {sample['y_bits']:08x} {sample['x_bits']:08x} {sample['result']}\n"
                for sample in samples
            )
            for target, atan2_variant in (
                (False, None),
                (True, 2),
                (True, 1),
            ):
                compiled = subprocess.run(
                    [compiler, "-std=c11", "-D_GNU_SOURCE", "-Wall", "-Wextra", "-Werror", "-ffunction-sections", "-fdata-sections",
                     *( ["-DENGINE_ATAN2_Q16_TARGET=1"] if target else [] ),
                     *( [f"-DSATURN_ATAN2_VARIANT={atan2_variant}"] if atan2_variant is not None else [] ),
                     *( ["-DSM64_SATURN_TEST_MUTATE_ATAN2_Q16=1"] if os.environ.get("SM64_SATURN_TEST_MUTATE_ATAN2_Q16") else [] ),
                     "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                     "-I", str(ROOT / "src/port/saturn/gfx"), "-I", str(ROOT / "src/port/saturn/platform"),
                     "-I", str(ROOT / "src/port/saturn/runtime"),
                    str(RUNNER), "-Wl,--gc-sections", "-lm", "-o", str(executable)],
                    text=True, capture_output=True, check=False,
                    env=compiler_environment(Path(temporary), compiler=compiler),
                )
                self.assertEqual(compiled.returncode, 0, compiled.stderr)
                completed = subprocess.run([str(executable)], input=corpus, text=True, capture_output=True, check=False)
                if target and atan2_variant == 2 and expect_mutation:
                    self.assertNotEqual(
                        completed.returncode,
                        0,
                        "engine atan2 Q16 mutation escaped the captured-route differential",
                    )
                    self.assertIn(
                        "captured atan2 sample changed",
                        completed.stderr,
                        "mutation run failed for an infrastructure reason, not a changed result",
                    )
                else:
                    self.assertEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
