#!/usr/bin/env python3
"""Seal and verify the compiler-discovered Saturn source input closure."""

from __future__ import annotations

import argparse
import hashlib
import json
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

from hermetic_manifest import (
    canonical_json_bytes,
    normalize_repo_path,
    reject_case_collisions,
    sha256_file,
    write_if_changed,
)


CLASS_PRECEDENCE = {
    "header": 0,
    "compiled-source": 1,
    "linker/build-recipe": 2,
    "generator": 3,
    "generated-input": 4,
}

_EXPLICIT_CLASSES = tuple(name for name in CLASS_PRECEDENCE if name != "header")
EXTERNAL_DEPENDENCIES_SCHEMA = "sm64-saturn-external-dependencies-v1"


@dataclass(frozen=True)
class ClosureBuild:
    document: dict[str, Any]
    canonical: bytes
    sha256: str
    external_dependencies: tuple[Path, ...]


def parse_make_depfile(text: str) -> tuple[str, ...]:
    """Parse one strict GNU Make depfile rule and return its prerequisites."""
    logical: list[str] = []
    continued_at_end = False
    index = 0
    while index < len(text):
        character = text[index]
        if character == "\\" and index + 1 < len(text) and text[index + 1] == "\n":
            continued_at_end = index + 2 == len(text)
            index += 2
            continue
        if (character == "\\" and index + 2 < len(text) and text[index + 1] == "\r"
                and text[index + 2] == "\n"):
            continued_at_end = index + 3 == len(text)
            index += 3
            continue
        logical.append(character)
        index += 1
    rule = "".join(logical).strip()
    if continued_at_end or not rule or "\n" in rule or "\r" in rule:
        raise ValueError("malformed depfile: expected one continued rule")
    colon = _first_unescaped(rule, ":")
    if colon < 1:
        raise ValueError("malformed depfile: expected one target and one colon")
    targets = _make_words(rule[:colon])
    dependencies = _make_words(rule[colon + 1 :])
    if len(targets) != 1 or not targets or not dependencies:
        raise ValueError("malformed depfile: expected one target and dependencies")
    return tuple(dependencies)


def _first_unescaped(value: str, sought: str) -> int:
    escaped = False
    for index, character in enumerate(value):
        if escaped:
            escaped = False
        elif character == "\\":
            escaped = True
        elif character == sought:
            return index
    if escaped:
        raise ValueError("malformed depfile: trailing escape")
    return -1


def _make_words(value: str) -> list[str]:
    words: list[str] = []
    current: list[str] = []
    escaped = False
    for character in value:
        if escaped:
            current.append(character)
            escaped = False
        elif character == "\\":
            escaped = True
        elif character.isspace():
            if current:
                words.append("".join(current))
                current = []
        else:
            current.append(character)
    if escaped:
        raise ValueError("malformed depfile: trailing escape")
    if current:
        words.append("".join(current))
    return words


def build_source_closure(
    root: Path,
    compiled_sources: Sequence[Path],
    depfiles: Sequence[Path],
    recipe_inputs: Sequence[Path],
    generator_inputs: Sequence[Path],
    generated_inputs: Sequence[Path],
    derived_outputs: Sequence[Path],
    external_roots: Sequence[Path],
) -> ClosureBuild:
    """Build a deterministic source closure from explicit and compiler inputs."""
    root = root.resolve()
    derived = _normalized_derived_outputs(root, derived_outputs)
    explicit = _explicit_classes(
        root, derived, compiled_sources, recipe_inputs, generator_inputs, generated_inputs
    )
    records, external = _classify_dependencies(
        root, depfiles, (), derived_outputs, external_roots, explicit
    )
    document = {"schema": "sm64-saturn-source-closure-v2", "inputs": _record_rows(root, records)}
    canonical = canonical_json_bytes(document)
    return ClosureBuild(
        document=document,
        canonical=canonical,
        sha256=hashlib.sha256(canonical).hexdigest(),
        external_dependencies=tuple(sorted(external, key=_path_sort_key)),
    )


