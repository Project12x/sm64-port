#!/usr/bin/env python3
"""Behavioral contract for the isolated default-camera replay route.

Break caught: a missing or altered default-camera route could move the sole
R trigger away from route tick 121, change its pad value, or change the
2,000-tick route endpoint.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_DIR = ROOT / "src/port/saturn/sourceboot"
OLD_MANIFEST = ROOT / "tools/saturn/routes/bob_parity_v1.json"
MANIFEST = ROOT / "tools/saturn/routes/bob_default_camera_v1.json"
ROUTE_SOURCE = ROOT / "src/port/saturn/sourceboot/source_camera_acceptance_route.c"
ROUTE_HEADER = ROOT / "src/port/saturn/sourceboot/source_camera_acceptance_route.h"
ROUTE_HELPER = ROOT / "tools/saturn/camera_acceptance_route.py"
HOST_GCC = Path("C:/msys64/mingw64/bin/gcc.exe")
MSYS_MAKE = Path("C:/msys64/usr/bin/make.exe")
YAUL_INSTALL_ROOT = Path("D:/Code/RetroDev/sm64-saturn-port/work/yaul-install")

EXPECTED_OLD_SHA256 = (
    "f62861516c708ef0bda82c0a32bf10706a93b0948160f42b50c1e6ec9ddd44ee"
)
EXPECTED_SAMPLES = [
    {"ticks": 120, "stick_x": 0, "stick_y": 0, "buttons": 0},
    {"ticks": 1, "stick_x": 0, "stick_y": 0, "buttons": 0x0010},
    {"ticks": 1879, "stick_x": 0, "stick_y": 0, "buttons": 0},
]
EXPECTED_FIRST_MARIO_DISPATCH_TICK = 121


def host_environment() -> dict[str, str]:
    """Match the host fixture's isolated compiler environment."""
    environment = os.environ.copy()
    for name in (
        "GCC_EXEC_PREFIX", "COMPILER_PATH", "LIBRARY_PATH", "C_INCLUDE_PATH",
        "CPLUS_INCLUDE_PATH", "CFLAGS", "CPPFLAGS", "LDFLAGS",
    ):
        environment.pop(name, None)
    environment["PATH"] = "C:\\msys64\\mingw64\\bin;" + environment.get("PATH", "")
    return environment


def yaul_environment() -> dict[str, str]:
    """Build-only environment for Make configuration checks."""
    environment = os.environ.copy()
    environment["YAUL_INSTALL_ROOT"] = str(YAUL_INSTALL_ROOT)
    environment["YAUL_PROG_SH_PREFIX"] = "sh-elf"
    environment["YAUL_ARCH_SH_PREFIX"] = "sh-elf"
    environment["YAUL_ARCH_M68K_PREFIX"] = "m68keb-elf"
    environment["YAUL_BUILD_ROOT"] = "D:/Code/RetroDev/sm64-saturn-port/sm64-port/build/saturn/yaul"
    environment["YAUL_BUILD"] = "release"
    environment["YAUL_OPTION_MALLOC_IMPL"] = "tlsf"
    environment["DEBUG_RELEASE"] = "1"
    environment["COMPILER_PATH"] = (
        f"{YAUL_INSTALL_ROOT}/bin:{YAUL_INSTALL_ROOT}/libexec/gcc/sh-elf/14.3.0"
    )
    environment["PATH"] = (
        f"C:\\msys64\\usr\\bin;{YAUL_INSTALL_ROOT}\\bin;" +
        environment.get("PATH", "")
    )
    return environment


def sourceboot_make(*assignments: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [str(MSYS_MAKE), "-C", str(SOURCEBOOT_DIR), "-pn", *assignments],
        capture_output=True,
        text=True,
        env=yaul_environment(),
    )


def make_value(output: str, name: str) -> str:
    prefix = f"{name} := "
    for line in output.splitlines():
        if line.startswith(prefix):
            return line.removeprefix(prefix)
    raise AssertionError(f"Make did not report {name}")


