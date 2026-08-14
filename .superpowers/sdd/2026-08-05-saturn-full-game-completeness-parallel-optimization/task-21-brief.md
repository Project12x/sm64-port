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
- [x] Cold boot and explicit recovery only: stage validation, generic SNDOFF,
  bounded stopped wait, 512-KiB mode, manifest-owner callbacks for clear/
  copy/mailbox, explicit barrier, generic SNDON, bounded READY plus advancing
  heartbeat.
- [x] Named fail-closed state covers bad configuration, staging, memory mode,
  command rejection, stop timeout, clear/copy/mailbox failure, and READY/
  heartbeat timeout. OREG31 is telemetry, never a boolean success shortcut.
- [x] Soundtest invokes/verifies the 512-KiB callback after SNDOFF and retains
  persistent typed-command diagnostics. The host seam preserves raw `0x00`
  and `0xFF` OREG31 values without boolean interpretation.
- [x] Static ownership rejects warned calls and requires the wrapper as the
  sole production owner of `smpc_smc_call`.
- [x] Serialized `verify-audio-sound-cpu-boot`, `verify-soundtest-boot`,
  `verify-pcm-protocol`, and `verify-audio-protocol-v2` pass.
- [ ] Sourceboot service/loader, real S64A/S64P residency, completion identity,
  production MC68000 linkage, target/Ymir/hardware/manual/audible evidence.

## Wave 2 host completion/ack ABI

Status: source-complete pending independent review for the bounded SH-2 host
ABI/transport slice only.

- [x] Reserve header offsets `0x4026..0x403f` and a 32-by-16-byte completion
  ring at `0x4240..0x443f`; preserve every existing command record word.
- [x] Return pointer-free `{source ring, two-lap cursor, opcode}` tickets and
  refuse a cursor's ABA reuse until its terminal completion retires it.
- [x] Poll completion records in order with exact ring/opcode/lap validation,
  explicit ACCEPTED, REJECTED, STALE, FAULT, DROPPED_SFX, FINISHED, PREPARED,
  and COMMITTED values, and full 32-bit big-endian generation fields.
- [x] Require the completion capability for ticketed enqueue/poll, bound status
  reads with the write-in-progress/stable-flags contract, saturate 16-bit
  producer telemetry, and reserve the FIFO's last eight slots for required
  acknowledgments under nonessential SFX pressure. These are host ABI rules;
  no MC68000 publisher is linked or claimed.
- [x] Keep PLAY_REFRESH word 3 as its existing 16-bit package epoch. Generation
  zero is reserved, boot starts with generation 1, `0xffff` is the final valid
  epoch, and typed enqueue rejects any larger 32-bit value without publishing
  bytes or advancing a cursor.
- [ ] Asynchronous FINISHED source/sequence/player identity does not fit this
  command-ack record. A later versioned tagged semantic-event extension must
  carry that identity before FINISHED can mutate source policy. Production
  MC68000 publication, source service, package commit, target/Ymir/manual/
  audible evidence remain open.

### Wave 2 fix round 1

- [x] Pending two-lap entries store the exact expected opcode, not a boolean;
  a wrong same-class completion cannot advance the completion consumer or
  retire the ticket.
- [x] Legacy control/SFX enqueue is explicitly no-ack only when completion
  capability is absent. Capability mode rejects it before any wire write and
  requires every command to reserve a ticket.
- [x] Split status publication uses an advancing encoded sequence:
  stable-even -> writing-odd -> next stable-even. The bounded reader requires
  the same stable-even value before and after; an observer-driven interleaving
  proves a mixed generation is retried rather than accepted.
  Bits 2..15 hold the sequence, bit 1 is the odd/write marker, and the producer
  contract forbids wrapping 16384 publications within one three-attempt read.
- [x] Live control/SFX saturation counters call the saturating helper and stay
  at `0xffff` under another full-ring enqueue.
- [x] One legality matrix is shared by completion polling and required-capacity
  classification. FINISHED is illegal in this command-ack wave, DROPPED_SFX is
  PLAY_REFRESH-only, PREPARED/COMMITTED are exact package opcode terminals,
  and package prepare/commit cannot use generic ACCEPTED.
- [x] The six focused DLL/MSYS-preflight host gates pass serially, including
  publication order and sound-CPU ownership 2/2.
- [x] Scoped fix-round rereviews pass SPEC/QUALITY PASS, C0/I0/M0. No
  production or audible gate changes.

### Wave 2 fix round 2

- [x] Completion-mode/no-ack mixing is prohibited at the producer boundary;
  a direct memory snapshot proves rejection leaves command bytes and producer
  cursor unchanged.
- [x] An unmatched, malformed, or duplicate completion latches a transport
  fault. Ticketed enqueue remains fail-closed until explicit recovery, so a
  delayed old completion cannot later match a reused ring/cursor/opcode.
- [x] Capability-absent legacy tests continue to prove the no-ack compatibility
  path; completion-aware tests advertise capability and use ticket APIs only.
- [x] Fix-round-2 primary and backup rereviews pass SPEC/QUALITY PASS, C0/I0/M0;
  all production/audible gates stay open.
