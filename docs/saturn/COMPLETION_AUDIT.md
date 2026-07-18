# Saturn-port objective evidence matrix

This is the current requirement-by-requirement audit for the Saturn bootstrap,
hardware test, measurement disc, and host classifier work. Emulator evidence is
development evidence; a physical Saturn with the specified 4 MiB cartridge is
the authority for bus legality and final timings.

| Requirement | Current evidence | Status |
|---|---|---|
| One-command pinned toolchain bootstrap | [`BUILDING.md`](BUILDING.md), Docker wrappers, and local `verify-all` gate; Yaul Docker layout pinned in [`PROVENANCE.md`](PROVENANCE.md) | Local fallback gate verified; container execution awaits a Docker-capable host |
| C/SH-2 Saturn image compilation | [`hwtest-layout-assert-2026-07-17.md`](evidence/hwtest-layout-assert-2026-07-17.md); ELF verifier output at `0x06004000` | Verified locally with pinned `sh-elf-gcc`/Yaul install |
| Cartridge ID `0x5C` detection | [`src/port/saturn/hwtest/main.c`](../../src/port/saturn/hwtest/main.c) and visible Kronos capture | BIOS-backed emulator verified; retail pending |
| Destructive 4 MiB mapped-DRAM test | Exact ID-and-size gate precedes the full write/read pattern and visible PASS/FAIL state in `main.c` | Code/build verified; retail pending |
| Deterministic WRAM telemetry | Fixed 64-byte `SAT0` block, 56-byte `SATX` block, compile-time size/offset assertions ([offset build record](evidence/hwtest-offset-assert-2026-07-17.md)), strict version/magic and `0x5C`/4 MiB decoder gates, and raw Ymir export in [`telemetry_decode.py`](../../tools/saturn/telemetry_decode.py)/[`capture_hwtest.py`](../../tools/saturn/capture_hwtest.py) | Valid `SAT0`/`SATX` Ymir capture at `0x06030000`; retail pending |
| DMA measurements | CPU copy, cached/uncached reads, CPU-DMAC, SCU cart→WRAM, SCU WRAM→VDP1 fields | Emulator fields and the visible rejection path are captured; 0x5C cart/DMA measurements await retail |
| VDP1 measurements | Seven isolated probes: solid quad/triangle, concave, transparency, textured quad/triangle, Gouraud | Valid Ymir capture reports all seven mode bits (`0x7F`), 28 commands, and draw timing; retail pending |
| SM64 geometry/UV classifier | [`asset_classifier.py`](../../tools/saturn/asset_classifier.py), checked-out source scan and six-way report in [`asset-classifier-sm64-2026-07-17.md`](evidence/asset-classifier-sm64-2026-07-17.md), 8/8 regression tests | Verified host-side |
| BIOS-backed evidence | [`ymir-bios-hwtest-2026-07-17.md`](evidence/ymir-bios-hwtest-2026-07-17.md), raw 120-byte `SAT0`/`SATX` export, report, and 320x224 PNG; [Ymir CD-block handoff](YMIR_CD_BLOCK_HANDOFF.md) records the fixed bounded-run scheduling defect | Verified in Ymir with user-supplied USA BIOS |
| Retail evidence | [`RETAIL_CAPTURE.md`](RETAIL_CAPTURE.md) handoff with two cold-boot runs, raw 120-byte telemetry, hashes, and manifest schema | Pending physical console/cart access |

## Current conclusion

The software, build, measurement, classifier, and BIOS-backed emulator-evidence
portions are implemented and traceable. The project must not be marked fully
complete until the retail handoff produces a valid `SAT0`/`SATX` dump with cart
ID `0x5C`, complete status, and both cold-boot runs.
