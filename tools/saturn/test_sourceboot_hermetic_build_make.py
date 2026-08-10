#!/usr/bin/env python3
"""Dry-run contracts for the hermetic Saturn sourceboot Make pipeline."""

from __future__ import annotations

import os
import shlex
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
            "SH_CC = fake-sh-gcc\nSH_CXX = fake-sh-g++\n"
            "SH_AR = fake-sh-ar\nSH_OBJCOPY = fake-objcopy\n"
            "SH_CFLAGS = -m2 -mb -DC_ONLY=1 -save-temps=obj\n"
            "SH_CXXFLAGS = -m2 -mb -DCXX_ONLY=1 -save-temps=obj\n",
            encoding="utf-8",
        )
        (share / "build.post.iso-cue.mk").write_text(
            "SH_SRCS += $(SOURCEBOOT_TEST_CXX_SOURCE)\n"
            "SH_SRCS_UNIQ := $(sort $(SH_SRCS))\n"
            "SH_SRCS_C := $(filter %.c,$(SH_SRCS_UNIQ))\n"
            "SH_SRCS_CXX := $(filter %.cxx,$(SH_SRCS_UNIQ)) "
            "$(filter %.cpp,$(SH_SRCS_UNIQ)) $(filter %.cc,$(SH_SRCS_UNIQ)) "
            "$(filter %.C,$(SH_SRCS_UNIQ))\n"
            "SH_SRCS_S := $(filter %.sx,$(SH_SRCS_UNIQ))\n"
            "ifneq ($(strip $(SH_SRCS_CXX)),)\n"
            "SH_CXX_SPECS := yaul-main-c++.specs\n"
            "endif\n"
            "SH_OBJS_UNIQ := $(addsuffix .o,$(basename $(SH_SRCS_UNIQ)))\n"
            "SH_DEPS := $(SH_OBJS_UNIQ:.o=.d)\n",
            encoding="utf-8",
        )
        yaul_post = (
            ROOT / "third_party/libyaul/libyaul/build/build.post.bin.mk"
        ).read_text(encoding="utf-8")

        def real_recipe(macro: str, marker: str) -> str:
            block = yaul_post.split(f"define {macro}\n", 1)[1].split("\nendef", 1)[0]
            recipe = next(
                line for line in block.splitlines()
                if line.startswith("\t$(ECHO)")
            ).removeprefix("\t$(ECHO)")
            recipe = recipe.replace("$2", "$(call sourceboot-test-object,$(1))")
            recipe = recipe.replace("$1", "$(1)")
            return f"\t@echo {marker} {recipe}\n"

        with (share / "build.post.iso-cue.mk").open("a", encoding="utf-8") as post:
            post.write(
                "sourceboot-test-key = $(subst :,_,$(subst \\\\,_,$(subst /,_,$(1))))\n"
                "sourceboot-test-object = $(SH_BUILD_PATH)/parity/$(call sourceboot-test-key,$(1)).o\n"
                "define sourceboot-test-c-rule\n"
                ".PHONY: $(call sourceboot-test-object,$(1))\n"
                "$(call sourceboot-test-object,$(1)):\n"
                f"{real_recipe('macro-generate-sh-build-object', 'SOURCEBOOT_REAL_C')}"
                "endef\n"
                "define sourceboot-test-cxx-rule\n"
                ".PHONY: $(call sourceboot-test-object,$(1))\n"
                "$(call sourceboot-test-object,$(1)):\n"
                f"{real_recipe('macro-generate-sh-build-c++-object', 'SOURCEBOOT_REAL_CXX')}"
                "endef\n"
                "define sourceboot-test-sx-rule\n"
                ".PHONY: $(call sourceboot-test-object,$(1))\n"
                "$(call sourceboot-test-object,$(1)):\n"
                f"{real_recipe('macro-generate-sh-build-asm-object', 'SOURCEBOOT_REAL_SX')}"
                "endef\n"
                "$(foreach src,$(SH_SRCS_C),$(eval $(call sourceboot-test-c-rule,$(src))))\n"
                "$(foreach src,$(SH_SRCS_CXX),$(eval $(call sourceboot-test-cxx-rule,$(src))))\n"
                "$(foreach src,$(SH_SRCS_S),$(eval $(call sourceboot-test-sx-rule,$(src))))\n"
                "SOURCEBOOT_TEST_REAL_OBJECTS := $(foreach src,$(SH_SRCS_C) $(SH_SRCS_CXX) "
                "$(SH_SRCS_S),$(call sourceboot-test-object,$(src)))\n"
                ".PHONY: sourceboot-test-real-compile-model\n"
                "sourceboot-test-real-compile-model: $(SOURCEBOOT_TEST_REAL_OBJECTS)\n"
                "sourceboot-test-post-object = $(SH_BUILD_PATH)/parity/"
                "$(call sourceboot-test-key,$(1)).postlink\n"
                "define sourceboot-test-postlink-rule\n"
                ".PHONY: $(call sourceboot-test-post-object,$(1))\n"
                "$(call sourceboot-test-post-object,$(1)):\n"
                "\t$$(call sourceboot-discover-sx-dependency,$(1),"
                "postlink-parity-$(call sourceboot-test-key,$(1)).d)\n"
                "endef\n"
                "$(foreach src,$(SH_SRCS_S),$(eval $(call sourceboot-test-postlink-rule,$(src))))\n"
                "SOURCEBOOT_TEST_POSTLINK_OBJECTS := $(foreach src,$(SH_SRCS_S),"
                "$(call sourceboot-test-post-object,$(src)))\n"
                ".PHONY: sourceboot-test-postlink-sx-model\n"
                "sourceboot-test-postlink-sx-model: $(SOURCEBOOT_TEST_POSTLINK_OBJECTS)\n"
                "identity-discovery: sourceboot-test-real-compile-model "
                "sourceboot-test-postlink-sx-model\n"
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

    def run_outer_make(self, *variables: str) -> subprocess.CompletedProcess[str]:
        environment = os.environ.copy()
        environment.update({
            "YAUL_INSTALL_ROOT": self.yaul.as_posix(),
            "YAUL_PROG_SH_PREFIX": "sh-elf",
            "YAUL_ARCH_SH_PREFIX": "sh-elf",
        })
        return subprocess.run(
            ["make", "-n", "--no-print-directory", "-f", "Makefile.saturn.mk",
             "MAKE=echo", *variables, "sourceboot"],
            cwd=ROOT, check=False, capture_output=True, text=True, env=environment,
        )

    def assert_stage_rejected(self, stage: str, goal: str) -> None:
        result = self.run_make(stage, goal)
        self.assertNotEqual(result.returncode, 0, result.stdout)
        self.assertIn(f"SOURCEBOOT_BUILD_IDENTITY_STAGE={stage}", result.stderr)

    @staticmethod
    def normalized_preprocessor_argv(command: str, *, discovery: bool) -> list[str]:
        tokens = shlex.split(command)
        normalized: list[str] = []
        skip_argument = False
        switches_with_arguments = {"-MT", "-MF", "-o"}
        switches_without_arguments = {"-MM", "-MG"} if discovery else {"-MD", "-c"}
        for token in tokens:
            if skip_argument:
                skip_argument = False
                continue
            if token in switches_with_arguments:
                skip_argument = True
                continue
            if token in switches_without_arguments or token == "-save-temps=obj":
                continue
            normalized.append(token.replace("\\", "/"))
        return normalized

    @staticmethod
    def commands_by_source(output: str, marker: str) -> dict[str, str]:
        commands: dict[str, str] = {}
        for line in output.splitlines():
            if marker not in line:
                continue
            command = line.partition(marker)[2].strip()
            source = shlex.split(command)[-1].replace("\\", "/")
            if source in commands:
                raise AssertionError(f"duplicate {marker.strip()} command for {source}")
            commands[source] = command
        return commands

    @staticmethod
    def discovery_commands_by_source(output: str) -> dict[str, str]:
        commands: dict[str, str] = {}
        for line in output.splitlines():
            if " -MM -MG " not in line or "postlink-parity-" in line:
                continue
            source = shlex.split(line)[-1].replace("\\", "/")
            if source in commands:
                raise AssertionError(f"duplicate discovery command for {source}")
            commands[source] = line
        return commands

    @staticmethod
    def postlink_commands_by_source(output: str) -> dict[str, str]:
        commands: dict[str, str] = {}
        for line in output.splitlines():
            if " -MM -MG " not in line or "postlink-parity-" not in line:
                continue
            source = shlex.split(line)[-1].replace("\\", "/")
            if source in commands:
                raise AssertionError(f"duplicate post-link scan command for {source}")
            commands[source] = line
        return commands

    @staticmethod
    def host_path(rendered: str) -> Path:
        normalized = rendered.replace("\\", "/")
        if len(normalized) > 2 and normalized[0] == "/" and normalized[2] == "/":
            normalized = f"{normalized[1]}:{normalized[2:]}"
        return Path(normalized)

    def test_outer_build_orders_assets_discovery_seal_build_and_verify(self) -> None:
        result = self.run_outer_make(
            "SOURCEBOOT_RELEASE_MODE=release",
            "SOURCEBOOT_TARGET_PROFILE=custom-bob-profile.json",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for token in ("verify-sealed-inputs", "seal-release"):
            self.assertIn(token, result.stdout)
        positions = [result.stdout.index(token) for token in (
            "SOURCEBOOT_BUILD_IDENTITY_STAGE=assets identity-assets",
            "SOURCEBOOT_BUILD_IDENTITY_STAGE=discover identity-discovery",
            "print-identity-tag",
            "SOURCEBOOT_SEALED_IDENTITY=\"$tag\"",
            "verify-sealed-inputs",
            "seal-release",
        )]
        self.assertEqual(positions, sorted(positions))
        self.assertGreaterEqual(result.stdout.count("SOURCEBOOT_RELEASE_MODE=\"release\""), 5)
        self.assertGreaterEqual(result.stdout.count("SOURCEBOOT_TARGET_PROFILE=\"custom-bob-profile.json\""), 5)

    def test_identity_assets_extract_allowed_baserom_inputs_before_consumers(self) -> None:
        result = self.run_make("assets", "identity-assets")
        self.assertEqual(result.returncode, 0, result.stderr)
        output = result.stdout.replace("\\", "/")
        extraction = 'extract_assets.py'
        first_consumer = 'compile-bob-bsp-fragments'
        self.assertIn(extraction, output)
        self.assertIn('--output-root', output)
        self.assertIn('--path-list', output)
        self.assertIn(first_consumer, output)
        self.assertLess(output.index(extraction), output.index(first_consumer))

        makefile = self.sourceboot_makefile().replace("\\", "/")
        self.assertIn(".PHONY: source-extracted-assets", makefile)
        self.assertIn("source-assets: source-extracted-assets", makefile)
        self.assertGreaterEqual(makefile.count("| source-extracted-assets"), 3)
        outer = (ROOT / "Makefile.saturn.mk").read_text(encoding="utf-8")
        self.assertEqual(outer.count('--asset-root "$(BOB_ASSET_ROOT)"'), 2)
        self.assertEqual(
            makefile.count('BOB_ASSET_ROOT="$(SOURCEBOOT_EXTRACTED_ASSET_ROOT)"'),
            3,
        )

    def test_seal_and_post_link_verification_consume_exact_manifests(self) -> None:
        makefile = self.sourceboot_makefile()
        self.assertIn("--source-closure", makefile)
        self.assertIn("--toolchain-attestation", makefile)
        self.assertIn("verify-sealed-inputs", makefile)
        self.assertIn("SOURCEBOOT_C_AND_CXX_DEPS", makefile)
        self.assertIn("SOURCEBOOT_POSTLINK_SX_DEPS", makefile)
        self.assertIn("$(call sourceboot-discover-sx-dependency", makefile)
        self.assertIn("--actual-depfile", makefile)
        self.assertIn("--assembly-scan-depfile", makefile)
        self.assertIn('--expected-external "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"', makefile)
        self.assertIn('--mode "$(SOURCEBOOT_RELEASE_MODE)"', makefile)
        self.assertIn('--verify "$(SOURCEBOOT_TOOLCHAIN_ATTESTATION)"', makefile)
        self.assertIn('--external-dependencies "$(SOURCEBOOT_EXTERNAL_DEPENDENCIES)"', makefile)
        self.assertIn("release_manifest.py", makefile)
        self.assertIn("seal-release", makefile)
        self.assertIn("verify-release", makefile)

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
            "--recipe-input-list", "--generator-input-list",
            "--generated-input-list", "--derived-output-list",
        ):
            self.assertIn(token, output)
        makefile = self.sourceboot_makefile().replace("\\", "/")
        for token in (
            "sourceboot.specs", "sourceboot-cart.x", "build.pre.mk",
            "bootstrap_sourceboot_identity_spec.py",
            "saturn_geo_depth_manifest.ld", "saturn_build_identity_spec.json",
            "saturn-source-closure-v2.json", "saturn-toolchain-attestation-v1.json",
            "saturn-external-dependencies-v1.json", "SOURCE.DAT", ".elf", ".map", ".sym", ".asm",
        ):
            self.assertIn(token, makefile)

    def test_dry_run_c_and_sx_inventory_and_normalized_argv_match_real_compile(self) -> None:
        result = self.run_make("discover", "identity-discovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        discovery = self.discovery_commands_by_source(result.stdout)
        real_c = self.commands_by_source(result.stdout, "SOURCEBOOT_REAL_C ")
        real_sx = self.commands_by_source(result.stdout, "SOURCEBOOT_REAL_SX ")
        postlink_sx = self.postlink_commands_by_source(result.stdout)
        real = {**real_c, **real_sx}
        self.assertEqual(set(discovery), set(real))
        self.assertEqual(set(postlink_sx), set(real_sx))
        self.assertGreater(len(real_c), 1)
        self.assertGreater(len(real_sx), 1)
        for source, command in real.items():
            self.assertEqual(
                self.normalized_preprocessor_argv(discovery[source], discovery=True),
                self.normalized_preprocessor_argv(command, discovery=False),
                source,
            )
        for source, command in real_sx.items():
            self.assertEqual(
                self.normalized_preprocessor_argv(postlink_sx[source], discovery=True),
                self.normalized_preprocessor_argv(command, discovery=False),
                f"post-link {source}",
            )
        scan_end = max(result.stdout.index(command) for command in discovery.values())
        closure = result.stdout.index("gen_source_closure.py\" build")
        attestation = result.stdout.index("gen_toolchain_attestation.py\"")
        publication = result.stdout.index("mv -f")
        self.assertLess(scan_end, closure)
        self.assertLess(closure, attestation)
        self.assertLess(attestation, publication)

    def test_closure_handoff_uses_bounded_path_list_argv(self) -> None:
        result = self.run_make("discover", "identity-discovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("--compiled-source-list", result.stdout)
        self.assertIn("--depfile-list", result.stdout)
        self.assertIn("--derived-output-list", result.stdout)
        self.assertNotIn("--compiled-source \"", result.stdout)
        self.assertNotIn("--depfile \"", result.stdout)

    def test_second_discovery_rescans_cached_depfile_after_flag_drift(self) -> None:
        dep_root = Path(self.temporary.name) / "discovery-deps"
        dep_root_arg = f"SOURCEBOOT_DISCOVERY_DEPS={dep_root.as_posix()}"
        first = self.run_make("discover", "identity-discovery", dep_root_arg)
        self.assertEqual(first.returncode, 0, first.stderr)
        main_command = self.discovery_commands_by_source(first.stdout)["main.c"]
        main_tokens = shlex.split(main_command)
        depfile = self.host_path(main_tokens[main_tokens.index("-MF") + 1])
        depfile.parent.mkdir(parents=True, exist_ok=True)
        depfile.write_text("main.o: main.c\n", encoding="utf-8")

        second = self.run_make(
            "discover", "identity-discovery", dep_root_arg, "SATURN_DIAGNOSTIC_MODE=1",
        )
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertIn("main.c", self.discovery_commands_by_source(second.stdout))

    def test_dry_run_cxx_inventory_and_normalized_argv_match_real_compile(self) -> None:
        cxx_source = (ROOT / "src/pc/gfx/gfx_dxgi.cpp").as_posix()
        result = self.run_make(
            "discover", "identity-discovery", f"SOURCEBOOT_TEST_CXX_SOURCE={cxx_source}",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        discovery = self.discovery_commands_by_source(result.stdout)
        real_c = self.commands_by_source(result.stdout, "SOURCEBOOT_REAL_C ")
        real_cxx = self.commands_by_source(result.stdout, "SOURCEBOOT_REAL_CXX ")
        real_sx = self.commands_by_source(result.stdout, "SOURCEBOOT_REAL_SX ")
        real = {**real_c, **real_cxx, **real_sx}
        self.assertEqual(set(discovery), set(real))
        self.assertEqual(len(real_cxx), 1)
        source, cxx_command = next(iter(real_cxx.items()))
        self.assertTrue(source.endswith("/src/pc/gfx/gfx_dxgi.cpp"), source)
        for source, command in real.items():
            self.assertEqual(
                self.normalized_preprocessor_argv(discovery[source], discovery=True),
                self.normalized_preprocessor_argv(command, discovery=False),
                source,
            )
        cxx_tokens = self.normalized_preprocessor_argv(cxx_command, discovery=False)
        self.assertEqual(cxx_tokens[0], "fake-sh-g++")
        self.assertIn("-DCXX_ONLY=1", cxx_tokens)
        self.assertIn("-specs=sourceboot.specs", cxx_tokens)
        self.assertIn("-specs=yaul-main-c++.specs", cxx_tokens)
        self.assertEqual(sum(token.startswith("-ffile-prefix-map=") for token in cxx_tokens), 1)
        self.assertEqual(sum(token.startswith("-fdebug-prefix-map=") for token in cxx_tokens), 1)
        self.assertEqual(sum(token.startswith("-fmacro-prefix-map=") for token in cxx_tokens), 1)


if __name__ == "__main__":
    unittest.main()
