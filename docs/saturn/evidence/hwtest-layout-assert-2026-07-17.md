# Telemetry layout assertion build — 2026-07-17

The hardware-test source now has compile-time assertions that the stable WRAM
telemetry block is exactly 64 bytes and the extended block is exactly 56 bytes.
This prevents Ymir and retail capture readers from silently drifting when the
field list changes.

## Build verification

Command (using the locally installed pinned SH-2/Yaul toolchain):

```text
make -f Makefile.saturn.mk hwtest verify-hwtest
```

Result:

```text
Verified ELF32 big-endian SH-2 hardware-test image at 0x06004000
```

## Artifact hashes

| Artifact | SHA-256 |
|---|---|
| `sm64-saturn-hwtest.elf` | `ae60779e104841f1d0bf006cabea1945bf1e7c122cbd86d85961e7b8249956d5` |
| `sm64-saturn-hwtest.bin` | `1fa20aace3165e8268e21c2ce123507939125e6370b9b687df544d3c2f657307` |
| `sm64-saturn-hwtest.iso` | `d3f1a3f2bfd89066bc14325745eae425783599a9e3eda2d4c8bdb1dad92a93f7` |

The binary and ISO hashes remain identical to the BIOS-backed visible-probe
capture. Only the debug ELF changed because the source assertion was added;
the existing screenshot remains valid for the unchanged disc image.
