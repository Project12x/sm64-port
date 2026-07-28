# Task 5 paired profile/route visual diagnostic — 2026-07-28

Status: **partial replay diagnostic; not gallery-accepted**.

This capture uses the current demo/replay CUE, `--dram-cart`, BIOS automation, a 3,600-frame post-poke budget, and fresh symbols from the exact replay ELF: `_sourceboot_fast3d=0x060d1f50` and `_sourceboot_route_checkpoint=0x060d1ee8`. The paired probe confirms the image is live (`frame_serial=51`, `triangles_transformed=2171`) and the route block is valid (`magic=SBR1`, `replay_ticks=25`, `global_timer=26`).

The 600-tick route checkpoint was not reached in this budget, so this artifact cannot establish replay determinism or visual acceptance. The screenshot is retained as diagnostic evidence only and must not be promoted to `docs/saturn/evidence/index.html` or `TIMELINE.md`.

The JSON report contains the decoded profile/route fields plus ELF, CUE, and screenshot hashes/mtimes for reproducibility.