def verify_source_closure(
    root: Path,
    sealed_path: Path,
    actual_depfiles: Sequence[Path],
    assembly_scan_depfiles: Sequence[Path],
    derived_outputs: Sequence[Path],
    external_roots: Sequence[Path],
    expected_external_dependencies: Sequence[Path],
    release_mode: bool,
) -> tuple[Path, ...]:
    """Reject post-build source, dependency, external, or release-tree drift."""
    root = root.resolve()
    sealed = _load_sealed_closure(sealed_path)
    sealed_rows = _sealed_rows(root, sealed)
    explicit = _explicit_classes_from_sealed(sealed_rows)
    actual_rows, actual_external = _classify_dependencies(
        root, actual_depfiles, assembly_scan_depfiles, derived_outputs, external_roots, explicit
    )
    if set(actual_rows) != set(sealed_rows) or any(
        _owners(actual_rows.get(key)) != _owners(sealed_rows.get(key))
        for key in set(actual_rows) | set(sealed_rows)
    ):
        raise ValueError(_render_closure_difference(sealed_rows, actual_rows))
    expected_external = tuple(sorted((Path(path).resolve() for path in expected_external_dependencies), key=_path_sort_key))
    actual_external_tuple = tuple(sorted(actual_external, key=_path_sort_key))
    if actual_external_tuple != expected_external:
        raise ValueError("actual external dependency set differs from sealed discovery")
    if release_mode:
        _verify_release_cleanliness(root, sealed_rows)
    for _key, row in sealed_rows.items():
        actual = sha256_file(root / row["path"])
        if actual != row["sha256"]:
            raise ValueError(f"source closure input changed after discovery: {row['path']}")
    return actual_external_tuple


def external_dependency_handoff_bytes(paths: Sequence[Path]) -> bytes:
    """Encode the diagnostic absolute-path handoff excluded from identity."""
    requested = [str(Path(path)) for path in paths]
    if any(not Path(path).is_absolute() for path in paths):
        raise ValueError("external dependency handoff paths must be absolute")
    if len(set(requested)) != len(requested):
        raise ValueError("duplicate external dependency handoff paths")
    try:
        reject_case_collisions(requested)
    except ValueError as error:
        raise ValueError(str(error).replace("repository paths", "external dependency paths")) from error
    ordered = sorted(requested, key=lambda value: value.encode("utf-8"))
    return canonical_json_bytes({"schema": EXTERNAL_DEPENDENCIES_SCHEMA, "paths": ordered})


def load_external_dependency_handoff(path: Path) -> tuple[Path, ...]:
    """Load one strict diagnostic handoff without admitting it to identity."""
    _require_file(path, "external dependency handoff")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("external dependency handoff is invalid JSON") from error
    if (not isinstance(document, dict) or set(document) != {"schema", "paths"}
            or document.get("schema") != EXTERNAL_DEPENDENCIES_SCHEMA
            or not isinstance(document.get("paths"), list)
            or any(not isinstance(value, str) or not value for value in document["paths"])):
        raise ValueError("external dependency handoff schema is invalid")
    paths = tuple(Path(value) for value in document["paths"])
    if any(not value.is_absolute() for value in paths):
        raise ValueError("external dependency handoff paths must be absolute")
    if raw != external_dependency_handoff_bytes(paths):
        raise ValueError("external dependency handoff is not canonical and sorted")
    return paths


