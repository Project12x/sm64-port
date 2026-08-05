#ifndef SM64_SATURN_FAST3D_FRONTEND_H
#define SM64_SATURN_FAST3D_FRONTEND_H

#include <stdint.h>

#include "port/saturn/runtime/saturn_source_runtime.h"
#include "saturn_matrix.h"
#include "saturn_light_q16.h"

/*
 * Bounded source-display-list intake for the Saturn renderer.
 *
 * This is deliberately a front end, not a Castle or Mario renderer.  It
 * consumes the SPTask produced by original game code, follows the Fast3D
 * display-list control flow, transforms and resolves real source
 * geometry, and hands a bounded buffer of resolved triangles to a
 * separate, Yaul-dependent emission stage (saturn_fast3d_vdp1_emit.h).
 * This file has zero Yaul/Saturn dependency by design, so it stays
 * host-testable via tools/saturn/runtime_contract_test.c.  Its command
 * ABI is verified against this tree's `src/pc/gfx/gfx_pc.c`, but this
 * implementation is new target code and does not import the PC renderer.
 */
#define SM64_SATURN_FAST3D_MAX_CALL_DEPTH 32U
#define SM64_SATURN_FAST3D_MAX_COMMANDS 16384U
#define SM64_SATURN_FAST3D_MAX_VERTICES 64U
#define SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES 1536U
#define SM64_SATURN_FAST3D_DEPTH_BUCKETS 16U

/* Ordinals a single display list's quad-map row may be indexed at. The
 * resolve stage keeps one resolved[]-slot entry per ordinal so a pair whose
 * two ordinals are NOT adjacent can still be merged -- only 71 of the
 * generated table's 284 pairs are consecutive, so an adjacent-only rule
 * leaves three quarters of the proven-safe merges unclaimed.
 *
 * Bounded, not unbounded: it is per-list, and quad_map.py emits the longest
 * row it generated as SM64_SATURN_QUAD_MAP_MAX_ENTRIES.
 * saturn_fast3d_frontend.c static-asserts this capacity against that
 * generated constant, so the bound cannot drift away from the data; the
 * headroom above it exists only so a regenerated map that grows a little
 * does not need a source edit. sm64_saturn_fast3d_quad_map_bind() also
 * refuses, loudly and at runtime, any row longer than this. */
#define SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY 128U

enum sm64_saturn_fast3d_fault {
    SM64_SATURN_FAST3D_FAULT_NONE = 0U,
    SM64_SATURN_FAST3D_FAULT_NULL_TASK = 1U << 0,
    SM64_SATURN_FAST3D_FAULT_NULL_DISPLAY_LIST = 1U << 1,
    SM64_SATURN_FAST3D_FAULT_CALL_DEPTH = 1U << 2,
    SM64_SATURN_FAST3D_FAULT_COMMAND_LIMIT = 1U << 3,
};

