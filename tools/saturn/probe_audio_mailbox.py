from __future__ import annotations

import json
import hashlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT / "tools" / "saturn"))
from capture_route_views import YmirClient
from capture_sourceboot_boot_trace import run_bios_handoff

ARTIFACT = ROOT / "build/saturn/sourceboot/e2-bob-identity-id-7deb747eb230b595"
YMIR = Path(r"D:\Code\RetroDev\sm64-saturn-port\ymir-agent\build-agent2\apps\ymir-headless\Release\ymir-headless.exe")
BIOS = Path(r"D:\Code\RetroDev\sm64-saturn-port\sm64-port\.ymir-profile\roms\ipl\Sega Saturn BIOS (USA).bin")

def peek(client, address, count):
    value = client.call("mem.peek", {"address": address, "count": count})
    data = value.get("data", value)
    if isinstance(data, dict):
        data = data.get("data", [])
    if isinstance(data, str):
        data = list(bytes.fromhex(data))
    return list(data)

def be16(data, off):
    return (data[off] << 8) | data[off + 1]

try:
    client = YmirClient(YMIR, BIOS, ARTIFACT / "sm64-saturn-sourceboot-e2.cue", 300.0)
    try:
        run_bios_handoff(client, lambda frames: client.call("exec.run_for", {"frames": frames}), lambda _label: None)
        client.call("exec.run_for", {"frames": 3600})
        mailbox = peek(client, 0x25A04000, 0x100)
        sfxb = peek(client, 0x25A05000, 0x600)
        scsp = peek(client, 0x25B00000, 0x200)
        scsp_master = peek(client, 0x25B00400, 0x2)
        diagnostics = peek(client, 0x25A07F00, 0x20)
        ring = []
        for slot in range(8):
            off = 0x40 + slot * 16
            ring.append({"slot": slot, "opcode": be16(mailbox, off), "words": [be16(mailbox, off + 2 + 2*i) for i in range(7)]})
        print(json.dumps({
            "mailbox_words": [be16(mailbox, 2*i) for i in range(0x40 // 2)],
            "service_tick": be16(mailbox, 0x3A),
            "active_voice_count": be16(mailbox, 0x3C),
            "music_diagnostics": {
                "starts": be16(diagnostics, 0),
                "faults": be16(diagnostics, 2),
                "vm_ticks": be16(diagnostics, 4),
                "active": be16(diagnostics, 6),
                "notes": be16(diagnostics, 8),
                "sequence_offset": be16(diagnostics, 10),
                "sequence_bytes": be16(diagnostics, 12),
                "reject_mask": be16(diagnostics, 14),
                "malformed": be16(diagnostics, 16),
                "dropped": be16(diagnostics, 18),
                "consume_fail": be16(diagnostics, 20),
                "scsp_fail": be16(diagnostics, 22),
                "last_note": be16(diagnostics, 24),
                "last_arg1": be16(diagnostics, 26),
                "last_arg0": be16(diagnostics, 28),
                "last_failure": be16(diagnostics, 30),
            },
            "sfxb_header": {
                "target_prefix_sha256": hashlib.sha256(bytes(sfxb[:1232])).hexdigest(),
                "local_prefix_sha256": hashlib.sha256((ROOT / "build/saturn/audio/generated/sourceboot-sfx/bob_sfx_metadata.bin").read_bytes()).hexdigest(),
                "magic": bytes(sfxb[0:4]).hex(),
                "version": be16(sfxb, 4),
                "header_bytes": be16(sfxb, 6),
                "generation": be16(sfxb, 8),
                "mapping_count": be16(sfxb, 10),
                "sample_count": be16(sfxb, 12),
                "mapping_offset": be16(sfxb, 14),
                "sample_offset": be16(sfxb, 16),
                "metadata_bytes": be16(sfxb, 18),
                "pcm_bytes": int.from_bytes(bytes(sfxb[20:24]), "big"),
                "sequence_offset": int.from_bytes(bytes(sfxb[24:28]), "big"),
                "sequence_bytes": be16(sfxb, 28),
                "music_sample_index": be16(sfxb, 30),
                "sample63": sfxb[be16(sfxb, 16) + 63*12:be16(sfxb, 16) + 64*12],
            },
            "control_ring": ring,
            "scsp_words": [be16(scsp, 2*i) for i in range(64)],
            "scsp_master": scsp_master,
        }, sort_keys=True))
    finally:
        client.shutdown()
except Exception as exc:
    print(type(exc).__name__ + ": " + str(exc))
    raise
