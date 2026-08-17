import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def function_body(path: str, name: str) -> str:
    text = (ROOT / path).read_text(encoding="utf-8")
    match = re.search(rf"\b{name}\s*\([^;]*?\)\s*\{{", text, re.S)
    if match is None:
        raise AssertionError(f"missing function {name} in {path}")
    start = match.end() - 1
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{": depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0: return text[start:index + 1]
    raise AssertionError(f"unterminated function {name} in {path}")


def braced_block_after(text: str, pattern: str) -> str:
    match = re.search(pattern, text, re.S)
    if match is None:
        raise AssertionError(f"missing pattern {pattern!r}")
    start = text.find("{", match.end())
    if start < 0:
        raise AssertionError(f"missing braced block after {pattern!r}")
    depth = 0
    for index in range(start, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[start:index + 1]
    raise AssertionError(f"unterminated braced block after {pattern!r}")


class TransferPipelineSourceTests(unittest.TestCase):
    def test_both_emitters_are_construction_only(self):
        for path, name in (
            ("src/port/saturn/gfx/saturn_demo_render.c", "demo_render_finalize"),
            ("src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c", "sm64_saturn_fast3d_vdp1_emit"),
        ):
            body = function_body(path, name)
            self.assertNotIn("sm64_saturn_gouraud_transfer_submit", body)
            self.assertNotIn("sm64_saturn_vdp1_backend_upload", body)
            self.assertNotRegex(body, r"vdp1_sync_wait\s*\(\s*\)\s*;[\s\S]*?saturn_dma_queue_kick[\s\S]*?saturn_dma_queue_wait")

    def test_sourceboot_arms_resident_list_once_after_safe_transfer(self):
        path = "src/port/saturn/sourceboot/main.c"
        source = (ROOT / path).read_text(encoding="utf-8")
        transfer = function_body(path, "sourceboot_frame_poll_transfers")
        publish = function_body(path, "sourceboot_frame_publish")
        dispatch = function_body(path, "sourceboot_frame_pipeline_dispatch")

        busy = transfer.find("vdp1_sync_busy()")
        defer = transfer.find("sm64_saturn_frame_pipeline_transfer_deferred")
        submit = transfer.find("sm64_saturn_vdp1_frame_bank_submit_transfers")
        poll = transfer.find("sm64_saturn_vdp1_frame_bank_poll_transfers")
        busy_block = braced_block_after(transfer, r"if\s*\(\s*overwrite_busy\s*\)")
        arm = publish.find("sm64_saturn_vdp1_frame_bank_arm_resident_list")
        force = publish.find("vdp1_sync_force_put()")
        bank_publish = publish.find("sm64_saturn_vdp1_frame_bank_publish")
        self.assertTrue(0 <= busy < defer < submit < poll)
        self.assertEqual(transfer.count("vdp1_sync_busy()"), 1)
        self.assertIn("sm64_saturn_frame_pipeline_transfer_deferred", busy_block)
        self.assertIn("goto finish;", busy_block)
        # A busy observation leaves the exact READY bank owned by the frame
        # bank state machine.  The only allowed state transition here is the
        # scheduler acknowledgement; no transfer, bank-owner, poison, or
        # present path may mutate its ownership before the later-epoch retry.
        for forbidden in (
            "sm64_saturn_vdp1_frame_bank_submit_transfers",
            "sm64_saturn_vdp1_frame_bank_poll_transfers",
            "sm64_saturn_vdp1_frame_bank_quarantine",
            "sourceboot_vdp1_destination_poisoned",
            "sourceboot_vdp1_transfer_pending",
            "sourceboot_vdp1_render_ready",
            "sourceboot_vdp1_bank_submitted",
            "sourceboot_vdp1_transfer_faults",
            "sourceboot_vdp1_transfer_queued_not_started",
            "sourceboot_frame_reuse_previous",
            "sourceboot_present_generation",
            "vdp1_sync_force_put",
        ):
            with self.subTest(forbidden=forbidden):
                self.assertNotIn(forbidden, busy_block)
        self.assertNotIn("vdp1_sync_wait()", transfer)
        self.assertNotRegex(transfer, r"while\s*\(\s*vdp1_sync_busy\s*\(\s*\)\s*\)")
        self.assertNotIn("sourceboot_vdp1_fence_spin", source)
        self.assertTrue(0 <= arm < force < bank_publish)
        self.assertLess(
            dispatch.index("case SM64_SATURN_FRAME_POLL_TRANSFERS"),
            dispatch.index("case SM64_SATURN_FRAME_PUBLISH_FRAME"),
        )
        self.assertEqual(source.count("sm64_saturn_vdp1_frame_bank_submit_transfers"), 1)
        self.assertEqual(source.count("vdp1_sync_force_put()"), 1)
        self.assertNotIn("sm64_saturn_vdp1_frame_bank_wait_for_publish", source)

    def test_cpu_dmac_channel_zero_has_one_frame_queue_owner(self):
        users = []
        for subtree in ("gfx", "gpl", "sourceboot"):
          for path in (ROOT / "src/port/saturn" / subtree).rglob("*.c"):
            text = path.read_text(encoding="utf-8")
            self.assertNotIn("cpu_dmac_transfer(0", text)
            if "cpu_dmac_channel_config_set(" in text:
                users.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(users, ["src/port/saturn/gpl/slavedriver_dma_queue.c"])


if __name__ == "__main__": unittest.main()
