"""SCC1 raw contract tests.

Breaks caught: accepting a malformed byte window, omitting a mapped sample
word, or treating a baseline/Q divergence as stable camera state.
"""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

from camera_idle_contract import (
    Scc1Error, compare_same_role, decode_scc1,
    validate_scc1,
)

SCC1_MAGIC = 0x53434331
SCC1_VERSION = 1
SCC1_HEADER_WORDS = 24
SCC1_SAMPLE_WORDS = 81
SCC1_SAMPLE_COUNT = 600
SCC1_PAYLOAD_WORDS = 48600
SCC1_BYTES = 194496
SCC1_ROUTE_ID = 2
ROLE_NAMES = {
    1: "camera-source-baseline",
    2: "camera-bypass-diagnostic",
    3: "camera-fixed-candidate",
}

FLOAT_WORDS = set(range(4, 10)) | set(range(12, 30)) | set(range(32, 42)) | set(range(44, 51)) | set(range(52, 59)) | {61, 63} | set(range(65, 75)) | {78}
PACKED_WORDS = {10, 11, 30, 31, 42, 43, 51, 59, 60, 62, 64, 75, 76, 77, 79, 80}


def f32(value: float) -> int:
    return struct.unpack(">I", struct.pack(">f", value))[0]


def build_scc1(*, variant: int = 1, bridges: tuple[int, int] = (0, 0),
               generation: int | None = None, idle_start: int = 31) -> bytes:
    """Hand-written big endian SCC1 fixture; never imports production layout."""
    if generation is None:
        generation = 0 if variant == 1 else 7
    header = [
        SCC1_MAGIC, SCC1_VERSION, SCC1_HEADER_WORDS, SCC1_SAMPLE_WORDS,
        SCC1_SAMPLE_COUNT, variant, 2, 0x53425234, 5, 2000, idle_start,
        2000 + idle_start, 0, 0x3F, 0, 0, 0, 0, 0, bridges[0], bridges[1],
        generation, SCC1_PAYLOAD_WORDS, SCC1_ROUTE_ID,
    ]
    sample = [0] * SCC1_SAMPLE_WORDS
    sample[1] = 0
    sample[2] = 0x3F
    for offset in FLOAT_WORDS:
        sample[offset] = f32(1.25 + offset / 16.0)
    sample[71] = 0x43AF0000
    # Signed two's complement fixtures at every packed map position.
    for offset in PACKED_WORDS:
        sample[offset] = 0x8001FFFF if offset != 64 else 0xFFFFFFFE
    sample[77] &= 0xFFFF0000
    sample[80] &= 0xFFFF0000
    words = header[:]
    for tick in range(SCC1_SAMPLE_COUNT):
        item = sample[:]
        item[0] = header[11] + tick
        words.extend(item)
    return struct.pack(">48624I", *words)


def mutate_word(raw: bytes, word_index: int, value: int) -> bytes:
    values = list(struct.unpack(">48624I", raw))
    values[word_index] = value
    return struct.pack(">48624I", *values)


def mutate_every_sample_word(raw: bytes, offset: int, value: int) -> bytes:
    values = list(struct.unpack(">48624I", raw))
    for sample in range(SCC1_SAMPLE_COUNT):
        values[SCC1_HEADER_WORDS + sample * SCC1_SAMPLE_WORDS + offset] = value
    return struct.pack(">48624I", *values)


