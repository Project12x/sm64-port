#!/usr/bin/env python3
"""Source policy checks for the Saturn IR-owned render path."""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
AREA_C = REPO_ROOT / "src" / "game" / "area.c"
SOURCEBOOT_C = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"
SOURCEBOOT_MAKEFILE = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "Makefile"
SOURCEBOOT_DIR = SOURCEBOOT_MAKEFILE.parent


def extract_c_function(path: Path, name: str) -> str:
    text = path.read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise AssertionError(f"function {name} not found in {path}")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.end() : index]
    raise AssertionError(f"function {name} is not terminated in {path}")


def extract_if_block(body: str, condition: str) -> str:
    match = re.search(rf"if\s*\(\s*{re.escape(condition)}\s*\)\s*\{{", body)
    if match is None:
        raise AssertionError(f"guard if ({condition}) not found")
    depth = 0
    for index in range(match.end() - 1, len(body)):
        if body[index] == "{":
            depth += 1
        elif body[index] == "}":
            depth -= 1
            if depth == 0:
                return body[match.end() : index]
    raise AssertionError(f"guard if ({condition}) is not terminated")


def extract_preprocessor_partition(
    body: str, condition: str
) -> tuple[str, str, str, str]:
    directives = list(
        re.finditer(
            r"(?m)^[ \t]*#(if|ifdef|ifndef|else|elif|endif)\b([^\r\n]*)",
            body,
        )
    )
    opening_index = next(
        (
            index
            for index, directive in enumerate(directives)
            if directive.group(1) == "if"
            and directive.group(2).strip() == condition
        ),
        None,
    )
    if opening_index is None:
        raise AssertionError(f"#if {condition} not found")

    opening = directives[opening_index]
    branch_start = opening.end()
    else_directive = None
    depth = 1
    for directive in directives[opening_index + 1 :]:
        kind = directive.group(1)
        if kind in ("if", "ifdef", "ifndef"):
            depth += 1
        elif kind == "endif":
            depth -= 1
            if depth == 0:
                if else_directive is None:
                    raise AssertionError(f"#if {condition} has no #else branch")
                return (
                    body[: opening.start()],
                    body[branch_start : else_directive.start()],
                    body[else_directive.end() : directive.start()],
                    body[directive.end() :],
                )
        elif depth == 1 and kind == "else":
            if else_directive is not None:
                raise AssertionError(f"#if {condition} has multiple #else branches")
            else_directive = directive
        elif depth == 1 and kind == "elif":
            raise AssertionError(f"#if {condition} uses #elif instead of #else")
    raise AssertionError(f"#if {condition} is not terminated")


def msys_path_to_windows(value: str) -> Path:
    match = re.fullmatch(r"/([A-Za-z])/(.*)", value)
    if match is not None:
        return Path(f"{match.group(1)}:/{match.group(2)}")
    return Path(value)


def local_yaul_install_root() -> Path:
    configured = os.environ.get("YAUL_INSTALL_ROOT")
    if configured:
        return msys_path_to_windows(configured)

    for directory in (REPO_ROOT, *REPO_ROOT.parents):
        env_file = directory / ".yaul.env"
        if not env_file.is_file():
            continue
        match = re.search(
            r"(?m)^export YAUL_INSTALL_ROOT=([^\r\n#]+)",
            env_file.read_text(encoding="utf-8"),
        )
        if match is not None:
            return msys_path_to_windows(match.group(1).strip().strip('"'))
    raise AssertionError("YAUL_INSTALL_ROOT is unset and no parent .yaul.env exists")


def local_msys_make() -> Path:
    configured = os.environ.get("SATURN_MSYS_MAKE")
    if configured:
        return Path(configured)
    discovered = shutil.which("make")
    if discovered:
        return Path(discovered)
    msys_root = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
    return msys_root / "usr" / "bin" / "make.exe"


