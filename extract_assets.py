#!/usr/bin/env python3
import sys
import os
import json
import argparse
import stat
from pathlib import Path, PurePosixPath, PureWindowsPath


def read_asset_map():
    with open("assets.json") as f:
        ret = json.load(f)
    return ret


def read_local_asset_list(f):
    if f is None:
        return []
    ret = []
    for line in f:
        ret.append(line.strip())
    return ret


def asset_needs_update(asset, version):
    if version <= 6 and asset in ["actors/king_bobomb/king_bob-omb_eyes.rgba16.png", "actors/king_bobomb/king_bob-omb_hand.rgba16.png"]:
        return True
    if version <= 5 and asset == "textures/spooky/bbh_textures.00800.rgba16.png":
        return True
    if version <= 4 and asset in ["textures/mountain/ttm_textures.01800.rgba16.png", "textures/mountain/ttm_textures.05800.rgba16.png"]:
        return True
    if version <= 3 and asset == "textures/cave/hmc_textures.01800.rgba16.png":
        return True
    if version <= 2 and asset == "textures/inside/inside_castle_textures.09000.rgba16.png":
        return True
    if version <= 1 and asset.endswith(".m64"):
        return True
    if version <= 0 and asset.endswith(".aiff"):
        return True
    return False


def _is_symlink_or_reparse(path):
    if path.is_symlink():
        return True
    try:
        attributes = os.lstat(path).st_file_attributes
    except (AttributeError, FileNotFoundError, OSError):
        return False
    return bool(attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)


def _clean_asset_path(fname, output_root):
    if not isinstance(fname, str) or not fname or fname != fname.strip() or "\x00" in fname:
        raise ValueError("clean asset path is empty or noncanonical")
    if "\\" in fname:
        raise ValueError(f"clean asset path is not canonical relative POSIX: {fname!r}")
    posix = PurePosixPath(fname)
    windows = PureWindowsPath(fname)
    if (posix.is_absolute() or windows.is_absolute() or windows.drive
            or any(part in ("", ".", "..") for part in posix.parts)
            or posix.as_posix() != fname):
        raise ValueError(f"clean asset path must be strict relative without traversal: {fname!r}")

    root = Path(output_root).resolve()
    candidate = root.joinpath(*posix.parts)
    try:
        candidate.resolve(strict=False).relative_to(root)
    except ValueError as error:
        raise ValueError(f"clean asset path escapes output root: {fname!r}") from error

    current = root
    for part in posix.parts:
        current = current / part
        if current.exists() or current.is_symlink():
            if _is_symlink_or_reparse(current):
                raise ValueError(f"clean asset path uses a symlink or reparse point: {fname!r}")
            try:
                current.resolve().relative_to(root)
            except ValueError as error:
                raise ValueError(f"clean asset path escapes output root: {fname!r}") from error
    return root, candidate


def remove_file(fname, output_root=Path(".")):
    root, path = _clean_asset_path(fname, output_root)
    path.unlink()
    print("deleting", path)
    parent = path.parent
    while parent != root:
        try:
            parent.rmdir()
        except OSError:
            break
        parent = parent.parent


def clean_assets(local_asset_file, output_root=Path(".")):
    assets = set(read_asset_map().keys())
    assets.update(read_local_asset_list(local_asset_file))
    if local_asset_file is not None:
        local_asset_file.close()
    names = [fname for fname in assets if not fname.startswith("@")] + [".assets-local.txt"]
    for fname in names:
        _clean_asset_path(fname, output_root)
    for fname in names:
        try:
            remove_file(fname, output_root)
        except FileNotFoundError:
            pass


