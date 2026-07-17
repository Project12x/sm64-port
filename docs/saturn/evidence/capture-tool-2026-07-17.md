# Reproducible Kronos capture helper — 2026-07-17

`tools/saturn/capture_kronos.ps1` wraps RetroArch’s bounded-frame screenshot
mode and emits a JSON manifest containing the core, options, game, BIOS, ISO,
and screenshot SHA-256 values. It is intended to make future portfolio and
retail-comparison captures repeatable.

A five-frame smoke run completed successfully against the expanded hwtest:

```text
evidence_kind: retroarch-emulator
core_name: Kronos
frames: 5
screenshot SHA-256: aad8fc58631dba8ccb9cf1b4ef5d50968ec6f729f42199beaceb071e524c3199
manifest SHA-256: 20c5c2c86f50b64d1f3c3483296b042eab917844b9f0bf5bd33e67aa9e5157a3
```

The smoke image is not a hardware-result capture; it verifies the helper’s
process, screenshot, and provenance-manifest contract.
