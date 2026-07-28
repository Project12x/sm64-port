# Task 5 invalid-vertex cull diagnostic — 2026-07-27

Status: **diagnostic; not gallery-accepted**.

The demo renderer now rejects any BOB primitive containing a failed vertex transform instead of substituting the screen center. The fresh capture still lands in the intro camera: `demo_actor_vertices_valid=0`, so Mario is correctly near-clipped, and the frame remains malformed. Keep this as implementation evidence only; it is not a visual milestone or gallery candidate.