class Scc1DecodeTest(unittest.TestCase):
    def test_decodes_hand_written_big_endian_capture(self) -> None:
        capture = decode_scc1(build_scc1())
        self.assertEqual(capture.header[0], SCC1_MAGIC)
        self.assertEqual(len(capture.samples), 600)
        self.assertEqual(capture.samples[0].source_tick, 2031)
        self.assertEqual(capture.samples[-1].source_tick, 2630)
        self.assertEqual(capture.samples[0].state_words[67], 0x43AF0000)

    def test_rejects_wrong_byte_lengths(self) -> None:
        raw = build_scc1()
        for bad in (raw[:-1], raw + b"\0"):
            with self.assertRaises(Scc1Error):
                decode_scc1(bad)

    def test_rejects_each_header_word_mutation(self) -> None:
        raw = build_scc1()
        for offset in set(range(SCC1_HEADER_WORDS)) - {19, 20, 21}:
            with self.subTest(offset=offset), self.assertRaises(Scc1Error):
                decode_scc1(mutate_word(raw, offset, 0xDEADBEEF))

    def test_rejects_each_mapped_sample_word_mutation(self) -> None:
        raw = build_scc1()
        base = SCC1_HEADER_WORDS
        for offset in range(SCC1_SAMPLE_WORDS):
            with self.subTest(offset=offset), self.assertRaises(Scc1Error):
                capture = decode_scc1(mutate_word(raw, base + offset, 0xDEADBEEF))
                validate_scc1(capture, expected_role="camera-source-baseline",
                              expected_idle_start_tick=31, expected_route_id=2)

    def test_rejects_later_state_drift_nonfinite_and_reserved_bits(self) -> None:
        raw = build_scc1()
        later = SCC1_HEADER_WORDS + SCC1_SAMPLE_WORDS + 4
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, later, 0x7FC00000))
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, SCC1_HEADER_WORDS + 2, 0x40))
        with self.assertRaises(Scc1Error):
            decode_scc1(mutate_word(raw, SCC1_HEADER_WORDS + 77, 1))

    def test_rejects_a_wrong_zoom_witness_even_when_every_sample_agrees(self) -> None:
        raw = mutate_every_sample_word(build_scc1(), 71, f32(349.0))
        with self.assertRaises(Scc1Error):
            decode_scc1(raw)


class Scc1ValidationTest(unittest.TestCase):
    def test_validates_each_phase_a_role_without_legacy_bridge_counts(self) -> None:
        for role_id, role_name in ROLE_NAMES.items():
            with self.subTest(role=role_name):
                capture = decode_scc1(build_scc1(variant=role_id, bridges=(0, 0), generation=0))
                validate_scc1(capture, expected_role=role_name,
                              expected_idle_start_tick=31, expected_route_id=2)

    def test_rejects_unknown_phase_a_role(self) -> None:
        with self.assertRaises(Scc1Error):
            validate_scc1(decode_scc1(build_scc1()), expected_role="camera-q",
                          expected_idle_start_tick=31, expected_route_id=2)

    def test_rejects_nonneutral_input_missing_flag_route_and_zoom(self) -> None:
        cases = [
            (SCC1_HEADER_WORDS + 1, 1), (SCC1_HEADER_WORDS + 2, 0x3D),
            (23, 3), (SCC1_HEADER_WORDS + 71, f32(349.0)),
        ]
        raw = build_scc1(variant=2, bridges=(0, 0), generation=0)
        for index, value in cases:
            with self.subTest(word=index), self.assertRaises(Scc1Error):
                validate_scc1(decode_scc1(mutate_word(raw, index, value)),
                              expected_role="camera-bypass-diagnostic", expected_idle_start_tick=31,
                              expected_route_id=2)

    def test_same_role_requires_raw_byte_identity(self) -> None:
        raw = build_scc1()
        compare_same_role(decode_scc1(raw), decode_scc1(raw))
        changed = mutate_word(raw, SCC1_HEADER_WORDS + SCC1_SAMPLE_WORDS + 10, f32(3.0))
        with self.assertRaises(Scc1Error):
            compare_same_role(decode_scc1(raw), decode_scc1(changed))


class SaturnCameraProbeMappingTest(unittest.TestCase):
    def test_real_camera_translation_unit_packs_each_semantic_offset_once(self) -> None:
        root = Path(__file__).resolve().parents[2]
        harness_source = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/camera.h"
#include "port/saturn/runtime/saturn_camera_probe.h"

Vec3f sOldPosition, sOldFocus;
struct ModeTransitionInfo sModeInfo;
struct TransitionInfo sModeTransition;
s16 sAreaYaw, sLakituDist, sLakituPitch, sModeOffsetYaw, sYawSpeed;
f32 gCameraZoomDist, sZoomAmount, sPanDistance, sZeroZoomDist;
struct Camera *gCamera;
struct LakituState gLakituState;

