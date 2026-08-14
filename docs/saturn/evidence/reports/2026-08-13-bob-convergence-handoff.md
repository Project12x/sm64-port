# BOB product-recovery handoff — 2026-08-14

## Truth at handoff

There is **no owner-accepted current development CUE**.  Do not launch, show,
or describe the earlier generic-actor image as the current demo.  In
particular, the prior `id-e3cb8ee1f5a955d6` handoff image is superseded
diagnostic evidence, not a presentation candidate.

The only recent CUE that has both booted and produced a post-startup BOB frame
is an isolated historical Mario donor:

- source: `d7b04d61` plus donor-only extractor compatibility commit
  `5808cdbf`;
- CUE:
  `build/saturn/sourceboot/e2-bob-demo-replay-camroute0-live-input-boot600-atan2v2-camv3-stage8-r6000-slave1-poly2-hot1-clip1-bsp1-frag0-pipe4/sm64-saturn-sourceboot-e2.cue`;
- ELF SHA-256:
  `60c978973e682f8a1cdb3c060d58b8038ce021f7718d8f9c5a430b5cfbaff8d5`;
- ISO SHA-256:
  `0e6eb40e868016df8b321187a27cd35738edcbe34d81938b01ef09a2484b3270`;
- CUE SHA-256:
  `cdbf0bfa299b64cde5ba985d531f864f3c0192c0de566fa89e1bfc9b0f46dba7`.

An input capture at VBlank 3,420 showed BOB terrain and a red/blue Mario
frame.  It is evidence of boot, terrain presentation, input response, and a
Mario draw only.  It is **not** evidence of correct Mario scale, source
animation, face order, occlusion, controls/camera, Bob-omb rendering, audio,
or cadence.  The owner has not accepted it.

The current generic package route remains rejected: its identity-bound
diagnostic reached zero presentation events in its bounded observation, and
the current normal build can fail before SH-2 compilation at actor-family
closure/package validation.  Neither is a usable demo.

## What was actually learned

The small Mario color hypothesis is now closed **without a source change**.
Both the historical donor and the dirty current renderer contain the A9A
policy in `demo_emit_mario_range`:

1. per-material RGB multiplied by each source light intensity for Gouraud
   entries;
2. a neutral polygon base for Gouraud polygons; and
3. RGB1555 texture detail emitted with `VDP1_CMDT_CC_REPLACE`.

The focused source policy test passes for the current source, and the donor
has the same required statements.  Reapplying that patch would make a new CUE
without changing the tested behavior.  Do not do that.

The four direct sourceboot audio CUE experiments are terminal negative
evidence.  Each left rebuilt `_s_active` at eight zero bytes after 1,200
post-BIOS target frames:

| Attempt | ELF SHA-256 | Falsified boundary |
| --- | --- | --- |
| Initial direct SFX | `105ab7af…5afb74a` | sourceboot-only SCSP latch removal was not sufficient |
| Explicit `audio_init()` | `9df4fbef…f1d691` | direct initializer call was not sufficient |
| Soundtest-style volatile copy | `fdeeca23…2fa60e` | driver/PCM copy route was not sufficient |
| Earlier pre-correction image | preserved under `e2-jump-sfx-pre-*` | no active sound CPU guard |

The standalone target sound test reaches its READY mailbox with the same custom
PCM68K driver.  Therefore the next audio work must first identify the missing
**game-entry-to-driver activation boundary** with target telemetry.  It must
not create a fifth sourceboot audio variant, a new sound format, or a GUI
listening claim.

## What went wrong

1. We treated infrastructure completion, format validation, review PASSes, and
   successful links as if they were product progress.  They were not.
2. We expanded actor, residency, package, and scheduling abstractions before
   keeping one normal BOB consumer alive.  That created memory and integration
   debt, then redesign work, without proving a player-visible improvement.
3. We reused screenshots before the scene was ready and launched stale or
   wrongly profiled images.  Those are invalid visual evidence.
4. We continued the direct sourceboot-audio experiment after its live guard
   already failed.  The resulting CUEs are useful negative evidence, but they
   exceeded the two-attempt recovery budget and did not move the product.
5. We created source-policy tests that prove code spelling while failing to
   bind the tested code to a newly observed CUE.  The Mario policy test is a
   guardrail, not a visual verdict.
6. We allowed a broad “full-game-capable” objective to justify work that could
   not immediately demonstrate normal BOB.  The product is the assembled port,
   not the number of generalized components.

## Non-negotiable operating rules

- One behavior hypothesis, one focused regression, one uniquely named CUE,
  then one target observation.  No second behavior change before that result.
- A CUE is progress only if it changes an observed product fact: Mario visual
  correctness, normal Bob-omb, audible game sound, or cadence.  Compiled,
  linked, screenshot-written, and host-contract-passed are separate states.
- For every CART/HWRAM/LWRAM/SCU/VDP1/SCSP boundary, record source and
  destination, byte cap/alignment/margin, owner/lifetime, producer and first
  real consumer, atomic failure behavior, and the first target observation.
  If that record cannot name a normal BOB consumer, do not add the component.
- Maximum two causal attempts or two hours per boundary.  On failure, preserve
  evidence and bypass to a proven donor; do not start a repair or redesign
  sprint.
- Do not run release sealing, reproducibility, broad mutation waves, capacity
  campaigns, generic level work, or multi-actor enumeration while BOB lacks a
  current accepted CUE.
- Keep Mario out of probationary generic actor machinery until it has a
  separately owner-accepted normal BOB result.

## Exact next action

Do not patch the renderer or build a duplicate Mario candidate.  Use the
historical donor CUE as a bounded diagnostic only and inspect the live VDP1
command chain after the known scene-ready window:

1. identify the command records belonging to Mario’s solid and RGB1555 detail
   draws;
2. record polygon order/depth link, `CMDPMOD`, `CMDGRDA`, base color, and
   texture source for the frame actually presented; and
3. compare those records with the owner-observed defect before selecting a
   change.

If the donor command trace does not expose a discrepancy, the next task is a
manual owner verdict on the **named donor CUE**, not speculative rendering
work.  If it does expose one discrepancy, make only that local correction,
build one new CUE, and observe it before touching Bob-ombs or audio.

Bob-omb and audio remain required product gates, but neither may be advanced by
an injected actor, object-specific renderer branch, synthetic audio trigger,
or a source-only test.  Whomp’s Fortress remains closed until normal BOB is
owner-accepted.
