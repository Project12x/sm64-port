# Saturn PCM/68K Audio Transport Prototype Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Prove an independently built MC68000 PCM service, bounded SH-2 command transport, and one audible generated sample in a standalone Saturn `soundtest` image without perturbing the FPS-critical sourceboot runtime.

**Architecture:** A freestanding 68K image owns four SCSP PCM slots and polls a fixed 16-bit mailbox/ring in sound RAM. The master SH-2 performs bounded enqueue only. A standalone `soundtest` boots the driver, copies a generated PCM bank, checks a heartbeat, and sends `PLAY`, `STOP_ALL`, and `SET_MASTER`; sourceboot integration is explicitly deferred until the owner approves promotion after the standalone gates pass.

**Tech Stack:** C11, GNU `m68keb-elf` freestanding toolchain, SH-2/Yaul, SCSP sound RAM/registers, Python 3 asset generation, host C contract tests, Ymir for emulator evidence.

## Global Constraints

- This is a research/prototype lane with a 5% capacity budget. FPS implementation and target captures take priority.
- Do not add the driver, bank, or transport to `sourceboot` in this plan. The last task produces an integration packet and stops.
- Do not use SM64's N64 synthesis loop, Sega's proprietary sound driver, SGL audio APIs, ADX, CDDA, streaming, music sequencing, positional sound, or SH-2 mixing/resampling.
- Initial samples are generated or public-domain. Do not commit extracted Nintendo audio; later baserom conversion must operate on user-supplied assets.
- SH-2 and 68K exchange only aligned big-endian 16-bit fields and sound-RAM offsets. They never exchange C pointers or compiler-dependent structs.
- Sound RAM map is fixed: `0x00000-0x03FFF` driver/vector/stack/BSS, `0x04000-0x04FFF` mailbox/ring/status/metadata, `0x05000-0x07FFF` reserve, `0x08000-0x7FFFF` PCM bank. Driver cap is 16 KiB; first bank cap is 32 KiB.
- The ring is 32 entries × 16 bytes. SH-2 writes payload then producer index; 68K consumes payload then writes consumer index.
- Never invoke cross tools bare from PowerShell. Run the complete build and inspection inside `C:/msys64/usr/bin/bash.exe -lc` with `PATH=/usr/bin:$PATH` and `TMPDIR=/tmp/sm64-saturn-$MSYSTEM` so every MSYS/GCC DLL remains discoverable.
- Pinned close-port reference: `ponut64/SCSP_poneSound@31782e4c61337327f23eb9aa45ecd37fe0944ea0`, MIT. Files inspected: `LICENSE`, `README.md`, `documentation.md`, `PROJ/main.c`, `PROJ/linker`, `PROJ/makefile`, `jo_demo/pcmsys.c`, and `jo_demo/pcmsys.h`. Permitted direct adaptations: vector/linker shape, SNDOFF/SNDON lifecycle, SCSP slot words, pitch calculation, and PCM metadata. Preserve the MIT text, copyright, upstream paths, pinned SHA, and change notes. Do not copy `sdrv.bin`.
- Existing libyaul `6012f79f237773378c8014e70d8998ad95a38d98` is MIT but has no complete SCSP driver; use it as the Saturn platform dependency. Jo Engine, Sonic Z-Treme, and SlaveDriver audio remain pattern-only for the reasons recorded in the approved design.

---

## Task 1: Freeze the Wire Protocol and Provenance

**Files:**

- Create: `src/port/saturn/audio/saturn_pcm_protocol.h`
- Create: `tools/saturn/pcm_protocol_test.c`
- Modify: `Makefile.saturn.mk`
- Create: `docs/saturn/audio/PONESOUND_MIT.txt`
- Create: `docs/saturn/audio/PCM68K_PROVENANCE.md`

- [ ] Copy the upstream MIT license text verbatim into `PONESOUND_MIT.txt`; record repository, pinned SHA, inspected files, reuse mode, and a list of changes in `PCM68K_PROVENANCE.md`.
- [ ] Add a failing host test that validates every byte offset, total size, opcode value, ring wrap, and sound-RAM region boundary without casting a shared byte buffer to a C struct.