typedef struct sm64_saturn_fast3d_profile {
    uint32_t frame_serial;
    uint32_t command_count;
    uint32_t display_list_calls;
    uint32_t display_list_branches;
    uint32_t matrix_commands;
    uint32_t vertex_commands;
    uint32_t triangle_count;
    uint32_t texture_commands;
    uint32_t rdp_commands;
    uint32_t other_commands;
    uint16_t max_call_depth;
    uint16_t fault_flags;

    /* Added for Fast3D-to-VDP1 lowering (see design spec). */
    uint32_t triangles_transformed;
    uint32_t triangles_emitted; /* resolved into the bounded intermediate
                                  * buffer -- NOT necessarily reaching a
                                  * real VDP1 command. See
                                  * triangles_vdp1_emitted below, which is
                                  * the Yaul-dependent emission stage's
                                  * own counter and can only be observed
                                  * on real/cross-compiled hardware. */
    uint32_t reject_near_far;
    uint32_t reject_backface;
    uint32_t reject_degenerate;
    uint32_t reject_vertex_range;
    uint32_t reject_command_capacity; /* the frontend's resolved-triangle
                                        * buffer (this file) filling up --
                                        * a DIFFERENT ceiling than
                                        * reject_vdp1_arena_capacity below,
                                        * which is the VDP1 command arena
                                        * in saturn_fast3d_vdp1_emit.c. */
    uint32_t modelview_stack_overflow;
    uint16_t max_modelview_depth_reached;

    /* Added for Task 11's VDP1 emission adapter (saturn_fast3d_vdp1_emit.c).
     * Both fields are Yaul-dependent counters that only advance once a
     * real vdp1_cmdt_t is reserved/written -- they cannot be observed by
     * a host-native test of sm64_saturn_fast3d_frontend_submit alone. */
    uint32_t triangles_vdp1_emitted;
    uint32_t reject_vdp1_arena_capacity;

    /* Bring-up diagnostics: attribute reject_near_far's composite check
     * to its constituent causes (reject_near_far itself still counts the
     * total, preserving every existing test's expectations). Added when
     * live Bob-omb Battlefield data showed 100% of forward-facing
     * triangles dying in the composite check with no way to tell which
     * limit was responsible. A triangle can trip several conditions;
     * each tripped condition's counter increments, so these can sum to
     * more than reject_near_far. */
    uint32_t reject_w_nonpositive; /* any vertex at clip w <= 0 */
    uint32_t reject_z_near;        /* quad min_z below the near depth */
    uint32_t reject_z_far;         /* quad max_z beyond the far depth */
    uint32_t reject_offscreen;     /* clip_and nonzero: fully outside */
    uint32_t reject_span;          /* screen-space extent over the cap */

    /* Snapshot of the FIRST composite-rejected quad each frame -- real
     * magnitudes tell more than counts during bring-up. Valid only when
     * reject_near_far > reject_w_nonpositive (w-rejects never reach quad
     * analysis and leave no snapshot). */
    int32_t dbg_first_reject_min_z;
    int32_t dbg_first_reject_max_z;
    int16_t dbg_first_reject_min_x;
    int16_t dbg_first_reject_max_x;
    int16_t dbg_first_reject_min_y;
    int16_t dbg_first_reject_max_y;
    uint32_t dbg_first_reject_clip_and;

    /* Bring-up diagnostic, added 2026-07-22 investigating the 96%
     * transformed-triangle rejection rate: the FIRST w<=0 reject each
     * frame. reject_w_nonpositive is the single largest rejection bucket
     * (51% of transformed triangles in the frame this was added to
     * investigate) and the existing dbg_first_reject_* snapshot above
     * cannot see it -- a w<=0 vertex returns before the quad is ever
     * built. Captures the failing vertex's model-space position, the
     * computed w, which triangle corner (0/1/2) failed, the live
     * modelview-stack depth at that moment, and the composed MP matrix's
     * full w-column (mp[0][3]..mp[3][3], raw Q16.16) -- enough to tell a
     * genuinely off-camera/behind-camera vertex apart from a corrupted/
     * overflowed matrix composition producing a bogus w.
     *
     * KEPT (not reverted) after the 2026-07-22 investigation: live capture
     * showed mp[2][3] == INT32_MIN (a Q16.16 narrowing overflow/wraparound
     * sentinel) recurring bit-for-bit identically across independent
     * rebuilds, for a triangle at modelview-stack depth 1 whose
     * model-space coordinates (mx=4864, my=1024, mz=4096) are ordinary
     * small values -- i.e. a real, reproducible arithmetic defect, not
     * uninitialized memory or expected off-camera geometry. Whoever fixes
     * the underlying overflow will want this snapshot to confirm the fix
     * (mp columns back in a sane +-32768-ish Q16.16 range, computed w no
     * longer beyond a few tens of thousands in magnitude). Valid only
     * when reject_w_nonpositive > 0. Intentionally NOT wired to any test
     * -- bring-up instrumentation, matching the existing dbg_first_reject_*
     * fields' own convention above. */
    float dbg_first_w_reject_mx;
    float dbg_first_w_reject_my;
    float dbg_first_w_reject_mz;
    float dbg_first_w_reject_w;
    int32_t dbg_first_w_reject_mp03;
    int32_t dbg_first_w_reject_mp13;
    int32_t dbg_first_w_reject_mp23;
    int32_t dbg_first_w_reject_mp33;
    uint32_t dbg_first_w_reject_triangle_ordinal;
    uint8_t dbg_first_w_reject_corner;
    uint8_t dbg_first_w_reject_stack_depth;

    /* Bring-up diagnostic, added 2026-07-22 (same investigation as the
     * dbg_first_w_reject_* block above): aggregate count of w<=0 rejects
     * whose |w| is orders of magnitude beyond any plausible real SM64
     * world-unit value for this boot path (measured live: Bob-omb
     * Battlefield's own static geometry spans roughly +-8192 units, and
     * this boot's forced intro-cutscene camera/focus spline tops out
     * around 30,000 units -- see the 2026-07-22 investigation notes).
     * Distinguishes "genuinely behind/off camera" (small-magnitude
     * negative w, expected) from "fixed-point overflow producing a bogus
     * w" (huge-magnitude w, a defect) without needing a snapshot of every
     * single reject.
     *
     * KEPT (not reverted): live capture measured this at 392/399 (98.2%)
     * of one frame's w<=0 rejects, confirming the overflow (not genuine
     * off-camera geometry) is the dominant cause of the reject_w_nonpositive
     * bucket. Re-measuring this counter is the cheapest way to confirm any
     * future fix to the matrix-composition overflow actually worked (it
     * should drop to near 0, leaving only genuine behind-camera rejects). */
    uint32_t reject_w_nonpositive_overflow_suspect;

    /* Bring-up diagnostic, added 2026-07-22 to trace the mp[2][3]==INT32_MIN
     * root cause back to its source. Hand-derived from guPerspectiveF's
     * projection matrix (projection[2][3] == -65536 in Q16.16 is the ONLY
     * nonzero entry in that whole column; every other row's column-3 entry
     * is exactly 0) that mp[2][3] = top->m[2][2] * projection[2][3] is a
     * SINGLE-TERM product -- so mp[2][3] can only narrow to exactly
     * INT32_MIN if top->m[2][2] (the modelview stack's root-slot [2][2]
     * entry) was ALREADY exactly INT32_MIN before the final MP compose.
     * This snapshot traces one level further back: it's overwritten on
     * every non-projection G_MTX command processed while the matrix stack
     * sits at its un-pushed root depth (1) -- exactly the depth the
     * corrupted triangle in dbg_first_w_reject_* was found at -- so by
     * the time a w-reject snapshot is taken, this reflects the actual
     * root-modelview write that produced the corrupted entry, letting a
     * live capture tell apart: (a) the RAW SOURCE FLOAT feeding cell
     * [2][2] already being huge/NaN/Inf before any conversion at all
     * (implicating the source data itself, or whatever upstream code
     * produced this matrix), vs (b) the source float being a sane, small
     * value but sm64_saturn_matrix_decode's or sm64_saturn_matrix_mul's
     * OWN arithmetic corrupting it during conversion/composition
     * (implicating this port's own fixed-point code, not its input). */
    float dbg_root_mtx_source_m22;       /* gbi_floats[2*4+2], raw, pre-conversion */
    int32_t dbg_root_mtx_decoded_m22;    /* sm64_saturn_matrix_decode's output for that cell */
    int32_t dbg_root_mtx_post_top_m22;   /* matrix_stack.entries[0].m[2][2] after this command finished (post-load or post-multiply) */
    uint8_t dbg_root_mtx_params;         /* raw (push-bit-corrected) G_MTX params byte */
    uint8_t dbg_root_mtx_took_mul_path;  /* 1 if this command multiplied against the existing top rather than a fresh load */
    uint8_t dbg_root_mtx_mul_overflowed; /* sm64_saturn_matrix_mul's own return value for this command's compose, valid only when dbg_root_mtx_took_mul_path == 1 */
    uint32_t dbg_root_mtx_command_ordinal; /* profile->matrix_commands value at capture time, for cross-reference against dbg_first_w_reject_triangle_ordinal */

    /* Bring-up diagnostic, added 2026-07-22: surfaces
     * sm64_saturn_matrix_stack_t.mp_overflowed (saturn_matrix.h), which
     * sm64_saturn_matrix_stack_mp() already computes via
     * sm64_saturn_matrix_mul()'s return value but which nothing previously
     * read. NOT a per-frame count -- mp_overflowed is a one-way latch
     * (set true once a composed MP narrows out of Q16.16 int32 range,
     * never cleared, matching this struct's existing documented decision
     * not to reset matrix_stack state per frame) -- so this field reads
     * as "has an MP compose overflowed at any point up to and including
     * this frame", not "did it overflow THIS frame specifically". Still
     * directly answers whether the final MP compose (as opposed to the
     * G_MTX decode/multiply chain that built its modelview input, see
     * dbg_root_mtx_* above) is itself capable of manufacturing an
     * overflow from otherwise-sane inputs. */
    uint8_t dbg_mp_compose_overflowed_ever;

    /* Bring-up diagnostic, added 2026-07-22 after the gGfxPool two-front
     * collision theory was measured dead (12 live samples across 23 game
     * frames: forward command stream 2,552 B, backward alloc front
     * 8,832 B, of 51,200 B -- a 39.8 KiB standing gap; see
     * e2-sourceboot-gfxpool-fronts-2026-07-22.txt). The proven corruption
     * signature (a G_SETOTHERMODE_L opcode word 0xE200001C inside the
     * float data a G_MTX decode consumed, narrowing m[2][2] to INT32_MIN)
     * therefore cannot be commands overwriting a pool-resident matrix --
     * the far more likely mechanism is the G_MTX command's w1 POINTER
     * itself referencing the wrong memory. Latch the FIRST such command
     * per frame: its raw w1, its params byte, and the matrix_commands
     * ordinal at latch time (0 ordinal = nothing latched this frame), so
     * a follow-up capture can dump the pointed-at bytes and classify the
     * region (pool forward/backward, LWRAM heap, static data, garbage). */
    uint32_t dbg_bad_mtx_w1;
    uint32_t dbg_bad_mtx_ordinal;
    uint8_t dbg_bad_mtx_params;

    /* Added for Gouraud shading (docs/superpowers/specs/2026-07-24-
     * gouraud-shading-design.md). Degradation contract: every
     * dropped/simplified feature gets a counter so the cost stays
     * visible in captures (design spec's "Degradation contract"
     * table). Appended at the very end of this struct, not
     * interleaved above -- existing capture-decode scripts hand-map
     * earlier field offsets, a lesson carried over from the prior
     * matrix sprint.
     *
     * unsupported_num_lights is populated THIS task: it increments
     * whenever G_MW_NUMLIGHT requests anything other than exactly 1
     * directional light (state is unconditionally clamped to 1
     * directional + ambient regardless -- "first light used"). The
     * remaining four fields are declared here, zero-initialized, so
     * later tasks in this plan only have to increment an existing
     * field rather than edit this struct again: lit_vertices/
     * unlit_vertices (Task 3's G_VTX lighting gate), fog_dropped_
     * triangles (Task 4's triangle resolve), gouraud_bank_overflow
     * (Task 6's VDP1 emission). */
    uint32_t unsupported_num_lights;
    uint32_t lit_vertices;
    uint32_t unlit_vertices;
    uint32_t fog_dropped_triangles;
    uint32_t gouraud_bank_overflow;

    /* Added for general quad merging (docs/superpowers/specs/2026-07-25-
     * general-quad-merging-design.md). Appended at the very END of this
     * struct, after the Gouraud block, for the same reason that block
     * says it was: capture-decode scripts and this project's recorded
     * offsetof() probe figures hand-map every earlier field, so no
     * existing field may move. sizeof() grows; that is expected and is
     * recorded in the quad-merge evidence.
     *
     * quads_merged      one increment per PAIR that left the resolve stage
     *                   as a single four-corner VDP1 primitive instead of
     *                   two degenerate ones. VDP1 command count drops by
     *                   exactly this number; triangles_transformed and
     *                   triangles_emitted do NOT change, because merging
     *                   changes commands, not geometry.
     * quad_map_mismatch a map entry that contradicts ITSELF: a corner code
     *                   outside 0-5, or a partner ordinal that is the
     *                   entry's own or lies past the end of the very row
     *                   it indexes. Nothing a well-formed generated table
     *                   can produce, so this MUST read 0; a nonzero value
     *                   is a correctness fault, not a tuning knob, and the
     *                   offending entry is refused rather than merged.
     *
     *                   Deliberately does NOT include "live ordinal past
     *                   entry_count" -- see quad_ordinal_past_row.
     * quad_ordinal_past_row
     *                   a triangle whose ordinal is past the end of its
     *                   display list's row. EXPECTED nonzero, and not a
     *                   fault: quad_map.py trims each row after its last
     *                   paired ordinal, because the encoding makes a
     *                   missing tail decode as "do not merge" anyway (see
     *                   the generated header's own note listing "an ordinal
     *                   past entry_count" among the normal all-zero reads).
     *                   18 of the 27 generated rows are shorter than their
     *                   list's real triangle-command count for exactly this
     *                   reason, so treating the case as a fault would make
     *                   the mismatch counter fire dozens of times a frame
     *                   on a perfectly healthy build -- measured at 65/frame
     *                   before this counter was split out. Kept as a
     *                   separate number because it is still the only coarse
     *                   cross-check available between row lengths and the
     *                   live stream.
     * quad_pair_not_adjacent
     *                   the map offered a legal pair whose two ordinals are
     *                   NOT consecutive, counted once per pair at the lower
     *                   ordinal. This is now purely a SHAPE statistic about
     *                   the generated table -- since the ordinal->slot side
     *                   array landed, such pairs merge like any other, so a
     *                   high value here no longer implies a loss. Kept
     *                   because it is the number that shows how much of the
     *                   merge yield depends on the side array existing at
     *                   all: 213 of the map's 284 pairs are non-adjacent.
     *                   Expected nonzero; not a fault. Read
     *                   quad_pairs_declined, not this, for pairs actually
     *                   lost.
     * quad_pairs_declined
     *                   a pair the runtime offered but did not merge,
     *                   counted at the HIGHER ordinal -- the partner had
     *                   already been walked but never reached resolved[],
     *                   because it was backface-culled, clipped, degenerate
     *                   or hit the buffer ceiling. Expected nonzero (the
     *                   frame this shipped with culls over half of every
     *                   triangle it transforms) and not a fault, but it is
     *                   the honest measure of merges left on the table. */
    uint32_t quads_merged;
    uint32_t quad_map_mismatch;
    uint32_t quad_pair_not_adjacent;
    uint32_t quad_ordinal_past_row;
    uint32_t quad_pairs_declined;

    /* Demo-path Task 0 timing. Yaul initializes the master FRT at phi/8;
     * CPU_FRT_NTSC_320_8_COUNT_1MS therefore converts ticks to milliseconds.
     * Each sample is a modulo-2^16 delta, so one counter wrap is defined. */
    uint32_t sim_frt_ticks_last;
    uint32_t sim_frt_ticks_accum;
    uint32_t sim_tick_count;
    uint32_t render_frt_ticks_last;
    /* Demo-path diagnostics; append-only so existing probe offsets remain
     * stable. These count the live actor pass separately from terrain. */
    uint32_t demo_actor_vertices_valid;
    uint32_t demo_actor_primitives_emitted;
    uint32_t demo_actor_snapshot_valid;
    uint32_t demo_actor_pose_vertices;
    uint32_t slave_jobs_completed;
    uint32_t slave_busy_ticks;
    uint32_t master_wait_ticks;
    uint32_t slave_timeouts;
    /* Task 7 utilization evidence. These are append-only profile fields so
     * archived probe offsets remain valid. render_frt_ticks_accum is the
     * denominator for the cumulative slave-share calculation; the residency
     * fields describe the fixed sourceboot BOB display configuration. */
    uint32_t render_frt_ticks_accum;
    uint32_t vdp1_commands_last;
    uint32_t vdp1_vram_bytes;
    uint32_t vdp2_display_mask;
    uint32_t vdp2_vram_bytes;
    /* Demo BOB visibility diagnostics; append-only. These are cumulative
     * counters, like the existing frame/profile fields, and separate the
     * coarse bounds policy from near-plane and screen degeneracy loss. */
    uint32_t demo_bob_primitives_visible;
    uint32_t demo_bob_primitives_radius_rejected;
    uint32_t demo_bob_primitives_near_rejected;
    uint32_t demo_bob_primitives_degenerate;
    /* Task 3 Z-Treme-style spatial admission diagnostics; append-only. */
    uint32_t demo_bob_nodes_visited;
    uint32_t demo_bob_nodes_inside;
    uint32_t demo_bob_nodes_intersecting;
    uint32_t demo_bob_nodes_outside;
    uint32_t demo_bob_primitives_spatial_admitted;
    uint32_t demo_bob_primitives_spatial_dropped;
    uint32_t demo_bob_clip_away;
    uint32_t demo_bob_clip_to_one;
    uint32_t demo_bob_clip_to_two;
    uint32_t demo_bob_clip_recovery;
    uint32_t demo_bob_clip_overflow;
    uint32_t demo_bob_results_master;
    uint32_t demo_bob_results_slave;
    uint32_t demo_bob_result_reserve_rejects;
    /* Task 6: explicit frame-ownership evidence.  These are sourceboot's
     * measured CPU-staging lifecycle counters; a non-zero destination-bank
     * capability bit is intentionally not assumed from staging alone. */
    uint32_t vdp1_bank_generation;
    uint32_t vdp1_bank_submitted;
    uint32_t vdp1_bank_displayed;
    uint32_t vdp1_bank_overwrite_attempts;
    uint32_t vdp1_bank_late_dma;
    uint32_t vdp1_command_highwater;
    uint32_t vdp1_gouraud_highwater;
    /* Task 8: dynamic shared-vertex tier diagnostics. */
    uint32_t demo_lod_tier_near;
    uint32_t demo_lod_tier_mid;
    uint32_t demo_lod_tier_far;
    uint32_t demo_lod_transitions;
    uint32_t demo_lod_primitives_suppressed;
    uint32_t demo_lod_texture_downgrades;
    uint32_t demo_lod_resident_bytes;
    /* Terrain emission-path evidence. These count intentional flat RGB1555
     * commands separately from successful Gouraud allocations; allocation
     * exhaustion remains reported by gouraud_bank_overflow above. */
    uint32_t flat_primitives;
    uint32_t gouraud_primitives;
    /* Compact/fused terrain staging. Descriptor traffic excludes the private
     * 32-byte command images; legacy fallbacks count records whose immutable
     * material template could not be patched by the owning classify lane. */
    uint32_t demo_bob_terrain_descriptor_bytes_written;
    uint32_t demo_bob_terrain_descriptor_bytes_read;
    uint32_t demo_bob_terrain_legacy_fallbacks;
    uint32_t demo_bob_terrain_sequence_rejects;
    /* Dual-SH2/dual-VDP ownership evidence. These diagnostics describe the
     * existing pipeline only: master keeps game state, final VDP1 order, and
     * presentation; the slave receives only its bounded terrain range. */
    uint32_t master_worker_started;
    uint32_t slave_worker_started;
    uint32_t vdp1_commands;
    uint32_t vdp2_active_layers;
    uint32_t pipeline_faults;
    /* Task 10 Gouraud-fast-path diagnostics. Appended so existing counters
     * retain their offsets; evidence only, never a frame-policy input. */
    uint32_t gouraud_tables_saved;
    uint32_t gouraud_bytes_saved;
    /* Task 11 VDP2 HUD diagnostics. These are published measurements, not
     * scheduling inputs: transform/order are actual renderer work counters;
     * DMA and VDP1 values are FRT wait durations captured at their real
     * fences. Kept as a final suffix for capture ABI compatibility. */
    uint32_t master_transform_count;
    uint32_t slave_transform_count;
    uint32_t ordering_count;
    uint32_t dma_wait_ticks_last;
    uint32_t dma_wait_ticks_accum;
    uint32_t vdp1_wait_ticks_last;
    uint32_t vdp1_wait_ticks_accum;
    /* Task 1 overlapped-render checkpoint: copied from the source runtime
     * after each authoritative tick. Appended to preserve capture offsets. */
    uint32_t scene_graph_walks;
    uint32_t scene_graph_walks_suppressed;
    /* Emergency A9.0 cadence diagnostics. These append-only counters expose
     * the VBlank generation consumed by the terminal presentation boundary
     * and every eligible VBlank credit deliberately dropped after recovery. */
    uint32_t vblank_presentation_generation;
    uint32_t sim_vblank_credit_dropped;
    /* Task 3 pre-transform admission evidence. The active demo currently
     * treats its generated BOB span as one coarse cluster before preserving
     * the existing per-primitive projected rejection after transform. */
    uint32_t demo_render_clusters_tested;
    uint32_t demo_render_clusters_admitted;
    uint32_t demo_positions_admitted;
    uint32_t demo_positions_transformed;
    /* Task 4 actor meshlet admission evidence. These remain diagnostics only;
     * final command ownership and presentation stay on the master. */
    uint32_t demo_actor_meshlets_tested;
    uint32_t demo_actor_meshlets_admitted;
    uint32_t demo_actor_meshlets_culled;
    uint32_t demo_actor_positions_admitted;
    /* A5.9 scheduling evidence. These are the most recently completed
     * render generation, not cumulative counters. Phase order is world
     * admit/lower then actor admit/lower; the HUD uses the same order. */
    uint32_t render_job_master_world_admit_claims;
    uint32_t render_job_master_world_lower_claims;
    uint32_t render_job_master_actor_admit_claims;
    uint32_t render_job_master_actor_lower_claims;
    uint32_t render_job_slave_world_admit_claims;
    uint32_t render_job_slave_world_lower_claims;
    uint32_t render_job_slave_actor_admit_claims;
    uint32_t render_job_slave_actor_lower_claims;
    uint32_t render_job_notified_generation;
    uint32_t render_job_retired_generation;
    uint32_t render_job_master_wait_iterations;
    uint32_t render_job_failures;
    uint32_t render_job_quarantined;
    /* A8 transfer-pipeline diagnostics. The two transport waits remain zero
     * on the ordinary deferred path; overwrite and terminal fences are timed
     * separately so VDP1W no longer conflates unrelated boundaries. */
    uint32_t command_cpu_dmac_wait_ticks_last;
    uint32_t command_cpu_dmac_wait_ticks_accum;
    uint32_t gouraud_scu_dma_wait_ticks_last;
    uint32_t gouraud_scu_dma_wait_ticks_accum;
    uint32_t vdp1_overwrite_wait_ticks_last;
    uint32_t vdp1_overwrite_wait_ticks_accum;
    uint32_t vdp1_bank_unavailable_skips;
    uint32_t vdp1_terminal_fence_wait_ticks_last;
    uint32_t vdp1_terminal_fence_wait_ticks_accum;
    uint32_t vdp1_transfer_faults;
    uint32_t vdp1_transfer_queued_not_started;
} sm64_saturn_fast3d_profile_t;

