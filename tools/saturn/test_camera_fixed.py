#!/usr/bin/env python3
"""Host contract for the Phase A integer fixed follow camera."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HOST_GCC = Path("C:/msys64/mingw64/bin/gcc.exe")
FIXED_SOURCE = ROOT / "src/port/saturn/runtime/saturn_camera_fixed.c"
TRIG_SOURCE = ROOT / "src/port/saturn/gfx/saturn_trig_q16.inc.c"


def host_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("COMPILER_PATH", None)
    environment["PATH"] = "C:\\msys64\\mingw64\\bin;" + environment.get("PATH", "")
    return environment


class SaturnCameraFixedTest(unittest.TestCase):
    def test_fixed_follow_semantics_and_integer_publication(self) -> None:
        harness_source = r'''
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/camera.h"
#include "port/saturn/runtime/saturn_camera_fixed.h"

static uint32_t bits(f32 value) {
    uint32_t result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

int main(void) {
    struct Camera camera;
    struct LakituState lakitu;
    struct MarioState mario;
    sm64_saturn_camera_fixed_state_t state;
    memset(&camera, 0, sizeof(camera));
    memset(&lakitu, 0, sizeof(lakitu));
    memset(&mario, 0, sizeof(mario));

    camera.focus[0] = 1.0f; camera.focus[1] = 2.0f; camera.focus[2] = 3.0f;
    camera.pos[0] = 1.0f; camera.pos[1] = 2.0f; camera.pos[2] = 803.0f;
    mario.pos[0] = 1.0f; mario.pos[1] = -118.0f; mario.pos[2] = 3.0f;
    camera.yaw = 0x7ff0;
    sm64_saturn_camera_fixed_init(&state, &camera);
    sm64_saturn_camera_fixed_step(&state, &mario, 0, 0);
    if (state.rendered_focus[0] != (1 << 16)) return 10;

    sm64_saturn_camera_fixed_step(&state, &mario, 0x40, 0);
    if ((uint16_t)state.yaw != 0x8030) return 11;

    mario.pos[0] = 1024.0f;
    sm64_saturn_camera_fixed_step(&state, &mario, 0, 0);
    if (state.rendered_focus[0] <= (1 << 16) ||
        state.rendered_focus[0] >= (1024 << 16)) return 12;

    state.distance = INT32_MAX; state.vertical_offset = INT32_MIN;
    sm64_saturn_camera_fixed_step(&state, &mario, 0, 0);
    if (state.distance != INT32_MAX || state.vertical_offset != INT32_MIN ||
        (state.diagnostics & SM64_SATURN_CAMERA_FIXED_DIAG_SATURATED) == 0) return 13;

    state.rendered_focus[0] = 0x00018000;
    state.rendered_focus[1] = -0x00018000;
    state.rendered_focus[2] = 0;
    state.rendered_position[0] = 0x00010000;
    state.yaw = -7;
    sm64_saturn_camera_fixed_publish(&state, &camera, &lakitu);
    if (bits(camera.focus[0]) != 0x3fc00000U ||
        bits(camera.focus[1]) != 0xbfc00000U || bits(camera.focus[2]) != 0U ||
        bits(camera.pos[0]) != 0x3f800000U || camera.yaw != -7) return 14;
    state.rendered_focus[0] = INT32_MIN;
    sm64_saturn_camera_fixed_publish(&state, &camera, &lakitu);
    if (bits(camera.focus[0]) != 0xc7000000U) return 16;
    if ((state.diagnostics & SM64_SATURN_CAMERA_FIXED_DIAG_UNSUPPORTED_OBSTRUCTION) == 0)
        return 15;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            harness = directory / "fixed_harness.c"
            executable = directory / "fixed_harness.exe"
            harness.write_text(harness_source, encoding="utf-8")
            result = subprocess.run(
                [str(HOST_GCC), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DNON_MATCHING=1", "-DAVOID_UB=1", "-DTARGET_SATURN=1",
                 "-I", str(ROOT), "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                 str(harness), str(FIXED_SOURCE), str(TRIG_SOURCE), "-o", str(executable)],
                capture_output=True, text=True, env=host_environment(), check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True,
                env=host_environment(), check=False, timeout=3,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)

    def test_candidate_source_has_no_forbidden_helpers(self) -> None:
        source = FIXED_SOURCE.read_text(encoding="utf-8")
        forbidden = ("float", "double", "fix16_div", "sqrt", "sin(", "cos(",
                     "__muldi3", "__divdi3", "int64_t", "uint64_t")
        for token in forbidden:
            self.assertNotIn(token, source, token)
        self.assertIn("sm64_saturn_q16_mul_sh2", source)
        self.assertIn("memcpy", source)

    def test_host_assembly_has_no_forbidden_helper_edges(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            assembly = Path(temporary) / "saturn_camera_fixed.s"
            result = subprocess.run(
                [str(HOST_GCC), "-std=c11", "-O2", "-S", "-DNON_MATCHING=1",
                 "-DAVOID_UB=1", "-DTARGET_SATURN=1", "-I", str(ROOT),
                 "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                 str(FIXED_SOURCE), "-o", str(assembly)],
                capture_output=True, text=True, env=host_environment(), check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            text = assembly.read_text(encoding="utf-8")
            for helper in ("__muldi3", "__divdi3", "__udivdi3", "__addsf3",
                           "__subsf3", "__mulsf3", "__divsf3", "sqrt", "sin", "cos"):
                self.assertNotIn(helper, text, helper)


if __name__ == "__main__":
    unittest.main()
