# Task 4 live-Mario counter diagnostic — 2026-07-27

Status: **diagnostic; not gallery-accepted**.

The append-only demo counters remove the screenshot ambiguity: `demo_actor_vertices_valid=0` and `demo_actor_primitives_emitted=0` at `frame_serial=51`, while the bridge snapshot itself is valid. The authoritative intro camera is within the actor's 128-unit near clip, so this is expected clipping—not a VDP1 command-arena failure.

This capture remains evidence only. A later route phase must make the actor visible before the Task 4 visual sanity gate or Task 5 gallery candidate can pass.
