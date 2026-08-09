#!/usr/bin/env python3
"""Standalone expanded-sequence-bank generator (no PC game build required).

Reproduces the PC build's `sequences.bin` assembly path byte-for-byte:

  1. `sound/sequences/00_sound_player.s` is preprocessed with the C
     preprocessor (`cpp -P -Wno-trigraphs -I include -DVERSION_US`, matching
     the decomp Makefile's `$(CPP) $(CPPFLAGS) $< | $(AS)` rule at
     Makefile:901-903), assembled with GNU as, and extracted with
     `objcopy -j .rodata -O binary` (Makefile:771-773).  The sequence source
     emits only `.byte` directives (include/seq_macros.inc), so the bytes are
     identical for any GNU as target; the pinned sh-elf toolchain is used.
  2. The assembled seq00 plus the extracted US `.m64` set are concatenated by
     the repo's own canonical serializer, `tools/assemble_sound.py
     --sequences` (Makefile:761-763), invoked as a subprocess so the index
     table, garbage alignment, and padding are the reference implementation's
     bytes, not a reimplementation.  Reuse mode: dependency (in-repo decomp
     tool, pinned by the working tree).

Endianness/word size are fixed to big-endian / 32-bit words: the consumer is
the Saturn's big-endian SH-2/68k audio pipeline, and this matches the N64
sequence-bank layout that `src/port/saturn/audio68k/sequence_vm.c` decodes.

Toolchain discovery follows Makefile.saturn.mk's existing convention
(check-sdk): `--toolchain-bin` argument first, then `$YAUL_INSTALL_ROOT/bin`,
then PATH.  Missing inputs or tools fail closed with named errors.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

SEQFILE_MAGIC = 3  # TYPE_SEQ in tools/assemble_sound.py
SEQUENCE_DEFINES = ("VERSION_US",)
_TOOL_NAMES = ("cpp", "as", "objcopy")


class SequenceBankError(RuntimeError):
    pass


def _sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def convert_msys_path(text: str) -> str:
    """Convert MSYS2-style `/d/...` paths to Windows `D:/...` paths.

    YAUL_INSTALL_ROOT is normally exported from .yaul.env in MSYS2 form; this
    tool runs under the Windows host Python, which cannot resolve that form.
    Non-MSYS paths pass through unchanged.
    """
    match = re.match(r"^/([a-zA-Z])(/|$)", text)
    if match and os.name == "nt":
        return f"{match.group(1).upper()}:/{text[3:]}"
    return text


def _find_in_dir(bin_dir: Path, prefix: str, tool: str) -> Path | None:
    for suffix in (".exe", ""):
        candidate = bin_dir / f"{prefix}-{tool}{suffix}"
        if candidate.is_file():
            return candidate
    return None


def discover_toolchain(toolchain_bin: str | Path | None = None,
                       env: dict[str, str] | None = None,
                       which=None,
                       prefix: str = "sh-elf") -> dict[str, str]:
    """Locate cpp/as/objcopy, fail closed if the pinned toolchain is absent."""
    if env is None:
        env = dict(os.environ)
    if which is None:
        which = shutil.which
    search_dirs: list[Path] = []
    if toolchain_bin:
        search_dirs.append(Path(convert_msys_path(str(toolchain_bin))))
    yaul_root = env.get("YAUL_INSTALL_ROOT")
    if yaul_root:
        search_dirs.append(Path(convert_msys_path(yaul_root)) / "bin")
    tools: dict[str, str] = {}
    for tool in _TOOL_NAMES:
        found: str | None = None
        for bin_dir in search_dirs:
            candidate = _find_in_dir(bin_dir, prefix, tool)
            if candidate is not None:
                found = str(candidate)
                break
        if found is None:
            path_hit = which(f"{prefix}-{tool}")
            if path_hit:
                found = path_hit
        if found is None:
            raise SequenceBankError(
                f"pinned SH toolchain binary not found: {prefix}-{tool} "
                f"(searched {[str(d) for d in search_dirs]} and PATH). "
                "Pass --toolchain-bin <dir> or set YAUL_INSTALL_ROOT "
                "(source .yaul.env).")
        tools[tool] = found
    return tools


def _run(command: list[str], what: str, stdout=None) -> subprocess.CompletedProcess:
    try:
        result = subprocess.run(command, stdout=stdout,
                                stderr=subprocess.PIPE, check=False)
    except OSError as error:
        raise SequenceBankError(f"{what}: cannot execute {command[0]}: {error}") from error
    if result.returncode != 0:
        stderr = (result.stderr or b"").decode("utf-8", "replace").strip()
        raise SequenceBankError(f"{what} failed (exit {result.returncode}): {stderr}")
    return result


def assemble_sequence_source(source: Path, include_dir: Path, work_dir: Path,
                             tools: dict[str, str],
                             defines: tuple[str, ...] = SEQUENCE_DEFINES) -> Path:
    """cpp + as + objcopy one sequence `.s` into a raw `.m64`, per the decomp
    Makefile's assembly path.  Input checks precede any tool invocation."""
    source = Path(source)
    if not source.is_file():
        raise SequenceBankError(f"missing sequence source: {source}")
    if not include_dir.is_dir():
        raise SequenceBankError(f"missing include directory: {include_dir}")
    work_dir.mkdir(parents=True, exist_ok=True)
    preprocessed = work_dir / f"{source.stem}.i"
    obj = work_dir / f"{source.stem}.o"
    m64 = work_dir / f"{source.stem}.m64"
    with open(preprocessed, "wb") as stream:
        _run([tools["cpp"], "-P", "-Wno-trigraphs", "-I", str(include_dir)]
             + [f"-D{define}" for define in defines] + [str(source)],
             f"preprocess {source.name}", stdout=stream)
    _run([tools["as"], "-I", str(include_dir), "-o", str(obj), str(preprocessed)],
         f"assemble {source.name}")
    _run([tools["objcopy"], "-j", ".rodata", "-O", "binary", str(obj), str(m64)],
         f"objcopy {source.name}")
    if not m64.is_file() or m64.stat().st_size == 0:
        raise SequenceBankError(f"assembled sequence is empty: {m64}")
    return m64


