# E2 sourceboot triangle-capacity baseline -- 2026-07-22 (before)

Captured immediately before raising SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES (192 -> 1536) and SOURCEBOOT_VDP1_COMMAND_CAPACITY (512 -> 2048), at boot depth 240 + 3,600 frames -- confirmed live (not the CPU hang that starts between 3,600 and 3,800 post-poke frames; the original 2026-07-22 first-geometry capture used 14,000 and is of uncertain validity, tracked separately), for a real before/after comparison.

Report: [reports/e2-sourceboot-capacity-before-2026-07-22.json](reports/e2-sourceboot-capacity-before-2026-07-22.json)
Screenshot: [screenshots/e2-sourceboot-capacity-before-2026-07-22.png](screenshots/e2-sourceboot-capacity-before-2026-07-22.png)

| Counter | Value |
| --- | --- |
| triangle_cnt (decoded) | 574 |
| tri_transformed | 573 |
| tri_emitted (resolved, capped at 192) | 18 |
| tri_vdp1_emitted | 0 |
| rej_cmd_cap (192-cap rejects) | 0 |
| rej_near_far / rej_backface / rej_degenerate | 447 / 107 / 0 |
| attribution: w_nonpos / z_near / z_far / offscreen / span | 293 / 0 / 154 / 0 / 0 |
