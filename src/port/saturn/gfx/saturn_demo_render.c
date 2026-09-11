/* Measured hot path: spend bounded code space on speed (12 FPS continuation). */
#if defined(__sh__) && defined(__GNUC__)
#pragma GCC optimize ("O2")
#endif
#include "saturn_demo_render.h"
#include "../platform/saturn_cart_code.h"

#ifndef SATURN_DEMO_BSP_FRAGMENT_FLAT
#define SATURN_DEMO_BSP_FRAGMENT_FLAT 0
#endif

/* Terrain Gouraud shading. Owner decision 2026-09-11: removed. Terrain lowers
 * flat; actors keep their shading.
 *
 * Three sites have to agree or the result is garbage rather than flat, which
 * is why this is one switch and not an edit at the emit site:
 *   1. demo_resolve_terrain_command_templates() bakes the VDP1 draw mode into
 *      each per-primitive template variant at load time. A template baked with
 *      CC_GOURAUD carries SM64_SATURN_TERRAIN_PATCH_GOURAUD and expects a
 *      per-frame GRDA patch.
 *   2. demo_emit_terrain_result() picks the runtime shade path. If it says
 *      flat while the template was baked Gouraud, the GRDA patch never runs
 *      and the command draws CC_GOURAUD against an unpatched base address.
 *   3. The legacy per-entry fallback in the same function.
 * sm64_saturn_terrain_lower_gouraud() allocates only for SHADE_GOURAUD and is
 * a clean no-op otherwise, so removing the path removes the allocation.
 *
 * Kept as a switch rather than deleted so the change is bisectable and the
 * shading can be compared without reconstructing it. */
#ifndef SATURN_DEMO_TERRAIN_GOURAUD
#define SATURN_DEMO_TERRAIN_GOURAUD 0
#endif

/* The generic actor-instance queue is infrastructure-only until the
 * production actor cutover has a source-derived family/meshlet package. Keep
 * the feature switch visible at this boundary so feature-off builds prove
 * that ACTOR_ADMIT/ACTOR_LOWER still execute the established Mario callbacks.
 * Feature-on is intentionally fail-closed here; it must not silently
 * reinterpret the Mario pair as a generic actor renderer. */
#ifndef SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
#define SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE 0
#endif

/* S1.11: run the generic-actor phase on the slave CPU, overlapped with the
 * master's terrain/Mario preparation.
 *
 * ON in the shipped profile. It was held off until it had been cleared on a
 * caches-ON basis, because the default cadence harness runs with SH-2 cache
 * emulation disabled and therefore cannot observe a coherency defect in this
 * path at all -- a green caches-off run is not evidence of correctness here.
 * With caches emulated the two arms render byte-identical frames and the
 * offload still measures +5.76%, so the gate is met.
 *
 * The fallback below stays 0 deliberately: a build that never passed the -D
 * (host fixtures) has no slave to dispatch to, and must run the phase inline.
 * The Makefile always passes it, so the fallback never applies on target.
 * `sourceboot-bob-coins-noslave-v1.json` is the offload-off control profile,
 * which keeps both arms buildable from one tree for future A/B work. */
#ifndef SATURN_FEATURE_ACTOR_PHASE_SLAVE
#define SATURN_FEATURE_ACTOR_PHASE_SLAVE 0
#endif

/* S1.12 step 3: build the generic actors' VDP1 commands on the slave.
 *
 * This targets the largest single block left in the frame -- `master_finalize`
 * measured 5.000 VB, 47.7%, executed by the master alone while the slave sits
 * idle (its measured idle is 5.687 VB/frame, almost exactly this window).
 * Actors are 53.98% of emitted commands.
 *
 * ON in the shipped profile. It was held off until cleared on a caches-ON
 * basis, because the default cadence harness runs with SH-2 cache emulation
 * disabled and cannot observe a coherency defect in this path at all. With
 * caches emulated the two arms render byte-identical frames and the offload
 * still measures +6.8%, so the gate is met.
 * `sourceboot-bob-coins-noslave-v1.json` keeps both arms buildable. */
#ifndef SATURN_FEATURE_ACTOR_EMIT_SLAVE
#define SATURN_FEATURE_ACTOR_EMIT_SLAVE 0
#endif

/* Staging capacity for slave-built actor commands. Measured need is ~310
 * commands per present; this is deliberate headroom, and the dispatch falls
 * back to inline master emit rather than truncating if a frame ever exceeds
 * it -- silently dropping actor geometry would look like a win in cadence. */
#define DEMO_ACTOR_EMIT_STAGING_CAPACITY 512U

#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include "game/camera.h"
#include "saturn_dual_frame_bank.h"
#include "saturn_actor_gouraud_reservation.h"
#include "saturn_actor_meshlets.h"
#include "saturn_actor_material.h"
#include "saturn_actor_terrain_order.h"
#include "saturn_actor_pose.h"
#include "saturn_actor_runtime_handoff.h"
#include "saturn_actor_texture_residency.h"
#include "saturn_gouraud.h"
#include "saturn_ir_texture.h"
#include "saturn_ir_transform.h"
#include "saturn_lod_lifetime.h"
#include "saturn_matrix_kernels.h"
#include "saturn_render_job_bridge.h"
#include "saturn_render_callback_context.h"
#include "saturn_render_job_graph.h"
#include "saturn_render_job_queue.h"
#include "saturn_render_job_runtime.h"
#include "saturn_render_lifecycle.h"
#include "saturn_render_output_bank.h"
#include "../runtime/saturn_prenotify_profile.h"
/* T2.9: demo_spatial_admit()'s caller-side half.  Diagnostic-only; the
 * product arm is unchanged. */
#if defined(SATURN_DIAGNOSTIC_MODE) && SATURN_DIAGNOSTIC_MODE != 0 && \
    defined(__sh__)
#define SM64_SATURN_ADMIT_DIAG 1
#else
#define SM64_SATURN_ADMIT_DIAG 0
#endif
#include "saturn_render_payload_bank.h"
#include "saturn_scene_admission.h"
#include "saturn_terrain_command_template.h"
#include "saturn_terrain_emit_policy.h"
#include "saturn_terrain_fused.h"
#include "saturn_terrain_queue_handoff.h"
#include "saturn_transform.h"
#include "saturn_visible_position_set.h"
#include "../sourceboot/source_scene_bundle.h"
#include "bob_scene.h"
#include "bob_bsp.h"

#if SM64_SATURN_BOB_SCENE_BSP_CONTENT_ID != SM64_SATURN_BOB_BSP_CONTENT_ID
#error "bob_scene.h and bob_bsp.h were generated from different node spans"
#endif
#if defined(SATURN_DEMO_BSP_FRAGMENTS) && SATURN_DEMO_BSP_FRAGMENTS
#include "bob_bsp_fragments.h"
#undef SM64_SATURN_BOB_POSITION_COUNT
#undef SM64_SATURN_BOB_PRIMITIVE_COUNT
#undef sm64_saturn_bob_positions
#undef sm64_saturn_bob_primitives
#define SM64_SATURN_BOB_POSITION_COUNT SM64_SATURN_BOB_FRAGMENT_POSITION_COUNT
#define SM64_SATURN_BOB_PRIMITIVE_COUNT SM64_SATURN_BOB_FRAGMENT_PRIMITIVE_COUNT
#define sm64_saturn_bob_positions sm64_saturn_bob_fragment_positions
#define sm64_saturn_bob_primitives sm64_saturn_bob_fragment_primitives
#define sm64_saturn_bob_primitive_t sm64_saturn_bob_fragment_primitive_t
#define sm64_saturn_bob_lod_mid_mask sm64_saturn_bob_fragment_lod_mid_mask
#define sm64_saturn_bob_lod_far_mask sm64_saturn_bob_fragment_lod_far_mask
#undef SM64_SATURN_BOB_LOD_TIER_COUNT
#undef SM64_SATURN_BOB_LOD_POSITION_REF_COUNT
#undef sm64_saturn_bob_lod_position_ref_offsets
#undef sm64_saturn_bob_lod_position_refs
#undef SM64_SATURN_BOB_CLUSTER_COUNT
#undef SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT
#undef sm64_saturn_bob_render_clusters
#undef sm64_saturn_bob_cluster_position_refs
#define SM64_SATURN_BOB_LOD_TIER_COUNT SM64_SATURN_BOB_FRAGMENT_LOD_TIER_COUNT
#define SM64_SATURN_BOB_LOD_POSITION_REF_COUNT SM64_SATURN_BOB_FRAGMENT_LOD_POSITION_REF_COUNT
#define sm64_saturn_bob_lod_position_ref_offsets sm64_saturn_bob_fragment_lod_position_ref_offsets
#define sm64_saturn_bob_lod_position_refs sm64_saturn_bob_fragment_lod_position_refs
#define SM64_SATURN_BOB_CLUSTER_COUNT SM64_SATURN_BOB_FRAGMENT_CLUSTER_COUNT
#define SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT SM64_SATURN_BOB_FRAGMENT_CLUSTER_POSITION_REF_COUNT
#define sm64_saturn_bob_render_clusters sm64_saturn_bob_fragment_render_clusters
#define sm64_saturn_bob_cluster_position_refs sm64_saturn_bob_fragment_cluster_position_refs
#endif
#include "saturn_mario_actor_mesh.h"
/* Exact RGB1555 values for each material's baked 5-bit light. This is the
 * fixed Gouraud table consumed by VDP1: no dynamic colour math belongs in
 * the HWRAM-linked lowerer. */
static const uint16_t sm64_saturn_mario_gouraud_color
    [11U][32U] = {
    {0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007, 0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E, 0x800F, 0x8010, 0x8011, 0x8012, 0x8013, 0x8014, 0x8015, 0x8016, 0x8017, 0x8018, 0x8019, 0x801A, 0x801B, 0x801C, 0x801D, 0x801E, 0x801F},
    {0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007, 0x8008, 0x8009, 0x800A, 0x800B, 0x800C, 0x800D, 0x800E, 0x800F, 0x8010, 0x8011, 0x8012, 0x8013, 0x8014, 0x8015, 0x8016, 0x8017, 0x8018, 0x8019, 0x801A, 0x801B, 0x801C, 0x801D, 0x801E, 0x801F},
    {0x8000, 0x8400, 0x8800, 0x8C00, 0x9000, 0x9400, 0x9800, 0x9C00, 0xA000, 0xA400, 0xA800, 0xAC00, 0xB000, 0xB400, 0xB800, 0xBC00, 0xC000, 0xC400, 0xC800, 0xCC00, 0xD000, 0xD400, 0xD800, 0xDC00, 0xE000, 0xE400, 0xE800, 0xEC00, 0xF000, 0xF400, 0xF800, 0xFC00},
    {0x8000, 0x8400, 0x8800, 0x8C00, 0x9000, 0x9400, 0x9800, 0x9C00, 0xA000, 0xA400, 0xA800, 0xAC00, 0xB000, 0xB400, 0xB800, 0xBC00, 0xC000, 0xC400, 0xC800, 0xCC00, 0xD000, 0xD400, 0xD800, 0xDC00, 0xE000, 0xE400, 0xE800, 0xEC00, 0xF000, 0xF400, 0xF800, 0xFC00},
    {0x8000, 0x8400, 0x8820, 0x8C41, 0x9061, 0x9462, 0x9882, 0x9CA3, 0xA0C3, 0xA4C4, 0xA8E4, 0xAD05, 0xB125, 0xB546, 0xB946, 0xBD67, 0xC187, 0xC5A8, 0xC9A8, 0xCDC9, 0xD1E9, 0xD60A, 0xDA2A, 0xDE2B, 0xE24B, 0xE66C, 0xEA8C, 0xEE8D, 0xF2AD, 0xF6CE, 0xFAEE, 0xFF0F},
    {0x8000, 0x8400, 0x8820, 0x8C41, 0x9061, 0x9462, 0x9882, 0x9CA3, 0xA0C3, 0xA4C4, 0xA8E4, 0xAD05, 0xB125, 0xB546, 0xB946, 0xBD67, 0xC187, 0xC5A8, 0xC9A8, 0xCDC9, 0xD1E9, 0xD60A, 0xDA2A, 0xDE2B, 0xE24B, 0xE66C, 0xEA8C, 0xEE8D, 0xF2AD, 0xF6CE, 0xFAEE, 0xFF0F},
    {0x8000, 0x8400, 0x8820, 0x8C41, 0x9061, 0x9462, 0x9882, 0x9CA3, 0xA0C3, 0xA4C4, 0xA8E4, 0xAD05, 0xB125, 0xB546, 0xB946, 0xBD67, 0xC187, 0xC5A8, 0xC9A8, 0xCDC9, 0xD1E9, 0xD60A, 0xDA2A, 0xDE2B, 0xE24B, 0xE66C, 0xEA8C, 0xEE8D, 0xF2AD, 0xF6CE, 0xFAEE, 0xFF0F},
    {0x8000, 0x8400, 0x8820, 0x8C41, 0x9061, 0x9462, 0x9882, 0x9CA3, 0xA0C3, 0xA4C4, 0xA8E4, 0xAD05, 0xB125, 0xB546, 0xB946, 0xBD67, 0xC187, 0xC5A8, 0xC9A8, 0xCDC9, 0xD1E9, 0xD60A, 0xDA2A, 0xDE2B, 0xE24B, 0xE66C, 0xEA8C, 0xEE8D, 0xF2AD, 0xF6CE, 0xFAEE, 0xFF0F},
    {0x8000, 0x8000, 0x8000, 0x8400, 0x8400, 0x8800, 0x8800, 0x8C00, 0x8C00, 0x9000, 0x9000, 0x9000, 0x9400, 0x9400, 0x9800, 0x9800, 0x9C00, 0x9C00, 0xA000, 0xA000, 0xA400, 0xA400, 0xA400, 0xA800, 0xA800, 0xAC00, 0xAC00, 0xB000, 0xB000, 0xB400, 0xB400, 0xB820},
    {0x8000, 0x8421, 0x8842, 0x8C63, 0x9084, 0x94A5, 0x98C6, 0x9CE7, 0xA108, 0xA529, 0xA94A, 0xAD6B, 0xB18C, 0xB5AD, 0xB9CE, 0xBDEF, 0xC210, 0xC631, 0xCA52, 0xCE73, 0xD294, 0xD6B5, 0xDAD6, 0xDEF7, 0xE318, 0xE739, 0xEB5A, 0xEF7B, 0xF39C, 0xF7BD, 0xFBDE, 0xFFFF},
    {0x8000, 0x8000, 0x8000, 0x8400, 0x8400, 0x8800, 0x8800, 0x8C00, 0x8C20, 0x9020, 0x9020, 0x9020, 0x9420, 0x9420, 0x9820, 0x9820, 0x9C41, 0x9C41, 0xA041, 0xA041, 0xA441, 0xA441, 0xA441, 0xA841, 0xA861, 0xAC61, 0xAC61, 0xB061, 0xB061, 0xB461, 0xB461, 0xB882},
};
_Static_assert(sizeof(sm64_saturn_mario_gouraud_color) /
                   sizeof(sm64_saturn_mario_gouraud_color[0]) ==
                   SM64_MARIO_MATERIAL_COUNT,
               "fixed Mario Gouraud table must match generated materials");
/* Row nine is the existing neutral RGB1555 gray ramp.  VDP1 treats Gouraud
 * entries as signed corrections around 0xC210, while each command supplies
 * its material RGB base.  This one fixed light ramp therefore shades both
 * direct-color texture texels and solid Mario material surfaces without a
 * material hue being added twice. */
#define SM64_SATURN_MARIO_TEXTURE_GOURAUD_MATERIAL 9U
#if defined(SATURN_DEMO_MARIO_TEXTURES)
#include "mario_eye_uv_tiles.h"
#endif
#include "../gpl/slavedriver_dual_worker.h"
#include "../gpl/slavedriver_terrain_clip.h"
#include "../gpl/slavedriver_terrain_result.h"
#include "../gpl/slavedriver_terrain_worker.h"
#include "../gpl/ztreme_frustum.h"
#include "../gpl/ztreme_hot_promotion.h"

#ifndef SATURN_DEMO_NEAR_DEPTH
#define SATURN_DEMO_NEAR_DEPTH 128
#endif
#define DEMO_FAR_DEPTH 8192
#define DEMO_FOCAL_LENGTH 256
#define DEMO_CENTER_X 160
#define DEMO_CENTER_Y 112
#define DEMO_COORD_MIN (-1024)
#define DEMO_COORD_MAX 1023
#ifndef SATURN_DEMO_BUCKETS
/* SGL/Z-Treme use finer depth staging than the original 16-pass bring-up
 * sweep. Thirty-two keeps the bounded SRAM footprint while reducing
 * same-bucket painter ambiguity for overlapping BOB terrain. */
#define SATURN_DEMO_BUCKETS 32U
#endif
#define DEMO_BUCKETS SATURN_DEMO_BUCKETS
#define DEMO_CANCEL_POLL_INTERVAL 16U
#define DEMO_TERRAIN_RESULT_CAPACITY \
    (SM64_SATURN_BOB_PRIMITIVE_COUNT * 2U)
#ifndef SATURN_DEMO_VIEW_RADIUS
#define SATURN_DEMO_VIEW_RADIUS 6000
#endif
#ifndef SATURN_SLAVE_RENDER
#define SATURN_SLAVE_RENDER 1
#endif
#ifndef SATURN_DEMO_POLY_TIER
#define SATURN_DEMO_POLY_TIER 0
#endif
#ifndef SATURN_DEMO_HOT_PROMOTION
#define SATURN_DEMO_HOT_PROMOTION 0
#endif
#ifndef SATURN_DEMO_NEAR_CLIP
#define SATURN_DEMO_NEAR_CLIP 0
#endif
#ifndef SATURN_DEMO_BSP_ORDER
#define SATURN_DEMO_BSP_ORDER 1
#endif
#ifndef SATURN_DEMO_BSP_FRAGMENTS
#define SATURN_DEMO_BSP_FRAGMENTS 0
#endif
#if SATURN_DEMO_POLY_TIER < 0 || SATURN_DEMO_POLY_TIER > 2
#error "SATURN_DEMO_POLY_TIER must be 0 (reference), 1 (material), or 2 (far)"
#endif
#define DEMO_LOD_MANDATORY_ROUTE_PREFIX 128U
#if SATURN_DEMO_BSP_FRAGMENTS
#define DEMO_FRAGMENT_CACHE __attribute__((section(".lwram_bss")))
#else
#define DEMO_FRAGMENT_CACHE
#endif
#define DEMO_TERRAIN_TRANSFORM_CACHE __attribute__((section(".lwram_bss")))

/* Shared transform-once cache. Both SH-2s write their disjoint indices through
 * these normal cached pointers.  After the uncached release fence, each
 * reader keeps its own range cached and reads the peer range via the explicit
 * cache-through alias selected by demo_*_read().  This is deliberately more
 * precise than the earlier whole-cache-purge recovery. */
