# Current-head green-screen investigation — 2026-08-06

## Artifact under test

- Identity directory: `e2-bob-identity-id-a15ffdec4e406673`
- ELF SHA-256: `b64737bfba5bcac8eeb04e7d149b1af2fe111aefc0535fa46b09552cdcdcea38`
- CUE SHA-256: `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
- ISO SHA-256: `7aa1a3be0335a84a721abd9202b84348b53b44bbe2d9426fb271e1f1ac720bdb`
- Launch inputs: build-agent2 desktop Ymir, profile-managed USA BIOS, 32-Mbit
  DRAM cart, and the exact CUE above.

## Texture-flag reconciliation (2026-08-06)

The no-texture observation is explained by the image's compile-time route, not
by the cartridge profile. The previously launched current-head image was built
with `SATURN_DEMO_PATH=0`. That route enters
`sm64_saturn_fast3d_vdp1_emit()`, whose current command lowering emits RGB-only
VDP1 polygons and does not bind texture/CLUT state. `SATURN_DEMO_MARIO_TEXTURES=1`
only enables Mario's demo texture assets; it cannot make the source Fast3D
terrain emitter textured. The repository `.ymir-profile` still supplies the
32-Mbit DRAM cart correctly.

For a visual diagnostic, a fresh current-head image was built with the same
dual-SH2, memory, and exception-capture contract but with
`SATURN_DEMO_PATH=1`:

- Identity directory: `e2-bob-identity-id-b16071bfda7aa10d`
- ELF SHA-256: `d3470255cb6aa704ea09a1ac0a13c2e9a05198f1380477d6f8979967551a5b55`
- CUE SHA-256: `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`
- ISO SHA-256: `147703d665874ed17c59ad7880f9c3715bb2b5c90b90666714df6ec992f1dbcc`
- Launch: `ymir-agent/build-agent/apps/ymir-sdl3/Release/ymir-sdl3.exe`
  with `sm64-port/.ymir-profile` and profile-managed 32-Mbit DRAM.

This is explicitly a textured BOB/demo-path manual artifact, not closure of
the full-game source renderer. The permanent source-path work remains to add
texture residency and texture-aware VDP1 lowering to the `SATURN_DEMO_PATH=0`
route while preserving dual-SH2 ownership; changing the default flag would
hide that missing implementation.

## Linked memory contract

- HWRAM `___end = 0x060F66BC`; physical-top margin is `0x9944`, above the
  required `0x1B00` Yaul/TLSF floor.
- LWRAM static work ends at `0x0027A270`; the reserved downward-growing slave
  stack begins at `0x002FC000` and ends at `0x00300000`.
- VDP1 command banks remain the HWRAM-owned, 32-byte-aligned transport block;
  Gouraud/SCU-visible staging remains in its existing HWRAM owner.
- The build is still `SATURN_SLAVE_RENDER=1`; no single-SH-2 or texture-flag
  fallback is part of this artifact.

## What the green/blank screen means

Yaul's reset path deliberately sets VDP2's back color to green and disables
the slave when `__exception_assert()` is reached. Therefore a green/blank
surface is an SH-2 exception/reset symptom, not evidence that the CUE or DRAM
cart failed and not a texture-residency diagnosis by itself.

The prior report attributed the desktop observation to master-stack exhaustion
because an older probe saw a low master SP. That attribution is not established
by the current evidence and is withdrawn. The LWRAM slave-stack reservation is
still retained as a legitimate capacity/ownership fix, but it is not being
used as a substitute for identifying the faulting instruction.

## Source-owned exception capture

The current image adds a small diagnostic record at the linked symbol
`_sourceboot_exception_record = 0x0608C380` and vector trampolines for illegal
instruction, illegal slot, CPU address error, and DMA address error. The
trampolines preserve the Yaul register frame, write the record, and then call
Yaul's normal debug/reset handler. The same handlers are installed in both the
master and slave INTC tables; a worker-side exception can no longer erase the
only useful frame before the master observes the reset.

## Evidence so far

- The first rebuilt image with only master hooks was stable at the former
  3,510-frame failure point, but its record stayed zero; that showed the
  observed failure was not a master-only vector event.
- The current master+slave-hook image reached post-BIOS frame 8,970 in a
  bounded headless run. Every 30-frame sample had a live master and slave, a
  changing frame hash, and a textured Mario/BOB image; the final capture is
  `tools/saturn/current-exception-video-1/post-bios-8970.png`.
- A longer 9,000→30,000 frame soak hit the headless client's bounded response
  timeout while advancing a large RPC batch. It did not return a Ymir stopped
  reason or a target exception; because the client did not emit a completed
  report, this is a tooling timeout, not a target-stability pass.
- The first desktop retry used `build-agent2`; its log confirmed a 32-Mbit DRAM
  cartridge, but the owner still saw no textures. The last known textured
  desktop evidence used `build-agent`, so the manual retry has been switched to
  that executable without changing the profile or CUE. Its result is
  intentionally not marked complete until the owner observes a textured frame
  beyond the prior green transition.
- The fresh textured diagnostic above was launched after the flag audit. Its
  visual result is still owner/manual evidence pending; no FPS, source-path
  texture, target/P2, or full-game claim is made from the build alone.

## Disposition and next gate

- Keep the dual-SH-2 stack/layout ownership fix and both-CPU exception capture.
- Treat `SATURN_DEMO_PATH=0` no-texture output as a known source-renderer gap,
  not a DRAM/profile failure. Keep the `SATURN_DEMO_PATH=1` image isolated as a
  manual texture diagnostic while implementing the permanent source-path
  texture residency/lowering seam.
- Do not claim that the green-screen root cause is solved yet. If the desktop
  run reproduces green, read the exception record before changing camera,
  culling, textures, or math. If it remains stable, archive the visible run
  as a fresh-image manual result and retain the headless soak as bounded
  corroboration.
- The target/P2/cache, sourceboot visual, manual, texture, audio, and FPS gates
  remain open. A stable desktop boot is necessary evidence, not a performance
  claim.

## Ymir memory-dump decode (2026-08-06)

The exception-enabled desktop run wrote a complete memory dump to
`sm64-port/.ymir-profile/dumps/` after the green/reset transition. The dump's
`wram-hi.bin` contains the current image's source-owned record at
`0x06093640` (the linked symbol for `e2-bob-identity-id-b16071bfda7aa10d`).
Its magic is `0x53484258`; the exception-name pointer `0x06004064` decodes to
`Illegal instruction`.

The saved register frame is decisive:

- `vbr = 0x06000000`, so the fault was on the master SH-2 (the slave vector is
  at `0x06000400`).
- `sp = 0x060026DC`, while the linked master-stack entry is
  `___master_stack = 0x06004000`. The frame is therefore `0x19324` bytes
  (103,204 bytes) below the configured stack top, far beyond the 16-KiB
  HWRAM stack window and inside the preceding boot/IP region.
- `pc = 0x881C8901` and `pr = 0x06000928` are both outside the linked
  executable text (`0x06004000` onward). Their corruption is consistent with
  the master stack overrunning the boot/IP region, not with a texture or CUE
  loading error.

This supersedes the earlier “master attribution withdrawn” interim status:
the earlier headless runs only showed that the hooked record was empty; the
new desktop dump contains a valid master illegal-instruction record and a
stack pointer below the linker-owned stack. No code workaround is accepted
yet. The next bounded implementation task is to give the master runtime a
source-owned, linker-asserted stack arena with enough capacity (or eliminate
the unbounded call-frame demand) while preserving the dual-SH2 and LWRAM
ownership contract, then repeat the exact dump/manual gate.

The linked assembly identifies the likely demand source. In the exact ELF,
`geo_process_node_and_siblings` subtracts `0x1A4` bytes after saving 32 bytes
of callee state, so every recursive scene-graph descent consumes 452 bytes of
master stack. The observed `0x19324`-byte underrun corresponds to roughly 228
simultaneously live walker frames. The sourceboot loop intentionally runs the
original `geo_process_root()` walk even for the textured diagnostic, so this
is a source-path recursion limit, not a renderer/texture flag effect. The
permanent repair should make that traversal explicitly bounded/iterative (or
give it a separately owned, linker-asserted traversal arena); simply moving a
small stack or disabling the geo walk is not an accepted production fix.