def _add_common_paths(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--derived-output", type=Path, action="append", default=[])
    parser.add_argument("--external-root", type=Path, action="append", default=[])


def _parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build", help="discover and seal the pre-build closure")
    _add_common_paths(build)
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--external-output", type=Path, required=True)
    build.add_argument("--compiled-source", type=Path, action="append", default=[])
    build.add_argument("--depfile", type=Path, action="append", default=[])
    build.add_argument("--recipe-input", type=Path, action="append", default=[])
    build.add_argument("--generator-input", type=Path, action="append", default=[])
    build.add_argument("--generated-input", type=Path, action="append", default=[])

    verify = subparsers.add_parser("verify", help="verify post-link dependency equality")
    _add_common_paths(verify)
    verify.add_argument("--sealed", type=Path, required=True)
    verify.add_argument("--actual-depfile", type=Path, action="append", default=[])
    verify.add_argument("--assembly-scan-depfile", type=Path, action="append", default=[])
    verify.add_argument("--expected-external", type=Path, required=True)
    verify.add_argument("--mode", choices=("development", "release"), required=True)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    if args.command == "build":
        built = build_source_closure(
            args.root, args.compiled_source, args.depfile, args.recipe_input,
            args.generator_input, args.generated_input, args.derived_output,
            args.external_root,
        )
        handoff = external_dependency_handoff_bytes(built.external_dependencies)
        write_if_changed(args.output, built.canonical)
        write_if_changed(args.external_output, handoff)
    else:
        verify_source_closure(
            args.root, args.sealed, args.actual_depfile, args.assembly_scan_depfile,
            args.derived_output, args.external_root,
            load_external_dependency_handoff(args.expected_external),
            release_mode=args.mode == "release",
        )
    return 0


def _normalized_derived_outputs(root: Path, values: Sequence[Path]) -> set[str]:
    rendered = [_canonical_repo_path(root, value) for value in values]
    _reject_duplicate_paths(rendered, "derived output")
    return set(rendered)


def _explicit_classes(
    root: Path,
    derived: set[str],
    compiled_sources: Sequence[Path],
    recipe_inputs: Sequence[Path],
    generator_inputs: Sequence[Path],
    generated_inputs: Sequence[Path],
) -> dict[str, str]:
    assignments: list[tuple[str, str]] = []
    for label, paths in (
        ("compiled-source", compiled_sources),
        ("linker/build-recipe", recipe_inputs),
        ("generator", generator_inputs),
        ("generated-input", generated_inputs),
    ):
        for path in paths:
            rendered = _canonical_repo_path(root, path)
            if rendered not in derived:
                assignments.append((rendered, label))
    _reject_duplicate_paths([path for path, _label in assignments], "explicit input")
    classes: dict[str, str] = {}
    for path, label in assignments:
        previous = classes.get(path)
        if previous is not None and previous != label:
            raise ValueError(f"explicit input belongs to multiple classes: {path}")
        classes[path] = label
        _require_file(root / path, f"{label} input")
    return classes


def _explicit_classes_from_sealed(sealed_rows: Mapping[tuple[str, str], dict[str, Any]]) -> dict[str, str]:
    classes: dict[str, str] = {}
    for (path, label), row in sealed_rows.items():
        if label == "header":
            continue
        if label not in _EXPLICIT_CLASSES or label not in row["owners"]:
            raise ValueError(f"sealed closure has invalid explicit ownership: {path}")
        existing = classes.get(path)
        if existing is not None and existing != label:
            raise ValueError(f"sealed closure has multiple classes for one path: {path}")
        classes[path] = label
    return classes


def _classify_dependencies(
    root: Path,
    depfiles: Sequence[Path],
    assembly_scan_depfiles: Sequence[Path],
    derived_outputs: Sequence[Path],
    external_roots: Sequence[Path],
    explicit: Mapping[str, str] | None = None,
) -> tuple[dict[tuple[str, str], dict[str, Any]], set[Path]]:
    """Classify depfile inputs; external inputs are deliberately not serialized."""
    root = root.resolve()
    derived = _normalized_derived_outputs(root, derived_outputs)
    explicit = dict(explicit or {})
    roots = _external_roots(root, external_roots)
    records: dict[tuple[str, str], dict[str, Any]] = {}
    for path, label in explicit.items():
        if path in derived:
            continue
        _require_file(root / path, f"{label} input")
        # Explicit inputs are registered even if a compiler does not list them.
        _add_record(records, path, label, label)
    external: set[Path] = set()
    for owner_prefix, paths in (("depfile", depfiles), ("assembly-scan", assembly_scan_depfiles)):
        normalized_depfiles = [_canonical_repo_path(root, path) for path in paths]
        _reject_duplicate_paths(normalized_depfiles, f"{owner_prefix} depfile")
        for rendered_depfile in normalized_depfiles:
            depfile = root / rendered_depfile
            _require_file(depfile, f"{owner_prefix} depfile")
            for dependency in parse_make_depfile(depfile.read_text(encoding="utf-8")):
                candidate = _resolve_dependency(root, dependency)
                try:
                    rendered = _canonical_repo_path(root, dependency)
                except ValueError:
                    external_path = candidate.resolve()
                    _require_file(external_path, "external dependency")
                    if not any(_is_within(external_path, allowed) for allowed in roots):
                        raise ValueError(f"unclassified external dependency: {external_path}")
                    external.add(external_path)
                    continue
                if rendered in derived:
                    continue
                _require_file(root / rendered, "compiler dependency")
                label = explicit.get(rendered, "header")
                _add_record(records, rendered, label, "compiler")
    return records, external


def _resolve_dependency(root: Path, dependency: str) -> Path:
    candidate = Path(dependency)
    return candidate if candidate.is_absolute() else root / candidate


def _canonical_repo_path(root: Path, value: str | Path) -> str:
    """Return Task 1's one normalized canonical repository spelling."""
    return normalize_repo_path(root, value)


def _external_roots(root: Path, roots: Sequence[Path]) -> tuple[Path, ...]:
    resolved = tuple(Path(path).resolve() for path in roots)
    if len(set(resolved)) != len(resolved):
        raise ValueError("duplicate external roots")
    for path in resolved:
        if _is_within(path, root):
            raise ValueError(f"external root lies inside repository: {path}")
        if not path.exists():
            raise ValueError(f"external root does not exist: {path}")
    return resolved


def _is_within(candidate: Path, parent: Path) -> bool:
    try:
        candidate.relative_to(parent)
    except ValueError:
        return False
    return True


def _add_record(records: dict[tuple[str, str], dict[str, Any]], path: str, label: str, owner: str) -> None:
    key = (path, label)
    existing = records.get(key)
    if existing is None:
        records[key] = {"path": path, "class": label, "owners": {owner}}
    else:
        existing["owners"].add(owner)


def _record_rows(root: Path, records: Mapping[tuple[str, str], dict[str, Any]]) -> list[dict[str, Any]]:
    paths = [row["path"] for row in records.values()]
    _reject_duplicate_paths(paths, "closure record")
    rows = [
        {
            "path": row["path"],
            "sha256": sha256_file(root / row["path"]),
            "class": row["class"],
            "owners": sorted(row["owners"]),
        }
        for row in records.values()
    ]
    return sorted(rows, key=lambda row: (row["path"].encode("utf-8"), row["class"].encode("ascii")))


def _reject_duplicate_paths(values: Iterable[str], kind: str) -> None:
    rendered = list(values)
    if len(set(rendered)) != len(rendered):
        raise ValueError(f"duplicate {kind} path")
    try:
        reject_case_collisions(rendered)
    except ValueError as error:
        raise ValueError(str(error).replace("repository paths", f"{kind} paths")) from error


def _require_file(path: Path, kind: str) -> None:
    if not path.is_file():
        raise ValueError(f"{kind} is not a file: {path}")


def _load_sealed_closure(path: Path) -> dict[str, Any]:
    _require_file(path, "sealed source closure")
    raw = path.read_bytes()
    try:
        document = json.loads(raw.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError("sealed source closure is not canonical JSON") from error
    if canonical_json_bytes(document) != raw:
        raise ValueError("sealed source closure is not canonical JSON")
    return document


def _sealed_rows(root: Path, sealed: Mapping[str, Any]) -> dict[tuple[str, str], dict[str, Any]]:
    if set(sealed) != {"schema", "inputs"} or sealed.get("schema") != "sm64-saturn-source-closure-v2":
        raise ValueError("sealed source closure schema is invalid")
    inputs = sealed.get("inputs")
    if not isinstance(inputs, list):
        raise ValueError("sealed source closure inputs are invalid")
    rows: dict[tuple[str, str], dict[str, Any]] = {}
    paths: list[str] = []
    for row in inputs:
        if not isinstance(row, dict) or set(row) != {"path", "sha256", "class", "owners"}:
            raise ValueError("sealed source closure record is invalid")
        path, digest, label, owners = row["path"], row["sha256"], row["class"], row["owners"]
        if (not isinstance(path, str) or not isinstance(digest, str) or len(digest) != 64
                or any(character not in "0123456789abcdef" for character in digest)
                or label not in CLASS_PRECEDENCE or not isinstance(owners, list)
                or not owners or owners != sorted(owners) or any(not isinstance(owner, str) for owner in owners)):
            raise ValueError("sealed source closure record is invalid")
        if path != normalize_repo_path(root, path):
            raise ValueError("sealed source closure record path is invalid")
        key = (path, label)
        if key in rows:
            raise ValueError("sealed source closure has duplicate records")
        rows[key] = row
        paths.append(path)
    _reject_duplicate_paths(paths, "sealed closure")
    ordered = sorted(inputs, key=lambda row: (
        row["path"].encode("utf-8"), row["class"].encode("ascii")
    ))
    if inputs != ordered:
        raise ValueError("sealed source closure records are not sorted")
    return rows


def _render_closure_difference(
    sealed: Mapping[tuple[str, str], dict[str, Any]],
                               actual: Mapping[tuple[str, str], dict[str, Any]]) -> str:
    missing = sorted(set(sealed) - set(actual))
    extra = sorted(set(actual) - set(sealed))
    owner_changes = sorted(
        key for key in set(sealed) & set(actual)
        if _owners(sealed[key]) != _owners(actual[key])
    )
    return f"source closure differs after build; missing={missing}, extra={extra}, owner_changes={owner_changes}"


def _owners(row: Mapping[str, Any] | None) -> tuple[str, ...]:
    if row is None:
        return ()
    return tuple(sorted(row["owners"]))


def _verify_release_cleanliness(root: Path, sealed_rows: Mapping[tuple[str, str], dict[str, Any]]) -> None:
    checked_in = sorted(
        row["path"] for row in sealed_rows.values()
        if row["class"] != "generated-input" or not row["path"].startswith("build/")
    )
    if not checked_in:
        return
    for path in checked_in:
        tracked = subprocess.run(
            ["git", "ls-files", "--error-unmatch", "--", path],
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
        if tracked.returncode != 0:
            raise ValueError(f"release closure input is not tracked: {path}")
    result = subprocess.run(
        ["git", "status", "--porcelain=v1", "--untracked-files=all", "--", *checked_in],
        cwd=root,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or result.stdout:
        raise ValueError("release closure inputs are not clean")


def _path_sort_key(path: Path) -> tuple[bytes, ...]:
    return tuple(part.encode("utf-8") for part in path.as_posix().split("/"))


if __name__ == "__main__":
    raise SystemExit(main())