static sm64_saturn_vec3i_t s_view[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static sm64_saturn_projected_vertex_t s_projected[
    SM64_SATURN_BOB_POSITION_COUNT] DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_valid[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_use_mask[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_position_owner[SM64_SATURN_BOB_POSITION_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
#define DEMO_VISIBLE_POSITION_WORDS \
    SM64_SATURN_VISIBLE_POSITION_SET_WORDS(SM64_SATURN_BOB_POSITION_COUNT)
static uint32_t s_visible_position_words[DEMO_VISIBLE_POSITION_WORDS]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static sm64_saturn_visible_position_set_t s_visible_position_set;
/* Cross-CPU fence records and result-span headers MUST live in the uncached
 * partition (.uncached, addresses >= 0x20000000). The SH7604 has no
 * inter-CPU cache coherency: a cached-alias poll spins on the reader's own
 * stale line (it wrote the 0 reset itself), and a cached-alias span header
 * read at the join drops the slave's entire record span. This was the
 * Pipe 5 root cause -- both Mario and terrain vanished from one incoherent
 * header (stale count = slave terrain dropped; post-eviction fresh count =
 * arena overflow starving Mario's reservation). SlaveDriver's discipline is
 * an uncached release record plus cache-through peer reads; tools/saturn/
 * verify_dual_cpu_coherency.py pins these placements at build time. */
#define DEMO_CROSS_CPU_SHARED __attribute__((section(".uncached")))
/* CPU-only renderer state is not a VDP1/SCU transport object.  The arrays
 * below are either master-owned or disjoint lane-owned; payloads that cross
 * the worker boundary still use the explicit cache-through records and LWRAM
 * banks declared separately.
 *
 * Placement: the FULL hot working set lives in 32-bit HWRAM .bss again
 * (Sprint 2 T2.2 un-split).  These are per-primitive inner-loop operands.
 * The A9A baseline (5.29 FPS) kept them in 32-bit HWRAM .bss; the
 * memory-budget relief work (ec7b992a, then 91f02ffd) evicted them to
 * 16-bit LWRAM, which is the mechanism of the accepted 5.29 FPS collapsing
 * to ~1 FPS on this memory-bound loop.  The stage-1b three-way split
 * (49370e31) paid for the link with the actor scratch + workarea in LWRAM;
 * T1's attribution (sprint2-t1-hwram-attribution.md) then funded the full
 * return with peak-gated capacity shrinks (cmdt 2048->1664, GFX pool
 * 6400->4096, libyaul _private_pool 0xA000->0x4000 = 67,584 B recovered
 * against the 54,080 B home).  T2.0's reference sweep corroborates the
 * shape (L2: neither SlaveDriver nor Z-Treme places ANY per-frame working
 * set in LWRAM; Z-Treme's loader literally names "move the vertices to
 * high work ram").
 *
 *   - Terrain/primitive scratch (DEMO_CPU_WORK_CACHE, empty macro): HWRAM.
 *   - Actor-path-only scratch (DEMO_ACTOR_WORK_CACHE, empty macro): HWRAM.
 *   - The 43,776 B hot workarea (s_bob_hot_workarea below): HWRAM.
 *
 * Still in LWRAM, deliberately and recorded as the next rung:
 * _sourceboot_fast3d (44,616 B, main.c) -- the reclamation arithmetic
 * does not close for it (~13.5 KB spare after the un-split); T2.0 L3's
 * build-in-VRAM staging-window lever (~120 KB) is the unlock.
 * was: __attribute__((section(".lwram_bss"))) */
#define DEMO_CPU_WORK_CACHE
/* Actor-path-only scratch: every array below is read/written exclusively by
 * the actor lanes (demo_actor_queue_assemble_done, demo_reserve_mario_gouraud,
 * demo_emit_mario, demo_emit_mario_range) -- never by the terrain path.
 * Kept as a distinct placement class so the two sets stay independently
 * steerable; T2.2 returns it to HWRAM with the rest of the hot set (empty
 * macro, see the placement note above).
 * was: __attribute__((section(".lwram_bss"))) */
#define DEMO_ACTOR_WORK_CACHE
static sm64_saturn_dual_frame_bank_t s_transform_frame_bank
    DEMO_CROSS_CPU_SHARED;
static volatile uint16_t s_transform_phase_failed
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_terrain_result_spans_t s_terrain_spans_shared
    DEMO_CROSS_CPU_SHARED;
#define DEMO_SPATIAL_REF_SEEN_WORDS \
    ((SM64_SATURN_BOB_PRIMITIVE_COUNT + 31U) / 32U)
/* This compact first-reference-wins bitset replaces the former 867-byte
 * admission array. It is reset as a fixed number of words and never scanned:
 * traversal alone appends generated spans to the bounded work list. */
static uint32_t s_spatial_ref_seen[DEMO_SPATIAL_REF_SEEN_WORDS]
    DEMO_CPU_WORK_CACHE;
/* The generated BSP is expected to be a tree, but the runtime must not turn
 * a malformed/self-referential bake into unbounded recursion.  Z-Treme's
 * fixed-capacity traversal has the same safety property: each node is visited
 * at most once per frame. */
static uint8_t s_spatial_node_seen[SM64_SATURN_BOB_BSP_NODE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint16_t s_render_work_order[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint16_t s_render_work_count;
static uint16_t s_primitive_leaf_id[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
/* Master-only memoization; rank payload remains in its existing shared path. */
static sm64_saturn_vec3i_t s_painter_rank_position;
static uint8_t s_painter_ranks_valid;
#endif
static uint16_t s_primitive_work_weight[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_visible[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_clipped[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_recovery[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_corner_count[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
/* Tier and cluster hysteresis are bulk CPU-only work state. Keep one physical
 * LWRAM owner and make both SH-2s use its P2 cache-through alias; retaining a
 * cached P1 owner beside that alias would make scene reset/select incoherent.
 * The small exact-generation publication record remains in HWRAM `.uncached`.
 */
typedef struct demo_lod_storage {
    uint8_t primitive_tiers[SM64_SATURN_BOB_PRIMITIVE_COUNT];
    sm64_saturn_render_lod_state_t cluster_lod[
        SM64_SATURN_BOB_CLUSTER_COUNT];
} demo_lod_storage_t;
static demo_lod_storage_t s_lod_storage
    __attribute__((section(".lwram_bss")));

static demo_lod_storage_t *demo_lod_storage_cache_through(void)
{
    return (demo_lod_storage_t *)
        sm64_saturn_dual_frame_cache_through(&s_lod_storage);
}

static uint8_t s_primitive_lod_transition[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_lod_suppressed[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint8_t s_primitive_lod_texture_downgraded[
    SM64_SATURN_BOB_PRIMITIVE_COUNT] DEMO_CPU_WORK_CACHE;
/* Compatibility names retained for the compact actor-bank contract: the
 * generalized scene admission path consumes the same selected tier stream
 * that the former Mario-only path exposed as
 * sm64_mario_render_cluster_lod_vertex_offsets /
 * sm64_mario_render_cluster_lod_vertex_list.  `transform_ref_count` remains
 * the bounded span count and s_actor_vertex_owner the lane ownership map. */
static uint8_t s_pretransform_lod_tier;
static sm64_saturn_render_cluster_result_t s_admitted_cluster_results[
    SM64_SATURN_BOB_CLUSTER_COUNT] __attribute__((section(".lwram_bss")));
static uint16_t s_admitted_cluster_count;
static sm64_saturn_lod_lifetime_t s_lod_lifetime DEMO_CROSS_CPU_SHARED;
static sm64_saturn_projected_vertex_t s_clipped_projected[
    SM64_SATURN_BOB_PRIMITIVE_COUNT][5]
    __attribute__((section(".lwram_bss")));
static uint8_t s_primitive_buckets[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static int32_t s_primitive_depth[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static uint16_t s_primitive_slots[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    DEMO_CPU_WORK_CACHE;
static sm64_saturn_terrain_result_t s_terrain_master_results[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
static sm64_saturn_terrain_result_t s_terrain_slave_results[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
static uint8_t s_terrain_master_commands[DEMO_TERRAIN_RESULT_CAPACITY]
    [SM64_SATURN_TERRAIN_COMMAND_BYTES]
    __attribute__((section(".lwram_bss")));
static uint8_t s_terrain_slave_commands[DEMO_TERRAIN_RESULT_CAPACITY]
    [SM64_SATURN_TERRAIN_COMMAND_BYTES]
    __attribute__((section(".lwram_bss")));
/* A5.8 descriptor output metadata is uncached.  The physical terrain arrays
 * remain LWRAM work storage; this metadata determines which lane owns an
 * exact descriptor span before a renderer callback can write it. */
static sm64_saturn_render_job_queue_t s_render_job_queue
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_job_graph_t s_render_job_graph
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_callback_context_bank_t s_render_callback_contexts
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_output_bank_t s_terrain_output_bank
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_output_bank_t s_actor_output_bank
    DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_payload_bank_t s_terrain_record_payload;
static sm64_saturn_render_payload_bank_t s_terrain_command_payload;
/* Queue callbacks may not hand a lower/merge stage an inferred result count.
 * Each entry is a P2-visible, descriptor-keyed release record: READY is
 * written last after the producer's position/result payload and identity.
 * The legacy spans below remain the accepted default until the atomic
 * terrain+Mario cutover. */
typedef struct demo_terrain_queue_metadata {
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint16_t job_index;
    volatile uint16_t record_count;
    volatile uint8_t writer_lane;
    volatile uint8_t claimed_state;
    volatile uint16_t ready;
} demo_terrain_queue_metadata_t;
static demo_terrain_queue_metadata_t s_terrain_admit_metadata[
    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY] DEMO_CROSS_CPU_SHARED;
static demo_terrain_queue_metadata_t s_terrain_result_metadata[
    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY] DEMO_CROSS_CPU_SHARED;
static sm64_saturn_terrain_emit_ref_t s_terrain_emit_refs[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
static sm64_saturn_terrain_emit_ref_t s_terrain_emit_scratch[
    DEMO_TERRAIN_RESULT_CAPACITY] __attribute__((section(".lwram_bss")));
static uint16_t s_terrain_emit_count;
static uint8_t s_terrain_emit_commands_bound;
static uint32_t s_terrain_publish_sequence;
/* The live queue merge keeps one stream for every exact WORLD_LOWER
 * descriptor.  It deliberately does not coerce those streams back into the
 * legacy master/slave arenas: once live, an SH-2 may claim either descriptor.
 * The master still owns the one final depth/order pass and later VDP1 lower. */
typedef struct demo_terrain_queue_merge_spans {
    const sm64_saturn_terrain_result_t *records[
        SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    const uint8_t *commands[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    size_t counts[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t job_indices[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t stream_count;
    uint32_t generation;
    uint32_t sequence;
} demo_terrain_queue_merge_spans_t;
static demo_terrain_queue_merge_spans_t s_terrain_queue_merge_spans
    DEMO_CPU_WORK_CACHE;
static sm64_saturn_render_job_result_identity_t s_terrain_queue_merge_ids[
    DEMO_TERRAIN_RESULT_CAPACITY] DEMO_CPU_WORK_CACHE;
typedef struct demo_actor_vertex_result {
    sm64_saturn_projected_vertex_t projected;
    uint8_t valid;
} demo_actor_vertex_result_t;
typedef struct demo_actor_primitive_ref {
    uint16_t primitive_id;
    uint16_t material_vertex;
} demo_actor_primitive_ref_t;
typedef struct demo_actor_queue_vertex_result {
    demo_actor_vertex_result_t result;
    uint16_t vertex_id;
} demo_actor_queue_vertex_result_t;
typedef struct demo_actor_queue_metadata {
    volatile uint32_t generation;
    volatile uint32_t sequence;
    volatile uint16_t job_index;
    volatile uint16_t record_count;
    volatile uint8_t writer_lane;
    volatile uint8_t claimed_state;
    volatile uint16_t ready;
} demo_actor_queue_metadata_t;
#define DEMO_ACTOR_PRIMITIVE_REJECTED UINT16_MAX
#define DEMO_ACTOR_DRAW_REF_TRANSLUCENT 0x8000U
#define DEMO_ACTOR_DRAW_REF_INDEX_MASK 0x7FFFU
#define DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY \
    (SM64_MARIO_VERTEX_COUNT + SM64_MARIO_PRIMITIVE_COUNT)
_Static_assert(SM64_MARIO_PRIMITIVE_COUNT <=
                   DEMO_ACTOR_DRAW_REF_INDEX_MASK,
               "Mario draw-ref encoding requires a free pass bit");
/* This one contiguous bank is deliberately split into master [0, split) and
 * slave [split, vertex_count) result spans.  It contains no VDP state. */
static demo_actor_vertex_result_t s_actor_results[SM64_MARIO_VERTEX_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
/* Actor tier streams are compact lists of original vertex IDs.  Ownership is
 * therefore by stream slot, not by the numeric vertex ID. */
static uint8_t s_actor_vertex_owner[SM64_MARIO_VERTEX_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static demo_actor_primitive_ref_t s_actor_refs[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint8_t s_actor_ref_owner[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
/* Dormant A5.8 queue storage. Descriptor output offsets are global and
 * disjoint, so both typed payload banks retain the same bounded address
 * space. Only the descriptor-appropriate region is ever read. */
static demo_actor_queue_vertex_result_t s_actor_queue_vertex_master[
    DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY] __attribute__((section(".lwram_bss")));
static demo_actor_queue_vertex_result_t s_actor_queue_vertex_slave[
    DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY] __attribute__((section(".lwram_bss")));
static demo_actor_primitive_ref_t s_actor_queue_ref_master[
    DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY] __attribute__((section(".lwram_bss")));
static demo_actor_primitive_ref_t s_actor_queue_ref_slave[
    DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY] __attribute__((section(".lwram_bss")));
static sm64_saturn_render_payload_bank_t s_actor_vertex_payload;
static sm64_saturn_render_payload_bank_t s_actor_ref_payload;
static demo_actor_queue_metadata_t s_actor_admit_metadata[
    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY] DEMO_CROSS_CPU_SHARED;
static demo_actor_queue_metadata_t s_actor_result_metadata[
    SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY] DEMO_CROSS_CPU_SHARED;
static sm64_saturn_render_job_result_identity_t s_actor_queue_merge_ids[
    SM64_MARIO_PRIMITIVE_COUNT] DEMO_ACTOR_WORK_CACHE;
static uint16_t s_actor_draw_order[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static uint16_t s_actor_transform_refs[SM64_MARIO_VERTEX_COUNT]
    DEMO_TERRAIN_TRANSFORM_CACHE;
static sm64_saturn_actor_draw_ref_t s_actor_opaque_refs[
    SM64_MARIO_PRIMITIVE_COUNT] DEMO_TERRAIN_TRANSFORM_CACHE;
static sm64_saturn_actor_draw_ref_t s_actor_translucent_refs[
    SM64_MARIO_PRIMITIVE_COUNT] DEMO_TERRAIN_TRANSFORM_CACHE;
static uint16_t s_actor_slots[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_ACTOR_WORK_CACHE;
static uint16_t s_actor_texture_slots[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_ACTOR_WORK_CACHE;

static const sm64_saturn_render_job_callback_table_t *
demo_render_job_callbacks(void);
static uint8_t s_render_job_runtime_active;

static uint16_t s_actor_texture_count;
static uint16_t s_actor_command_count;
static sm64_saturn_gouraud_table_t *s_actor_gouraud[
    SM64_MARIO_PRIMITIVE_COUNT] DEMO_ACTOR_WORK_CACHE;
static uintptr_t s_actor_gouraud_addresses[SM64_MARIO_PRIMITIVE_COUNT]
    DEMO_ACTOR_WORK_CACHE;
static uint16_t s_actor_draw_count;

/* The meshlet core already owns Mario's painter-sorted opaque/translucent
 * refs.  Keep a compact index+pass token in the existing draw-order array so
 * final VDP1 lowering retains the bin instead of discarding it. */
static const sm64_saturn_actor_draw_ref_t *
demo_actor_draw_ref_from_order(uint16_t order)
{
    const uint16_t index = order & DEMO_ACTOR_DRAW_REF_INDEX_MASK;
    if (index >= SM64_MARIO_PRIMITIVE_COUNT) return NULL;
    return (order & DEMO_ACTOR_DRAW_REF_TRANSLUCENT) != 0U
        ? &s_actor_translucent_refs[index] : &s_actor_opaque_refs[index];
}
static uint16_t s_actor_transform_ref_count;
#if !SATURN_DEMO_HOT_PROMOTION
/* Hot mode reads the generated immutable bank directly and promotes it into
 * its single HWRAM work-area owner below.  Keeping a second resident copy in
 * hot mode would spend the same 43,776 bytes twice for no semantic benefit.
 * The non-hot build retains the explicit LWRAM resident copy so that the
 * feature remains independently switchable. */
static int32_t s_bob_positions_resident[SM64_SATURN_BOB_POSITION_COUNT][3]
    __attribute__((section(".lwram_bss")));
static sm64_saturn_bob_primitive_t s_bob_primitives_resident[
    SM64_SATURN_BOB_PRIMITIVE_COUNT]
    __attribute__((section(".lwram_bss")));
#endif
#define DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY SM64_SATURN_BOB_PRIMITIVE_COUNT
typedef enum demo_terrain_template_variant {
    DEMO_TERRAIN_TEMPLATE_BASE,
    DEMO_TERRAIN_TEMPLATE_RECOVERY,
    DEMO_TERRAIN_TEMPLATE_TEXTURE_SUPPRESSED,
    DEMO_TERRAIN_TEMPLATE_VARIANT_COUNT
} demo_terrain_template_variant_t;
/* Full templates live in LWRAM because static material state is immutable,
 * read only by the master after join, and must not be disabled by an
 * arbitrary HWRAM byte gate.  The workers never dereference this bank. */
static sm64_saturn_terrain_resolved_command_t s_bob_terrain_resolved_templates[
    DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY][DEMO_TERRAIN_TEMPLATE_VARIANT_COUNT]
    __attribute__((section(".lwram_bss")));
static uint8_t s_bob_terrain_template_valid[
    DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY] DEMO_CPU_WORK_CACHE;
static uint8_t s_bob_terrain_templates_resolved;
static uint8_t s_bob_terrain_templates_enabled;
_Static_assert(sizeof(vdp1_cmdt_t) == SM64_SATURN_VDP1_COMMAND_BYTES,
               "terrain patcher requires the 32-byte VDP1 command format");
_Static_assert(sizeof(sm64_saturn_terrain_resolved_command_t) == 34U,
               "terrain template must retain every VDP1 word and patch mask");
_Static_assert(offsetof(vdp1_cmdt_t, cmd_ctrl) == 0U &&
                   offsetof(vdp1_cmdt_t, cmd_link) == 2U &&
                   offsetof(vdp1_cmdt_t, cmd_vertices) == 12U &&
                   offsetof(vdp1_cmdt_t, cmd_grda) == 28U,
               "terrain patch offsets must match vdp1_cmdt_t");
_Static_assert(sizeof(int16_vec2_t) == 2U * sizeof(int16_t) &&
                   offsetof(int16_vec2_t, x) == 0U &&
                   offsetof(int16_vec2_t, y) == sizeof(int16_t),
               "terrain patch vertices require packed x/y pairs");
/* demo_render_finalize() hands the terrain bin table straight to the painter
 * relink, whose counting sort carries one chain head per bin on the stack. */
_Static_assert(SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT <=
                   SM64_SATURN_VDP1_BACKEND_MAX_DEPTH_BINS,
               "painter relink cannot order more bins than it has heads");

static bool demo_terrain_template_valid(uint16_t primitive_index,
                                        demo_terrain_template_variant_t variant)
{
    return primitive_index < DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY &&
        variant < DEMO_TERRAIN_TEMPLATE_VARIANT_COUNT &&
        (s_bob_terrain_template_valid[primitive_index] &
         (uint8_t)(1U << variant)) != 0U;
}

static void demo_terrain_template_valid_set(uint16_t primitive_index,
                                            demo_terrain_template_variant_t variant,
                                            bool valid)
{
    if (primitive_index >= DEMO_TERRAIN_TEMPLATE_CACHE_CAPACITY ||
        variant >= DEMO_TERRAIN_TEMPLATE_VARIANT_COUNT)
        return;
    const uint8_t mask = (uint8_t)(1U << variant);
    uint8_t *const bits = &s_bob_terrain_template_valid[primitive_index];
    if (valid)
        *bits |= mask;
    else
        *bits &= (uint8_t)~mask;
}

static demo_terrain_template_variant_t demo_terrain_template_variant_for(
    bool recovery, bool texture_suppressed)
{
    return recovery ? DEMO_TERRAIN_TEMPLATE_RECOVERY :
        (texture_suppressed ? DEMO_TERRAIN_TEMPLATE_TEXTURE_SUPPRESSED :
         DEMO_TERRAIN_TEMPLATE_BASE);
}

static const sm64_saturn_terrain_resolved_command_t *
demo_terrain_resolved_template(uint16_t primitive_index, bool recovery,
                               bool texture_suppressed)
{
    const demo_terrain_template_variant_t variant =
        demo_terrain_template_variant_for(recovery, texture_suppressed);
    return s_bob_terrain_templates_enabled != 0U &&
           s_bob_terrain_templates_resolved != 0U &&
           demo_terrain_template_valid(primitive_index, variant)
        ? &s_bob_terrain_resolved_templates[primitive_index][variant] : NULL;
}
#if SATURN_DEMO_HOT_PROMOTION
/* Optional Z-Treme-style hot arena. The generated bank is immutable source
 * data; this one enclosing work-area owner is populated once before the
 * frame loop and then becomes the renderer's active read-only bank.
 * Placement (Sprint 2 T2.2): back in 32-bit HWRAM .bss -- it is the
 * renderer's hottest per-frame read bank.  The stage-1b LWRAM eviction
 * (49370e31) was link-pressure relief, not a design choice; T2.2's
 * peak-gated capacity shrinks fund the return (see the
 * DEMO_CPU_WORK_CACHE placement note above and T1's attribution).
 * One enclosing object is deliberate: Z-Treme's workarea.c pattern uses
 * compile-time offsets rather than two cursors that can collide at runtime. */
typedef struct demo_hot_workarea {
    int32_t positions[SM64_SATURN_BOB_POSITION_COUNT][3];
    sm64_saturn_bob_primitive_t primitives[SM64_SATURN_BOB_PRIMITIVE_COUNT];
} demo_hot_workarea_t;
static demo_hot_workarea_t s_bob_hot_workarea __attribute__((aligned(16)));
_Static_assert(offsetof(demo_hot_workarea_t, positions) == 0U,
               "hot positions must be the first fixed work-area region");
_Static_assert(offsetof(demo_hot_workarea_t, primitives) >=
                   sizeof(s_bob_hot_workarea.positions),
               "hot work-area regions must not overlap");
_Static_assert(sizeof(s_bob_hot_workarea) <= 65536U,
               "hot immutable BOB work area exceeds its fixed budget");
static saturn_hot_promotion_t s_bob_hot_promotion;
#endif
static const int32_t (*s_bob_positions_active)[3];
static const sm64_saturn_bob_primitive_t *s_bob_primitives_active;
static uint8_t s_bob_resident_ready;
static uint16_t s_slave_begin = SM64_SATURN_BOB_PRIMITIVE_COUNT / 2U;
static uint16_t s_last_master_wait_ticks;
static uint32_t s_transform_publish_sequence;
static uint32_t s_actor_publish_sequence;
static uint32_t s_actor_ref_publish_sequence;
static uint16_t s_actor_slave_begin;
static uint16_t s_actor_primitive_slave_begin;

typedef struct demo_render_transaction {
    sm64_saturn_render_lifecycle_t lifecycle;
    sm64_saturn_vdp1_backend_t *backend;
    sm64_saturn_gouraud_bank_t *gouraud_bank;
    sm64_saturn_fast3d_profile_t *profile;
    const sm64_saturn_mario_actor_snapshot_t *snapshot;
    const sm64_saturn_mario_actor_pose_t *pose;
    const sm64_saturn_render_snapshot_t *scene_snapshot;
    sm64_saturn_actor_runtime_storage_t *actor_runtime;
    sm64_saturn_actor_runtime_handoff_t actor_handoff;
    sm64_saturn_ir_transform_job_t actor_job;
    vdp1_vram_partitions_t partitions;
    uint32_t transform_generation;
    uint16_t required_positions;
    uint16_t actor_vertex_count;
    uint16_t generic_actor_output_count;
    /* S1.11: retained across the publish/join split so the join can walk the
     * same descriptor range the publish admitted, without re-deriving it. */
    uint16_t generic_actor_descriptor_count;
    uint16_t generic_actor_gouraud_count;
    uint16_t generic_actor_gouraud_first;
    /* S1.12 step 1: one past the actor phase's last reserved Gouraud table.
     * Snapshotted at reservation time so emit never reads the live bank
     * cursor. */
    uint16_t generic_actor_gouraud_limit;
    uint16_t frame_job_count;
    uint8_t generic_actor_prepared;
} demo_render_transaction_t;

static demo_render_transaction_t s_demo_render_transaction;

/* Ownership is master-produced immutable frame metadata.  The slave never
 * treats a potentially stale cached copy as permission to read a peer output
 * through its cached alias. */
static inline uint8_t demo_position_owner_read(uint8_t lane,
                                                uint16_t position)
{
    return *((const uint8_t *)sm64_saturn_dual_frame_read_range(
        lane, 0U, s_position_owner) + position);
}

static inline const sm64_saturn_vec3i_t *demo_view_read(uint8_t lane,
                                                          uint16_t position)
{
    return (const sm64_saturn_vec3i_t *)
        sm64_saturn_dual_frame_read_range(
            lane, demo_position_owner_read(lane, position), s_view) + position;
}

static inline const sm64_saturn_projected_vertex_t *demo_projected_read(
    uint8_t lane, uint16_t position)
{
    return (const sm64_saturn_projected_vertex_t *)
        sm64_saturn_dual_frame_read_range(
            lane, demo_position_owner_read(lane, position), s_projected) +
        position;
}

static inline const uint8_t *demo_position_valid_read(uint8_t lane,
                                                        uint16_t position)
{
    return (const uint8_t *)sm64_saturn_dual_frame_read_range(
        lane, demo_position_owner_read(lane, position), s_position_valid) +
        position;
}

static inline const demo_actor_vertex_result_t *demo_actor_result_read_lane_split(
    uint8_t lane, uint16_t slave_begin, uint16_t vertex)
{
    (void)slave_begin;
    const uint8_t owner = vertex < SM64_MARIO_VERTEX_COUNT &&
        s_actor_vertex_owner[vertex] <= 1U ? s_actor_vertex_owner[vertex] : 0U;
    return (const demo_actor_vertex_result_t *)
        sm64_saturn_dual_frame_read_range(lane, owner, s_actor_results) +
        vertex;
}

static inline const demo_actor_vertex_result_t *demo_actor_result_read_lane(
    uint8_t lane, uint16_t vertex)
{
    return demo_actor_result_read_lane_split(lane, s_actor_slave_begin,
                                             vertex);
}

static inline const demo_actor_vertex_result_t *demo_actor_result_read(
    uint16_t vertex)
{
    return demo_actor_result_read_lane(0U, vertex);
}

static inline const demo_actor_primitive_ref_t *demo_actor_ref_read(
    uint16_t primitive)
{
    const uint8_t owner = primitive < SM64_MARIO_PRIMITIVE_COUNT &&
        s_actor_ref_owner[primitive] <= 1U ? s_actor_ref_owner[primitive] : 0U;
    return (const demo_actor_primitive_ref_t *)
        sm64_saturn_dual_frame_read_range(0U, owner, s_actor_refs) +
        primitive;
}

static inline const sm64_saturn_projected_vertex_t *demo_actor_projected_read(
    uint16_t vertex)
{
    return &demo_actor_result_read(vertex)->projected;
}

static inline bool demo_actor_valid_read(uint16_t vertex)
{
    return demo_actor_result_read(vertex)->valid != 0U;
}

#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
static int32_t demo_q16_from_world(int32_t value);
static void demo_spatial_append_node_span(
    uint16_t node, sm64_saturn_fast3d_profile_t *profile)
{
    if (node >= SM64_SATURN_BOB_NODE_SPAN_COUNT) return;
    const uint16_t first = sm64_saturn_bob_node_first_ref[node];
    const uint16_t count = sm64_saturn_bob_node_ref_count[node];
    if (first > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT ||
        count > SM64_SATURN_BOB_PRIMITIVE_REF_COUNT - first) {
        profile->pipeline_faults++;
        return;
    }
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t primitive = sm64_saturn_bob_primitive_refs[first + offset];
        if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT) {
            profile->pipeline_faults++;
            continue;
        }
        const uint16_t word = primitive >> 5;
        const uint32_t bit = UINT32_C(1) << (primitive & 31U);
        if ((s_spatial_ref_seen[word] & bit) != 0U) continue;
        s_spatial_ref_seen[word] |= bit;
        if (s_render_work_count < SM64_SATURN_BOB_PRIMITIVE_COUNT) {
            s_render_work_order[s_render_work_count++] = primitive;
            profile->demo_bob_primitives_spatial_admitted++;
        } else {
            /* Match the predecessor: once capacity is exhausted, later
             * duplicate references cannot displace an earlier survivor. */
            profile->demo_bob_primitives_spatial_dropped++;
        }
    }
}

static sm64_saturn_ztreme_frustum_result_t demo_spatial_admit_node(
    int16_t node, sm64_saturn_ztreme_frustum_result_t inherited,
    const sm64_saturn_ztreme_frustum_t *frustum,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (node < 0 || node >= (int16_t)SM64_SATURN_BOB_BSP_NODE_COUNT)
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    if (s_spatial_node_seen[node] != 0U)
        return SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE;
    s_spatial_node_seen[node] = 1U;
    sm64_saturn_ztreme_frustum_result_t state = inherited;
    if (state != SM64_SATURN_ZTREME_FRUSTUM_INSIDE) {
        const sm64_saturn_ztreme_frustum_result_t tested =
            sm64_saturn_ztreme_frustum_aabb(
                frustum,
                sm64_saturn_bob_bsp_bounds_min[node],
                sm64_saturn_bob_bsp_bounds_max[node]);
        state = tested;
    }
    profile->demo_bob_nodes_visited++;
    if (state == SM64_SATURN_ZTREME_FRUSTUM_OUTSIDE) {
        profile->demo_bob_nodes_outside++;
        return state;
    }
    if (state == SM64_SATURN_ZTREME_FRUSTUM_INSIDE)
        profile->demo_bob_nodes_inside++;
    else
        profile->demo_bob_nodes_intersecting++;

    const int64_t side =
        (int64_t)sm64_saturn_bob_bsp_planes[node][0] * frustum->position[0] +
        (int64_t)sm64_saturn_bob_bsp_planes[node][1] * frustum->position[1] +
        (int64_t)sm64_saturn_bob_bsp_planes[node][2] * frustum->position[2] +
        sm64_saturn_bob_bsp_distances[node];
    const bool camera_front = side >= 0;
    const int16_t plane_near =
        sm64_saturn_bob_bsp_children[node][camera_front ? 0 : 1];
    const int16_t plane_far =
        sm64_saturn_bob_bsp_children[node][camera_front ? 1 : 0];
    const int32_t center_x =
        (sm64_saturn_bob_bsp_bounds_min[node][0] +
         sm64_saturn_bob_bsp_bounds_max[node][0]) / 2;
    const int32_t center_y =
        (sm64_saturn_bob_bsp_bounds_min[node][1] +
         sm64_saturn_bob_bsp_bounds_max[node][1]) / 2;
    const int32_t center_z =
        (sm64_saturn_bob_bsp_bounds_min[node][2] +
         sm64_saturn_bob_bsp_bounds_max[node][2]) / 2;
    const uint8_t octant =
        (frustum->position[0] >= center_x ? 1U : 0U) |
        (frustum->position[1] >= center_y ? 2U : 0U) |
        (frustum->position[2] >= center_z ? 4U : 0U);
    int16_t ordered_near = plane_near;
    int16_t ordered_far = plane_far;
    const int16_t baked_first =
        sm64_saturn_bob_bsp_octant_child_order[node][octant][0];
    const int16_t baked_second =
        sm64_saturn_bob_bsp_octant_child_order[node][octant][1];
    /* The baked order is advisory; the exact runtime plane sign remains the
     * authority. Normalize the baked pair to that near/far truth. */
    if ((baked_first == plane_near || baked_first == plane_far) &&
        (baked_second == plane_near || baked_second == plane_far) &&
        baked_first != baked_second) {
        if (baked_first == plane_near) {
            ordered_near = baked_first;
            ordered_far = baked_second;
        } else {
            ordered_near = baked_second;
            ordered_far = baked_first;
        }
    }
    demo_spatial_admit_node(ordered_near, state, frustum, profile);

    /* Local refs are admitted after the near child and before the far child,
     * preserving predecessor source order while avoiding a post-traversal
     * all-primitive admission scan. */
    demo_spatial_append_node_span((uint16_t)node, profile);

    demo_spatial_admit_node(ordered_far, state, frustum, profile);
    return state;
}

/* S1.13 step 1 -- publish admission's outputs through an uncached staging
 * block, so that moving this span to the idle slave later is a scheduling
 * change rather than a data-ownership change.
 *
 * No concurrency is introduced here: the master still runs admission and folds
 * its own staging block immediately afterwards, so frame identity must be
 * unchanged. What this removes are the two things that would otherwise make
 * the slave version wrong, both named in prenotify-breakdown-post-s1_11.md:
 * `s_render_work_order` is cached and master-owned, so a slave writing it
 * would leave dirty lines the master never sees; and the profile counters are
 * the shared-accumulator hazard S1.11 already solved once by folding an
 * uncached per-phase block on the master.
 *
 * The scratch moves for the same reason. Admission borrows a terrain command
 * bank, and the MASTER bank is later filled with the master's own VDP1
 * commands -- a slave scribbling there would write back over them. The slave
 * bank is safe because one CPU owns it and its own reuse is sequential. */
#define DEMO_SPATIAL_ADMIT_CACHE_LINE_BYTES 16U

typedef struct demo_spatial_admit_accum {
    uint32_t primitives_spatial_admitted;
    uint32_t primitives_spatial_dropped;
    uint32_t nodes_visited;
    uint32_t nodes_outside;
    uint32_t nodes_inside;
    uint16_t cluster_count;
    uint8_t generic_path_ok;
    uint8_t reserved;
} demo_spatial_admit_accum_t;

static demo_spatial_admit_accum_t s_spatial_admit_accum
    __attribute__((aligned(DEMO_SPATIAL_ADMIT_CACHE_LINE_BYTES)));

static uint16_t s_spatial_admit_staging[SM64_SATURN_BOB_PRIMITIVE_COUNT]
    __attribute__((section(".lwram_bss")))
    __attribute__((aligned(DEMO_SPATIAL_ADMIT_CACHE_LINE_BYTES)));

static demo_spatial_admit_accum_t *demo_spatial_admit_accum_uncached(void)
{
#if defined(__sh__)
    return (demo_spatial_admit_accum_t *)(CPU_CACHE_THROUGH |
                                          (uintptr_t)&s_spatial_admit_accum);
#else
    return &s_spatial_admit_accum;
#endif
}

static uint16_t *demo_spatial_admit_staging_uncached(void)
{
#if defined(__sh__)
    return (uint16_t *)(CPU_CACHE_THROUGH | (uintptr_t)s_spatial_admit_staging);
#else
    return s_spatial_admit_staging;
#endif
}

static void demo_spatial_admit(
    const sm64_saturn_camera_transform_t *camera,
    sm64_saturn_fast3d_profile_t *profile, uint32_t generation)
{
    demo_spatial_admit_accum_t *const accum = demo_spatial_admit_accum_uncached();
    accum->primitives_spatial_admitted = 0U;
    accum->primitives_spatial_dropped = 0U;
    accum->nodes_visited = 0U;
    accum->nodes_outside = 0U;
    accum->nodes_inside = 0U;
    accum->cluster_count = 0U;
    accum->generic_path_ok = 0U;
    /* The generic package view is the production admission owner. BOB's
     * legacy recursive painter remains below as a fail-closed compatibility
     * fallback for a malformed generated header, but normal frames never
     * enter that scene-specific path. */
#if SM64_SATURN_ADMIT_DIAG
    const uint16_t admit_view_cursor = sm64_saturn_prenotify_profile_frt();
#endif
    sm64_saturn_render_view_t render_view = {0};
    sm64_saturn_scene_admission_output_t admission_output;
    sm64_saturn_scene_admission_stats_t admission_stats;
    uint16_t portal_indices[1];
    sm64_saturn_scene_admission_view_t scene = {0};
    render_view.camera_position_q16[0] = demo_q16_from_world(camera->position.x);
    render_view.camera_position_q16[1] = demo_q16_from_world(camera->position.y);
    render_view.camera_position_q16[2] = demo_q16_from_world(camera->position.z);
    render_view.view_forward_q16[0] = camera->forward.x;
    render_view.view_forward_q16[1] = camera->forward.y;
    render_view.view_forward_q16[2] = camera->forward.z;
    render_view.view_projection_q16[0][0] = camera->right.x;
    render_view.view_projection_q16[0][1] = camera->right.y;
    render_view.view_projection_q16[0][2] = camera->right.z;
    render_view.view_projection_q16[1][0] = camera->up.x;
    render_view.view_projection_q16[1][1] = camera->up.y;
    render_view.view_projection_q16[1][2] = camera->up.z;
    render_view.generation = generation;
    scene.metadata_version = SM64_SATURN_SCENE_ADMISSION_VERSION;
    scene.metadata_valid = 1U;
    /* Every array bound below is static const in .cart_rodata, which the
     * sourceboot link places in the A-bus cartridge window -- read-only
     * hardware, not merely const-qualified -- so the binding satisfies the
     * immutability contract and admission validates it once instead of once
     * per frame (T2.10 item 2, T2.9 Finding C). */
    scene.metadata_immutable = 1U;
    scene.clusters = sm64_saturn_bob_render_clusters;
    scene.cluster_count = SM64_SATURN_BOB_CLUSTER_COUNT;
    scene.nodes = sm64_saturn_bob_scene_admission_nodes;
    scene.node_count = SM64_SATURN_BOB_ADMISSION_NODE_COUNT;
    scene.cluster_refs = sm64_saturn_bob_scene_admission_cluster_refs;
    scene.cluster_ref_count = SM64_SATURN_BOB_ADMISSION_CLUSTER_REF_COUNT;
    scene.portal_ref_count = 0U;
    scene.root_node = 0U;
    scene.frustum.near_depth = SATURN_DEMO_NEAR_DEPTH;
    scene.frustum.far_depth = DEMO_FAR_DEPTH;
    scene.frustum.half_width = DEMO_CENTER_X;
    scene.frustum.half_height = DEMO_CENTER_Y;
    scene.frustum.focal_length = DEMO_FOCAL_LENGTH;
    admission_output = (sm64_saturn_scene_admission_output_t){
        .cluster_indices = demo_spatial_admit_staging_uncached(),
        .cluster_capacity = SM64_SATURN_BOB_PRIMITIVE_COUNT,
        .portal_indices = portal_indices,
        .portal_capacity = 0U};
    _Static_assert(sizeof(s_terrain_slave_commands) >=
                       sizeof(sm64_saturn_scene_admission_scratch_t),
                   "scene admission phase scratch must fit the reusable terrain bank");
    /* Slave bank, not master: the master bank is later filled with the
     * master's own VDP1 commands, so admission scribbling there from the
     * other CPU would write back over them. */
    sm64_saturn_scene_admission_scratch_t *const admission_scratch =
        (sm64_saturn_scene_admission_scratch_t *)(void *)
            &s_terrain_slave_commands[0][0];
#if SM64_SATURN_ADMIT_DIAG
    {
        uint32_t admit_setup_ticks = 0U;
        uint32_t admit_setup_max = 0U;
        (void)sm64_saturn_prenotify_profile_span(
            admit_view_cursor, &admit_setup_ticks, &admit_setup_max);
        sm64_saturn_prenotify_profile_publish_admit_view(admit_setup_ticks, 0U);
    }
#endif
    if (sm64_saturn_scene_admit_with_scratch(
            &scene, &render_view, &admission_output, &admission_stats,
            admission_scratch)) {
        accum->cluster_count = admission_output.cluster_count;
        accum->primitives_spatial_admitted = admission_stats.clusters_admitted;
        accum->primitives_spatial_dropped = admission_stats.output_exhausted;
        accum->nodes_visited = admission_stats.nodes_tested;
        accum->nodes_outside = admission_stats.nodes_tested -
            admission_stats.nodes_admitted;
        accum->nodes_inside = admission_stats.nodes_admitted;
        accum->generic_path_ok = 1U;
        return;
    }
#if SM64_SATURN_ADMIT_DIAG
    /* Reached only when the generic admission path declines.  T2.7 records
     * that normal frames never enter BOB's legacy recursive painter; this
     * counter is the on-target proof of that claim. */
    sm64_saturn_prenotify_profile_publish_admit_view(0U, 1U);
#endif
    memset(s_spatial_ref_seen, 0, sizeof(s_spatial_ref_seen));
    memset(s_spatial_node_seen, 0, sizeof(s_spatial_node_seen));
    s_render_work_count = 0U;
    const sm64_saturn_ztreme_frustum_t frustum = {
        .position = {camera->position.x, camera->position.y, camera->position.z},
        .right = {camera->right.x, camera->right.y, camera->right.z},
        .up = {camera->up.x, camera->up.y, camera->up.z},
        .forward = {camera->forward.x, camera->forward.y, camera->forward.z},
        .near_depth = SATURN_DEMO_NEAR_DEPTH,
        .far_depth = DEMO_FAR_DEPTH,
        .half_width = DEMO_CENTER_X,
        .half_height = DEMO_CENTER_Y,
        .focal_length = DEMO_FOCAL_LENGTH};
    (void)demo_spatial_admit_node(
        0, SM64_SATURN_ZTREME_FRUSTUM_INTERSECTS, &frustum, profile);
}
#endif

/* Master half of the S1.13 handoff. Copies admission's staged cluster list
 * into the cached array the later hot loops read, and folds the staged
 * counters into the frame profile. Totals are identical to the previous direct
 * accumulation; the legacy recursive fallback writes both directly and sets
 * generic_path_ok = 0, so this leaves its results alone. */
static void demo_spatial_admit_join(sm64_saturn_fast3d_profile_t *profile)
{
    const demo_spatial_admit_accum_t *const accum =
        demo_spatial_admit_accum_uncached();
    if (accum->generic_path_ok == 0U) return;
    const uint16_t *const staged = demo_spatial_admit_staging_uncached();
    uint16_t count = accum->cluster_count;
    if (count > SM64_SATURN_BOB_PRIMITIVE_COUNT)
        count = SM64_SATURN_BOB_PRIMITIVE_COUNT;
    for (uint16_t i = 0U; i < count; i++)
        s_render_work_order[i] = staged[i];
    s_render_work_count = count;
    profile->demo_bob_primitives_spatial_admitted +=
        accum->primitives_spatial_admitted;
    profile->demo_bob_primitives_spatial_dropped +=
        accum->primitives_spatial_dropped;
    profile->demo_bob_nodes_visited += accum->nodes_visited;
    profile->demo_bob_nodes_outside += accum->nodes_outside;
    profile->demo_bob_nodes_inside += accum->nodes_inside;
}

static int32_t demo_q16_from_world(int32_t value)
{
    if (value > INT32_MAX / 65536) return INT32_MAX;
    if (value < INT32_MIN / 65536) return INT32_MIN;
    return value * 65536;
}

#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
static void demo_painter_rank_node(int16_t node,
    const sm64_saturn_vec3i_t *position, uint16_t *rank)
{
    if (node < 0 || node >= SM64_SATURN_BOB_BSP_NODE_COUNT ||
        s_spatial_node_seen[node] != 0U) return;
    s_spatial_node_seen[node] = 1U;
    const int64_t side =
        (int64_t)sm64_saturn_bob_bsp_planes[node][0] * position->x +
        (int64_t)sm64_saturn_bob_bsp_planes[node][1] * position->y +
        (int64_t)sm64_saturn_bob_bsp_planes[node][2] * position->z +
        sm64_saturn_bob_bsp_distances[node];
    const uint8_t near_child = side >= 0 ? 0U : 1U;
    demo_painter_rank_node(sm64_saturn_bob_bsp_children[node][1U - near_child],
                          position, rank);
    const uint16_t first = sm64_saturn_bob_node_first_ref[node];
    const uint16_t count = sm64_saturn_bob_node_ref_count[node];
    for (uint16_t offset = 0U; offset < count; offset++) {
        const uint16_t primitive = sm64_saturn_bob_primitive_refs[first + offset];
        if (primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT &&
            s_primitive_leaf_id[primitive] == UINT16_MAX)
            s_primitive_leaf_id[primitive] = (*rank)++;
    }
    demo_painter_rank_node(sm64_saturn_bob_bsp_children[node][near_child],
                          position, rank);
}

static void demo_update_painter_ranks(const sm64_saturn_vec3i_t *position)
{
    /* Far edges can share a depth bin even when a bridge is wholly in front
     * of its underlying ground. Static leaf/source IDs cannot order that
     * overlap. Preserve camera-side BSP order within each bin, including
     * coplanar references owned by internal nodes, not just terminal leaves. */
    if (s_painter_ranks_valid &&
        position->x == s_painter_rank_position.x &&
        position->y == s_painter_rank_position.y &&
        position->z == s_painter_rank_position.z) return;
    memset(s_primitive_leaf_id, 0xFF, sizeof(s_primitive_leaf_id));
    memset(s_spatial_node_seen, 0, sizeof(s_spatial_node_seen));
    uint16_t rank = 0U;
    demo_painter_rank_node(0, position, &rank);
    s_painter_rank_position = *position;
    s_painter_ranks_valid = 1U;
}
#endif

static void demo_prepare_render_work_order(
    const sm64_saturn_camera_transform_t *camera,
    sm64_saturn_fast3d_profile_t *profile, uint32_t transform_generation)
{
    demo_lod_storage_t *const lod_storage =
        demo_lod_storage_cache_through();
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    /* Admission has already appended the bounded work list. */
    demo_update_painter_ranks(&camera->position);
#else
    s_render_work_count = 0U;
    for (uint16_t i = 0U; i < SM64_SATURN_BOB_PRIMITIVE_COUNT; i++)
        s_render_work_order[s_render_work_count++] = i;
#endif
    const sm64_saturn_render_view_t view = {
        .camera_position_q16 = {
            demo_q16_from_world(camera->position.x),
            demo_q16_from_world(camera->position.y),
            demo_q16_from_world(camera->position.z)},
        .view_forward_q16 = {
            camera->forward.x, camera->forward.y, camera->forward.z},
        .generation = transform_generation,
    };
    const uint16_t candidate_count = s_render_work_count;
    s_render_work_count = 0U;
    s_admitted_cluster_count = 0U;
    for (uint16_t work = 0U; work < candidate_count; work++) {
        const uint16_t primitive = s_render_work_order[work];
        if (primitive >= SM64_SATURN_BOB_CLUSTER_COUNT) {
            profile->pipeline_faults++;
            continue;
        }
        sm64_saturn_render_lod_state_t *const lod =
            &lod_storage->cluster_lod[primitive];
        if (lod->thresholds.mid_enter_depth == 0 &&
            lod->thresholds.far_enter_depth == 0)
            lod->thresholds = saturn_lod_default_thresholds();
        sm64_saturn_render_cluster_result_t result;
        profile->demo_render_clusters_tested++;
        if (!sm64_saturn_render_cluster_admit(
                &sm64_saturn_bob_render_clusters[primitive], &view, lod,
                &result))
            continue;
        if (result.primitive_first != primitive || result.primitive_count != 1U ||
            result.position_ref_first > SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT ||
            result.position_ref_count >
                SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT -
                    result.position_ref_first) {
            profile->pipeline_faults++;
            continue;
        }
        s_render_work_order[s_render_work_count++] = primitive;
        s_admitted_cluster_results[s_admitted_cluster_count++] = result;
        profile->demo_render_clusters_admitted++;
    }
}

/* The master finishes this complete bitset before it publishes either SH-2
 * transform job. The workers only read it while assigning and transforming
 * their disjoint position ranges. */
static uint16_t demo_build_visible_position_set(
    sm64_saturn_fast3d_profile_t *profile, uint32_t transform_generation)
{
    sm64_saturn_visible_position_set_reset(
        &s_visible_position_set, s_visible_position_words,
        DEMO_VISIBLE_POSITION_WORDS, SM64_SATURN_BOB_POSITION_COUNT);
    for (uint16_t admitted = 0U; admitted < s_admitted_cluster_count;
         admitted++) {
        const sm64_saturn_render_cluster_result_t *const result =
            &s_admitted_cluster_results[admitted];
        if (result->generation != transform_generation ||
            result->position_ref_first >
                SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT ||
            result->position_ref_count >
                SM64_SATURN_BOB_CLUSTER_POSITION_REF_COUNT -
                    result->position_ref_first ||
            !sm64_saturn_visible_position_set_mark_refs(
                &s_visible_position_set,
                &sm64_saturn_bob_cluster_position_refs[
                    result->position_ref_first], result->position_ref_count)) {
            profile->pipeline_faults++;
            return 0U;
        }
    }
    const uint16_t required_positions =
        sm64_saturn_visible_position_set_count(&s_visible_position_set);
    assert(required_positions <= SM64_SATURN_BOB_POSITION_COUNT);
    profile->demo_positions_admitted += required_positions;
    return required_positions;
}

static uint16_t demo_choose_work_split(void)
{
    const uint16_t count = s_render_work_count;
    if (count < 2U) return count;
    uint32_t total = 0U;
    for (uint16_t work = 0U; work < count; work++) {
        const uint16_t primitive = s_render_work_order[work];
        total += s_primitive_work_weight[primitive];
    }
    const uint32_t target = total / 2U;
    uint32_t accumulated = 0U;
    uint16_t split = count / 2U;
    for (uint16_t work = 0U; work < count; work++) {
        accumulated += s_primitive_work_weight[s_render_work_order[work]];
        if (accumulated >= target) {
            split = (uint16_t)(work + 1U);
            break;
        }
    }
    const uint16_t minimum = count < 64U ? 1U : 32U;
    if (split < minimum) split = minimum;
    if (split > count - minimum) split = (uint16_t)(count - minimum);
    /* SlaveDriver's prior-spin correction is bounded to a few work units per
     * frame. A high master wait means the slave needs less work; a low wait
     * lets it claim a little more on the next frame. */
    if (s_last_master_wait_ticks > 100U &&
        split + 8U < (uint16_t)(count - minimum))
        split = (uint16_t)(split + 8U);
    else if (s_last_master_wait_ticks < 20U && split > minimum + 8U)
        split = (uint16_t)(split - 8U);
    return split;
}

static void demo_prepare_position_owners(uint16_t split, bool dual_phase)
{
    memset(s_position_use_mask, 0, sizeof(s_position_use_mask));
    memset(s_position_owner, UINT8_MAX, sizeof(s_position_owner));

    for (uint16_t work = 0U; work < s_render_work_count; work++) {
        const uint16_t primitive_index = s_render_work_order[work];
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[primitive_index];
        const uint8_t lane_mask =
            dual_phase && work >= split ? 2U : 1U;
        /* Results and VDP1 distorted sprites always carry four corners.
         * Generated triangles repeat C in slot 3, so include that index in
         * the shared bank even when the source primitive has three corners. */
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const uint16_t position = primitive->indices[corner];
            if (sm64_saturn_visible_position_set_test(
                    &s_visible_position_set, position))
                s_position_use_mask[position] |= lane_mask;
        }
    }

    uint16_t owned[2] = {0U, 0U};
    for (uint16_t position = 0U;
         position < SM64_SATURN_BOB_POSITION_COUNT; position++) {
        const uint8_t use_mask = s_position_use_mask[position];
        if (use_mask == 0U) continue;
        uint8_t owner;
        if (!dual_phase || use_mask == 1U)
            owner = 0U;
        else if (use_mask == 2U)
            owner = 1U;
        else
            owner = owned[0] <= owned[1] ? 0U : 1U;
        s_position_owner[position] = owner;
        owned[owner]++;
    }
}

static void __attribute__((unused)) demo_build_clipped_quad(
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_projected_vertex_t output[4])
{
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        const uint16_t index = primitive->indices[corner];
        output[corner] = *demo_projected_read(0U, index);
        if (demo_view_read(0U, index)->z > SATURN_DEMO_NEAR_DEPTH) continue;

        const uint8_t next = (uint8_t)((corner + 1U) & 3U);
        const uint8_t previous = (uint8_t)((corner + 3U) & 3U);
        uint8_t front = UINT8_MAX;
        if (demo_view_read(0U, primitive->indices[next])->z >
                SATURN_DEMO_NEAR_DEPTH &&
            demo_view_read(0U, primitive->indices[previous])->z <=
                SATURN_DEMO_NEAR_DEPTH) {
            front = next;
        } else if (
            demo_view_read(0U, primitive->indices[previous])->z >
                SATURN_DEMO_NEAR_DEPTH &&
            demo_view_read(0U, primitive->indices[next])->z <=
                SATURN_DEMO_NEAR_DEPTH) {
            front = previous;
        }
        if (front == UINT8_MAX &&
            demo_view_read(0U, primitive->indices[next])->z >
                SATURN_DEMO_NEAR_DEPTH &&
            demo_view_read(0U, primitive->indices[previous])->z >
                SATURN_DEMO_NEAR_DEPTH) {
            /* An isolated back corner has two valid edge intersections;
             * choose the next edge deterministically. */
            front = next;
        }
        if (front == UINT8_MAX) continue;

        const sm64_saturn_projected_vertex_t edge =
            *demo_projected_read(0U, primitive->indices[front]);
        const int32_t back_z = demo_view_read(0U, index)->z;
        const int32_t front_z =
            demo_view_read(0U, primitive->indices[front])->z;
        const int32_t denominator = front_z - back_z;
        if (denominator <= 0) continue;
        int32_t ratio;
        if (!sm64_saturn_div_s64_s32(
                (int64_t)(front_z - SATURN_DEMO_NEAR_DEPTH) << 16,
                denominator, &ratio)) {
            ratio = INT32_MAX;
        }
        output[corner].x = (int16_t)(edge.x -
            (int32_t)(((int64_t)(edge.x - output[corner].x) * ratio) >> 16));
        output[corner].y = (int16_t)(edge.y -
            (int32_t)(((int64_t)(edge.y - output[corner].y) * ratio) >> 16));
        output[corner].z = SATURN_DEMO_NEAR_DEPTH;
    }
}

static void demo_primitive_screen_vertices(
    const sm64_saturn_bob_primitive_t *primitive,
    int16_vec2_t vertices[4])
{
    const sm64_saturn_projected_vertex_t *projected =
        s_primitive_clipped[primitive - s_bob_primitives_active] != 0U
            ? s_clipped_projected[primitive - s_bob_primitives_active]
            : NULL;
    for (uint8_t corner = 0U; corner < 4U; corner++) {
        const sm64_saturn_projected_vertex_t point = projected != NULL
            ? projected[corner]
            : *demo_projected_read(0U, primitive->indices[corner]);
        vertices[corner].x = point.x;
        vertices[corner].y = point.y;
    }
}

typedef struct demo_mario_transform_context {
    /* Values are copied by the master before dispatch.  The only pointers in
     * this worker context name project-owned immutable/output banks; it has
     * no SM64 live-state, graph, VDP1, texture-residency, or allocator link. */
    sm64_saturn_ir_transform_job_t job;
    sm64_saturn_mario_actor_snapshot_t snapshot;
    int16_t vertices[SM64_MARIO_VERTEX_COUNT][3];
    uint8_t light_intensity[SM64_MARIO_VERTEX_COUNT];
    uint16_t vertex_count;
    uint16_t pose_frame;
    uint16_t pose_frame_count;
    uint8_t pose_walking_bank;
    /* Dynamic compact references are copied inline so a peer never follows a
     * cached master pointer from an otherwise cache-through snapshot. The
     * generated primitive/material banks are addressed by local symbols. */
    uint16_t vertex_refs[SM64_MARIO_VERTEX_COUNT];
    uint16_t vertex_ref_slot[SM64_MARIO_VERTEX_COUNT];
    uint16_t vertex_slave_begin;
    uint16_t transform_ref_count;
    uint16_t primitive_slave_begin;
    uint32_t sequence;
} demo_mario_transform_context_t;
union demo_actor_phase_workspace {
    demo_mario_transform_context_t mario;
    uint8_t source_scene_workspace[
        SM64_SATURN_SOURCE_SCENE_BUNDLE_LIFETIME_BYTES];
};
/* S1.11 substrate: 4-byte alignment was sufficient while the actor drain ran
 * master-only, because only lane 0 was ever live.  The moment the slave takes
 * lane 1 the two lane spans must not share an SH-2 cache line -- the CPUs have
 * no coherency, so each one's write-back would clobber the other's bytes at
 * the boundary.  The bundle's workspace_lane_stride is already a multiple of
 * 16 (1456 = 16*91), so aligning the base to the cache line is all that is
 * needed to make every lane span start on its own line. */
#define DEMO_ACTOR_WORKSPACE_CACHE_LINE_BYTES 16U
static union demo_actor_phase_workspace s_actor_phase_workspace
    DEMO_TERRAIN_TRANSFORM_CACHE
    __attribute__((aligned(DEMO_ACTOR_WORKSPACE_CACHE_LINE_BYTES)));
#define s_mario_transform_context s_actor_phase_workspace.mario
_Static_assert(sizeof(demo_mario_transform_context_t) <= UINT16_MAX,
               "Mario callback context must retain a bounded byte count");
_Static_assert(sizeof(union demo_actor_phase_workspace) >=
                   SM64_SATURN_SOURCE_SCENE_BUNDLE_LIFETIME_BYTES,
               "phase union must cover generic actor workspace");

void *sm64_saturn_demo_render_actor_workspace(uint32_t *byte_count)
{
    if (byte_count != NULL)
        *byte_count = sizeof(s_actor_phase_workspace.source_scene_workspace);
    return s_actor_phase_workspace.source_scene_workspace;
}

typedef struct demo_classify_context {
    const sm64_saturn_bob_primitive_t *primitives;
    const uint16_t *work_order;
    const sm64_saturn_camera_transform_t *camera;
    const sm64_saturn_ir_transform_job_t *job;
    uint32_t visible[2];
    uint32_t transformed[2];
    uint32_t radius_rejected[2];
    uint32_t near_rejected[2];
    uint32_t degenerate[2];
    uint32_t clip_away[2];
    uint32_t clip_to_one[2];
    uint32_t clip_to_two[2];
    uint32_t clip_recovery[2];
    uint32_t clip_overflow[2];
    uint16_t work_count;
    uint16_t required_positions;
    uint32_t transform_sequence;
    bool dual_phase;
} demo_classify_context_t;

typedef struct demo_emit_stats {
    uint32_t triangles_emitted;
    uint32_t texture_commands;
    uint32_t gouraud_bank_overflow;
} demo_emit_stats_t;

typedef struct demo_emit_context {
    const sm64_saturn_bob_primitive_t *primitives;
    vdp1_cmdt_t *cmdts;
    const vdp1_vram_partitions_t *partitions;
    sm64_saturn_gouraud_table_t *const *gouraud_tables;
    const uintptr_t *gouraud_addresses;
    demo_emit_stats_t stats[2];
} demo_emit_context_t;

static void demo_emit_mario_range(void *opaque, uint16_t begin,
                                  uint16_t end);

/* Z-Treme's hysteretic LOD boundary is applied after transform, when the
 * primitive's view-space depth is known.  All tiers share the same promoted
 * positions; only the generated primitive masks and texture binding policy
 * change. */
static uint16_t demo_primitive_projected_span(
    uint8_t lane, const sm64_saturn_bob_primitive_t *primitive)
{
    int32_t min_x = demo_projected_read(lane, primitive->indices[0])->x;
    int32_t max_x = min_x;
    int32_t min_y = demo_projected_read(lane, primitive->indices[0])->y;
    int32_t max_y = min_y;
    for (uint8_t corner = 1U; corner < 4U; corner++) {
        const sm64_saturn_projected_vertex_t *point =
            demo_projected_read(lane, primitive->indices[corner]);
        if (point->x < min_x) min_x = point->x;
        if (point->x > max_x) max_x = point->x;
        if (point->y < min_y) min_y = point->y;
        if (point->y > max_y) max_y = point->y;
    }
    const int32_t span = (max_x - min_x) > (max_y - min_y)
        ? (max_x - min_x) : (max_y - min_y);
    return span > UINT16_MAX ? UINT16_MAX : (uint16_t)span;
}

/* Tier selection runs before clipping, template resolution, texture lookup or
 * Gouraud allocation.  The controller's depth plus projected-span windows
 * make boundary jitter stable while retaining the renderer's source identity
 * and route safety checks below. */
static uint8_t demo_lod_select(uint32_t generation, uint16_t primitive_index,
                               int32_t depth, uint16_t projected_span)
{
    uint8_t next = SATURN_LOD_NEAR;
    uint8_t transition = 0U;
#if SATURN_DEMO_POLY_TIER != 0
    const saturn_lod_thresholds_t thresholds = saturn_lod_default_thresholds();
    const saturn_lod_thresholds_t *thresholds_ptr = &thresholds;
#else
    const saturn_lod_thresholds_t *thresholds_ptr = NULL;
#endif
    if (!sm64_saturn_lod_lifetime_select(
            &s_lod_lifetime, generation, primitive_index, depth,
            projected_span, thresholds_ptr, &next, &transition))
        return SATURN_LOD_NEAR;
    s_primitive_lod_transition[primitive_index] = transition;
    return next;
}

static bool demo_transform_mario_vertex(
    const demo_mario_transform_context_t *context, uint16_t vertex,
    int32_t sine, int32_t cosine, demo_actor_vertex_result_t *result)
{
    if (context == NULL || result == NULL || vertex >= context->vertex_count)
        return false;
    const int16_t *source = context->vertices[vertex];
    const int32_t sx = (int32_t)source[0] << 16;
    const int32_t sz = (int32_t)source[2] << 16;
    const sm64_saturn_vec3i_t world = {
        (int32_t)context->snapshot.position[0] +
            ((sm64_saturn_q16_mul(sx, cosine) +
              sm64_saturn_q16_mul(sz, sine)) >> 16),
        (int32_t)context->snapshot.position[1] + source[1],
        (int32_t)context->snapshot.position[2] +
            ((-sm64_saturn_q16_mul(sx, sine) +
              sm64_saturn_q16_mul(sz, cosine)) >> 16)};
    sm64_saturn_vec3i_t view;
    result->valid = sm64_saturn_ir_transform_one(
        &context->job, world, &view, &result->projected) ? 1U : 0U;
    return true;
}

/* This begins only after sm64_saturn_terrain_worker_run() has returned.  The
 * copied records let the slave transform/classify a disjoint vertex span with
 * no game-state or VDP1 dependency; the master remains the only actor command
 * allocator and inserter. */
static bool demo_snapshot_mario_transform_context(
    demo_mario_transform_context_t *context,
    const sm64_saturn_ir_transform_job_t *job,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose)
{
    if (context == NULL || job == NULL || snapshot == NULL || pose == NULL ||
        !snapshot->valid || pose->vertices == NULL ||
        pose->vertex_count != SM64_MARIO_VERTEX_COUNT ||
        s_actor_transform_ref_count == 0U)
        return false;
    memset(context, 0, sizeof(*context));
    context->job = *job;
    context->snapshot = *snapshot;
    memcpy(context->vertices, pose->vertices, sizeof(context->vertices));
    if (pose->light_intensity != NULL)
        memcpy(context->light_intensity, pose->light_intensity,
               sizeof(context->light_intensity));
    context->vertex_count = pose->vertex_count;
    context->pose_frame = pose->frame;
    context->pose_frame_count = pose->frame_count;
    context->pose_walking_bank = pose->walking_bank;
    context->transform_ref_count = s_actor_transform_ref_count;
    memcpy(context->vertex_refs, s_actor_transform_refs,
           context->transform_ref_count * sizeof(context->vertex_refs[0]));
    memset(context->vertex_ref_slot, 0xFF, sizeof(context->vertex_ref_slot));
    for (uint16_t ref = 0U; ref < context->transform_ref_count; ref++) {
        const uint16_t vertex = context->vertex_refs[ref];
        if (vertex >= SM64_MARIO_VERTEX_COUNT ||
            context->vertex_ref_slot[vertex] != UINT16_MAX)
            return false;
        context->vertex_ref_slot[vertex] = ref;
    }
    return true;
}

static void demo_resolve_terrain_command_templates(
    const vdp1_vram_partitions_t *partitions);

static void demo_build_primitive_work_metadata(void)
{
    for (uint16_t primitive = 0U;
         primitive < SM64_SATURN_BOB_PRIMITIVE_COUNT; primitive++) {
        s_primitive_leaf_id[primitive] = UINT16_MAX;
        s_primitive_work_weight[primitive] = 1U;
    }
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    for (uint16_t node = 0U; node < SM64_SATURN_BOB_BSP_NODE_COUNT; node++) {
        const int16_t start = sm64_saturn_bob_bsp_leaf_ranges[node][0];
        const int16_t count = sm64_saturn_bob_bsp_leaf_ranges[node][1];
        if (start < 0 || count <= 0) continue;
        const uint16_t baked_weight = sm64_saturn_bob_bsp_work_weight[node];
        const uint16_t per_primitive =
            baked_weight / (uint16_t)count == 0U
                ? 1U : baked_weight / (uint16_t)count;
        for (int16_t offset = 0; offset < count; offset++) {
            const uint16_t ref = (uint16_t)(start + offset);
            const uint16_t primitive = sm64_saturn_bob_bsp_refs[ref];
            if (primitive >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;
            if (s_primitive_leaf_id[primitive] != UINT16_MAX) continue;
            s_primitive_leaf_id[primitive] = node;
            s_primitive_work_weight[primitive] = per_primitive;
        }
    }
#endif
}

SM64_SATURN_CART_COLD
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE && SATURN_FEATURE_ACTOR_PHASE_SLAVE
/* Defined with the rest of the actor phase, far below; registration happens
 * here during init because the runtime refuses a rebind after the first
 * notify. */
static bool demo_actor_phase_slave_task(void);
#endif

SM64_SATURN_CART_COLD
void sm64_saturn_demo_render_init(void)
{
    demo_lod_storage_t *const lod_storage =
        demo_lod_storage_cache_through();
    sm64_saturn_render_job_queue_init(&s_render_job_queue);
    sm64_saturn_render_callback_context_bank_init(
        &s_render_callback_contexts);
    sm64_saturn_render_job_graph_init(&s_render_job_graph,
                                      &s_render_job_queue);
    sm64_saturn_render_output_bank_init(
        &s_terrain_output_bank, SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN);
    sm64_saturn_render_output_bank_init(
        &s_actor_output_bank, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    sm64_saturn_render_payload_bank_init(
        &s_terrain_record_payload, s_terrain_master_results,
        s_terrain_slave_results, sizeof(s_terrain_master_results[0]),
        DEMO_TERRAIN_RESULT_CAPACITY);
    sm64_saturn_render_payload_bank_init(
        &s_terrain_command_payload, s_terrain_master_commands,
        s_terrain_slave_commands, SM64_SATURN_TERRAIN_COMMAND_BYTES,
        DEMO_TERRAIN_RESULT_CAPACITY);
    sm64_saturn_render_payload_bank_init(
        &s_actor_vertex_payload, s_actor_queue_vertex_master,
        s_actor_queue_vertex_slave, sizeof(s_actor_queue_vertex_master[0]),
        DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY);
    sm64_saturn_render_payload_bank_init(
        &s_actor_ref_payload, s_actor_queue_ref_master,
        s_actor_queue_ref_slave, sizeof(s_actor_queue_ref_master[0]),
        DEMO_ACTOR_QUEUE_PAYLOAD_CAPACITY);
    sm64_saturn_lod_lifetime_init(
        &s_lod_lifetime, lod_storage->primitive_tiers,
        sizeof(lod_storage->primitive_tiers), lod_storage->cluster_lod,
        sizeof(lod_storage->cluster_lod));
    memset(s_primitive_lod_transition, 0,
           sizeof(s_primitive_lod_transition));
    memset(s_primitive_lod_suppressed, 0,
           sizeof(s_primitive_lod_suppressed));
    memset(s_primitive_lod_texture_downgraded, 0,
           sizeof(s_primitive_lod_texture_downgraded));
#if SATURN_DEMO_HOT_PROMOTION
    s_bob_positions_active = sm64_saturn_bob_positions;
    s_bob_primitives_active = sm64_saturn_bob_primitives;
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_hot_workarea.positions,
        sizeof(s_bob_hot_workarea.positions));
    const int32_t (*hot_positions)[3] = saturn_hot_promote(
        &s_bob_hot_promotion, sm64_saturn_bob_positions,
        sizeof(s_bob_hot_workarea.positions), 16U);
    /* Reset to the second compile-time region; the enclosing work-area
     * assertions prove that this cannot overlap the position bank. */
    saturn_hot_promotion_init(
        &s_bob_hot_promotion, s_bob_hot_workarea.primitives,
        sizeof(s_bob_hot_workarea.primitives));
    const sm64_saturn_bob_primitive_t *hot_primitives = saturn_hot_promote(
        &s_bob_hot_promotion, sm64_saturn_bob_primitives,
        sizeof(s_bob_hot_workarea.primitives), 16U);
    if (hot_positions != NULL && hot_primitives != NULL) {
        s_bob_positions_active = hot_positions;
        s_bob_primitives_active = hot_primitives;
    }
#else
    memcpy(s_bob_positions_resident, sm64_saturn_bob_positions,
           sizeof(s_bob_positions_resident));
    memcpy(s_bob_primitives_resident, sm64_saturn_bob_primitives,
           sizeof(s_bob_primitives_resident));
    s_bob_positions_active = s_bob_positions_resident;
    s_bob_primitives_active = s_bob_primitives_resident;
#endif
    memset(s_bob_terrain_template_valid, 0,
           sizeof(s_bob_terrain_template_valid));
    s_bob_terrain_templates_enabled = 1U;
    s_bob_terrain_templates_resolved = 0U;
    /* sourceboot establishes VDP1 partitions before this load hook. Resolve
     * static texture, draw-mode, colour, and command words once here; the
     * frame workers publish only dynamic coordinate patches. */
    vdp1_vram_partitions_t partitions;
    vdp1_vram_partitions_get(&partitions);
    demo_resolve_terrain_command_templates(&partitions);
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    s_painter_ranks_valid = 0U;
#endif
    demo_build_primitive_work_metadata();
    s_render_job_runtime_active =
        sm64_saturn_render_job_runtime_activate_graph(
            &s_render_job_graph, demo_render_job_callbacks(), NULL) ? 1U : 0U;
    if (s_render_job_runtime_active == 0U) return;
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE && SATURN_FEATURE_ACTOR_PHASE_SLAVE
    /* S1.11: register before the first notify -- the runtime refuses a rebind
     * once notification has begun, so a late registration fails loudly here
     * rather than racing a live frame. */
    if (!sm64_saturn_render_job_runtime_set_slave_task(
            demo_actor_phase_slave_task)) {
        s_render_job_runtime_active = 0U;
        return;
    }
#endif
    s_bob_resident_ready = 1U;
}

void sm64_saturn_demo_render_scene_observe(bool active, int16_t level,
                                           int16_t area)
{
    (void)sm64_saturn_lod_lifetime_observe_scene(
        &s_lod_lifetime, active, level, area);
}

static sm64_saturn_camera_transform_t demo_camera(
    const sm64_saturn_mario_actor_snapshot_t *snapshot)
{
    const sm64_saturn_vec3i_t position = {
        snapshot->camera_position[0],
        snapshot->camera_position[1],
        snapshot->camera_position[2]
    };
    const sm64_saturn_vec3i_t focus = {
        snapshot->camera_focus[0],
        snapshot->camera_focus[1],
        snapshot->camera_focus[2]
    };
    const sm64_saturn_vec3i_t forward = sm64_saturn_vec3_normalize_q16(
        (sm64_saturn_vec3i_t){focus.x - position.x, focus.y - position.y,
                              focus.z - position.z});
    const sm64_saturn_vec3i_t right = sm64_saturn_vec3_normalize_q16(
        (sm64_saturn_vec3i_t){-forward.z, 0, forward.x});
    const sm64_saturn_vec3i_t up = {
        (int32_t)(-(int64_t)right.z * forward.y >> 16),
        (int32_t)(((int64_t)right.z * forward.x -
                   (int64_t)right.x * forward.z) >> 16),
        (int32_t)((int64_t)right.x * forward.y >> 16)
    };
    return (sm64_saturn_camera_transform_t){position, right, up, forward};
}

static uint16_t demo_bucket(int32_t z)
{
    if (z <= SATURN_DEMO_NEAR_DEPTH) return 0;
    if (z >= DEMO_FAR_DEPTH) return DEMO_BUCKETS - 1U;
    int32_t bucket;
    const int64_t scaled = (int64_t)(z - SATURN_DEMO_NEAR_DEPTH) *
        (DEMO_BUCKETS - 1U);
    if (!sm64_saturn_div_s64_s32(
            scaled, DEMO_FAR_DEPTH - SATURN_DEMO_NEAR_DEPTH, &bucket)) {
        bucket = DEMO_BUCKETS - 1U;
    }
    return (uint16_t)bucket;
}

static bool __attribute__((unused)) demo_primitive_in_radius(
    const sm64_saturn_bob_primitive_t *primitive,
    const sm64_saturn_camera_transform_t *camera)
{
    const uint16_t count = primitive->source1 == 0xFFFFU ? 3U : 4U;
    int32_t minimum[3] = {INT32_MAX, INT32_MAX, INT32_MAX};
    int32_t maximum[3] = {INT32_MIN, INT32_MIN, INT32_MIN};
    for (uint16_t corner = 0U; corner < count; corner++) {
        const int32_t *point = s_bob_positions_active[
            primitive->indices[corner]];
        for (uint8_t axis = 0U; axis < 3U; axis++) {
            if (point[axis] < minimum[axis]) minimum[axis] = point[axis];
            if (point[axis] > maximum[axis]) maximum[axis] = point[axis];
        }
    }
    const int32_t center[3] = {
        (minimum[0] + maximum[0]) / 2,
        (minimum[1] + maximum[1]) / 2,
        (minimum[2] + maximum[2]) / 2
    };
    int64_t bound = 0;
    for (uint16_t corner = 0U; corner < count; corner++) {
        const int32_t *point = s_bob_positions_active[
            primitive->indices[corner]];
        const int64_t dx = (int64_t)point[0] - center[0];
        const int64_t dy = (int64_t)point[1] - center[1];
        const int64_t dz = (int64_t)point[2] - center[2];
        const int64_t extent = (dx < 0 ? -dx : dx) +
                               (dy < 0 ? -dy : dy) +
                               (dz < 0 ? -dz : dz);
        if (extent > bound) bound = extent;
    }
    const int64_t dx = (int64_t)center[0] - camera->position.x;
    const int64_t dy = (int64_t)center[1] - camera->position.y;
    const int64_t dz = (int64_t)center[2] - camera->position.z;
    const int64_t distance = dx * dx + dy * dy + dz * dz;
    const int64_t view = (int64_t)SATURN_DEMO_VIEW_RADIUS;
    const int64_t limit = view + bound;
    return distance <= limit * limit;
}

static bool demo_transform_owned_positions(
    demo_classify_context_t *context, uint8_t lane, bool single_producer)
{
    if (context == NULL || context->job == NULL || lane > 1U ||
        s_transform_phase_failed != 0U)
        return false;
    assert(context->required_positions ==
           sm64_saturn_visible_position_set_count(&s_visible_position_set));

    for (uint16_t position = 0U;
         position < SM64_SATURN_BOB_POSITION_COUNT; position++) {
        if (!sm64_saturn_visible_position_set_test(
                &s_visible_position_set, position) ||
            (!single_producer &&
             demo_position_owner_read(lane, position) != lane))
            continue;
        if ((position % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled()) {
            s_transform_phase_failed = 1U;
            return false;
        }
        const int32_t *world = s_bob_positions_active[position];
        const bool valid = sm64_saturn_ir_transform_one(
            context->job,
            (sm64_saturn_vec3i_t){world[0], world[1], world[2]},
            &s_view[position], &s_projected[position]);
        s_position_valid[position] = valid ? 1U : 0U;
        context->transformed[lane]++;
    }

    /* Bulk writes are complete before this uncached release.  The sequence
     * and count are published before ready, which is deliberately last. */
    sm64_saturn_dual_frame_publish(
        &s_transform_frame_bank, lane, context->transform_sequence,
        (uint16_t)context->transformed[lane]);
    if (!context->dual_phase) return true;

    uint32_t spins = 0U;
    uint16_t peer_count = 0U;
    while (!sm64_saturn_dual_frame_peer_ready(
               &s_transform_frame_bank, lane, context->transform_sequence,
               &peer_count) && s_transform_phase_failed == 0U &&
           spins++ < 4000000U) {
        if ((spins & 0x3FFFU) == 0U &&
            sm64_saturn_dual_worker_cancelled()) {
            s_transform_phase_failed = 1U;
            break;
        }
    }
    if (!sm64_saturn_dual_frame_peer_ready(
            &s_transform_frame_bank, lane, context->transform_sequence,
            &peer_count)) {
        s_transform_phase_failed = 1U;
        return false;
    }
    /* peer_count is intentionally observed here as part of the accepted
     * uncached header; it supplies a bounded, diagnostic cross-check without
     * changing the work split. */
    if (peer_count > context->required_positions) {
        s_transform_phase_failed = 1U;
        return false;
    }
    return true;
}

/* Classification is entered with the actual execution lane.  Queue work may
 * legally begin at zero on the slave, so no range boundary can identify a
 * cache owner here. */
static void demo_classify_exact(demo_classify_context_t *context,
                                uint16_t begin, uint16_t end, uint8_t lane)
{
#if !(SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS)
    const sm64_saturn_camera_transform_t *camera = context->camera;
#endif
    for (uint16_t work = begin; work < end; work++) {
        if (((uint16_t)(work - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const uint16_t i = context->work_order[work];
        const sm64_saturn_bob_primitive_t *primitive =
            &context->primitives[i];
        s_primitive_visible[i] = 0U;
        s_primitive_clipped[i] = 0U;
        s_primitive_recovery[i] = 0U;
        s_primitive_corner_count[i] =
            primitive->source1 == 0xFFFFU ? 3U : 4U;
        bool spatial_rejected = false;
#if !(SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS)
        spatial_rejected = !demo_primitive_in_radius(primitive, camera);
#endif
        if (spatial_rejected) {
            context->radius_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        /* The shared position bank was transformed once before this phase.
         * Faces only validate and index it, as in SlaveDriver's WALLS.C wall
         * path and Z-Treme's full/LOD meshes sharing one pntbl. */
        bool transform_valid = true;
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const uint16_t index = primitive->indices[corner];
            if (index >= SM64_SATURN_BOB_POSITION_COUNT ||
                *demo_position_valid_read(lane, index) == 0U)
                transform_valid = false;
        }
        if (!transform_valid) {
            context->near_rejected[lane]++;
            continue;
        }
        bool any_front = false;
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            if (demo_view_read(lane, primitive->indices[corner])->z >
                SATURN_DEMO_NEAR_DEPTH) {
                any_front = true;
                break;
            }
        }
        if (!any_front) {
            context->near_rejected[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        /* LOD keys on the NEAREST corner, deliberately unlike the painter key
         * below (L2141), which takes the furthest corner because ordering
         * wants a primitive's back edge. Detail selection wants the opposite:
         * a primitive is only cheap to degrade if ALL of it is far away.
         *
         * This used to take the maximum too, and that is what made near
         * geometry lose its texture (owner-reported 2026-09-04). BOB's ground
         * is large quads viewed at a grazing angle: the far corner is
         * thousands of units away while the near corner is under Mario's
         * feet, and the grazing angle compresses the quad on screen so the
         * projected span stays under far_enter_span (40). Max-corner depth
         * plus a small span is exactly the MID/FAR predicate, so a polygon the
         * player is standing on scored as distant and had its texture
         * dropped. Min-corner depth makes "far" mean entirely far. */
        int32_t depth = demo_view_read(lane, primitive->indices[0])->z;
        for (uint8_t corner = 1U; corner < 4U; corner++)
            if (demo_view_read(lane, primitive->indices[corner])->z < depth)
                depth = demo_view_read(lane, primitive->indices[corner])->z;
        const uint16_t projected_span =
            demo_primitive_projected_span(lane, primitive);
        const uint8_t lod_tier = demo_lod_select(
            context->transform_sequence, i, depth, projected_span);
        s_primitive_lod_suppressed[i] = 0U;
        s_primitive_lod_texture_downgraded[i] = 0U;
        /* The far mask is baked from stable source identity. Preserve the
         * route-critical prefix in every build; only optional tier-2 builds
         * remove the conservative one-in-eight far subset. */
        if (saturn_lod_can_suppress((saturn_lod_tier_t)lod_tier,
                                    SATURN_DEMO_POLY_TIER,
                                    sm64_saturn_bob_lod_far_mask[i] == 0U,
                                    primitive->source0,
                                    DEMO_LOD_MANDATORY_ROUTE_PREFIX)) {
            s_primitive_lod_suppressed[i] = 1U;
            s_primitive_visible[i] = 0U;
            continue;
        }
        if (saturn_lod_can_degrade_material((saturn_lod_tier_t)lod_tier,
                                            SATURN_DEMO_POLY_TIER,
                                            primitive->textured != 0U,
                                            primitive->tile_size > 16U)) {
            s_primitive_lod_texture_downgraded[i] = 1U;
        }
        bool any_back = false;
        sm64_saturn_terrain_clip_vertex_t clip_input[4];
        for (uint8_t corner = 0U; corner < 4U; corner++) {
            const sm64_saturn_vec3i_t view =
                *demo_view_read(lane, primitive->indices[corner]);
            clip_input[corner] = (sm64_saturn_terrain_clip_vertex_t){
                .view = view,
                .shade = (uint16_t)(((uint16_t)primitive->rgb[0] << 10) |
                                    ((uint16_t)primitive->rgb[1] << 5) |
                                    primitive->rgb[2]),
                .source_edge = corner,
                .source_corner = corner,
                .edge_t_q16 = 0};
            if (view.z < SATURN_DEMO_NEAR_DEPTH) any_back = true;
        }
        if (any_back && context->job->clip_near) {
            sm64_saturn_terrain_clip_output_t clipped;
            const int clipped_count = sm64_saturn_terrain_clip_near_quad(
                clip_input, SATURN_DEMO_NEAR_DEPTH, &clipped);
            if (clipped_count == 0) {
                if (clipped.classification == SM64_SATURN_TERRAIN_CLIP_AWAY)
                    context->clip_away[lane]++;
                else
                    context->clip_overflow[lane]++;
                context->near_rejected[lane]++;
                s_primitive_visible[i] = 0U;
                continue;
            }
            if (clipped_count < 3) {
                context->clip_to_one[lane]++;
                context->near_rejected[lane]++;
                s_primitive_visible[i] = 0U;
                continue;
            }
            if (clipped_count == 3) context->clip_to_one[lane]++;
            else if (clipped_count == 4) context->clip_to_two[lane]++;
            else if (clipped_count == 5)
                context->clip_recovery[lane]++;
            else {
                context->clip_overflow[lane]++;
                context->near_rejected[lane]++;
                continue;
            }
            /* Preserve the source material across the software near-plane
             * split.  Mapping the complete source tile over the clipped
             * distorted sprite can stretch its edge, but changing the same
             * surface to a flat recovery material as it crosses the plane is
             * far more visible: it makes terrain textures blink with camera
             * motion.  Z-Treme keeps textured ATTRs under UseNearClip
             * (ZT_LOAD_MODEL.c:88-102), while SlaveDriver confines its flat
             * drawClippedFace recovery to a bounded exceptional path
             * (WALLS.C:723-803).  A future UV-domain split can improve the
             * clipped edge without violating this material-stability rule. */
            s_primitive_clipped[i] = 1U;
            const uint8_t projected_count = (uint8_t)clipped_count;
            s_primitive_corner_count[i] = projected_count;
            bool clip_projection_valid = true;
            for (uint8_t corner = 0U; corner < projected_count; corner++) {
                if (!sm64_saturn_ir_project_view(
                        context->job, clipped.vertices[corner].view,
                        &s_clipped_projected[i][corner])) {
                    clip_projection_valid = false;
                    context->near_rejected[lane]++;
                    break;
                }
            }
            if (!clip_projection_valid) continue;
            if (projected_count == 3U)
                s_clipped_projected[i][3] = s_clipped_projected[i][2];
        }
        /* Match the castleviewer/SlaveDriver painter contract: a primitive's
         * farthest projected corner owns its painter key.  Using the nearest
         * corner made large BOB quads jump in front of neighboring surfaces as
         * the camera moved, because a quad crossing a depth boundary was
         * classified as near before its far half had been painted.  Proper
         * BSP splitting remains the long-term fix; max-z is the conservative
         * unsplit fallback used by the reference path. */
        int32_t z = demo_projected_read(lane, primitive->indices[0])->z;
        for (uint8_t corner = 1U; corner < 4U; corner++) {
            const int32_t corner_z =
                demo_projected_read(lane, primitive->indices[corner])->z;
            if (corner_z > z) z = corner_z;
        }
        const sm64_saturn_projected_vertex_t *area_vertices =
            s_primitive_clipped[i] != 0U
                ? s_clipped_projected[i] : NULL;
        const uint8_t area_count = s_primitive_clipped[i] != 0U
            ? s_primitive_corner_count[i] : 4U;
        bool has_area = false;
        for (uint8_t fan = 1U; fan + 1U < area_count; fan++) {
            const sm64_saturn_projected_vertex_t a = area_vertices != NULL
                ? area_vertices[0]
                : *demo_projected_read(lane, primitive->indices[0]);
            const sm64_saturn_projected_vertex_t b = area_vertices != NULL
                ? area_vertices[fan] :
                  *demo_projected_read(lane, primitive->indices[fan]);
            const sm64_saturn_projected_vertex_t c = area_vertices != NULL
                ? area_vertices[fan + 1U] :
                  *demo_projected_read(lane, primitive->indices[fan + 1U]);
            const int32_t cross =
                (int32_t)(b.x - a.x) * (c.y - a.y) -
                (int32_t)(b.y - a.y) * (c.x - a.x);
            if (cross != 0) {
                has_area = true;
                break;
            }
        }
        if (!has_area) {
            context->degenerate[lane]++;
            s_primitive_visible[i] = 0U;
            continue;
        }
        s_primitive_buckets[i] = (uint8_t)demo_bucket(z);
        s_primitive_depth[i] = z;
        s_primitive_visible[i] = 1U;
        context->visible[lane]++;
    }
}

typedef struct demo_terrain_compact_context {
    demo_classify_context_t *classify;
    sm64_saturn_terrain_result_spans_t *spans;
    uint32_t sequence;
} demo_terrain_compact_context_t;
typedef struct demo_terrain_queue_context {
    sm64_saturn_ir_transform_job_t job;
    uint16_t work_order[SM64_SATURN_BOB_PRIMITIVE_COUNT];
    uint16_t work_count;
    uint16_t required_positions;
    uint32_t transform_sequence;
    uint32_t sequence;
    uint8_t dual_phase;
} demo_terrain_queue_context_t;
static demo_terrain_queue_context_t s_terrain_queue_context
    DEMO_TERRAIN_TRANSFORM_CACHE;
_Static_assert(sizeof(demo_terrain_queue_context_t) <= UINT16_MAX,
               "terrain callback context must retain a bounded byte count");

static bool demo_snapshot_terrain_queue_context(
    demo_terrain_queue_context_t *context,
    const demo_classify_context_t *classify, uint32_t sequence)
{
    if (context == NULL || classify == NULL || classify->job == NULL ||
        classify->work_order == NULL || sequence == 0U ||
        s_render_work_count > SM64_SATURN_BOB_PRIMITIVE_COUNT)
        return false;
    *context = (demo_terrain_queue_context_t){0};
    context->job = *classify->job;
    memcpy(context->work_order, classify->work_order,
           s_render_work_count * sizeof(context->work_order[0]));
    context->work_count = s_render_work_count;
    context->required_positions = classify->required_positions;
    context->transform_sequence = classify->transform_sequence;
    context->sequence = sequence;
    context->dual_phase = classify->dual_phase ? 1U : 0U;
    return true;
}

static bool demo_terrain_queue_classify_context(
    const demo_terrain_queue_context_t *snapshot,
    demo_classify_context_t *classify)
{
    if (snapshot == NULL || classify == NULL || snapshot->sequence == 0U ||
        snapshot->work_count > SM64_SATURN_BOB_PRIMITIVE_COUNT)
        return false;
    *classify = (demo_classify_context_t){
        .primitives = s_bob_primitives_active,
        .work_order = snapshot->work_order,
        .camera = &snapshot->job.camera,
        .job = &snapshot->job,
        .work_count = snapshot->work_count,
        .required_positions = snapshot->required_positions,
        .transform_sequence = snapshot->transform_sequence,
        .dual_phase = snapshot->dual_phase != 0U,
    };
    return true;
}

typedef struct demo_terrain_queue_output {
    sm64_saturn_terrain_result_t *records;
    uint8_t *commands;
    uint16_t capacity;
    uint8_t writer_lane;
} demo_terrain_queue_output_t;

static bool demo_terrain_queue_claim_index(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, uint16_t *job_index)
{
    if (job_index != NULL) *job_index = UINT16_MAX;
    const uintptr_t first = (uintptr_t)&s_render_job_queue.jobs[0];
    const uintptr_t end = (uintptr_t)&s_render_job_queue.jobs[
        s_render_job_queue.count];
    const uintptr_t address = (uintptr_t)job;
    if (job == NULL || job_index == NULL ||
        (claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
        address < first || address >= end ||
        (address - first) % sizeof(*job) != 0U)
        return false;
    const uint16_t index = (uint16_t)((address - first) / sizeof(*job));
    if (sm64_saturn_render_job_queue_claimed_job(
            &s_render_job_queue, job->snapshot_generation, index,
            claimed_state) != job)
        return false;
    *job_index = index;
    return true;
}

/* The queue publishes no context pointer. Each callback resolves its own
 * statically bounded payload only after the exact generation/phase/claim
 * record has become visible through P2. */
static bool __attribute__((unused)) demo_render_queue_context_publish(
    uint16_t job_index, uint16_t payload_bytes)
{
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_published_job(
            &s_render_job_queue, s_render_job_graph.generation, job_index);
    return job != NULL && sm64_saturn_render_callback_context_publish(
        &s_render_callback_contexts, &s_render_job_queue,
        job->snapshot_generation, job_index, job->snapshot_generation,
        payload_bytes, SM64_SATURN_RENDER_OUTPUT_LANE_MASTER);
}

static bool __attribute__((unused)) demo_render_queue_contexts_publish(void)
{
    const uint32_t generation = s_render_job_graph.generation;
    if (generation == 0U || s_terrain_queue_context.sequence == 0U)
        return false;
    const sm64_saturn_render_job_queue_t *const queue =
        (const sm64_saturn_render_job_queue_t *)
            sm64_saturn_dual_frame_cache_through(&s_render_job_queue);
    for (uint16_t job_index = 0U; job_index < queue->count; job_index++) {
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_published_job(
                &s_render_job_queue, generation, job_index);
        if (job == NULL) return false;
        const bool world_job =
            job->callback_id == SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT ||
            job->callback_id == SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER;
        if (!world_job && s_mario_transform_context.sequence == 0U)
            return false;
        const uint16_t bytes =
            world_job
                ? (uint16_t)sizeof(s_terrain_queue_context)
                : (uint16_t)sizeof(s_mario_transform_context);
        if (!demo_render_queue_context_publish(job_index, bytes)) return false;
    }
    return true;
}

/* Called after the complete graph and Mario snapshot are published, but
 * before either SH-2 may claim a descriptor. */
static bool __attribute__((unused)) demo_render_queue_prepare_contexts(
    const demo_classify_context_t *classify, uint32_t sequence)
{
    return demo_snapshot_terrain_queue_context(
               &s_terrain_queue_context, classify, sequence) &&
        demo_render_queue_contexts_publish();
}

static bool demo_render_queue_reset_frame_banks(void)
{
    if (sm64_saturn_render_job_queue_generation(&s_render_job_queue) != 0U)
        return false;
    sm64_saturn_render_callback_context_bank_init(
        &s_render_callback_contexts);
    sm64_saturn_render_output_bank_init(
        &s_terrain_output_bank, SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN);
    sm64_saturn_render_output_bank_init(
        &s_actor_output_bank, SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR);
    memset(s_terrain_admit_metadata, 0, sizeof(s_terrain_admit_metadata));
    memset(s_terrain_result_metadata, 0, sizeof(s_terrain_result_metadata));
    memset(s_actor_admit_metadata, 0, sizeof(s_actor_admit_metadata));
    memset(s_actor_result_metadata, 0, sizeof(s_actor_result_metadata));
    return true;
}

static bool demo_render_queue_context_open(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, const void *cached_payload,
    uint16_t payload_bytes, const void **payload)
{
    if (payload != NULL) *payload = NULL;
    uint16_t job_index;
    sm64_saturn_render_callback_context_access_t access;
    if (job == NULL || payload == NULL ||
        !demo_terrain_queue_claim_index(job, claimed_state, &job_index))
        return false;
    bool opened = false;
    switch (job->callback_id) {
    case SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT:
        opened = sm64_saturn_render_callback_context_open_world_admit(
            &s_render_callback_contexts, &s_render_job_queue, job_index,
            claimed_state, job->snapshot_generation, payload_bytes,
            cached_payload, &access);
        break;
    case SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER:
        opened = sm64_saturn_render_callback_context_open_world_lower(
            &s_render_callback_contexts, &s_render_job_queue, job_index,
            claimed_state, job->snapshot_generation, payload_bytes,
            cached_payload, &access);
        break;
    case SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT:
        opened = sm64_saturn_render_callback_context_open_actor_admit(
            &s_render_callback_contexts, &s_render_job_queue, job_index,
            claimed_state, job->snapshot_generation, payload_bytes,
            cached_payload, &access);
        break;
    case SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER:
        opened = sm64_saturn_render_callback_context_open_actor_lower(
            &s_render_callback_contexts, &s_render_job_queue, job_index,
            claimed_state, job->snapshot_generation, payload_bytes,
            cached_payload, &access);
        break;
    default: break;
    }
    if (!opened) return false;
    *payload = access.payload;
    return true;
}

static bool demo_terrain_queue_publish_metadata(
    demo_terrain_queue_metadata_t *metadata,
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, uint8_t writer_lane,
    uint16_t record_count, uint32_t sequence)
{
    uint16_t job_index;
    if (metadata == NULL || job == NULL || sequence == 0U || writer_lane > 1U ||
        !demo_terrain_queue_claim_index(job, claimed_state, &job_index))
        return false;
    metadata = (demo_terrain_queue_metadata_t *)
        sm64_saturn_dual_frame_cache_through(metadata);
    metadata->ready = 0U;
    metadata->generation = job->snapshot_generation;
    metadata->sequence = sequence;
    metadata->job_index = job_index;
    metadata->record_count = record_count;
    metadata->writer_lane = writer_lane;
    metadata->claimed_state = (uint8_t)claimed_state;
    sm64_saturn_dual_frame_compiler_fence();
    metadata->ready = 1U;
    return true;
}

static bool demo_terrain_queue_publish_admit(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, uint8_t writer_lane,
    uint16_t transformed_count, uint32_t sequence)
{
    uint16_t job_index;
    if (!demo_terrain_queue_claim_index(job, claimed_state, &job_index))
        return false;
    return demo_terrain_queue_publish_metadata(
        &s_terrain_admit_metadata[job_index], job, claimed_state, writer_lane,
        transformed_count, sequence);
}

static bool demo_terrain_queue_publish_result(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, uint8_t writer_lane,
    uint16_t record_count, uint32_t sequence)
{
    uint16_t job_index;
    if (!demo_terrain_queue_claim_index(job, claimed_state, &job_index) ||
        record_count > job->output_capacity)
        return false;
    return demo_terrain_queue_publish_metadata(
        &s_terrain_result_metadata[job_index], job, claimed_state, writer_lane,
        record_count, sequence);
}

static const demo_terrain_queue_metadata_t *
demo_terrain_queue_result_metadata(uint16_t job_index,
                                   const sm64_saturn_render_job_t *job,
                                   uint8_t reader_lane)
{
    if (job == NULL || reader_lane > 1U ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY)
        return NULL;
    const demo_terrain_queue_metadata_t *const metadata =
        &s_terrain_result_metadata[job_index];
    if (metadata->ready == 0U) return NULL;
    sm64_saturn_dual_frame_compiler_fence();
    if (metadata->generation != job->snapshot_generation ||
        metadata->job_index != job_index || metadata->sequence == 0U ||
        metadata->record_count > job->output_capacity ||
        metadata->writer_lane > 1U ||
        (metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE))
        return NULL;
    uint8_t output_lane = UINT8_MAX;
    if (!sm64_saturn_render_output_bank_owner_lane(
            &s_terrain_output_bank, &s_render_job_queue, job_index,
            &output_lane) || output_lane != metadata->writer_lane)
        return NULL;
    return metadata;
}

static const demo_terrain_queue_metadata_t *
demo_terrain_queue_admit_metadata(uint16_t job_index,
                                  const sm64_saturn_render_job_t *job,
                                  uint8_t reader_lane)
{
    if (job == NULL || reader_lane > 1U ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY)
        return NULL;
    const demo_terrain_queue_metadata_t *const metadata =
        &s_terrain_admit_metadata[job_index];
    if (metadata->ready == 0U) return NULL;
    sm64_saturn_dual_frame_compiler_fence();
    uint8_t output_lane = UINT8_MAX;
    if (metadata->generation != job->snapshot_generation ||
        metadata->job_index != job_index || metadata->sequence == 0U ||
        metadata->writer_lane > 1U ||
        (metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
        !sm64_saturn_render_output_bank_owner_lane(
            &s_terrain_output_bank, &s_render_job_queue, job_index,
            &output_lane) || output_lane != metadata->writer_lane)
        return NULL;
    return metadata;
}

/* A graph callback calls this immediately after it has claimed a WORLD
 * descriptor, making begin-offset/lane inference impossible. */
static bool __attribute__((unused)) demo_terrain_queue_bind_output(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state,
    demo_terrain_queue_output_t *output)
{
    if (output != NULL) *output = (demo_terrain_queue_output_t){0};
    const uintptr_t job_address = (uintptr_t)job;
    const uintptr_t first_job = (uintptr_t)&s_render_job_queue.jobs[0];
    const uintptr_t end_job = (uintptr_t)&s_render_job_queue.jobs[
        s_render_job_queue.count];
    if (job == NULL || output == NULL ||
        (claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
        job_address < first_job || job_address >= end_job ||
        (job_address - first_job) % sizeof(*job) != 0U ||
        sm64_saturn_render_output_bank_kind_for_job(job) !=
            SM64_SATURN_RENDER_OUTPUT_BANK_TERRAIN)
        return false;
    const uint16_t job_index = (uint16_t)((job_address - first_job) /
                                          sizeof(*job));
    const sm64_saturn_render_job_t *const claimed =
        sm64_saturn_render_job_queue_claimed_job(
            &s_render_job_queue, job->snapshot_generation, job_index,
            claimed_state);
    if (claimed != job) return false;
    sm64_saturn_render_job_execution_t execution;
    if (!sm64_saturn_render_job_bridge_begin_output(
            &s_render_job_queue, &s_terrain_output_bank, &s_actor_output_bank,
            job_index, &execution))
        return false;
    output->records = sm64_saturn_render_payload_bank_write(
        &s_terrain_record_payload, &execution);
    output->commands = sm64_saturn_render_payload_bank_write(
        &s_terrain_command_payload, &execution);
    if (output->records == NULL || output->commands == NULL) {
        *output = (demo_terrain_queue_output_t){0};
        return false;
    }
    output->capacity = execution.output_capacity;
    output->writer_lane = execution.writer_lane;
    return true;
}

typedef struct demo_actor_queue_output {
    void *records;
    uint16_t capacity;
    uint16_t job_index;
    uint8_t writer_lane;
} demo_actor_queue_output_t;

static bool demo_actor_queue_bind_output(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state,
    demo_actor_queue_output_t *output)
{
    if (output != NULL) *output = (demo_actor_queue_output_t){0};
    uint16_t job_index;
    if (job == NULL || output == NULL ||
        sm64_saturn_render_output_bank_kind_for_job(job) !=
            SM64_SATURN_RENDER_OUTPUT_BANK_ACTOR ||
        !demo_terrain_queue_claim_index(job, claimed_state, &job_index))
        return false;
    sm64_saturn_render_job_execution_t execution;
    if (!sm64_saturn_render_job_bridge_begin_output(
            &s_render_job_queue, &s_terrain_output_bank, &s_actor_output_bank,
            job_index, &execution))
        return false;
    sm64_saturn_render_payload_bank_t *const payload =
        job->type == SM64_SATURN_RENDER_JOB_ACTOR_ADMIT
            ? &s_actor_vertex_payload : &s_actor_ref_payload;
    output->records = sm64_saturn_render_payload_bank_write(payload,
                                                             &execution);
    if (output->records == NULL) {
        *output = (demo_actor_queue_output_t){0};
        return false;
    }
    output->capacity = execution.output_capacity;
    output->job_index = execution.job_index;
    output->writer_lane = execution.writer_lane;
    return true;
}

static bool demo_actor_queue_publish_metadata(
    demo_actor_queue_metadata_t *metadata,
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, uint8_t writer_lane,
    uint16_t record_count, uint32_t sequence)
{
    uint16_t job_index;
    if (metadata == NULL || job == NULL || sequence == 0U || writer_lane > 1U ||
        record_count > job->output_capacity ||
        !demo_terrain_queue_claim_index(job, claimed_state, &job_index))
        return false;
    metadata = (demo_actor_queue_metadata_t *)
        sm64_saturn_dual_frame_cache_through(metadata);
    metadata->ready = 0U;
    metadata->generation = job->snapshot_generation;
    metadata->sequence = sequence;
    metadata->job_index = job_index;
    metadata->record_count = record_count;
    metadata->writer_lane = writer_lane;
    metadata->claimed_state = (uint8_t)claimed_state;
    sm64_saturn_dual_frame_compiler_fence();
    metadata->ready = 1U;
    return true;
}

static const demo_actor_queue_metadata_t *demo_actor_queue_metadata_read(
    const demo_actor_queue_metadata_t *metadata,
    const sm64_saturn_render_job_t *job, uint16_t job_index)
{
    if (metadata == NULL || job == NULL ||
        job_index >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY ||
        metadata->ready == 0U)
        return NULL;
    sm64_saturn_dual_frame_compiler_fence();
    uint8_t output_lane = UINT8_MAX;
    if (metadata->generation != job->snapshot_generation ||
        metadata->job_index != job_index || metadata->sequence == 0U ||
        metadata->record_count > job->output_capacity ||
        metadata->writer_lane > 1U ||
        (metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
         metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
        !sm64_saturn_render_output_bank_owner_lane(
            &s_actor_output_bank, &s_render_job_queue, job_index,
            &output_lane) || output_lane != metadata->writer_lane)
        return NULL;
    return metadata;
}

static bool demo_actor_queue_read_vertices_done(
    uint16_t job_index, uint8_t reader_lane,
    const demo_actor_queue_vertex_result_t **records, uint16_t *record_count,
    uint32_t *sequence)
{
    if (records != NULL) *records = NULL;
    if (record_count != NULL) *record_count = 0U;
    if (sequence != NULL) *sequence = 0U;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_done_job(&s_render_job_queue, job_index);
    const demo_actor_queue_metadata_t *const metadata = job == NULL ? NULL :
        demo_actor_queue_metadata_read(&s_actor_admit_metadata[job_index],
                                       job, job_index);
    if (records == NULL || record_count == NULL || sequence == NULL ||
        job == NULL || metadata == NULL ||
        job->type != SM64_SATURN_RENDER_JOB_ACTOR_ADMIT ||
        reader_lane > SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE)
        return false;
    *records = sm64_saturn_render_payload_bank_read(
        &s_actor_vertex_payload, &s_render_job_queue, &s_terrain_output_bank,
        &s_actor_output_bank, job_index, reader_lane);
    if (*records == NULL) return false;
    *record_count = metadata->record_count;
    *sequence = metadata->sequence;
    return true;
}

static bool demo_actor_queue_read_refs_done(
    uint16_t job_index, uint8_t reader_lane,
    const demo_actor_primitive_ref_t **records, uint16_t *record_count,
    uint32_t *sequence)
{
    if (records != NULL) *records = NULL;
    if (record_count != NULL) *record_count = 0U;
    if (sequence != NULL) *sequence = 0U;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_done_job(&s_render_job_queue, job_index);
    const demo_actor_queue_metadata_t *const metadata = job == NULL ? NULL :
        demo_actor_queue_metadata_read(&s_actor_result_metadata[job_index],
                                       job, job_index);
    if (records == NULL || record_count == NULL || sequence == NULL ||
        job == NULL || metadata == NULL ||
        job->type != SM64_SATURN_RENDER_JOB_ACTOR_LOWER ||
        reader_lane > SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE)
        return false;
    *records = sm64_saturn_render_payload_bank_read(
        &s_actor_ref_payload, &s_render_job_queue, &s_terrain_output_bank,
        &s_actor_output_bank, job_index, reader_lane);
    if (*records == NULL) return false;
    *record_count = metadata->record_count;
    *sequence = metadata->sequence;
    return true;
}

static bool __attribute__((unused)) demo_actor_queue_transform(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *opaque)
{
    (void)opaque;
    const void *published_context;
    if (!demo_render_queue_context_open(
            job, claimed_state, &s_mario_transform_context,
            (uint16_t)sizeof(s_mario_transform_context), &published_context))
        return false;
    const demo_mario_transform_context_t *const context = published_context;
    demo_actor_queue_output_t output;
    if (job == NULL || context == NULL || !context->snapshot.valid ||
        job->type != SM64_SATURN_RENDER_JOB_ACTOR_ADMIT ||
        job->callback_id != SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT ||
        job->input_offset != 0U ||
        job->input_count != context->transform_ref_count ||
        job->input_count > SM64_MARIO_VERTEX_COUNT ||
        !demo_actor_queue_bind_output(job, claimed_state, &output) ||
        output.capacity < job->input_count)
        return false;
    demo_actor_queue_vertex_result_t *const records = output.records;
    const int32_t sine = sm64_saturn_sins_q16(context->snapshot.yaw);
    const int32_t cosine = sm64_saturn_coss_q16(context->snapshot.yaw);
    for (uint16_t local = 0U; local < job->input_count; local++) {
        const uint16_t ref = (uint16_t)(job->input_offset + local);
        const uint16_t vertex = context->vertex_refs[ref];
        if (vertex >= context->vertex_count ||
            !demo_transform_mario_vertex(context, vertex, sine, cosine,
                                          &records[local].result))
            return false;
        records[local].vertex_id = vertex;
    }
    return demo_actor_queue_publish_metadata(
        &s_actor_admit_metadata[output.job_index], job, claimed_state,
        output.writer_lane, job->input_count, context->sequence);
}

static const demo_actor_vertex_result_t *demo_actor_queue_vertex_lookup(
    const demo_mario_transform_context_t *context,
    const demo_actor_queue_vertex_result_t *records, uint16_t record_count,
    uint16_t vertex)
{
    if (context == NULL || records == NULL || vertex >= context->vertex_count)
        return NULL;
    const uint16_t slot = context->vertex_ref_slot[vertex];
    if (slot >= record_count || records[slot].vertex_id != vertex) return NULL;
    return &records[slot].result;
}

static bool __attribute__((unused)) demo_actor_queue_classify(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *opaque)
{
    (void)opaque;
    const void *published_context;
    if (!demo_render_queue_context_open(
            job, claimed_state, &s_mario_transform_context,
            (uint16_t)sizeof(s_mario_transform_context), &published_context))
        return false;
    const demo_mario_transform_context_t *const context = published_context;
    demo_actor_queue_output_t output;
    uint16_t lower_job_index;
    uint16_t admit_job_index;
    if (job == NULL || context == NULL ||
        job->type != SM64_SATURN_RENDER_JOB_ACTOR_LOWER ||
        job->callback_id != SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER ||
        job->input_offset > SM64_MARIO_PRIMITIVE_COUNT ||
        job->input_count >
            (uint16_t)(SM64_MARIO_PRIMITIVE_COUNT - job->input_offset) ||
        !demo_actor_queue_bind_output(job, claimed_state, &output) ||
        output.capacity < job->input_count ||
        !demo_terrain_queue_claim_index(job, claimed_state, &lower_job_index) ||
        !sm64_saturn_render_job_graph_actor_lower_admit_done(
            &s_render_job_graph, job->snapshot_generation, lower_job_index,
            claimed_state, &admit_job_index))
        return false;
    const demo_actor_queue_vertex_result_t *vertices;
    uint16_t vertex_count;
    uint32_t sequence;
    if (!demo_actor_queue_read_vertices_done(
            admit_job_index, output.writer_lane, &vertices, &vertex_count,
            &sequence) || vertex_count != context->transform_ref_count ||
        sequence != context->sequence)
        return false;
    demo_actor_primitive_ref_t *const records = output.records;
    for (uint16_t local = 0U; local < job->input_count; local++) {
        const uint16_t primitive_id = (uint16_t)(job->input_offset + local);
        const uint16_t *const primitive = sm64_mario_primitives[primitive_id];
        demo_actor_primitive_ref_t *const result = &records[local];
        result->primitive_id = DEMO_ACTOR_PRIMITIVE_REJECTED;
        result->material_vertex = 0U;
        if (sm64_mario_material_rgb[primitive[0]][0] > 31U) continue;
        const demo_actor_vertex_result_t *const a =
            demo_actor_queue_vertex_lookup(context, vertices, vertex_count,
                                           primitive[1]);
        const demo_actor_vertex_result_t *const b =
            demo_actor_queue_vertex_lookup(context, vertices, vertex_count,
                                           primitive[2]);
        const demo_actor_vertex_result_t *const c =
            demo_actor_queue_vertex_lookup(context, vertices, vertex_count,
                                           primitive[3]);
        const demo_actor_vertex_result_t *const d =
            demo_actor_queue_vertex_lookup(context, vertices, vertex_count,
                                           primitive[4]);
        if (a == NULL || b == NULL || c == NULL || d == NULL ||
            a->valid == 0U || b->valid == 0U || c->valid == 0U ||
            d->valid == 0U)
            continue;
        const int32_t cross =
            (int32_t)(b->projected.x - a->projected.x) *
                (c->projected.y - a->projected.y) -
            (int32_t)(b->projected.y - a->projected.y) *
                (c->projected.x - a->projected.x);
        /* The Mario back-face cull. There is exactly one, and keeping it
         * that way is load-bearing -- see the history at the end of this
         * comment.
         *
         * Reachability: demo_actor_lower_compat_wrapper calls this, and
         * is registered in demo_render_job_callbacks as the ACTOR_LOWER
         * callback; demo_actor_queue_assemble_done (from
         * demo_render_finalize) then overwrites s_actor_refs with these
         * records, which demo_finalize_mario_draws reads. The
         * __attribute__((unused)) marker above is load-bearing noise: this
         * function is reached only through that callback table.
         *
         * Until 2026-09-10 a second, unreachable copy of this classifier
         * existed (demo_classify_mario_range, reached only from
         * demo_dispatch_mario_transform, which nothing called). Every sign
         * flip from 2026-09-03 onward edited THAT copy and correctly
         * reported "changed nothing". It has been deleted; do not
         * reintroduce a second Mario cull.
         *
         * Sign: projected coordinates are Y-DOWN
         * (saturn_ir_transform.c:53, screen_y = center_y - project(view.y)).
         * The fast3d frontend computes (v0-v1)x(v2-v1) = -(a x b) in Y-UP and
         * rejects G_CULL_BACK at cross >= 0 (saturn_fast3d_frontend.c:698).
         * This site computes a x b in Y-DOWN, and the Y flip negates it once
         * more, so the two are the SAME quantity and need the SAME predicate.
         * Counted on the real bank (all 644 primitives are G_CULL_BACK), the
         * old `cross <= 0` kept 39.2% of primitives and rejected 60.8%, with
         * 19.7% zero-area at integer screen resolution and dropped by either
         * sign. That census establishes the cull is live and does split the
         * body; it does NOT by itself label which half survives -- a keep
         * rate carries no near/far tag. The near/far claim rests on the
         * derivation above, not on the count.
         *
         * Why a kept far half looks like a facing error: VDP1 has no
         * Z-buffer, and Mario's whole body falls inside one 128-unit actor
         * painter bin (actor_depth_bin), so front and back faces of the same
         * body part share a meshlet and emit in bake order. Culling is the
         * only thing that can separate them. */
        if (cross >= 0)
            continue;
        result->primitive_id = primitive_id;
        result->material_vertex = primitive[0];
    }
    return demo_actor_queue_publish_metadata(
        &s_actor_result_metadata[output.job_index], job, claimed_state,
        output.writer_lane, job->input_count, context->sequence);
}

static bool demo_actor_queue_validate_payloads(
    const demo_mario_transform_context_t *context,
    const demo_actor_queue_vertex_result_t *vertices, uint16_t vertex_count,
    const uint16_t *lower_jobs,
    const demo_actor_primitive_ref_t *const *lower_records,
    const uint16_t *lower_record_counts, uint16_t lower_count)
{
    if (context == NULL || vertices == NULL || lower_jobs == NULL ||
        lower_records == NULL || lower_record_counts == NULL)
        return false;
    for (uint16_t local = 0U; local < vertex_count; local++) {
        const uint16_t vertex = vertices[local].vertex_id;
        if (vertex >= context->vertex_count ||
            context->vertex_ref_slot[vertex] != local)
            return false;
    }
    for (uint16_t stream = 0U; stream < lower_count; stream++) {
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_done_job(&s_render_job_queue,
                                                   lower_jobs[stream]);
        if (job == NULL || lower_records[stream] == NULL ||
            lower_record_counts[stream] != job->input_count)
            return false;
        for (uint16_t local = 0U; local < lower_record_counts[stream]; local++) {
            const uint16_t primitive = (uint16_t)(job->input_offset + local);
            const uint16_t result_id = lower_records[stream][local].primitive_id;
            if (primitive >= SM64_MARIO_PRIMITIVE_COUNT ||
                (result_id != DEMO_ACTOR_PRIMITIVE_REJECTED &&
                 result_id != primitive))
                return false;
        }
    }
    return true;
}

/* Terminal assembly is master-only and deterministic: lower descriptors are
 * consumed in queue order, then local result order. It restores the legacy
 * master-owned projected/ref banks so the proven Castle animation emission
 * path remains unchanged after the atomic scheduler cutover. */
static bool __attribute__((unused)) demo_actor_queue_assemble_done(
    uint8_t reader_lane, demo_mario_transform_context_t *context)
{
    if (reader_lane != SM64_SATURN_RENDER_OUTPUT_LANE_MASTER ||
        context == NULL)
        return false;
    uint16_t lower_jobs[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t lower_count = 0U;
    if (!sm64_saturn_render_job_graph_collect_done_actor_lower(
            &s_render_job_graph, s_render_job_graph.generation, lower_jobs,
            SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY, &lower_count))
        return false;
    const demo_actor_primitive_ref_t *lower_records[
        SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t lower_record_counts[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t identity_count = 0U;
    uint16_t expected_primitive = 0U;
    uint16_t admit_job_index = UINT16_MAX;
    uint32_t sequence = 0U;
    for (uint16_t stream = 0U; stream < lower_count; stream++) {
        const uint16_t job_index = lower_jobs[stream];
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_done_job(&s_render_job_queue,
                                                   job_index);
        uint16_t predecessor;
        uint32_t stream_sequence;
        if (job == NULL || job->input_offset != expected_primitive ||
            !sm64_saturn_render_job_graph_actor_done_lower_admit_done(
                &s_render_job_graph, s_render_job_graph.generation, job_index,
                &predecessor) ||
            !demo_actor_queue_read_refs_done(
                job_index, reader_lane, &lower_records[stream],
                &lower_record_counts[stream], &stream_sequence) ||
            lower_record_counts[stream] != job->input_count ||
            identity_count > SM64_MARIO_PRIMITIVE_COUNT - job->input_count)
            return false;
        if (admit_job_index == UINT16_MAX) admit_job_index = predecessor;
        if (predecessor != admit_job_index) return false;
        if (sequence == 0U) sequence = stream_sequence;
        if (stream_sequence != sequence || sequence != context->sequence)
            return false;
        for (uint16_t local = 0U; local < job->input_count; local++)
            s_actor_queue_merge_ids[identity_count++] =
                (sm64_saturn_render_job_result_identity_t){job_index, local};
        expected_primitive =
            (uint16_t)(expected_primitive + job->input_count);
    }
    if (expected_primitive != SM64_MARIO_PRIMITIVE_COUNT ||
        !sm64_saturn_render_job_graph_validate_actor_merge(
            &s_render_job_graph, s_render_job_graph.generation,
            s_actor_queue_merge_ids, identity_count))
        return false;
    const demo_actor_queue_vertex_result_t *vertices;
    uint16_t vertex_count;
    uint32_t vertex_sequence;
    if (admit_job_index == UINT16_MAX ||
        !demo_actor_queue_read_vertices_done(
            admit_job_index, reader_lane, &vertices, &vertex_count,
            &vertex_sequence) || vertex_count != context->transform_ref_count ||
        vertex_sequence != sequence ||
        !demo_actor_queue_validate_payloads(
            context, vertices, vertex_count, lower_jobs, lower_records,
            lower_record_counts, lower_count))
        return false;
    memset(s_actor_results, 0, sizeof(s_actor_results));
    memset(s_actor_vertex_owner, 0, sizeof(s_actor_vertex_owner));
    for (uint16_t local = 0U; local < vertex_count; local++) {
        const uint16_t vertex = vertices[local].vertex_id;
        s_actor_results[vertex] = vertices[local].result;
    }
    memset(s_actor_refs, 0xFF, sizeof(s_actor_refs));
    memset(s_actor_ref_owner, 0, sizeof(s_actor_ref_owner));
    for (uint16_t stream = 0U; stream < lower_count; stream++) {
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_done_job(&s_render_job_queue,
                                                   lower_jobs[stream]);
        for (uint16_t local = 0U; local < lower_record_counts[stream]; local++) {
            const uint16_t primitive = (uint16_t)(job->input_offset + local);
            const demo_actor_primitive_ref_t result = lower_records[stream][local];
            s_actor_refs[primitive] = result;
        }
    }
    return true;
}

/* One coarse same-frame callback. Each SH-2 transforms its owned subset of
 * the shared indexed position bank, crosses one phase fence, then classifies
 * and compacts its disjoint primitive range. This deliberately follows
 * SlaveDriver's coarse split/join rather than dispatching per face. */
/* Both the isolated legacy coarse worker and the live graph callback enter this
 * exact producer.  The caller supplies the physical output arena and the
 * claimant lane; this routine must never infer either from a logical range.
 */
static bool demo_terrain_compact_transformed(
    demo_terrain_compact_context_t *context, uint16_t begin, uint16_t end,
    uint8_t lane, sm64_saturn_terrain_result_arena_t *arena)
{
    if (context == NULL || context->classify == NULL || arena == NULL ||
        lane > 1U || begin > end || end > context->classify->work_count)
        return false;
    demo_classify_exact(context->classify, begin, end, lane);
    for (uint16_t work = begin; work < end; work++) {
        const uint16_t primitive_index = context->classify->work_order[work];
        if (s_primitive_visible[primitive_index] == 0U) continue;
        const sm64_saturn_bob_primitive_t *primitive =
            &context->classify->primitives[primitive_index];
        const sm64_saturn_projected_vertex_t *projected =
            s_primitive_clipped[primitive_index] != 0U
                ? s_clipped_projected[primitive_index] : NULL;
        const uint8_t clipped_count =
            s_primitive_corner_count[primitive_index];
        const uint8_t result_count = clipped_count == 5U ? 2U : 1U;
        sm64_saturn_terrain_result_t *results = NULL;
        uint8_t *commands = NULL;
        if (!sm64_saturn_terrain_result_reserve(
                arena, result_count, &results, &commands))
            continue;
        const uint16_t first_command =
            (uint16_t)(arena->count - result_count);
        const bool recovery = s_primitive_recovery[primitive_index] != 0U;
        const bool texture_suppressed =
            s_primitive_lod_texture_downgraded[primitive_index] != 0U;
        const uint16_t shade = (uint16_t)(
            ((uint16_t)primitive->rgb[0] << 10) |
            ((uint16_t)primitive->rgb[1] << 5) | primitive->rgb[2]);
        const uint16_t colors[4] = {shade, shade, shade, shade};
        const uint16_t material_flags =
            SM64_SATURN_TERRAIN_RESULT_OPAQUE |
            (primitive->textured != 0U
                ? SM64_SATURN_TERRAIN_RESULT_TEXTURED : 0U) |
            (recovery ? SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL : 0U) |
            (texture_suppressed
                ? SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED : 0U);
        /* The current compact BOB bake exposes one primitive RGB only, not
         * four post-light corner shades. Never manufacture four identical
         * inputs from it to select FLAT: that would flatten a future gradient.
         * The pixels are retained as the conservative Gouraud fallback until
         * the generated scene supplies real per-corner shade data. */
        const bool post_light_shades_available = false;
        /* Classification happens in the owning worker before the compact
         * record crosses CPUs. Recovery deliberately remains conservative
         * because its template uses the dynamic Gouraud material; normal
         * equal post-light colors and LOD-suppressed colors select REPLACE. */
        const sm64_saturn_shade_path_t shade_path =
            sm64_saturn_terrain_shade_path(material_flags,
                recovery || !post_light_shades_available ? NULL : colors);
        const sm64_saturn_terrain_resolved_command_t *resolved =
            demo_terrain_resolved_template(
                primitive_index, recovery, texture_suppressed);
        for (uint8_t fragment = 0U; fragment < result_count; fragment++) {
            sm64_saturn_terrain_result_t *result = &results[fragment];
            uint8_t *command = commands +
                (size_t)fragment * SM64_SATURN_TERRAIN_COMMAND_BYTES;
            const uint8_t result_corners =
                fragment == 1U ? 3U :
                (clipped_count < 4U ? clipped_count : 4U);
            int16_t shape_vertices[4][2];
            for (uint8_t corner = 0U; corner < 4U; corner++) {
                uint8_t source_corner;
                if (fragment == 1U) {
                    static const uint8_t fan_tail[4] = {0U, 3U, 4U, 4U};
                    source_corner = fan_tail[corner];
                } else {
                    source_corner = corner < result_corners
                        ? corner : (uint8_t)(result_corners - 1U);
                }
            const sm64_saturn_projected_vertex_t screen = projected != NULL
                ? projected[source_corner]
                : *demo_projected_read(lane,
                                       primitive->indices[source_corner]);
                shape_vertices[corner][0] = screen.x;
                shape_vertices[corner][1] = screen.y;
            }
            (void)sm64_saturn_terrain_result_write_with_shades(
                arena, result, command,
                (uint16_t)(first_command + fragment), primitive_index,
                *((const uint16_t *)sm64_saturn_dual_frame_read_range(
                    lane, 0U, s_primitive_leaf_id) + primitive_index),
                (uint32_t)s_primitive_depth[primitive_index], result_corners,
                (s_primitive_clipped[primitive_index] != 0U
                    ? SM64_SATURN_TERRAIN_CLIP_CROSSES
                    : SM64_SATURN_TERRAIN_CLIP_FRONT) |
                (s_primitive_recovery[primitive_index] != 0U
                    ? SM64_SATURN_TERRAIN_RESULT_RECOVERY_MATERIAL : 0U) |
                (s_primitive_lod_texture_downgraded[primitive_index] != 0U
                    ? SM64_SATURN_TERRAIN_RESULT_TEXTURE_SUPPRESSED : 0U) |
                (post_light_shades_available
                    ? SM64_SATURN_TERRAIN_RESULT_POST_LIGHT_SHADES : 0U) |
                sm64_saturn_terrain_shade_path_compact_flags(shade_path),
                resolved, colors, shape_vertices);
        }
    }
    sm64_saturn_terrain_result_arena_seal(arena, context->sequence);
    return true;
}

/* The legacy worker keeps its transform+lower operation together. Queue
 * WORLD_LOWER deliberately bypasses this wrapper: its graph predecessor is
 * WORLD_ADMIT, which publishes the transformed-position payload first. */
static bool demo_terrain_compact_exact(demo_terrain_compact_context_t *context,
                                       uint16_t begin, uint16_t end,
                                       uint8_t lane,
                                       sm64_saturn_terrain_result_arena_t *arena)
{
    return context != NULL &&
        demo_transform_owned_positions(context->classify, lane, false) &&
        demo_terrain_compact_transformed(context, begin, end, lane, arena);
}

/* Legacy-only adapter.  It keeps the old worker's fixed split contained while
 * the live queue callback below proves that queue work takes its lane from
 * the accepted descriptor claim instead. */
static void demo_terrain_compact_range(void *opaque, uint16_t begin,
                                       uint16_t end)
{
    demo_terrain_compact_context_t *context = opaque;
    if (context == NULL) return;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    sm64_saturn_terrain_result_arena_t *const arena = lane == 0U
        ? &context->spans->master : &context->spans->slave;
    (void)demo_terrain_compact_exact(context, begin, end, lane, arena);
}

/* WORLD_ADMIT owns transformed-position publication by descriptor identity.
 * Its P2 metadata record is the release between transform payload writes and
 * the graph-dependent lower callback. */
static bool __attribute__((unused)) demo_terrain_queue_world_admit(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *opaque)
{
    (void)opaque;
    const void *published_context;
    if (!demo_render_queue_context_open(
            job, claimed_state, &s_terrain_queue_context,
            (uint16_t)sizeof(s_terrain_queue_context),
            &published_context))
        return false;
    const demo_terrain_queue_context_t *const context = published_context;
    demo_classify_context_t classify;
    demo_terrain_queue_output_t output;
    sm64_saturn_terrain_queue_handoff_t handoff;
    if (job == NULL || context == NULL ||
        !demo_terrain_queue_classify_context(context, &classify) ||
        job->type != SM64_SATURN_RENDER_JOB_WORLD_ADMIT ||
        job->callback_id != SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT ||
        !demo_terrain_queue_bind_output(job, claimed_state, &output) ||
        !sm64_saturn_terrain_queue_handoff_single_producer(
            output.writer_lane, &handoff) ||
        handoff.peer_transform_required != 0U)
        return false;
    /* One coarse admit descriptor owns the complete transformed-position
     * payload. Rebuild ownership from the actual claimant lane; retaining the
     * legacy logical split here would leave half the visible positions
     * unwritten when the job is stolen by the other SH-2. */
    demo_prepare_position_owners(
        handoff.producer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_MASTER
            ? context->work_count : 0U,
        handoff.producer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE);
    if (!demo_transform_owned_positions(
            &classify, output.writer_lane, true))
        return false;
    return demo_terrain_queue_publish_admit(
        job, claimed_state, output.writer_lane,
        (uint16_t)classify.transformed[output.writer_lane],
        context->sequence);
}

/* Runtime completes the exact claimed WORLD_LOWER job
 * only after this returns true, so sealing the result arena here precedes the
 * queue's DONE publication. */
static bool __attribute__((unused)) demo_terrain_queue_world_lower(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *opaque)
{
    (void)opaque;
    const void *published_context;
    if (!demo_render_queue_context_open(
            job, claimed_state, &s_terrain_queue_context,
            (uint16_t)sizeof(s_terrain_queue_context),
            &published_context))
        return false;
    const demo_terrain_queue_context_t *const context = published_context;
    demo_classify_context_t classify;
    demo_terrain_compact_context_t compact = {
        .classify = &classify,
        .spans = NULL,
        .sequence = context->sequence,
    };
    demo_terrain_queue_output_t output;
    uint16_t lower_job_index;
    uint16_t admit_job_index;
    if (job == NULL || context == NULL ||
        !demo_terrain_queue_classify_context(context, &classify) ||
        job->type != SM64_SATURN_RENDER_JOB_WORLD_LOWER ||
        job->callback_id != SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER ||
        job->input_offset > context->work_count ||
        job->input_count >
            (uint16_t)(context->work_count - job->input_offset) ||
        !demo_terrain_queue_bind_output(job, claimed_state, &output) ||
        output.capacity == 0U)
        return false;
    if (!demo_terrain_queue_claim_index(job, claimed_state, &lower_job_index) ||
        !sm64_saturn_render_job_graph_world_lower_admit_done(
            &s_render_job_graph, job->snapshot_generation, lower_job_index,
            claimed_state, &admit_job_index))
        return false;
    const sm64_saturn_render_job_t *const admit =
        sm64_saturn_render_job_queue_done_job(&s_render_job_queue,
                                               admit_job_index);
    const demo_terrain_queue_metadata_t *const admit_metadata =
        demo_terrain_queue_admit_metadata(admit_job_index, admit,
                                          output.writer_lane);
    sm64_saturn_terrain_queue_handoff_t handoff;
    if (admit_metadata == NULL ||
        !sm64_saturn_terrain_queue_handoff_single_producer(
            admit_metadata->writer_lane, &handoff) ||
        handoff.peer_transform_required != 0U)
        return false;
    /* Rebuild the owner bytes in this lower claimant's local cache from the
     * exact DONE admit claimant before selecting P1/P2 position payloads.
     * This makes slave-admit -> master-lower safe across frame generations. */
    demo_prepare_position_owners(
        handoff.producer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_MASTER
            ? context->work_count : 0U,
        handoff.producer_lane == SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE);
    sm64_saturn_terrain_result_arena_t arena;
    sm64_saturn_terrain_result_arena_init(
        &arena, output.records, output.commands, output.capacity, 8U);
    if (!demo_terrain_compact_transformed(
        &compact, job->input_offset,
        (uint16_t)(job->input_offset + job->input_count),
        output.writer_lane, &arena))
        return false;
    return demo_terrain_queue_publish_result(
        job, claimed_state, output.writer_lane, arena.count, compact.sequence);
}

#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
struct demo_actor_emit_context;
struct demo_actor_command_sink;
static bool demo_generic_actor_emit(const struct demo_actor_emit_context *ctx,
                                    struct demo_actor_command_sink *sink,
                                    sm64_saturn_vdp1_backend_t *backend);

static int64_t demo_generic_saturating_add_i64(int64_t left, int64_t right)
{
    if (right > 0 && left > INT64_MAX - right) return INT64_MAX;
    if (right < 0 && left < INT64_MIN - right) return INT64_MIN;
    return left + right;
}

static int64_t demo_generic_saturating_mul_i64(int64_t left, int64_t right)
{
    if (left == 0 || right == 0) return 0;
    if (left == -1) return right == INT64_MIN ? INT64_MAX : -right;
    if (right == -1) return left == INT64_MIN ? INT64_MAX : -left;
    if (left > 0) {
        if (right > 0 && left > INT64_MAX / right) return INT64_MAX;
        if (right < 0 && right < INT64_MIN / left) return INT64_MIN;
    } else {
        if (right > 0 && left < INT64_MIN / right) return INT64_MIN;
        if (right < 0 && left < INT64_MAX / right) return INT64_MAX;
    }
    return left * right;
}

static int32_t demo_generic_clamp_i64_i32(int64_t value)
{
    return value > INT32_MAX ? INT32_MAX :
        value < INT32_MIN ? INT32_MIN : (int32_t)value;
}

static bool demo_generic_actor_world_vertex(
    const sm64_saturn_actor_instance_snapshot_t *instance,
    const sm64_saturn_actor_pose_work_t *pose, uint16_t vertex,
    sm64_saturn_vec3i_t *world)
{
    int64_t scaled_x, scaled_y, scaled_z, rotated_x, rotated_z;
    const int32_t sine = instance == NULL ? 0 :
        sm64_saturn_sins_q16(instance->angle[1]);
    const int32_t cosine = instance == NULL ? 0 :
        sm64_saturn_coss_q16(instance->angle[1]);
    if (instance == NULL || pose == NULL || pose->vertices == NULL ||
        world == NULL || vertex >= pose->vertex_capacity)
        return false;
    /* Instance scale is already folded into pose->vertices by
     * sm64_saturn_actor_pose_evaluate (see saturn_actor_pose.c); left-shift
     * to Q16.16 to match the rotate math below, do not re-scale here --
     * this used to multiply by instance->scale_q16 a second time, which
     * would now double-apply it. */
    scaled_x = (int64_t)pose->vertices[vertex][0] << 16;
    scaled_y = (int64_t)pose->vertices[vertex][1] << 16;
    scaled_z = (int64_t)pose->vertices[vertex][2] << 16;
    rotated_x = demo_generic_saturating_add_i64(
        demo_generic_saturating_mul_i64(scaled_x, cosine),
        demo_generic_saturating_mul_i64(scaled_z, sine)) >> 32;
    rotated_z = demo_generic_saturating_add_i64(
        demo_generic_saturating_mul_i64(-scaled_x, sine),
        demo_generic_saturating_mul_i64(scaled_z, cosine)) >> 32;
    world->x = demo_generic_clamp_i64_i32(
        ((int64_t)instance->position_q16[0] >> 16) + rotated_x);
    world->y = demo_generic_clamp_i64_i32(
        ((int64_t)instance->position_q16[1] >> 16) + (scaled_y >> 16));
    world->z = demo_generic_clamp_i64_i32(
        ((int64_t)instance->position_q16[2] >> 16) + rotated_z);
    return true;
}

/* File-scope tag declaration first: a tag whose first mention is inside a
 * prototype gets prototype scope, which would make this a different type from
 * the definition below. */
struct demo_actor_phase_accum;
static bool demo_generic_actor_run(struct demo_actor_phase_accum *accum,
                                   bool on_slave);

/* Compiler barrier only: every field it orders lives in uncached storage, so
 * there is no write-back to flush -- the hazard being prevented is the
 * compiler hoisting a load or sinking a store across the handshake, not the
 * cache. Same shape as actor_queue_fence()/runtime_fence() elsewhere. */
static inline void demo_render_fence(void)
{
#if defined(__GNUC__)
    __asm__ volatile("" ::: "memory");
#endif
}

/* A slave that faulted mid-phase must fail the frame closed rather than hang
 * the master forever. The phase is a few thousand cycles of work the master
 * has already overlapped, so any spin count this large means the peer is gone,
 * not merely slow. */
#define DEMO_ACTOR_PHASE_WAIT_LIMIT UINT32_C(0x00400000)

/* Mario's meshlet walk is split into publish / run / join (see
 * demo_mario_phase_publish) so that the run half can execute on the slave.
 *
 * DEMO_MARIO_PHASE_SLAVE is deliberately a file-local switch and not a sealed
 * build flag: a sealed flag shifts every build identity, and the point of this
 * switch is to let a regression be bisected to exactly this change. Set it to
 * 0 and the walk runs inline on the master, which is the pre-split behaviour
 * with the work moved earlier in the frame. */
#if SATURN_FEATURE_ACTOR_PHASE_SLAVE && defined(__sh__)
#define DEMO_MARIO_PHASE_SLAVE 1
#else
#define DEMO_MARIO_PHASE_SLAVE 0
#endif

/* S1.11: everything the generic-actor phase accumulates while it walks the
 * instance queue. This block is written by whichever CPU runs the phase and
 * read by the master when it folds the totals in, so it must not be cached:
 * the two SH-2s have no coherency, and a cached accumulator would let each
 * CPU's write-back discard the other's. The master folds it after the phase
 * completes -- a sequenced handoff across a barrier, never a concurrent
 * read-modify-write.
 *
 * It replaces two shared mutable writes that used to sit inside the process
 * callback: transaction->generic_actor_gouraud_count, and the four meshlet
 * counters written straight into the frame profile. Both were safe only
 * because the drain was master-only. */
/* S1.12 step 2: every profile counter the generic-actor EMIT path writes.
 * Emit is the next phase slated to move off the master, and these are
 * read-modify-writes into the shared frame profile -- the same shape as the
 * meshlet counters S1.11 had to relocate. Landing them in the uncached phase
 * block now, while emit still runs master-only, is a behaviour-preserving step
 * that removes the hazard before any concurrency exists to trip over it. */
typedef struct demo_actor_emit_counters {
    uint32_t instance_skipped;
    uint32_t prims_seen;
    uint32_t reject_switch_case;
    uint32_t reject_near_far;
    uint32_t reject_degenerate;
    uint32_t reject_backface;
    uint32_t flat_replace;
    uint32_t prims_emitted;
    uint32_t triangles_vdp1_emitted;
    uint32_t triangles_emitted;
    uint32_t actor_primitives_emitted;
    uint32_t texture_commands;
} demo_actor_emit_counters_t;

/* S1.12 step 3: everything demo_generic_actor_emit reads out of the render
 * transaction, published explicitly.
 *
 * The transaction lives in cached, master-owned HWRAM, so a slave that
 * dereferenced it would read through its own incoherent cache. Naming the
 * dependency as a value type both makes the offload possible and makes the
 * coupling auditable -- it was previously eleven scattered `transaction->`
 * reads with no statement anywhere of what emit actually needs. */
typedef struct demo_actor_emit_context {
    sm64_saturn_actor_runtime_storage_t *actor_runtime;
    const sm64_saturn_actor_instance_snapshot_t *snapshots;
    sm64_saturn_gouraud_table_t *gouraud_staging;
    uintptr_t gouraud_vram_base;
    sm64_saturn_ir_transform_job_t actor_job;
    uint32_t handoff_generation;
    uint16_t instance_count;
    uint16_t gouraud_first;
    uint16_t gouraud_limit;
    uint16_t gouraud_capacity;
    uint16_t gouraud_count;
    uint16_t output_count;
    uint16_t occluder_count;
    sm64_saturn_actor_occluder_t occluders[8];
} demo_actor_emit_context_t;

/* Where emit puts the commands it builds.
 *
 * `commands == NULL` appends straight into the VDP1 arena, which is what the
 * master does and is byte-for-byte the pre-S1.12 path. Otherwise commands land
 * in a caller-owned staging array and the master splices them into the arena
 * afterwards -- the arrangement that lets the slave build them without ever
 * touching the shared arena cursor. Splicing preserves painter order because
 * the staged block is appended after terrain and Mario, keeping actor commands
 * at the highest arena indices exactly as before. */
typedef struct demo_actor_command_sink {
    vdp1_cmdt_t *commands;
    uint16_t capacity;
    uint16_t count;
    uint8_t overflowed;
} demo_actor_command_sink_t;

typedef struct demo_actor_phase_accum {
    /* --- published by the master before the phase is dispatched --- */
    sm64_saturn_actor_output_record_t *outputs;
    sm64_saturn_actor_instance_queue_t *queue;
    sm64_saturn_render_view_t camera;
    uint32_t generation;
    uint16_t descriptor_count;
    /* --- accumulated by whichever CPU runs the phase --- */
    sm64_saturn_actor_meshlet_stats_t walk;
    uint32_t gouraud_count;
    uint32_t instance_skipped;
    demo_actor_emit_counters_t emit;
    /* --- dispatch handshake --- */
    sm64_saturn_actor_instance_processor_t processor;
    volatile uint32_t pending;
    volatile uint32_t done;
    volatile uint32_t drained;
    volatile uint32_t ok;
    /* --- S1.12 step 3: the emit phase, dispatched separately --- */
    demo_actor_emit_context_t emit_ctx;
    demo_actor_command_sink_t emit_sink;
    volatile uint32_t emit_pending;
    volatile uint32_t emit_done;
    volatile uint32_t emit_ok;
    /* --- Mario's meshlet walk, dispatched separately (2026-09-11) ---
     *
     * The single largest block of master-only work in the frame: pose
     * evaluate + live depth bounds + meshlet core + position ref measured at
     * ~400k cycles/frame, during which the slave is measured 100% idle. It is
     * a separate phase from the generic-actor one above because the master
     * must join it at a different point -- before
     * demo_snapshot_mario_transform_context needs the position refs.
     *
     * Inputs are copied by value rather than referenced: the master may move
     * on to terrain work while the slave walks, so nothing here may alias
     * master frame state that is still changing. `pose` is the exception and
     * carries pointers, which is why the run purges those lines before
     * reading -- see demo_mario_phase_run. */
    sm64_saturn_render_snapshot_t mario_snapshot;
    sm64_saturn_mario_actor_pose_t mario_pose;
    sm64_saturn_render_view_t mario_view;
    sm64_saturn_actor_meshlet_stats_t mario_walk;
    uint16_t mario_draw_count;
    uint16_t mario_transform_ref_count;
    volatile uint32_t mario_pending;
    volatile uint32_t mario_done;
    volatile uint32_t mario_ok;
} demo_actor_phase_accum_t;

/* Slave-built commands land here, then the master splices them into the arena.
 * Uncached because the producer and consumer are different CPUs with no
 * coherency: a cached staging array would leave the slave's commands sitting
 * in its own dirty lines while the master read stale memory. ~16 KB. */
static vdp1_cmdt_t s_actor_emit_staging[DEMO_ACTOR_EMIT_STAGING_CAPACITY]
    __attribute__((section(".lwram_bss")))
    __attribute__((aligned(DEMO_ACTOR_WORKSPACE_CACHE_LINE_BYTES)));

static vdp1_cmdt_t *demo_actor_emit_staging_uncached(void)
{
#if defined(__sh__)
    return (vdp1_cmdt_t *)(CPU_CACHE_THROUGH |
                           (uintptr_t)s_actor_emit_staging);
#else
    return s_actor_emit_staging;
#endif
}

static demo_actor_phase_accum_t s_actor_phase_accum;

static demo_actor_phase_accum_t *demo_actor_phase_accum_uncached(void)
{
#if defined(__sh__)
    return (demo_actor_phase_accum_t *)(CPU_CACHE_THROUGH |
                                        (uintptr_t)&s_actor_phase_accum);
#else
    return &s_actor_phase_accum;
#endif
}

static void demo_actor_phase_accum_reset(void)
{
    demo_actor_phase_accum_t *const accum = demo_actor_phase_accum_uncached();
    accum->walk.meshlets_tested = 0U;
    accum->walk.meshlets_admitted = 0U;
    accum->walk.meshlets_culled = 0U;
    accum->walk.positions_admitted = 0U;
    accum->gouraud_count = 0U;
    accum->instance_skipped = 0U;
    memset((void *)&accum->emit, 0, sizeof(accum->emit));
    accum->pending = 0U;
    accum->done = 0U;
    accum->drained = 0U;
    accum->ok = 0U;
    accum->emit_pending = 0U;
    accum->emit_done = 0U;
    accum->emit_ok = 0U;
    memset((void *)&accum->emit_sink, 0, sizeof(accum->emit_sink));
    /* Deliberately NOT resetting the mario_* fields here. This function is
     * called from demo_generic_actor_publish, which runs AFTER Mario's phase
     * has already been raised -- resetting them here would silently clear the
     * raise every frame, leaving the slave with nothing to claim and the
     * master spinning out the full wait limit at the join. Mario's own
     * publish owns that reset; see demo_mario_phase_publish. */
}

static bool demo_mario_phase_run(demo_actor_phase_accum_t *accum);

/* The slave's half of the handshake. Registered once with the render job
 * runtime; offered every slave wake-up and claims only the ones raised for an
 * actor phase, so the graph pass is untouched on every other wake-up. */
static bool demo_actor_phase_slave_task(void)
{
    demo_actor_phase_accum_t *const accum = demo_actor_phase_accum_uncached();
    /* S1.12 step 3: the emit dispatch is a separate wake-up raised later in
     * the frame, after the Gouraud reservation. Check it first -- it is never
     * outstanding at the same time as the two below, because the master joins
     * them before raising it. */
    if (accum->emit_pending != 0U) {
        accum->emit_pending = 0U;
        demo_render_fence();
        accum->emit_ok = demo_generic_actor_emit(
            (const demo_actor_emit_context_t *)&accum->emit_ctx,
            (demo_actor_command_sink_t *)&accum->emit_sink, NULL) ? 1U : 0U;
        demo_render_fence();
        accum->emit_done = 1U;
        demo_render_fence();
        return true;
    }
    bool claimed = false;
    /* The generic-actor phase and Mario's walk are both raised before a single
     * wake-up, so one invocation must be able to claim both. A second
     * cpu_dual_slave_notify() issued while the slave is already inside this
     * function is not guaranteed to be observed, which is why the master does
     * not dispatch them separately. They run in the master's join order: the
     * generic phase is joined first, so it runs first. */
    if (accum->pending != 0U) {
        accum->pending = 0U;
        demo_render_fence();
        accum->ok = demo_generic_actor_run(accum, true) ? 1U : 0U;
        demo_render_fence();
        /* `done` is the release marker: publish every other field before it,
         * so a master that observes done == 1 sees a complete phase. */
        accum->done = 1U;
        demo_render_fence();
        claimed = true;
    }
    if (accum->mario_pending != 0U) {
        accum->mario_pending = 0U;
        demo_render_fence();
        accum->mario_ok = demo_mario_phase_run(accum) ? 1U : 0U;
        demo_render_fence();
        accum->mario_done = 1U;
        demo_render_fence();
        claimed = true;
    }
    return claimed;
}

/* Master's half. With the offload off this simply runs the phase inline,
 * which is exactly the pre-S1.11 behaviour. */
static void demo_actor_phase_dispatch(demo_actor_phase_accum_t *accum)
{
    accum->done = 0U;
    accum->ok = 0U;
#if SATURN_FEATURE_ACTOR_PHASE_SLAVE && defined(__sh__)
    accum->pending = 1U;
    demo_render_fence();
    sm64_saturn_render_job_runtime_dispatch_slave_task();
#else
    accum->pending = 0U;
    accum->ok = demo_generic_actor_run(accum, false) ? 1U : 0U;
    demo_render_fence();
    accum->done = 1U;
#endif
}

/* Bounded wait for the phase. The bound matters: a slave that faulted must
 * fail the frame closed rather than hang the master forever, which is the
 * same discipline the rest of this pipeline uses. The master reaches here
 * only after its own overlapped work, so in the healthy case the phase is
 * already complete and this spins zero times. */
static bool demo_actor_phase_wait(demo_actor_phase_accum_t *accum)
{
    if (accum == NULL) return false;
#if SATURN_FEATURE_ACTOR_PHASE_SLAVE && defined(__sh__)
    uint32_t spins = 0U;
    while (accum->done == 0U) {
        if (++spins > DEMO_ACTOR_PHASE_WAIT_LIMIT) return false;
    }
    demo_render_fence();
#endif
    return accum->done != 0U;
}

static bool demo_generic_actor_process(
    const sm64_saturn_actor_instance_descriptor_t *descriptor,
    const sm64_saturn_actor_instance_snapshot_t *snapshot, uint8_t lane,
    void *context, uint16_t *output_count)
{
    /* S1.11: the context is the uncached phase block, never the transaction.
     * The transaction lives in cached HWRAM and is master-owned, so a slave
     * that dereferenced it would be reading through its own incoherent cache.
     * Everything this callback needs is published into the block before the
     * phase is dispatched. */
    demo_actor_phase_accum_t *const accum = context;
    sm64_saturn_actor_bundle_resolution_t resolution;
    bool prepared = false;
    if (output_count != NULL) *output_count = 0U;
    if (accum == NULL || accum->outputs == NULL || descriptor == NULL ||
        snapshot == NULL || output_count == NULL ||
        descriptor->output_offset > SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING ||
        descriptor->output_capacity >
            SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING - descriptor->output_offset)
        return false;
    const uint8_t workspace_lane =
        lane == SM64_SATURN_ACTOR_CLAIMED_MASTER ? 0U : 1U;
    if (!sm64_saturn_source_scene_bundle_resolve(
            snapshot, workspace_lane,
            &accum->outputs[descriptor->output_offset],
            descriptor->output_capacity, &resolution))
        return false;
    sm64_saturn_render_view_t view = accum->camera;
    view.generation = descriptor->generation;
    prepared = resolution.package_generation ==
            snapshot->scene_package_generation &&
        sm64_saturn_actor_meshlets_prepare_bank_stats(
            &resolution.bank, snapshot, &view,
            &resolution.workspace.pose_work, &resolution.workspace.output,
            &accum->walk);
    if (prepared) {
        const uint32_t count =
            (uint32_t)resolution.workspace.output.output.opaque_count +
            resolution.workspace.output.output.translucent_count;
        prepared = count <= descriptor->output_capacity;
        /* S64B-v2 seals the maximum Gouraud claim for this bank.  Reserve it
         * once per selected instance rather than decoding every primitive
         * during the meshlet walk merely to reconstruct this capacity fact.
         * S1.11: the running total lives in the uncached phase block rather
         * than in the master-owned transaction, so the phase stays correct
         * whichever CPU executes it. */
        if (prepared && resolution.bank.gouraud_tables_per_instance >
                UINT16_MAX - accum->gouraud_count)
            prepared = false;
        if (prepared)
            accum->gouraud_count += resolution.bank.gouraud_tables_per_instance;
        if (prepared) {
            *output_count = (uint16_t)count;
        }
    }
    if (!sm64_saturn_source_scene_bundle_release(
            workspace_lane, resolution.residency_generation))
        prepared = false;
    return prepared;
}

/* Master-side publish half: admit instances, build descriptors, hand the
 * queue over, then dispatch the phase. Cheap -- one bank header read per
 * instance -- and it is deliberately the only half that must precede the
 * master's own terrain/Mario preparation. */
static bool demo_generic_actor_publish(demo_render_transaction_t *transaction,
                                       uint32_t generation)
{
    sm64_saturn_actor_instance_processor_t processor;
    const sm64_saturn_actor_instance_snapshot_t *snapshots;
    uint16_t snapshot_count = 0U, descriptor_count = 0U, output_cursor = 0U;
    if (transaction == NULL || transaction->scene_snapshot == NULL ||
        transaction->actor_runtime == NULL || generation == 0U)
        return false;
    transaction->generic_actor_prepared = 0U;
    transaction->generic_actor_output_count = 0U;
    transaction->generic_actor_descriptor_count = 0U;
    transaction->generic_actor_gouraud_count = 0U;
    transaction->generic_actor_gouraud_first = 0U;
    transaction->generic_actor_gouraud_limit = 0U;
    demo_actor_phase_accum_reset();
    sm64_saturn_actor_runtime_handoff_init(&transaction->actor_handoff);
    if (transaction->scene_snapshot->actor_instance_count == 0U) {
        /* Nothing to dispatch: mark the phase prepared so the join is a no-op
         * rather than waiting on a dispatch that will never be raised. */
        transaction->generic_actor_prepared = 1U;
        return true;
    }
    if (transaction->scene_snapshot->actor_instance_bank_valid == 0U ||
        transaction->scene_snapshot->actor_instance_bank >= 2U)
        return false;
    snapshots = sm64_saturn_actor_instance_bank_ready_view(
        &transaction->actor_runtime->instances,
        transaction->scene_snapshot->actor_instance_bank, generation,
        &snapshot_count);
    if (snapshots == NULL || snapshot_count == 0U ||
        snapshot_count != transaction->scene_snapshot->actor_instance_count)
        return false;
    for (uint16_t index = 0U; index < snapshot_count; index++) {
        sm64_saturn_actor_bundle_resolution_t resolution;
        if (snapshots[index].render_active == 0U) continue;
        const uint16_t remaining = (uint16_t)(
            SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING - output_cursor);
        if (!sm64_saturn_source_scene_bundle_resolve(
                &snapshots[index], 0U,
                &transaction->actor_runtime->outputs[output_cursor], remaining,
                &resolution)) {
            /* No claim was made -- resolve failed before acquiring one --
             * so there is nothing to release. Skip this instance only; a
             * single bad bank/residency lookup must not drop the frame. */
            demo_actor_phase_accum_uncached()->instance_skipped++;
            continue;
        }
        const uint32_t capacity = resolution.bank.draw_records_per_instance;
        const bool descriptor_ok = capacity != 0U && capacity <= remaining &&
            capacity <= UINT16_MAX &&
            sm64_saturn_actor_instance_descriptor_from_snapshot(
                &snapshots[index], index, index, 0U,
                SM64_SATURN_ACTOR_OUTPUT_OPAQUE, output_cursor,
                (uint16_t)capacity,
                &transaction->actor_runtime->queue.descriptors[descriptor_count]);
        /* Release unconditionally, before the pass/fail check below, so the
         * claim is never held past this instance regardless of outcome. */
        const bool released = sm64_saturn_source_scene_bundle_release(
            0U, resolution.residency_generation);
        if (!descriptor_ok || !released) {
            /* Claim already released above. Skip this instance without
             * creating a descriptor for it -- descriptor_count/output_cursor
             * only advance for admitted instances below. */
            demo_actor_phase_accum_uncached()->instance_skipped++;
            continue;
        }
        output_cursor = (uint16_t)(output_cursor + capacity);
        descriptor_count++;
    }
    if (!sm64_saturn_actor_runtime_handoff_begin(
            &transaction->actor_handoff,
            &transaction->actor_runtime->instances,
            &transaction->actor_runtime->queue,
            transaction->scene_snapshot->actor_instance_bank, generation,
            transaction->actor_runtime->queue.descriptors, descriptor_count,
            SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE,
            SM64_SATURN_ACTOR_OUTPUT_RECORD_CEILING,
            transaction->actor_runtime->batches,
            SM64_SATURN_ACTOR_INSTANCE_MAX_LIVE))
        return false;
    /* S1.11: publish everything the phase needs into the uncached block, then
     * either dispatch it to the slave or run it here. The descriptor loop
     * above is the cheap half (one bank header read per instance); the drain
     * below is the expensive half that S0.1 attributed ~10.5% of a saturated
     * master to, and the half worth overlapping. */
    {
        demo_actor_phase_accum_t *const accum =
            demo_actor_phase_accum_uncached();
        accum->outputs = transaction->actor_runtime->outputs;
        accum->queue = &transaction->actor_runtime->queue;
        accum->camera = transaction->scene_snapshot->camera;
        accum->generation = generation;
        accum->descriptor_count = descriptor_count;
        accum->processor = (sm64_saturn_actor_instance_processor_t){
            .snapshots = transaction->actor_handoff.snapshots,
            .snapshot_count = transaction->actor_handoff.snapshot_count,
            .generation = generation,
            .process = demo_generic_actor_process,
            .context = accum,
        };
        transaction->generic_actor_descriptor_count = descriptor_count;
        demo_actor_phase_dispatch(accum);
    }
    return true;
}

/* Run the actor phase. Executes on whichever CPU claimed the dispatch; every
 * byte it touches is either uncached or lane-private, which is what makes that
 * choice free. */
static bool demo_generic_actor_run(demo_actor_phase_accum_t *accum,
                                   bool on_slave)
{
    if (accum == NULL || accum->queue == NULL) return false;
    const uint16_t drained = on_slave
        ? sm64_saturn_actor_instance_queue_drain_slave(
              accum->queue, accum->generation, &accum->processor)
        : sm64_saturn_actor_instance_queue_drain_master(
              accum->queue, accum->generation, &accum->processor);
    accum->drained = drained;
    return drained == accum->descriptor_count &&
        sm64_saturn_actor_instance_queue_all_terminal(
            accum->queue, accum->generation);
}

/* Master-side join: wait for the phase, then fold its results in. Called after
 * the master has finished the work it overlapped the phase with. */
static bool demo_generic_actor_join(demo_render_transaction_t *transaction,
                                    uint32_t generation)
{
    demo_actor_phase_accum_t *const accum = demo_actor_phase_accum_uncached();
    if (transaction == NULL || generation == 0U) return false;
    if (transaction->generic_actor_prepared != 0U) return true;
    const uint16_t descriptor_count = transaction->generic_actor_descriptor_count;
    if (!demo_actor_phase_wait(accum)) return false;
    if (accum->ok == 0U) return false;
    for (uint16_t index = 0U; index < descriptor_count; index++) {
        const sm64_saturn_actor_instance_result_t *const result =
            sm64_saturn_actor_instance_queue_result(
                accum->queue, generation, index);
        if (result == NULL || result->reason !=
                SM64_SATURN_ACTOR_QUARANTINE_NONE ||
            result->output_count > UINT16_MAX -
                transaction->generic_actor_output_count)
            return false;
        transaction->generic_actor_output_count = (uint16_t)(
            transaction->generic_actor_output_count + result->output_count);
    }
    if (!sm64_saturn_actor_runtime_handoff_finalize(
            &transaction->actor_handoff))
        return false;
    /* S1.11: the phase is complete and single-owner at this point, so folding
     * its uncached block into the master-owned transaction and frame profile
     * is a sequenced handoff. Totals are identical to the pre-S1.11 direct
     * accumulation; only the moment at which they land has moved. */
    if (accum->gouraud_count > UINT16_MAX) return false;
    transaction->generic_actor_gouraud_count = (uint16_t)accum->gouraud_count;
    sm64_saturn_actor_meshlet_stats_fold(transaction->profile, &accum->walk);
    transaction->profile->actor_instance_skipped += accum->instance_skipped;
    transaction->generic_actor_prepared = 1U;
    return true;
}

#endif

/* The existing ACTOR_ADMIT/ACTOR_LOWER descriptors own Mario's transform and
 * classify payloads in every feature mode. Generic actors are prepared before
 * graph publication and emitted after terrain/Mario assembly; retargeting
 * these callback IDs leaves the Mario jobs DONE without their required result
 * metadata and makes finalization fail closed. */
static bool demo_actor_admit_compat_wrapper(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *context)
{
    return demo_actor_queue_transform(job, claimed_state, context);
}

static bool demo_actor_lower_compat_wrapper(
    const sm64_saturn_render_job_t *job,
    sm64_saturn_render_job_state_t claimed_state, void *context)
{
    return demo_actor_queue_classify(job, claimed_state, context);
}

static const sm64_saturn_render_job_callback_table_t *
demo_render_job_callbacks(void)
{
    static const sm64_saturn_render_job_callback_table_t callbacks = {{
        demo_terrain_queue_world_admit,
        demo_terrain_queue_world_lower,
        demo_actor_admit_compat_wrapper,
        demo_actor_lower_compat_wrapper,
    }};
    return &callbacks;
}

/* The master-side merge route likewise has no fixed peer range: DONE plus the
 * descriptor index determines both payload aliases.  The caller supplies the
 * record count it collected from that job's arena; it cannot ask for more
 * than the immutable descriptor reserved. */
static bool __attribute__((unused)) demo_terrain_queue_read_done(
    uint16_t job_index, uint8_t reader_lane,
    const sm64_saturn_terrain_result_t **records, const uint8_t **commands,
    uint16_t *record_count, uint32_t *sequence)
{
    if (records != NULL) *records = NULL;
    if (commands != NULL) *commands = NULL;
    if (record_count != NULL) *record_count = 0U;
    if (sequence != NULL) *sequence = 0U;
    const sm64_saturn_render_job_t *const job =
        sm64_saturn_render_job_queue_done_job(&s_render_job_queue, job_index);
    const demo_terrain_queue_metadata_t *const metadata =
        demo_terrain_queue_result_metadata(job_index, job, reader_lane);
    if (records == NULL || commands == NULL || record_count == NULL ||
        sequence == NULL || job == NULL || metadata == NULL ||
        job->type != SM64_SATURN_RENDER_JOB_WORLD_LOWER ||
        reader_lane > SM64_SATURN_RENDER_OUTPUT_LANE_SLAVE ||
        metadata->record_count > job->output_capacity)
        return false;
    *records = sm64_saturn_render_payload_bank_read(
        &s_terrain_record_payload, &s_render_job_queue,
        &s_terrain_output_bank, &s_actor_output_bank, job_index, reader_lane);
    *commands = sm64_saturn_render_payload_bank_read(
        &s_terrain_command_payload, &s_render_job_queue,
        &s_terrain_output_bank, &s_actor_output_bank, job_index, reader_lane);
    if (*records == NULL || *commands == NULL) return false;
    *record_count = metadata->record_count;
    *sequence = metadata->sequence;
    return true;
}

/* Assemble only terminal, descriptor-owned WORLD_LOWER output.  Descriptor
 * index is the producer order for stable ties; the bounded bin pass below is
 * still master-owned, so queue work cannot change final VDP1 ordering.  This
 * route consumes the same DONE contract as Mario. */
static bool __attribute__((unused)) demo_terrain_queue_assemble_merge_spans(
    uint8_t reader_lane, demo_terrain_queue_merge_spans_t *spans)
{
    s_terrain_emit_commands_bound = 0U;
    if (spans == NULL || reader_lane != SM64_SATURN_RENDER_OUTPUT_LANE_MASTER)
        return false;
    *spans = (demo_terrain_queue_merge_spans_t){0};
    uint16_t lower_jobs[SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY];
    uint16_t lower_count = 0U;
    if (!sm64_saturn_render_job_graph_collect_done_world_lower(
            &s_render_job_graph, s_render_job_graph.generation, lower_jobs,
            SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY, &lower_count))
        return false;
    uint16_t identity_count = 0U;
    for (uint16_t lower_index = 0U; lower_index < lower_count; lower_index++) {
        const uint16_t job_index = lower_jobs[lower_index];
        const sm64_saturn_render_job_t *const descriptor =
            sm64_saturn_render_job_queue_published_job(
                &s_render_job_queue, s_render_job_graph.generation, job_index);
        if (descriptor == NULL) return false;
        const sm64_saturn_render_job_t *const job =
            sm64_saturn_render_job_queue_done_job(&s_render_job_queue,
                                                   job_index);
        if (job == NULL || job != descriptor) return false;
        const demo_terrain_queue_metadata_t *const metadata =
            demo_terrain_queue_result_metadata(job_index, job, reader_lane);
        const sm64_saturn_terrain_result_t *records;
        const uint8_t *commands;
        uint16_t record_count;
        uint32_t sequence;
        if (metadata == NULL ||
            (metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_MASTER &&
             metadata->claimed_state != SM64_SATURN_RENDER_JOB_CLAIMED_SLAVE) ||
            !demo_terrain_queue_read_done(job_index, reader_lane, &records,
                                          &commands, &record_count,
                                          &sequence) ||
            record_count != metadata->record_count ||
            sequence != metadata->sequence || sequence == 0U ||
            spans->stream_count >= SM64_SATURN_RENDER_JOB_QUEUE_CAPACITY ||
            identity_count > DEMO_TERRAIN_RESULT_CAPACITY - record_count)
            return false;
        if (spans->sequence == 0U) spans->sequence = sequence;
        if (spans->sequence != sequence) return false;
        if (spans->generation == 0U)
            spans->generation = job->snapshot_generation;
        if (spans->generation != job->snapshot_generation) return false;
        const uint16_t stream = spans->stream_count++;
        spans->records[stream] = records;
        spans->commands[stream] = commands;
        spans->counts[stream] = record_count;
        spans->job_indices[stream] = job_index;
        for (uint16_t output_index = 0U; output_index < record_count;
             output_index++)
            s_terrain_queue_merge_ids[identity_count++] =
                (sm64_saturn_render_job_result_identity_t){
                    .job_index = job_index, .output_index = output_index};
    }
    if (spans->stream_count == 0U ||
        !sm64_saturn_render_job_graph_validate_terrain_merge(
            &s_render_job_graph, spans->generation, s_terrain_queue_merge_ids,
            identity_count))
        return false;
    const size_t count = sm64_saturn_terrain_depth_bins_build_command_streams(
        spans->records, spans->commands, spans->counts, spans->stream_count,
        s_terrain_emit_refs, s_terrain_emit_scratch,
        DEMO_TERRAIN_RESULT_CAPACITY);
    if (count == SIZE_MAX) return false;
    s_terrain_emit_count = (uint16_t)count;
    s_terrain_emit_commands_bound = 1U;
    return true;
}

static bool demo_merge_terrain_results(
    const sm64_saturn_terrain_result_spans_t *spans)
{
    s_terrain_emit_commands_bound = 0U;
    /* BOB's source BSP does not split crossing polygons, so tree traversal is
     * not a complete painter order. Z-Treme retains per-polygon SORT_MAX/MIN
     * depth policy, and SlaveDriver sorts visible leaves by distance and cut
     * planes before dispatch (WALLS.C:1986-2052, 2180-2232). Keep spatial
     * admission from the bake, but reduce the final order to a stable
     * far-to-near fixed-bin stream.  The master alone performs this join;
     * both workers published only immutable, disjoint result ranges.
     *
     * The portable merger validates both lane publication sequences before it
     * exposes any record or private command image to the master. */
    const size_t count = sm64_saturn_terrain_depth_bins_visible(
        spans, s_terrain_publish_sequence, s_terrain_emit_refs,
        s_terrain_emit_scratch, DEMO_TERRAIN_RESULT_CAPACITY);
    if (count == SIZE_MAX) {
        s_terrain_emit_count = 0U;
        return false;
    }
    s_terrain_emit_count = (uint16_t)count;
    return true;
}

/* Queue refs retain their exact descriptor-local command image through the
 * master-owned sort. Legacy refs predate that paired stream and continue to
 * resolve through the accepted two-arena helper until the atomic cutover. */
static const uint8_t *demo_terrain_final_command(
    const sm64_saturn_terrain_result_spans_t *legacy_spans,
    const sm64_saturn_terrain_emit_ref_t *ref)
{
    if (ref == NULL) return NULL;
    return s_terrain_emit_commands_bound != 0U ? ref->command :
        sm64_saturn_terrain_emit_ref_command(legacy_spans, ref);
}

static void __attribute__((unused)) demo_emit_primitive(
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const vdp1_vram_partitions_t *partitions)
{
    int16_vec2_t vertices[4];
    demo_primitive_screen_vertices(primitive, vertices);
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        /* Castleviewer and the shared bake path lower textured triangles as
         * repeated-C A/B/C/C sprites. True source quads retain their fourth
         * corner; flat triangles use the same repeated-C convention. */
        primitive->source1 == 0xFFFFU
            ? vertices[2] : vertices[3]
    };
    const int32_t cross = (int32_t)(vertices[1].x - vertices[0].x) *
                              (vertices[2].y - vertices[0].y) -
                          (int32_t)(vertices[1].y - vertices[0].y) *
                              (vertices[2].x - vertices[0].x);
    if (cross == 0) {
        profile->reject_degenerate++;
        return;
    }

    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
    if (cmdt == NULL) {
        profile->reject_vdp1_arena_capacity++;
        profile->pipeline_faults++;
        return;
    }
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (primitive->textured != 0U && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        s_primitive_recovery[primitive - s_bob_primitives_active] == 0U) {
        const bool bound = sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices);
        if (bound) {
            profile->texture_commands++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
    }
    sm64_saturn_gouraud_table_t *table = NULL;
    uintptr_t gouraud_address = 0;
    if (primitive->textured == 0U) {
        table = sm64_saturn_gouraud_bank_alloc(gouraud_bank,
                                                &gouraud_address);
    }
    if (table != NULL) {
        const rgb1555_t color = RGB1555(1, primitive->rgb[0],
                                        primitive->rgb[1], primitive->rgb[2]);
        table->colors[0] = color.raw;
        table->colors[1] = color.raw;
        table->colors[2] = color.raw;
        table->colors[3] = color.raw;
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_GOURAUD});
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = sm64_saturn_gouraud_neutral_color()});
        vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)gouraud_address);
    } else {
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                          primitive->rgb[1], primitive->rgb[2]));
        if (primitive->textured == 0U) profile->gouraud_bank_overflow++;
    }
    profile->triangles_vdp1_emitted++;
    profile->triangles_emitted++;
}

static void demo_resolve_terrain_command_templates(
    const vdp1_vram_partitions_t *partitions)
{
    if (s_bob_terrain_templates_enabled == 0U ||
        s_bob_terrain_templates_resolved != 0U || partitions == NULL)
        return;

    const int16_vec2_t placeholder_vertices[4] = {
        INT16_VEC2_INITIALIZER(0, 0), INT16_VEC2_INITIALIZER(0, 0),
        INT16_VEC2_INITIALIZER(0, 0), INT16_VEC2_INITIALIZER(0, 0)};
    for (uint16_t primitive_index = 0U;
         primitive_index < SM64_SATURN_BOB_PRIMITIVE_COUNT;
         primitive_index++) {
        sm64_saturn_terrain_command_template_t metadata;
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[primitive_index];
        if (!sm64_saturn_terrain_template_build_from_bob(
                &metadata, primitive->textured != 0U, primitive->rgb,
                primitive->tile_offset)) {
            continue;
        }
        for (uint8_t variant = DEMO_TERRAIN_TEMPLATE_BASE;
             variant < DEMO_TERRAIN_TEMPLATE_VARIANT_COUNT; variant++) {
            const bool force_gouraud = SATURN_DEMO_TERRAIN_GOURAUD &&
                variant == DEMO_TERRAIN_TEMPLATE_RECOVERY;
            sm64_saturn_shade_path_t variant_shade_path =
                variant == DEMO_TERRAIN_TEMPLATE_TEXTURE_SUPPRESSED
                    ? SM64_SATURN_SHADE_FLAT_REPLACE
                    : (sm64_saturn_shade_path_t)metadata.shade_path;
#if !SATURN_DEMO_TERRAIN_GOURAUD
            /* The recovery variant exists to share the static UNTEXTURED
             * material for clipped quads, and reached it via force_gouraud
             * above. With Gouraud gone it must be sent to the flat branch
             * explicitly -- leaving it on metadata.shade_path would try to
             * bind a texture for a record whose texture bind already failed. */
            if (variant == DEMO_TERRAIN_TEMPLATE_RECOVERY ||
                variant_shade_path == SM64_SATURN_SHADE_GOURAUD)
                variant_shade_path = SM64_SATURN_SHADE_FLAT_REPLACE;
#endif
            bool valid = true;
            vdp1_cmdt_t command;
            memset(&command, 0, sizeof(command));
            if (force_gouraud) {
                /* Clipped recovery and texture-suppressed LOD share the
                 * static untextured material; their colours/GRDA remain
                 * patchable frame data. */
                vdp1_cmdt_polygon_set(&command);
                vdp1_cmdt_draw_mode_set(&command, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_GOURAUD});
                vdp1_cmdt_color_set(&command, (rgb1555_t){
                    .raw = sm64_saturn_gouraud_neutral_color()});
            } else switch (variant_shade_path) {
            case SM64_SATURN_SHADE_FLAT_REPLACE:
                vdp1_cmdt_polygon_set(&command);
                vdp1_cmdt_draw_mode_set(&command, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_REPLACE});
                vdp1_cmdt_color_set(&command, (rgb1555_t){
                    .raw = (uint16_t)(metadata.color | 0x8000U)});
                break;
            case SM64_SATURN_SHADE_GOURAUD:
                vdp1_cmdt_polygon_set(&command);
                vdp1_cmdt_draw_mode_set(&command, (vdp1_cmdt_draw_mode_t){
                    .color_mode = VDP1_CMDT_CM_RGB_32768,
                    .cc_mode = VDP1_CMDT_CC_GOURAUD});
                vdp1_cmdt_color_set(&command, (rgb1555_t){
                    .raw = sm64_saturn_gouraud_neutral_color()});
                break;
            case SM64_SATURN_SHADE_TEXTURED:
                valid = !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
                    sm64_saturn_ir_texture_bind_clut16(
                        &command, partitions, primitive->tile_offset,
                        primitive->tile_size, primitive->tile_size,
                        (uint16_t)(primitive->clut_offset /
                                   sizeof(vdp1_clut_t)),
                        VDP1_CMDT_CC_REPLACE, placeholder_vertices);
                break;
            default:
                valid = false;
                break;
            }
            demo_terrain_template_valid_set(
                primitive_index, (demo_terrain_template_variant_t)variant,
                valid);
            if (valid) {
                sm64_saturn_terrain_resolved_command_t *const resolved =
                    &s_bob_terrain_resolved_templates[primitive_index][variant];
                vdp1_cmdt_end_clear(&command);
                command.cmd_link = 0U;
                memcpy(resolved->words, &command, sizeof(resolved->words));
                resolved->patch_mask = SM64_SATURN_TERRAIN_PATCH_END |
                    SM64_SATURN_TERRAIN_PATCH_LINK |
                    SM64_SATURN_TERRAIN_PATCH_XY |
                    ((force_gouraud || variant_shade_path ==
                        SM64_SATURN_SHADE_GOURAUD)
                        ? SM64_SATURN_TERRAIN_PATCH_GOURAUD : 0U);
            }
        }
    }
    s_bob_terrain_templates_resolved = 1U;
}

static void demo_emit_terrain_result(
    const sm64_saturn_terrain_result_t *result,
    const uint8_t command[SM64_SATURN_TERRAIN_COMMAND_BYTES],
    const sm64_saturn_bob_primitive_t *primitive,
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const vdp1_vram_partitions_t *partitions)
{
    if (result == NULL || command == NULL || primitive == NULL ||
        !sm64_saturn_terrain_result_validate(result))
        return;
    vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1U);
    if (cmdt == NULL) {
        profile->reject_vdp1_arena_capacity++;
        profile->pipeline_faults++;
        return;
    }
    const uint16_t painter_bin =
        sm64_saturn_terrain_depth_bin(result->painter_key);
    const bool recovery = sm64_saturn_terrain_result_recovery(result);
    const bool texture_suppressed =
        sm64_saturn_terrain_result_texture_suppressed(result);
    const uint16_t shade = (uint16_t)(
        ((uint16_t)primitive->rgb[0] << 10) |
        ((uint16_t)primitive->rgb[1] << 5) | primitive->rgb[2]);
    uint16_t gouraud_colors[4] = {shade, shade, shade, shade};
#if SATURN_DEMO_TERRAIN_GOURAUD
    if (sm64_saturn_terrain_result_has_post_light_shades(result))
        memcpy(gouraud_colors, command, sizeof(gouraud_colors));
#endif
    /* The worker made this decision while it still owned classification.
     * The master reads only the compact tag: it remains the sole owner of
     * the finite Gouraud bank, command list, and final draw order. */
    sm64_saturn_shade_path_t shade_path =
        sm64_saturn_terrain_shade_path_from_compact_flags(result->clip_class);
#if !SATURN_DEMO_TERRAIN_GOURAUD
    /* Must match how demo_resolve_terrain_command_templates() baked this
     * primitive's template, or the GRDA patch is skipped on a command that
     * still carries CC_GOURAUD. Both are driven by the same switch. */
    if (shade_path == SM64_SATURN_SHADE_GOURAUD)
        shade_path = SM64_SATURN_SHADE_FLAT_REPLACE;
#endif
    const bool textured = shade_path == SM64_SATURN_SHADE_TEXTURED_REPLACE;
    const sm64_saturn_terrain_resolved_command_t *const resolved =
        demo_terrain_resolved_template(result->primitive_id, recovery,
                                       texture_suppressed);
    if (sm64_saturn_terrain_result_template_patched(result) &&
        resolved != NULL) {
        int16_t vertices[4][2];
        memcpy(vertices, command + 12U, sizeof(vertices));
        sm64_saturn_terrain_gouraud_lowering_t gouraud_lowering;
        (void)sm64_saturn_terrain_lower_gouraud(
            gouraud_bank, shade_path, gouraud_colors, &gouraud_lowering);
        sm64_saturn_gouraud_table_t *table = gouraud_lowering.table;
        const uintptr_t gouraud_address = gouraud_lowering.address;
        if (!sm64_saturn_terrain_template_patch_resolved_record_ex(
                cmdt, resolved, vertices, 0U, false,
                gouraud_lowering.patch,
                gouraud_address, false, 0U)) {
            /* A poisoned/malformed template is the only supported reason to
             * reconstruct command state at frame time; the common fallback
             * tail below records it exactly once. */
        } else if (textured) {
            cmdt->cmd_link = painter_bin;
            profile->texture_commands++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        } else if (shade_path == SM64_SATURN_SHADE_FLAT_REPLACE) {
            cmdt->cmd_link = painter_bin;
            sm64_saturn_gouraud_bank_note_saved(gouraud_bank);
            profile->flat_primitives++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        } else if (table != NULL) {
            cmdt->cmd_link = painter_bin;
            profile->gouraud_primitives++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        } else {
            /* The template is still copied/patches correctly; only the
             * master-owned finite Gouraud resource degraded this draw. */
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(cmdt, RGB1555(
                1, primitive->rgb[0], primitive->rgb[1], primitive->rgb[2]));
            cmdt->cmd_link = painter_bin;
            profile->gouraud_bank_overflow++;
            profile->pipeline_faults++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
    }

    /* Per-entry fallback is deliberately narrow: malformed worker records,
     * genuinely dynamic texture-source state, or a rejected static template.
     * Recovery and texture-suppressed BOB records use their load-time
     * variants above and therefore must not reach this path. */
    profile->demo_bob_terrain_legacy_fallbacks++;
    int16_vec2_t shape_vertices[4];
    memcpy(shape_vertices, command + 12U, sizeof(shape_vertices));
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (textured && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        !recovery) {
        const bool bound = sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices);
        if (bound) {
            cmdt->cmd_link = painter_bin;
            profile->texture_commands++;
            profile->triangles_vdp1_emitted++;
            profile->triangles_emitted++;
            return;
        }
    }
    if (shade_path == SM64_SATURN_SHADE_FLAT_REPLACE) {
        sm64_saturn_gouraud_bank_note_saved(gouraud_bank);
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = (uint16_t)(shade | 0x8000U)});
        profile->flat_primitives++;
    } else {
        sm64_saturn_terrain_gouraud_lowering_t gouraud_lowering;
        (void)sm64_saturn_terrain_lower_gouraud(
            gouraud_bank, shade_path, gouraud_colors, &gouraud_lowering);
        sm64_saturn_gouraud_table_t *table = gouraud_lowering.table;
        const uintptr_t gouraud_address = gouraud_lowering.address;
        if (table != NULL) {
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()});
            vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)gouraud_address);
            profile->gouraud_primitives++;
        } else {
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                              primitive->rgb[1], primitive->rgb[2]));
            if (!textured || recovery) {
                profile->gouraud_bank_overflow++;
                profile->pipeline_faults++;
            }
        }
    }
    cmdt->cmd_link = painter_bin;
    profile->triangles_vdp1_emitted++;
    profile->triangles_emitted++;
}

#if SATURN_SLAVE_RENDER
/* Direct-slot variant used by the dual-worker path. The caller has already
 * reserved and assigned the command slot in source draw order, so this
 * routine performs no shared arena mutation. The slave deliberately uses
 * flat RGB1555 for untextured primitives: Gouraud-table allocation remains a
 * master-owned resource, while textured setup is entirely disjoint. */
static void __attribute__((unused)) demo_emit_primitive_at(
    const sm64_saturn_bob_primitive_t *primitive,
    vdp1_cmdt_t *cmdt,
    const vdp1_vram_partitions_t *partitions,
    sm64_saturn_gouraud_table_t *gouraud_table,
    uintptr_t gouraud_address,
    demo_emit_stats_t *stats)
{
    int16_vec2_t vertices[4];
    demo_primitive_screen_vertices(primitive, vertices);
    const int16_vec2_t shape_vertices[4] = {
        vertices[0], vertices[1], vertices[2],
        primitive->source1 == 0xFFFFU
            ? vertices[2] : vertices[3]
    };
    vdp1_cmdt_polygon_set(cmdt);
    vdp1_cmdt_vtx_set(cmdt, shape_vertices);
    if (primitive->textured != 0U && !SATURN_DEMO_BSP_FRAGMENT_FLAT &&
        s_primitive_recovery[primitive - s_bob_primitives_active] == 0U &&
        sm64_saturn_ir_texture_bind_clut16(
            cmdt, partitions, primitive->tile_offset, primitive->tile_size,
            primitive->tile_size,
            (uint16_t)(primitive->clut_offset / sizeof(vdp1_clut_t)),
            VDP1_CMDT_CC_REPLACE, shape_vertices)) {
        stats->texture_commands++;
        stats->triangles_emitted++;
        return;
    }
    if (gouraud_table != NULL) {
        const rgb1555_t color = RGB1555(1, primitive->rgb[0],
                                        primitive->rgb[1], primitive->rgb[2]);
        gouraud_table->colors[0] = color.raw;
        gouraud_table->colors[1] = color.raw;
        gouraud_table->colors[2] = color.raw;
        gouraud_table->colors[3] = color.raw;
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_GOURAUD});
        vdp1_cmdt_color_set(cmdt, (rgb1555_t){
            .raw = sm64_saturn_gouraud_neutral_color()});
        vdp1_cmdt_gouraud_base_set(cmdt, (vdp1_vram_t)gouraud_address);
    } else {
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, RGB1555(1, primitive->rgb[0],
                                          primitive->rgb[1], primitive->rgb[2]));
    }
    if (primitive->textured == 0U && gouraud_table == NULL)
        stats->gouraud_bank_overflow++;
    stats->triangles_emitted++;
}

static void __attribute__((unused)) demo_emit_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_emit_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t ordinal = begin; ordinal < end; ordinal++) {
        if (((uint16_t)(ordinal - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const uint16_t i = s_render_work_order[ordinal];
        if (s_primitive_visible[i] == 0U) continue;
        demo_emit_primitive_at(&context->primitives[i],
                               &context->cmdts[s_primitive_slots[i]],
                               context->partitions,
                               context->gouraud_tables[i],
                               context->gouraud_addresses[i],
                               &context->stats[lane]);
    }
}

#endif /* SATURN_SLAVE_RENDER */

static void __attribute__((unused)) demo_emit_mario_range(void *opaque, uint16_t begin, uint16_t end)
{
    demo_emit_context_t *context = opaque;
    const uint8_t lane = begin == 0U ? 0U : 1U;
    for (uint16_t ordinal = begin; ordinal < end; ordinal++) {
        if (((uint16_t)(ordinal - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        const sm64_saturn_actor_draw_ref_t *const ref =
            demo_actor_draw_ref_from_order(s_actor_draw_order[ordinal]);
        if (ref == NULL) continue;
        const uint16_t primitive = ref->primitive_id;
        const uint16_t *indices = sm64_mario_primitives[primitive];
        const int16_vec2_t vertices[4] = {
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(indices[1])->x,
                                   demo_actor_projected_read(indices[1])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(indices[2])->x,
                                   demo_actor_projected_read(indices[2])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(indices[3])->x,
                                   demo_actor_projected_read(indices[3])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(indices[4])->x,
                                   demo_actor_projected_read(indices[4])->y)};
        vdp1_cmdt_t *cmdt = &context->cmdts[s_actor_slots[ordinal]];
        vdp1_cmdt_polygon_set(cmdt);
        const uint8_t *rgb = sm64_mario_material_rgb[indices[0]];
        sm64_saturn_gouraud_table_t *table = s_actor_gouraud[ordinal];
        if (table != NULL) {
            const uint16_t corners[4] = {indices[1], indices[2], indices[3],
                                         indices[4]};
            for (uint8_t corner = 0; corner < 4U; corner++) {
                const uint8_t intensity =
                    s_mario_transform_context.light_intensity[corners[corner]];
                const uint8_t r = (uint8_t)((rgb[0] * intensity) / 31U);
                const uint8_t g = (uint8_t)((rgb[1] * intensity) / 31U);
                const uint8_t b = (uint8_t)((rgb[2] * intensity) / 31U);
                table->colors[corner] = RGB1555(
                    1, r > 31U ? 31U : r, g > 31U ? 31U : g,
                    b > 31U ? 31U : b).raw;
            }
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_GOURAUD});
            vdp1_cmdt_color_set(cmdt, (rgb1555_t){
                .raw = sm64_saturn_gouraud_neutral_color()});
            vdp1_cmdt_gouraud_base_set(
                cmdt, (vdp1_vram_t)s_actor_gouraud_addresses[ordinal]);
        } else {
            vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
                .color_mode = VDP1_CMDT_CM_RGB_32768,
                .cc_mode = VDP1_CMDT_CC_REPLACE});
            vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
        }
        vdp1_cmdt_vtx_set(cmdt, vertices);
        cmdt->cmd_link = (uint16_t)(ref->sort_key >> 16);
        const uint16_t texture_start = sm64_mario_texture_tile_start[primitive];
        if (texture_start != SM64_MARIO_TEXTURE_TILE_NONE &&
            context->partitions != NULL) {
            const uint16_t *texture_indices =
                sm64_mario_textured_source_vertices[texture_start / 4U];
            const int16_vec2_t texture_vertices[4] = {
                INT16_VEC2_INITIALIZER(demo_actor_projected_read(texture_indices[0])->x,
                                       demo_actor_projected_read(texture_indices[0])->y),
                INT16_VEC2_INITIALIZER(demo_actor_projected_read(texture_indices[1])->x,
                                       demo_actor_projected_read(texture_indices[1])->y),
                INT16_VEC2_INITIALIZER(demo_actor_projected_read(texture_indices[2])->x,
                                       demo_actor_projected_read(texture_indices[2])->y),
                INT16_VEC2_INITIALIZER(demo_actor_projected_read(texture_indices[2])->x,
                                        demo_actor_projected_read(texture_indices[2])->y)};
            vdp1_cmdt_t *detail = &context->cmdts[s_actor_texture_slots[ordinal]];
            if (sm64_saturn_ir_texture_bind_rgb1555(
                    detail, context->partitions,
                    SATURN_MARIO_TEXTURE_BASE_OFFSET +
                        (texture_start / 4U) *
                        (SM64_MARIO_TEXTURE_UV_TILE_WIDTH *
                         SM64_MARIO_TEXTURE_UV_TILE_WIDTH * sizeof(uint16_t)),
                    SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                    SM64_MARIO_TEXTURE_UV_TILE_WIDTH,
                    VDP1_CMDT_CC_REPLACE, texture_vertices)) {
                detail->cmd_link = (uint16_t)(ref->sort_key >> 16);
            }
        }
        context->stats[lane].triangles_emitted++;
    }
}

#if SATURN_SLAVE_RENDER && defined(SM64_SATURN_VDP1_LWRAM_STAGING)
typedef struct demo_vdp1_upload_context {
    volatile const uint32_t *source;
    volatile uint32_t *destination;
} demo_vdp1_upload_context_t;

static void demo_upload_vdp1_range(void *opaque, uint16_t begin,
                                   uint16_t end)
{
    demo_vdp1_upload_context_t *context = opaque;
    for (uint16_t i = begin; i < end; i++) {
        if (((uint16_t)(i - begin) % DEMO_CANCEL_POLL_INTERVAL) == 0U &&
            sm64_saturn_dual_worker_cancelled())
            break;
        context->destination[i] = context->source[i];
    }
}

static void demo_upload_vdp1_serial(sm64_saturn_vdp1_backend_t *backend)
{
    volatile const uint32_t *source = (volatile const uint32_t *)
        ((uintptr_t)backend->list.cmdts & ~CPU_ADDRESS_PARTITION_MASK);
    volatile uint32_t *destination = (volatile uint32_t *)VDP1_VRAM(0);
    uint32_t words = (uint32_t)backend->list.count *
                     (sizeof(vdp1_cmdt_t) / sizeof(uint32_t));
    while (words-- > 0U) *destination++ = *source++;
}

static void demo_upload_vdp1_dual(sm64_saturn_vdp1_backend_t *backend,
                                  sm64_saturn_dual_worker_stats_t *stats)
{
    vdp1_sync_wait();
    assert(!vdp1_sync_busy());
    demo_vdp1_upload_context_t context = {
        .source = (volatile const uint32_t *)
            ((uintptr_t)backend->list.cmdts & ~CPU_ADDRESS_PARTITION_MASK),
        .destination = (volatile uint32_t *)VDP1_VRAM(0)};
    const uint32_t words = (uint32_t)backend->list.count *
                           (sizeof(vdp1_cmdt_t) / sizeof(uint32_t));
    const bool completed = sm64_saturn_dual_worker_run(
        demo_upload_vdp1_range, &context, (uint16_t)words,
        (uint16_t)(words / 2U), stats);
    if (!completed) demo_upload_vdp1_serial(backend);
    vdp1_sync_force_put();
}
#endif

/* Drop this CPU's own stale lines over a range the other CPU wrote.
 *
 * The SH7604 data cache is write-through with no write-back mode and no dirty
 * bit, so the producer's ordinary cached stores are already in memory the
 * moment they retire; nothing needs flushing, and no flush primitive exists.
 * The only cross-CPU hazard is the consumer still holding lines it cached in
 * an earlier frame at these same addresses. cpu_cache_area_purge invalidates
 * and never writes back, so it is always safe here -- including on the
 * partial lines at either end, which can only be re-read from memory. */
static void demo_mario_phase_purge(const void *base, uint32_t bytes)
{
#if DEMO_MARIO_PHASE_SLAVE
    if (base == NULL || bytes == 0U) return;
    const uintptr_t start =
        (uintptr_t)base & ~(uintptr_t)(CPU_CACHE_LINE_SIZE - 1U);
    const uint32_t span = (uint32_t)(((uintptr_t)base + bytes) - start);
    cpu_cache_area_purge(
        (void *)start,
        (span + (CPU_CACHE_LINE_SIZE - 1U)) &
            ~(uint32_t)(CPU_CACHE_LINE_SIZE - 1U));
#else
    (void)base;
    (void)bytes;
#endif
}

/* The phase body. Runs on whichever CPU claimed the dispatch. Every input it
 * reads is either a by-value copy inside the uncached block or a range purged
 * immediately before the read; every output it writes is an LWRAM buffer the
 * master purges at the join. */
static bool demo_mario_phase_run(demo_actor_phase_accum_t *accum)
{
    if (accum == NULL) return false;
    const sm64_saturn_mario_actor_pose_t *const pose = &accum->mario_pose;
    /* The master re-evaluates the pose into the hot arena every frame, and an
     * alternating-buffer arena puts it at the same address every other frame,
     * so this CPU can be holding stale lines over exactly these bytes. */
    demo_mario_phase_purge(pose->vertices,
                           (uint32_t)pose->vertex_count * 3U *
                               (uint32_t)sizeof(int16_t));
    demo_mario_phase_purge(pose->light_intensity,
                           (uint32_t)pose->vertex_count);
    sm64_saturn_actor_meshlet_output_t output = {
        .opaque = s_actor_opaque_refs,
        .translucent = s_actor_translucent_refs,
        .positions = s_actor_transform_refs,
        .position_capacity = SM64_MARIO_VERTEX_COUNT,
    };
    if (!sm64_saturn_actor_meshlets_prepare_stats(
            &accum->mario_snapshot, pose, &accum->mario_view, &output,
            SM64_MARIO_PRIMITIVE_COUNT, &accum->mario_walk))
        return false;
    uint16_t draw_count = 0U;
    for (uint8_t pass = 0U; pass < 2U; pass++) {
        const sm64_saturn_actor_draw_ref_t *const refs =
            pass == 0U ? output.opaque : output.translucent;
        const uint16_t count = pass == 0U ? output.opaque_count
                                          : output.translucent_count;
        for (uint16_t i = 0U; i < count; i++) {
            if (refs[i].primitive_id >= SM64_MARIO_PRIMITIVE_COUNT)
                return false;
            s_actor_draw_order[draw_count++] =
                (uint16_t)(i | (pass == 0U ? 0U :
                                  DEMO_ACTOR_DRAW_REF_TRANSLUCENT));
        }
    }
    /* Counts are published only on success, so a failed walk cannot leave the
     * master reading a half-filled draw order. */
    accum->mario_draw_count = draw_count;
    accum->mario_transform_ref_count = output.position_count;
    return true;
}

/* Master half: copy every input into the uncached block and raise the phase.
 * Nothing here may alias master frame state that keeps changing while the
 * slave walks, which is why the snapshot, pose and view are copied by value.
 * `pose` still carries pointers; demo_mario_phase_run owns that hazard. */
static bool demo_mario_phase_publish(
    demo_actor_phase_accum_t *accum,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_ir_transform_job_t *job)
{
    if (accum == NULL) return false;
    accum->mario_pending = 0U;
    accum->mario_done = 0U;
    accum->mario_ok = 0U;
    accum->mario_draw_count = 0U;
    accum->mario_transform_ref_count = 0U;
    s_actor_draw_count = 0U;
    s_actor_transform_ref_count = 0U;
    s_actor_texture_count = 0U;
    s_actor_command_count = 0U;
    if (snapshot == NULL || pose == NULL || job == NULL || !snapshot->valid ||
        pose->vertices == NULL ||
        pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return false;

    /* T2.5 sub-stage boundary: the setup half of the largest measured block in
     * the port. The walk itself is no longer bracketed here because it no
     * longer runs on this CPU. */
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_MARIO_SETUP);
    memset((void *)&accum->mario_walk, 0, sizeof(accum->mario_walk));
    const uint32_t meshlet_generation = s_actor_publish_sequence == UINT32_MAX
        ? 1U : s_actor_publish_sequence + 1U;
    memset((void *)&accum->mario_snapshot, 0, sizeof(accum->mario_snapshot));
    accum->mario_snapshot.generation = meshlet_generation;
    accum->mario_snapshot.actor_generation = meshlet_generation;
    accum->mario_snapshot.mario = *snapshot;
    accum->mario_pose = *pose;
    memset((void *)&accum->mario_view, 0, sizeof(accum->mario_view));
    accum->mario_view.camera_position_q16[0] =
        (int32_t)((int64_t)job->camera.position.x * (1 << 16));
    accum->mario_view.camera_position_q16[1] =
        (int32_t)((int64_t)job->camera.position.y * (1 << 16));
    accum->mario_view.camera_position_q16[2] =
        (int32_t)((int64_t)job->camera.position.z * (1 << 16));
    accum->mario_view.view_forward_q16[0] = job->camera.forward.x;
    accum->mario_view.view_forward_q16[1] = job->camera.forward.y;
    accum->mario_view.view_forward_q16[2] = job->camera.forward.z;
    accum->mario_view.generation = meshlet_generation;
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
#if DEMO_MARIO_PHASE_SLAVE
    /* Raised, not dispatched. The generic-actor publish that follows owns the
     * single wake-up, and the slave task claims both phases from it -- see
     * demo_actor_phase_slave_task. A second notify while the slave is still
     * inside the first is not guaranteed to be observed. */
    demo_render_fence();
    accum->mario_pending = 1U;
    demo_render_fence();
#else
    accum->mario_ok = demo_mario_phase_run(accum) ? 1U : 0U;
    demo_render_fence();
    accum->mario_done = 1U;
#endif
    return true;
}

/* Master half of the join. Called after the master has finished the work it
 * overlapped the phase with. */
static bool demo_mario_phase_join(demo_actor_phase_accum_t *accum,
                                  sm64_saturn_fast3d_profile_t *profile,
                                  uint16_t *vertex_count_out)
{
    if (vertex_count_out == NULL || profile == NULL || accum == NULL)
        return false;
    *vertex_count_out = 0U;
#if DEMO_MARIO_PHASE_SLAVE
    {
        uint32_t spins = 0U;
        while (accum->mario_done == 0U) {
            if (++spins > DEMO_ACTOR_PHASE_WAIT_LIMIT) return false;
        }
        demo_render_fence();
    }
    /* The slave wrote these four LWRAM buffers through its own cache, which
     * put the bytes in memory. What the master must drop is its own lines,
     * cached when it read the same buffers on the previous frame. */
    demo_mario_phase_purge(s_actor_draw_order, sizeof(s_actor_draw_order));
    demo_mario_phase_purge(s_actor_transform_refs,
                           sizeof(s_actor_transform_refs));
    demo_mario_phase_purge(s_actor_opaque_refs, sizeof(s_actor_opaque_refs));
    demo_mario_phase_purge(s_actor_translucent_refs,
                           sizeof(s_actor_translucent_refs));
#endif
    if (accum->mario_done == 0U || accum->mario_ok == 0U) {
        profile->pipeline_faults++;
        return false;
    }
    sm64_saturn_actor_meshlet_stats_fold(profile, &accum->mario_walk);
    s_actor_draw_count = accum->mario_draw_count;
    s_actor_transform_ref_count = accum->mario_transform_ref_count;
    *vertex_count_out = s_actor_transform_ref_count;
    return true;
}

static uint16_t demo_finalize_mario_draws(void)
{
    const uint16_t candidate_count = s_actor_draw_count;
    s_actor_draw_count = 0U;
    s_actor_texture_count = 0U;
    for (uint16_t i = 0U; i < candidate_count; i++) {
        const uint16_t order = s_actor_draw_order[i];
        const sm64_saturn_actor_draw_ref_t *const ref =
            demo_actor_draw_ref_from_order(order);
        if (ref == NULL) continue;
        const uint16_t primitive_id = ref->primitive_id;
        if (demo_actor_ref_read(primitive_id)->primitive_id ==
            DEMO_ACTOR_PRIMITIVE_REJECTED)
            continue;
        s_actor_draw_order[s_actor_draw_count++] = order;
#if defined(SATURN_DEMO_MARIO_TEXTURES)
        if (sm64_mario_texture_tile_start[primitive_id] !=
            SM64_MARIO_TEXTURE_TILE_NONE)
            s_actor_texture_count++;
#endif
    }
    s_actor_command_count = (uint16_t)(s_actor_draw_count + s_actor_texture_count);
    return s_actor_command_count;
}

static void demo_reserve_mario_gouraud(
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile)
{
    memset(s_actor_gouraud, 0, sizeof(s_actor_gouraud));
    memset(s_actor_gouraud_addresses, 0, sizeof(s_actor_gouraud_addresses));
#if defined(SATURN_DEMO_MARIO_TEXTURES)
    const uint32_t required_tables = s_actor_draw_count;
    if (gouraud_bank == NULL ||
        required_tables > (uint32_t)gouraud_bank->capacity -
                              (uint32_t)gouraud_bank->used) {
        profile->gouraud_bank_overflow += required_tables;
        profile->pipeline_faults++;
        return;
    }
    for (uint16_t i = 0; i < s_actor_draw_count; i++) {
        s_actor_gouraud[i] = sm64_saturn_gouraud_bank_alloc(
            gouraud_bank, &s_actor_gouraud_addresses[i]);
    }
#else
    (void)gouraud_bank;
    (void)profile;
#endif
}

static void demo_emit_mario(
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    sm64_saturn_vdp1_backend_t *backend,
    const vdp1_vram_partitions_t *partitions,
    sm64_saturn_fast3d_profile_t *profile)
{
    if (snapshot == NULL || pose == NULL || !snapshot->valid ||
        pose->vertices == NULL || pose->vertex_count != SM64_MARIO_VERTEX_COUNT)
        return;
    for (uint16_t i = 0; i < SM64_MARIO_VERTEX_COUNT; i++)
        if (demo_actor_valid_read(i)) profile->demo_actor_vertices_valid++;
#if defined(SATURN_DEMO_MARIO_TEXTURES)
    if (s_actor_draw_count != 0U &&
        sm64_saturn_vdp1_backend_reserve(
            backend, s_actor_command_count) != NULL) {
        uint16_t command_slot = (uint16_t)(backend->commands.cursor -
                                           s_actor_command_count);
        for (uint16_t i = 0; i < s_actor_draw_count; i++) {
            s_actor_slots[i] = command_slot++;
            const sm64_saturn_actor_draw_ref_t *const ref =
                demo_actor_draw_ref_from_order(s_actor_draw_order[i]);
            if (ref == NULL) return;
            if (sm64_mario_texture_tile_start[ref->primitive_id] !=
                SM64_MARIO_TEXTURE_TILE_NONE)
                s_actor_texture_slots[i] = command_slot++;
        }
        /* Mario stays master-owned.  The old experiment dispatched this
         * actor setup through the slave, which made the textured/Gouraud path
         * depend on a second command producer and left the master fallback as
         * flat RGB polygons.  Lower the already-sorted actor range directly on
         * the master so visual fidelity does not depend on the terrain worker. */
        demo_emit_context_t actor_emit = {
            .primitives = NULL,
            .cmdts = backend->list.cmdts,
            .partitions = partitions,
            .gouraud_tables = s_actor_gouraud,
            .gouraud_addresses = s_actor_gouraud_addresses,
            .stats = {{0U, 0U, 0U}, {0U, 0U, 0U}}
        };
        demo_emit_mario_range(&actor_emit, 0U, s_actor_draw_count);
        profile->triangles_vdp1_emitted +=
            actor_emit.stats[0].triangles_emitted;
        profile->triangles_emitted += actor_emit.stats[0].triangles_emitted;
        profile->demo_actor_primitives_emitted += s_actor_draw_count;
        return;
    }
#endif
    for (uint16_t i = 0; i < s_actor_draw_count; i++) {
        const sm64_saturn_actor_draw_ref_t *const ref =
            demo_actor_draw_ref_from_order(s_actor_draw_order[i]);
        if (ref == NULL) continue;
        const uint16_t *primitive = sm64_mario_primitives[ref->primitive_id];
        const int16_vec2_t vertices[4] = {
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(primitive[1])->x,
                                   demo_actor_projected_read(primitive[1])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(primitive[2])->x,
                                   demo_actor_projected_read(primitive[2])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(primitive[3])->x,
                                   demo_actor_projected_read(primitive[3])->y),
            INT16_VEC2_INITIALIZER(demo_actor_projected_read(primitive[4])->x,
                                   demo_actor_projected_read(primitive[4])->y)};
        const int32_t cross = (int32_t)(vertices[1].x - vertices[0].x) *
                                  (vertices[2].y - vertices[0].y) -
                              (int32_t)(vertices[1].y - vertices[0].y) *
                                  (vertices[2].x - vertices[0].x);
        if (cross == 0) continue;
        vdp1_cmdt_t *cmdt = sm64_saturn_vdp1_backend_reserve(backend, 1);
        if (cmdt == NULL) {
            profile->reject_vdp1_arena_capacity++;
            profile->pipeline_faults++;
            continue;
        }
        const uint8_t *rgb = sm64_mario_material_rgb[primitive[0]];
        vdp1_cmdt_polygon_set(cmdt);
        vdp1_cmdt_draw_mode_set(cmdt, (vdp1_cmdt_draw_mode_t){
            .color_mode = VDP1_CMDT_CM_RGB_32768,
            .cc_mode = VDP1_CMDT_CC_REPLACE});
        vdp1_cmdt_color_set(cmdt, RGB1555(1, rgb[0], rgb[1], rgb[2]));
        vdp1_cmdt_vtx_set(cmdt, vertices);
        cmdt->cmd_link = (uint16_t)(ref->sort_key >> 16);
        profile->triangles_vdp1_emitted++;
        profile->triangles_emitted++;
        profile->demo_actor_primitives_emitted++;
    }
}

#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
/* True when this primitive belongs to a GEO_SWITCH_CASE branch the instance
 * has not selected this frame.
 *
 * The N64 walk descends into exactly one switch child; the Saturn bank bakes
 * every branch, so without this every case is drawn at once.  For a coin that
 * is all eight spin phases superimposed -- simultaneously the reason coins do
 * not appear to animate and eight times the VDP1 commands they need.
 *
 * `binding.flags` is zero for unconditional primitives, otherwise
 * (switch << 8) | (case + 1).  `switch_state` is captured by the observer
 * from geo_process_switch's selectedCase, so no new plumbing is involved.
 * A selector naming a switch the instance never reported is treated as
 * unselected: drawing an unresolvable branch is what the bug already did. */
static bool demo_generic_actor_switch_rejects(
    const sm64_saturn_actor_instance_snapshot_t *snapshot,
    const sm64_saturn_actor_render_binding_t *binding)
{
    if (snapshot == NULL || binding == NULL || binding->flags == 0U)
        return false;
    const uint16_t switch_ordinal = (uint16_t)(binding->flags >> 8);
    const uint16_t selected_case = (uint16_t)((binding->flags & 0xFFU) - 1U);
    if (switch_ordinal >= snapshot->switch_count ||
        switch_ordinal >= SM64_SATURN_ACTOR_MAX_SWITCHES)
        return true;
    return snapshot->switch_state[switch_ordinal] != selected_case;
}

static bool demo_generic_actor_emit(const demo_actor_emit_context_t *ctx,
                                    demo_actor_command_sink_t *sink,
                                    sm64_saturn_vdp1_backend_t *backend)
{
    uint16_t emitted = 0U, gouraud_ordinal = 0U;
    if (ctx == NULL || ctx->actor_runtime == NULL || sink == NULL ||
        (sink->commands == NULL && backend == NULL))
        return false;
    /* S1.12 step 2: accumulate into the uncached phase block, not the shared
     * frame profile. The master folds it in once emit returns. */
    demo_actor_emit_counters_t *const emit_counters =
        &demo_actor_phase_accum_uncached()->emit;
    if (ctx->instance_count == 0U) return true;
    for (uint16_t descriptor_index = 0U;
         descriptor_index < ctx->actor_runtime->queue.count;
         descriptor_index++) {
        const sm64_saturn_actor_instance_descriptor_t *const descriptor =
            sm64_saturn_actor_instance_queue_descriptor(
                &ctx->actor_runtime->queue,
                ctx->handoff_generation, descriptor_index);
        const sm64_saturn_actor_instance_result_t *const result =
            sm64_saturn_actor_instance_queue_result(
                &ctx->actor_runtime->queue,
                ctx->handoff_generation, descriptor_index);
        const sm64_saturn_actor_instance_snapshot_t *const snapshot =
            descriptor == NULL ? NULL :
            &ctx->snapshots[descriptor->snapshot_index];
        sm64_saturn_actor_bundle_resolution_t resolution;
        sm64_saturn_actor_texture_mapping_t mapping;
        vdp1_vram_partitions_t actor_texture_partitions;
        if (descriptor == NULL || result == NULL || snapshot == NULL ||
            result->reason != SM64_SATURN_ACTOR_QUARANTINE_NONE ||
            result->output_count > descriptor->output_capacity ||
            !sm64_saturn_source_scene_bundle_resolve(
                snapshot, 0U,
                &ctx->actor_runtime->outputs[descriptor->output_offset],
                descriptor->output_capacity, &resolution)) {
            /* resolve() only runs once descriptor/result/snapshot/reason/
             * capacity have already passed, and short-circuits before it on
             * any earlier failure, so no claim is ever left outstanding on
             * this path -- nothing to release. Skip this instance instead of
             * failing the whole frame over one bad bank/residency lookup.
             * Its records must still count toward `emitted` the same way a
             * rejected primitive does, so generic_actor_output_count (summed
             * in demo_generic_actor_prepare from this same result lookup)
             * still matches exactly. If result is NULL there is no count to
             * credit -- that is a queue-consistency fault, not a per-instance
             * lookup failure, and is left to fail closed at the final
             * `emitted != generic_actor_output_count` check below rather than
             * dereference a NULL result here. */
            emit_counters->instance_skipped++;
            if (result != NULL)
                emitted = (uint16_t)(emitted + result->output_count);
            continue;
        }
        sm64_saturn_actor_pose_view_t pose;
        bool valid = sm64_saturn_actor_pose_evaluate(
            &resolution.bank, snapshot->animation_id, snapshot->animation_frame,
            &resolution.workspace.pose_work, snapshot->scale_q16, &pose);
        const sm64_saturn_actor_texture_publication_t *const publication =
            sm64_saturn_source_scene_bundle_textures(
                resolution.residency_generation);
        valid = valid && pose.vertices != NULL &&
            pose.vertex_count == resolution.bank.bank.vertex_count &&
            publication != NULL &&
            sm64_saturn_source_scene_bundle_texture_partitions(
                resolution.residency_generation, &actor_texture_partitions) &&
            sm64_saturn_actor_texture_residency_lookup(
                publication, resolution.residency_generation,
                resolution.bank.bank.source_hash_words[0], &mapping);
        for (uint16_t local = 0U; valid && local < result->output_count;
             local++) {
                const sm64_saturn_actor_output_record_t *const record =
                    &ctx->actor_runtime->outputs[
                        descriptor->output_offset + local];
                sm64_saturn_actor_primitive_t primitive;
                sm64_saturn_actor_render_binding_t binding;
                sm64_saturn_actor_target_material_t material;
                sm64_saturn_actor_material_color_t color;
                sm64_saturn_projected_vertex_t projected[4];
                int32_t world_corners[4][3];
                int16_vec2_t vertices[4];
                vdp1_cmdt_t command = {0};
                uint16_t painter_bin =
                    (uint16_t)(record->sort_key >> 16);
                bool projected_visible = true;
                valid = sm64_saturn_actor_bank_primitive(
                        &resolution.bank, record->primitive_id,
                        &primitive) &&
                    sm64_saturn_actor_bank_render_binding(
                        &resolution.bank, record->primitive_id,
                        &binding) &&
                    primitive.material_id == binding.material_id &&
                    sm64_saturn_actor_bank_target_material(
                        &resolution.bank, binding.material_id, &material) &&
                    sm64_saturn_actor_bank_material_color(
                        &resolution.bank, binding.material_id, &color) &&
                    painter_bin < SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT;
                /* Skip before any projection work: an unselected switch
                 * branch costs neither vertex transforms nor a VDP1 command.
                 * The record still counts as emitted so the descriptor's
                 * output accounting stays exact. */
                emit_counters->prims_seen++;
                if (valid &&
                    demo_generic_actor_switch_rejects(snapshot, &binding)) {
                    emit_counters->reject_switch_case++;
                    emitted++;
                    continue;
                }
                for (uint16_t corner = 0U; valid && corner < 4U; corner++) {
                    sm64_saturn_vec3i_t world, view_position;
                    valid = demo_generic_actor_world_vertex(
                        snapshot, &resolution.workspace.pose_work,
                        primitive.vertex[corner], &world);
                    if (valid && !sm64_saturn_ir_transform_one(
                            &ctx->actor_job, world, &view_position,
                            &projected[corner])) {
                        projected_visible = false;
                        break;
                    }
                    if (!valid) break;
                    world_corners[corner][0] = world.x;
                    world_corners[corner][1] = world.y;
                    world_corners[corner][2] = world.z;
                    vertices[corner].x = projected[corner].x;
                    vertices[corner].y = projected[corner].y;
                }
                if (!valid) break;
                if (!projected_visible) {
                    emit_counters->reject_near_far++;
                    emitted++;
                    continue;
                }
                for (uint16_t i = 0; i < ctx->occluder_count; i++) {
                    const sm64_saturn_actor_occluder_t *surface = &ctx->occluders[i];
                    if (painter_bin <= surface->bin &&
                        sm64_saturn_actor_behind_terrain(surface, world_corners,
                            (const int16_t (*)[2])vertices))
                        painter_bin = surface->bin + 1U;
                }
                const int32_t cross =
                    (int32_t)(vertices[1].x - vertices[0].x) *
                        (vertices[2].y - vertices[0].y) -
                    (int32_t)(vertices[1].y - vertices[0].y) *
                        (vertices[2].x - vertices[0].x);
                /* Same predicate as the Mario cull, for the same reason, and
                 * corrected with it on 2026-09-10.
                 *
                 * These coordinates come from projected[corner] just above,
                 * i.e. sm64_saturn_ir_transform_one's output, which is
                 * Y-DOWN (saturn_ir_transform.c, screen_y = center_y -
                 * project(view.y)); and the cross below is the same
                 * a x b ordering the Mario site uses. Same space plus same
                 * ordering means the same predicate, and the fast3d frontend
                 * rejects G_CULL_BACK at cross >= 0
                 * (saturn_fast3d_frontend.c). The old `cross <= 0` kept each
                 * actor's FAR shell, which on a near-symmetric body like a
                 * goomba reads as merely "fine" -- the reason this sat
                 * unnoticed while Mario's identical bug was hunted.
                 *
                 * Note the two counters below are incremented together for
                 * one condition, so they cannot distinguish a back face from
                 * a zero-area primitive. Zero-area is rejected under either
                 * sign, so the totals stay usable; separating them is left
                 * alone here to keep this change to one predicate. */
                if (cross >= 0) {
                    emit_counters->reject_degenerate++;
                    emit_counters->reject_backface++;
                    emitted++;
                    continue;
                } else if (!sm64_saturn_actor_material_bind(
                               &command, &actor_texture_partitions,
                               &resolution.bank, record->primitive_id,
                               &mapping, resolution.residency_generation,
                               vertices)) {
                    valid = false;
                }
                const bool uses_gouraud =
                    material.recipe == SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD ||
                    material.recipe == SM64_SATURN_ACTOR_RECIPE_CLUT16_GOURAUD ||
                    material.recipe == SM64_SATURN_ACTOR_RECIPE_RGB1555_GOURAUD;
                /* A Gouraud table is four RGB1555 corners uploaded to
                 * VDP1 every frame.  It only earns that cost when the corners
                 * actually differ: an untextured primitive whose four vertices
                 * share one light intensity is a single flat colour, which
                 * VDP1 draws natively in replace mode for free.  The generic
                 * pose evaluator currently hands every vertex the same default
                 * intensity, so this covers the whole of an actor's flat
                 * geometry -- 56 of a Bob-omb's 68 primitives.  The test is on
                 * the measured corners rather than on that default, so real
                 * baked per-vertex lighting will simply stop taking it. */
                bool flat_replace = false;
                if (valid &&
                    material.recipe == SM64_SATURN_ACTOR_RECIPE_FLAT_GOURAUD) {
                    const uint8_t *const intensities =
                        resolution.workspace.pose_work.light_intensity;
                    const uint8_t first = intensities[primitive.vertex[0]];
                    flat_replace =
                        intensities[primitive.vertex[1]] == first &&
                        intensities[primitive.vertex[2]] == first &&
                        intensities[primitive.vertex[3]] == first;
                    if (flat_replace) {
                        const uint8_t r = (uint8_t)(
                            ((uint16_t)color.rgb555[0] * first + 127U) / 255U);
                        const uint8_t g = (uint8_t)(
                            ((uint16_t)color.rgb555[1] * first + 127U) / 255U);
                        const uint8_t b = (uint8_t)(
                            ((uint16_t)color.rgb555[2] * first + 127U) / 255U);
                        vdp1_cmdt_draw_mode_set(&command,
                            (vdp1_cmdt_draw_mode_t){
                                .color_mode = VDP1_CMDT_CM_RGB_32768,
                                .cc_mode = VDP1_CMDT_CC_REPLACE});
                        vdp1_cmdt_color_set(&command, RGB1555(1, r, g, b));
                        emit_counters->flat_replace++;
                    }
                }
                if (valid && uses_gouraud && !flat_replace) {
                    const uint32_t table_index =
                        (uint32_t)ctx->gouraud_first +
                        gouraud_ordinal;
                    /* S1.12 step 1: bound against this actor phase's OWN
                     * reservation rather than live-reading gouraud_bank->used,
                     * which terrain emit advances as it allocates its optional
                     * shading. The reservation is fixed before any emission
                     * begins, so the snapshot is exact -- and it is a strictly
                     * tighter bound, catching an ordinal that overruns the
                     * actor range instead of silently borrowing terrain's. */
                    if (table_index >= ctx->gouraud_limit ||
                        table_index >= ctx->gouraud_capacity) {
                        valid = false;
                        break;
                    }
                    sm64_saturn_gouraud_table_t *const table =
                        &ctx->gouraud_staging[table_index];
                    for (uint16_t corner = 0U; corner < 4U; corner++) {
                        const uint8_t intensity =
                            resolution.workspace.pose_work.light_intensity[
                                primitive.vertex[corner]];
                        const uint8_t r = (uint8_t)(
                            ((uint16_t)color.rgb555[0] * intensity + 127U) /
                            255U);
                        const uint8_t g = (uint8_t)(
                            ((uint16_t)color.rgb555[1] * intensity + 127U) /
                            255U);
                        const uint8_t b = (uint8_t)(
                            ((uint16_t)color.rgb555[2] * intensity + 127U) /
                            255U);
                        const rgb1555_t shade = RGB1555(1, r, g, b);
                        table->colors[corner] = shade.raw;
                    }
                    vdp1_cmdt_color_set(&command, (rgb1555_t){
                        .raw = sm64_saturn_gouraud_neutral_color()});
                    vdp1_cmdt_gouraud_base_set(
                        &command,
                        (vdp1_vram_t)(ctx->gouraud_vram_base +
                            table_index * sizeof(*table)));
                    gouraud_ordinal++;
                }
                if (valid) {
                    vdp1_cmdt_t *target;
                    if (sink->commands != NULL) {
                        if (sink->count >= sink->capacity) {
                            sink->overflowed = 1U;
                            valid = false;
                            break;
                        }
                        target = &sink->commands[sink->count++];
                    } else {
                        target = sm64_saturn_vdp1_backend_reserve(backend, 1U);
                    }
                    if (target == NULL) {
                        valid = false;
                        break;
                    }
                    emit_counters->prims_emitted++;
                    vdp1_cmdt_end_clear(&command);
                    command.cmd_link = painter_bin;
                    *target = command;
                    emit_counters->triangles_vdp1_emitted++;
                    emit_counters->triangles_emitted++;
                    emit_counters->actor_primitives_emitted++;
                    if (binding.tile_id != UINT16_MAX)
                        emit_counters->texture_commands++;
                }
                emitted++;
            }
        if (!sm64_saturn_source_scene_bundle_release(
                0U, resolution.residency_generation))
            valid = false;
        if (!valid) return false;
    }
    if (emitted != ctx->output_count) return false;
    return gouraud_ordinal <= ctx->gouraud_count;
}
#endif

/* S1.12 step 3 master half. Publishes the emit context, then either hands it
 * to the slave or runs it inline, exactly mirroring how the prepare phase is
 * dispatched. Inline is the default and is byte-for-byte the previous path. */
static bool demo_actor_emit_dispatch(demo_render_transaction_t *transaction,
                                     sm64_saturn_gouraud_bank_t *gouraud_bank)
{
    demo_actor_phase_accum_t *const accum = demo_actor_phase_accum_uncached();
    demo_actor_emit_context_t *const ctx =
        (demo_actor_emit_context_t *)&accum->emit_ctx;
    demo_actor_command_sink_t *const sink =
        (demo_actor_command_sink_t *)&accum->emit_sink;

    if (transaction->actor_handoff.state != SM64_SATURN_ACTOR_HANDOFF_BATCHED &&
        transaction->scene_snapshot->actor_instance_count != 0U)
        return false;
    memset((void *)ctx, 0, sizeof(*ctx));
    memset((void *)sink, 0, sizeof(*sink));
    ctx->actor_runtime = transaction->actor_runtime;
    ctx->snapshots = transaction->actor_handoff.snapshots;
    ctx->gouraud_staging = gouraud_bank->staging;
    ctx->gouraud_vram_base = gouraud_bank->vram_base;
    ctx->actor_job = transaction->actor_job;
    ctx->handoff_generation = transaction->actor_handoff.generation;
    ctx->instance_count = transaction->scene_snapshot->actor_instance_count;
    ctx->gouraud_first = transaction->generic_actor_gouraud_first;
    ctx->gouraud_limit = transaction->generic_actor_gouraud_limit;
    ctx->gouraud_capacity = gouraud_bank->capacity;
    ctx->gouraud_count = transaction->generic_actor_gouraud_count;
    ctx->output_count = transaction->generic_actor_output_count;
    /* Terrain results are complete before this dispatch. Publish only eight
     * large screen surfaces, then let the slave consume immutable P2 planes. */
    const int32_t camera_position[3] = {ctx->actor_job.camera.position.x,
        ctx->actor_job.camera.position.y, ctx->actor_job.camera.position.z};
    for (uint16_t i = 0; i < s_terrain_emit_count; i++) {
        const sm64_saturn_terrain_emit_ref_t *ref = &s_terrain_emit_refs[i];
        if (ref->record->primitive_id >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;
        const uint8_t *command = demo_terrain_final_command(&s_terrain_spans_shared, ref);
        if (command == NULL) continue;
        const sm64_saturn_bob_primitive_t *primitive =
            &s_bob_primitives_active[ref->record->primitive_id];
        int32_t world[4][3];
        for (unsigned corner = 0; corner < 4; corner++)
            memcpy(world[corner], s_bob_positions_active[primitive->indices[corner]],
                   sizeof(world[corner]));
        int16_t xy[4][2];
        memcpy(xy, command + 12U, sizeof(xy));
        sm64_saturn_actor_occluder_t candidate;
        if (!sm64_saturn_actor_occluder_init(&candidate, world, xy, camera_position,
                sm64_saturn_terrain_depth_bin(ref->record->painter_key))) continue;
        uint16_t slot = ctx->occluder_count;
        if (slot == 8U) {
            slot = 0U;
            for (uint16_t j = 1U; j < 8U; j++)
                if (ctx->occluders[j].area < ctx->occluders[slot].area) slot = j;
            if (candidate.area <= ctx->occluders[slot].area) continue;
        } else ctx->occluder_count++;
        ctx->occluders[slot] = candidate;
    }

    if (ctx->instance_count == 0U)
        transaction->generic_actor_gouraud_count = 0U;

    accum->emit_done = 0U;
    accum->emit_ok = 0U;
#if SATURN_FEATURE_ACTOR_EMIT_SLAVE && defined(__sh__)
    /* Fall back to inline emit rather than truncate: a frame whose actor
     * commands do not fit the staging array must still draw them all. */
    if (transaction->generic_actor_output_count <=
            DEMO_ACTOR_EMIT_STAGING_CAPACITY) {
        sink->commands = demo_actor_emit_staging_uncached();
        sink->capacity = (uint16_t)DEMO_ACTOR_EMIT_STAGING_CAPACITY;
        accum->emit_pending = 1U;
        demo_render_fence();
        sm64_saturn_render_job_runtime_dispatch_slave_task();
        return true;
    }
#endif
    accum->emit_pending = 0U;
    accum->emit_ok = demo_generic_actor_emit(ctx, sink, transaction->backend)
        ? 1U : 0U;
    demo_render_fence();
    accum->emit_done = 1U;
    return accum->emit_ok != 0U;
}

/* Master half of the join. When the slave staged commands, splice them into
 * the arena in producer order; when emit ran inline they are already there. */
static bool demo_actor_emit_join(demo_render_transaction_t *transaction,
                                 sm64_saturn_vdp1_backend_t *backend)
{
    demo_actor_phase_accum_t *const accum = demo_actor_phase_accum_uncached();
    demo_actor_command_sink_t *const sink =
        (demo_actor_command_sink_t *)&accum->emit_sink;
    (void)transaction;
#if SATURN_FEATURE_ACTOR_EMIT_SLAVE && defined(__sh__)
    {
        uint32_t spins = 0U;
        while (accum->emit_done == 0U) {
            if (++spins > DEMO_ACTOR_PHASE_WAIT_LIMIT) return false;
        }
        demo_render_fence();
    }
#endif
    if (accum->emit_done == 0U || accum->emit_ok == 0U) return false;
    if (sink->overflowed != 0U) return false;
    for (uint16_t index = 0U; index < sink->count; index++) {
        vdp1_cmdt_t *const target =
            sm64_saturn_vdp1_backend_reserve(backend, 1U);
        if (target == NULL) return false;
        *target = sink->commands[index];
    }
    return true;
}

static bool demo_render_prepare_publish(void *opaque, uint32_t generation)
{
    demo_render_transaction_t *transaction = opaque;
    if (transaction == NULL || generation == 0U || !s_bob_resident_ready)
        return false;
    sm64_saturn_vdp1_backend_t *const backend = transaction->backend;
    sm64_saturn_gouraud_bank_t *const gouraud_bank = transaction->gouraud_bank;
    sm64_saturn_fast3d_profile_t *const profile = transaction->profile;
    const sm64_saturn_mario_actor_snapshot_t *const snapshot =
        transaction->snapshot;
    const sm64_saturn_mario_actor_pose_t *const pose = transaction->pose;
    if (backend == NULL || gouraud_bank == NULL || profile == NULL ||
        snapshot == NULL || pose == NULL)
        return false;
    const sm64_saturn_ir_transform_job_t terrain_job = {
        .camera = demo_camera(snapshot),
        .focal_length = DEMO_FOCAL_LENGTH,
        .near_depth = SATURN_DEMO_NEAR_DEPTH,
        .center_x = DEMO_CENTER_X,
        .center_y = DEMO_CENTER_Y,
        .coord_min = DEMO_COORD_MIN,
        .coord_max = DEMO_COORD_MAX,
        .clip_near = SATURN_DEMO_NEAR_CLIP != 0
    };
    const sm64_saturn_ir_transform_job_t actor_job = {
        .camera = terrain_job.camera,
        .focal_length = terrain_job.focal_length,
        .near_depth = terrain_job.near_depth,
        .center_x = terrain_job.center_x,
        .center_y = terrain_job.center_y,
        .coord_min = terrain_job.coord_min,
        .coord_max = terrain_job.coord_max,
        .clip_near = false
    };
    transaction->actor_job = actor_job;
    /* Mario's walk is raised here, as early as the actor job allows, and is
     * joined only after the generic-actor join below. Both phases are raised
     * before the single wake-up the generic publish issues; the slave claims
     * both from it. Raising the phase without moving the raise this early
     * would buy nothing -- the master would simply wait where the walk used to
     * run, which is the lesson S1.11 recorded for the generic phase. */
    demo_actor_phase_accum_t *const mario_accum =
        demo_actor_phase_accum_uncached();
    const bool mario_publish_ok =
        demo_mario_phase_publish(mario_accum, snapshot, pose, &actor_job);
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
    /* S1.11: dispatch the generic-actor phase as early as the snapshot allows.
     * Everything between here and the join below -- spatial admission, work
     * order, the visible position set, Mario preparation -- is master work
     * that now overlaps it. Dispatching after that block instead would leave
     * the master with nothing to do but wait, which is why simply moving the
     * phase onto the slave without moving the dispatch buys nothing. */
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_ACTOR_CLOSURE);
    const bool actor_publish_ok =
        demo_generic_actor_publish(transaction, generation);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    if (!actor_publish_ok) return false;
#if DEMO_MARIO_PHASE_SLAVE
    /* The generic publish issues the wake-up that carries BOTH phases, but it
     * has one success path that issues none: a scene with zero actor instances
     * returns early with generic_actor_prepared set, having dispatched
     * nothing. Menu, intro and load frames take exactly that path. Mario is
     * already raised by then, so without this his join would spin out the full
     * wait limit on every one of those frames. */
    if (mario_publish_ok && transaction->generic_actor_prepared != 0U)
        sm64_saturn_render_job_runtime_dispatch_slave_task();
#endif
#elif DEMO_MARIO_PHASE_SLAVE
    /* No generic-actor phase in this build, so Mario's raise owns the wake-up. */
    if (mario_publish_ok) sm64_saturn_render_job_runtime_dispatch_slave_task();
#endif
    /* Mario keeps the selected build tier while terrain cluster admission
     * below derives each tier from the immutable camera view and its own
     * hysteretic state before any worker transforms positions. */
    s_pretransform_lod_tier = (uint8_t)SATURN_DEMO_POLY_TIER;
    const uint32_t transform_generation = generation;
    vdp1_vram_partitions_get(&transaction->partitions);
#if SATURN_DEMO_BSP_ORDER && !SATURN_DEMO_BSP_FRAGMENTS
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_SPATIAL_ADMIT);
    demo_spatial_admit(&terrain_job.camera, profile, transform_generation);
    demo_spatial_admit_join(profile);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
#endif
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_WORK_ORDER);
    demo_prepare_render_work_order(
        &terrain_job.camera, profile, transform_generation);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_POSITION_SET);
    const uint16_t required_positions = demo_build_visible_position_set(
        profile, transform_generation);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    transaction->transform_generation = transform_generation;
    transaction->required_positions = required_positions;
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_FRAME_RESET);
    memset(s_position_valid, 0, sizeof(s_position_valid));
    sm64_saturn_dual_frame_reset(&s_transform_frame_bank);
    s_transform_phase_failed = 0U;
    s_transform_publish_sequence = transform_generation;
    memset(s_primitive_lod_transition, 0,
           sizeof(s_primitive_lod_transition));
    memset(s_primitive_lod_suppressed, 0,
           sizeof(s_primitive_lod_suppressed));
    memset(s_primitive_lod_texture_downgraded, 0,
           sizeof(s_primitive_lod_texture_downgraded));
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    demo_classify_context_t classify = {
        .primitives = s_bob_primitives_active,
        .work_order = s_render_work_order,
        .camera = &terrain_job.camera,
        .job = &terrain_job,
        .work_count = s_render_work_count,
        .required_positions = required_positions,
        .transform_sequence = s_transform_publish_sequence,
        .dual_phase = false,
    };

    s_terrain_publish_sequence =
        sm64_saturn_render_generation_next(s_terrain_publish_sequence);
    uint16_t actor_vertex_count = 0U;
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
    /* S1.11: the phase was raised before spatial admission began, so by the
     * time control reaches here the slave has been working on it for the whole
     * of that window. Joining is where the overlap is cashed in. */
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_ACTOR_CLOSURE);
    if (!demo_generic_actor_join(transaction, generation)) {
        SM64_SATURN_PRENOTIFY_PROFILE_POP();
        return false;
    }
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
#endif
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_PREPARE_MARIO);
    const bool actor_prepare_ok = mario_publish_ok &&
        demo_mario_phase_join(mario_accum, profile, &actor_vertex_count);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    if (actor_prepare_ok)
        s_actor_publish_sequence =
            sm64_saturn_render_generation_next(s_actor_publish_sequence);
    bool queue_ok = s_render_job_runtime_active != 0U && actor_prepare_ok;
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(SM64_SATURN_PRENOTIFY_PROFILE_NODE_MARIO_CTX);
    if (queue_ok && actor_vertex_count != 0U) {
        queue_ok = demo_snapshot_mario_transform_context(
            &s_mario_transform_context, &actor_job, snapshot, pose);
        if (queue_ok)
            s_mario_transform_context.sequence = s_actor_publish_sequence;
    }
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(SM64_SATURN_PRENOTIFY_PROFILE_NODE_QUEUE_RESET);
    queue_ok = queue_ok && demo_render_queue_reset_frame_banks();
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    /* A fully culled/offscreen actor is an ordinary scene result. Keep the
     * terrain chain contiguous so that generation can publish without
     * manufacturing a zero-length actor descriptor (which the queue rejects
     * by contract). Visible actors append their own admit/lower chain. */
    const sm64_saturn_render_job_t frame_jobs[] = {
        {
            .type = SM64_SATURN_RENDER_JOB_WORLD_ADMIT,
            .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_ADMIT,
            .snapshot_generation = transform_generation,
            .input_count = s_render_work_count,
            .output_capacity = DEMO_TERRAIN_RESULT_CAPACITY,
        }, {
            .type = SM64_SATURN_RENDER_JOB_WORLD_LOWER,
            .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_WORLD_LOWER,
            .snapshot_generation = transform_generation,
            .input_count = s_render_work_count,
            .output_capacity = DEMO_TERRAIN_RESULT_CAPACITY,
        }, {
            .type = SM64_SATURN_RENDER_JOB_ACTOR_ADMIT,
            .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_ADMIT,
            .snapshot_generation = transform_generation,
            .input_count = actor_vertex_count,
            .output_capacity = actor_vertex_count,
        }, {
            .type = SM64_SATURN_RENDER_JOB_ACTOR_LOWER,
            .callback_id = SM64_SATURN_RENDER_JOB_CALLBACK_ACTOR_LOWER,
            .snapshot_generation = transform_generation,
            .input_count = SM64_MARIO_PRIMITIVE_COUNT,
            .output_capacity = SM64_MARIO_PRIMITIVE_COUNT,
        },
    };
    const uint8_t frame_dependencies[] = {
        0U, (uint8_t)(1U << 0U), 0U, (uint8_t)(1U << 2U),
    };
    const uint16_t frame_job_count = actor_vertex_count != 0U ? 4U : 2U;
    transaction->actor_vertex_count = actor_vertex_count;
    transaction->frame_job_count = frame_job_count;
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_GRAPH_PUBLISH);
    queue_ok = queue_ok && sm64_saturn_render_job_graph_publish(
        &s_render_job_graph, transform_generation, frame_jobs,
        frame_dependencies, frame_job_count);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(
        SM64_SATURN_PRENOTIFY_PROFILE_NODE_QUEUE_CONTEXTS);
    queue_ok = queue_ok &&
        demo_render_queue_prepare_contexts(&classify,
                                           s_terrain_publish_sequence);
    SM64_SATURN_PRENOTIFY_PROFILE_POP();
    if (!queue_ok) {
        /* The lifecycle controller owns the one failure transition. It calls
         * demo_render_quarantine() after this returns, including when graph
         * publication left READY descriptors behind before the first notify. */
        return false;
    }

    profile->master_worker_started++;
    profile->slave_worker_started++;
    /* Left open deliberately: the matching charge happens inside
     * SM64_SATURN_PRENOTIFY_PROFILE_END(), which the NOTIFIED marker observer
     * calls.  Everything between here and the marker -- the lifecycle
     * controller's own notify call and the runtime's publish path -- is
     * therefore attributed to NOTIFY rather than to the window residue. */
    SM64_SATURN_PRENOTIFY_PROFILE_PUSH(SM64_SATURN_PRENOTIFY_PROFILE_NODE_NOTIFY);
    return true;
}

static void demo_render_notify(void *opaque)
{
    (void)opaque;
    sm64_saturn_render_job_runtime_notify();
}

static bool demo_render_slave_retired(void *opaque)
{
    (void)opaque;
    return sm64_saturn_render_job_runtime_slave_retired();
}

static uint16_t demo_render_drain_master(void *opaque)
{
    (void)opaque;
    return sm64_saturn_render_job_runtime_drain_master();
}

static bool demo_render_finalize(void *opaque, uint32_t generation,
                                 uint16_t master_jobs)
{
    demo_render_transaction_t *transaction = opaque;
    if (transaction == NULL || generation == 0U ||
        transaction->transform_generation != generation)
        return false;
    sm64_saturn_vdp1_backend_t *const backend = transaction->backend;
    sm64_saturn_gouraud_bank_t *const gouraud_bank = transaction->gouraud_bank;
    sm64_saturn_fast3d_profile_t *const profile = transaction->profile;
    const sm64_saturn_mario_actor_snapshot_t *const snapshot =
        transaction->snapshot;
    const sm64_saturn_mario_actor_pose_t *const pose = transaction->pose;
    const uint32_t transform_generation = transaction->transform_generation;
    const uint16_t required_positions = transaction->required_positions;
    const uint16_t actor_vertex_count = transaction->actor_vertex_count;
    const uint16_t frame_job_count = transaction->frame_job_count;
    const vdp1_vram_partitions_t *const partitions = &transaction->partitions;

#if SATURN_DIAGNOSTIC_MODE
    /* The normal BOB profile has no dashboard consumer for these counters. */
    sm64_saturn_render_job_runtime_telemetry_t queue_telemetry;
    sm64_saturn_render_job_runtime_refresh_terminal_telemetry();
    const bool telemetry_ok =
        sm64_saturn_render_job_runtime_telemetry_snapshot(&queue_telemetry);
    if (telemetry_ok) {
        profile->render_job_master_world_admit_claims =
            queue_telemetry.master_claims[0];
        profile->render_job_master_world_lower_claims =
            queue_telemetry.master_claims[1];
        profile->render_job_master_actor_admit_claims =
            queue_telemetry.master_claims[2];
        profile->render_job_master_actor_lower_claims =
            queue_telemetry.master_claims[3];
        profile->render_job_slave_world_admit_claims =
            queue_telemetry.slave_claims[0];
        profile->render_job_slave_world_lower_claims =
            queue_telemetry.slave_claims[1];
        profile->render_job_slave_actor_admit_claims =
            queue_telemetry.slave_claims[2];
        profile->render_job_slave_actor_lower_claims =
            queue_telemetry.slave_claims[3];
        profile->render_job_notified_generation =
            queue_telemetry.notified_generation;
        profile->render_job_retired_generation =
            queue_telemetry.retired_generation;
        profile->render_job_master_wait_iterations =
            queue_telemetry.master_wait_iterations;
        profile->render_job_failures = queue_telemetry.master_failures +
            queue_telemetry.slave_failures;
        profile->render_job_quarantined = queue_telemetry.quarantined;
    }
#endif
    const bool terminal = sm64_saturn_render_job_queue_all_terminal(
        &s_render_job_queue, transform_generation);
    bool queue_ok = terminal &&
        demo_terrain_queue_assemble_merge_spans(
            SM64_SATURN_RENDER_OUTPUT_LANE_MASTER,
            &s_terrain_queue_merge_spans) &&
        (actor_vertex_count == 0U ||
         demo_actor_queue_assemble_done(
             SM64_SATURN_RENDER_OUTPUT_LANE_MASTER,
             &s_mario_transform_context));
    uint32_t terrain_results_by_lane[2] = {0U, 0U};
    if (queue_ok) {
        for (uint16_t job_index = 0U;
             job_index < s_render_job_graph.count; job_index++) {
            const demo_terrain_queue_metadata_t *const metadata =
                &s_terrain_result_metadata[job_index];
            if (metadata->ready != 0U && metadata->writer_lane < 2U)
                terrain_results_by_lane[metadata->writer_lane] +=
                    metadata->record_count;
        }
    }
    const bool retired = terminal &&
        sm64_saturn_render_job_queue_reset_retired(
            &s_render_job_queue, transform_generation);
    if (!queue_ok || !retired) {
        /* No serial replay: backend_begin() has not run, so the previously
         * complete VDP1 frame remains the only presentable command list. */
        profile->pipeline_faults++;
        return false;
    }
    profile->slave_jobs_completed += (uint32_t)(
        frame_job_count - (master_jobs > frame_job_count
            ? frame_job_count : master_jobs));
    profile->triangles_transformed += required_positions;
    profile->demo_positions_transformed += required_positions;
    profile->demo_bob_results_master += terrain_results_by_lane[0];
    profile->demo_bob_results_slave += terrain_results_by_lane[1];
    profile->demo_bob_terrain_descriptor_bytes_read +=
        (uint32_t)s_terrain_emit_count *
        (uint32_t)sizeof(sm64_saturn_visible_terrain_t);
    const uint16_t actor_command_count = actor_vertex_count != 0U
        ? demo_finalize_mario_draws() : 0U;
    uint32_t essential_actor_commands = actor_command_count;
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
    essential_actor_commands += transaction->generic_actor_output_count;
#endif
    if (essential_actor_commands > UINT16_MAX) return false;
    sm64_saturn_gouraud_bank_begin(gouraud_bank);
    /* Essential actor shading is reserved before optional world shading.
     * Previously terrain consumed the Gouraud bank first, which made Mario
     * flat even on frames where his command batch happened to fit. */
    if (actor_vertex_count != 0U)
        demo_reserve_mario_gouraud(gouraud_bank, profile);
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
    /* S1.12 step 1: sm64_saturn_gouraud_table_t is 8 bytes, so TWO entries
     * share every 16-byte SH-2 cache line. The actor reservation sits directly
     * against terrain's subsequent allocations, so without this the boundary
     * pair straddles one line -- and once the two phases run on different CPUs,
     * with no coherency, each write-back would discard the other's half. Start
     * the actor range on an even index and round its length up to even so both
     * edges land on a line boundary. Costs at most two 8-byte entries. */
    {
        sm64_saturn_actor_gouraud_reservation_t reservation;
        if (!sm64_saturn_actor_gouraud_reserve(
                gouraud_bank->used, gouraud_bank->capacity,
                transaction->generic_actor_gouraud_count, &reservation))
            return false;
        transaction->generic_actor_gouraud_first = reservation.first;
        transaction->generic_actor_gouraud_limit = reservation.limit;
        gouraud_bank->used = reservation.limit;
    }
#endif
    sm64_saturn_vdp1_backend_begin(backend);
    /* Preserve Mario's all-or-nothing textured tail batch, then retain the
     * nearest terrain results if the command arena is oversubscribed.
     * Z-Treme traverses near-to-far specifically so buffer exhaustion keeps
     * nearby geometry (ZT_RENDERING.c:494-503); our painter stream must still
     * be emitted far-to-near, so select its near tail before lowering it. */
    const uint16_t terrain_command_budget =
        sm64_saturn_command_arena_budget_before_tail(
            &backend->commands, (uint16_t)essential_actor_commands);
    const uint16_t terrain_first =
        s_terrain_emit_count > terrain_command_budget
        ? (uint16_t)(s_terrain_emit_count - terrain_command_budget) : 0U;
    profile->reject_vdp1_arena_capacity += terrain_first;
    profile->pipeline_faults += terrain_first;
    /* S1.12 step 3: publish exactly what emit needs and hand it to the slave
     * before the master starts its own terrain lowering, so the two overlap.
     * This is the earliest the dispatch can be raised: gouraud_first is an
     * input and is only fixed by the reservation just above. */
    if (!demo_actor_emit_dispatch(transaction, gouraud_bank)) return false;
    /* The master owns terrain and Mario lowering. Compact terrain results are
     * merged above, then consumed in the stable baked painter order here. */
    for (uint16_t ordinal = terrain_first;
         ordinal < s_terrain_emit_count; ordinal++) {
        const sm64_saturn_terrain_result_t *result =
            s_terrain_emit_refs[ordinal].record;
        if (result->primitive_id >= SM64_SATURN_BOB_PRIMITIVE_COUNT) continue;
        demo_emit_terrain_result(
            result, demo_terrain_final_command(
                        &s_terrain_spans_shared, &s_terrain_emit_refs[ordinal]),
            &s_bob_primitives_active[result->primitive_id], backend,
            gouraud_bank, profile, partitions);
    }
    /* Mario remains master-owned and consumes the live bridge pose, textured
     * material bindings, and per-vertex Gouraud data after terrain compaction.
     * The 68000 stays out of this path; as in Z-Treme and SlaveDriver it is
     * reserved for SCSP/audio service rather than geometry dispatch. */
    if (actor_vertex_count != 0U)
        demo_emit_mario(snapshot, pose, backend, partitions, profile);
#if SATURN_FEATURE_DYNAMIC_ACTOR_CLOSURE
    /* S1.12 step 3: join the actor emit dispatched before the terrain loop and
     * splice its commands into the arena.
     *
     * Splicing through backend_reserve rather than a raw memcpy keeps every
     * arena invariant (cursor, live_count, list.count) maintained by the code
     * that owns them; link_depth_bins validates all three and would fail
     * closed on a mismatch. Appending here -- after terrain and Mario -- is
     * what preserves painter order: link_depth_bins sorts stably by arena
     * index within a bin, so actor commands keep the highest indices exactly
     * as they did when emit ran inline. */
    if (!demo_actor_emit_join(transaction, backend)) return false;
    /* S1.12 step 2: emit is single-owner and complete here, so folding its
     * uncached block into the frame profile is a sequenced handoff. Totals are
     * identical to the previous direct accumulation. */
    {
        const demo_actor_emit_counters_t *const ec =
            &demo_actor_phase_accum_uncached()->emit;
        profile->actor_instance_skipped += ec->instance_skipped;
        profile->actor_prims_seen += ec->prims_seen;
        profile->reject_switch_case += ec->reject_switch_case;
        profile->reject_near_far += ec->reject_near_far;
        profile->reject_degenerate += ec->reject_degenerate;
        profile->actor_reject_backface += ec->reject_backface;
        profile->actor_flat_replace += ec->flat_replace;
        profile->actor_prims_emitted += ec->prims_emitted;
        profile->triangles_vdp1_emitted += ec->triangles_vdp1_emitted;
        profile->triangles_emitted += ec->triangles_emitted;
        profile->demo_actor_primitives_emitted += ec->actor_primitives_emitted;
        profile->texture_commands += ec->texture_commands;
    }
    if (transaction->scene_snapshot->actor_instance_count != 0U &&
        (!sm64_saturn_actor_runtime_handoff_acknowledge_consumed(
             &transaction->actor_handoff) ||
         !sm64_saturn_actor_runtime_handoff_retire(
             &transaction->actor_handoff)))
        return false;
#endif
    sm64_saturn_vdp1_backend_finish(backend);
    if (!sm64_saturn_vdp1_backend_link_depth_bins(
            backend, SM64_SATURN_TERRAIN_DEPTH_BIN_COUNT))
        return false;
    /* Published profile diagnostics: never read to choose an allocation,
     * scheduling, LOD, or promotion decision. */
    profile->gouraud_tables_saved += gouraud_bank->saved_tables;
    profile->gouraud_bytes_saved += gouraud_bank->saved_bytes;
    profile->frame_serial++;
    return true;
}

static void demo_render_quarantine(void *opaque, uint32_t generation)
{
    demo_render_transaction_t *transaction = opaque;
    if (transaction == NULL || generation == 0U) return;
    if (sm64_saturn_render_job_queue_generation(&s_render_job_queue) ==
            generation) {
        for (uint16_t job_index = 0U;
             job_index < s_render_job_graph.count; job_index++)
            (void)sm64_saturn_render_job_queue_quarantine_ready(
                &s_render_job_queue, generation, job_index);
        if (sm64_saturn_render_job_queue_all_terminal(
                &s_render_job_queue, generation))
            (void)sm64_saturn_render_job_queue_reset_retired(
                &s_render_job_queue, generation);
    }
    if (transaction->profile != NULL)
        transaction->profile->pipeline_faults++;
}

static const sm64_saturn_render_lifecycle_ops_t s_demo_render_lifecycle_ops = {
    .prepare_publish = demo_render_prepare_publish,
    .notify = demo_render_notify,
    .slave_retired = demo_render_slave_retired,
    .drain_master = demo_render_drain_master,
    .finalize = demo_render_finalize,
    .quarantine = demo_render_quarantine,
};

bool sm64_saturn_demo_render_observe_lifecycle(
    sm64_saturn_render_lifecycle_observer_t observer, void *context)
{
    return sm64_saturn_render_lifecycle_observe(
        &s_demo_render_transaction.lifecycle, observer, context);
}

bool sm64_saturn_demo_render_start_frame(
    sm64_saturn_vdp1_backend_t *backend,
    sm64_saturn_gouraud_bank_t *gouraud_bank,
    sm64_saturn_fast3d_profile_t *profile,
    const sm64_saturn_mario_actor_snapshot_t *snapshot,
    const sm64_saturn_mario_actor_pose_t *pose,
    const sm64_saturn_render_snapshot_t *scene_snapshot,
    sm64_saturn_actor_runtime_storage_t *actor_runtime,
    uint32_t generation)
{
    if (backend == NULL || gouraud_bank == NULL || profile == NULL ||
        snapshot == NULL || pose == NULL || scene_snapshot == NULL ||
        actor_runtime == NULL || generation == 0U ||
        s_demo_render_transaction.lifecycle.active)
        return false;
    if (!sm64_saturn_lod_lifetime_begin(&s_lod_lifetime, generation))
        return false;
    s_demo_render_transaction.backend = backend;
    s_demo_render_transaction.gouraud_bank = gouraud_bank;
    s_demo_render_transaction.profile = profile;
    s_demo_render_transaction.snapshot = snapshot;
    s_demo_render_transaction.pose = pose;
    s_demo_render_transaction.scene_snapshot = scene_snapshot;
    s_demo_render_transaction.actor_runtime = actor_runtime;
    if (!sm64_saturn_render_lifecycle_start(
            &s_demo_render_transaction.lifecycle,
            &s_demo_render_lifecycle_ops, &s_demo_render_transaction,
            generation)) {
        (void)sm64_saturn_lod_lifetime_finish(
            &s_lod_lifetime, generation);
        s_demo_render_transaction.backend = NULL;
        s_demo_render_transaction.gouraud_bank = NULL;
        s_demo_render_transaction.profile = NULL;
        s_demo_render_transaction.snapshot = NULL;
        s_demo_render_transaction.pose = NULL;
        s_demo_render_transaction.scene_snapshot = NULL;
        s_demo_render_transaction.actor_runtime = NULL;
        return false;
    }
    return true;
}

sm64_saturn_demo_render_status_t sm64_saturn_demo_render_poll_frame(
    sm64_saturn_fast3d_profile_t *profile, uint32_t generation)
{
    if (profile == NULL || profile != s_demo_render_transaction.profile ||
        generation == 0U || !s_demo_render_transaction.lifecycle.active ||
        generation != s_demo_render_transaction.lifecycle.active_generation)
        return SM64_SATURN_DEMO_RENDER_FAILED;
    const sm64_saturn_render_lifecycle_status_t status =
        sm64_saturn_render_lifecycle_poll(
            &s_demo_render_transaction.lifecycle,
            &s_demo_render_lifecycle_ops, &s_demo_render_transaction,
            generation);
    if (status == SM64_SATURN_RENDER_LIFECYCLE_PENDING)
        return SM64_SATURN_DEMO_RENDER_PENDING;
    if (!sm64_saturn_lod_lifetime_finish(&s_lod_lifetime, generation))
        return SM64_SATURN_DEMO_RENDER_FAILED;
    s_demo_render_transaction.backend = NULL;
    s_demo_render_transaction.gouraud_bank = NULL;
    s_demo_render_transaction.profile = NULL;
    s_demo_render_transaction.snapshot = NULL;
    s_demo_render_transaction.pose = NULL;
    s_demo_render_transaction.scene_snapshot = NULL;
    s_demo_render_transaction.actor_runtime = NULL;
    return status == SM64_SATURN_RENDER_LIFECYCLE_COMPLETE
        ? SM64_SATURN_DEMO_RENDER_COMPLETE
        : SM64_SATURN_DEMO_RENDER_FAILED;
}
