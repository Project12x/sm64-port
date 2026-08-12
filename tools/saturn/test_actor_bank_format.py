#!/usr/bin/env python3
"""Version-owned S64B host-parser contracts."""

from __future__ import annotations

import hashlib
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from actor_bank_format import (  # noqa: E402
    validate_actor_bank,
    validate_actor_bank_expected,
)
from compile_actor_bank import compile_mario_actor_bank  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "tools/saturn/manifests/actors/mario.json"
_MARIO_S64B_SHA256 = "242ecd7a91ddbfb49e65a0f04949168f1de9c24d66070c299b8889d6604ce539"


class ActorBankFormatTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.document, cls.mario = compile_mario_actor_bank(ROOT, MANIFEST)

    def test_historical_mario_v1_bytes_remain_accepted(self) -> None:
        """Fails if the version-owned v1 parser changes historical Mario bytes."""
        self.assertEqual(hashlib.sha256(self.mario).hexdigest(), _MARIO_S64B_SHA256)
        self.assertEqual(self.document["payload_sha256"], _MARIO_S64B_SHA256)
        view = validate_actor_bank(self.mario)
        self.assertEqual(view.version, 1)
        self.assertEqual(view.payload, self.mario)
        self.assertEqual(view.source_sha256, bytes.fromhex(self.document["source_sha256"]))
        self.assertEqual(validate_actor_bank_expected(self.mario, view.source_sha256), view)

    def test_unknown_versions_fail_at_version_dispatch(self) -> None:
        """Fails if an unknown S64B version reaches a format-specific parser."""
        for version in (0, 3):
            payload = self.mario[:4] + version.to_bytes(2, "big") + self.mario[6:]
            with self.subTest(version=version), \
                    self.assertRaisesRegex(ValueError, "unsupported S64B version"):
                validate_actor_bank(payload)

    def test_version_two_stops_at_its_named_unimplemented_boundary(self) -> None:
        """Fails if v2 is accidentally accepted before its contract exists."""
        v2 = self.mario[:4] + b"\x00\x02" + self.mario[6:]
        with self.assertRaisesRegex(ValueError, "S64B v2 contract is not implemented"):
            validate_actor_bank(v2)


if __name__ == "__main__":
    unittest.main()
