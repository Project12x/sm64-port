#!/usr/bin/env python3
"""Host contract for the held-camera attribution role."""

from __future__ import annotations

import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
HOST_GCC = Path("C:/msys64/mingw64/bin/gcc.exe")
ROLE_SOURCE = ROOT / "src/port/saturn/runtime/saturn_camera_role.c"
FIXED_SOURCE = ROOT / "src/port/saturn/runtime/saturn_camera_fixed.c"
TRIG_SOURCE = ROOT / "src/port/saturn/gfx/saturn_trig_q16.inc.c"


def host_environment() -> dict[str, str]:
    environment = os.environ.copy()
    environment.pop("COMPILER_PATH", None)
    environment["PATH"] = "C:\\msys64\\mingw64\\bin;" + environment.get("PATH", "")
    return environment


class SaturnCameraRoleContractTest(unittest.TestCase):
    def test_fixed_role_publishes_renderer_facing_lakitu_state(self) -> None:
        harness_source = r'''
#include <stdint.h>
#include <string.h>
#include "game/camera.h"
#include "game/mario.h"
#include "port/saturn/runtime/saturn_camera_role.h"

struct Camera *gCamera;
struct LakituState gLakituState;
struct MarioState *gMarioState;

static int source_updates;

static void call_update(struct Camera *camera) {
    gCamera = camera;
    if (!sm64_saturn_camera_role_update(camera))
        source_updates++;
}

int main(void) {
    struct Camera camera;
    struct MarioState mario;
    memset(&camera, 0, sizeof(camera));
    memset(&mario, 0, sizeof(mario));
    memset(&gLakituState, 0x5A, sizeof(gLakituState));

    camera.focus[0] = 100.0f; camera.focus[1] = 120.0f; camera.focus[2] = 200.0f;
    camera.pos[0] = 100.0f; camera.pos[1] = 120.0f; camera.pos[2] = 1000.0f;
    camera.mode = 9; camera.defMode = 10;
    mario.pos[0] = 100.0f; mario.pos[1] = 0.0f; mario.pos[2] = 200.0f;
    gMarioState = &mario;

    call_update(&camera);
    if (source_updates != 0)
        return 10;
    if (gLakituState.pos[0] != 100.0f || gLakituState.pos[1] != 120.0f ||
        gLakituState.pos[2] != 1000.0f || gLakituState.focus[0] != 100.0f ||
        gLakituState.focus[1] != 120.0f || gLakituState.focus[2] != 200.0f)
        return 11;
    if (gLakituState.mode != 9 || gLakituState.defMode != 10 ||
        gLakituState.yaw != 0 || gLakituState.nextYaw != 0)
        return 12;
    if (gLakituState.oldPitch != 0 || gLakituState.oldYaw != (int16_t)0x8000)
        return 13;
    if (gLakituState.focusDistance != 800.0f)
        return 14;
    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            harness = directory / "fixed_role_harness.c"
            executable = directory / "fixed_role_harness.exe"
            harness.write_text(harness_source, encoding="utf-8")
            result = subprocess.run(
                [str(HOST_GCC), "-std=c11", "-Wall", "-Wextra", "-Werror",
                 "-DNON_MATCHING=1", "-DAVOID_UB=1", "-DTARGET_SATURN=1",
                 "-DSATURN_CAMERA_VARIANT=3",
                 "-I", str(ROOT), "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                 str(harness), str(ROLE_SOURCE), str(FIXED_SOURCE), str(TRIG_SOURCE),
                 "-o", str(executable)],
                capture_output=True, text=True, env=host_environment(), check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True,
                env=host_environment(), check=False,
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)

    def test_bypass_holds_the_seeded_camera_and_lakitu_pose(self) -> None:
        harness_source = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/camera.h"
#include "port/saturn/runtime/saturn_camera_role.h"

struct Camera *gCamera;
struct LakituState gLakituState;

static int source_updates;

static void call_update(struct Camera *camera) {
    gCamera = camera;
    if (!sm64_saturn_camera_role_update(camera))
        source_updates++;
}

int main(void) {
    struct Camera camera;
    memset(&camera, 0, sizeof(camera));
    memset(&gLakituState, 0, sizeof(gLakituState));

    if (SM64_SATURN_CAMERA_SOURCE_BASELINE != 1 ||
        SM64_SATURN_CAMERA_BYPASS_DIAGNOSTIC != 2 ||
        SM64_SATURN_CAMERA_FIXED_CANDIDATE != 3)
        return 10;
#if SATURN_CAMERA_VARIANT == 1
    call_update(&camera);
    sm64_saturn_camera_bypass_arm(2000U);
    call_update(&camera);
    return source_updates == 2 && !sm64_saturn_camera_bypass_is_armed() ? 0 : 11;
#else
    struct Camera seeded;
    struct LakituState lakitu_seeded;
    call_update(&camera);
    if (source_updates != 1 || sm64_saturn_camera_bypass_is_armed())
        return 11;

    camera.pos[0] = 1.0f; camera.pos[1] = 2.0f; camera.pos[2] = 3.0f;
    camera.focus[0] = 4.0f; camera.focus[1] = 5.0f; camera.focus[2] = 6.0f;
    camera.yaw = -7; camera.nextYaw = 8; camera.mode = 9; camera.defMode = 10;
    gLakituState.curPos[0] = 11.0f; gLakituState.goalFocus[2] = 12.0f;
    gLakituState.pos[1] = 13.0f; gLakituState.focus[2] = 14.0f;
    gLakituState.yaw = -15; gLakituState.nextYaw = 16; gLakituState.mode = 17;
    seeded = camera;
    lakitu_seeded = gLakituState;
    sm64_saturn_camera_bypass_arm(2000U);
    if (!sm64_saturn_camera_bypass_is_armed())
        return 12;

    memset(&camera, 0xA5, sizeof(camera));
    memset(&gLakituState, 0x5A, sizeof(gLakituState));
    call_update(&camera);
    if (source_updates != 1 || memcmp(&camera, &seeded, sizeof(camera)) != 0 ||
        memcmp(&gLakituState, &lakitu_seeded, sizeof(gLakituState)) != 0)
        return 13;
    return 0;
#endif
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            harness = directory / "role_harness.c"
            executable = directory / "role_harness.exe"
            harness.write_text(harness_source, encoding="utf-8")
            for variant in (1, 2):
                result = subprocess.run(
                    [str(HOST_GCC), "-std=c11", "-Wall", "-Wextra", "-Werror",
                     "-DNON_MATCHING=1", "-DAVOID_UB=1", "-DTARGET_SATURN=1",
                     f"-DSATURN_CAMERA_VARIANT={variant}",
                     "-I", str(ROOT), "-I", str(ROOT / "include"), "-I", str(ROOT / "src"),
                     str(harness), str(ROLE_SOURCE), "-o", str(executable)],
                    capture_output=True, text=True, env=host_environment(), check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                run_result = subprocess.run(
                    [str(executable)], capture_output=True, text=True,
                    env=host_environment(), check=False,
                )
                self.assertEqual(run_result.returncode, 0, run_result.stderr)


if __name__ == "__main__":
    unittest.main()