def _strip_comments(text: str) -> str:
    text = re.sub(re.compile(r"/\*.*?\*/", re.DOTALL), "", text)
    return re.sub(re.compile(r"//.*?\n"), "", text)


def _sequence_index_names(sequences_json: Path,
                          defines: tuple[str, ...]) -> list[str | None]:
    """Replicate assemble_sound.py's key -> index-table mapping."""
    raw = json.loads(_strip_comments(sequences_json.read_text(encoding="utf-8")))
    ind_to_name: list[str | None] = []
    define_set = set(defines)
    for key, value in raw.items():
        if key == "comment":
            continue
        if isinstance(value, dict):
            value = value["banks"] if any(d in define_set for d in value["ifdef"]) else None
        index = int(key.split("_")[0], 16)
        while len(ind_to_name) <= index:
            ind_to_name.append(None)
        ind_to_name[index] = key if value is not None else None
    while ind_to_name and ind_to_name[-1] is None:
        ind_to_name.pop()
    return ind_to_name


def parse_sequence_bank(raw: bytes) -> list[tuple[int, int]]:
    """Validate and read the big-endian 32-bit sequence-bank index table."""
    if len(raw) < 4:
        raise SequenceBankError("sequence bank is truncated (no header)")
    magic, count = struct.unpack_from(">HH", raw, 0)
    if magic != SEQFILE_MAGIC:
        raise SequenceBankError(
            f"sequence bank has wrong magic {magic} (expected {SEQFILE_MAGIC})")
    if count < 1:
        raise SequenceBankError("sequence bank has no entries")
    table_end = 4 + count * 8
    if len(raw) < table_end:
        raise SequenceBankError("sequence bank is truncated (index table)")
    table = []
    for index in range(count):
        offset, length = struct.unpack_from(">II", raw, 4 + index * 8)
        if length and (offset < table_end or offset + length > len(raw)):
            raise SequenceBankError(
                f"sequence bank entry {index} is out of range: "
                f"offset {offset} length {length} file {len(raw)}")
        table.append((offset, length))
    return table


