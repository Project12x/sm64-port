#!/usr/bin/env python3
"""List host texture-include targets needed by a linked SM64 source closure.

The decomp sources include generated texture C fragments such as
``actors/mist/mist.ia16.inc.c``.  They are intentionally ignored local build
outputs, derived from the user's verified ROM by the repository's existing
asset extractor.  This tool follows only local C include edges from a bounded
set of source roots and prints the matching ``build/us_pc/...`` make targets.

It never reads a ROM and never writes an asset.  Its output lets the Saturn
sourceboot target prepare exactly the source assets it compiles, without asking
the desktop host toolchain to compile unrelated PC objects.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path


INCLUDE = re.compile(r'^\s*#\s*include\s+"([^"]+)"')
DIRECTIVE = re.compile(r"^\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)$")


def include_candidate(root: Path, source: Path, include: str) -> Path:
    """Resolve a quoted include using the source directory then project root."""
    local = source.parent / include
    if local.is_file():
        return local
    return root / include


def version_condition(expression: str, defines: set[str]) -> bool:
    """Evaluate the version-only preprocessor forms used by source textures.

    The source tree keeps regional texture alternatives inside ordinary C
    preprocessor branches.  Asset preparation must follow the same branch as
    the Saturn compile so it never asks make for an inactive regional file.
    Unknown conditions remain enabled: that safely over-approximates the
    package inputs without hiding a real dependency.
    """
    expression = expression.strip()
    if expression == "0":
        return False
    if expression == "1":
        return True
    if not re.fullmatch(r"[A-Za-z0-9_\s()!&|]+", expression):
        return True

    def defined(match: re.Match[str]) -> str:
        return "True" if match.group(1) in defines else "False"

    expression = re.sub(r"defined\s*\(\s*([A-Za-z_]\w*)\s*\)", defined, expression)
    expression = re.sub(r"\bdefined\s+([A-Za-z_]\w*)", defined, expression)
    for name in re.findall(r"\bVERSION_[A-Za-z0-9_]+\b", expression):
        expression = re.sub(rf"\b{re.escape(name)}\b", "True" if name in defines else "False", expression)
    expression = expression.replace("&&", " and ").replace("||", " or ")
    expression = re.sub(r"!(?!=)", " not ", expression)
    try:
        return bool(eval(expression, {"__builtins__": {}}, {}))
    except (SyntaxError, ValueError, TypeError, NameError):
        return True


def active_lines(source: Path, defines: set[str]) -> list[str]:
    """Return source lines active for the sourceboot's selected ROM version."""
    active = True
    stack: list[tuple[bool, bool, bool]] = []
    result: list[str] = []

    for line in source.read_text(encoding="utf-8").splitlines():
        match = DIRECTIVE.match(line)
        if match is None:
            if active:
                result.append(line)
            continue

        directive, expression = match.groups()
        expression = expression.strip()
        if directive in {"ifdef", "ifndef", "if"}:
            parent = active
            if directive == "ifdef":
                condition = expression in defines
            elif directive == "ifndef":
                condition = expression not in defines
            else:
                condition = version_condition(expression, defines)
            active = parent and condition
            stack.append((parent, condition, active))
        elif directive == "elif" and stack:
            parent, matched, _ = stack[-1]
            condition = not matched and version_condition(expression, defines)
            active = parent and condition
            stack[-1] = (parent, matched or condition, active)
        elif directive == "else" and stack:
            parent, matched, _ = stack[-1]
            active = parent and not matched
            stack[-1] = (parent, True, active)
        elif directive == "endif" and stack:
            parent, _, _ = stack.pop()
            active = parent

    return result


def collect_targets(root: Path, sources: list[Path], build_prefix: str,
                    defines: set[str]) -> list[str]:
    pending = list(sources)
    visited: set[Path] = set()
    targets: set[str] = set()

    while pending:
        source = pending.pop()
        if not source.is_file():
            raise FileNotFoundError(f"source include root is missing: {source}")
        source = source.resolve()
        if source in visited:
            continue
        visited.add(source)

        for line in active_lines(source, defines):
            match = INCLUDE.match(line)
            if match is None:
                continue
            include = match.group(1)
            candidate = include_candidate(root, source, include)
            if candidate.is_file():
                # The source closure is C-only for this use.  Headers provide
                # declarations and are not traversed into implementation data.
                if candidate.suffix == ".c":
                    pending.append(candidate)
                continue
            if include.endswith(".inc.c"):
                targets.add(f"{build_prefix}/{include}")

    return sorted(targets)


