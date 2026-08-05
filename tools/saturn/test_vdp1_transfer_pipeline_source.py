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
            ("src/port/saturn/gfx/saturn_demo_render.c", "sm64_saturn_demo_render_frame"),
            ("src/port/saturn/gfx/saturn_fast3d_vdp1_emit.c", "sm64_saturn_fast3d_vdp1_emit"),
        ):
            body = function_body(path, name)
            self.assertNotIn("sm64_saturn_gouraud_transfer_submit", body)
            self.assertNotIn("sm64_saturn_vdp1_backend_upload", body)
            self.assertNotRegex(body, r"vdp1_sync_wait\s*\(\s*\)\s*;[\s\S]*?saturn_dma_queue_kick[\s\S]*?saturn_dma_queue_wait")

    def test_sourceboot_arms_resident_list_once_after_safe_transfer(self):
        body = function_body("src/port/saturn/sourceboot/main.c", "main")
        poll = body.find("sm64_saturn_vdp1_frame_bank_poll_transfers")
        arm = body.find("vdp1_sync_force_put()")
        publish = body.find("sm64_saturn_vdp1_frame_bank_publish")
        safe = body.rfind("vdp1_sync_wait()")
        submit = body.find("sm64_saturn_vdp1_frame_bank_submit_transfers")
        first_kick = body.find("sm64_saturn_vdp1_frame_bank_poll_transfers", poll + 1)
        self.assertTrue(0 <= poll < arm < publish < safe < submit < first_kick)
        self.assertEqual(body.count("sm64_saturn_vdp1_frame_bank_submit_transfers"), 1)
        self.assertEqual(body.count("vdp1_sync_force_put()"), 1)
        self.assertNotIn("sm64_saturn_vdp1_frame_bank_wait_for_publish", body)

    def test_cpu_dmac_channel_zero_has_one_frame_queue_owner(self):
        users = []
        for subtree in ("gfx", "gpl", "sourceboot"):
          for path in (ROOT / "src/port/saturn" / subtree).rglob("*.c"):
            if "cpu_dmac_transfer(0" in path.read_text(encoding="utf-8"):
                users.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(users, ["src/port/saturn/gpl/slavedriver_dma_queue.c"])


if __name__ == "__main__": unittest.main()