def write_path_list(path, output_root, asset_map, langs):
    expected = []
    for asset, data in asset_map.items():
        if asset.startswith("@") or not any(lang in data[-1] for lang in langs):
            continue
        candidate = output_root / asset
        if not candidate.is_file():
            raise FileNotFoundError(f"extracted asset is missing: {candidate}")
        expected.append(candidate.resolve().as_posix())
    expected.append((output_root / ".assets-local.txt").resolve().as_posix())
    if len(expected) != len(set(expected)):
        raise ValueError("extracted asset path list contains duplicates")
    folded = [value.casefold() for value in expected]
    if len(folded) != len(set(folded)):
        raise ValueError("extracted asset path list contains case collisions")
    path.parent.mkdir(parents=True, exist_ok=True)
    rows = sorted(expected, key=lambda value: value.encode("utf-8"))
    path.write_text(
        "sm64-saturn-path-list-v1\n" + "\n".join(rows) + "\n",
        encoding="utf-8",
        newline="\n",
    )


def tool_output_path(path):
    """Render an output without a Windows drive colon for tool `path:offset` args."""
    resolved = path.resolve()
    try:
        return resolved.relative_to(Path.cwd().resolve()).as_posix()
    except ValueError:
        rendered = resolved.as_posix()
        if ":" in rendered:
            raise ValueError("asset output root must be inside the repository")
        return rendered


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("langs", nargs="*")
    parser.add_argument("--clean", action="store_true")
    parser.add_argument("--output-root", type=Path, default=Path("."))
    parser.add_argument("--path-list", type=Path)
    options = parser.parse_args()
    output_root = options.output_root.resolve()

    # In case we ever need to change formats of generated files, we keep a
    # revision ID in the local asset file.
    new_version = 7

    try:
        local_asset_file = open(output_root / ".assets-local.txt")
        local_asset_file.readline()
        local_version = int(local_asset_file.readline().strip())
    except Exception:
        local_asset_file = None
        local_version = -1

    langs = options.langs
    if options.clean:
        if langs or options.path_list is not None:
            parser.error("--clean cannot be combined with languages or --path-list")
        clean_assets(local_asset_file, output_root)
        sys.exit(0)

    all_langs = ["jp", "us", "eu", "sh"]
    if not langs or not all(a in all_langs for a in langs):
        langs_str = " ".join("[" + lang + "]" for lang in all_langs)
        print("Usage: " + sys.argv[0] + " " + langs_str)
        print("For each version, baserom.<version>.z64 must exist")
        sys.exit(1)

    asset_map = read_asset_map()
    all_assets = []
    any_missing_assets = False
    for asset, data in asset_map.items():
        if asset.startswith("@"):
            continue
        if (output_root / asset).is_file():
            all_assets.append((asset, data, True))
        else:
            all_assets.append((asset, data, False))
            if not any_missing_assets and any(lang in data[-1] for lang in langs):
                any_missing_assets = True

    if not any_missing_assets and local_version == new_version:
        # Nothing to do, no need to read a ROM. For efficiency we don't check
        # the list of old assets either.
        if options.path_list is not None:
            write_path_list(options.path_list.resolve(), output_root, asset_map, langs)
        return

    # Late imports (to optimize startup perf)
    import subprocess
    import hashlib
    import tempfile
    from collections import defaultdict

    new_assets = {a[0] for a in all_assets}

    previous_assets = read_local_asset_list(local_asset_file)
    if local_version == -1:
        # If we have no local asset file, we assume that files are version
        # controlled and thus up to date.
        local_version = new_version

    # Create work list
    todo = defaultdict(lambda: [])
    for (asset, data, exists) in all_assets:
        # Leave existing assets alone if they have a compatible version.
        if exists and not asset_needs_update(asset, local_version):
            continue

        meta = data[:-2]
        size, positions = data[-2:]
        for lang, pos in positions.items():
            mio0 = None if len(pos) == 1 else pos[0]
            pos = pos[-1]
            if lang in langs:
                todo[(lang, mio0)].append((asset, pos, size, meta))
                break

    # Load ROMs
    roms = {}
    for lang in langs:
        fname = "baserom." + lang + ".z64"
        try:
            with open(fname, "rb") as f:
                roms[lang] = f.read()
        except Exception as e:
            print("Failed to open " + fname + "! " + str(e))
            sys.exit(1)
        sha1 = hashlib.sha1(roms[lang]).hexdigest()
        with open("sm64." + lang + ".sha1", "r") as f:
            expected_sha1 = f.read().split()[0]
        if sha1 != expected_sha1:
            print(
                fname
                + " has the wrong hash! Found "
                + sha1
                + ", expected "
                + expected_sha1
            )
            sys.exit(1)

    make = "make"

    for path in os.environ["PATH"].split(os.pathsep):
        if os.path.isfile(os.path.join(path, "gmake")):
            make = "gmake"

    # Make sure tools exist
    subprocess.check_call(
        [make, "-s", "-C", "tools/", "n64graphics", "skyconv", "mio0", "aifc_decode"]
    )

    # Go through the assets in roughly alphabetical order (but assets in the same
    # mio0 file still go together).
    keys = sorted(list(todo.keys()), key=lambda k: todo[k][0][0])

    # Import new assets
    for key in keys:
        assets = todo[key]
        lang, mio0 = key
        if mio0 == "@sound":
            rom = roms[lang]
            args = [
                "python3",
                "tools/disassemble_sound.py",
                "baserom." + lang + ".z64",
            ]
            def append_args(key):
                size, locs = asset_map["@sound " + key + " " + lang]
                offset = locs[lang][0]
                args.append(str(offset))
                args.append(str(size))
            append_args("ctl")
            append_args("tbl")
            if lang == "sh":
                args.append("--shindou-headers")
                append_args("ctl header")
                append_args("tbl header")
            args.append("--only-samples")
            for (asset, pos, size, meta) in assets:
                print("extracting", asset)
                target = output_root / asset
                target.parent.mkdir(parents=True, exist_ok=True)
                args.append(tool_output_path(target) + ":" + str(pos))
            subprocess.run(args, check=True)
            continue

        if mio0 is not None:
            image = subprocess.run(
                [
                    "./tools/mio0",
                    "-d",
                    "-o",
                    str(mio0),
                    "baserom." + lang + ".z64",
                    "-",
                ],
                check=True,
                stdout=subprocess.PIPE,
            ).stdout
        else:
            image = roms[lang]

        for (asset, pos, size, meta) in assets:
            print("extracting", asset)
            input = image[pos : pos + size]
            target = output_root / asset
            os.makedirs(target.parent, exist_ok=True)
            if asset.endswith(".png"):
                png_file = tempfile.NamedTemporaryFile(prefix="asset", delete=False)
                try:
                    png_file.write(input)
                    png_file.flush()
                    png_file.close()
                    if asset.startswith("textures/skyboxes/") or asset.startswith("levels/ending/cake"):
                        if asset.startswith("textures/skyboxes/"):
                            imagetype = "sky"
                        else:
                            imagetype =  "cake" + ("-eu" if "eu" in asset else "")
                        subprocess.run(
                            [
                                "./tools/skyconv",
                                "--type",
                                imagetype,
                                "--combine",
                                png_file.name,
                                str(target),
                            ],
                            check=True,
                        )
                    else:
                        w, h = meta
                        fmt = asset.split(".")[-2]
                        subprocess.run(
                            [
                                "./tools/n64graphics",
                                "-e",
                                png_file.name,
                                "-g",
                                str(target),
                                "-f",
                                fmt,
                                "-w",
                                str(w),
                                "-h",
                                str(h),
                            ],
                            check=True,
                        )
                finally:
                    png_file.close()
                    os.remove(png_file.name)
            else:
                with open(target, "wb") as f:
                    f.write(input)

    # Remove old assets
    for asset in previous_assets:
        if asset not in new_assets:
            try:
                remove_file(asset, output_root)
            except FileNotFoundError:
                pass

    # Replace the asset list
    output = "\n".join(
        [
            "# This file tracks the assets currently extracted by extract_assets.py.",
            str(new_version),
            *sorted(list(new_assets)),
            "",
        ]
    )
    output_root.mkdir(parents=True, exist_ok=True)
    with open(output_root / ".assets-local.txt", "w") as f:
        f.write(output)
    if options.path_list is not None:
        write_path_list(options.path_list.resolve(), output_root, asset_map, langs)


if __name__ == "__main__":
    main()