static f32 fb(uint32_t bits) { union { uint32_t u; f32 f; } v = { bits }; return v.f; }

int main(void) {
    static struct Camera camera;
    sm64_saturn_camera_probe_snapshot_t out;
    memset(&camera, 0, sizeof(camera));
    memset(&gLakituState, 0, sizeof(gLakituState));
    gCamera = &camera;
    camera.pos[0]=fb(0x3f800001); camera.pos[1]=fb(0x3f800002); camera.pos[2]=fb(0x3f800003);
    camera.focus[0]=fb(0x3f800004); camera.focus[1]=fb(0x3f800005); camera.focus[2]=fb(0x3f800006);
    camera.yaw=-2; camera.nextYaw=3; camera.mode=4; camera.defMode=5; camera.cutscene=0; camera.doorStatus=7;
    gLakituState.curPos[0]=fb(0x3f800008); gLakituState.curPos[1]=fb(0x3f800009); gLakituState.curPos[2]=fb(0x3f80000a);
    gLakituState.curFocus[0]=fb(0x3f80000b); gLakituState.curFocus[1]=fb(0x3f80000c); gLakituState.curFocus[2]=fb(0x3f80000d);
    gLakituState.goalPos[0]=fb(0x3f80000e); gLakituState.goalPos[1]=fb(0x3f80000f); gLakituState.goalPos[2]=fb(0x3f800010);
    gLakituState.goalFocus[0]=fb(0x3f800011); gLakituState.goalFocus[1]=fb(0x3f800012); gLakituState.goalFocus[2]=fb(0x3f800013);
    gLakituState.pos[0]=fb(0x3f800014); gLakituState.pos[1]=fb(0x3f800015); gLakituState.pos[2]=fb(0x3f800016);
    gLakituState.focus[0]=fb(0x3f800017); gLakituState.focus[1]=fb(0x3f800018); gLakituState.focus[2]=fb(0x3f800019);
    gLakituState.yaw=-26; gLakituState.nextYaw=27; gLakituState.roll=-28; gLakituState.mode=29; gLakituState.defMode=30;
    gLakituState.focHSpeed=fb(0x3f80001f); gLakituState.focVSpeed=fb(0x3f800020);
    gLakituState.posHSpeed=fb(0x3f800021); gLakituState.posVSpeed=fb(0x3f800022);
    sOldPosition[0]=fb(0x3f800023); sOldPosition[1]=fb(0x3f800024); sOldPosition[2]=fb(0x3f800025);
    sOldFocus[0]=fb(0x3f800026); sOldFocus[1]=fb(0x3f800027); sOldFocus[2]=fb(0x3f800028);
    sModeInfo.newMode=-41; sModeInfo.lastMode=42; sModeInfo.max=-43; sModeInfo.frame=44;
    sModeInfo.transitionStart.focus[0]=fb(0x3f80002d); sModeInfo.transitionStart.focus[1]=fb(0x3f80002e); sModeInfo.transitionStart.focus[2]=fb(0x3f80002f);
    sModeInfo.transitionStart.pos[0]=fb(0x3f800030); sModeInfo.transitionStart.pos[1]=fb(0x3f800031); sModeInfo.transitionStart.pos[2]=fb(0x3f800032);
    sModeInfo.transitionStart.dist=fb(0x3f800033); sModeInfo.transitionStart.pitch=-52; sModeInfo.transitionStart.yaw=53;
    sModeInfo.transitionEnd.focus[0]=fb(0x3f800036); sModeInfo.transitionEnd.focus[1]=fb(0x3f800037); sModeInfo.transitionEnd.focus[2]=fb(0x3f800038);
    sModeInfo.transitionEnd.pos[0]=fb(0x3f800039); sModeInfo.transitionEnd.pos[1]=fb(0x3f80003a); sModeInfo.transitionEnd.pos[2]=fb(0x3f80003b);
    sModeInfo.transitionEnd.dist=fb(0x3f80003c); sModeInfo.transitionEnd.pitch=-61; sModeInfo.transitionEnd.yaw=62;
    sModeTransition.posPitch=-63; sModeTransition.posYaw=64; sModeTransition.posDist=fb(0x3f800041);
    sModeTransition.focPitch=-66; sModeTransition.focYaw=67; sModeTransition.focDist=fb(0x3f800044);
    sModeTransition.framesLeft=0;
    sModeTransition.marioPos[0]=fb(0x3f800046); sModeTransition.marioPos[1]=fb(0x3f800047); sModeTransition.marioPos[2]=fb(0x3f800048);
    camera.areaCenX=fb(0x3f800049); camera.areaCenY=fb(0x3f80004a); camera.areaCenZ=fb(0x3f80004b);
    gCameraZoomDist=fb(0x43af0000); sZoomAmount=fb(0x3f80004d); sPanDistance=fb(0x3f80004e); sZeroZoomDist=fb(0x3f80004f);
    sYawSpeed=-80; sLakituDist=81; sLakituPitch=-82; sModeOffsetYaw=83; sAreaYaw=-84;
    gLakituState.focusDistance=fb(0x3f800055); gLakituState.oldPitch=-86; gLakituState.oldYaw=87; gLakituState.oldRoll=-88;
    if (!sm64_saturn_camera_probe_read(&out)) return 2;
    for (unsigned i=0; i<SM64_SATURN_CAMERA_PROBE_STATE_WORDS; ++i) printf("%08x\n", out.state_words[i]);
    printf("flags=%08x dispatch=%08x diag=%08x\n", out.camera_flags, out.source_dispatch,
           out.diagnostics.overflow_count | out.diagnostics.saturation_count |
           out.diagnostics.divide_fault_count | out.diagnostics.unexpected_reseed_count |
           out.diagnostics.range_fallback_count | out.diagnostics.bridge_export_count |
           out.diagnostics.bridge_import_count | out.diagnostics.shadow_generation);
    return 0;
}
'''
        camera_source = (root / "src/game/camera.c").read_text(encoding="utf-8")
        packer_start = camera_source.index(
            "static u32 saturn_camera_probe_f32_bits"
        )
        packer_end = camera_source.index("\n#endif", packer_start)
        harness_source = harness_source.replace(
            "int main(void)", camera_source[packer_start:packer_end] + "\n\nint main(void)"
        )
        expected = [
            0x3F800001,0x3F800002,0x3F800003,0x3F800004,0x3F800005,0x3F800006,
            0xFFFE0003,0x04050007,0x3F800008,0x3F800009,0x3F80000A,0x3F80000B,
            0x3F80000C,0x3F80000D,0x3F80000E,0x3F80000F,0x3F800010,0x3F800011,
            0x3F800012,0x3F800013,0x3F800014,0x3F800015,0x3F800016,0x3F800017,
            0x3F800018,0x3F800019,0xFFE6001B,0xFFE41D1E,0x3F80001F,0x3F800020,
            0x3F800021,0x3F800022,0x3F800023,0x3F800024,0x3F800025,0x3F800026,
            0x3F800027,0x3F800028,0xFFD7002A,0xFFD5002C,0x3F80002D,0x3F80002E,
            0x3F80002F,0x3F800030,0x3F800031,0x3F800032,0x3F800033,0xFFCC0035,
            0x3F800036,0x3F800037,0x3F800038,0x3F800039,0x3F80003A,0x3F80003B,
            0x3F80003C,0xFFC3003E,0xFFC10040,0x3F800041,0xFFBE0043,0x3F800044,
            0x00000000,0x3F800046,0x3F800047,0x3F800048,0x3F800049,0x3F80004A,
            0x3F80004B,0x43AF0000,0x3F80004D,0x3F80004E,0x3F80004F,0xFFB00051,
            0xFFAE0053,0xFFAC0000,0x3F800055,0xFFAA0057,0xFFA80000,
        ]
        environment = os.environ.copy()
        environment.pop("COMPILER_PATH", None)
        environment["PATH"] = r"C:\msys64\mingw64\bin;" + environment.get("PATH", "")
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            harness = temp / "probe.c"
            executable = temp / "probe.exe"
            harness.write_text(harness_source, encoding="utf-8")
            completed = subprocess.run(
                [
                    r"C:\msys64\mingw64\bin\gcc.exe", "-std=c11",
                    "-DTARGET_SATURN=1", "-DNON_MATCHING=1", "-DAVOID_UB=1",
                    "-DVERSION_US=1", "-D_LANGUAGE_C=1", "-DF3DEX_GBI_2E=1",
                    "-I", str(root), "-I", str(root / "include"),
                    "-I", str(root / "src"), "-I", str(root / "src/game"),
                    str(harness), "-o", str(executable),
                ],
                capture_output=True, text=True, env=environment,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True,
                                 env=environment)
        self.assertEqual(run.returncode, 0, run.stderr)
        lines = run.stdout.splitlines()
        self.assertEqual([int(line, 16) for line in lines[:77]], expected)
        self.assertEqual(lines[77], "flags=00000019 dispatch=00000000 diag=00000000")


class SourcebootCameraRecorderTest(unittest.TestCase):
    def _build_and_run(self, discovery: int, drift: int = 0) -> list[str]:
        root = Path(__file__).resolve().parents[2]
        harness_source = r'''
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "game/level_update.h"
#include "port/saturn/runtime/saturn_camera_probe.h"
#include "port/saturn/sourceboot/source_camera_idle_probe.h"

