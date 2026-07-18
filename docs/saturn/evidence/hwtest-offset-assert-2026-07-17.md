# Telemetry field-offset assertion build — 2026-07-17

The hardware-test source now locks both telemetry block sizes and selected
field offsets with C11 `_Static_assert`/`offsetof` checks. This prevents a
packing or compiler change from preserving the total 64/56-byte sizes while
silently shifting individual Ymir/retail fields.

Build command:

```text
make -f Makefile.saturn.mk hwtest verify-hwtest
```

Result:

```text
Verified ELF32 big-endian SH-2 hardware-test image at 0x06004000
```

| Artifact | SHA-256 |
|---|---|
| `sm64-saturn-hwtest.elf` | `494cc0c946bd50e3ab8c09028314d5e53c0aeaa19a9a3e8bcf736d53f330a302` |
| `sm64-saturn-hwtest.bin` | `059e9e45ffc0283b29aaf06ea83b3cab2b2a41d32dbe7fc09ae6fbfae5b4eba3` |
| `sm64-saturn-hwtest.iso` | `d36b3a0591d77da499ef73461018e789800d780d88edfe24f1372005a043009b` |
| `sm64-saturn-hwtest.cue` | `c0b93a455ca2e183ce7d3cc247a6f3eb53ec35ee909de70e43d52bd185d83462` |

The ISO hash is unchanged by the compile-time-only assertions, so the latest
BIOS-backed screenshot remains valid for the runtime image.
