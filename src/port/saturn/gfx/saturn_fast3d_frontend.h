#ifndef SM64_SATURN_FAST3D_FRONTEND_H
#define SM64_SATURN_FAST3D_FRONTEND_H

#include <stdint.h>

#include "port/saturn/runtime/saturn_source_runtime.h"
#include "saturn_matrix.h"

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
} sm64_saturn_fast3d_profile_t;

/* Screen-space position + flat color for one already-transformed,
 * projected, culled, and depth-bucketed triangle. Populated by
 * saturn_fast3d_frontend.c (no Yaul dependency); consumed by
 * saturn_fast3d_vdp1_emit.c (Yaul-dependent) to write real VDP1
 * commands. Corner order is (i0, i1, i2, i2) -- the last vertex
 * duplicated -- ready to hand to VDP1's degenerate-quad polygon command. */
typedef struct sm64_saturn_resolved_triangle {
    int16_t x[3];
    int16_t y[3];
    uint16_t color_rgb1555;
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
 * (16 bytes/entry) and SM64_SATURN_FAST3D_MAX_VERTICES (16 bytes/entry) are
 * the two knobs to shrink first (Task 10's VDP1 command list lives in LWRAM,
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
 * before adding any further static HWRAM consumer. */
typedef struct sm64_saturn_fast3d_frontend {
    sm64_saturn_fast3d_profile_t profile;
    sm64_saturn_matrix_stack_t matrix_stack;
    sm64_saturn_fast3d_viewport_t viewport;
    uint32_t geometry_mode;
    sm64_saturn_fast3d_vertex_t vertices[SM64_SATURN_FAST3D_MAX_VERTICES];
    sm64_saturn_resolved_triangle_t
        resolved[SM64_SATURN_FAST3D_MAX_RESOLVED_TRIANGLES];
    uint16_t resolved_count;
} sm64_saturn_fast3d_frontend_t;

void sm64_saturn_fast3d_frontend_init(
    sm64_saturn_fast3d_frontend_t *frontend);
void sm64_saturn_fast3d_frontend_submit(struct SPTask *task, void *context);

#endif
