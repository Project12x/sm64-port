# Task 5 replay-route visual diagnostic — 2026-07-27

Status: **failed diagnostic; not gallery-accepted**.

This was the demo/replay variant with a 36,000-frame chunked post-run budget, `--dram-cart`, BIOS automation, and a freshly resolved `_sourceboot_fast3d` probe. It reached `frame_serial=851`, but the screenshot is mostly black with only partial terrain/debug text visible.

The probe targeted the frontend profile, not `sourceboot_route_checkpoint`, so this artifact makes no claim that the 600-tick route checkpoint was reached. It is retained to document the late-phase visual failure and must not be promoted to `evidence/index.html` or `TIMELINE.md`. The next capture must probe the route block at the fresh replay ELF symbol and separately verify checkpoint/hash parity before visual acceptance.