def build_sequence_bank(sequence_files: list[Path], sequences_json: Path,
                        sound_banks_dir: Path, output_bin: Path,
                        bank_sets_out: Path, manifest_out: Path,
                        assemble_sound_py: Path,
                        defines: tuple[str, ...] = SEQUENCE_DEFINES,
                        python_exe: str | None = None) -> dict[str, object]:
    """Concatenate assembled/extracted sequences via assemble_sound.py and
    emit the raw bank plus a JSON manifest (offsets/sizes/SHA-256)."""
    sequences_json = Path(sequences_json)
    sound_banks_dir = Path(sound_banks_dir)
    assemble_sound_py = Path(assemble_sound_py)
    if not sequences_json.is_file():
        raise SequenceBankError(f"missing sequence catalog: {sequences_json}")
    if not sound_banks_dir.is_dir():
        raise SequenceBankError(f"missing sound-bank directory: {sound_banks_dir}")
    if not assemble_sound_py.is_file():
        raise SequenceBankError(f"missing serializer: {assemble_sound_py}")
    for path in sequence_files:
        if not Path(path).is_file():
            raise SequenceBankError(f"missing sequence input: {path}")
    output_bin = Path(output_bin)
    bank_sets_out = Path(bank_sets_out)
    manifest_out = Path(manifest_out)
    output_bin.parent.mkdir(parents=True, exist_ok=True)
    bank_sets_out.parent.mkdir(parents=True, exist_ok=True)
    manifest_out.parent.mkdir(parents=True, exist_ok=True)
    # The Shindou header file is only written for VERSION_SH; a path is still
    # required positionally by the serializer CLI.
    unused_header = output_bin.parent / (output_bin.name + ".sh_header.unused")
    command = [python_exe or sys.executable, str(assemble_sound_py),
               "--endian", "big", "--bitwidth", "32"]
    for define in defines:
        command += ["-D", define]
    command += ["--sequences", str(output_bin), str(unused_header),
                str(bank_sets_out), str(sound_banks_dir), str(sequences_json)]
    command += [str(path) for path in sequence_files]
    _run(command, "assemble_sound.py --sequences")
    if not output_bin.is_file():
        raise SequenceBankError(f"serializer produced no output: {output_bin}")
    raw = output_bin.read_bytes()
    table = parse_sequence_bank(raw)
    names = _sequence_index_names(sequences_json, defines)
    if len(names) != len(table):
        raise SequenceBankError(
            f"index table count {len(table)} does not match sequence catalog "
            f"count {len(names)}")
    entries = []
    for index, ((offset, length), name) in enumerate(zip(table, names)):
        entries.append({"id": index, "name": name, "offset": offset,
                        "bytes": length,
                        "sha256": _sha(raw[offset:offset + length])})
    bank_sets_raw = bank_sets_out.read_bytes()
    manifest = {
        "schema": "sm64-saturn-sequence-bank-manifest-v1",
        "endian": "big",
        "word_bytes": 4,
        "magic": SEQFILE_MAGIC,
        "entry_count": len(table),
        "defines": list(defines),
        "bin": {"path": output_bin.name, "bytes": len(raw), "sha256": _sha(raw)},
        "bank_sets": {"path": bank_sets_out.name, "bytes": len(bank_sets_raw),
                      "sha256": _sha(bank_sets_raw)},
        "sequences": entries,
        "inputs": [{"name": Path(path).name,
                    "bytes": Path(path).stat().st_size,
                    "sha256": _sha(Path(path).read_bytes())}
                   for path in sorted(sequence_files, key=lambda p: Path(p).name)],
    }
    manifest_out.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                            encoding="utf-8")
    return manifest


def generate(root: Path, output_dir: Path,
             toolchain_bin: str | Path | None = None,
             defines: tuple[str, ...] = SEQUENCE_DEFINES) -> dict[str, object]:
    """Assemble every sequence `.s` and emit the standalone sequence bank."""
    root = Path(root)
    output_dir = Path(output_dir)
    sequence_dirs = [root / "sound" / "sequences", root / "sound" / "sequences" / "us"]
    source_files = sorted(path for directory in sequence_dirs
                          for path in directory.glob("*.s"))
    m64_files = sorted(path for directory in sequence_dirs
                       for path in directory.glob("*.m64"))
    if not source_files:
        raise SequenceBankError(
            f"no sequence .s sources under {sequence_dirs[0]} "
            "(expected sound/sequences/00_sound_player.s)")
    tools = discover_toolchain(toolchain_bin)
    work_dir = output_dir / "sequences_work"
    assembled = [assemble_sequence_source(source, root / "include", work_dir,
                                          tools, defines)
                 for source in source_files]
    return build_sequence_bank(
        sequence_files=assembled + m64_files,
        sequences_json=root / "sound" / "sequences.json",
        sound_banks_dir=root / "sound" / "sound_banks",
        output_bin=output_dir / "sequences.bin",
        bank_sets_out=output_dir / "sequences.bank_sets.bin",
        manifest_out=output_dir / "sequences.manifest.json",
        assemble_sound_py=root / "tools" / "assemble_sound.py",
        defines=defines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path.cwd())
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--toolchain-bin", default=None,
                        help="directory containing sh-elf-{cpp,as,objcopy}; "
                             "defaults to $YAUL_INSTALL_ROOT/bin, then PATH")
    args = parser.parse_args()
    try:
        manifest = generate(args.root.resolve(), args.output_dir,
                            args.toolchain_bin)
    except SequenceBankError as error:
        print(f"gen_sequence_bank: {error}", file=sys.stderr)
        raise SystemExit(1)
    summary = {"entry_count": manifest["entry_count"],
               "bin": manifest["bin"],
               "seq00": manifest["sequences"][0] if manifest["sequences"] else None}
    print(json.dumps(summary, sort_keys=True))


if __name__ == "__main__":
    main()
