"""Sprint 2 T2.22 -- write-if-changed must not become silent staleness.

Two properties are load-bearing and they pull in opposite directions:

1.  Re-running a generator over unchanged inputs must NOT touch the output,
    because a fresh mtime restales every consumer and costs a recompile, a
    relink and a 52 MB `objdump -S` listing per make parse.
2.  A generator whose inputs really moved, or whose output on disk is wrong,
    MUST still rewrite it.  A skip-always implementation would ship a stale
    header while every gate stayed green -- strictly worse than the waste it
    replaces.

Property 2 is the one worth testing hardest, so it is exercised both at the
helper level and end to end through a real generator subprocess.
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from write_if_changed import (  # noqa: E402
    write_bytes_if_changed,
    write_text_if_changed,
)

TOOLS = Path(__file__).resolve().parent
GEO_DEPTH = TOOLS / "geo_depth_manifest.py"

# NTFS timestamps are fine-grained, but a same-tick rewrite would still make
# an mtime assertion flaky. Compare mtime_ns AND stamp the file into the past
# first, so "unchanged" is proven by the past stamp surviving.
PAST = 1_000_000_000


def _stamp_past(path: Path) -> int:
    os.utime(path, (PAST, PAST))
    return path.stat().st_mtime_ns


class TestWriteBytesIfChanged(unittest.TestCase):
    def test_missing_file_is_written(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "nested" / "payload.bin"
            self.assertTrue(write_bytes_if_changed(target, b"abc"))
            self.assertEqual(target.read_bytes(), b"abc")

    def test_identical_bytes_leave_the_mtime_alone(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "payload.bin"
            write_bytes_if_changed(target, b"abc")
            before = _stamp_past(target)
            self.assertFalse(write_bytes_if_changed(target, b"abc"))
            self.assertEqual(target.stat().st_mtime_ns, before)
            self.assertEqual(target.read_bytes(), b"abc")

    def test_changed_bytes_rewrite_and_advance_the_mtime(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "payload.bin"
            write_bytes_if_changed(target, b"abc")
            _stamp_past(target)
            self.assertTrue(write_bytes_if_changed(target, b"abd"))
            self.assertEqual(target.read_bytes(), b"abd")
            self.assertGreater(target.stat().st_mtime_ns, PAST * 1_000_000_000)

    def test_corrupted_output_is_repaired(self) -> None:
        """The silent-staleness guard: wrong bytes on disk must be replaced."""
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "payload.bin"
            write_bytes_if_changed(target, b"correct")
            target.write_bytes(b"corrupt")
            _stamp_past(target)
            self.assertTrue(write_bytes_if_changed(target, b"correct"))
            self.assertEqual(target.read_bytes(), b"correct")
            self.assertGreater(target.stat().st_mtime_ns, PAST * 1_000_000_000)

    def test_truncated_output_is_repaired(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "payload.bin"
            write_bytes_if_changed(target, b"correct")
            target.write_bytes(b"corr")
            self.assertTrue(write_bytes_if_changed(target, b"correct"))
            self.assertEqual(target.read_bytes(), b"correct")


class TestWriteTextIfChanged(unittest.TestCase):
    def test_default_newline_matches_write_text_bytes(self) -> None:
        """Callers that never pinned `newline` must keep platform endings."""
        with tempfile.TemporaryDirectory() as directory:
            reference = Path(directory) / "reference.txt"
            reference.write_text("a\nb\n", encoding="utf-8")
            target = Path(directory) / "target.txt"
            write_text_if_changed(target, "a\nb\n", encoding="utf-8")
            self.assertEqual(target.read_bytes(), reference.read_bytes())

    def test_pinned_lf_newline_matches_write_text_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            reference = Path(directory) / "reference.txt"
            reference.write_text("a\nb\n", encoding="utf-8", newline="\n")
            target = Path(directory) / "target.txt"
            write_text_if_changed(target, "a\nb\n", encoding="utf-8",
                                  newline="\n")
            self.assertEqual(target.read_bytes(), reference.read_bytes())
            self.assertNotIn(b"\r", target.read_bytes())

    def test_identical_text_leaves_the_mtime_alone_under_both_newlines(self) -> None:
        for newline in (None, "\n"):
            with self.subTest(newline=newline):
                with tempfile.TemporaryDirectory() as directory:
                    target = Path(directory) / "target.txt"
                    write_text_if_changed(target, "a\nb\n", newline=newline)
                    before = _stamp_past(target)
                    self.assertFalse(
                        write_text_if_changed(target, "a\nb\n", newline=newline))
                    self.assertEqual(target.stat().st_mtime_ns, before)

    def test_a_line_ending_difference_counts_as_a_difference(self) -> None:
        """CRLF on disk must not be accepted as matching an LF-pinned write."""
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "target.txt"
            target.write_bytes(b"a\r\nb\r\n")
            self.assertTrue(
                write_text_if_changed(target, "a\nb\n", newline="\n"))
            self.assertEqual(target.read_bytes(), b"a\nb\n")

    def test_changed_text_rewrites(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "target.txt"
            write_text_if_changed(target, "a\n", newline="\n")
            _stamp_past(target)
            self.assertTrue(write_text_if_changed(target, "b\n", newline="\n"))
            self.assertEqual(target.read_bytes(), b"b\n")


class TestGeneratorEndToEnd(unittest.TestCase):
    """The property the build depends on, through a real generator process."""

    def _source(self, directory: Path, nested: int) -> Path:
        path = directory / "geo.c"
        body = ["const GeoLayout probe_geo[] = {"]
        for _ in range(nested):
            body.append("    GEO_OPEN_NODE(),")
        for _ in range(nested):
            body.append("    GEO_CLOSE_NODE(),")
        body.append("    GEO_END(),")
        body.append("};")
        path.write_text("\n".join(body) + "\n", encoding="utf-8")
        return path

    def _run(self, source_dir: Path, out: Path) -> None:
        result = subprocess.run(
            [sys.executable, str(GEO_DEPTH),
             "--source-dir", str(source_dir),
             "--output-header", str(out / "manifest.h"),
             "--output-linker", str(out / "manifest.ld"),
             "--output-json", str(out / "manifest.json")],
            capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_rerun_is_idempotent_but_a_real_input_change_regenerates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            sources = root / "sources"
            sources.mkdir()
            out = root / "out"
            out.mkdir()
            self._source(sources, nested=3)

            self._run(sources, out)
            products = [out / "manifest.h", out / "manifest.ld",
                        out / "manifest.json"]
            first = {p: p.read_bytes() for p in products}
            stamps = {p: _stamp_past(p) for p in products}

            # 1. Re-running over unchanged inputs must touch nothing at all.
            self._run(sources, out)
            for path in products:
                self.assertEqual(path.stat().st_mtime_ns, stamps[path],
                                 f"{path.name} was rewritten with equal bytes")
                self.assertEqual(path.read_bytes(), first[path])

            # 2. A genuine input change must still regenerate. The header and
            #    the JSON report bind both the measured depth and a digest of
            #    the inputs, so a deeper source moves their emitted bytes.
            self._source(sources, nested=9)
            self._run(sources, out)
            for path in (out / "manifest.h", out / "manifest.json"):
                self.assertGreater(path.stat().st_mtime_ns,
                                   PAST * 1_000_000_000,
                                   f"{path.name} was not regenerated")
                self.assertNotEqual(path.read_bytes(), first[path],
                                    f"{path.name} content did not move")
            report = json.loads((out / "manifest.json").read_text(
                encoding="utf-8"))
            self.assertEqual(report["max_proven_depth"], 9)

            # The linker fragment carries only the aligned capacity, which is
            # allowed to survive a depth change. Not rewriting it is correct,
            # not stale -- prove that by regenerating into a virgin directory
            # and comparing bytes.
            virgin = root / "virgin"
            virgin.mkdir()
            self._run(sources, virgin)
            self.assertEqual((out / "manifest.ld").read_bytes(),
                             (virgin / "manifest.ld").read_bytes())

            # 3. A corrupted product must be repaired even though the inputs
            #    did not move. This is the silent-staleness case.
            header = out / "manifest.h"
            good = header.read_bytes()
            header.write_bytes(b"/* clobbered */\n")
            _stamp_past(header)
            self._run(sources, out)
            self.assertEqual(header.read_bytes(), good)
            self.assertGreater(header.stat().st_mtime_ns,
                               PAST * 1_000_000_000)


if __name__ == "__main__":
    unittest.main()