```c
enum {
    SM64_SATURN_PCM_MAILBOX_OFFSET = 0x4000,
    SM64_SATURN_PCM_RING_OFFSET = 0x4040,
    SM64_SATURN_PCM_RING_COUNT = 32,
    SM64_SATURN_PCM_COMMAND_BYTES = 16,
    SM64_SATURN_PCM_BANK_OFFSET = 0x8000
};

typedef enum sm64_saturn_pcm_opcode {
    SM64_SATURN_PCM_NOP = 0,
    SM64_SATURN_PCM_PLAY = 1,
    SM64_SATURN_PCM_STOP_ALL = 2,
    SM64_SATURN_PCM_SET_MASTER = 3
} sm64_saturn_pcm_opcode_t;

void sm64_saturn_pcm_put_be16(volatile uint8_t *base,
                              uint16_t offset, uint16_t value);
uint16_t sm64_saturn_pcm_get_be16(const volatile uint8_t *base,
                                 uint16_t offset);
```

- [ ] Add `verify-pcm-protocol` to `Makefile.saturn.mk` and run it; expect compile failure before the header exists.
- [ ] Implement constants and byte-wise big-endian accessors. Add `_Static_assert` checks for non-overlap and exact ring capacity.
- [ ] Run `make -f Makefile.saturn.mk verify-pcm-protocol`; expect pass.
- [ ] Commit: `audio: define attributed PCM68K wire protocol`.

## Task 2: Build a Freestanding 68K Heartbeat Image

**Files:**

- Create: `src/port/saturn/audio68k/Makefile`
- Create: `src/port/saturn/audio68k/linker.ld`
- Create: `src/port/saturn/audio68k/start.S`
- Create: `src/port/saturn/audio68k/main.c`
- Create: `src/port/saturn/audio68k/scsp_regs.h`
- Create: `tools/saturn/verify_pcm68k_image.py`
- Modify: `tools/saturn/test_tools.py`

- [ ] Add Python fixtures for a valid ELF/map and failures for nonzero image base, driver end beyond `0x4000`, mailbox overlap, writable content in the PCM bank, and unresolved symbols.
- [ ] Run `./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_tools.py`; expect failure because the verifier does not exist.
- [ ] Adapt the minimal vector/reset/linker structure from PoneSound, with attribution comments at each closely ported block. Initialize the stack within the driver region, clear only declared BSS, then poll forever.
- [ ] Publish a 16-bit protocol magic, version, `BOOTING` state, and monotonically wrapping heartbeat in the mailbox. Do not touch SCSP slots yet.

```c
for (;;) {
    sm64_saturn_pcm_put_be16(sound_ram, SM64_SATURN_PCM_STATUS_OFFSET,
                            SM64_SATURN_PCM_STATUS_READY);
    heartbeat++;
    sm64_saturn_pcm_put_be16(sound_ram,
                            SM64_SATURN_PCM_HEARTBEAT_OFFSET, heartbeat);
}
```

- [ ] Build with `m68keb-elf-gcc -mc68000 -ffreestanding -fno-builtin -nostdlib`, emit ELF/BIN/MAP, and make `verify_pcm68k_image.py` enforce the 16 KiB cap and zero unresolved symbols.
- [ ] In the guarded MSYS2 shell run `make -C src/port/saturn/audio68k clean all verify`; expect pass. Inspect only from the same shell.
- [ ] Re-run the Python suite; expect pass.
- [ ] Commit: `audio: add freestanding 68K heartbeat image`.

## Task 3: Implement and Host-Test the SH-2 Ring Writer

**Files:**

- Create: `src/port/saturn/audio/saturn_pcm_transport.h`
- Create: `src/port/saturn/audio/saturn_pcm_transport.c`
- Create: `tools/saturn/pcm_transport_test.c`
- Modify: `Makefile.saturn.mk`

- [ ] Add fake-sound-RAM tests for empty/full rings, index wrap, payload-before-publication ordering, invalid opcodes, null base, drop count, and high-water count.

```c
typedef struct sm64_saturn_pcm_transport {
    volatile uint8_t *sound_ram;
    uint32_t enqueued;
    uint32_t dropped;
    uint16_t high_water;
} sm64_saturn_pcm_transport_t;

bool sm64_saturn_pcm_enqueue(sm64_saturn_pcm_transport_t *transport,
                            sm64_saturn_pcm_opcode_t opcode,
                            const uint16_t words[7]);
```

- [ ] Add `verify-pcm-transport` and run it; expect compile failure before the module exists.
- [ ] Implement one bounded occupancy check and at most seven payload-word stores. Use a compiler barrier before publishing the producer index. Do not spin, mix, convert samples, or copy banks.
- [ ] Run `verify-pcm-transport`; expect pass, including the mutation that publishes the producer index first.
- [ ] Commit: `audio: add bounded SH2 PCM command ring`.

