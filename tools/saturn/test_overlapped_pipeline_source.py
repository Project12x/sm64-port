#!/usr/bin/env python3
"""Source contracts for the Task 10 hardened sourceboot candidate."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MAKEFILE = ROOT / "src" / "port" / "saturn" / "sourceboot" / "Makefile"


class OverlappedPipelineSourceContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.makefile = MAKEFILE.read_text(encoding="utf-8")

    def test_pipeline_selector_is_validated_and_reaches_the_sh_compile(self) -> None:
        self.assertRegex(
            self.makefile,
            r"SATURN_RENDERER_PIPELINE\s*\?=\s*2",
            "the sourceboot default pipeline must remain the accepted baseline",
        )
        self.assertRegex(
            self.makefile,
            r"filter\s+\$\(SATURN_RENDERER_PIPELINE\),\s*2\s+3\s+4",
            "Task 10 must reject unreviewed pipeline selectors",
        )
        self.assertRegex(
            self.makefile,
            r"-DSATURN_RENDERER_PIPELINE=\$\(SATURN_RENDERER_PIPELINE\)",
            "the output-directory pipeline tag must match the compiled feature",
        )
        self.assertGreaterEqual(
            len(re.findall(r"SATURN_RENDERER_PIPELINE", self.makefile)),
            3,
        )

    def test_sourceboot_links_the_scene_neutral_overlap_components(self) -> None:
        required_sources = (
            "saturn_frame_pipeline.c",
            "saturn_render_overlap_phase.c",
            "saturn_render_snapshot.c",
            "saturn_render_job_queue.c",
            "saturn_render_job_runtime.c",
            "saturn_render_output_bank.c",
            "saturn_gouraud_transfer.c",
            "saturn_vdp1_frame_bank.c",
            "saturn_vdp2_frame.c",
            "slavedriver_dma_queue.c",
            "slavedriver_dual_worker.c",
            "ztreme_frustum.c",
        )
        for source_name in required_sources:
            self.assertIn(source_name, self.makefile, source_name)

    def test_pipeline_tag_is_not_the_only_pipeline_contract(self) -> None:
        cflags_start = self.makefile.index("SH_CFLAGS +=")
        cflags = self.makefile[cflags_start:]
        self.assertIn("SATURN_RENDERER_PIPELINE", cflags)
        self.assertNotIn(
            "SATURN_RENDERER_PIPELINE=0",
            cflags,
            "the selector must remain an explicit build input, not a disabled stub",
        )


if __name__ == "__main__":
    unittest.main()
