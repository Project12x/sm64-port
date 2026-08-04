#!/usr/bin/env python3
import unittest

from verify_pcm68k_zero_map import ZeroMapContractError, verify_disassembly


VALID = """
000004ee <pcm68k_main>:
 50e: lea 8b8 <sm64_saturn_pcm68k_consume_mapped_zero>,%a5
 524: jsr %a5@
0000088c <sm64_saturn_pcm68k_consume>:
 890: tstl %a0
 892: beqs 8a6
 89c: jsr 610 <sm64_saturn_pcm68k_consume_internal>
000008b8 <sm64_saturn_pcm68k_consume_mapped_zero>:
 8b8: movel %sp@(8),%sp@-
 8bc: movel %sp@(8),%sp@-
 8c0: clrl %sp@-
 8c2: jsr 610 <sm64_saturn_pcm68k_consume_internal>
000008cc <next>:
"""


class ZeroMapAssemblyTests(unittest.TestCase):
    def test_accepts_explicit_zero_mapped_target_entry(self) -> None:
        verify_disassembly(VALID)

    def test_rejects_main_calling_null_rejecting_api(self) -> None:
        bad = VALID.replace("sm64_saturn_pcm68k_consume_mapped_zero>,%a5",
                            "sm64_saturn_pcm68k_consume_scsp>,%a5", 1)
        with self.assertRaisesRegex(ZeroMapContractError, "pcm68k_main"):
            verify_disassembly(bad)

    def test_rejects_zero_entry_with_early_null_branch(self) -> None:
        bad = VALID.replace(" 8bc: movel %sp@(8),%sp@-\n",
                            " 8bc: tstl %a0\n 8be: beqs 8ca\n")
        with self.assertRaisesRegex(ZeroMapContractError, "rejects address zero"):
            verify_disassembly(bad)


if __name__ == "__main__":
    unittest.main()