def verify_existing_targets(root: Path, targets: list[str]) -> None:
    """Require candidate-local generated inputs without consulting mtimes."""
    for target in targets:
        path = root / target
        if not path.is_file():
            raise FileNotFoundError(
                f"required generated source asset is missing: {target}"
            )
        try:
            path.resolve(strict=True).relative_to(root)
        except ValueError as exc:
            raise ValueError(
                f"required generated source asset escapes root: {target}"
            ) from exc


def collect_verified_target_closure(
    root: Path, targets: list[str], build_prefix: str
) -> list[str]:
    """Follow quoted includes that stay inside the verified generated root.

    This is the source-closure equivalent of the transitive generated-header
    walk previously used by identity v1. It runs only after the direct targets
    exist, so development-mode target discovery can still ask root Make to
    materialize those targets first.
    """
    verify_existing_targets(root, targets)
    generated_root = (root / build_prefix).resolve()
    pending = [root / target for target in targets]
    closure: set[str] = set()

    while pending:
        path = pending.pop()
        if not path.is_file():
            raise FileNotFoundError(
                f"required generated source asset is missing: "
                f"{path.relative_to(root).as_posix()}"
            )
        resolved = path.resolve(strict=True)
        try:
            resolved.relative_to(generated_root)
            relative = resolved.relative_to(root).as_posix()
        except ValueError as exc:
            raise ValueError(
                f"required generated source asset escapes root: {path}"
            ) from exc
        if relative in closure:
            continue
        closure.add(relative)

        for line in resolved.read_text(encoding="utf-8").splitlines():
            match = INCLUDE.match(line)
            if match is None:
                continue
            candidate = resolved.parent / match.group(1)
            try:
                candidate.resolve().relative_to(generated_root)
            except ValueError:
                continue
            if not candidate.is_file():
                missing = candidate.relative_to(root).as_posix()
                raise FileNotFoundError(
                    f"required generated source asset is missing: {missing}"
                )
            pending.append(candidate)

    return sorted(closure)


def write_path_list(path: Path, root: Path, targets: list[str]) -> None:
    """Publish the verified target inventory in the closure's strict format."""
    values = [(root / target).resolve(strict=True).as_posix() for target in targets]
    if len(values) != len(set(values)):
        raise ValueError("generated source asset path list contains duplicates")
    folded = [value.casefold() for value in values]
    if len(folded) != len(set(folded)):
        raise ValueError("generated source asset path list contains case collisions")
    rows = sorted(values, key=lambda value: value.encode("utf-8"))
    contents = "sm64-saturn-path-list-v1\n" + "".join(
        f"{value}\n" for value in rows
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(f"{path.name}.tmp")
    temporary.write_text(contents, encoding="utf-8", newline="\n")
    temporary.replace(path)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-prefix", default="build/us_pc")
    parser.add_argument("--source", action="append", required=True)
    parser.add_argument("--define", action="append", default=[])
    parser.add_argument("--required", action="append", default=[])
    parser.add_argument("--verify-existing", action="store_true")
    parser.add_argument("--path-list", type=Path)
    args = parser.parse_args()

    if args.path_list is not None and not args.verify_existing:
        parser.error("--path-list requires --verify-existing")

    root = args.root.resolve()
    sources = [root / source for source in args.source]
    targets = sorted(set(
        collect_targets(root, sources, args.build_prefix.rstrip("/"),
                        set(args.define)) + args.required
    ))
    if args.verify_existing:
        targets = collect_verified_target_closure(
            root, targets, args.build_prefix.rstrip("/")
        )
    if args.path_list is not None:
        write_path_list(args.path_list, root, targets)
    print(" ".join(targets))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
