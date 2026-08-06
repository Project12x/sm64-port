# Full-game S64A audio package — 2026-08-05

Task 12 compiles the repository's user-extracted Saturn audio inputs without
invoking a PC game build. The source-authoritative catalog contains 35 stable
sequence IDs, 38 sound banks, 219 AIFF sample records, all source SHA-256
identities, and canonical bank metadata (including instrument/percussion splits,
tuning and envelope/release fields). AIFF PCM16 is converted to deterministic
unsigned Saturn PCM8; source sequence bytes remain unchanged in S64A chunks.

## Package evidence

The serial DLL-preflight gate generated untracked outputs under
`build/saturn/audio/generated/`:

| Output | Result |
| --- | ---: |
| `AUDIO.DAT` | 4,498,411 bytes |
| catalog format | S64A v1, 77 aligned chunks |
| source inventory | 35 sequences / 38 banks / 219 AIFFs |
| source SHA-256 | `00e0871f6ef78fb657666335472034d1a67c09086334ed3ee5c482d81c9efb33` |
| package SHA-256 (zeroed package-digest field) | `42ce539739263a88d11c8dd1a07580e110114d27b16110faca1ab4bfbaba72a2` |
| BOB closure | bank 22, 253,952 / 491,520 bytes |
| WF closure | bank 22, 253,952 / 491,520 bytes |

`audio_manifest.json`, `bob_audio_closure.json`, and `wf_audio_closure.json`
are generated and intentionally untracked. The catalog rejects missing/stale
inputs and changes its source/package hashes when an input byte changes.
Manifest sample paths are repository-relative so the same source tree produces
identical metadata across checkout locations.

## Residency contract

The SH-2 preparation API allocates replacement driver, mailbox, and sample spans
after the committed generation and returns false without mutating the output
plan when the two generations cannot coexist under the 480 KiB bound. Whole
sound-RAM clear is legal only before the first generation; post-boot clear and
generation reuse are rejected. The MC68000-side acceptance API requires the
complete 35/38/219 catalog identity and the retained-generation flags, and
publishes only scalar generation/source identities.

## Gates

* `tools/saturn/test_compile_saturn_audio.py`: **4/4**.
* DLL-preflight `mingw32-make -f Makefile.saturn.mk -j1 compile-saturn-audio verify-audio-residency`: **PASS**.
* Compiler run twice; complete hashes for `AUDIO.DAT`, manifest, and both scene
  closures: **identical**.

Still open by design: sequence VM execution, MC68000 voice scheduling/SCSP
playback, transport integration, S64P `AUDIO_DEPENDENCIES` final reseal,
target artifact/Ymir boot, hardware audio, and manual FPS/performance evidence.
