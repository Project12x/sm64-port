# Task 21 Wave 0/1 brief — project-owned sound-CPU boot contract

Status: bounded source/host slice only. This wave creates one project-owned
wrapper around Yaul's generic SMPC command boundary and a fail-closed cold
boot/recovery model. It does not link sourceboot semantic audio, load real
packages, change residency, link the production MC68000 semantic engine, build
a target, run Ymir/hardware, or claim audible output.

## Reference-code-first record

- `yaul-org/libyaul@6012f79f237773378c8014e70d8998ad95a38d98`,
  MIT: inspected `smpc/smc.h:93-102,199-215`. Reuse mode: dependency/API
  boundary. The wrapper calls only generic `smpc_smc_call`; warned convenience
  SNDON/SNDOFF wrappers are forbidden.
- `ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0`,
  MIT: inspected `PROJ/main.c:507-577` and `jo_demo/pcmsys.c:218-272`.
  Reuse mode: pattern-only for 512-KiB mode and boot ordering. No code,
  mutable structure, scheduler, driver, or asset copied.
- `yaul-org/libyaul-examples@66b648eb059bb8bb7392eac70821605a68205b85`,
  license unclear at this pin: inspected
  `scsp-ponesound-pcm8/ponesound.c:63-92`. Reuse mode: validation/pattern-only;
  no source or binary copied.

## Bounded acceptance

- [x] RED proves the wrapper/header is absent and current production contains
  two warned convenience calls with no single generic-command owner.
- [x] Cold boot and explicit recovery only: stage validation, 512-KiB mode,
  generic SNDOFF, bounded stopped wait, manifest-owner callbacks for clear/
  copy/mailbox, explicit barrier, generic SNDON, bounded READY plus advancing
  heartbeat.
- [x] Named fail-closed state covers bad configuration, staging, memory mode,
  command rejection, stop timeout, clear/copy/mailbox failure, and READY/
  heartbeat timeout. OREG31 is telemetry, never a boolean success shortcut.
- [x] Static ownership rejects warned calls and requires the wrapper as the
  sole production owner of `smpc_smc_call`.
- [x] Serialized `verify-audio-sound-cpu-boot`, `verify-soundtest-boot`,
  `verify-pcm-protocol`, and `verify-audio-protocol-v2` pass.
- [ ] Sourceboot service/loader, real S64A/S64P residency, completion identity,
  production MC68000 linkage, target/Ymir/hardware/manual/audible evidence.