/* Screen-space position + per-corner color for one already-transformed,
 * projected, culled, and depth-bucketed triangle. Populated by
 * saturn_fast3d_frontend.c (no Yaul dependency); consumed by
 * saturn_fast3d_vdp1_emit.c (Yaul-dependent) to write real VDP1
 * commands. Corner order is (i0, i1, i2, i2) for a triangle -- the last
 * vertex duplicated, now explicitly by the resolve stage rather than
 * implicitly at emit time -- ready to hand to VDP1's degenerate-quad
 * polygon command.
 *
 * corner_rgb1555[3] (Gouraud shading, design spec 2026-07-24) replaces
 * the single flat color this struct carried before: Task 3 made each
 * vertex's lit/unlit color correct, but this struct was still discarding
 * corners 1 and 2. Grows the struct 16->20 bytes; x1536 resolved slots
 * = +6 KiB static (see this file's HWRAM budget comment on
 * sm64_saturn_fast3d_frontend_t below for the live margin -- do not
 * re-quote a figure from memory, measure it). Since Task 6, the emit stage
 * (saturn_fast3d_vdp1_emit.c) uses corner_rgb1555[0..2] as the real
 * per-corner VDP1 Gouraud table for every triangle a table could be
 * allocated for; corner_rgb1555[0] alone is read only in the counted
 * bank-exhausted/partition-unusable fallback case (CC_REPLACE flat
 * color, degradation contract). */