struct MarioState mario;
struct MarioState gMarioStates[1];
struct MarioState *gMarioState = &mario;
static sm64_saturn_camera_probe_snapshot_t camera;

s32 sm64_saturn_camera_probe_read(sm64_saturn_camera_probe_snapshot_t *out) {
    *out = camera;
    return 1;
}

static void seed(void) {
    memset(&camera, 0, sizeof(camera));
    camera.camera_flags = 0x19;
    for (unsigned i=0; i<77; ++i) camera.state_words[i] = 0x01000000U + i;
    camera.state_words[67] = 0x43AF0000U;
    memset(&mario, 0, sizeof(mario));
    mario.action = 0x04000440U;
    mario.pos[0] = 1.0f; mario.pos[1] = 2.0f; mario.pos[2] = 3.0f;
    mario.faceAngle[0] = -4; mario.faceAngle[1] = 5; mario.faceAngle[2] = -6;
}

int main(void) {
    sm64_saturn_source_runtime_state_t runtime;
    memset(&runtime, 0, sizeof(runtime));
    runtime.input_replay_complete = 1;
    runtime.input_replay_ticks = 2000;
    seed();
    sm64_saturn_sourceboot_camera_idle_probe_reset();
#if SATURN_CAMERA_IDLE_DISCOVERY
#if TEST_DRIFT
    for (uint32_t tick=2000; tick<=2059; ++tick)
        sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, tick);
    camera.state_words[0] ^= 1U;
    sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, 2060);
    camera.state_words[0] ^= 1U;
    for (uint32_t tick=2061; tick<=2700; ++tick)
        sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, tick);
    printf("%08x\n", sourceboot_camera_idle_capture.words[0]);
