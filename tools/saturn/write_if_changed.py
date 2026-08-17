"""Publish generator output only when the bytes actually change.

Sprint 2 T2.22.  Several Saturn asset generators are re-run on every make
parse by design: their targets carry PHONY prerequisites so a stale package
generation, a renamed geo source, or a re-extracted asset can never survive
into a build.  Re-running them is cheap.  *Rewriting* their outputs is not.

Every rewrite gave the file a fresh mtime, which restaled each translation
unit that included it, which forced a recompile, a relink, an `nm`, and a
52,289,956-byte `objdump -S` listing.  One build did that four times and
produced byte-identical output every time -- roughly 510 s of a 695 s
`-j12` build, and the same waste in a serial build.  See
`docs/saturn/evidence/reports/sprint2-t2_18-parallel-build-identity.md`
section 6 and `sprint2-t2_22-build-relink-loop.md`.

Comparing content before writing keeps the regeneration guarantee.  The
generator still runs, still recomputes from live inputs, and still repairs
an output whose bytes on disk are wrong; only the redundant write is
skipped.  This is deliberately *not* an order-only prerequisite and *not* a
dropped dependency edge: a genuine input change still yields different
bytes, still rewrites the file, and still triggers the downstream recompile
and relink.

The write path delegates to the caller's original `Path.write_text` /
`Path.write_bytes` semantics, so the bytes published here are exactly the
bytes the generator published before -- including platform line-ending
translation for callers that did not pin `newline`.
"""

from __future__ import annotations

import os
from pathlib import Path

__all__ = ["write_bytes_if_changed", "write_text_if_changed"]


def write_bytes_if_changed(path: Path, payload: bytes) -> bool:
    """Write `payload` to `path` only when it differs.

    Returns True when the file was written.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    try:
        if path.read_bytes() == payload:
            return False
    except OSError:
        pass
    path.write_bytes(payload)
    return True


def write_text_if_changed(path: Path, text: str, *,
                          encoding: str = "utf-8",
                          newline: str | None = None) -> bool:
    """`Path.write_text` that skips the write when the file already matches.

    `newline` carries the meaning it has for `open()`, so a caller that
    previously wrote with the default (platform line endings) keeps writing
    platform line endings, and a caller that pinned `newline="\\n"` keeps
    writing LF.  The comparison applies the identical translation, so the
    file is judged unchanged only when the bytes on disk are byte-for-byte
    what this call would have written.

    Returns True when the file was written.
    """
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if newline is None:
        translated = text.replace("\n", os.linesep)
    elif newline == "":
        translated = text
    else:
        translated = text.replace("\n", newline)
    try:
        if path.read_bytes() == translated.encode(encoding):
            return False
    except (OSError, UnicodeEncodeError):
        pass
    path.write_text(text, encoding=encoding, newline=newline)
    return True
