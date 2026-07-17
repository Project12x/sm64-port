# Exact 4 MiB mapping-gate capture — 2026-07-17

The hardware-test image was rebuilt after adding an exact mapped-size guard:
the destructive pass now requires both cartridge ID `0x5C` and a reported
`0x00400000` byte mapping before it writes the test pattern.

![Kronos exact 4 MiB mapping-gate capture](screenshots/2026-07-17/hwtest-kronos-size-gate.png)

Kronos with the user-provided USA BIOS and the 4 MiB extended-RAM option
visibly reports `0x5C (4 MiB detected)` and `cart test: PASS`. This confirms the
new guard passes the intended configuration. The SCU-DMA line remains an
emulator observation (`FAIL` in this run); retail hardware is authoritative for
that behavior.

| Artifact | SHA-256 |
|---|---|
| Screenshot | `df90384033dfb2dc95e961898c56cddf33672b0f9aefd936845f7de2ff53a0fd` |
| Manifest | `8ab2d8671c73bf296688de6f62e57ee31470c726463c36d76f6d884004f44d50` |
| ISO | `d36b3a0591d77da499ef73461018e789800d780d88edfe24f1372005a043009b` |

The full RetroArch/Kronos provenance is in
`reports/hwtest-kronos-size-gate.manifest.json`.