def yaul_environment(yaul_install_root: Path, msys_make: Path) -> dict[str, str]:
    environment = os.environ.copy()
    environment["YAUL_INSTALL_ROOT"] = str(yaul_install_root)
    environment["YAUL_PROG_SH_PREFIX"] = "sh-elf"
    environment["YAUL_ARCH_SH_PREFIX"] = "sh-elf"
    environment["YAUL_ARCH_M68K_PREFIX"] = "m68keb-elf"
    environment["YAUL_BUILD_ROOT"] = str(REPO_ROOT / "build" / "saturn" / "yaul")
    environment["YAUL_BUILD"] = "release"
    environment["YAUL_OPTION_MALLOC_IMPL"] = "tlsf"
    environment["DEBUG_RELEASE"] = "1"
    environment["PATH"] = (
        f"{msys_make.parent};{yaul_install_root}\\bin;"
        + environment.get("PATH", "")
    )
    return environment


def sourceboot_make(*assignments: str) -> subprocess.CompletedProcess[str]:
    msys_make = local_msys_make()
    yaul_install_root = local_yaul_install_root()
    return subprocess.run(
        [str(msys_make), "-C", str(SOURCEBOOT_DIR), "-pn", *assignments],
        capture_output=True,
        text=True,
        env=yaul_environment(yaul_install_root, msys_make),
    )


def make_value(output: str, name: str) -> str:
    match = re.search(
        rf"(?m)^{re.escape(name)}\s*(?::|\?|\+)?=\s*(.*)$",
        output,
    )
    if match is None:
        raise AssertionError(f"Make did not report {name}")
    return match.group(1)


def strip_c_comments(text: str) -> str:
    return re.sub(r"/\*.*?\*/|//[^\r\n]*", "", text, flags=re.S)


def extract_c_macro_default(path: Path, name: str) -> str:
    source = strip_c_comments(path.read_text(encoding="utf-8"))
    match = re.search(
        rf"(?m)^\s*#ifndef\s+{re.escape(name)}\s*$\s*"
        rf"^\s*#define\s+{re.escape(name)}\s+(\S+)\s*$\s*"
        r"^\s*#endif\s*$",
        source,
    )
    if match is None:
        raise AssertionError(f"guarded default for {name} not found in {path}")
    return match.group(1)


