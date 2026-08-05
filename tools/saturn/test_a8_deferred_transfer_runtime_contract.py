#!/usr/bin/env python3
"""Regression contracts for A8's deferred VDP1 transfer integration.

These checks protect four runtime-visible boundaries that are not exercised by
the frame-bank unit alone: queue service cadence, fail-closed presentation after
a partial destination write, camera/generation coherence, and honest wait/stall
telemetry.  They intentionally fail until the A8 review repairs land.
"""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
SOURCEBOOT_C = REPO_ROOT / "src" / "port" / "saturn" / "sourceboot" / "main.c"
FRAME_BANK_H = (
    REPO_ROOT / "src" / "port" / "saturn" / "gfx" / "saturn_vdp1_frame_bank.h"
)


def extract_c_function(text: str, name: str) -> str:
    match = re.search(rf"\b{name}\s*\([^)]*\)\s*\{{", text)
    if match is None:
        raise AssertionError(f"function {name} not found")
    depth = 0
    for index in range(match.end() - 1, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[match.start() : index + 1]
    raise AssertionError(f"function {name} is not terminated")


class A8DeferredTransferRuntimeContractTests(unittest.TestCase):
    def setUp(self) -> None:
        self.source = SOURCEBOOT_C.read_text(encoding="utf-8")
        self.frame_bank_header = FRAME_BANK_H.read_text(encoding="utf-8")
        self.main = extract_c_function(self.source, "main")

    def test_stale_iterations_progress_pending_transfers_before_vblank_wait(self) -> None:
        """A serial CPU-DMAC/SCU-DMA pair must not advance only once per field."""
        loop_start = self.main.index("for (;;)")
        stale_if = self.main.index(
            "if (scheduler_now == sourceboot_presentation_generation)", loop_start
        )
        stale_wait = self.main.index(
            "sm64_saturn_source_runtime_wait_vblank();", stale_if
        )
        before_stale_wait = self.main[loop_start:stale_wait]
        stale_branch_before_wait = self.main[stale_if:stale_wait]

        self.assertIn(
            "sm64_saturn_vdp1_frame_bank_poll_transfers(",
            before_stale_wait,
            "pending transfers must be polled before the stale-field VBlank wait",
        )
        self.assertIn(
            "sourceboot_vdp1_transfer_pending",
            stale_branch_before_wait,
            "the stale path must distinguish an in-flight transfer from idle",
        )
        self.assertIn(
            "continue;",
            stale_branch_before_wait,
            "an incomplete transfer must re-enter the loop for another poll before VBlank",
        )

    def test_partial_destination_failure_poison_blocks_old_bank_presentation(self) -> None:
        """Old metadata must never plot over a partially overwritten VDP1 bank."""
        self.assertRegex(
            self.source,
            r"static\s+bool\s+sourceboot_vdp1_destination_poisoned\s*;",
            "sourceboot needs a persistent fail-closed destination-poison state",
        )

        quarantine = self.main.index(
            "SM64_SATURN_VDP1_FRAME_BANK_QUARANTINED"
        )
        quarantine_exit = self.main.index(
            "sourceboot_vdp1_transfer_pending = NULL;", quarantine
        )
        self.assertIn(
            "sourceboot_vdp1_destination_poisoned = true;",
            self.main[quarantine:quarantine_exit],
            "a failed transfer must poison the shared resident destination before clearing ownership",
        )

        presentation = self.main.index("const uint32_t presentation_generation =")
        presentation_call = self.main.index("sourceboot_present_generation(", presentation)
        self.assertIn(
            "!sourceboot_vdp1_destination_poisoned",
            self.main[presentation:presentation_call],
            "presentation must remain disabled after any partial resident-bank write",
        )

    def test_vdp2_camera_is_captured_with_the_published_vdp1_generation(self) -> None:
        """A later simulation tick cannot change the camera paired with old geometry."""
        self.assertIn(
            "sm64_saturn_vdp2_camera_snapshot_t camera_snapshot;",
            self.frame_bank_header,
            "each frame bank must own the immutable camera snapshot for its generation",
        )
        self.assertIn(
            "sm64_saturn_vdp1_frame_bank_set_camera_snapshot(",
            self.frame_bank_header,
            "camera capture must be an explicit BUILDING-bank operation",
        )

        camera = extract_c_function(self.source, "sourceboot_vdp2_camera_snapshot")
        self.assertNotIn(
            "sourceboot_mario_snapshot",
            camera,
            "VDP2 presentation must not reread the mutable simulation snapshot",
        )
        self.assertIn(
            "bank->camera_snapshot",
            camera,
            "VDP2 must consume the camera stored on the same published bank as VDP1",
        )

        signature = re.search(
            r"sourceboot_present_generation\s*\(([^)]*)\)", self.source
        )
        self.assertIsNotNone(signature)
        assert signature is not None
        self.assertIn(
            "sm64_saturn_vdp1_frame_bank_t",
            signature.group(1),
            "the terminal presentation boundary must receive the published bank, not only an integer",
        )

    def test_wait_and_queue_stall_telemetry_reports_only_real_waits(self) -> None:
        """Nonblocking submit/poll time is not a wait, and one stall is counted once."""
        terminal = extract_c_function(self.source, "sourceboot_present_generation")
        for field in (
            "vdp1_terminal_fence_wait_ticks_last",
            "vdp1_terminal_fence_wait_ticks_accum",
        ):
            self.assertRegex(
                terminal,
                rf"{field}\s*=\s*0U\s*;",
                f"{field} must be explicitly zero when the terminal boundary does not wait",
            )

        for field in (
            "command_cpu_dmac_wait_ticks_last",
            "command_cpu_dmac_wait_ticks_accum",
            "gouraud_scu_dma_wait_ticks_last",
            "gouraud_scu_dma_wait_ticks_accum",
        ):
            self.assertRegex(
                self.main,
                rf"{field}\s*=\s*0U\s*;",
                f"{field} must be explicitly zero on the ordinary nonblocking path",
            )

        increment = "sourceboot_vdp1_transfer_queued_not_started++;"
        self.assertEqual(
            self.main.count(increment),
            1,
            "one submitted transfer may contribute at most one queue-not-started event",
        )
        increment_at = self.main.index(increment)
        guard_window = self.main[max(0, increment_at - 500) : increment_at]
        self.assertIn(
            "!saturn_dma_queue_sequence_started(",
            guard_window,
            "queue-not-started must increment only after the start-state query reports blocked",
        )
        self.assertIn(
            "!saturn_dma_queue_sequence_retired(",
            guard_window,
            "an already-retired transfer is not a queue-start stall",
        )

    def test_target_vdp1_address_is_explicitly_converted_to_pointer(self) -> None:
        """Yaul exposes VDP1_VRAM as an integer address on the SH-2 target."""
        self.assertIn(
            ".command_vram = (void *)(uintptr_t)VDP1_VRAM(0),",
            self.main,
            "the transfer descriptor must compile under the target's integer-address macro",
        )


if __name__ == "__main__":
    unittest.main()
