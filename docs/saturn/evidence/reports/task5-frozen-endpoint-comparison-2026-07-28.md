# Task 5 frozen endpoint comparison — 2026-07-28

Status: **authority gate passed; renderer counters informational**.

The v2 route comparator reports `deterministic=true` and identical checkpoint
hashes (`d6f8bb72…`). Renderer deltas are retained but excluded from source
authority: demo minus interpreted is `+1323` transformed and `+615` VDP1
commands. This is the required evidence boundary for the renderer swap; it is
not a gallery acceptance because the endpoint frame still needs owner visual
confirmation.
