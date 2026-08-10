#!/usr/bin/env python3
"""Contracts for compiler-derived Saturn source closure sealing."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


TOOLS_DIR = Path(__file__).resolve().parent
if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))

from gen_source_closure import (
    build_source_closure,
    load_path_list,
    main,
    parse_make_depfile,
    verify_source_closure,
)


class SourceClosureTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name) / "repo"
        self.root.mkdir()
        self.write("src/main.c", '#include "main.h"\n')
        self.write("include/main.h", "#define MAIN 1\n")
        self.write("tools/saturn/gen_build_identity.py", "generator\n")
        self.write("Makefile.saturn.mk", "recipe\n")
        self.write("build/generated/scene.h", "generated\n")
        self.write("build/generated/saturn_build_identity_values.inc", "derived\n")
        self.write("obj/main.d", "obj/main.o: src/main.c include/main.h build/generated/scene.h \\\n tools/saturn/gen_build_identity.py Makefile.saturn.mk\n")
        self.write("obj/scan.d", "obj/main.sx.o: src/main.c include/main.h\n")
        self.depfiles = (self.root / "obj/main.d",)
        self.asm_depfiles = (self.root / "obj/scan.d",)
        self.derived = (self.root / "build/generated/saturn_build_identity_values.inc",)
        self.external_roots: tuple[Path, ...] = ()
        self.expected_external: tuple[Path, ...] = ()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def write(self, relative: str, contents: str) -> Path:
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")
        return path

    def build_closure(self, generated_inputs: tuple[Path, ...] | None = None):
        if generated_inputs is None:
            generated_inputs = (self.root / "build/generated/scene.h",)
        return build_source_closure(
            self.root,
            (self.root / "src/main.c",),
            self.depfiles,
            (self.root / "Makefile.saturn.mk",),
            (self.root / "tools/saturn/gen_build_identity.py",),
            generated_inputs,
            self.derived,
            self.external_roots,
        )

    def write_sealed_closure(self) -> Path:
        built = self.build_closure()
        sealed = self.root / "build/sealed-source-closure.json"
        sealed.parent.mkdir(parents=True, exist_ok=True)
        sealed.write_bytes(built.canonical)
        return sealed

    def test_depfile_parser_handles_continuations_and_escaped_spaces(self) -> None:
        self.assertEqual(
            parse_make_depfile("obj.o: src/main.c include/a.h \\\n include/with\\ space.h\n"),
            ("src/main.c", "include/a.h", "include/with space.h"),
        )

    def test_depfile_parser_rejects_multiple_targets_and_malformed_syntax(self) -> None:
        for contents in ("a.o b.o: src/main.c\n", "a.o src/main.c\n", "a.o: src/main.c\\\n"):
            with self.subTest(contents=contents):
                with self.assertRaisesRegex(ValueError, "depfile"):
                    parse_make_depfile(contents)

    def test_closure_excludes_derived_identity_outputs_but_includes_generators(self) -> None:
        built = self.build_closure()
        records = {row["path"]: row["class"] for row in built.document["inputs"]}
        self.assertNotIn("build/generated/saturn_build_identity_values.inc", records)
        self.assertEqual(records["tools/saturn/gen_build_identity.py"], "generator")
        self.assertEqual(records["src/main.c"], "compiled-source")
        self.assertEqual(records["include/main.h"], "header")
        self.assertEqual(records["build/generated/scene.h"], "generated-input")
        self.assertEqual(built.document["schema"], "sm64-saturn-source-closure-v2")
        self.assertEqual(built.canonical, json.dumps(
            built.document, sort_keys=True, separators=(",", ":"), ensure_ascii=True
        ).encode("ascii") + b"\n")

    def test_explicit_duplicate_and_case_colliding_records_fail_closed(self) -> None:
        for sources, recipes, message in (
            ((self.root / "src/main.c", self.root / "src/main.c"), (), "duplicate"),
            ((self.root / "src/main.c",), (self.root / "src/main.c",), "explicit"),
        ):
            with self.subTest(message=message):
                with self.assertRaisesRegex(ValueError, message):
                    build_source_closure(self.root, sources, self.depfiles, recipes, (), (), (), ())
        if (self.root / "include/MAIN.h").is_file():
            self.skipTest("host filesystem does not permit case-colliding files")
        self.write("include/MAIN.h", "case collision\n")
        self.write("obj/case.d", "obj/case.o: include/main.h include/MAIN.h\n")
        with self.assertRaisesRegex(ValueError, "case-colliding"):
            build_source_closure(self.root, (), (self.root / "obj/case.d",), (), (), (), (), ())

    def test_missing_depfile_and_repo_escape_fail_closed(self) -> None:
        with self.assertRaisesRegex(ValueError, "depfile.*not a file"):
            build_source_closure(self.root, (), (self.root / "obj/missing.d",), (), (), (), (), ())
        escaped = self.root.parent / "escape.h"
        escaped.write_text("escape\n", encoding="utf-8")
        self.write("obj/escape.d", f"obj/escape.o: {escaped.as_posix()}\n")
        with self.assertRaisesRegex(ValueError, "unclassified external"):
            build_source_closure(self.root, (), (self.root / "obj/escape.d",), (), (), (), (), ())

    def test_generated_input_precedence_wins_over_compiler_discovery(self) -> None:
        built = self.build_closure()
        rows = {row["path"]: row for row in built.document["inputs"]}
        self.assertEqual(rows["build/generated/scene.h"]["class"], "generated-input")
        self.assertIn("compiler", rows["build/generated/scene.h"]["owners"])

    def test_external_dependencies_are_returned_but_unclassified_ones_fail(self) -> None:
        external = self.root.parent / "toolchain"
        external.mkdir()
        header = external / "sdk.h"
        header.write_text("sdk\n", encoding="utf-8")
        self.write("obj/external.d", f"obj/external.o: src/main.c {header.as_posix()}\n")
        built = build_source_closure(
            self.root, (self.root / "src/main.c",), (self.root / "obj/external.d",),
            (), (), (), (), (external,),
        )
        self.assertEqual(built.external_dependencies, (header.resolve(),))
        self.assertNotIn(str(header.resolve()), json.dumps(built.document))

    def test_post_build_rejects_missing_extra_stale_and_toc_tou_inputs(self) -> None:
        sealed = self.write_sealed_closure()
        mutations = (
            ("missing", lambda: self.write("obj/main.d", "obj/main.o: src/main.c include/main.h\n"), "closure"),
            ("extra", lambda: (self.write("include/extra.h", "extra\n"), self.write("obj/main.d", "obj/main.o: src/main.c include/main.h Makefile.saturn.mk tools/saturn/gen_build_identity.py build/generated/scene.h include/extra.h\n")), "closure"),
            ("stale-removed", lambda: (self.root / "include/main.h").unlink(), "not a file"),
            ("toc-tou", lambda: self.write("include/main.h", "changed after seal\n"), "changed after discovery"),
        )
        for name, mutation, message in mutations:
            with self.subTest(mutation=name):
                self.write("include/main.h", "#define MAIN 1\n")
                self.write("obj/main.d", "obj/main.o: src/main.c include/main.h build/generated/scene.h \\\n tools/saturn/gen_build_identity.py Makefile.saturn.mk\n")
                sealed = self.write_sealed_closure()
                mutation()
                with self.assertRaisesRegex(ValueError, message):
                    verify_source_closure(
                        self.root, sealed, self.depfiles, self.asm_depfiles,
                        self.derived, self.external_roots, self.expected_external,
                        release_mode=False,
                    )

    def git_init_with_tracked_closure(self) -> Path:
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        subprocess.run(["git", "add", "src", "include", "tools", "Makefile.saturn.mk", "obj"], cwd=self.root, check=True)
        subprocess.run(
            ["git", "-c", "user.email=test@example.invalid", "-c", "user.name=Test", "commit", "-qm", "baseline"],
            cwd=self.root, check=True,
        )
        sealed = self.write_sealed_closure()
        return sealed

    def test_release_mode_ignores_dirty_file_outside_closure(self) -> None:
        sealed = self.git_init_with_tracked_closure()
        self.write("docs/unrelated.md", "dirty\n")
        verify_source_closure(
            self.root, sealed, self.depfiles, (), self.derived,
            self.external_roots, self.expected_external, release_mode=True,
        )

    def test_release_mode_rejects_dirty_or_untracked_checked_in_closure_inputs(self) -> None:
        sealed = self.git_init_with_tracked_closure()
        self.write("include/main.h", "dirty\n")
        with self.assertRaisesRegex(ValueError, "release closure inputs are not clean"):
            verify_source_closure(self.root, sealed, self.depfiles, (), self.derived, (), (), True)

        self.write("include/untracked.h", "untracked\n")
        self.write("obj/main.d", "obj/main.o: src/main.c include/main.h include/untracked.h build/generated/scene.h tools/saturn/gen_build_identity.py Makefile.saturn.mk\n")
        sealed = self.write_sealed_closure()
        with self.assertRaisesRegex(ValueError, "release closure input is not tracked"):
            verify_source_closure(self.root, sealed, self.depfiles, (), self.derived, (), (), True)

    def test_release_mode_allows_clean_ignored_generated_input_without_git_status(self) -> None:
        sealed = self.git_init_with_tracked_closure()
        self.write(".gitignore", "build/\n")
        subprocess.run(["git", "add", ".gitignore"], cwd=self.root, check=True)
        subprocess.run(
            ["git", "-c", "user.email=test@example.invalid", "-c", "user.name=Test", "commit", "-qm", "ignore-build"],
            cwd=self.root, check=True,
        )
        verify_source_closure(self.root, sealed, self.depfiles, (), self.derived, (), (), True)

    def test_release_mode_rejects_ignored_untracked_header(self) -> None:
        self.git_init_with_tracked_closure()
        self.write(".gitignore", "include/ignored.h\n")
        subprocess.run(["git", "add", ".gitignore"], cwd=self.root, check=True)
        subprocess.run(
            ["git", "-c", "user.email=test@example.invalid", "-c", "user.name=Test", "commit", "-qm", "ignore-header"],
            cwd=self.root, check=True,
        )
        self.write("include/ignored.h", "ignored and untracked\n")
        self.write("obj/main.d", "obj/main.o: src/main.c include/main.h include/ignored.h build/generated/scene.h tools/saturn/gen_build_identity.py Makefile.saturn.mk\n")
        sealed = self.write_sealed_closure()
        with self.assertRaisesRegex(ValueError, "release closure input is not tracked"):
            verify_source_closure(self.root, sealed, self.depfiles, (), self.derived, (), (), True)

    def test_release_mode_git_checks_generated_input_outside_build(self) -> None:
        self.git_init_with_tracked_closure()
        self.write(".gitignore", "generated/\n")
        subprocess.run(["git", "add", ".gitignore"], cwd=self.root, check=True)
        subprocess.run(
            ["git", "-c", "user.email=test@example.invalid", "-c", "user.name=Test", "commit", "-qm", "ignore-generated"],
            cwd=self.root, check=True,
        )
        generated = self.write("generated/outside.h", "ignored generated input\n")
        built = self.build_closure((self.root / "build/generated/scene.h", generated))
        sealed = self.root / "build/sealed-source-closure.json"
        sealed.write_bytes(built.canonical)
        with self.assertRaisesRegex(ValueError, "release closure input is not tracked"):
            verify_source_closure(self.root, sealed, self.depfiles, (), self.derived, (), (), True)

    def test_depfile_case_spelling_normalizes_to_the_repository_path_when_supported(self) -> None:
        alternate = self.root / "include/MAIN.h"
        if not alternate.is_file():
            self.skipTest("host filesystem distinguishes case spellings")
        self.write("obj/main.d", "obj/main.o: src/main.c include/MAIN.h build/generated/scene.h tools/saturn/gen_build_identity.py Makefile.saturn.mk\n")
        records = {row["path"] for row in self.build_closure().document["inputs"]}
        self.assertIn("include/main.h", records)
        self.assertNotIn("include/MAIN.h", records)

    def test_cli_build_publishes_closure_and_sorted_external_handoff(self) -> None:
        external = self.root.parent / "toolchain"
        external.mkdir()
        header = external / "sdk.h"
        header.write_text("sdk\n", encoding="utf-8")
        self.write("obj/external.d", f"obj/main.o: src/main.c include/main.h {header.as_posix()}\n")
        closure = self.root / "build/closure.json"
        handoff = self.root / "build/external.json"

        self.assertEqual(main([
            "build", "--root", str(self.root), "--output", str(closure),
            "--external-output", str(handoff),
            "--compiled-source", "src/main.c", "--depfile", "obj/external.d",
            "--recipe-input", "Makefile.saturn.mk",
            "--generator-input", "tools/saturn/gen_build_identity.py",
            "--generated-input", "build/generated/scene.h",
            "--derived-output", "build/generated/saturn_build_identity_values.inc",
            "--external-root", str(external),
        ]), 0)
        self.assertEqual(json.loads(closure.read_text(encoding="utf-8"))["schema"],
                         "sm64-saturn-source-closure-v2")
        self.assertEqual(json.loads(handoff.read_text(encoding="utf-8")), {
            "schema": "sm64-saturn-external-dependencies-v1",
            "paths": [str(header.resolve())],
        })
        self.assertNotIn(str(header.resolve()), closure.read_text(encoding="utf-8"))

    def test_cli_build_accepts_canonical_path_lists_without_repeated_argv(self) -> None:
        def path_list(relative: str, values: tuple[str, ...]) -> Path:
            path = self.root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(
                ("sm64-saturn-path-list-v1\n" + "".join(
                    f"{value}\n" for value in values
                )).encode("utf-8")
            )
            return path

        closure = self.root / "build/closure-from-lists.json"
        handoff = self.root / "build/external-from-lists.json"
        lists = {
            "compiled-source": path_list("lists/compiled.txt", ("src/main.c",)),
            "depfile": path_list("lists/depfiles.txt", ("obj/main.d",)),
            "recipe-input": path_list("lists/recipes.txt", ("Makefile.saturn.mk",)),
            "generator-input": path_list(
                "lists/generators.txt", ("tools/saturn/gen_build_identity.py",)
            ),
            "generated-input": path_list(
                "lists/generated.txt", ("build/generated/scene.h",)
            ),
            "derived-output": path_list(
                "lists/derived.txt", ("build/generated/saturn_build_identity_values.inc",)
            ),
        }
        argv = [
            "build", "--root", str(self.root), "--output", str(closure),
            "--external-output", str(handoff),
        ]
        for option, path in lists.items():
            argv.extend((f"--{option}-list", str(path)))
        self.assertEqual(main(argv), 0)
        self.assertEqual(closure.read_bytes(), self.build_closure().canonical)

    def test_path_list_rejects_noncanonical_or_duplicate_rows(self) -> None:
        for name, contents, message in (
            ("header", "wrong\nobj/main.d\n", "schema"),
            ("order", "sm64-saturn-path-list-v1\nz.d\na.d\n", "sorted"),
            ("duplicate", "sm64-saturn-path-list-v1\na.d\na.d\n", "duplicate"),
            ("blank", "sm64-saturn-path-list-v1\na.d\n\n", "blank"),
            ("crlf", "sm64-saturn-path-list-v1\r\na.d\r\n", "canonical LF"),
        ):
            with self.subTest(name=name):
                path = self.root / f"lists/{name}.txt"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(contents.encode("utf-8"))
                with self.assertRaisesRegex(ValueError, message):
                    load_path_list(path)

    @unittest.skipUnless(os.name == "nt", "MSYS drive conversion is Windows-only")
    def test_path_list_converts_msys_drive_paths_for_windows_python(self) -> None:
        path = self.root / "lists/msys-drive.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"sm64-saturn-path-list-v1\n/d/repo/src/main.c\n")
        self.assertEqual(load_path_list(path), (Path("D:/repo/src/main.c"),))

    def test_cli_build_rejects_aliased_outputs_before_preserving_existing_bytes(self) -> None:
        shared = self.root / "build/shared.json"
        shared.parent.mkdir(parents=True, exist_ok=True)
        shared.write_bytes(b"prior output\n")

        with self.assertRaisesRegex(ValueError, "output paths must differ"):
            main([
                "build", "--root", str(self.root), "--output", str(shared),
                "--external-output", str(shared.parent / ".." / "build" / "shared.json"),
                "--compiled-source", "src/main.c", "--depfile", "obj/main.d",
                "--recipe-input", "Makefile.saturn.mk",
                "--generator-input", "tools/saturn/gen_build_identity.py",
                "--generated-input", "build/generated/scene.h",
                "--derived-output", "build/generated/saturn_build_identity_values.inc",
            ])

        self.assertEqual(shared.read_bytes(), b"prior output\n")

    def test_cli_verify_consumes_exact_handoff_and_release_mode(self) -> None:
        external = self.root.parent / "toolchain"
        external.mkdir()
        header = external / "sdk.h"
        header.write_text("sdk\n", encoding="utf-8")
        self.write(
            "obj/main.d",
            f"obj/main.o: src/main.c include/main.h build/generated/scene.h "
            f"tools/saturn/gen_build_identity.py Makefile.saturn.mk {header.as_posix()}\n",
        )
        self.external_roots = (external,)
        sealed = self.write_sealed_closure()
        handoff = self.root / "build/external.json"
        handoff.write_bytes(json.dumps({
            "schema": "sm64-saturn-external-dependencies-v1",
            "paths": [str(header.resolve())],
        }, sort_keys=True, separators=(",", ":")).encode("ascii") + b"\n")
        self.assertEqual(main([
            "verify", "--root", str(self.root), "--sealed", str(sealed),
            "--actual-depfile", "obj/main.d", "--assembly-scan-depfile", "obj/scan.d",
            "--derived-output", "build/generated/saturn_build_identity_values.inc",
            "--external-root", str(external), "--expected-external", str(handoff),
            "--mode", "development",
        ]), 0)

        handoff.write_bytes(json.dumps({
            "schema": "sm64-saturn-external-dependencies-v1", "paths": [],
        }, sort_keys=True, separators=(",", ":")).encode("ascii") + b"\n")
        with self.assertRaisesRegex(ValueError, "external dependency set differs"):
            main([
                "verify", "--root", str(self.root), "--sealed", str(sealed),
                "--actual-depfile", "obj/main.d", "--assembly-scan-depfile", "obj/scan.d",
                "--derived-output", "build/generated/saturn_build_identity_values.inc",
                "--external-root", str(external), "--expected-external", str(handoff),
                "--mode", "development",
            ])


if __name__ == "__main__":
    unittest.main()