/* Four corners, not three. VDP1's native primitive is a quadrilateral, so
 * a triangle and a quad cost the same one command; corner 3 is the real
 * fourth vertex for a merged quad, or a copy of corner 2 for a triangle.
 * Making the duplication explicit here (rather than implicit in the emit
 * adapter) is what lets the resolve stage produce true quads without the
 * emit stage needing to know which it is looking at.
 *
 * HWRAM cost: 26 bytes per entry, up from 20, times
 * SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES -- see the budget comment on
 * that macro before raising either number. */
typedef struct sm64_saturn_resolved_triangle {
    int16_t x[4];
    int16_t y[4];
    uint16_t corner_rgb1555[4];
    uint16_t depth_bucket;
} sm64_saturn_resolved_triangle_t;

typedef struct sm64_saturn_fast3d_viewport {
    int16_t x;
    int16_t y;
    int16_t width;
    int16_t height;
} sm64_saturn_fast3d_viewport_t;

/* One decoded source vertex: model-space position and flat RGBA color
 * (Vtx_t.cn[4]). Position is `float`, matching Vtx_t.ob[3] under this
 * build's GBI_FLOATS configuration (include/PR/gbi.h:1112-1121) -- NOT
 * the classic short[3] model-space encoding. This keeps the frontend's
 * per-triangle scratch transform math (Task 9) in one consistent domain
 * without an extra, unnecessary Q16.16 round-trip for data that already
 * arrives as float on this target. */