## Task 4: Implement the 68K Ring Consumer and Four-Voice Allocator

**Files:**

- Modify: `src/port/saturn/audio68k/main.c`
- Create: `src/port/saturn/audio68k/pcm_voice.c`
- Create: `src/port/saturn/audio68k/pcm_voice.h`
- Create: `tools/saturn/pcm68k_model_test.c`
- Modify: `Makefile.saturn.mk`

- [ ] Add a portable model test for command decoding, consumer publication last, round-robin slots `0-3`, reuse key-off before reprogramming, master-volume clamp, stop-all, invalid sample metadata, and unknown opcode counts.
- [ ] Add `verify-pcm68k-model` and run it; expect compile failure.
- [ ] Closely adapt only the attributed PoneSound SCSP slot-word and pitch calculations. Keep register writes behind a tiny interface so the host model supplies fake registers.

```c
typedef struct sm64_saturn_pcm_sample {
    uint32_t sound_ram_offset;
    uint16_t sample_count;
    uint16_t sample_rate;
    uint16_t default_volume;
    uint16_t flags;
} sm64_saturn_pcm_sample_t;

bool sm64_saturn_pcm_voice_play(uint16_t slot,
                               const sm64_saturn_pcm_sample_t *sample,
                               uint16_t volume, int16_t pan);
```

- [ ] Support only `PLAY`, `STOP_ALL`, and `SET_MASTER`. A `PLAY` command validates the metadata table, keys off the selected slot, programs PCM8 start/loop/end/pitch/volume/pan, and keys on. SCSP hardware owns completion.
- [ ] Consume at most eight commands per poll iteration so a malformed producer cannot starve heartbeat publication.
- [ ] Publish telemetry fields: commands consumed, voices started, unknown opcodes, last opcode, active/reused slot, and heartbeat.
- [ ] Run `verify-pcm68k-model`, rebuild/verify the 68K image, and run `verify-pcm-protocol`; expect pass.
- [ ] Commit: `audio: consume PCM commands on four SCSP voices`.

## Task 5: Generate the Three-Sample Proof Bank

**Files:**

- Create: `tools/saturn/gen_pcm_proof_bank.py`
- Create: `tools/saturn/test_gen_pcm_proof_bank.py`
- Create: `src/port/saturn/soundtest/assets/README.md`
- Modify: `Makefile.saturn.mk`

- [ ] Add deterministic tests for signed mono 8-bit encoding, sample boundaries, 2-byte alignment, metadata offsets, reproducible hashes, maximum 65,536 samples per entry, and total bank cap of 32 KiB.
- [ ] Run `./.venv-saturn-tools/Scripts/python.exe tools/saturn/test_gen_pcm_proof_bank.py`; expect import/command failure before the generator exists.
- [ ] Generate three unmistakable short public-domain waveforms at 11,025 Hz: low tone, high tone, and decaying noise burst. Emit `pcm_proof_bank.bin`, `pcm_proof_bank.h`, and a JSON manifest under `build/saturn/soundtest/generated`; do not commit generated binaries.
- [ ] Record generation formulas and public-domain dedication in the asset README.
- [ ] Run the generator test and `make -f Makefile.saturn.mk compile-pcm-proof-bank`; expect pass and bank size at most 32 KiB.
- [ ] Commit: `audio: generate bounded public-domain PCM proof bank`.

## Task 6: Add the Standalone `soundtest` Saturn Target

**Files:**

- Create: `src/port/saturn/soundtest/Makefile`
- Create: `src/port/saturn/soundtest/main.c`
- Modify: `Makefile.saturn.mk`
- Create: `tools/saturn/soundtest_boot_contract_test.c`

- [ ] Add a host state-machine test for correct order: prepare driver/bank, SNDOFF, copy declared regions through `0x25A00000`, initialize 4-Mbit mode/master volume, SNDON, bounded heartbeat wait, then enqueue. Add timeout and oversize mutations.
- [ ] Add `verify-soundtest-boot` and run it; expect compile failure.
- [ ] Create a small Yaul target that shows status/counters on VDP2 and maps controller buttons A/B/C to the three `PLAY` commands, X to `STOP_ALL`, and L/R to `SET_MASTER`.
- [ ] Copy only the verified 68K binary, protocol region initialization, metadata table, and generated bank. Require heartbeat change within a fixed VBlank budget; on timeout, display failure and never enqueue.
- [ ] Add top-level `soundtest` and `verify-soundtest` targets. Make `verify-soundtest` depend on protocol, transport, model, generator, image, boot, ELF, CUE, and size gates.
- [ ] Run all host gates; expect pass.
- [ ] In the guarded MSYS2 shell run `make -f Makefile.saturn.mk soundtest -j1` and `make -f Makefile.saturn.mk verify-soundtest`; expect a valid standalone CUE. Do not run sourceboot.
- [ ] Commit: `audio: add standalone PCM68K soundtest`.

