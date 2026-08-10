#!/usr/bin/env python3
"""Dry-run contracts for the hermetic Saturn sourceboot Make pipeline."""

from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT = ROOT / "src/port/saturn/sourceboot"


class SourcebootHermeticBuildMakeTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.yaul = Path(self.temporary.name) / "yaul"
        share = self.yaul / "share"
        share.mkdir(parents=True)
        (share / "build.pre.mk").write_text(
            "SH_BUILD_PATH = $(abspath $(SH_BUILD_DIR))\n"
            "SH_OUTPUT_PATH = $(abspath $(SH_OUTPUT_DIR))\n"
            "SH_CC = fake-sh-gcc\nSH_AR = fake-sh-ar\nSH_OBJCOPY = fake-objcopy\n"
            "SH_CFLAGS = -m2 -mb -save-temps=obj\n",
            encoding="utf-8",
        )
        (share / "build.post.iso-cue.mk").write_text(
            "SH_SRCS_UNIQ := $(sort $(SH_SRCS))\n"
            "SH_SRCS_C := $(filter %.c,$(SH_SRCS_UNIQ))\n"
            "SH_SRCS_CXX := $(filter %.cc %.cpp %.cxx,$(SH_SRCS_UNIQ))\n"
            "SH_SRCS_S := $(filter %.sx,$(SH_SRCS_UNIQ))\n"
            "SH_OBJS_UNIQ := $(addsuffix .o,$(basename $(SH_SRCS_UNIQ)))\n"
            "SH_DEPS := $(SH_OBJS_UNIQ:.o=.d)\n",
            encoding="utf-8",
        )

    def tearDown(self) -> None:
        self.temporary.cleanup()

    @staticmethod
    def sourceboot_makefile() -> str:
        return (SOURCEBOOT / "Makefile").read_text(encoding="utf-8")

    def run_make(self, stage: str, goal: str, *variables: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update({
            "YAUL_INSTALL_ROOT": self.yaul.as_posix(),
            "YAUL_PROG_SH_PREFIX": "sh-elf",
            "YAUL_ARCH_SH_PREFIX": "sh-elf",
        })
        return subprocess.run(
            ["make", "-n", "--no-print-directory", "-C", str(SOURCEBOOT),
             f"SOURCEBOOT_BUILD_IDENTITY_STAGE={stage}", *variables, goal],
            check=False, capture_output=True, text=True, env=environment,
        )

    def assert_stage_rejected(self, stage: str, goal: str) -> None:
        result = self.run_make(stage, goal)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(f"SOURCEBOOT_BUILD_IDENTITY_STAGE={stage}", result.stderr)

    def test_outer_build_orders_assets_discovery_seal_build_and_verify(self) -> None:
        makefile = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
        positions = [makefile.index(token) for token in (
            "SOURCEBOOT_BUILD_IDENTITY_STAGE=assets identity-assets",
            "SOURCEBOOT_BUILD_IDENTITY_STAGE=discover identity-discovery",
            "print-identity-tag",
            "verify-sealed-inputs",
        )]
        self.assertEqual(positions, sorted(positions))

    def test_discovery_uses_real_flags_and_seal_consumes_manifests(self) -> None:
        makefile = self.sourceboot_makefile()
        self.assertIn("$(filter-out -save-temps=obj,$(SH_CFLAGS))", makefile)
        self.assertIn("$(foreach specs,$(SH_SPECS),-specs=$(specs))", makefile)
        self.assertIn("-ffile-prefix-map=$(ROOT)=.", makefile)
        self.assertIn("-fdebug-prefix-map=$(ROOT)=.", makefile)
        self.assertIn("-fmacro-prefix-map=$(ROOT)=.", makefile)
        self.assertIn("--source-closure", makefile)
        self.assertIn("--toolchain-attestation", makefile)
        self.assertIn("verify-sealed-inputs", makefile)
        self.assertIn("SOURCEBOOT_C_AND_CXX_DEPS", makefile)
        self.assertIn("SOURCEBOOT_POSTLINK_SX_DEPS", makefile)
        self.assertIn("--actual-depfile", makefile)
        self.assertIn("--assembly-scan-depfile", makefile)
        self.assertIn('--expected-external "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"', makefile)
        self.assertIn('--mode "$(SOURCEBOOT_RELEASE_MODE)"', makefile)
        self.assertIn('--verify "$(SOURCEBOOT_TOOLCHAIN_ATTESTATION)"', makefile)
        self.assertIn('--external-dependencies "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"', makefile)

    def test_discovery_stage_cannot_build_or_reuse_identity(self) -> None:
        self.assert_stage_rejected("discover", "all")
        self.assert_stage_rejected("assets", "identity-discovery")
        discovery = self.run_make("discover", "identity-discovery")
        self.assertEqual(discovery.returncode, 0, discovery.stderr)
        self.assertNotIn("--print-directory-tag", discovery.stdout)
        self.assertNotIn("bootstrap_sourceboot_identity_spec.py\" --root", discovery.stdout)

    def test_dry_run_propagates_profile_mode_and_classifies_cycle_breaking_inputs(self) -> None:
        result = self.run_make(
            "discover", "identity-discovery", "SOURCEBOOT_RELEASE_MODE=release",
            "SOURCEBOOT_TARGET_PROFILE=custom-bob-profile.json",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.replace("\\", "/")
        self.assertIn("sourceboot discovery profile=custom-bob-profile.json mode=release", output)
        outer = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
        self.assertIn('SOURCEBOOT_RELEASE_MODE="$(SOURCEBOOT_RELEASE_MODE)"', outer)
        self.assertIn('SOURCEBOOT_TARGET_PROFILE="$(SOURCEBOOT_TARGET_PROFILE)"', outer)
        self.assertIn("tools/saturn/profiles/sourceboot-bob-demo-v1.json", outer)
        for token in (
            "--recipe-input", "sourceboot.specs", "sourceboot-cart.x", "build.pre.mk",
            "--generator-input", "bootstrap_sourceboot_identity_spec.py",
            "--generated-input", "saturn_geo_depth_manifest.ld",
            "--derived-output", "saturn_build_identity_spec.json",
            "saturn-source-closure-v2.json", "saturn-toolchain-attestation-v1.json",
            "saturn-external-dependencies-v1.json", "SOURCE.DAT", ".elf", ".map", ".sym", ".asm",
        ):
            self.assertIn(token, output)

    def test_dry_run_scans_each_c_and_sx_source_once_with_flag_parity(self) -> None:
        result = self.run_make("discover", "identity-discovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        scans = [line for line in result.stdout.splitlines() if " -MM -MG " in line]
        self.assertGreater(len(scans), 2)
        sources = [line.rsplit('"', 2)[1].replace("\\", "/") for line in scans]
        self.assertEqual(len(sources), len(set(sources)))
        self.assertTrue(any(source.endswith(".c") for source in sources))
        self.assertTrue(any(source.endswith(".sx") for source in sources))
        for command in scans:
            self.assertIn("-m2 -mb", command)
            self.assertNotIn("-save-temps=obj", command)
            self.assertIn("-specs=sourceboot.specs", command)
            self.assertIn("-ffile-prefix-map=", command)
            self.assertIn("-fdebug-prefix-map=", command)
            self.assertIn("-fmacro-prefix-map=", command)


if __name__ == "__main__":
    unittest.main()