typedef struct sm64_saturn_fast3d_vertex {
    float x, y, z;
    uint8_t r, g, b, a;
} sm64_saturn_fast3d_vertex_t;

typedef struct sm64_saturn_fast3d_cached_vertex {
    int32_t clip_x;
    int32_t clip_y;
    int32_t clip_w;
    uint32_t generation;
} sm64_saturn_fast3d_cached_vertex_t;


/* Task 2 capture-only differential record. Compiled only into a trace build;
 * normal sourceboot pays neither the HWRAM footprint nor per-triangle stores.
 * Records are captured after all three float clip transforms pass w > 0 and
 * before cull/divide policy, preserving the direct arithmetic oracle. */
#ifdef SM64_SATURN_FAST3D_Q16_TRACE
#define SM64_SATURN_FAST3D_Q16_TRACE_CAPACITY 16U
#define SM64_SATURN_FAST3D_Q16_TRACE_MAGIC 0x51363454U /* "Q64T" */

typedef struct sm64_saturn_fast3d_q16_trace_record {
    int32_t mp[4][4];
    int16_t viewport[4];
    uint32_t geometry_mode;
    float source_xyz[3][3];
    float float_clip_xyw[3][3];
    uint8_t dir_col[3];
    uint8_t amb_col[3];
    int8_t dir_dir[3];
    uint8_t num_lights;
    uint8_t lighting_enabled;
    uint8_t reserved[2];
} sm64_saturn_fast3d_q16_trace_record_t;