## Task 7: Prove the Standalone Transport in Ymir

**Files:**

- Create: `tools/saturn/capture_soundtest.py`
- Create: `tools/saturn/test_capture_soundtest.py`
- Create: `docs/saturn/evidence/reports/pcm68k-soundtest-2026-08-01.json`
- Create: `docs/saturn/evidence/reports/pcm68k-soundtest-2026-08-01.md`

- [ ] Add parser/client tests for fresh image identity, magic/version, heartbeat advancement, command consumption, zero drops, bounded high-water, three voices started, timeout, and capped emulator stderr.
- [ ] Run the Python test; expect failure before the capture tool exists.
- [ ] Implement a bounded Ymir client using the existing JSON-RPC patterns. Read the mailbox from paused sound RAM, pulse A/B/C with duration-aware input, and read it again. Do not infer audibility from slot state.
- [ ] Run the Python test; expect pass.
- [ ] Run one serial Ymir capture of the fresh soundtest image. Require heartbeat advancement, at least three commands consumed, three voices started, zero drops, and high-water below 32.
- [ ] Ask the owner for one manual audible confirmation. Record it separately from machine telemetry and label all Ymir results emulator evidence.
- [ ] Measure SH-2 enqueue/transport work and require it below 1% of soundtest source-tick time.
- [ ] Commit: `docs: prove standalone PCM68K transport`.

## Task 8: Produce the Sourceboot Promotion Packet and Stop

**Files:**

- Create: `docs/saturn/audio/PCM68K_SOURCEBOOT_PROMOTION.md`
- Modify: `docs/saturn/ROADMAP.md`
- Modify: `docs/saturn/UPSTREAM_CODE_LEDGER.md`

- [ ] Document the exact future boot seam: after `sm64_saturn_source_cart_load()` succeeds and before `thread5_game_loop(NULL)` in `src/port/saturn/sourceboot/main.c`.
- [ ] Document the future stub replacement seam in `src/port/saturn/sourceboot/source_audio_stub.c`: `play_sound()` maps only three stable `(bank << 8) | soundID` values; unknown effects, music, sequence, dialog, and unsupported stop calls increment visible counters.
- [ ] Specify future APIs without implementing or linking them:

```c
bool sm64_saturn_pcm_service_boot(void);
bool sm64_saturn_pcm_play(uint16_t stable_sound_id,
                          uint16_t volume, int16_t pan,
                          uint16_t priority);
const sm64_saturn_pcm_transport_t *sm64_saturn_pcm_service_stats(void);
```

- [ ] Include standalone ELF/BIN/bank hashes, memory sizes, telemetry, attribution, change notices, and the explicit rollback rule: removing the future sourceboot module restores silent stubs without touching renderer scheduling.
- [ ] Mark full SM64 sound effects, positional audio, sample-bank extraction, music, sequence playback, streaming, and retail validation as later milestones—not claims of this prototype.
- [ ] Stop and request explicit owner approval before creating any sourceboot audio implementation task.
- [ ] Commit: `docs: prepare PCM68K sourceboot promotion`.

## Final Review Checklist

- [ ] All host protocol/model/generator/boot tests pass.
- [ ] The 68K image is source-built, fixed-address, unresolved-symbol-free, and at most 16 KiB.
- [ ] The generated bank is public-domain, deterministic, and at most 32 KiB.
- [ ] Ymir shows heartbeat, three consumed play commands, three voices started, zero drops, bounded high-water, and transport below 1%.
- [ ] The owner separately confirms one sample is audible.
- [ ] PoneSound MIT attribution, pinned SHA, inspected paths, direct-adaptation sites, and change notices are preserved.
- [ ] No proprietary driver/binary, extracted Nintendo sample, ADX/CDDA path, or copied unlicensed reference code enters the repository.
- [ ] `sourceboot` remains unchanged and the FPS comparison image remains audio-free.
