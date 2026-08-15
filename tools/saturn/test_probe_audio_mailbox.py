#!/usr/bin/env python3
"""Pin probe_audio_mailbox's pure peek-normalization and word decoding."""
from __future__ import annotations

import unittest

import probe_audio_mailbox as probe


class _FakeClient:
    def __init__(self, value):
        self.value = value
        self.calls = []

    def call(self, method, params):
        self.calls.append((method, params))
        return self.value


class ProbeAudioMailboxTest(unittest.TestCase):
    def test_peek_normalizes_list_nested_and_hex_payloads(self) -> None:
        for payload, expected in (
            ({"data": [1, 2, 3]}, [1, 2, 3]),
            ({"data": {"data": [4, 5]}}, [4, 5]),
            ({"data": "0a0bff"}, [0x0A, 0x0B, 0xFF]),
        ):
            with self.subTest(payload=payload):
                client = _FakeClient(payload)
                self.assertEqual(probe.peek(client, 0x25A04000, 0x100), expected)
                self.assertEqual(
                    client.calls,
                    [("mem.peek", {"address": 0x25A04000, "count": 0x100})],
                )

    def test_be16_decodes_big_endian_words(self) -> None:
        data = [0x12, 0x34, 0xAB, 0xCD]
        self.assertEqual(probe.be16(data, 0), 0x1234)
        self.assertEqual(probe.be16(data, 2), 0xABCD)


if __name__ == "__main__":
    unittest.main()
