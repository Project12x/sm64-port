#!/usr/bin/env python3
import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from gen_pcm_proof_bank import generate_proof_bank


class PcmProofBankTests(unittest.TestCase):
    def test_generates_fixed_signed_pcm8_bank_and_manifest(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)
            manifest = generate_proof_bank(output)
            bank = (output / "pcm_proof_bank.bin").read_bytes()
            disk_manifest = json.loads(
                (output / "pcm_proof_bank.json").read_text(encoding="utf-8"))

            self.assertEqual(bank[:8], bytes([64] * 8))
            self.assertEqual(bank[1102:1110], bytes([48] * 7 + [208]))
            self.assertEqual(bank[2204:2212],
                             bytes([160, 161, 161, 161, 95, 95, 95, 161]))
            self.assertEqual(len(bank), 4408)
            self.assertEqual(hashlib.sha256(bank).hexdigest(),
                             "05b33bfb7518118b2ab1601e03bdfac65f3f470526cddc8284b3090122b35b58")
            self.assertEqual(manifest, disk_manifest)
            self.assertEqual([entry["offset"] for entry in manifest["samples"]],
                             [0, 1102, 2204])
            self.assertTrue(all(entry["offset"] % 2 == 0
                                for entry in manifest["samples"]))
            self.assertTrue(all(0 < entry["sample_count"] <= 65535
                                for entry in manifest["samples"]))
            self.assertLessEqual(manifest["bank_bytes"], 32768)
            self.assertEqual(manifest["encoding"], "signed-mono-pcm8")

    def test_repeated_generation_is_byte_identical(self) -> None:
        with tempfile.TemporaryDirectory() as first, \
             tempfile.TemporaryDirectory() as second:
            generate_proof_bank(Path(first))
            generate_proof_bank(Path(second))
            for name in ("pcm_proof_bank.bin", "pcm_proof_bank.h",
                         "pcm_proof_bank.json"):
                self.assertEqual((Path(first) / name).read_bytes(),
                                 (Path(second) / name).read_bytes())


if __name__ == "__main__":
    unittest.main()
