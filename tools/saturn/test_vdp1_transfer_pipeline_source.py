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

        safe = transfer.rfind("vdp1_sync_wait()")
        submit = transfer.find("sm64_saturn_vdp1_frame_bank_submit_transfers")
        poll = transfer.find("sm64_saturn_vdp1_frame_bank_poll_transfers")
        arm = publish.find("sm64_saturn_vdp1_frame_bank_arm_resident_list")
        force = publish.find("vdp1_sync_force_put()")
        bank_publish = publish.find("sm64_saturn_vdp1_frame_bank_publish")
        self.assertTrue(0 <= safe < submit < poll)
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