typedef struct sm64_saturn_fast3d_q16_trace {
    uint32_t magic;
    uint32_t version;
    uint32_t write_count;
    uint32_t dropped_count;
    sm64_saturn_fast3d_q16_trace_record_t
        records[SM64_SATURN_FAST3D_Q16_TRACE_CAPACITY];
} sm64_saturn_fast3d_q16_trace_t;
#endif

/* HWRAM budget note: this struct is ~5,036 bytes (measured via sizeof against
 * the real F3DEX_GBI_2E build flags), grown from 44 bytes by this task's
 * addition of matrix_stack/vertices[]/resolved[]. The design spec measured
 * ~9,628 bytes of free HWRAM for the sourceboot target before this task
 * landed (docs/superpowers/specs/2026-07-20-fast3d-matrix-stack-design.md),
 * initially leaving roughly 4,600 bytes of estimated headroom after this
 * struct's data alone.
 *
 * UPDATE (post Task 14, full cross-compiled link): that 4,600-byte figure
 * only tracked this one struct's DATA size, not the .text code the rest of
 * this plan's decode/transform/VDP1-emission logic added afterward -- and
 * .text/.rodata/.data/.bss all draw from the same shared `ram` MEMORY region
 * in sourceboot-cart.x (ORIGIN 0x06004000, LENGTH 0xFC000), so there is no
 * separate "code budget" distinct from this "data budget." The real,
 * measured margin after Task 14's full link is ~380 bytes (___end vs. the
 * ram region's top, 0x06100000 - 0x060ffe84) -- under 1% of the region size.
 * If a future change needs more headroom, SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES
 * (26 bytes/entry as of the general-quad-merging design's four-corner
 * change, 2026-07-25 -- was 20, and 16 before the Gouraud design's
 * corner_rgb1555[3]) and SM64_SATURN_FAST3D_MAX_VERTICES (16 bytes/entry)
 * are the two knobs to shrink first (Task 10's VDP1 command list lives in LWRAM,
 * not HWRAM, so it doesn't compete with this budget) -- but note the region
 * is now tight enough that even a modest amount of new .text elsewhere in
 * sourceboot could overflow it before these knobs are touched at all.
 *
 * UPDATE (2026-07-22, capacity increase): the ~380-byte-free HWRAM
 * figure above is now stale. Earlier the same day, SM64's main pool
 * (previously 0x30000-0x60000 bytes of HWRAM .bss) moved to LWRAM
 * (src/port/saturn/sourceboot/main.c) to fix a boot-fatal heap
 * collision -- that freed ~191 KiB of HWRAM (___end measured at
 * 0x060D0284 vs. the ram region's top 0x06100000). This struct's
 * resolved[] array was grown from 192 to 1536 entries (+21,504 bytes)
 * on the strength of that headroom: 1,536 is grounded in this
 * project's own captured real Bob-omb Battlefield frame data
 * (1,365-1,431 triangles/frame, saturn_fast3d_frontend.c's NEAR/FAR
 * depth comment), stays comfortably under Sega's own SGL 3.02j default
 * of 1,786 polygons/frame (docs/saturn/SGL_REFERENCE_NOTES.md), and is
 * far inside this project's own castleviewer precedent (a working
 * scene with 1,032-1,105 live VDP1 commands, RENDERER_PRIOR_ART.md).
 * Re-measure HWRAM headroom (nm on ___end) after this change and
 * before adding any further static HWRAM consumer.
 *
 * UPDATE (2026-07-24, measured): the live margin is 149,084 bytes
 * (145.6 KiB) -- ___end at 0x060db9a4 against the ram region's top
 * 0x06100000. The "~191 KiB" above was already stale before the Gouraud
 * sprint and then got re-quoted twice more by that sprint's own
 * additions (+6 KiB for corner_rgb1555, +12 KiB for the Gouraud staging
 * array), each charging itself against the same figure without
 * accounting for the other. The lesson is the one already written above
 * and repeatedly ignored: MEASURE, do not re-quote. Two changes since
 * make that easier to honor -- sourceboot-cart.x now ASSERTs a 4 KiB
 * link-time floor for libyaul's TLSF control block (the linker
 * otherwise only enforces margin >= 0, which silently permits a
 * pre-main() heap overrun), and this note records the method rather
 * than only the number. */