class DefaultCameraRouteTest(unittest.TestCase):
    def test_legacy_manifest_is_immutable(self) -> None:
        self.assertEqual(
            hashlib.sha256(OLD_MANIFEST.read_bytes()).hexdigest(),
            EXPECTED_OLD_SHA256,
        )

    def test_default_camera_route_has_the_hand_derived_pad_timeline(self) -> None:
        self.assertTrue(
            MANIFEST.exists() and ROUTE_SOURCE.exists() and ROUTE_HEADER.exists(),
            "bob_default_camera_v1.json and the isolated route accessor must exist",
        )
        manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
        self.assertEqual(manifest["route_version"], "bob-default-camera-v1")
        self.assertEqual(manifest["route_id"], 2)
        self.assertEqual(manifest["checkpoint_tick"], 2000)
        self.assertEqual(manifest["simulation_ticks"], 2000)
        self.assertEqual(manifest["samples"], EXPECTED_SAMPLES)

        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            (temp / "yaul.h").write_text("/* host route fixture */\n", encoding="utf-8")
            harness = temp / "route_harness.c"
            harness.write_text(
                """
#include <stdint.h>
#include <stdio.h>
#include "source_camera_acceptance_route.h"

int main(void) {
    uint16_t count = 0U;
    const sm64_saturn_input_replay_sample_t *samples =
        sm64_saturn_sourceboot_bob_default_camera_v1(&count);
    printf("%u\\n", count);
    for (uint16_t index = 0U; index < count; ++index)
        printf("%u,%d,%d,%u\\n", samples[index].ticks, samples[index].stick_x,
               samples[index].stick_y, samples[index].buttons);
    return 0;
}
""",
                encoding="utf-8",
            )
            executable = temp / "route_harness.exe"
            compile_result = subprocess.run(
                [
                    str(HOST_GCC), "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(temp),
                    "-I", str(ROOT / "include"),
                    "-I", str(ROOT / "src/port/saturn/sourceboot"),
                    "-I", str(ROOT / "src/port/saturn/runtime"),
                    str(harness), str(ROUTE_SOURCE), "-o", str(executable),
                ],
                check=False,
                capture_output=True,
                text=True,
                env=host_environment(),
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            self.assertEqual(compile_result.stderr, "")
            output = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True,
                env=host_environment(),
            ).stdout.splitlines()

        self.assertEqual(output[0], "3")
        self.assertEqual(
            output[1:], ["120,0,0,0", "1,0,0,16", "1879,0,0,0"]
        )

        helper = importlib.util.spec_from_file_location(
            "camera_acceptance_route", ROUTE_HELPER
        )
        self.assertIsNotNone(helper)
        module = importlib.util.module_from_spec(helper)
        assert helper.loader is not None
        helper.loader.exec_module(module)
        self.assertEqual(
            module.first_mario_dispatch_tick(manifest),
            EXPECTED_FIRST_MARIO_DISPATCH_TICK,
        )
        self.assertEqual(
            manifest["first_mario_dispatch_tick"],
            module.first_mario_dispatch_tick(manifest),
        )
        self.assertEqual(sum(sample["ticks"] for sample in manifest["samples"]), 2000)

    def test_dispatch_tick_rejects_non_camera_input_shapes(self) -> None:
        self.assertTrue(ROUTE_HELPER.exists(), "route manifest validator must exist")
        spec = importlib.util.spec_from_file_location(
            "camera_acceptance_route", ROUTE_HELPER
        )
        self.assertIsNotNone(spec)
        module = importlib.util.module_from_spec(spec)
        assert spec.loader is not None
        spec.loader.exec_module(module)

        with self.assertRaises(ValueError):
            module.first_mario_dispatch_tick({"samples": EXPECTED_SAMPLES[:1]})
        with self.assertRaises(ValueError):
            module.first_mario_dispatch_tick(
                {"samples": [
                    EXPECTED_SAMPLES[0],
                    {"ticks": 1, "stick_x": 1, "stick_y": 0, "buttons": 0x0010},
                    EXPECTED_SAMPLES[2],
                ]}
            )
        with self.assertRaises(ValueError):
            module.first_mario_dispatch_tick(
                {"samples": [
                    EXPECTED_SAMPLES[0],
                    {"ticks": 2, "stick_x": 0, "stick_y": 0, "buttons": 0x0010},
                    EXPECTED_SAMPLES[2],
                ]}
            )

    def test_camera_make_configuration_rejects_invalid_roles_and_keys_outputs(self) -> None:
        baseline = sourceboot_make(
            "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
        )
        self.assertEqual(baseline.returncode, 0, baseline.stderr)
        baseline_dir = make_value(baseline.stdout, "SH_OUTPUT_DIR")
        self.assertIn(
            "e2-bob-demo-replay-camroute0-atan2v2-camv1-stage16", baseline_dir
        )
        self.assertNotIn("-idle", baseline_dir)
        self.assertNotIn("-disc", baseline_dir)
        self.assertNotIn("-range", baseline_dir)

        variants = {
            "camera_variant": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_CAMERA_VARIANT=2",
            ),
            "camera_route": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_SOURCEBOOT_CAMERA_ROUTE=1",
            ),
            "idle_start": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_SOURCEBOOT_CAMERA_ROUTE=1", "SATURN_CAMERA_IDLE_START_TICK=40",
            ),
            "idle_discovery": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_SOURCEBOOT_CAMERA_ROUTE=1", "SATURN_CAMERA_IDLE_DISCOVERY=1",
            ),
            "range_capture": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_SOURCEBOOT_CAMERA_ROUTE=1", "SATURN_CAMERA_RANGE_CAPTURE=1",
            ),
            "cart_stage": sourceboot_make(
                "SATURN_DEMO_PATH=1", "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                "SATURN_SOURCE_CART_STAGE_SECTORS=8",
            ),
        }
        variant_dirs = set()
        for name, result in variants.items():
            self.assertEqual(result.returncode, 0, f"{name}: {result.stderr}")
            output_dir = make_value(result.stdout, "SH_OUTPUT_DIR")
            self.assertNotEqual(output_dir, baseline_dir, name)
            variant_dirs.add(output_dir)
        self.assertEqual(len(variant_dirs), len(variants))

        route_one_dir = make_value(variants["camera_route"].stdout, "SH_OUTPUT_DIR")
        self.assertIn("-idle0-disc0-range0-stage16-r6000", route_one_dir)

        for assignment, message in (
            ("SATURN_CAMERA_VARIANT=3", "SATURN_CAMERA_VARIANT must be 1 (float) or 2 (Q)"),
            ("SATURN_SOURCEBOOT_CAMERA_ROUTE=2", "SATURN_SOURCEBOOT_CAMERA_ROUTE must be 0 (bob-parity-v1) or 1 (bob-default-camera-v1)"),
            ("SATURN_CAMERA_IDLE_DISCOVERY=2", "SATURN_CAMERA_IDLE_DISCOVERY must be 0 or 1"),
            ("SATURN_CAMERA_RANGE_CAPTURE=2", "SATURN_CAMERA_RANGE_CAPTURE must be 0 or 1"),
            ("SATURN_SOURCE_CART_STAGE_SECTORS=3", "SATURN_SOURCE_CART_STAGE_SECTORS must be 4, 8, or 16"),
            ("SATURN_SOURCEBOOT_CAMERA_ROUTE=1", "SATURN_SOURCEBOOT_CAMERA_ROUTE=1 requires SATURN_SOURCEBOOT_ROUTE_REPLAY=1"),
        ):
            result = sourceboot_make(assignment)
            self.assertEqual(result.returncode, 2, assignment)
            self.assertIn(message, result.stderr)

        for assignment in (
            "SATURN_CAMERA_IDLE_DISCOVERY=1",
            "SATURN_CAMERA_RANGE_CAPTURE=1",
        ):
            result = sourceboot_make(assignment)
            self.assertEqual(result.returncode, 0, result.stderr)
            route_zero_dir = make_value(result.stdout, "SH_OUTPUT_DIR")
            self.assertNotIn("-idle", route_zero_dir)
            self.assertNotIn("-disc", route_zero_dir)
            self.assertNotIn("-range", route_zero_dir)
if __name__ == "__main__":
    unittest.main()
