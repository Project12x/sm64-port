# Pipe 8 deterministic closure — 2026-07-29

Status: **passed the owner-set 10% dual-dispatch gate.** This closes the
bounded Pipe 8 sprint; it does not claim that BOB's remaining near-plane
mapping, coverage, or painter defects are fixed.

## Build identity

Both replay images were built from `saturn/bootstrap` at `83938f3` with:

`SATURN_DEMO_PATH=1 SATURN_SOURCEBOOT_ROUTE_REPLAY=1
SATURN_DEMO_VIEW_RADIUS=6000 SATURN_DEMO_POLY_TIER=0
SATURN_DEMO_HOT_PROMOTION=1 SATURN_DEMO_NEAR_CLIP=1
SATURN_DEMO_BSP_ORDER=1 SATURN_DEMO_BSP_FRAGMENTS=0
SATURN_RENDERER_PIPELINE=8`

Only `SATURN_SLAVE_RENDER` differs:

| Build | `_sourceboot_fast3d` | route symbol | ELF SHA-256 | ISO SHA-256 |
| --- | --- | --- | --- | --- |
| dual (`1`) | `0x060DCB90`, 444-byte profile | `0x060DCAD4` | `ea090ebb0001f8d07c4e0508a6ff822dffb3247d16ab3e2bdf518a18d84d2c37` | `b90df97d8244c255fcf08e576e293226808a1c27e40e0f37f4daa8748c0f5214` |
| serial (`0`) | `0x060DC670`, 444-byte profile | `0x060DC5B4` | `2ef5fbea6efe94c4d7bb1671ba53f95c110ff6349e7ff82b7e421e2fae54c41c` | `9f214a330556e72aea4ba63292e988b71b91b92e4f661f9ef838e020d5855ef5` |

The CUEs postdate their ELFs. Captures used the USA BIOS macro, `--dram-cart`,
the fresh symbols above, a 1,700-second wall-clock budget, and Ymir's internal
software-renderer capture path. No viewport resize or desktop screenshot was
used.

## Frozen-route result

[`compare_route_reports.py`](../../../../tools/saturn/compare_route_reports.py)
reports `deterministic: true` for
[`bob_parity_v1`](../../../../tools/saturn/routes/bob_parity_v1.json):

- dual and serial both reach `replay_ticks=600`, `global_timer=601`, and
  checkpoint SHA-256
  `d6f8bb725b0e094b5f659dc81cfedb6b2405b850ab9bea2904fc283781c8861c`;
- both record 70,516 transformed and 114,982 VDP1-emitted primitives at the
  checkpoint;
- both record zero `fault_flags`, command-capacity rejects, and slave
  timeouts.

The complete post-route profiles also report zero VDP1-arena rejects,
result-reserve rejects, clip overflow, Gouraud overflow, VDP1 bank overwrite
attempts, and late-DMA observations. Pipe 8's observed high-water marks are
940 VDP1 commands and 604 Gouraud tables.

Evidence:
[`dual report`](pipe8-dual-route-2026-07-29.json),
[`serial report`](pipe8-serial-route-2026-07-29.json), and
[`route comparison`](pipe8-route-compare-2026-07-29.json).

## Dual-dispatch gate

At the exact checkpoint:

| Build | render FRT accumulator | guest FPS median / 1% low | emulator speed ratio |
| --- | ---: | ---: | ---: |
| dual | 4,700,701 | 1 / 0 | 1.407 |
| serial | 5,274,468 | 1 / 0 | 1.331 |

The dual path reduces accumulated render time by **10.88%**:

`(5,274,468 - 4,700,701) / 5,274,468`

On 2026-07-29 the owner set **10% as the acceptable gate for now**, so this
passes. The longer-term 15 FPS median / 12 FPS 1%-low target remains open;
these guest-cadence values are emulator evidence and are not retail-hardware
claims.

## Exact visual milestones

`capture_route_views.py` pauses on the live SBR2 checkpoint and calls Ymir
`video.capture` only when the route reports the requested tick. Each 320x224
PNG has a same-basename paired report.

| View | route tick / frame serial | sequence | PNG SHA-256 | Honest observation |
| --- | ---: | ---: | --- | --- |
| [near Mario](../screenshots/ymir-bob-pipe8-near-mario-blue-hole-tick360-2026-07-29.png) | 360 / 90 | 6,301 | `8999b00020ed9c1fe3ec12affe86565aa12c27caee0e1cdcb1d86b430229793d` | Mario and terrain textures persist; the large near green surface preserves the open near-plane mapping/coverage defect. |
| [distant terrain](../screenshots/ymir-bob-pipe8-distant-terrain-disappearance-tick504-2026-07-29.png) | 504 / 126 | 7,559 | `f249d78e336f5584cb6e3e76886f8bbf4e10aeaacb25f023247fcb340ff4c3a6` | Mario, the local path, and distant textured walls are present; detached distant pieces and terrain ordering remain visible. |
| [frozen route](../screenshots/ymir-bob-pipe8-frozen-route-tick600-2026-07-29.png) | 600 / 150 | 8,485 | `83363265976937e732dfd0190fc51f2675daa53ffd7fa5c385b455eef0da0288` | Textured Mario and stable local textured terrain are visible together; this is the primary accepted Pipe 8 gallery frame. |

Combined evidence:
[`exact-view report`](pipe8-route-views-2026-07-29.json). Paired reports:
[`tick 360`](ymir-bob-pipe8-near-mario-blue-hole-tick360-2026-07-29.json),
[`tick 504`](ymir-bob-pipe8-distant-terrain-disappearance-tick504-2026-07-29.json),
and
[`tick 600`](ymir-bob-pipe8-frozen-route-tick600-2026-07-29.json).

## Capture-tool provenance

The exact-tick runner extends the existing in-repo newline JSON-RPC tooling.
The compatible external process inspected for this work is
`Project12x/Ymir@bf3e4a4a58031663d404ecc999348d72473135ba` (GPL-3.0):
`apps/ymir-headless/src/main.cpp:109-171` (flushed line transport and ready
notification) and `debug_service.cpp:543-622` (synchronous bounded
`exec.run_for`, paused-state `video.capture`). Reuse mode: protocol client/API
use; no Ymir source or headers are copied into this repository.