typedef struct sm64_saturn_fast3d_frontend {
    sm64_saturn_fast3d_profile_t profile;
    sm64_saturn_matrix_stack_t matrix_stack;
    sm64_saturn_fast3d_viewport_t viewport;
    uint32_t geometry_mode;
    /* Gouraud shading addition (design spec 2026-07-24): light state
     * decoded from G_MOVEWORD/G_MOVEMEM (saturn_fast3d_frontend.c),
     * re-transformed lazily against the modelview top on the next lit
     * G_VTX (Task 3). Deliberately placed alongside geometry_mode, not
     * inside profile: it must persist across submit()'s per-frame
     * profile memset, mirroring gfx_pc.c's own persistent
     * rsp.current_lights/current_num_lights/lights_changed state.
     * Confirmed by reading submit()'s reset code (saturn_fast3d_
     * frontend.c) -- it only memsets profile and zeroes
     * resolved_count, exactly like matrix_stack/geometry_mode/
     * vertices[] already aren't touched either. */
    sm64_saturn_light_state_t lights;
    sm64_saturn_fast3d_vertex_t vertices[SM64_SATURN_FAST3D_MAX_VERTICES];
    sm64_saturn_fast3d_cached_vertex_t
        transformed[SM64_SATURN_FAST3D_MAX_VERTICES];
    uint32_t transform_generation;
    sm64_saturn_resolved_triangle_t
        resolved[SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
    uint16_t resolved_count;

    /* Quad-merge state for the display list currently being interpreted
     * (general-quad-merging design, 2026-07-25).
     *
     * quad_entries points into the generated table
     * (build/saturn/sourceboot/generated/saturn_quad_map.c), indexed by
     * triangle_ordinal -- the triangle command's index WITHIN ITS OWN
     * display list, which is the only key the runtime can reproduce: a
     * global ordinal would be shifted by GEO_SWITCH_CASE selection and by
     * the master list's 8-layer bucketing. NULL means this list has no map
     * row, which decodes exactly like an all-zero entry: do not merge.
     *
     * Declared as `const uint32_t *` rather than as the generated
     * `sm64_saturn_quad_map_entry_t *` on purpose -- this header is
     * deliberately free of generated-build-artifact includes so it stays
     * host-includable without running the offline compiler first (see the
     * file-header note about host-testability). saturn_fast3d_frontend.c
     * carries a _Static_assert tying the two types together, so a change
     * to the generated width is a compile error rather than a silent
     * misread.
     *
     * All three are per-display-list, saved and restored across a G_DL
     * call by sm64_saturn_fast3d_frontend_submit's own stack, alongside
     * its return_stack. They live here rather than as submit() locals
     * because the resolve stage, which is where the lookup is consumed,
     * only ever receives the frontend pointer. */
    const uint32_t *quad_entries;
    uint16_t quad_entry_count;
    uint16_t triangle_ordinal;

    /* Ordinal -> resolved[] slot for the display list being walked, so the
     * second triangle of a pair can merge into the first's primitive no
     * matter how far apart their ordinals are.
     *
     * quad_slot_stamp[] is what makes this safe without ever clearing the
     * arrays. A slot is live only while its stamp equals the CURRENT
     * quad_slot_generation, and every display list entered takes a fresh
     * generation, so a slot index recorded by one list -- or by an earlier
     * invocation of the same list, or by a previous frame -- can never be
     * mistaken for a live one. Clearing on entry would be the obvious
     * alternative and is strictly worse: it costs a memset per G_DL and
     * still leaves a same-list-reinvoked hole.
     *
     * Only triangles that actually reached resolved[] record a slot. A
     * culled or clipped triangle advances the ordinal (the offline walker
     * counts commands, not survivors) but leaves no stamp, so its partner
     * finds nothing live and declines instead of merging into a slot that
     * belongs to some other primitive.
     *
     * quad_slot_generation is the CURRENT list's stamp, saved and restored
     * across a G_DL call with the ordinal, so a caller's slots survive a
     * nested list except at the ordinals that list happened to overwrite.
     *
     * quad_slot_next_generation is a separate, strictly monotonic allocator
     * and is deliberately NOT restored. Deriving a new generation by
     * incrementing the current one instead looks equivalent and is not: a
     * caller that makes two nested calls restores its own generation
     * between them, so both children would be handed the SAME stamp and
     * the second could read the first's slot indices as live -- a
     * cross-display-list merge into another primitive. That defect was
     * live in this file for one build and was caught by the
     * resolved_count range guard firing in a real capture, not by
     * inspection. Allocate generations here and nowhere else.
     *
     * quad_slot_max_z[] rides along because a merged quad's depth bucket is
     * recomputed over all four corners; the earlier half's furthest-corner
     * depth has to survive until its partner arrives. */
    int32_t quad_slot_max_z[SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY];
    uint16_t quad_slot[SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY];
    uint16_t quad_slot_stamp[SM64_SATURN_FAST3D_QUAD_SLOT_CAPACITY];
    uint16_t quad_slot_generation;
    uint16_t quad_slot_next_generation;
#ifdef SM64_SATURN_FAST3D_Q16_TRACE
    sm64_saturn_fast3d_q16_trace_t q16_trace;
#endif
} sm64_saturn_fast3d_frontend_t;

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend);
void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context);

#endif
