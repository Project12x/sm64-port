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
screenshot SHA-256: 68acc1581b497b709b197b2e5037e6cbc2ef9b5c318e60b30393df4d5d5b544d
manifest SHA-256: 0a7c9b64119fd8e5bfb7e741f84aab81178d76ed49d3cdef2760bac4d54025b0
```

The smoke image is not a hardware-result capture; it verifies the helper’s
process, screenshot, and provenance-manifest contract.
