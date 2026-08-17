import unittest

from capture_prenotification_profile import vdp1_fence_summary


class Vdp1FenceSummaryTests(unittest.TestCase):
    def test_reports_busy_deferrals_and_status_without_wait_costs(self):
        summary = vdp1_fence_summary({
            "vdp1_fence_events": 8,
            "vdp1_fence_waits": 2,
            "vdp1_edsr_entry_last": 0x1234,
            "vdp1_edsr_cef_entry_count": 3,
            "vdp1_copr_entry_last": 0x5678,
            "vdp1_copr_exit_last": 0x9ABC,
            "vdp1_lopr_last": 0xDEF0,
        })

        self.assertEqual(summary["busy_deferrals"], 2)
        self.assertEqual(summary["deferral_share_of_events"], 0.25)
        self.assertEqual(summary["edsr_entry_last"], 0x1234)
        self.assertEqual(summary["edsr_cef_entry_count"], 3)
        self.assertEqual(summary["copr_entry_last"], 0x5678)
        self.assertEqual(summary["copr_exit_last"], 0x9ABC)
        self.assertEqual(summary["lopr_last"], 0xDEF0)
        for obsolete_key in (
            "waits",
            "wait_share_of_events",
            "mean_ticks",
            "mean_cycles",
            "mean_vblank_equiv",
            "max_ticks",
            "max_vblank_equiv",
            "mean_iterations",
            "max_raw_interval",
            "max_raw_interval_headroom",
        ):
            self.assertNotIn(obsolete_key, summary)

    def test_zero_events_have_no_deferral_or_cef_shares(self):
        summary = vdp1_fence_summary({
            "vdp1_fence_events": 0,
            "vdp1_fence_waits": 0,
            "vdp1_edsr_entry_last": 0,
            "vdp1_edsr_cef_entry_count": 0,
            "vdp1_copr_entry_last": 0,
            "vdp1_copr_exit_last": 0,
            "vdp1_lopr_last": 0,
        })

        self.assertIsNone(summary["deferral_share_of_events"])
        self.assertIsNone(summary["edsr_cef_entry_share"])


if __name__ == "__main__":
    unittest.main()