class SourceRenderSuppressionTests(unittest.TestCase):
    def test_dormant_policy_guard_keeps_known_outside_state_calls(self) -> None:
        """Keep the dormant guard structurally separated from known state calls."""
        body = extract_c_function(AREA_C, "render_game")
        self.assertIn("sm64_saturn_source_runtime_scene_graph_suppressed", body)
        guarded = extract_if_block(body, "!scene_graph_suppressed")
        self.assertIn("geo_process_root(", guarded)
        for call in (
            "do_cutscene_handler(",
            "print_displaying_credits_entry(",
            "render_menus_and_dialogs(",
            "render_screen_transition(",
        ):
            self.assertIn(call, body)
            self.assertNotIn(call, guarded)

    def test_make_configuration_defaults_off_and_keys_only_diag_output(self) -> None:
        """The real Make parse defaults off and keys the diagnostic artifact."""
        self.assertEqual(
            extract_c_macro_default(
                SOURCEBOOT_C, "SATURN_EXPERIMENTAL_SKIP_GEO_WALK"
            ),
            "0",
        )
        default = sourceboot_make()
        self.assertEqual(default.returncode, 0, default.stderr)
        self.assertEqual(
            make_value(default.stdout, "SATURN_EXPERIMENTAL_SKIP_GEO_WALK"),
            "0",
        )

        sealed_baseline = sourceboot_make(
            "SATURN_EXPERIMENTAL_SKIP_GEO_WALK=0",
            "SATURN_DEMO_PATH=1",
            "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
        )
        self.assertEqual(sealed_baseline.returncode, 0, sealed_baseline.stderr)
        baseline_output = make_value(sealed_baseline.stdout, "SH_OUTPUT_DIR")
        self.assertNotIn("diag-skip-geo", baseline_output)

        diagnostic = sourceboot_make(
            "SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1",
            "SATURN_DEMO_PATH=1",
            "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
        )
        self.assertEqual(diagnostic.returncode, 0, diagnostic.stderr)
        self.assertIn(
            "-DSATURN_EXPERIMENTAL_SKIP_GEO_WALK=1",
            make_value(diagnostic.stdout, "SH_CFLAGS"),
        )
        diagnostic_output = make_value(diagnostic.stdout, "SH_OUTPUT_DIR")
        self.assertEqual(diagnostic_output.count("diag-skip-geo"), 1)
        self.assertEqual(
            diagnostic_output.replace("-diag-skip-geo", ""),
            baseline_output,
        )

    def test_make_configuration_rejects_unsealed_diagnostic_values(self) -> None:
        """The real Make parse rejects non-binary and incompletely sealed modes."""
        cases = (
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=2",), "must be 0 or 1"),
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=",), "must be 0 or 1"),
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=01",), "must be 0 or 1"),
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1x",), "must be 0 or 1"),
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1 0",), "must be 0 or 1"),
            (("SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1",), "requires SATURN_DEMO_PATH=1"),
            ((
                "SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1",
                "SATURN_DEMO_PATH=1",
            ), "requires SATURN_DEMO_PATH=1"),
            ((
                "SATURN_EXPERIMENTAL_SKIP_GEO_WALK=1",
                "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
            ), "requires SATURN_DEMO_PATH=1"),
        )
        for assignments, message in cases:
            with self.subTest(assignments=assignments):
                rejected = sourceboot_make(*assignments)
                self.assertEqual(rejected.returncode, 2, rejected.stderr)
                self.assertIn(message, rejected.stderr)

        for padded in ("1 ", "1\t"):
            with self.subTest(padded=repr(padded)):
                rejected = sourceboot_make(
                    f"SATURN_EXPERIMENTAL_SKIP_GEO_WALK={padded}",
                    "SATURN_DEMO_PATH=1",
                    "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
                )
                self.assertEqual(rejected.returncode, 2, rejected.stderr)
                self.assertIn("must be 0 or 1", rejected.stderr)

        # GNU Make removes leading assignment whitespace before the Makefile
        # can observe it. It is safe only because the canonical 1 still runs
        # the demo/replay prerequisite checks and drives every later decision.
        unsealed_canonicalized = sourceboot_make(
            "SATURN_EXPERIMENTAL_SKIP_GEO_WALK= 1"
        )
        self.assertEqual(
            unsealed_canonicalized.returncode,
            2,
            unsealed_canonicalized.stderr,
        )
        self.assertIn(
            "requires SATURN_DEMO_PATH=1",
            unsealed_canonicalized.stderr,
        )
        canonicalized = sourceboot_make(
            "SATURN_EXPERIMENTAL_SKIP_GEO_WALK= 1",
            "SATURN_DEMO_PATH=1",
            "SATURN_SOURCEBOOT_ROUTE_REPLAY=1",
        )
        self.assertEqual(canonicalized.returncode, 0, canonicalized.stderr)
        self.assertEqual(
            make_value(
                canonicalized.stdout, "SATURN_EXPERIMENTAL_SKIP_GEO_WALK"
            ),
            "1",
        )
        self.assertEqual(
            make_value(canonicalized.stdout, "SH_OUTPUT_DIR").count(
                "diag-skip-geo"
            ),
            1,
        )

    def test_skip_geo_diagnostic_scopes_exactly_one_loop_in_each_branch(self) -> None:
        """The diagnostic branch is paired and the actual #else stays setter-free."""
        body = extract_c_function(SOURCEBOOT_C, "sourceboot_run_source_tick")
        prefix, branch, normal, suffix = extract_preprocessor_partition(
            body, "SATURN_EXPERIMENTAL_SKIP_GEO_WALK"
        )
        prefix = strip_c_comments(prefix)
        branch = strip_c_comments(branch)
        normal = strip_c_comments(normal)
        suffix = strip_c_comments(suffix)
        setter_call = re.compile(
            r"\bsm64_saturn_source_runtime_set_scene_graph_suppressed\s*\("
        )
        self.assertEqual(len(setter_call.findall(branch)), 2)
        self.assertEqual(branch.count("game_loop_one_iteration("), 1)
        self.assertRegex(branch, re.compile(r"^\s*sm64_saturn_source_runtime_set_scene_graph_suppressed\s*\(\s*true\s*\);\s*game_loop_one_iteration\s*\(\s*\);\s*sm64_saturn_source_runtime_set_scene_graph_suppressed\s*\(\s*false\s*\);\s*$", re.S))
        self.assertIsNone(setter_call.search(normal))
        self.assertEqual(normal.count("game_loop_one_iteration("), 1)
        self.assertRegex(normal, r"^\s*game_loop_one_iteration\(\);\s*$")
        self.assertIsNone(setter_call.search(prefix + normal + suffix))


if __name__ == "__main__":
    unittest.main()