#else
    for (uint32_t tick=2000; tick<=2599; ++tick)
        sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, tick);
    uint32_t latched = sourceboot_camera_idle_capture.words[24 + 4];
    camera.state_words[0] ^= 1U;
    sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, 2600);
    printf("%08x %u %u %u %08x %08x %u %u\n",
           sourceboot_camera_idle_capture.words[0],
           sourceboot_camera_idle_capture.words[10],
           sourceboot_camera_idle_capture.words[11],
           sourceboot_camera_idle_capture.words[23],
           sourceboot_camera_idle_capture.words[24 + 2],
           sourceboot_camera_idle_capture.words[24 + 3],
           sourceboot_camera_idle_capture.words[24 + 599 * 81],
           sourceboot_camera_idle_capture.words[24 + 4] == latched);
#endif
#else
    for (uint32_t tick=2005; tick<=2034; ++tick)
        sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, tick);
    camera.state_words[0] ^= 1U;
    sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, 2035);
    camera.state_words[0] ^= 1U;
    for (uint32_t tick=2036; tick<=2700; ++tick)
        sm64_saturn_sourceboot_camera_idle_probe_record(&runtime, tick);
    printf("%08x\n", sourceboot_camera_idle_capture.words[0]);
#endif
    return 0;
}
'''
        environment = os.environ.copy()
        environment.pop("COMPILER_PATH", None)
        environment["PATH"] = r"C:\msys64\mingw64\bin;" + environment.get("PATH", "")
        with tempfile.TemporaryDirectory() as temporary:
            temp = Path(temporary)
            (temp / "cpu").mkdir()
            (temp / "cpu/cache.h").write_text(
                "#define CPU_CACHE_THROUGH 0U\n", encoding="utf-8"
            )
            harness = temp / "record.c"
            executable = temp / "record.exe"
            harness.write_text(harness_source, encoding="utf-8")
            completed = subprocess.run(
                [
                    r"C:\msys64\mingw64\bin\gcc.exe", "-std=c11", "-Wall",
                    "-Wextra", "-Werror", f"-DSATURN_CAMERA_IDLE_DISCOVERY={discovery}",
                    f"-DTEST_DRIFT={drift}",
                    "-DSATURN_CAMERA_IDLE_START_TICK=5", "-DSATURN_CAMERA_VARIANT=1",
                    "-DNON_MATCHING=1", "-DAVOID_UB=1", "-DVERSION_US=1",
                    "-I", str(temp), "-I", str(root), "-I", str(root / "include"),
                    "-I", str(root / "src"), "-I", str(root / "src/game"),
                    "-I", str(root / "src/port/saturn/runtime"),
                    "-I", str(root / "src/port/saturn/sourceboot"),
                    str(harness),
                    str(root / "src/port/saturn/sourceboot/source_camera_idle_probe.c"),
                    "-o", str(executable),
                ],
                capture_output=True, text=True, env=environment,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True,
                                 env=environment)
        self.assertEqual(run.returncode, 0, run.stderr)
        return run.stdout.splitlines()

    def test_discovery_backfills_60_ticks_publishes_magic_last_and_latches(self) -> None:
        self.assertEqual(
            self._build_and_run(1),
            ["53434331 0 2000 2 0000003f 00000000 2599 1"],
        )

    def test_fixed_window_change_fails_without_searching_later(self) -> None:
        self.assertEqual(self._build_and_run(0), ["00000000"])

    def test_discovery_change_after_backfill_fails_without_restarting(self) -> None:
        self.assertEqual(self._build_and_run(1, drift=1), ["00000000"])


class SourceTickBoundaryTest(unittest.TestCase):
    def test_recorder_runs_after_sim_accounting_and_before_route_publication(self) -> None:
        root = Path(__file__).resolve().parents[2]
        source = (root / "src/port/saturn/sourceboot/main.c").read_text(
            encoding="utf-8"
        )
        start = source.index("static void sourceboot_run_source_tick(void)")
        end = source.index("\n}", start)
        function = source[start:end]
        game = function.index("game_loop_one_iteration();")
        accum = function.index(
            "sourceboot_fast3d.profile.sim_frt_ticks_accum"
        )
        count = function.index("sourceboot_fast3d.profile.sim_tick_count")
        record = function.index(
            "sm64_saturn_sourceboot_camera_idle_probe_record("
        )
        self.assertLess(game, accum)
        self.assertLess(accum, count)
        self.assertLess(count, record)
        self.assertNotIn("sourceboot_capture_route_checkpoint", function)
        configure = source.index(
            "sm64_saturn_source_runtime_configure_input_replay("
        )
        reset = source.index(
            "sm64_saturn_sourceboot_camera_idle_probe_reset();"
        )
        game_loop = source.index("thread5_game_loop(NULL);")
        self.assertLess(configure, reset)
        self.assertLess(reset, game_loop)


if __name__ == "__main__":
    unittest.main()
